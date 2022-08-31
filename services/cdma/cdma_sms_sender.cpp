/*
 * Copyright (C) 2021 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "cdma_sms_sender.h"

#include "cdma_sms_message.h"
#include "core_manager_inner.h"
#include "radio_event.h"
#include "securec.h"
#include "string_utils.h"
#include "telephony_log_wrapper.h"

namespace OHOS {
namespace Telephony {
using namespace std;
CdmaSmsSender::CdmaSmsSender(const shared_ptr<AppExecFwk::EventRunner> &runner, int32_t slotId,
    function<void(shared_ptr<SmsSendIndexer>)> sendRetryFun)
    : SmsSender(runner, slotId, sendRetryFun)
{}

CdmaSmsSender::~CdmaSmsSender() {}

void CdmaSmsSender::TextBasedSmsDelivery(const string &desAddr, const string &scAddr, const string &text,
    const sptr<ISendShortMessageCallback> &sendCallback,
    const sptr<IDeliveryShortMessageCallback> &deliveryCallback)
{
    if (isImsNetDomain_ && imsSmsCfg_) {
        TextBasedSmsDeliveryViaIms(desAddr, scAddr, text, sendCallback, deliveryCallback);
        return;
    }
    CdmaSmsMessage message;
    SmsCodingScheme codingType;
    std::vector<struct SplitInfo> splits;
    message.SplitMessage(splits, text, CheckForce7BitEncodeType(), codingType);
    if (splits.size() > MAX_SEGMENT_NUM) {
        SendResultCallBack(sendCallback, ISendShortMessageCallback::SEND_SMS_FAILURE_UNKNOWN);
        TELEPHONY_LOGE("message exceed the limit.");
        return;
    }
    std::unique_ptr<SmsTransMsg> transMsg = nullptr;
    bool bStatusReport = (deliveryCallback == nullptr) ? false : true;
    transMsg = message.CreateSubmitTransMsg(desAddr, scAddr, text, bStatusReport, codingType);
    if (transMsg == nullptr) {
        SendResultCallBack(sendCallback, ISendShortMessageCallback::SEND_SMS_FAILURE_UNKNOWN);
        TELEPHONY_LOGE("CreateSubmitTransMsg nullptr fail.");
        return;
    }
    /* 1. Set Reply sequence number. */
    uint8_t msgRef8bit = GetSeqNum();
    transMsg->data.p2pMsg.transReplySeq = msgRef8bit;
    /* 2. Set msg ID. */
    uint16_t msgId = GetSubmitMsgId();
    transMsg->data.p2pMsg.telesvcMsg.data.submit.msgId.msgId = msgId;
    shared_ptr<uint8_t> unSentCellCount = make_shared<uint8_t>(splits.size());
    shared_ptr<bool> hasCellFailed = make_shared<bool>(false);
    if (unSentCellCount == nullptr || hasCellFailed == nullptr) {
        SendResultCallBack(sendCallback, ISendShortMessageCallback::SEND_SMS_FAILURE_UNKNOWN);
        return;
    }
    chrono::system_clock::duration timePoint = chrono::system_clock::now().time_since_epoch();
    long timeStamp = chrono::duration_cast<chrono::seconds>(timePoint).count();

    for (std::size_t i = 0; i < splits.size(); i++) {
        (void)memset_s(transMsg->data.p2pMsg.telesvcMsg.data.submit.userData.userData.data,
            sizeof(transMsg->data.p2pMsg.telesvcMsg.data.submit.userData.userData.data), 0x00,
            sizeof(transMsg->data.p2pMsg.telesvcMsg.data.submit.userData.userData.data));
        if (memcpy_s(transMsg->data.p2pMsg.telesvcMsg.data.submit.userData.userData.data,
            sizeof(transMsg->data.p2pMsg.telesvcMsg.data.submit.userData.userData.data),
            splits[i].encodeData.data(), splits[i].encodeData.size()) != EOK) {
            SendResultCallBack(sendCallback, ISendShortMessageCallback::SEND_SMS_FAILURE_UNKNOWN);
            return;
        }
        std::string segmentText;
        segmentText.append((char *)(splits[i].encodeData.data()), splits[i].encodeData.size());
        std::shared_ptr<SmsSendIndexer> indexer = nullptr;
        indexer = make_shared<SmsSendIndexer>(desAddr, scAddr, segmentText, sendCallback, deliveryCallback);
        if (indexer == nullptr) {
            SendResultCallBack(sendCallback, ISendShortMessageCallback::SEND_SMS_FAILURE_UNKNOWN);
            return;
        }
        indexer->SetDcs(splits[i].encodeType);
        SetPduSeqInfo(indexer, splits.size(), transMsg, i, msgRef8bit);
        transMsg->data.p2pMsg.telesvcMsg.data.submit.userData.userData.length =
            static_cast<int>(splits[i].encodeData.size());
        /* encode msg data */
        uint8_t pduStr[TAPI_NETTEXT_SMDATA_SIZE_MAX + 1] = { 0 };
        int len = CdmaSmsPduCodec::EncodeMsg(*transMsg.get(), pduStr);
        std::vector<uint8_t> pdu(pduStr, pduStr + len);
        indexer->SetEncodePdu(std::move(pdu));
        indexer->SetMsgRefId(msgRef8bit);
        indexer->SetNetWorkType(NET_TYPE_CDMA);
        indexer->SetUnSentCellCount(unSentCellCount);
        indexer->SetHasCellFailed(hasCellFailed);
        indexer->SetTimeStamp(timeStamp);
        indexer->SetMsgId(msgId);
        SendSmsToRil(indexer);
    }
}

