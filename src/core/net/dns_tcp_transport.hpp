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
 *   This file includes definitions for a reusable DNS-message-over-TCP transport.
 */

#ifndef OT_CORE_NET_DNS_TCP_TRANSPORT_HPP_
#define OT_CORE_NET_DNS_TCP_TRANSPORT_HPP_

#include "openthread-core-config.h"

#if OPENTHREAD_CONFIG_PLATFORM_TCP_ENABLE && OPENTHREAD_CONFIG_DNSSD_SERVER_OVER_TCP_ENABLE

#include "common/locator.hpp"
#include "common/message.hpp"
#include "common/non_copyable.hpp"
#include "common/owned_ptr.hpp"
#include "net/plat_tcp.hpp"

namespace ot {
namespace Dns {

/**
 * Implements a reusable DNS-message-over-TCP transport on top of the platform TCP (`Ip6::PlatTcp`).
 *
 * DNS messages are framed using the RFC 1035 section 4.2.2 two-byte (big-endian) length prefix (the same framing used
 * for DNS-over-TCP and for SRP-over-TCP). The transport owns a single TCP listener and a fixed pool of connections,
 * and knows nothing about a specific DNS server. A consumer (e.g., the DNS-SD server, or potentially the SRP registrar
 * on a different port) supplies a receive handler (invoked with each fully received DNS message and a connection
 * handle) and a disconnect handler (invoked when a connection closes, so the consumer can drop any pending work bound
 * to that connection). The consumer sends a response with `SendMessage()`, which prepends the length prefix.
 */
class TcpTransport : public InstanceLocator, private NonCopyable
{
public:
    /**
     * Represents a TCP connection handle (the reply destination), exposed to consumers.
     */
    typedef Ip6::PlatTcp::Connection Connection;

    /**
     * Pointer to a handler invoked when a complete DNS message has been received on a connection.
     *
     * The @p aMessage is owned by the transport and is only valid during the callback.
     *
     * @param[in] aContext     The consumer context.
     * @param[in] aConnection  The connection the message was received on (the reply handle).
     * @param[in] aMessage     The received DNS message (without the TCP length prefix).
     */
    typedef void (*ReceiveHandler)(void *aContext, Connection &aConnection, Message &aMessage);

    /**
     * Pointer to a handler invoked when a connection is closing.
     *
     * Invoked from the connection's disconnect event, before the connection's pool slot can be reused, so the consumer
     * can safely drop any pending (e.g., asynchronous) work that references @p aConnection.
     *
     * @param[in] aContext     The consumer context.
     * @param[in] aConnection  The connection that is closing.
     */
    typedef void (*DisconnectHandler)(void *aContext, Connection &aConnection);

    /**
     * Initializes the transport.
     *
     * @param[in] aInstance           The OpenThread instance.
     * @param[in] aReceiveHandler     The receive handler callback.
     * @param[in] aDisconnectHandler  The disconnect handler callback (can be `nullptr`).
     * @param[in] aContext            The consumer context passed back to the callbacks.
     * @param[in] aMaxMessageSize     Maximum allowed size of a single framed DNS message (larger aborts the connection).
     */
    TcpTransport(Instance         &aInstance,
                 ReceiveHandler    aReceiveHandler,
                 DisconnectHandler aDisconnectHandler,
                 void             *aContext,
                 uint16_t          aMaxMessageSize);

    /**
     * Starts the transport, listening for incoming TCP connections on the given port.
     *
     * @param[in] aPort  The local port to listen on.
     *
     * @retval kErrorNone   Successfully started.
     * @retval kErrorFailed Failed to start the listener (e.g., no functional platform-TCP backend).
     */
    Error Start(uint16_t aPort);

    /**
     * Stops the transport, disabling the listener and aborting all active connections.
     */
    void Stop(void);

    /**
     * Sends a DNS message over a connection (prepending the two-byte length prefix).
     *
     * Ownership of @p aMessage is always transferred.
     *
     * @param[in] aConnection  The connection to send on.
     * @param[in] aMessage     The DNS message to send (without the length prefix).
     *
     * @retval kErrorNone          Successfully queued for transmission.
     * @retval kErrorInvalidState  The connection is not in a state to accept data.
     * @retval kErrorNoBufs        Failed to prepend the length prefix.
     */
    Error SendMessage(Connection &aConnection, OwnedPtr<Message> aMessage);

private:
    static constexpr uint16_t kMaxConnections = OPENTHREAD_CONFIG_DNS_TCP_TRANSPORT_MAX_CONNECTIONS;
    static constexpr uint16_t kLengthSize     = sizeof(uint16_t);

    // A pooled listener / connection carrying a back-pointer to its owning transport. This lets the (static) platform
    // TCP callbacks recover the correct transport instance even when multiple `TcpTransport` instances exist.

    class PoolListener : public Ip6::PlatTcp::Listener
    {
    public:
        PoolListener(Instance &aInstance, TcpTransport &aTransport);
        TcpTransport &mTransport;
    };

    class PoolConnection : public Ip6::PlatTcp::Connection
    {
    public:
        PoolConnection(Instance &aInstance, TcpTransport &aTransport);
        TcpTransport &mTransport;
    };

    static Connection *HandleAccept(Ip6::PlatTcp::Listener &aListener, const Ip6::PlatTcp::SockAddr &aPeerSockAddr);
    static void        HandleEvent(Connection &aConnection, Connection::Event aEvent);

    Connection     *Accept(void);
    void            ProcessReceive(PoolConnection &aConnection);
    void            ProcessDisconnect(PoolConnection &aConnection);
    PoolConnection *FindFreeConnection(void);
    PoolConnection &ConnectionAt(uint16_t aIndex)
    {
        return reinterpret_cast<PoolConnection *>(mConnectionStorage)[aIndex];
    }

    PoolListener mListener;
    OT_DEFINE_ALIGNED_VAR(mConnectionStorage, sizeof(PoolConnection) * kMaxConnections, uint64_t);

    ReceiveHandler    mReceiveHandler;
    DisconnectHandler mDisconnectHandler;
    void             *mContext;
    uint16_t          mMaxMessageSize;
};

} // namespace Dns
} // namespace ot

#endif // OPENTHREAD_CONFIG_PLATFORM_TCP_ENABLE && OPENTHREAD_CONFIG_DNSSD_SERVER_OVER_TCP_ENABLE

#endif // OT_CORE_NET_DNS_TCP_TRANSPORT_HPP_
