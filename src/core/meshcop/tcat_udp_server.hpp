/*
 *  Copyright (c) 2024, The OpenThread Authors.
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
 *   Definitions for TCAT over UDP/DTLS server.
 */

#ifndef OT_CORE_MESHCOP_TCAT_UDP_SERVER_HPP_
#define OT_CORE_MESHCOP_TCAT_UDP_SERVER_HPP_

#include "openthread-core-config.h"

#if OPENTHREAD_CONFIG_TCAT_UDP_ENABLE

#include "common/locator.hpp"
#include "common/non_copyable.hpp"
#include "meshcop/secure_transport.hpp"
#include "meshcop/tcat_agent.hpp"

namespace ot {
namespace MeshCoP {

#if !OPENTHREAD_CONFIG_SECURE_TRANSPORT_ENABLE
#error "TCAT UDP feature requires `OPENTHREAD_CONFIG_SECURE_TRANSPORT_ENABLE`"
#endif

/**
 * Implements the TCAT server over UDP/DTLS.
 *
 * Accepts a single DTLS connection at a time on a configured UDP port (no MAC security).
 * Once connected, drives the TcatAgent for command processing.
 */
class TcatUdpServer : public InstanceLocator, public SecureTransport::Extension, private NonCopyable
{
public:
    explicit TcatUdpServer(Instance &aInstance);

    /**
     * Starts the TCAT UDP server and binds it to the given port.
     *
     * @param[in] aPort        UDP port to listen on (e.g. 1234).
     * @param[in] aJoinHandler Callback invoked when a network join/leave operation completes.
     *
     * @retval kErrorNone         Successfully started.
     * @retval kErrorAlready      Already started.
     * @retval kErrorInvalidArgs  Vendor info not set, see TcatAgent::SetTcatVendorInfo.
     */
    Error Start(uint16_t aPort, TcatAgent::JoinCallback aJoinHandler);

    /**
     * Stops the TCAT UDP server and closes the DTLS transport.
     */
    void Stop(void);

    /**
     * Indicates whether the TCAT UDP server is running.
     */
    bool IsStarted(void) const { return !mTransport.IsClosed(); }

    /**
     * Indicates whether a DTLS session is currently connected.
     */
    bool IsConnected(void) const { return mSession.IsConnected(); }

    /**
     * Sends a TCAT application TLV over the active DTLS session.
     *
     * @param[in] aApplicationProtocol  Application protocol selector.
     * @param[in] aBuf                  Payload bytes.
     * @param[in] aLength               Payload length.
     *
     * @retval kErrorNone          Successfully enqueued.
     * @retval kErrorNoBufs        Buffer allocation failure.
     * @retval kErrorInvalidState  Not connected.
     */
    Error SendApplicationTlv(TcatAgent::TcatApplicationProtocol aApplicationProtocol,
                              uint8_t                           *aBuf,
                              uint16_t                           aLength);

private:
    static SecureSession *HandleAccept(void *aContext, const Ip6::MessageInfo &aMessageInfo);

    static void HandleDtlsConnect(SecureSession::ConnectEvent aEvent, void *aContext);
    void        HandleDtlsConnect(SecureSession::ConnectEvent aEvent);

    static void HandleDtlsReceive(void *aContext, uint8_t *aBuf, uint16_t aLength);
    void        HandleDtlsReceive(uint8_t *aBuf, uint16_t aLength);

    Dtls::Transport mTransport; // DTLS server transport, no MAC security
    Dtls::Session   mSession;   // single accepted session
    Message        *mSendMessage;
};

} // namespace MeshCoP
} // namespace ot

#endif // OPENTHREAD_CONFIG_TCAT_UDP_ENABLE

#endif // OT_CORE_MESHCOP_TCAT_UDP_SERVER_HPP_
