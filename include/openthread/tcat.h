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

#define OT_TCAT_ADVERTISEMENT_MAX_LEN 29       ///< Maximum length of TCAT advertisement.
#define OT_TCAT_OPCODE 0x2                     ///< TCAT Advertisement Operation Code.
#define OT_TCAT_MAX_ADVERTISED_DEVICEID_SIZE 5 ///< TCAT max size of any type of advertised Device ID.
#define OT_TCAT_MAX_DEVICEID_SIZE 64           ///< TCAT max size of device ID.
#define OT_TCAT_ENABLE_MAX 600                 ///< TCAT_ENABLE_MAX, default max TMF TCAT enable time,  in seconds.

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
    OT_TCAT_DEVICE_ID_EMPTY         = 0, ///< Advertised device ID type not set
    OT_TCAT_DEVICE_ID_OUI24         = 1, ///< Advertised device ID type IEEE OUI-24
    OT_TCAT_DEVICE_ID_OUI36         = 2, ///< Advertised device ID type IEEE OUI-36
    OT_TCAT_DEVICE_ID_DISCRIMINATOR = 3, ///< Advertised device ID type Device Discriminator
    OT_TCAT_DEVICE_ID_IANAPEN       = 4, ///< Advertised device ID type IANA PEN
    OT_TCAT_DEVICE_ID_MAX           = 5, ///< Advertised device ID max number of types
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
    const otTcatAdvertisedDeviceId
        *mAdvertisedDeviceIds;                     /** Vendor managed advertised device ID array.
                                                       Array is terminated like C string with OT_TCAT_DEVICE_ID_EMPTY */
    const otTcatGeneralDeviceId *mGeneralDeviceId; /** Vendor managed general device ID array.
                                                       (if NULL: device ID is set to EUI-64 in binary format) */
    const char *mApplicationServiceName[OT_TCAT_APPLICATION_LAYER_MAX_COUNT]; /** Array with application service names
                                                                                  as C string with maximum length
                                                                                  OT_TCAT_SERVICE_NAME_MAX_LENGTH or
                                                                                  NULL if not supported */
    bool mApplicationServiceIsTcp[OT_TCAT_APPLICATION_LAYER_MAX_COUNT];       /** Array with boolean values indicating
                                                                                  if the service is of TCP type (otherwise
                                                                                  UDP) */

} otTcatVendorInfo;

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
 * Pointer to call to notify the completion of a network join/leave operation performed under
 * guidance of a TCAT Commissioner.
 *
 * @param[in]  aError           OT_ERROR_NONE if the network join/leave operation was successfully started.
 *                              OT_ERROR_INVALID_STATE if network join was requested but network credentials
 *                                                     were missing or incomplete.
 *                              OT_ERROR_REJECTED if a network join/leave operation was requested, but the
 *                                                TCAT Commissioner is not authorized to make such a request.
 *                              OT_ERROR_SECURITY is reserved for future use for a failed join due to
 *                                                credential mismatch.
 * @param[in]  aContext         A pointer to arbitrary context information.
 */
typedef void (*otHandleTcatJoin)(otError aError, void *aContext);

/**
 * @}
 */

/**
 * @defgroup api-tcat-generic TCAT generic API
 *
 * @brief   Transport-agnostic TCAT functions. Available whenever any TCAT transport is enabled
 *          (`OPENTHREAD_CONFIG_TCAT_ENABLE`).
 *
 * @{
 */

/**
 * Sets the TCAT vendor info used by the TCAT agent.
 *
 * Must be called before starting any TCAT transport. The pointed-to structure must remain
 * valid for the lifetime of the agent.
 *
 * @param[in] aInstance    A pointer to an OpenThread instance.
 * @param[in] aVendorInfo  Pointer to the vendor info.
 *
 * @retval OT_ERROR_NONE          Successfully set.
 * @retval OT_ERROR_INVALID_ARGS  Invalid vendor info.
 */
otError otTcatSetVendorInfo(otInstance *aInstance, const otTcatVendorInfo *aVendorInfo);

/**
 * Sets the TCAT agent into active or standby state.
 *
 * @param[in] aInstance    A pointer to an OpenThread instance.
 * @param[in] aActive      TRUE to activate, FALSE to go to standby.
 * @param[in] aDelayMs     Delay in ms before activating (0 = immediate). Ignored when going to standby.
 * @param[in] aDurationMs  Duration in ms to stay active (0 = indefinite). Ignored when going to standby.
 *
 * @retval OT_ERROR_NONE           Successfully applied.
 * @retval OT_ERROR_INVALID_STATE  TCAT agent not started.
 */
otError otTcatSetAgentState(otInstance *aInstance, bool aActive, uint32_t aDelayMs, uint32_t aDurationMs);

