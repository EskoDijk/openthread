/*
 *  Copyright (c) 2026, The OpenThread Authors.
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
 *   This file includes functions for the credentials of a Thread Commercial Commissioning Mode
 *   (CCM) device.
 */

#ifndef OPENTHREAD_CCM_H_
#define OPENTHREAD_CCM_H_

#include <stdbool.h>
#include <stdint.h>

#include <openthread/error.h>
#include <openthread/instance.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @addtogroup api-ccm
 *
 * @brief
 *   This module includes functions to provision and inspect the X.509 credentials of a Thread
 *   Commercial Commissioning Mode (CCM) device.
 *
 * @note
 *   The functions in this module require `OPENTHREAD_CONFIG_CCM_ENABLE=1`.
 *
 * @{
 */

/**
 * Represents the IDevID (Initial Device Identifier, per IEEE 802.1AR) credentials of a device.
 *
 * These are the factory-installed credentials used for cBRSKI onboarding: the device presents
 * @p mCert with @p mPrivateKey to the Registrar, and uses @p mCaCert as the Trust Anchor to
 * verify the Voucher that is signed by the manufacturer's MASA.
 *
 * The referenced buffers are NOT copied. They MUST remain valid, and MUST NOT be modified, for
 * as long as OpenThread may use them. Typically these point to storage owned by the platform.
 *
 * For PEM-encoded data, each length MUST include the terminating NUL byte. PEM is detected only
 * in a NUL-terminated buffer; without the NUL, the data is parsed as DER and parsing fails.
 */
typedef struct otCcmIdevid
{
    const uint8_t *mCert;             ///< The IDevID X.509 certificate, in PEM or DER format.
    const uint8_t *mPrivateKey;       ///< The private key belonging to @p mCert, in PEM or DER format.
    const uint8_t *mCaCert;           ///< The MASA CA certificate, in PEM or DER format.
    uint16_t       mCertLength;       ///< Length of @p mCert; includes the NUL terminator if PEM.
    uint16_t       mPrivateKeyLength; ///< Length of @p mPrivateKey; includes the NUL terminator if PEM.
    uint16_t       mCaCertLength;     ///< Length of @p mCaCert; includes the NUL terminator if PEM.
} otCcmIdevid;

/**
 * Sets the IDevID credentials that this device uses for CCM onboarding.
 *
 * Replaces the IDevID that is built into the firmware image. Intended to be called once during
 * startup, by the platform or the application, for a device whose identity is not compiled in.
 *
 * Must be called before a CCM join operation is started with #otJoinerStartCcm. The credentials
 * that are set at the time the operation starts are the ones that are used.
 *
 * Each certificate and the private key are parsed to validate them, and the private key is
 * checked to belong to the IDevID certificate. The credentials are only stored if all of these
 * checks pass, so a failure leaves the previous credentials in place.
 *
 * @param[in]  aInstance  A pointer to an OpenThread instance.
 * @param[in]  aIdevid    The IDevID credentials to use. The buffers it points to are not copied,
 *                        see #otCcmIdevid.
 *
 * @retval OT_ERROR_NONE          Successfully set the IDevID credentials.
 * @retval OT_ERROR_INVALID_ARGS  A certificate or key pointer was NULL, or a length was zero.
 * @retval OT_ERROR_PARSE         A certificate or the private key could not be parsed. For PEM
 *                                data, check that the length includes the NUL terminator.
 * @retval OT_ERROR_SECURITY      The private key does not belong to the IDevID certificate.
 */
otError otCcmSetIdevid(otInstance *aInstance, const otCcmIdevid *aIdevid);

/**
 * Indicates whether this device stores an LDevID certificate.
 *
 * A device obtains an LDevID by completing enrollment, for example Autonomous Enrollment (AE)
 * using cBRSKI. An LDevID is a prerequisite for Network Key Provisioning (NKP).
 *
 * @param[in]  aInstance  A pointer to an OpenThread instance.
 *
 * @retval TRUE   The device stores an LDevID certificate.
 * @retval FALSE  The device does not store an LDevID certificate.
 */
bool otCcmHasLdevidCert(otInstance *aInstance);

/**
 * Gets the LDevID certificate that this device stores.
 *
 * @param[in]   aInstance  A pointer to an OpenThread instance.
 * @param[out]  aLength    A pointer to where the certificate length is placed. MUST NOT be NULL.
 *                         Is set to zero if the device stores no LDevID certificate.
 *
 * @returns A pointer to the LDevID certificate, in DER format.
 */
const uint8_t *otCcmGetLdevidCert(otInstance *aInstance, uint16_t *aLength);

/**
 * Gets the Thread Domain Name of this device.
 *
 * The Domain Name is taken from a SubjectAltName field of the LDevID certificate. It is the
 * default Domain Name if the device stores no LDevID certificate.
 *
 * @param[in]  aInstance  A pointer to an OpenThread instance.
 *
 * @returns A pointer to the NUL-terminated Thread Domain Name.
 */
const char *otCcmGetDomainName(otInstance *aInstance);

/**
 * Clears the operational credentials of this device.
 *
 * Clears the operational certificate (LDevID), its private key, and the Domain CA certificates.
 * The IDevID credentials are not affected.
 *
 * @param[in]  aInstance  A pointer to an OpenThread instance.
 */
void otCcmClearCredentials(otInstance *aInstance);

/**
 * @}
 */

#ifdef __cplusplus
} // end of extern "C"
#endif

#endif // OPENTHREAD_CCM_H_