void CdmaSmsSender::TextBasedSmsDeliveryViaIms(const string &desAddr, const string &scAddr, const string &text,
    const sptr<ISendShortMessageCallback> &sendCallback, const sptr<IDeliveryShortMessageCallback> &deliveryCallback)
{
    bool isStatusReport = false;
    SmsCodingScheme codingType;
    GsmSmsMessage gsmSmsMessage;
    std::vector<struct SplitInfo> cellsInfos;
    gsmSmsMessage.SplitMessage(cellsInfos, text, CheckForce7BitEncodeType(), codingType);
    isStatusReport = (deliveryCallback == nullptr) ? false : true;
    std::shared_ptr<struct SmsTpdu> tpdu =
        gsmSmsMessage.CreateDefaultSubmitSmsTpdu(desAddr, scAddr, text, isStatusReport, codingType);
    int cellsInfosSize = static_cast<int>(cellsInfos.size());
    shared_ptr<uint8_t> unSentCellCount = make_shared<uint8_t>(cellsInfosSize);
    shared_ptr<bool> hasCellFailed = make_shared<bool>(false);
    if (TpduNullOrSmsPageOverNormalOrSmsEncodeFail(cellsInfos, tpdu, unSentCellCount, hasCellFailed, sendCallback)) {
        return;
    }

    std::unique_lock<std::mutex> lock(mutex_);
    uint8_t msgRef8bit = GetMsgRef8Bit();
    for (int i = 0; i < cellsInfosSize; i++) {
        SendSmsForEveryIndexer(i, cellsInfos, desAddr, scAddr, tpdu, gsmSmsMessage, unSentCellCount, hasCellFailed,
            codingType, msgRef8bit, sendCallback, deliveryCallback);
    }
    return;
}

void CdmaSmsSender::SendSmsForEveryIndexer(int &i, std::vector<struct SplitInfo> cellsInfos, const string &desAddr,
    const string &scAddr, std::shared_ptr<struct SmsTpdu> tpdu, GsmSmsMessage gsmSmsMessage,
    shared_ptr<uint8_t> unSentCellCount, shared_ptr<bool> hasCellFailed, SmsCodingScheme codingType, uint8_t msgRef8bit,
    const sptr<ISendShortMessageCallback> &sendCallback, const sptr<IDeliveryShortMessageCallback> &deliveryCallback)
{
    std::string segmentText;
    segmentText.append((char *)(cellsInfos[i].encodeData.data()), cellsInfos[i].encodeData.size());
    std::shared_ptr<SmsSendIndexer> indexer =
        make_shared<SmsSendIndexer>(desAddr, scAddr, segmentText, sendCallback, deliveryCallback);
    if (indexer == nullptr) {
        SendResultCallBack(sendCallback, ISendShortMessageCallback::SEND_SMS_FAILURE_UNKNOWN);
        return;
    }
    indexer->SetDcs(cellsInfos[i].encodeType);
    (void)memset_s(tpdu->data.submit.userData.data, MAX_USER_DATA_LEN + 1, 0x00, MAX_USER_DATA_LEN + 1);

    if (memcpy_s(tpdu->data.submit.userData.data, MAX_USER_DATA_LEN + 1, &cellsInfos[i].encodeData[0],
        cellsInfos[i].encodeData.size()) != EOK) {
        SendResultCallBack(indexer, ISendShortMessageCallback::SEND_SMS_FAILURE_UNKNOWN);
        return;
    }

    tpdu->data.submit.userData.length = cellsInfos[i].encodeData.size();
    tpdu->data.submit.userData.data[cellsInfos[i].encodeData.size()] = 0;
    tpdu->data.submit.msgRef = msgRef8bit;
    int headerCnt = 0;
    bool isMore = false;
    bool isStatusReport = false;
    isStatusReport = (deliveryCallback == nullptr) ? false : true;

    int cellsInfosSize = static_cast<int>(cellsInfos.size());
    if (cellsInfosSize > 1) {
        indexer->SetIsConcat(true);
        SmsConcat concat;
        concat.is8Bits = true;
        concat.msgRef = msgRef8bit;
        concat.totalSeg = static_cast<uint16_t>(cellsInfosSize);
        concat.seqNum = static_cast<uint16_t>(i + 1);
        indexer->SetSmsConcat(concat);
        headerCnt += gsmSmsMessage.SetHeaderConcat(headerCnt, concat);
    }

    /* Set User Data Header for Alternate Reply Address */
    headerCnt += gsmSmsMessage.SetHeaderReply(headerCnt);
    /* Set User Data Header for National Language Single Shift */
    headerCnt += gsmSmsMessage.SetHeaderLang(headerCnt, codingType, cellsInfos[i].langId);
    indexer->SetLangId(cellsInfos[i].langId);
    tpdu->data.submit.userData.headerCnt = headerCnt;
    tpdu->data.submit.bHeaderInd = (headerCnt > 0) ? true : false;

    if (cellsInfosSize > 1 && i < (cellsInfosSize - 1)) {
        tpdu->data.submit.bStatusReport = false;
        isMore = true;
    } else {
        tpdu->data.submit.bStatusReport = isStatusReport;
    }
    ReadySendSms(gsmSmsMessage, scAddr, isMore, indexer, msgRef8bit, unSentCellCount, hasCellFailed);
}