/**
 * Sends a TCAT application TLV over the currently active transport.
 *
 * Routes to whichever TCAT transport was compiled in. If both BLE and UDP are compiled in this
 * call goes to BLE; use the transport-specific API if explicit routing is needed.
 *
 * @param[in] aInstance             A pointer to an OpenThread instance.
 * @param[in] aApplicationProtocol  Application protocol selector.
 * @param[in] aBuf                  Payload buffer.
 * @param[in] aLength               Payload length in bytes.
 *
 * @retval OT_ERROR_NONE           Successfully enqueued.
 * @retval OT_ERROR_NO_BUFS        Buffer allocation failure.
 * @retval OT_ERROR_INVALID_STATE  Not connected.
 */
otError otTcatSendApplicationTlv(otInstance               *aInstance,
                                 otTcatApplicationProtocol aApplicationProtocol,
                                 uint8_t                  *aBuf,
                                 uint16_t                  aLength);

/**
 * Sets the own x509 certificate and private key for all enabled TCAT transports.
 *
 * @param[in] aInstance          A pointer to an OpenThread instance.
 * @param[in] aX509Cert          PEM-encoded X.509 certificate.
 * @param[in] aX509Length        Certificate length (including null terminator if PEM).
 * @param[in] aPrivateKey        PEM-encoded private key.
 * @param[in] aPrivateKeyLength  Private key length.
 */
void otTcatSetCertificate(otInstance    *aInstance,
                          const uint8_t *aX509Cert,
                          uint32_t       aX509Length,
                          const uint8_t *aPrivateKey,
                          uint32_t       aPrivateKeyLength);

/**
 * Sets the trusted CA certificate chain for all enabled TCAT transports.
 *
 * @param[in] aInstance                A pointer to an OpenThread instance.
 * @param[in] aX509CaCertificateChain  PEM-encoded CA chain.
 * @param[in] aX509CaCertChainLength   Chain length.
 */
void otTcatSetCaCertificateChain(otInstance    *aInstance,
                                 const uint8_t *aX509CaCertificateChain,
                                 uint32_t       aX509CaCertChainLength);

/**
 * Sets peer certificate verification mode for all enabled TCAT transports.
 *
 * @param[in] aInstance              A pointer to an OpenThread instance.
 * @param[in] aVerifyPeerCertificate TRUE to require peer certificate verification.
 */
void otTcatSetSslAuthMode(otInstance *aInstance, bool aVerifyPeerCertificate);

/**
 * Starts the TCAT agent on all compiled-in transports.
 *
 * Vendor info and certificates must be set beforehand via `otTcatSetVendorInfo`,
 * `otTcatSetCertificate`, etc.
 *
 * @param[in] aInstance        A pointer to an OpenThread instance.
 * @param[in] aUdpPort         UDP port for the DTLS transport (0 = ephemeral). Ignored for BLE.
 * @param[in] aVendorInfo      Pointer to vendor info (must remain valid while running).
 * @param[in] aAppDataHandler  Callback invoked when application-layer TCAT data arrives.
 * @param[in] aJoinHandler     Callback invoked when a network join/leave completes.
 *
 * @retval OT_ERROR_NONE          Successfully started.
 * @retval OT_ERROR_ALREADY       Already started.
 * @retval OT_ERROR_INVALID_ARGS  Invalid vendor info.
 */
otError otTcatStart(otInstance                        *aInstance,
                    uint16_t                           aUdpPort,
                    const otTcatVendorInfo            *aVendorInfo,
                    otHandleTcatApplicationDataReceive aAppDataHandler,
                    otHandleTcatJoin                   aJoinHandler);

/**
 * Stops the TCAT agent and all active transports.
 *
 * @param[in] aInstance  A pointer to an OpenThread instance.
 */
void otTcatStop(otInstance *aInstance);

/**
 * @}
 */

/**
 * @defgroup api-tcat-udp TCAT over UDP/DTLS
 *
 * @brief   Transport-specific send function for TCAT over UDP/DTLS. Requires
 *          `OPENTHREAD_CONFIG_TCAT_UDP_ENABLE`. All other TCAT configuration and
 *          lifecycle management is done through the generic `api-tcat-generic` API.
 *
 * @{
 */

/**
 * Sends a TCAT application TLV over the active DTLS session.
 *
 * @param[in] aInstance             A pointer to an OpenThread instance.
 * @param[in] aApplicationProtocol  Application protocol selector.
 * @param[in] aBuf                  Payload buffer.
 * @param[in] aLength               Payload length in bytes.
 *
 * @retval OT_ERROR_NONE           Successfully enqueued.
 * @retval OT_ERROR_NO_BUFS        Buffer allocation failure.
 * @retval OT_ERROR_INVALID_STATE  Not connected.
 */
otError otTcatUdpSendApplicationTlv(otInstance               *aInstance,
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
