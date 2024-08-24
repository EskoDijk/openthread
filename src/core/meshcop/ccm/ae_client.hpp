/*
 *  Copyright (c) 2019, The OpenThread Authors.
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
 *   This file includes definitions for a client of a EST protocol.
 *   Ref: https://tools.ietf.org/html/draft-ietf-ace-coap-est-11
 */

#ifndef EST_CLIENT_HPP_
#define EST_CLIENT_HPP_

#include "openthread-core-config.h"

#if OPENTHREAD_CONFIG_CCM_ENABLE

#include "credentials.hpp"
#include "coap/coap_secure.hpp"
#include "common/locator.hpp"
#include "mbedtls/ecp.h"
#include "mbedtls/pk.h"
#include "mbedtls/entropy.h"

namespace ot {

namespace MeshCoP {

class AeClient : InstanceLocator
{
public:
    typedef void (*Callback)(otError aError, void *aContext);

    explicit AeClient(Instance &aInstance);

    /**
     * Starts Autonomous Enrollment (AE) using cBRSKI on a connected CoAPs session to Registrar.
     *
     * @param[in]  aConnectedCoapSecure  A connected CoAPs session, using IDevID identity.
     * @param[in]  aCallback             A callback function to be called at the end of enrollment (nullable).
     * @param[in]  aContext              A context for the callback function (nullable).
     *
     * The aCallback is guaranteed to be called whether the enrollment succeeds or not.
     */
    void StartEnroll(Coap::CoapSecure &aConnectedCoapSecure, Callback aCallback, void *aContext);

    bool IsBusy() const { return mCoapSecure != NULL; }

    /**
     * Cleanup all temporary data structures and finish operation of this client.
     *
     * @param[in]  aError                Error code indicating cause of finishing.
     */
    void Finish(otError aError);

private:
    static const size_t kMaxCsrSize         = 512;
    static const size_t kMaxVoucherSize     = 1024;	// MASA service of Vendor defines max voucher size.
    static const size_t kVoucherNonceLength = 8;

    static const mbedtls_pk_type_t    kOperationalKeyType = MBEDTLS_PK_ECKEY;
    static const mbedtls_ecp_group_id kEcpGroupId         = MBEDTLS_ECP_DP_SECP256R1;

    /**
     * The constrained voucher request SID values defined in
     * draft-ietf-anima-constrained-voucher-NN
     */
    struct VoucherRequestSID
    {
        static const int kVoucher = 2501;
        static const int kAssertion = kVoucher + 1;
        static const int kCreatedOn = kVoucher + 2;
        static const int kDomainCertRevChecks = kVoucher + 3;
        static const int kExpiresOn = kVoucher + 4;
        static const int kIdevidIssuer = kVoucher + 5;
        static const int kLastRenewalDate = kVoucher + 6;
        static const int kNonce = kVoucher + 7;
        static const int kPinnedDomainCert = kVoucher + 8;
        static const int kPriorSignedVoucherReq = kVoucher + 9;
        static const int kProxRegistrarCert = kVoucher + 10;
        static const int kSha256RegistrarSPKI = kVoucher + 11;
        static const int kProxRegistrarSPKI = kVoucher + 12;
        static const int kSerialNumber = kVoucher + 13;

    private:
        VoucherRequestSID() {}
    };

    /**
     * The constrained voucher SID values defined in
     * draft-ietf-anima-constrained-voucher-NN
     */
    struct VoucherSID
    {
        static const int kVoucher = 2451;
        static const int kAssertion = kVoucher + 1;
        static const int kCreatedOn = kVoucher + 2;
        static const int kDomainCertRevChecks = kVoucher + 3;
        static const int kExpiresOn = kVoucher + 4;
        static const int kIdevidIssuer = kVoucher + 5;
        static const int kLastRenewalDate = kVoucher + 6;
        static const int kNonce = kVoucher + 7;
        static const int kPinnedDomainCert = kVoucher + 8;
        static const int kPinnedDomainSPKI = kVoucher + 9;
        static const int kPinnedSha256DomainSPKI = kVoucher + 10;
        static const int kSerialNumber = kVoucher + 11;

    private:
        VoucherSID() {}
    };

    enum VoucherAssertion
    {
        kVerified  = 0,
        kLogged    = 1,
        kProximity = 2,
    };

    struct VoucherRequest
    {
        int     mAssertion;
        uint8_t mNonce[kVoucherNonceLength];
        char    mSerialNumber[Credentials::kMaxSerialNumberLength + 1];
        uint8_t mRegPubKey[Credentials::kMaxKeyLength];
        size_t  mRegPubKeyLength;
    };