void CdmaSmsSender::ReadySendSms(GsmSmsMessage gsmSmsMessage, const string &scAddr, bool isMore,
    std::shared_ptr<SmsSendIndexer> indexer, uint8_t msgRef8bit, shared_ptr<uint8_t> unSentCellCount,
    shared_ptr<bool> hasCellFailed)
{
    std::shared_ptr<struct EncodeInfo> encodeInfo = gsmSmsMessage.GetSubmitEncodeInfo(scAddr, isMore);
    if (encodeInfo == nullptr) {
        SendResultCallBack(indexer, ISendShortMessageCallback::SEND_SMS_FAILURE_UNKNOWN);
        TELEPHONY_LOGE("create encodeInfo encodeInfo nullptr error.");
        return;
    }

    SetSendIndexerInfo(indexer, encodeInfo, msgRef8bit);
    indexer->SetUnSentCellCount(unSentCellCount);
    indexer->SetHasCellFailed(hasCellFailed);
    indexer->SetImsSmsForCdma(true);
    SendSmsToRil(indexer);
}

bool CdmaSmsSender::TpduNullOrSmsPageOverNormalOrSmsEncodeFail(std::vector<struct SplitInfo> cellsInfos,
    std::shared_ptr<struct SmsTpdu> tpdu, shared_ptr<uint8_t> unSentCellCount, shared_ptr<bool> hasCellFailed,
    const sptr<ISendShortMessageCallback> &sendCallback)
{
    if (tpdu == nullptr) {
        SendResultCallBack(sendCallback, ISendShortMessageCallback::SEND_SMS_FAILURE_UNKNOWN);
        TELEPHONY_LOGE("TextBasedSmsDelivery tpdu nullptr error.");
        return true;
    }

    int cellsInfosSize = static_cast<int>(cellsInfos.size());
    if (cellsInfosSize > MAX_SEGMENT_NUM) {
        SendResultCallBack(sendCallback, ISendShortMessageCallback::SEND_SMS_FAILURE_UNKNOWN);
        TELEPHONY_LOGE("message exceed the limit.");
        return true;
    }

    bool isStatusReport = tpdu->data.submit.bStatusReport;
    TELEPHONY_LOGI("TextBasedSmsDelivery isStatusReport= %{public}d", isStatusReport);
    if (unSentCellCount == nullptr || hasCellFailed == nullptr) {
        SendResultCallBack(sendCallback, ISendShortMessageCallback::SEND_SMS_FAILURE_UNKNOWN);
        return true;
    }
    return false;
}

void CdmaSmsSender::SetSendIndexerInfo(const std::shared_ptr<SmsSendIndexer> &indexer,
    const std::shared_ptr<struct EncodeInfo> &encodeInfo, uint8_t msgRef8bit)
{
    if (encodeInfo == nullptr || indexer == nullptr) {
        TELEPHONY_LOGE("CdmaSmsSender::SetSendIndexerInfo encodeInfo nullptr");
        return;
    }

    std::vector<uint8_t> smca(encodeInfo->smcaData_, encodeInfo->smcaData_ + encodeInfo->smcaLen);
    std::vector<uint8_t> pdu(encodeInfo->tpduData_, encodeInfo->tpduData_ + encodeInfo->tpduLen);
    chrono::system_clock::duration timePoint = chrono::system_clock::now().time_since_epoch();
    long timeStamp = chrono::duration_cast<chrono::seconds>(timePoint).count();
    indexer->SetTimeStamp(timeStamp);
    indexer->SetEncodeSmca(std::move(smca));
    indexer->SetEncodePdu(std::move(pdu));
    indexer->SetHasMore(encodeInfo->isMore_);
    indexer->SetMsgRefId(msgRef8bit);
    indexer->SetNetWorkType(NET_TYPE_CDMA);
}

