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
 *   This file includes definitions for the TCAT over CoAPS server.
 */

#ifndef OT_CORE_MESHCOP_TCAT_COAPS_SERVER_HPP_
#define OT_CORE_MESHCOP_TCAT_COAPS_SERVER_HPP_

#include "openthread-core-config.h"

#if OPENTHREAD_CONFIG_TCAT_COAPS_ENABLE

#if !OPENTHREAD_CONFIG_SECURE_TRANSPORT_ENABLE
#error "OPENTHREAD_CONFIG_TCAT_COAPS_ENABLE requires OPENTHREAD_CONFIG_SECURE_TRANSPORT_ENABLE"
#endif

#include "coap/coap.hpp"
#include "coap/coap_secure.hpp"
#include "common/error.hpp"
#include "common/message.hpp"
#include "meshcop/secure_transport.hpp"
#include "meshcop/tcat_agent.hpp"
#include "net/ip6_address.hpp"

namespace ot {
namespace MeshCoP {

/**
 * Implements the TCAT over CoAPS server, i.e. the TCAT transport using CoAP over DTLS over UDP.
 *
 * The server is hosted on a UDP port that is accessible without link-layer (MAC) security, so that it can be reached
 * by a TCAT Commissioner over the Thread radio link also when the TCAT Device is not yet part of a Thread Network.
 * A single TCAT Commissioner can be connected at a time. The DTLS session uses the same certificates and the same
 * cipher suite, "ECDHE ECDSA with AES 128 GCM SHA256", as the TLS session of TCAT over BLE.
 *
 * TCAT command TLVs are carried in the payload of a confirmable CoAP POST request to the TCAT resource. The
 * corresponding TCAT response TLVs are returned in the payload of the piggybacked CoAP 2.04 (Changed) response. A
 * request and its response must each fit in a single CoAP message.
 *
 * Similar to `Coap::ApplicationCoapSecure`, this class is at the same time the DTLS transport, the extension of
 * that transport providing the certificate functions, and the single CoAP secure session.
 */
class TcatCoapServer : public Dtls::Transport, public Dtls::Transport::Extension, public Coap::SecureSession
{
public:
    /**
     * Initializes the TCAT over CoAPS server.
     *
     * @param[in]  aInstance  A reference to the OpenThread instance.
     */
    explicit TcatCoapServer(Instance &aInstance);

    /**
     * Starts the TCAT over CoAPS server and the TCAT agent.
     *
     * The TCAT vendor info and certificates must be set before calling this method.
     *
     * @param[in]  aPort             The UDP port to host the server on.
     * @param[in]  aAppDataCallback  A pointer to a function that is called when TCAT application data is received.
     * @param[in]  aJoinHandler      A pointer to a function that is called when a network join or leave operation
     *                               is performed.
     *
     * @retval kErrorNone     Successfully started the server.
     * @retval kErrorAlready  The server was already started.
     * @retval kErrorFailed   The TCAT vendor info was not set, or the server could not be started.
     */
    Error Start(uint16_t                          aPort,
                TcatAgent::AppDataReceiveCallback aAppDataCallback,
                TcatAgent::JoinCallback           aJoinHandler);

    /**
     * Stops the TCAT over CoAPS server and the TCAT agent.
     */
    void Stop(void);

    /**
     * Indicates whether or not the server is started.
     *
     * @retval TRUE   The server is started.
     * @retval FALSE  The server is not started.
     */
    bool IsStarted(void) const { return !Dtls::Transport::IsClosed(); }

    /**
     * Sets the TCAT vendor info.
     *
     * @param[in]  aVendorInfo  A reference to the vendor info. It must remain valid while the TCAT agent is in use.
     *
     * @retval kErrorNone         Successfully set the vendor info.
     * @retval kErrorInvalidArgs  The vendor info is invalid.
     */
    Error SetTcatVendorInfo(const TcatAgent::VendorInfo &aVendorInfo);

    /**
     * Sets the TCAT agent to the active or standby state.
     *
     * @param[in]  aActive      TRUE to activate TCAT, FALSE to set TCAT to standby.
     * @param[in]  aDelayMs     The delay in milliseconds before activating. Ignored for standby.
     * @param[in]  aDurationMs  The duration in milliseconds to remain active (0 for indefinitely). Ignored for
     *                          standby.
     *
     * @retval kErrorNone          Successfully set the TCAT agent state.
     * @retval kErrorInvalidState  The TCAT agent is not started or cannot transition to the requested state.
     */
    Error TcatActive(bool aActive, uint32_t aDelayMs, uint32_t aDurationMs);

    /**
     * Adds a TCAT application TLV to the response to the TCAT Commissioner's request that is being handled.
     *
     * This method must only be called from within the TCAT application data receive callback.
     *
     * @param[in]  aApplicationProtocol  The application protocol (TLV type) of the TLV to send.
     * @param[in]  aBuf                  A pointer to the TLV value.
     * @param[in]  aLength               The length of the TLV value in bytes.
     *
     * @retval kErrorNone          Successfully added the TLV to the response.
     * @retval kErrorNoBufs        Failed to allocate buffer memory.
     * @retval kErrorInvalidState  There is no request being handled.
     * @retval kErrorRejected      The application protocol is a response with data or status, but no response is
     *                             pending.
     */
    Error SendApplicationTlv(TcatAgent::TcatApplicationProtocol aApplicationProtocol,
                             const uint8_t                     *aBuf,
                             uint16_t                           aLength);

private:
    static const char kTcatUriPath[];

    static MeshCoP::SecureSession *HandleAccept(void *aContext, const Ip6::MessageInfo &aMessageInfo);
    MeshCoP::SecureSession        *HandleAccept(void);

    static void HandleConnectEvent(Dtls::Session::ConnectEvent aEvent, void *aContext);
    void        HandleConnectEvent(Dtls::Session::ConnectEvent aEvent);

    static void HandleRequest(void *aContext, otMessage *aMessage, const otMessageInfo *aMessageInfo);
    void        HandleRequest(Coap::Message &aRequest);
    Error       ProcessRequestTlvs(const Coap::Message &aRequest, Coap::Message &aResponse);

    Coap::Resource mResource;
    Coap::Message *mResponse; // The response being composed while a request is handled, `nullptr` otherwise.
    uint16_t       mPort;
};

} // namespace MeshCoP
} // namespace ot

#endif // OPENTHREAD_CONFIG_TCAT_COAPS_ENABLE

#endif // OT_CORE_MESHCOP_TCAT_COAPS_SERVER_HPP_
