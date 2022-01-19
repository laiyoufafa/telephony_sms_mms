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

#include "cdma_sms_receive_handler.h"

#include "common_event.h"
#include "common_event_manager.h"
#include "common_event_support.h"
#include "want.h"

#include "cdma_sms_message.h"
#include "cdma_sms_sender.h"
#include "cdma_sms_types.h"
#include "core_manager_inner.h"
#include "radio_event.h"
#include "string_utils.h"
#include "telephony_log_wrapper.h"

namespace OHOS {
namespace Telephony {
CdmaSmsReceiveHandler::CdmaSmsReceiveHandler(const std::shared_ptr<AppExecFwk::EventRunner> &runner, int32_t slotId)
    : SmsReceiveHandler(runner, slotId)
{
    TELEPHONY_LOGI("%{public}d", slotId_);
}

int32_t CdmaSmsReceiveHandler::HandleSmsByType(const std::shared_ptr<SmsBaseMessage> &smsBaseMessage)
{
    if (smsBaseMessage == nullptr) {
        TELEPHONY_LOGE("SmsBaseMessage is null!");
        return AckIncomeCause::SMS_ACK_UNKNOWN_ERROR;
    }
    CdmaSmsMessage *message = static_cast<CdmaSmsMessage *>(smsBaseMessage.get());
    if (message->IsBroadcastMsg()) {
        SendCBBroadcast(smsBaseMessage);
        return AckIncomeCause::SMS_ACK_RESULT_OK;
    }
    int service = message->GetTransTeleService();
    if (SMS_TRANS_TELESVC_WEMT == service || SMS_TRANS_TELESVC_CMT_95 == service) {
        if (message->IsStatusReport()) {
            if (!cdmaSmsSender_.expired()) {
                std::shared_ptr<SmsSender> smsSender = cdmaSmsSender_.lock();
                CdmaSmsSender *cdmaSend = static_cast<CdmaSmsSender *>(smsSender.get());
                std::shared_ptr<SmsReceiveIndexer> statusInfo = std::make_shared<SmsReceiveIndexer>();
                statusInfo->SetMsgRefId(message->GetMsgRef());
                statusInfo->SetPdu(message->GetRawPdu());
                cdmaSend->ReceiveStatusReport(statusInfo);
            }
            return AckIncomeCause::SMS_ACK_RESULT_OK;
        }
    } else if (SMS_TRANS_TELESVC_WAP == service) {
        return AckIncomeCause::SMS_ACK_RESULT_OK;
    } else {
        return AckIncomeCause::SMS_ACK_RESULT_OK;
    }
    // Encapsulate key information to Tracker
    std::shared_ptr<SmsReceiveIndexer> indexer;
    if (!message->IsConcatMsg()) {
        indexer = std::make_shared<SmsReceiveIndexer>(message->GetRawPdu(), message->GetScTimestamp(),
            message->GetDestPort(), true, false, message->GetOriginatingAddress(),
            message->GetVisibleOriginatingAddress(), message->GetVisibleMessageBody());
    } else {
        std::shared_ptr<SmsConcat> smsConcat = message->GetConcatMsg();
        if (smsConcat == nullptr) {
            return AckIncomeCause::SMS_ACK_UNKNOWN_ERROR;
        }
        indexer = std::make_shared<SmsReceiveIndexer>(message->GetRawPdu(), message->GetScTimestamp(),
            message->GetDestPort(), true, message->GetOriginatingAddress(), message->GetVisibleOriginatingAddress(),
            smsConcat->msgRef, smsConcat->seqNum, smsConcat->totalSeg, false, message->GetVisibleMessageBody());
    }
    // add messages to the database
    if (indexer == nullptr) {
        TELEPHONY_LOGE("indexer is nullptr.");
        return AckIncomeCause::SMS_ACK_UNKNOWN_ERROR;
    }
    if (indexer->GetIsText() && IsRepeatedMessagePart(indexer)) {
        return AckIncomeCause::SMS_ACK_REPEATED_ERROR;
    }
    if (!AddMsgToDB(indexer)) {
        return AckIncomeCause::SMS_ACK_UNKNOWN_ERROR;
    }
    CombineMessagePart(indexer);
    return AckIncomeCause::SMS_ACK_RESULT_OK;
}

void CdmaSmsReceiveHandler::ReplySmsToSmsc(int result, const std::shared_ptr<SmsBaseMessage> &response)
{
    TELEPHONY_LOGI("Reply To Smsc ackResult %{public}d", result);
    CoreManagerInner::GetInstance().SendSmsAck(
        slotId_, SMS_EVENT_NEW_SMS_REPLY, result == AckIncomeCause::SMS_ACK_RESULT_OK, result, shared_from_this());
}

void CdmaSmsReceiveHandler::SetCdmaSender(const std::weak_ptr<SmsSender> &smsSender)
{
    cdmaSmsSender_ = smsSender;
}

std::shared_ptr<SmsBaseMessage> CdmaSmsReceiveHandler::TransformMessageInfo(
    const std::shared_ptr<SmsMessageInfo> &info)
{
    std::shared_ptr<SmsBaseMessage> baseMessage = nullptr;
    if (info == nullptr) {
        TELEPHONY_LOGE("SmsBaseMessage is null!");
        return baseMessage;
    }
    std::string pdu = StringUtils::StringToHex(info->pdu);
    baseMessage = CdmaSmsMessage::CreateMessage(pdu);
    return baseMessage;
}

CdmaSmsReceiveHandler::~CdmaSmsReceiveHandler()
{
    UnRegisterHandler();
}

void CdmaSmsReceiveHandler::Init()
{
    if (!RegisterHandler()) {
        TELEPHONY_LOGI("GsmSmsSender::Init Register RADIO_SMS_STATUS fail.");
    }
}

bool CdmaSmsReceiveHandler::RegisterHandler()
{
    TELEPHONY_LOGI("CdmaSmsReceiveHandler::RegisteHandler Register RADIO_CDMA_SMS ok.");
    CoreManagerInner::GetInstance().RegisterCoreNotify(
        slotId_, shared_from_this(), RadioEvent::RADIO_CDMA_SMS, nullptr);
    return true;
}

void CdmaSmsReceiveHandler::UnRegisterHandler()
{
    TELEPHONY_LOGI("CdmaSmsReceiveHandler::UnRegisterHandler::slotId= %{public}d", slotId_);
    CoreManagerInner::GetInstance().UnRegisterCoreNotify(slotId_, shared_from_this(), RadioEvent::RADIO_CDMA_SMS);
}

bool CdmaSmsReceiveHandler::SendCBBroadcast(const std::shared_ptr<SmsBaseMessage> &smsBaseMessage)
{
    if (smsBaseMessage == nullptr) {
        TELEPHONY_LOGE("smsBaseMessage is nullptr.");
        return false;
    }
    const int lac = -1;
    const int cid = -1;
    bool isMergency = false;
    SmsCbData::CbData sendData;
    GetCBData(smsBaseMessage, sendData, isMergency);
    EventFwk::Want want;
    EventFwk::CommonEventData data;
    if (isMergency) {
        want.SetAction(EventFwk::CommonEventSupport::COMMON_EVENT_SMS_EMERGENCY_CB_COMPLETED);
    } else {
        want.SetAction(EventFwk::CommonEventSupport::COMMON_EVENT_SMS_CB_RECEIVE_COMPLETED);
    }
    want.SetParam(SmsCbData::GEO_SCOPE, static_cast<char>(sendData.geoScope));
    want.SetParam(SmsCbData::CMAS_RESPONSE, static_cast<char>(sendData.cmasRes));
    want.SetParam(SmsCbData::SLOT_ID, static_cast<int>(slotId_));
    want.SetParam(SmsCbData::FORMAT, static_cast<char>(sendData.format));
    want.SetParam(SmsCbData::CB_MSG_TYPE, static_cast<char>(sendData.msgType));
    want.SetParam(SmsCbData::MSG_ID, static_cast<int>(sendData.msgId));
    want.SetParam(SmsCbData::SERVICE_CATEGORY, static_cast<int>(sendData.category));
    want.SetParam(SmsCbData::LANG_TYPE, static_cast<char>(sendData.langType));
    want.SetParam(SmsCbData::PRIORITY, static_cast<char>(sendData.priority));
    want.SetParam(SmsCbData::MSG_BODY, sendData.msgBody);
    want.SetParam(SmsCbData::CMAS_CLASS, static_cast<char>(sendData.cmasClass));
    want.SetParam(SmsCbData::CMAS_CATEGORY, static_cast<char>(sendData.cmasCate));
    want.SetParam(SmsCbData::SEVERITY, static_cast<char>(sendData.severity));
    want.SetParam(SmsCbData::URGENCY, static_cast<char>(sendData.urgency));
    want.SetParam(SmsCbData::CERTAINTY, static_cast<char>(sendData.certainty));
    want.SetParam(SmsCbData::IS_CMAS_MESSAGE, sendData.isCmas);
    want.SetParam(SmsCbData::SERIAL_NUM, static_cast<int>(sendData.serial));
    want.SetParam(SmsCbData::RECV_TIME, std::to_string(sendData.recvTime));
    want.SetParam(SmsCbData::DCS, static_cast<char>(sendData.dcs));

    want.SetParam(SmsCbData::IS_ETWS_PRIMARY, sendData.isPrimary);
    want.SetParam(SmsCbData::IS_ETWS_MESSAGE, sendData.isEtws);
    want.SetParam(SmsCbData::PLMN, StringUtils::ToUtf8(plmn_));
    want.SetParam(SmsCbData::LAC, static_cast<int>(lac));
    want.SetParam(SmsCbData::CID, static_cast<int>(cid));
    want.SetParam(SmsCbData::WARNING_TYPE, static_cast<int>(sendData.warnType));
    data.SetWant(want);
    EventFwk::CommonEventPublishInfo publishInfo;
    publishInfo.SetOrdered(true);
    bool publishResult = EventFwk::CommonEventManager::PublishCommonEvent(data, publishInfo, nullptr);
    if (!publishResult) {
        TELEPHONY_LOGE("SendBroadcast PublishBroadcastEvent result fail");
        return false;
    }
    return true;
}

void CdmaSmsReceiveHandler::GetCBData(
    const std::shared_ptr<SmsBaseMessage> &smsBaseMessage, SmsCbData::CbData &sendData, bool &isEmergency)
{
    if (smsBaseMessage == nullptr) {
        TELEPHONY_LOGE("smsBaseMessage is nullptr.");
        return;
    }
    CdmaSmsMessage *message = static_cast<CdmaSmsMessage *>(smsBaseMessage.get());
    if (message == nullptr) {
        TELEPHONY_LOGE("message is nullptr.");
        return;
    }

    sendData.format = message->GetFormat();
    sendData.geoScope = message->GetGeoScope();
    sendData.msgId = message->GetMessageId();
    sendData.serial = message->GetMessageId();
    sendData.category = message->GetServiceCategoty();
    sendData.langType = message->GetLanguage();
    sendData.msgBody = message->GetVisibleMessageBody();
    sendData.priority = message->GetPriority();
    sendData.isCmas = message->IsCMAS();
    sendData.cmasClass = message->GetCMASMessageClass();
    sendData.cmasCate = message->GetCMASCategory();
    sendData.severity = message->GetCMASSeverity();
    sendData.urgency = message->GetCMASUrgency();
    sendData.certainty = message->GetCMASCertainty();
    sendData.recvTime = message->GetReceTime();
    sendData.langType = message->GetLanguage();
    isEmergency = message->IsEmergencyMsg();
    plmn_ = CoreManagerInner::GetInstance().GetOperatorNumeric(slotId_);
}
} // namespace Telephony
} // namespace OHOS