void CdmaSmsSender::SetPduSeqInfo(const std::shared_ptr<SmsSendIndexer> &smsIndexer, const std::size_t size,
    const std::unique_ptr<SmsTransMsg> &transMsg, const std::size_t index, const uint8_t msgRef8bit)
{
    if (size > 1) {
        smsIndexer->SetIsConcat(true);
        SmsConcat smsConcat;
        transMsg->data.p2pMsg.transTelesvcId = SMS_TRANS_TELESVC_WEMT;
        transMsg->data.p2pMsg.telesvcMsg.data.submit.msgId.headerInd = true;
        transMsg->data.p2pMsg.telesvcMsg.data.submit.userData.userData.headerCnt = 1;
        transMsg->data.p2pMsg.telesvcMsg.data.submit.userData.userData.header[0].udhType = SMS_UDH_CONCAT_8BIT;
        transMsg->data.p2pMsg.telesvcMsg.data.submit.userData.userData.header[0].udh.concat8bit.msgRef = msgRef8bit;
        transMsg->data.p2pMsg.telesvcMsg.data.submit.userData.userData.header[0].udh.concat8bit.totalSeg =
            static_cast<uint8_t>(size);
        transMsg->data.p2pMsg.telesvcMsg.data.submit.userData.userData.header[0].udh.concat8bit.seqNum = index + 1;
        smsConcat.msgRef = msgRef8bit;
        smsConcat.seqNum = index + 1;
        smsConcat.totalSeg = static_cast<uint8_t>(size);
        smsConcat.is8Bits = true;
        smsIndexer->SetSmsConcat(smsConcat);
    }
}

void CdmaSmsSender::DataBasedSmsDelivery(const string &desAddr, const string &scAddr, int32_t port,
    const uint8_t *data, uint32_t dataLen, const sptr<ISendShortMessageCallback> &sendCallback,
    const sptr<IDeliveryShortMessageCallback> &deliveryCallback)
{
    if (isImsNetDomain_ && imsSmsCfg_) {
        DataBasedSmsDeliveryViaIms(desAddr, scAddr, port, data, dataLen, sendCallback, deliveryCallback);
        return;
    }
    CdmaSmsMessage message;
    SmsCodingScheme codingType;
    std::vector<struct SplitInfo> splits;
    std::string text((char *)data, dataLen);
    message.SplitMessage(splits, text, false, codingType);
    if (splits.size() == 0) {
        TELEPHONY_LOGE("splits fail.");
        return;
    }
    std::unique_ptr<SmsTransMsg> transMsg = nullptr;
    bool bStatusReport = (deliveryCallback == nullptr) ? false : true;
    transMsg = message.CreateSubmitTransMsg(desAddr, scAddr, port, data, dataLen, bStatusReport);
    if (transMsg == nullptr) {
        SendResultCallBack(sendCallback, ISendShortMessageCallback::SEND_SMS_FAILURE_UNKNOWN);
        TELEPHONY_LOGE("CreateSubmitTransMsg nullptr fail.");
        return;
    }
    /* Set Reply sequence number. */
    uint8_t msgRef8bit = GetSeqNum();
    transMsg->data.p2pMsg.transReplySeq = msgRef8bit;
    /* Set msg ID. */
    uint16_t msgId = GetSubmitMsgId();
    transMsg->data.p2pMsg.telesvcMsg.data.submit.msgId.msgId = msgId;
    /* while user data header isn't exist, headerInd must be set false. */
    transMsg->data.p2pMsg.telesvcMsg.data.submit.msgId.headerInd = true;

    chrono::system_clock::duration timePoint = chrono::system_clock::now().time_since_epoch();
    long timeStamp = chrono::duration_cast<chrono::seconds>(timePoint).count();
    transMsg->data.p2pMsg.telesvcMsg.data.submit.userData.userData.length =
        static_cast<int>(splits[0].encodeData.size());
    if (memcpy_s(transMsg->data.p2pMsg.telesvcMsg.data.submit.userData.userData.data,
        sizeof(transMsg->data.p2pMsg.telesvcMsg.data.submit.userData.userData.data),
        splits[0].encodeData.data(), splits[0].encodeData.size()) != EOK) {
        SendResultCallBack(sendCallback, ISendShortMessageCallback::SEND_SMS_FAILURE_UNKNOWN);
        TELEPHONY_LOGE("memcpy_s return error.");
        return;
    }
    /* encode msg data */
    uint8_t pduStr[TAPI_NETTEXT_SMDATA_SIZE_MAX + 1] = { 0 };
    int len = CdmaSmsPduCodec::EncodeMsg(*transMsg.get(), pduStr);
    if (len <= 0) {
        TELEPHONY_LOGE("EncodeMsg Error len = %{public}d", len);
        SendResultCallBack(sendCallback, ISendShortMessageCallback::SEND_SMS_FAILURE_UNKNOWN);
        return;
    }

    const uint8_t segmentCount = 1;
    std::shared_ptr<SmsSendIndexer> indexer = nullptr;
    shared_ptr<uint8_t> unSentCellCount = make_shared<uint8_t>(segmentCount);
    shared_ptr<bool> hasCellFailed = make_shared<bool>(false);
    indexer = make_shared<SmsSendIndexer>(desAddr, scAddr, port, splits[0].encodeData.data(),
        splits[0].encodeData.size(), sendCallback, deliveryCallback);
    if (indexer == nullptr || unSentCellCount == nullptr || hasCellFailed == nullptr) {
        SendResultCallBack(sendCallback, ISendShortMessageCallback::SEND_SMS_FAILURE_UNKNOWN);
        TELEPHONY_LOGE("Init SmsSend Indexer Error.");
        return;
    }
    std::vector<uint8_t> pdu(pduStr, pduStr + len);
    indexer->SetEncodePdu(std::move(pdu));
    indexer->SetMsgRefId(msgRef8bit);
    indexer->SetNetWorkType(NET_TYPE_CDMA);
    indexer->SetUnSentCellCount(unSentCellCount);
    indexer->SetHasCellFailed(hasCellFailed);
    indexer->SetTimeStamp(timeStamp);
    indexer->SetMsgId(msgId);
    SendSmsToRil(indexer);
}

