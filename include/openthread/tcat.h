/*
 *  Copyright (c) 2023, The OpenThread Authors.
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *  3. Neither the name of the copyright holder nor the
 *     names of its contributors may be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * @file
 * @brief
 *  This file defines the top-level functions for the OpenThread TCAT.
 *
 *  @note
 *   The functions in this module require the build-time feature `OPENTHREAD_CONFIG_TCAT_ENABLE=1`.
 *
 *  @note
 *   To enable the required cipher suite TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256,
 *    MBEDTLS_KEY_EXCHANGE_ECDHE_ECDSA_ENABLED must be enabled in mbedtls-config.h.
 */

#ifndef OPENTHREAD_TCAT_H_
#define OPENTHREAD_TCAT_H_

#include <stdbool.h>
#include <stdint.h>

#include <openthread/error.h>
#include <openthread/instance.h>
#include <openthread/message.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @addtogroup api-ble-secure
 *
 * @brief
 *   This module includes functions that implement TCAT communication.
 *
 *   The functions in this module are available when TCAT feature
 *   (`OPENTHREAD_CONFIG_TCAT_ENABLE`) is enabled.
 *
 * @{
 */

#define OT_TCAT_SERVICE_NAME_MAX_LENGTH \
    15 ///< Maximum string length of a UDP or TCP service name (does not include null char).
#define OT_TCAT_APPLICATION_LAYER_MAX_COUNT 4 ///< Maximum number of application layer service names supported

#define OT_TCAT_ADVERTISEMENT_MAX_LEN 29         ///< Maximum length of TCAT advertisement.
#define OT_TCAT_OPCODE 0x2                       ///< TCAT Advertisement Operation Code.
#define OT_TCAT_MAX_ADVERTISED_DEVICEID_COUNT 10 ///< TCAT max count of advertised Device IDs.
#define OT_TCAT_MAX_ADVERTISED_DEVICEID_SIZE 5   ///< TCAT max size of any type of advertised Device ID.
#define OT_TCAT_MAX_DEVICEID_SIZE 64             ///< TCAT max size of device ID.
#define OT_TCAT_ENABLE_MAX 600                   ///< TCAT_ENABLE_MAX, default max TMF TCAT enable time,  in seconds.

/**
 * Represents TCAT command TLV type.
 */
typedef enum otTcatCommandTlvType
{
    // Command Class General
    OT_TCAT_TLV_RESPONSE_WITH_STATUS      = 0x01, ///< TCAT response with status value TLV
    OT_TCAT_TLV_RESPONSE_WITH_PAYLOAD     = 0x02, ///< TCAT response with payload TLV
    OT_TCAT_TLV_RESPONSE_EVENT            = 0x03, ///< TCAT response event TLV (reserved)
    OT_TCAT_TLV_GET_NETWORK_NAME          = 0x08, ///< TCAT network name query TLV
    OT_TCAT_TLV_DISCONNECT                = 0x09, ///< TCAT disconnect request TLV
    OT_TCAT_TLV_PING                      = 0x0A, ///< TCAT ping request TLV
    OT_TCAT_TLV_GET_DEVICE_ID             = 0x0B, ///< TCAT device ID query TLV
    OT_TCAT_TLV_GET_EXTENDED_PAN_ID       = 0x0C, ///< TCAT extended PAN ID query TLV
    OT_TCAT_TLV_GET_PROVISIONING_URL      = 0x0D, ///< TCAT provisioning URL query TLV
    OT_TCAT_TLV_PRESENT_PSKD_HASH         = 0x10, ///< TCAT rights elevation request TLV using PSKd hash
    OT_TCAT_TLV_PRESENT_PSKC_HASH         = 0x11, ///< TCAT rights elevation request TLV using PSKc hash
    OT_TCAT_TLV_PRESENT_INSTALL_CODE_HASH = 0x12, ///< TCAT rights elevation TLV using install code
    OT_TCAT_TLV_REQUEST_RANDOM_CHALLENGE  = 0x13, ///< TCAT random number challenge query TLV

    // Command Class Commissioning
    OT_TCAT_TLV_SET_ACTIVE_OPERATIONAL_DATASET     = 0x20, ///< TCAT active operational dataset TLV
    OT_TCAT_TLV_SET_ACTIVE_OPERATIONAL_DATASET_ALT = 0x21, ///< TCAT active dataset alt #1 TLV (reserved)
    OT_TCAT_TLV_GET_COMMISSIONER_CERTIFICATE       = 0x25, ///< TCAT commissioner certificate query TLV
    OT_TCAT_TLV_GET_DIAGNOSTIC_TLVS                = 0x26, ///< TCAT diagnostics TLVs query TLV
    OT_TCAT_TLV_START_THREAD_INTERFACE             = 0x27, ///< TCAT start thread interface request TLV
    OT_TCAT_TLV_STOP_THREAD_INTERFACE              = 0x28, ///< TCAT stop thread interface request TLV

    // Command Class Extraction
    OT_TCAT_TLV_GET_ACTIVE_OPERATIONAL_DATASET     = 0x40, ///< TCAT active operational dataset query TLV
    OT_TCAT_TLV_GET_ACTIVE_OPERATIONAL_DATASET_ALT = 0x41, ///< TCAT active dataset alt #1 query TLV (reserved)

    // Command Class Decommissioning
    OT_TCAT_TLV_DECOMMISSION = 0x60, ///< TCAT decommission request TLV

    // Command Class Application
    OT_TCAT_TLV_GET_APPLICATION_LAYERS    = 0x80, ///< TCAT get application layers request TLV
    OT_TCAT_TLV_SEND_APPLICATION_DATA_1   = 0x81, ///< TCAT send application data 1 TLV
    OT_TCAT_TLV_SEND_APPLICATION_DATA_2   = 0x82, ///< TCAT send application data 2 TLV
    OT_TCAT_TLV_SEND_APPLICATION_DATA_3   = 0x83, ///< TCAT send application data 3 TLV
    OT_TCAT_TLV_SEND_APPLICATION_DATA_4   = 0x84, ///< TCAT send application data 4 TLV
    OT_TCAT_TLV_SERVICE_NAME_UDP          = 0x89, ///< TCAT service name UDP sub-TLV (not used as a command)
    OT_TCAT_TLV_SERVICE_NAME_TCP          = 0x8A, ///< TCAT service name TCP sub-TLV (not used as a command)
    OT_TCAT_TLV_SEND_VENDOR_SPECIFIC_DATA = 0x9F, ///< TCAT send vendor specific command or data TLV

    // Command Class CCM
    OT_TCAT_TLV_SET_LDEV_ID_OPERATIONAL_CERT = 0xA0, ///< TCAT set LDevID certificate TLV (reserved)
    OT_TCAT_TLV_SET_LDEV_ID_PRIVATE_KEY      = 0xA1, ///< TCAT set LDevID certificate private key TLV (reserved)
    OT_TCAT_TLV_SET_DOMAIN_CA_CERT           = 0xA2, ///< TCAT set domain CA certificate TLV (reserved)

} otTcatCommandTlvType;

/**
 * Represents TCAT status code.
 */
typedef enum otTcatStatusCode
{
    OT_TCAT_STATUS_SUCCESS       = 0, ///< Command or request was successfully processed
    OT_TCAT_STATUS_UNSUPPORTED   = 1, ///< Requested command or received TLV is not supported
    OT_TCAT_STATUS_PARSE_ERROR   = 2, ///< Request / command could not be parsed correctly
    OT_TCAT_STATUS_VALUE_ERROR   = 3, ///< The value of the transmitted TLV has an error
    OT_TCAT_STATUS_GENERAL_ERROR = 4, ///< An error not matching any other category occurred
    OT_TCAT_STATUS_BUSY          = 5, ///< Command cannot be executed because the resource is busy
    OT_TCAT_STATUS_UNDEFINED  = 6, ///< The requested value, data or service is not defined (currently) or not present
    OT_TCAT_STATUS_HASH_ERROR = 7, ///< The hash value presented by the commissioner was incorrect
    OT_TCAT_STATUS_INVALID_STATE = 8,  ///< The TCAT device is not in a correct state for the given command
    OT_TCAT_STATUS_UNAUTHORIZED  = 16, ///< Sender does not have sufficient authorization for the given command

} otTcatStatusCode;

/**
 * Represents TCAT application protocol options.
 */
typedef enum otTcatApplicationProtocol
{
    OT_TCAT_APPLICATION_PROTOCOL_NONE   = 0,    ///< Message which has been sent without activating the TCAT agent
    OT_TCAT_APPLICATION_PROTOCOL_STATUS = 0x01, /** Message directed to any application protocol indicating a
                                                    response with status value (one byte otTcatStatusCode) */
    OT_TCAT_APPLICATION_PROTOCOL_RESPONSE =
        0x02, ///< Message directed to any application protocol indicating a response with payload
    OT_TCAT_APPLICATION_PROTOCOL_1      = 0x81, ///< Message directed to application protocol 1
    OT_TCAT_APPLICATION_PROTOCOL_2      = 0x82, ///< Message directed to application protocol 2
    OT_TCAT_APPLICATION_PROTOCOL_3      = 0x83, ///< Message directed to application protocol 3
    OT_TCAT_APPLICATION_PROTOCOL_4      = 0x84, ///< Message directed to application protocol 4
    OT_TCAT_APPLICATION_PROTOCOL_VENDOR = 0x9F, ///< Message directed to a vendor specific application protocol

} otTcatApplicationProtocol;

/**
 * Represents a TCAT command class.
 */
typedef enum otTcatCommandClass
{
    OT_TCAT_COMMAND_CLASS_GENERAL         = 0, ///< TCAT commands related to general operations
    OT_TCAT_COMMAND_CLASS_COMMISSIONING   = 1, ///< TCAT commands related to commissioning
    OT_TCAT_COMMAND_CLASS_EXTRACTION      = 2, ///< TCAT commands related to key extraction
    OT_TCAT_COMMAND_CLASS_DECOMMISSIONING = 3, ///< TCAT commands related to de-commissioning
    OT_TCAT_COMMAND_CLASS_APPLICATION     = 4, ///< TCAT commands related to application layer

} otTcatCommandClass;

/**
 * Represents Advertised Device ID type. Used during TCAT advertisement.
 */
typedef enum otTcatAdvertisedDeviceIdType
{
    OT_TCAT_DEVICE_ID_EMPTY,                ///< Advertised device ID type not set
    OT_TCAT_DEVICE_ID_OUI,                  ///< Advertised device ID type IEEE OUI
    OT_TCAT_DEVICE_ID_DISCRIMINATOR,        ///< Advertised device ID type Device Discriminator
    OT_TCAT_DEVICE_ID_IANAPEN,              ///< Advertised device ID type IANA PEN
    OT_TCAT_DEVICE_ID_APPLICATION_PROTOCOL, ///< Advertised device ID type Application Protocol
    OT_TCAT_DEVICE_ID_MAX,                  ///< Advertised device ID max number of types
} otTcatAdvertisedDeviceIdType;

typedef struct otTcatAdvertisedDeviceId
{
    otTcatAdvertisedDeviceIdType mDeviceIdType;
    uint16_t                     mDeviceIdLen;
    uint8_t                      mDeviceId[OT_TCAT_MAX_ADVERTISED_DEVICEID_SIZE];
} otTcatAdvertisedDeviceId;

/**
 * Represents General Device ID type.
 */
typedef struct otTcatGeneralDeviceId
{
    uint16_t mDeviceIdLen;
    uint8_t  mDeviceId[OT_TCAT_MAX_DEVICEID_SIZE];
} otTcatGeneralDeviceId;

/**
 * Pointer to call when application data or vendor-specific data was received over a TCAT TLS connection.
 * The application may generate a response to an incoming TCAT application data packet. The TCAT agent
 * automatically responds with status OT_TCAT_STATUS_UNSUPPORTED if no response has been generated or
 * no handler is defined.
 *
 * @param[in]  aInstance                 A pointer to an OpenThread instance.
 * @param[in]  aMessage                  A pointer to the message.
 * @param[in]  aOffset                   The offset where the application data begins.
 * @param[in]  aTcatApplicationProtocol  The application protocol the message is targeted to.
 * @param[in]  aContext                  A pointer to arbitrary context information.
 */
typedef void (*otHandleTcatApplicationDataReceive)(otInstance               *aInstance,
                                                   const otMessage          *aMessage,
                                                   int32_t                   aOffset,
                                                   otTcatApplicationProtocol aTcatApplicationProtocol,
                                                   void                     *aContext);

/**
 * Pointer to call to notify of a network join/leave operation initiated under guidance of a TCAT Commissioner.
 *
 * @param[in]  aInstance        A pointer to an OpenThread instance.
 * @param[in]  aIsJoin          True if the operation was a network join (OT_TCAT_TLV_START_THREAD_INTERFACE),
 *                              false if it was a network leave (OT_TCAT_TLV_STOP_THREAD_INTERFACE or
 *                              OT_TCAT_TLV_DECOMMISSION).
 * @param[in]  aError           OT_ERROR_NONE if the network join/leave operation was successfully started.
 *                              OT_ERROR_INVALID_STATE if network join was requested but network credentials
 *                                                     were missing or incomplete.
 *                              OT_ERROR_REJECTED if a network join/leave operation was requested, but the
 *                                                TCAT Commissioner is not authorized to make such a request.
 *                              OT_ERROR_SECURITY is reserved for future use for a failed join due to
 *                                                credential mismatch.
 * @param[in]  aContext         A pointer to arbitrary context information.
 */
typedef void (*otHandleTcatJoin)(otInstance *aInstance, bool aIsJoin, otError aError, void *aContext);

/**
 * Pointer to call to control if a TCAT TLV of a specific type is supported. The application may allow
 * or reject processing of a received TCAT command based on an application defined policy.
 * If no handler is defined, all received TCAT commands will be allowed if the respective command class
 * is authorized. If a handler is defined and returns false, the TCAT command will be rejected with status
 * OT_TCAT_STATUS_UNSUPPORTED. If the handler returns true, the TCAT command will be allowed if the respective
 * command class is authorized.
 *
 * @param[in]  aInstance                 A pointer to an OpenThread instance.
 * @param[in]  aTlvType                  A TLV type to be authorized.
 * @param[in]  aContext                  A pointer to arbitrary context information.
 *
 * @returns a boolean value indicating whether the TLV type is supported, based on current policy.
 */
typedef bool (*otHandleTcatTlvSupport)(otInstance *aInstance, otTcatCommandTlvType aTlvType, void *aContext);

/**
 * This structure represents a TCAT vendor information.
 *
 * The content of this structure MUST persist and remain unchanged while a TCAT session is running.
 */
typedef struct otTcatVendorInfo
{
    const char *mProvisioningUrl; ///< Provisioning URL path string
    const char *mVendorName;      ///< Vendor name string
    const char *mVendorModel;     ///< Vendor model string
    const char *mVendorSwVersion; ///< Vendor software version string
    const char *mVendorData;      ///< Vendor specific data string
    const char *mPskdString;      ///< Vendor managed pre-shared key for device
    const char *mInstallCode;     ///< Vendor managed install code string

    /**
     * Vendor managed advertised device ID array. Array is terminated like C string with OT_TCAT_DEVICE_ID_EMPTY.
     */
    const otTcatAdvertisedDeviceId *mAdvertisedDeviceIds;

    /**
     * Vendor managed general device ID array (if NULL: device ID is set to EUI-64 in binary format)
     */
    const otTcatGeneralDeviceId *mGeneralDeviceId;

    /**
     * Array with application service names as C string with maximum length OT_TCAT_SERVICE_NAME_MAX_LENGTH or NULL if
     * not supported.
     */
    const char *mApplicationServiceName[OT_TCAT_APPLICATION_LAYER_MAX_COUNT];

    /**
     * Array with boolean values indicating if the service is of TCP type (otherwise UDP).
     */
    bool mApplicationServiceIsTcp[OT_TCAT_APPLICATION_LAYER_MAX_COUNT];

    bool mKeepActiveAfterJoining; ///< Continue advertising after thread interface has joined a network

    /**
     * Prevent activating advertising indefinitely after the TCAT command OT_TCAT_TLV_STOP_THREAD_INTERFACE or
     * OT_TCAT_TLV_DECOMMISSION has been received.
     */
    bool mDoNotActivateAfterLeaving;

    otHandleTcatTlvSupport mTlvSupportHandler; ///< Optional pointer to a function to control TCAT TLV support

} otTcatVendorInfo;

/**
 * @}
 */

/**
 * @defgroup api-tcat-generic TCAT generic API
 *
 * @brief
 *   Transport-agnostic TCAT functions. These are available whenever a TCAT transport is enabled
 *   (`OPENTHREAD_CONFIG_TCAT_ENABLE`) and operate on the TCAT transport that is selected at build time.
 *
 * @{
 */

/**
 * Sets the TCAT vendor info used by the TCAT agent.
 *
 * Must be called before starting TCAT using #otTcatStart. It may be called again while the TCAT agent is started
 * to update the vendor info. The pointed-to structure must remain valid for the lifetime of the TCAT agent.
 *
 * @param[in] aInstance    A pointer to an OpenThread instance.
 * @param[in] aVendorInfo  A pointer to the vendor info.
 *
 * @retval OT_ERROR_NONE          Successfully set the vendor info.
 * @retval OT_ERROR_INVALID_ARGS  The vendor info is invalid.
 */
otError otTcatSetVendorInfo(otInstance *aInstance, const otTcatVendorInfo *aVendorInfo);

/**
 * Sets the own X.509 certificate and corresponding private key that the TCAT transport uses to authenticate to a
 * TCAT Commissioner.
 *
 * @param[in] aInstance          A pointer to an OpenThread instance.
 * @param[in] aX509Cert          A pointer to the PEM formatted X.509 certificate.
 * @param[in] aX509Length        The length of the certificate, including the null terminator.
 * @param[in] aPrivateKey        A pointer to the PEM formatted private key.
 * @param[in] aPrivateKeyLength  The length of the private key, including the null terminator.
 */
void otTcatSetCertificate(otInstance    *aInstance,
                          const uint8_t *aX509Cert,
                          uint32_t       aX509Length,
                          const uint8_t *aPrivateKey,
                          uint32_t       aPrivateKeyLength);

/**
 * Sets the trusted top level CAs that the TCAT transport uses to validate the certificate of a TCAT Commissioner.
 *
 * @param[in] aInstance                A pointer to an OpenThread instance.
 * @param[in] aX509CaCertificateChain  A pointer to the PEM formatted X.509 CA chain.
 * @param[in] aX509CaCertChainLength   The length of the chain, including the null terminator.
 */
void otTcatSetCaCertificateChain(otInstance    *aInstance,
                                 const uint8_t *aX509CaCertificateChain,
                                 uint32_t       aX509CaCertChainLength);

/**
 * Sets the authentication mode of the TCAT transport.
 *
 * @param[in] aInstance               A pointer to an OpenThread instance.
 * @param[in] aVerifyPeerCertificate  TRUE to verify the certificate of the TCAT Commissioner, FALSE otherwise.
 */
void otTcatSetSslAuthMode(otInstance *aInstance, bool aVerifyPeerCertificate);

/**
 * Starts the TCAT agent and the TCAT transport.
 *
 * The vendor info and certificates must be set beforehand, using #otTcatSetVendorInfo, #otTcatSetCertificate
 * and #otTcatSetCaCertificateChain.
 *
 * @param[in] aInstance        A pointer to an OpenThread instance.
 * @param[in] aUdpPort         The UDP port on which TCAT is hosted, in case of a TCAT transport that uses UDP.
 *                             Ignored by the TCAT over BLE transport.
 * @param[in] aAppDataHandler  A pointer to a function that is called when TCAT application data is received.
 *                             May be NULL.
 * @param[in] aJoinHandler     A pointer to a function that is called when a network join or leave operation is
 *                             performed under guidance of the TCAT Commissioner. May be NULL.
 *
 * @retval OT_ERROR_NONE     Successfully started the TCAT agent.
 * @retval OT_ERROR_ALREADY  The TCAT agent or its transport was already started.
 * @retval OT_ERROR_FAILED   The vendor info was not set, or the transport could not be started.
 */
otError otTcatStart(otInstance                        *aInstance,
                    uint16_t                           aUdpPort,
                    otHandleTcatApplicationDataReceive aAppDataHandler,
                    otHandleTcatJoin                   aJoinHandler);

/**
 * Stops the TCAT agent and the TCAT transport.
 *
 * TCAT can be started again using #otTcatStart.
 *
 * @param[in] aInstance  A pointer to an OpenThread instance.
 */
void otTcatStop(otInstance *aInstance);

/**
 * Indicates whether the TCAT agent is started.
 *
 * @param[in] aInstance  A pointer to an OpenThread instance.
 *
 * @retval TRUE   The TCAT agent is started.
 * @retval FALSE  The TCAT agent is not started.
 */
bool otTcatIsAgentStarted(otInstance *aInstance);

/**
 * Sets the TCAT agent to the active or standby state.
 *
 * @param[in] aInstance    A pointer to an OpenThread instance.
 * @param[in] aActive      TRUE to activate TCAT, FALSE to set TCAT to standby.
 * @param[in] aDelayMs     The delay in milliseconds before activating (0 = immediately). Ignored for standby.
 * @param[in] aDurationMs  The duration in milliseconds to remain active (0 = indefinitely). Ignored for standby.
 *
 * @retval OT_ERROR_NONE           Successfully set the TCAT agent state.
 * @retval OT_ERROR_INVALID_STATE  The TCAT agent is not started, or the requested activation is not possible
 *                                 while a TCAT Commissioner is connected.
 */
otError otTcatSetAgentState(otInstance *aInstance, bool aActive, uint32_t aDelayMs, uint32_t aDurationMs);

/**
 * Sends a TCAT application TLV to the connected TCAT Commissioner.
 *
 * For the TCAT over CoAPS transport, this function can only be called from within the
 * #otHandleTcatApplicationDataReceive callback: the TLV is then added to the response to the TCAT Commissioner's
 * request that is being handled.
 *
 * @param[in] aInstance             A pointer to an OpenThread instance.
 * @param[in] aApplicationProtocol  The application protocol (TLV type) of the TLV to send.
 * @param[in] aBuf                  A pointer to the TLV value.
 * @param[in] aLength               The length of the TLV value in bytes.
 *
 * @retval OT_ERROR_NONE           Successfully enqueued the TLV for sending.
 * @retval OT_ERROR_NO_BUFS        Failed to allocate buffer memory.
 * @retval OT_ERROR_INVALID_STATE  No TCAT Commissioner is connected, or no response is being composed.
 * @retval OT_ERROR_REJECTED       The application protocol is a response with data or status, but no response is
 *                                 pending.
 */
otError otTcatSendApplicationTlv(otInstance               *aInstance,
                                 otTcatApplicationProtocol aApplicationProtocol,
                                 uint8_t                  *aBuf,
                                 uint16_t                  aLength);

/**
 * @}
 */

#ifdef __cplusplus
} // extern "C"
#endif

#endif // OPENTHREAD_TCAT_H_
