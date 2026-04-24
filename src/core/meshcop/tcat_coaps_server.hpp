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

#ifndef OT_CORE_MESHCOP_TCAT_COAPS_SERVER_HPP_
#define OT_CORE_MESHCOP_TCAT_COAPS_SERVER_HPP_

#include "openthread-core-config.h"

#if OPENTHREAD_CONFIG_TCAT_COAPS_ENABLE

#include "coap/coap.hpp"
#include "coap/coap_secure.hpp"
#include "common/locator.hpp"
#include "meshcop/tcat_agent.hpp"

namespace ot {
namespace MeshCoP {

#if !OPENTHREAD_CONFIG_SECURE_TRANSPORT_ENABLE
#error "TCAT CoAPS server requires OPENTHREAD_CONFIG_SECURE_TRANSPORT_ENABLE"
#endif

/**
 * TCAT server over CoAP Secure (CoAP over DTLS over UDP, no MAC security).
 *
 * Accepts one CoAPS connection at a time on a configured UDP port.
 * Incoming TCAT command TLVs are carried as the payload of a CON POST to URI "c/td".
 * Response TLVs are returned in the CoAP 2.04 response payload (Pattern A).
 *
 * Modelled on ApplicationCoapSecure: the object IS simultaneously the DTLS transport,
 * the certificate extension, and the single CoAP-over-DTLS session.
 */
class TcatCoapServer : public Dtls::Transport,
                       public Dtls::Transport::Extension,
                       public Coap::SecureSession
{
public:
    explicit TcatCoapServer(Instance &aInstance);

    /**
     * Starts the server and binds to @p aPort.
     *
     * @retval kErrorNone     Successfully started.
     * @retval kErrorAlready  Already started.
     */
    Error Start(uint16_t                           aPort,
                TcatAgent::AppDataReceiveCallback  aAppDataCallback,
                TcatAgent::JoinCallback            aJoinHandler);

    void Stop(void);

    bool IsStarted(void)   const { return !Dtls::Transport::IsClosed(); }
    bool IsConnected(void) const { return Coap::SecureSession::IsConnected(); }

    /**
     * Appends a TCAT application TLV to the CoAP response currently being assembled.
     *
     * Must be called only from within the app-data receive callback during POST request handling.
     *
     * @retval kErrorNone          Appended successfully.
     * @retval kErrorNoBufs        Buffer allocation failure.
     * @retval kErrorInvalidState  No active POST response context.
     */
    Error SendApplicationTlv(TcatAgent::TcatApplicationProtocol aApplicationProtocol,
                              uint8_t                           *aBuf,
                              uint16_t                           aLength);

private:
    static constexpr const char *kTcatDataUri = "c/tc";

    static MeshCoP::SecureSession *HandleAccept(void *aContext, const Ip6::MessageInfo &aMessageInfo);

    static void HandleCoapsConnect(MeshCoP::SecureSession::ConnectEvent aEvent, void *aContext);
    void        HandleCoapsConnect(MeshCoP::SecureSession::ConnectEvent aEvent);

    static void HandleTcatPost(void *aContext, otMessage *aMessage, const otMessageInfo *aMessageInfo);
    void        HandleTcatPost(Coap::Message &aRequest, const Ip6::MessageInfo &aMessageInfo);

    Coap::Resource  mTcatResource;    // CoAP resource for handling TCAT Commissioner's requests
    Coap::Message  *mResponseMessage; // set during HandleTcatPost; nullptr otherwise
};

} // namespace MeshCoP
} // namespace ot

#endif // OPENTHREAD_CONFIG_TCAT_COAPS_ENABLE

#endif // OT_CORE_MESHCOP_TCAT_COAPS_SERVER_HPP_