void CdmaSmsSender::DataBasedSmsDeliveryViaIms(const string &desAddr, const string &scAddr, int32_t port,
    const uint8_t *data, uint32_t dataLen, const sptr<ISendShortMessageCallback> &sendCallback,
    const sptr<IDeliveryShortMessageCallback> &deliveryCallback)
{
    uint8_t msgRef8bit = GetMsgRef8Bit();
    GsmSmsMessage gsmSmsMessage;
    std::shared_ptr<struct SmsTpdu> tpdu = gsmSmsMessage.CreateDataSubmitSmsTpdu(
        desAddr, scAddr, port, data, dataLen, msgRef8bit, (deliveryCallback == nullptr) ? false : true);
    std::shared_ptr<struct EncodeInfo> encodeInfo = gsmSmsMessage.GetSubmitEncodeInfo(scAddr, false);
    if (encodeInfo == nullptr || tpdu == nullptr) {
        SendResultCallBack(sendCallback, ISendShortMessageCallback::SEND_SMS_FAILURE_UNKNOWN);
        TELEPHONY_LOGE("DataBasedSmsDelivery encodeInfo or tpdu nullptr error.");
        return;
    }
    shared_ptr<SmsSendIndexer> indexer = nullptr;
    indexer = make_shared<SmsSendIndexer>(desAddr, scAddr, port, data, dataLen, sendCallback, deliveryCallback);
    if (indexer == nullptr) {
        SendResultCallBack(indexer, ISendShortMessageCallback::SEND_SMS_FAILURE_UNKNOWN);
        TELEPHONY_LOGE("DataBasedSmsDelivery create SmsSendIndexer nullptr");
        return;
    }

    std::vector<uint8_t> smca(encodeInfo->smcaData_, encodeInfo->smcaData_ + encodeInfo->smcaLen);
    std::vector<uint8_t> pdu(encodeInfo->tpduData_, encodeInfo->tpduData_ + encodeInfo->tpduLen);
    std::shared_ptr<uint8_t> unSentCellCount = make_shared<uint8_t>(1);
    std::shared_ptr<bool> hasCellFailed = make_shared<bool>(false);
    if (unSentCellCount == nullptr || hasCellFailed == nullptr) {
        SendResultCallBack(sendCallback, ISendShortMessageCallback::SEND_SMS_FAILURE_UNKNOWN);
        return;
    }
    chrono::system_clock::duration timePoint = chrono::system_clock::now().time_since_epoch();
    long timeStamp = chrono::duration_cast<chrono::seconds>(timePoint).count();
    indexer->SetUnSentCellCount(unSentCellCount);
    indexer->SetHasCellFailed(hasCellFailed);
    indexer->SetEncodeSmca(std::move(smca));
    indexer->SetEncodePdu(std::move(pdu));
    indexer->SetHasMore(encodeInfo->isMore_);
    indexer->SetMsgRefId(msgRef8bit);
    indexer->SetNetWorkType(NET_TYPE_GSM);
    indexer->SetTimeStamp(timeStamp);
    std::unique_lock<std::mutex> lock(mutex_);
    indexer->SetImsSmsForCdma(true);
    SendSmsToRil(indexer);
}

void CdmaSmsSender::StatusReportAnalysis(const AppExecFwk::InnerEvent::Pointer &event)
{
    if (event == nullptr) {
        TELEPHONY_LOGE("cdma_sms_sender: StatusReportAnalysis event nullptr error.");
        return;
    }
    std::shared_ptr<SmsReceiveIndexer> statusInfo = event->GetSharedObject<SmsReceiveIndexer>();
    if (statusInfo == nullptr) {
        TELEPHONY_LOGE("cdma_sms_sender: StatusReportAnalysis statusInfo nullptr error.");
        return;
    }
    std::string pdu = StringUtils::StringToHex(statusInfo->GetPdu());
    std::shared_ptr<CdmaSmsMessage> message = CdmaSmsMessage::CreateMessage(pdu);
    if (message == nullptr) {
        TELEPHONY_LOGE("message is nullptr.");
        return;
    }
    sptr<IDeliveryShortMessageCallback> deliveryCallback = nullptr;
    auto oldIndexer = reportList_.begin();
    while (oldIndexer != reportList_.end()) {
        auto iter = oldIndexer++;
        if (*iter != nullptr) {
            if (message->GetMsgRef() == (*iter)->GetMsgRefId()) {
                // save the message to db, or updata to db msg state(success or fail)
                deliveryCallback = (*iter)->GetDeliveryCallback();
                reportList_.erase(iter);
            }
        }
    }
    if (deliveryCallback != nullptr) {
        std::string ackpdu = StringUtils::StringToHex(message->GetRawPdu());
        deliveryCallback->OnSmsDeliveryResult(StringUtils::ToUtf16(ackpdu));
        TELEPHONY_LOGI("gsm_sms_sender: StatusReportAnalysis %{public}s", pdu.c_str());
    }
}

