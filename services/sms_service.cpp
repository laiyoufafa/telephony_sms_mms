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

#include "sms_service.h"

#include <string>

#include "core_manager_inner.h"
#include "sms_dump_helper.h"
#include "string_utils.h"
#include "telephony_permission.h"
#include "telephony_log_wrapper.h"

namespace OHOS {
namespace Telephony {
using namespace std;
using namespace AppExecFwk;
using namespace HiviewDFX;
bool g_registerResult = SystemAbility::MakeAndRegisterAbility(DelayedSingleton<SmsService>::GetInstance().get());

SmsService::SmsService() : SystemAbility(TELEPHONY_SMS_MMS_SYS_ABILITY_ID, true) {}

SmsService::~SmsService() {}

void SmsService::OnStart()
{
    TELEPHONY_LOGI("SmsService::OnStart start service Enter.");
    if (state_ == ServiceRunningState::STATE_RUNNING) {
        TELEPHONY_LOGE("msService has already started.");
        return;
    }
    if (!Init()) {
        TELEPHONY_LOGE("failed to init SmsService");
        return;
    }
    state_ = ServiceRunningState::STATE_RUNNING;
    TELEPHONY_LOGI("SmsService::OnStart start service Exit.");
}

bool SmsService::Init()
{
    if (!registerToService_) {
        bool ret = Publish(DelayedSingleton<SmsService>::GetInstance().get());
        if (!ret) {
            TELEPHONY_LOGE("SmsService::Init Publish failed!");
            return false;
        }
        bindTime_ = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
                        .count();
        registerToService_ = true;
        WaitCoreServiceToInit();
    }
    return true;
}

void SmsService::OnStop()
{
    state_ = ServiceRunningState::STATE_NOT_START;
    registerToService_ = false;
    TELEPHONY_LOGI("SmsService::OnStop stop service.");
}

int32_t SmsService::Dump(std::int32_t fd, const std::vector<std::u16string> &args)
{
    if (fd < 0) {
        TELEPHONY_LOGE("dump fd invalid");
        return TELEPHONY_ERR_FAIL;
    }
    std::vector<std::string> argsInStr;
    for (const auto &arg : args) {
        TELEPHONY_LOGI("Dump args: %{public}s", Str16ToStr8(arg).c_str());
        argsInStr.emplace_back(Str16ToStr8(arg));
    }
    std::string result;
    SmsDumpHelper dumpHelper;
    if (dumpHelper.Dump(argsInStr, result)) {
        TELEPHONY_LOGI("%{public}s", result.c_str());
        std::int32_t ret = dprintf(fd, "%s", result.c_str());
        if (ret < 0) {
            TELEPHONY_LOGE("dprintf to dump fd failed");
            return TELEPHONY_ERR_FAIL;
        }
        return TELEPHONY_SUCCESS;
    }
    TELEPHONY_LOGW("dumpHelper failed");
    return TELEPHONY_ERR_FAIL;
}

void SmsService::WaitCoreServiceToInit()
{
    std::thread connectTask([&]() {
        while (true) {
            TELEPHONY_LOGI("connect core service ...");
            if (CoreManagerInner::GetInstance().IsInitFinished()) {
                InitModule();
                TELEPHONY_LOGI("SmsService Connection successful");
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(CONNECT_SERVICE_WAIT_TIME));
        }
    });
    connectTask.detach();
}

std::string SmsService::GetBindTime()
{
    return std::to_string(bindTime_);
}

void SmsService::SendMessage(int32_t slotId, const u16string desAddr, const u16string scAddr,
    const u16string text, const sptr<ISendShortMessageCallback> &sendCallback,
    const sptr<IDeliveryShortMessageCallback> &deliveryCallback)
{
    if (!TelephonyPermission::CheckPermission(Permission::SEND_MESSAGES)) {
        SmsSender::SendResultCallBack(sendCallback, ISendShortMessageCallback::SEND_SMS_FAILURE_UNKNOWN);
        TELEPHONY_LOGE("Check Permission Failed, No Has Telephony Send Messages Permisson.");
        return;
    }

    std::shared_ptr<SmsInterfaceManager> interfaceManager = GetSmsInterfaceManager(slotId);
    if (interfaceManager == nullptr) {
        SmsSender::SendResultCallBack(sendCallback, ISendShortMessageCallback::SEND_SMS_FAILURE_UNKNOWN);
        TELEPHONY_LOGE("SmsService::SendMessage interfaceManager nullptr error.");
        return;
    }
    interfaceManager->TextBasedSmsDelivery(StringUtils::ToUtf8(desAddr), StringUtils::ToUtf8(scAddr),
        StringUtils::ToUtf8(text), sendCallback, deliveryCallback);
}

void SmsService::SendMessage(int32_t slotId, const u16string desAddr, const u16string scAddr, uint16_t port,
    const uint8_t *data, uint16_t dataLen, const sptr<ISendShortMessageCallback> &sendCallback,
    const sptr<IDeliveryShortMessageCallback> &deliveryCallback)
{
    if (!TelephonyPermission::CheckPermission(Permission::SEND_MESSAGES)) {
        SmsSender::SendResultCallBack(sendCallback, ISendShortMessageCallback::SEND_SMS_FAILURE_UNKNOWN);
        TELEPHONY_LOGE("Check Permission Failed, No Has Telephony Send Messages Permisson.");
        return;
    }

    std::shared_ptr<SmsInterfaceManager> interfaceManager = GetSmsInterfaceManager(slotId);
    if (interfaceManager == nullptr) {
        SmsSender::SendResultCallBack(sendCallback, ISendShortMessageCallback::SEND_SMS_FAILURE_UNKNOWN);
        TELEPHONY_LOGE("SmsService::SendMessage interfaceManager nullptr error.");
        return;
    }
    interfaceManager->DataBasedSmsDelivery(StringUtils::ToUtf8(desAddr), StringUtils::ToUtf8(scAddr), port, data,
        dataLen, sendCallback, deliveryCallback);
}

bool SmsService::IsImsSmsSupported()
{
    bool result = false;
    int32_t slotId = GetDefaultSmsSlotId();
    std::shared_ptr<SmsInterfaceManager> interfaceManager = GetSmsInterfaceManager(slotId);
    if (interfaceManager == nullptr) {
        TELEPHONY_LOGE("interfaceManager is nullptr.");
        return result;
    }
    return interfaceManager->IsImsSmsSupported();
}

std::u16string SmsService::GetImsShortMessageFormat()
{
    std::u16string result;
    int32_t slotId = GetDefaultSmsSlotId();
    std::shared_ptr<SmsInterfaceManager> interfaceManager = GetSmsInterfaceManager(slotId);
    if (interfaceManager == nullptr) {
        TELEPHONY_LOGE("interfaceManager is nullptr error.");
        return result;
    }
    return StringUtils::ToUtf16(interfaceManager->GetImsShortMessageFormat());
}

bool SmsService::HasSmsCapability()
{
    std::shared_ptr<SmsInterfaceManager> interfaceManager = GetSmsInterfaceManager();
    if (interfaceManager == nullptr) {
        TELEPHONY_LOGE("Get SmsInterfaceManager is nullptr error.");
        return true;
    }
    return interfaceManager->HasSmsCapability();
}

bool SmsService::SetSmscAddr(int32_t slotId, const std::u16string &scAddr)
{
    bool result = false;
    if (!TelephonyPermission::CheckPermission(Permission::SET_TELEPHONY_STATE)) {
        TELEPHONY_LOGE("Check Permission Failed, No Has Telephony Set State Permisson.");
        return false;
    }

    std::shared_ptr<SmsInterfaceManager> interfaceManager = GetSmsInterfaceManager(slotId);
    if (interfaceManager == nullptr) {
        TELEPHONY_LOGE("interfaceManager is nullptr error.");
        return result;
    }
    string sca = StringUtils::ToUtf8(scAddr);
    return interfaceManager->SetSmscAddr(sca);
}

std::u16string SmsService::GetSmscAddr(int32_t slotId)
{
    std::u16string result;
    if (!TelephonyPermission::CheckPermission(Permission::GET_TELEPHONY_STATE)) {
        TELEPHONY_LOGE("Check Permission Failed, No Has Telephony Get State Permisson.");
        return result;
    }

    std::shared_ptr<SmsInterfaceManager> interfaceManager = GetSmsInterfaceManager(slotId);
    if (interfaceManager == nullptr) {
        TELEPHONY_LOGE("SmsService::GetSmscAddr interfaceManager nullptr error");
        return result;
    }
    return StringUtils::ToUtf16(interfaceManager->GetSmscAddr());
}

bool SmsService::AddSimMessage(
    int32_t slotId, const std::u16string &smsc, const std::u16string &pdu, SimMessageStatus status)
{
    bool result = false;
    if (!TelephonyPermission::CheckPermission(Permission::RECEIVE_MESSAGES)) {
        TELEPHONY_LOGE("Check Permission Failed, No Has Telephony Receive Messages Permisson.");
        return result;
    }
    if (!TelephonyPermission::CheckPermission(Permission::SEND_MESSAGES)) {
        TELEPHONY_LOGE("Check Permission Failed, No Has Telephony Send Messages Permisson.");
        return result;
    }

    std::shared_ptr<SmsInterfaceManager> interfaceManager = GetSmsInterfaceManager(slotId);
    if (interfaceManager == nullptr) {
        TELEPHONY_LOGE("SmsService::AddSimMessage interfaceManager nullptr error");
        return result;
    }
    std::string smscData = StringUtils::ToUtf8(smsc);
    std::string pduData = StringUtils::ToUtf8(pdu);
    return interfaceManager->AddSimMessage(smscData, pduData, status);
}

bool SmsService::DelSimMessage(int32_t slotId, uint32_t msgIndex)
{
    bool result = false;
    if (!TelephonyPermission::CheckPermission(Permission::RECEIVE_MESSAGES)) {
        TELEPHONY_LOGE("Check Permission Failed, No Has Telephony Receive Messages Permisson.");
        return result;
    }
    if (!TelephonyPermission::CheckPermission(Permission::SEND_MESSAGES)) {
        TELEPHONY_LOGE("Check Permission Failed, No Has Telephony Send Messages Permisson.");
        return result;
    }

    std::shared_ptr<SmsInterfaceManager> interfaceManager = GetSmsInterfaceManager(slotId);
    if (interfaceManager == nullptr) {
        TELEPHONY_LOGE("SmsService::DelSimMessage interfaceManager nullptr error.");
        return result;
    }
    return interfaceManager->DelSimMessage(msgIndex);
}

bool SmsService::UpdateSimMessage(int32_t slotId, uint32_t msgIndex, SimMessageStatus newStatus,
    const std::u16string &pdu, const std::u16string &smsc)
{
    bool result = false;
    if (!TelephonyPermission::CheckPermission(Permission::RECEIVE_MESSAGES)) {
        TELEPHONY_LOGE("Check Permission Failed, No Has Telephony Receive Messages Permisson.");
        return result;
    }
    if (!TelephonyPermission::CheckPermission(Permission::SEND_MESSAGES)) {
        TELEPHONY_LOGE("Check Permission Failed, No Has Telephony Send Messages Permisson.");
        return result;
    }

    std::shared_ptr<SmsInterfaceManager> interfaceManager = GetSmsInterfaceManager(slotId);
    if (interfaceManager == nullptr) {
        TELEPHONY_LOGE("SmsService::UpdateSimMessage interfaceManager nullptr error.");
        return result;
    }
    std::string pduData = StringUtils::ToUtf8(pdu);
    std::string smscData = StringUtils::ToUtf8(smsc);
    return interfaceManager->UpdateSimMessage(msgIndex, newStatus, pduData, smscData);
}

std::vector<ShortMessage> SmsService::GetAllSimMessages(int32_t slotId)
{
    std::vector<ShortMessage> result;
    if (!TelephonyPermission::CheckPermission(Permission::RECEIVE_MESSAGES)) {
        TELEPHONY_LOGE("Check Permission Failed, No Has Telephony Receive Messages Permisson.");
        return result;
    }

    std::shared_ptr<SmsInterfaceManager> interfaceManager = GetSmsInterfaceManager(slotId);
    if (interfaceManager == nullptr) {
        TELEPHONY_LOGE("SmsService::GetAllSimMessages interfaceManager nullptr error.");
        return result;
    }
    return interfaceManager->GetAllSimMessages();
}

bool SmsService::SetCBConfig(
    int32_t slotId, bool enable, uint32_t fromMsgId, uint32_t toMsgId, uint8_t netType)
{
    bool result = false;
    if (!TelephonyPermission::CheckPermission(Permission::RECEIVE_MESSAGES)) {
        TELEPHONY_LOGE("Check Permission Failed, No Has Telephony Receive Messages Permisson.");
        return result;
    }

    std::shared_ptr<SmsInterfaceManager> interfaceManager = GetSmsInterfaceManager(slotId);
    if (interfaceManager == nullptr) {
        TELEPHONY_LOGE("SmsService::SetCBConfig interfaceManager nullptr error.");
        return result;
    }
    return interfaceManager->SetCBConfig(enable, fromMsgId, toMsgId, netType);
}

bool SmsService::SetDefaultSmsSlotId(int32_t slotId)
{
    bool result = false;
    if (!TelephonyPermission::CheckPermission(Permission::SET_TELEPHONY_STATE)) {
        TELEPHONY_LOGE("Check Permission Failed, No Has Telephony Set State Permisson.");
        return result;
    }

    std::shared_ptr<SmsInterfaceManager> interfaceManager = GetSmsInterfaceManager();
    if (interfaceManager == nullptr) {
        TELEPHONY_LOGE("SmsService::SetCBConfig interfaceManager nullptr error.");
        return result;
    }
    return interfaceManager->SetDefaultSmsSlotId(slotId);
}

int32_t SmsService::GetDefaultSmsSlotId()
{
    int32_t result = -1;
    std::shared_ptr<SmsInterfaceManager> interfaceManager = GetSmsInterfaceManager();
    if (interfaceManager == nullptr) {
        TELEPHONY_LOGE("SmsService::SetCBConfig interfaceManager nullptr error.");
        return result;
    }
    return interfaceManager->GetDefaultSmsSlotId();
}

std::vector<std::u16string> SmsService::SplitMessage(const std::u16string &message)
{
    std::vector<std::u16string> result;
    if (!TelephonyPermission::CheckPermission(Permission::SEND_MESSAGES)) {
        TELEPHONY_LOGE("Check Permission Failed, No Has Telephony Get State Permisson.");
        return result;
    }

    if (message.empty()) {
        return result;
    }
    int32_t slotId = GetDefaultSmsSlotId();
    std::shared_ptr<SmsInterfaceManager> interfaceManager = GetSmsInterfaceManager(slotId);
    if (interfaceManager == nullptr) {
        TELEPHONY_LOGE("SmsService::SplitMessage interfaceManager nullptr error.");
        return result;
    }
    std::string messageData = StringUtils::ToUtf8(message);
    std::vector<std::string> temp;
    temp = interfaceManager->SplitMessage(messageData);
    for (auto &item : temp) {
        result.emplace_back(StringUtils::ToUtf16(item));
    }
    return result;
}

bool SmsService::GetSmsSegmentsInfo(
    int32_t slotId, const std::u16string &message, bool force7BitCode, ISmsServiceInterface::SmsSegmentsInfo &info)
{
    if (message.empty()) {
        return false;
    }
    std::shared_ptr<SmsInterfaceManager> interfaceManager = GetSmsInterfaceManager(slotId);
    if (interfaceManager == nullptr) {
        TELEPHONY_LOGE("GetSmsSegmentsInfo interfaceManager is nullptr error.");
        return false;
    }
    std::string messageData = StringUtils::ToUtf8(message);
    LengthInfo result;
    if (!interfaceManager->GetSmsSegmentsInfo(messageData, force7BitCode, result)) {
        return false;
    }
    info.msgSegCount = static_cast<int32_t>(result.msgSegCount);
    info.msgEncodingCount = static_cast<int32_t>(result.msgEncodeCount);
    info.msgRemainCount = static_cast<int32_t>(result.msgRemainCount);
    SmsSegmentsInfo::SmsSegmentCodeScheme dcs = static_cast<SmsSegmentsInfo::SmsSegmentCodeScheme>(result.dcs);
    switch (dcs) {
        case SmsSegmentsInfo::SmsSegmentCodeScheme::SMS_ENCODING_7BIT:
        case SmsSegmentsInfo::SmsSegmentCodeScheme::SMS_ENCODING_8BIT:
        case SmsSegmentsInfo::SmsSegmentCodeScheme::SMS_ENCODING_16BIT:
            info.msgCodeScheme = dcs;
            break;
        default:
            info.msgCodeScheme = SmsSegmentsInfo::SmsSegmentCodeScheme::SMS_ENCODING_UNKNOWN;
            break;
    }
    return true;
}
} // namespace Telephony
} // namespace OHOS