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

#ifndef SMS_SERVICE_INTERFACE_H
#define SMS_SERVICE_INTERFACE_H

#include "i_delivery_short_message_callback.h"
#include "i_send_short_message_callback.h"
#include "short_message.h"

#include "iremote_broker.h"

namespace OHOS {
namespace Telephony {
class ISmsServiceInterface : public IRemoteBroker {
public:
    using MessageID = enum {
        TEXT_BASED_SMS_DELIVERY = 0,
        SEND_SMS_TEXT_WITHOUT_SAVE,
        DATA_BASED_SMS_DELIVERY,
        SET_SMSC_ADDRESS,
        GET_SMSC_ADDRESS,
        ADD_SIM_MESSAGE,
        DEL_SIM_MESSAGE,
        UPDATE_SIM_MESSAGE,
        GET_ALL_SIM_MESSAGE,
        SET_CB_CONFIG,
        SET_DEFAULT_SMS_SLOT_ID,
        GET_DEFAULT_SMS_SLOT_ID,
        SPLIT_MESSAGE,
        GET_SMS_SEGMENTS_INFO,
        GET_IMS_SHORT_MESSAGE_FORMAT,
        IS_IMS_SMS_SUPPORTED,
        HAS_SMS_CAPABILITY,
    };

    /**
     * @brief SimMessageStatus
     * from 3GPP TS 27.005 V4.1.0 (2001-09) section 3 Parameter Definitions
     */
    using SimMessageStatus = enum {
        SIM_MESSAGE_STATUS_UNREAD = 0, // 0 REC UNREAD received unread message
        SIM_MESSAGE_STATUS_READ = 1, // 1 REC READ received read message
        SIM_MESSAGE_STATUS_UNSENT = 2, // 2 "STO UNSENT" stored unsent message (only applicable to SMs)
        SIM_MESSAGE_STATUS_SENT = 3, // 3 "STO SENT" stored sent message (only applicable to SMs)
    };

    /**
     * @brief SmsEncodingScheme
     * from  3GPP TS 23.038 [9] DCS
     */
    enum class SmsEncodingScheme {
        SMS_ENCODING_UNKNOWN = 0,
        SMS_ENCODING_7BIT,
        SMS_ENCODING_8BIT,
        SMS_ENCODING_16BIT,
    };

    using SmsSegmentsInfo = struct {
        int32_t msgSegCount = 0;
        int32_t msgEncodingCount = 0;
        int32_t msgRemainCount = 0;
        enum class SmsSegmentCodeScheme {
            SMS_ENCODING_UNKNOWN = 0,
            SMS_ENCODING_7BIT,
            SMS_ENCODING_8BIT,
            SMS_ENCODING_16BIT,
        } msgCodeScheme = SmsSegmentCodeScheme::SMS_ENCODING_UNKNOWN;
    };

    virtual ~ISmsServiceInterface() = default;

    /**
     * @brief SendMessage
     * Sends a text or data SMS message.
     * @param slotId [in]
     * @param desAddr [in]
     * @param scAddr [in]
     * @param text [in]
     * @param sendCallback [in]
     * @param deliverCallback [in]
     */
    virtual void SendMessage(int32_t slotId, const std::u16string desAddr, const std::u16string scAddr,
        const std::u16string text, const sptr<ISendShortMessageCallback> &sendCallback,
        const sptr<IDeliveryShortMessageCallback> &deliverCallback) = 0;

    /**
     * @brief SendMessage
     * Sends a text or data SMS message.
     * @param slotId [in]
     * @param desAddr [in]
     * @param scAddr [in]
     * @param port [in]
     * @param data [in]
     * @param dataLen [in]
     * @param sendCallback [in]
     * @param deliverCallback [in]
     */
    virtual void SendMessage(int32_t slotId, const std::u16string desAddr, const std::u16string scAddr,
        uint16_t port, const uint8_t *data, uint16_t dataLen, const sptr<ISendShortMessageCallback> &sendCallback,
        const sptr<IDeliveryShortMessageCallback> &deliverCallback) = 0;

    /**
     * @brief SetSmscAddr
     * Sets the address for the Short Message Service Center (SMSC) based on a specified slot ID.
     * @param slotId [in]
     * @param scAddr [in]
     * @return true
     * @return false
     */
    virtual bool SetSmscAddr(int32_t slotId, const std::u16string &scAddr) = 0;