void CdmaSmsSender::SendSmsToRil(const shared_ptr<SmsSendIndexer> &smsIndexer)
{
    if (smsIndexer == nullptr) {
        TELEPHONY_LOGE("cdma_sms_sender: SendSms smsIndexer nullptr");
        return;
    }
    if ((!isImsNetDomain_ && voiceServiceState_ != static_cast<int32_t>(RegServiceState::REG_STATE_IN_SERVICE))) {
        SendResultCallBack(smsIndexer, ISendShortMessageCallback::SEND_SMS_FAILURE_SERVICE_UNAVAILABLE);
        TELEPHONY_LOGE("cdma_sms_sender: SendSms not in service");
        return;
    }
    int64_t refId = GetMsgRef64Bit();
    if (!SendCacheMapAddItem(refId, smsIndexer)) {
        TELEPHONY_LOGE("SendCacheMapAddItem Error!!");
    }

    std::string pdu = StringUtils::StringToHex(smsIndexer->GetEncodePdu());
    bool sendImsSMS = smsIndexer->IsImsSmsForCdma();
    if (lastSmsDomain_ == IMS_DOMAIN && smsIndexer->GetPsResendCount() < MAX_SEND_RETRIES) {
        sendImsSMS = true;
    }

    if (sendImsSMS) {
        SendImsSms(smsIndexer, refId, pdu);
    } else {
        SendCsSms(smsIndexer, refId, pdu);
    }
}

void CdmaSmsSender::SendCsSms(const shared_ptr<SmsSendIndexer> &smsIndexer, int64_t &refId, std::string &pdu)
{
    lastSmsDomain_ = CS_DOMAIN;
    smsIndexer->SetPsResendCount(INITIAL_COUNT);
    CoreManagerInner::GetInstance().SendCdmaSms(
        slotId_, RadioEvent::RADIO_SEND_CDMA_SMS, pdu, refId, shared_from_this());
    TELEPHONY_LOGI("SendCsSms pdu = %{public}s", pdu.c_str());
}

void CdmaSmsSender::SendImsSms(const shared_ptr<SmsSendIndexer> &smsIndexer, int64_t &refId, std::string &pdu)
{
    TELEPHONY_LOGI("ims network domain send sms interface.!");
    auto smsClient = DelayedSingleton<ImsSmsClient>::GetInstance();
    if (smsClient == nullptr) {
        TELEPHONY_LOGE("SendImsSms return, ImsSmsClient is nullptr.");
        return;
    }
    lastSmsDomain_ = IMS_DOMAIN;
    smsIndexer->SetCsResendCount(INITIAL_COUNT);
    ImsMessageInfo imsMessageInfo;
    imsMessageInfo.refId = refId;
    imsMessageInfo.smscPdu = "";
    imsMessageInfo.pdu = pdu;
    imsMessageInfo.tech = SMS_RADIO_TECH_3GPP;
    int32_t reply = smsClient->ImsSendMessage(slotId_, imsMessageInfo);
    TELEPHONY_LOGI("SendImsSms reply = %{public}d", reply);
}

bool CdmaSmsSender::IsImsSmsSupported(int32_t slotId)
{
    auto smsClient = DelayedSingleton<ImsSmsClient>::GetInstance();
    if (smsClient == nullptr) {
        TELEPHONY_LOGE("IsImsSmsSupported return, ImsSmsClient is nullptr.");
        return false;
    }
    std::unique_lock<std::mutex> lck(ctx_);
    resIsSmsReady_ = false;
    int32_t reply = smsClient->ImsGetSmsConfig(slotId);
    TELEPHONY_LOGI("IsImsSmsSupported reply = %{public}d", reply);
    while (resIsSmsReady_) {
        TELEPHONY_LOGI("IsImsSmsSupported::wait(), resIsSmsReady_ = false");
        if (cv_.wait_for(lck, std::chrono::seconds(WAIT_TIME_SECOND)) == std::cv_status::timeout) {
            break;
        }
    }
    TELEPHONY_LOGI("CdmaSmsSender::IsImsSmsSupported(), imsSmsCfg_:%{public}d", imsSmsCfg_);
    return (imsSmsCfg_ == IMS_SMS_ENABLE);
}