    otError SendVoucherRequest();
    otError CreateVoucherRequest(VoucherRequest &aVoucherReq, mbedtls_x509_crt &aRegistrarCert);
    otError SignVoucherRequest(uint8_t *aBuf, size_t &aLength, size_t aBufLength, const VoucherRequest &aVoucherReq);
    otError SerializeVoucherRequest(uint8_t *             aBuf,
                                    size_t &              aLength,
                                    size_t                aMaxLength,
                                    const VoucherRequest &aVoucherReq);

    static void HandleVoucherResponse(void *               aContext,
                                      otMessage *          aMessage,
                                      const otMessageInfo *aMessageInfo,
                                      otError              aResult);
    void        HandleVoucherResponse(Coap::Message &aMessage, const Ip6::MessageInfo *aMessageInfo, otError aResult);

    void ReportStatus(const char *aUri, otError aError, const char *aContext);

    otError     SendCaCertsRequest();
    static void HandleCaCertsResponse(void *               aContext,
                                      otMessage *          aMessage,
                                      const otMessageInfo *aMessageInfo,
                                      otError              aResult);
    void        HandleCaCertsResponse(Coap::Message &aMessage, const Ip6::MessageInfo *aMessageInfo, otError aResult);

    otError     SendCsrRequest();
    static void HandleCsrResponse(void *               aContext,
                                  otMessage *          aMessage,
                                  const otMessageInfo *aMessageInfo,
                                  otError              aResult);
    void        HandleCsrResponse(Coap::Message &aMessage, const Ip6::MessageInfo *aMessageInfo, otError aResult);

    /**
     * Process a received Voucher, check contents, verify security, and store any relevant info from it in this EstClient.
     * Stores the pinned-domain-cert in 'mPinnedDomainCert'.
     */
    otError ProcessVoucher(const uint8_t *aVoucher, size_t aVoucherLength);

    /**
     * Process a received LDevID operational cert, verify syntax, and store as 'mOperationalCert' in this EstClient.
     * @param [out] isNeedCaCertsRequest set True if a CA Certs (/crts) request is needed additionally to obtain
     *                                   the Domain CA cert needed to validate the LDevID. False, if not needed.
     */
    otError ProcessOperationalCert(const uint8_t *aCert,
    							   size_t aLength,
								   bool &isNeedCaCertsRequest);

    /**
     * Store the LDevID, Domain CA, toplevel Domain CA (if present), all into the local Credentials store (Trust Store).
     */
    otError ProcessCertsIntoTrustStore();

    // FIXME used?
    otError GetPeerCertificate(mbedtls_x509_crt &aCert);

    otError GenerateECKey(mbedtls_pk_context &aKey);

    // The data is written to the end of the buffer
    otError CreateCsrData(mbedtls_pk_context &aKey,
                          const char *        aSubjectName,
                          unsigned char *     aBuf,
                          size_t              aBufLen,
                          size_t &            aCsrLen);

    /**
     * Compare two encoded certs for equality.
     *
     * @return true if equal, false otherwise (e.g. if one or both is NULL).
     */
    bool IsCertsEqual(const mbedtls_x509_crt * cert1, const mbedtls_x509_crt * cert2);

    void PrintEncodedCert(const uint8_t * aCert, size_t aLength);

    Coap::CoapSecure *mCoapSecure;
    Callback          mCallback;
    void *            mCallbackContext;

    // whether current process is a reenrollment (true) or not (false)
    bool 			 mIsDoingReenroll;

    // the voucher request object
    VoucherRequest * mVoucherReq;

    // peer Registrar cert obtained during this session
    mbedtls_x509_crt mRegistrarCert;

    // pinned domain cert, if any, from Voucher, obtained during this session. May or may not be
    // equal to mDomainCACert.
    mbedtls_x509_crt mPinnedDomainCert;

    // Domain CA cert, if any, from EST /crts obtained during this session or from own Trust Store.
    mbedtls_x509_crt mDomainCaCert;

    // LDevID operational cert, if any, obtained with EST (/sen, /sren) during this session
    mbedtls_x509_crt mOperationalCert;
    //uint8_t  		 mEncodedOperationalCert[Credentials::kMaxCertLength];
    //size_t 		 	 mEncodedOperationalCertLength;

    // LDevID operational public key generated during this session.
    mbedtls_pk_context mOperationalKey;

    mbedtls_entropy_context mEntropyContext;
};

} // namespace MeshCoP
} // namespace ot

#endif // OPENTHREAD_CONFIG_CCM_ENABLE

#endif // EST_CLIENT_HPP_