    /**
     * @brief GetSmscAddr
     * Obtains the SMSC address based on a specified slot ID.
     * @param slotId [in]
     * @return std::u16string
     */
    virtual std::u16string GetSmscAddr(int32_t slotId) = 0;

    /**
     * @brief AddSimMessage
     * Add a sms to sim card.
     * @param slotId [in]
     * @param smsc [in]
     * @param pdu [in]
     * @param status [in]
     * @return true
     * @return false
     */
    virtual bool AddSimMessage(
        int32_t slotId, const std::u16string &smsc, const std::u16string &pdu, SimMessageStatus status) = 0;

    /**
     * @brief DelSimMessage
     * Delete a sms in the sim card.
     * @param slotId [in]
     * @param msgIndex [in]
     * @return true
     * @return false
     */
    virtual bool DelSimMessage(int32_t slotId, uint32_t msgIndex) = 0;

    /**
     * @brief UpdateSimMessage
     * Update a sms in the sim card.
     * @param slotId [in]
     * @param msgIndex [in]
     * @param newStatus [in]
     * @param pdu [in]
     * @param smsc [in]
     * @return true
     * @return false
     */
    virtual bool UpdateSimMessage(int32_t slotId, uint32_t msgIndex, SimMessageStatus newStatus,
        const std::u16string &pdu, const std::u16string &smsc) = 0;

    /**
     * @brief GetAllSimMessages
     * Get sim card all the sms.
     * @param slotId [in]
     * @return std::vector<ShortMessage>
     */
    virtual std::vector<ShortMessage> GetAllSimMessages(int32_t slotId) = 0;

    /**
     * @brief SetCBConfig
     * Configure a cell broadcast in a certain band range.
     * @param slotId [in]
     * @param enable [in]
     * @param fromMsgId [in]
     * @param toMsgId [in]
     * @param netType [in]
     * @return true
     * @return false
     */
    virtual bool SetCBConfig(
        int32_t slotId, bool enable, uint32_t fromMsgId, uint32_t toMsgId, uint8_t netType) = 0;

    /**
     * @brief SetDefaultSmsSlotId
     * Set the Default Sms Slot Id To SmsService
     * @param slotId [in]
     * @return true
     * @return false
     */
    virtual bool SetDefaultSmsSlotId(int32_t slotId) = 0;

    /**
     * @brief GetDefaultSmsSlotId
     * Get the Default Sms Slot Id From SmsService
     * @return int32_t
     */
    virtual int32_t GetDefaultSmsSlotId() = 0;

    /**
     * @brief SplitMessage
     * calculate Sms Message Split Segment count
     * @param message [in]
     * @return std::vector<std::u16string>
     */
    virtual std::vector<std::u16string> SplitMessage(const std::u16string &message) = 0;

    /**
     * @brief GetSmsSegmentsInfo
     * calculate the Sms Message Segments Info
     * @param slotId [in]
     * @param message [in]
     * @param force7BitCode [in]
     * @param info [out]
     * @return true
     * @return false
     */
    virtual bool GetSmsSegmentsInfo(
        int32_t slotId, const std::u16string &message, bool force7BitCode, SmsSegmentsInfo &info) = 0;

    /**
     * @brief IsImsSmsSupported
     * Check Sms Is supported Ims newtwork
     * Hide this for inner system use
     * @return true
     * @return false
     */
    virtual bool IsImsSmsSupported() = 0;

    /**
     * @brief GetImsShortMessageFormat
     * Get the Ims Short Message Format 3gpp/3gpp2
     * Hide this for inner system use
     * @return std::u16string
     */
    virtual std::u16string GetImsShortMessageFormat() = 0;

    /**
     * @brief HasSmsCapability
     * Check whether it is supported Sms Capability
     * Hide this for inner system use
     * @return true
     * @return false
     */
    virtual bool HasSmsCapability() = 0;

public:
    DECLARE_INTERFACE_DESCRIPTOR(u"OHOS.Telephony.ISmsServiceInterface");
};
} // namespace Telephony
} // namespace OHOS
#endif