void CdmaSmsSender::StatusReportSetImsSms(const AppExecFwk::InnerEvent::Pointer &event)
{
    if (event == nullptr) {
        TELEPHONY_LOGE("cdma_sms_sender: StatusReportSetImsSms event nullptr error.");
        return;
    }
    std::shared_ptr<HRilRadioResponseInfo> imsResponseInfo = event->GetSharedObject<HRilRadioResponseInfo>();

    if (imsResponseInfo->error != HRilErrType::NONE) {
        imsSmsCfg_ = IMS_SMS_DISABLE;
    }
}

void CdmaSmsSender::StatusReportGetImsSms(const AppExecFwk::InnerEvent::Pointer &event)
{
    if (event == nullptr) {
        TELEPHONY_LOGE("CdmaSmsSender: StatusReportGetImsSms event nullptr error.");
        return;
    }
    std::shared_ptr<int32_t> imsSmsInfo = event->GetSharedObject<int32_t>();
    imsSmsCfg_ = *imsSmsInfo;
}

void CdmaSmsSender::Init() {}

void CdmaSmsSender::ReceiveStatusReport(const std::shared_ptr<SmsReceiveIndexer> &smsIndexer)
{
    SendEvent(RadioEvent::RADIO_SMS_STATUS, smsIndexer);
}

void CdmaSmsSender::RegisterImsHandler()
{
    if (isImsCdmaHandlerRegistered) {
        return;
    }
    auto smsClient = DelayedSingleton<ImsSmsClient>::GetInstance();
    if (smsClient == nullptr) {
        TELEPHONY_LOGE("RegisterHandler return, ImsSmsClient is nullptr.");
        return;
    }
    smsClient->RegisterImsSmsCallbackHandler(slotId_, shared_from_this());
    TELEPHONY_LOGE("RegisterHandler  gsm ImsSmsClient successs");
    isImsCdmaHandlerRegistered = true;
}

uint8_t CdmaSmsSender::GetSeqNum()
{
    msgSeqNum_ = ((msgSeqNum_ + 1) % SMS_SEQ_NUM_MAX);
    return msgSeqNum_;
}

uint8_t CdmaSmsSender::GetSubmitMsgId()
{
    msgSubmitId_ = ((msgSubmitId_ + 1) % SMS_MAX_MESSAGE_ID);
    return msgSubmitId_;
}

void CdmaSmsSender::ResendTextDelivery(const std::shared_ptr<SmsSendIndexer> &smsIndexer)
{
    if (smsIndexer == nullptr) {
        TELEPHONY_LOGE("smsIndexer is nullptr!!");
        return;
    }
    CdmaSmsMessage message;
    SmsCodingScheme codingType = smsIndexer->GetDcs();
    std::unique_ptr<SmsTransMsg> transMsg = nullptr;
    bool bStatusReport = (smsIndexer->GetDeliveryCallback() == nullptr) ? false : true;
    transMsg = message.CreateSubmitTransMsg(
        smsIndexer->GetDestAddr(), smsIndexer->GetSmcaAddr(), smsIndexer->GetText(), bStatusReport, codingType);
    if (transMsg == nullptr) {
        SendResultCallBack(smsIndexer->GetSendCallback(), ISendShortMessageCallback::SEND_SMS_FAILURE_UNKNOWN);
        TELEPHONY_LOGE("CreateSubmitTransMsg nullptr fail.");
        return;
    }
    /* 1. Set Reply sequence number. */
    uint8_t msgRef8bit = smsIndexer->GetMsgRefId();
    transMsg->data.p2pMsg.transReplySeq = msgRef8bit;
    /* 2. Set msg ID. */
    transMsg->data.p2pMsg.telesvcMsg.data.submit.msgId.msgId = smsIndexer->GetMsgId();
    chrono::system_clock::duration timePoint = chrono::system_clock::now().time_since_epoch();
    long timeStamp = chrono::duration_cast<chrono::seconds>(timePoint).count();
    smsIndexer->SetTimeStamp(timeStamp);
    transMsg->data.p2pMsg.telesvcMsg.data.submit.userData.encodeType = SMS_ENCODE_OCTET;
    (void)memset_s(transMsg->data.p2pMsg.telesvcMsg.data.submit.userData.userData.data,
        sizeof(transMsg->data.p2pMsg.telesvcMsg.data.submit.userData.userData.data), 0x00,
        sizeof(transMsg->data.p2pMsg.telesvcMsg.data.submit.userData.userData.data));
    if (memcpy_s(transMsg->data.p2pMsg.telesvcMsg.data.submit.userData.userData.data,
        sizeof(transMsg->data.p2pMsg.telesvcMsg.data.submit.userData.userData.data),
        smsIndexer->GetText().data(), smsIndexer->GetText().length()) != EOK) {
        SendResultCallBack(smsIndexer->GetSendCallback(), ISendShortMessageCallback::SEND_SMS_FAILURE_UNKNOWN);
        return;
    }
    SetConcact(smsIndexer, transMsg);
    /* encode msg data */
    uint8_t pduStr[TAPI_NETTEXT_SMDATA_SIZE_MAX + 1] = { 0 };
    int len = CdmaSmsPduCodec::EncodeMsg(*transMsg.get(), pduStr);
    std::vector<uint8_t> pdu(pduStr, pduStr + len);
    smsIndexer->SetEncodePdu(std::move(pdu));
    smsIndexer->SetNetWorkType(NET_TYPE_CDMA);
    smsIndexer->SetTimeStamp(timeStamp);
    SendSmsToRil(smsIndexer);
}

