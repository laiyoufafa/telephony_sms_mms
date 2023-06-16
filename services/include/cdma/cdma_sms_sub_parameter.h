/*
 * Copyright (C) 2023 Huawei Device Co., Ltd.
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

#ifndef CDMA_SMS_SUB_PARAMETER_H
#define CDMA_SMS_SUB_PARAMETER_H

#include "cdma_sms_common.h"
#include "sms_pdu_buffer.h"

namespace OHOS {
namespace Telephony {

class CdmaSmsSubParameter {
public:
    virtual ~CdmaSmsSubParameter() = default;
    virtual bool Encode(SmsWriteBuffer &pdu)
    {
        return false;
    };
    virtual bool Decode(SmsReadBuffer &pdu)
    {
        return false;
    };

protected:
    inline bool IsInvalidPdu(SmsReadBuffer &pdu);

public:
    uint8_t id_ { RESERVED };
    uint8_t len_ { 0 };

    enum SubParameterId : uint8_t {
        MESSAGE_IDENTIFIER = 0x00,
        USER_DATA = 0x01,
        USER_RESPONSE_CODE = 0x02,
        MESSAGE_CENTER_TIME_STAMP = 0x03,
        VALIDITY_PERIOD_ABSOLUTE = 0x04,
        VALIDITY_PERIOD_RELATIVE = 0x05,
        DEFERRED_DELIVERY_TIME_ABSOLUTE = 0x06,
        DEFERRED_DELIVERY_TIME_RELATIVE = 0x07,
        PRIORITY_INDICATOR = 0x08,
        PRIVACY_INDICATOR = 0x09,
        REPLY_OPTION = 0x0A,
        NUMBER_OF_MESSAGES = 0x0B,
        ALERT_ON_MSG_DELIVERY = 0x0C,
        LANGUAGE_INDICATOR = 0x0D,
        CALLBACK_NUMBER = 0x0E,
        MESSAGE_DISPLAY_MODE = 0x0F,
        MULTI_ENCODING_USER_DATA = 0x10,
        MESSAGE_DEPOSIT_INDEX = 0x11,
        SERVICE_CATEGORY_PROGRAM_DATA = 0x12,
        SERVICE_CATEGORY_PROGRAM_RESULT = 0x13,
        MESSAGE_STATUS = 0x14,
        TP_FAILURE_CAUSE = 0x15,
        ENHANCED_VMN = 0x16,
        ENHANCED_VMN_ACK = 0x17,
        RESERVED
    };
};

class CdmaSmsBaseParameter : public CdmaSmsSubParameter {
public:
    CdmaSmsBaseParameter(uint8_t id, uint8_t &data);
    bool Encode(SmsWriteBuffer &pdu) override;
    bool Decode(SmsReadBuffer &pdu) override;

private:
    uint8_t &data_;
};

class CdmaSmsReservedParameter : public CdmaSmsSubParameter {
public:
    explicit CdmaSmsReservedParameter(uint8_t id);
    bool Encode(SmsWriteBuffer &pdu) override;
    bool Decode(SmsReadBuffer &pdu) override;
};

class CdmaSmsMessageId : public CdmaSmsSubParameter {
public:
    CdmaSmsMessageId(SmsTeleSvcMsgId &msgId, uint8_t type) {}
    uint8_t GetMessageType()
    {
        return 0;
    };
};

class CdmaSmsUserData : public CdmaSmsSubParameter {
public:
    CdmaSmsUserData(SmsTeleSvcUserData &data, bool &headerInd) {}
};

class CdmaSmsCmasData : public CdmaSmsSubParameter {
public:
    explicit CdmaSmsCmasData(SmsTeleSvcCmasData &data) {}
};

class CdmaSmsAbsoluteTime : public CdmaSmsSubParameter {
public:
    CdmaSmsAbsoluteTime(uint8_t id, SmsTimeAbs &time) {}
};

class CdmaSmsPriorityInd : public CdmaSmsSubParameter {
public:
    explicit CdmaSmsPriorityInd(SmsPriorityIndicator &priority) {}
};

class CdmaSmsPrivacyInd : public CdmaSmsSubParameter {
public:
    explicit CdmaSmsPrivacyInd(SmsPrivacyIndicator &privacy) {}
};

class CdmaSmsReplyOption : public CdmaSmsSubParameter {
public:
    explicit CdmaSmsReplyOption(SmsReplyOption &replyOpt) {}
};

class CdmaSmsAlertPriority : public CdmaSmsSubParameter {
public:
    explicit CdmaSmsAlertPriority(SmsAlertPriority &alertPriority) {}
};

class CdmaSmsLanguageInd : public CdmaSmsSubParameter {
public:
    explicit CdmaSmsLanguageInd(SmsLanguageType &language) {}
};

class CdmaSmsCallbackNumber : public CdmaSmsSubParameter {
public:
    CdmaSmsCallbackNumber() {}
    explicit CdmaSmsCallbackNumber(SmsTeleSvcAddr &address) {}
};

class CdmaSmsDepositIndex : public CdmaSmsSubParameter {
public:
    explicit CdmaSmsDepositIndex(uint16_t &index) {}
};

class CdmaSmsDisplayMode : public CdmaSmsSubParameter {
public:
    explicit CdmaSmsDisplayMode(SmsDisplayMode &mode) {}
};

class CdmaSmsEnhancedVmn : public CdmaSmsSubParameter {
public:
    explicit CdmaSmsEnhancedVmn(SmsEnhancedVmn &vmn) {}
};

class CdmaSmsEnhancedVmnAck : public CdmaSmsSubParameter {
public:
    explicit CdmaSmsEnhancedVmnAck(SmsEnhancedVmnAck &ack) {}
};

class CdmaSmsMessageStatus : public CdmaSmsSubParameter {
public:
    explicit CdmaSmsMessageStatus(SmsStatusCode &status) {}
};

class CdmaSmsNumberMessages : public CdmaSmsSubParameter {
public:
    explicit CdmaSmsNumberMessages(uint32_t &num) {}
};

} // namespace Telephony
} // namespace OHOS
#endif