void CdmaSmsSender::ResendDataDelivery(const std::shared_ptr<SmsSendIndexer> &smsIndexer)
{
    CdmaSmsMessage message;
    std::unique_ptr<SmsTransMsg> transMsg = nullptr;
    bool bStatusReport = (smsIndexer->GetDeliveryCallback() == nullptr) ? false : true;
    transMsg = message.CreateSubmitTransMsg(smsIndexer->GetDestAddr(), smsIndexer->GetSmcaAddr(),
        smsIndexer->GetDestPort(), smsIndexer->GetData().data(), smsIndexer->GetData().size(), bStatusReport);
    if (transMsg == nullptr) {
        SendResultCallBack(smsIndexer->GetSendCallback(), ISendShortMessageCallback::SEND_SMS_FAILURE_UNKNOWN);
        TELEPHONY_LOGE("CreateSubmitTransMsg nullptr fail.");
        return;
    }
    /* Set Reply sequence number. */
    uint8_t msgRef8bit = smsIndexer->GetMsgRefId();
    transMsg->data.p2pMsg.transReplySeq = msgRef8bit;
    /* Set msg ID. */
    transMsg->data.p2pMsg.telesvcMsg.data.submit.msgId.msgId = smsIndexer->GetMsgId();
    /* while user data header isn't exist, headerInd must be set false. */
    transMsg->data.p2pMsg.telesvcMsg.data.submit.msgId.headerInd = true;

    chrono::system_clock::duration timePoint = chrono::system_clock::now().time_since_epoch();
    long timeStamp = chrono::duration_cast<chrono::seconds>(timePoint).count();
    transMsg->data.p2pMsg.telesvcMsg.data.submit.userData.encodeType = SMS_ENCODE_OCTET;
    if (memcpy_s(transMsg->data.p2pMsg.telesvcMsg.data.submit.userData.userData.data,
        sizeof(transMsg->data.p2pMsg.telesvcMsg.data.submit.userData.userData.data),
        smsIndexer->GetData().data(), smsIndexer->GetData().size()) != EOK) {
        SendResultCallBack(smsIndexer->GetSendCallback(), ISendShortMessageCallback::SEND_SMS_FAILURE_UNKNOWN);
        return;
    }

    transMsg->data.p2pMsg.telesvcMsg.data.submit.userData.userData.length =
        static_cast<int>(smsIndexer->GetData().size());
    /* encode msg data */
    uint8_t pduStr[TAPI_NETTEXT_SMDATA_SIZE_MAX + 1] = { 0 };
    int len = CdmaSmsPduCodec::EncodeMsg(*transMsg.get(), pduStr);
    std::vector<uint8_t> pdu(pduStr, pduStr + len);
    smsIndexer->SetEncodePdu(std::move(pdu));
    smsIndexer->SetNetWorkType(NET_TYPE_CDMA);
    smsIndexer->SetTimeStamp(timeStamp);
    SendSmsToRil(smsIndexer);
}

void CdmaSmsSender::SetConcact(
    const std::shared_ptr<SmsSendIndexer> &smsIndexer, const std::unique_ptr<SmsTransMsg> &transMsg)
{
    if (smsIndexer->GetIsConcat()) {
        SmsConcat smsConcat = smsIndexer->GetSmsConcat();
        transMsg->data.p2pMsg.transTelesvcId = SMS_TRANS_TELESVC_WEMT;
        transMsg->data.p2pMsg.telesvcMsg.data.submit.msgId.headerInd = true;
        transMsg->data.p2pMsg.telesvcMsg.data.submit.userData.userData.headerCnt = 1;
        if (smsConcat.is8Bits) {
            transMsg->data.p2pMsg.telesvcMsg.data.submit.userData.userData.header[0].udhType = SMS_UDH_CONCAT_8BIT;
        } else {
            transMsg->data.p2pMsg.telesvcMsg.data.submit.userData.userData.header[0].udhType = SMS_UDH_CONCAT_16BIT;
        }
        transMsg->data.p2pMsg.telesvcMsg.data.submit.userData.userData.header[0].udh.concat8bit.msgRef =
            smsConcat.msgRef;
        transMsg->data.p2pMsg.telesvcMsg.data.submit.userData.userData.header[0].udh.concat8bit.totalSeg =
            smsConcat.totalSeg;
        transMsg->data.p2pMsg.telesvcMsg.data.submit.userData.userData.header[0].udh.concat8bit.seqNum =
            smsConcat.seqNum;
    }
}
} // namespace Telephony
} // namespace OHOS