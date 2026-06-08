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
 *   This file implements a reusable DNS-message-over-TCP transport.
 */

#include "dns_tcp_transport.hpp"

#if OPENTHREAD_CONFIG_PLATFORM_TCP_ENABLE && OPENTHREAD_CONFIG_DNSSD_SERVER_OVER_TCP_ENABLE

#include "common/code_utils.hpp"
#include "common/encoding.hpp"
#include "instance/instance.hpp"

namespace ot {
namespace Dns {

//---------------------------------------------------------------------------------------------------------------------
// TcpTransport::PoolListener / PoolConnection

TcpTransport::PoolListener::PoolListener(Instance &aInstance, TcpTransport &aTransport)
    : Ip6::PlatTcp::Listener(aInstance, &TcpTransport::HandleAccept, /* aDeleteHandler */ nullptr)
    , mTransport(aTransport)
{
}

TcpTransport::PoolConnection::PoolConnection(Instance &aInstance, TcpTransport &aTransport)
    : Ip6::PlatTcp::Connection(aInstance, &TcpTransport::HandleEvent, /* aDeleteHandler */ nullptr)
    , mTransport(aTransport)
{
}

//---------------------------------------------------------------------------------------------------------------------
// TcpTransport

TcpTransport::TcpTransport(Instance         &aInstance,
                           ReceiveHandler    aReceiveHandler,
                           DisconnectHandler aDisconnectHandler,
                           void             *aContext,
                           uint16_t          aMaxMessageSize)
    : InstanceLocator(aInstance)
    , mListener(aInstance, *this)
    , mReceiveHandler(aReceiveHandler)
    , mDisconnectHandler(aDisconnectHandler)
    , mContext(aContext)
    , mMaxMessageSize(aMaxMessageSize)
{
    // The pooled connections live for the lifetime of this transport. They are constructed once here (in place) and
    // are not explicitly destructed: their backing storage is part of the owning OpenThread instance, and any active
    // connection is aborted and reclaimed by the platform-TCP module (`Ip6::PlatTcp`) which outlives this transport.
    for (uint16_t i = 0; i < kMaxConnections; i++)
    {
        new (&ConnectionAt(i)) PoolConnection(aInstance, *this);
    }
}

Error TcpTransport::Start(uint16_t aPort)
{
    Ip6::PlatTcp::SockAddr sockAddr;

    sockAddr.SetPort(aPort);

    return mListener.Enable(sockAddr);
}

void TcpTransport::Stop(void)
{
    mListener.Disable();

    for (uint16_t i = 0; i < kMaxConnections; i++)
    {
        PoolConnection &connection = ConnectionAt(i);

        switch (connection.GetState())
        {
        case Connection::kStateUnused:
        case Connection::kStateDisconnected:
            break;
        default:
            connection.Abort();
            break;
        }
    }
}

Error TcpTransport::SendMessage(Connection &aConnection, OwnedPtr<Message> aMessage)
{
    Error   error;
    uint8_t lengthPrefix[kLengthSize];

    BigEndian::WriteUint16(aMessage->GetLength(), lengthPrefix);
    SuccessOrExit(error = aMessage->PrependBytes(lengthPrefix, kLengthSize));

    error = aConnection.Send(aMessage.PassOwnership());

exit:
    return error;
}

TcpTransport::Connection *TcpTransport::HandleAccept(Ip6::PlatTcp::Listener        &aListener,
                                                    const Ip6::PlatTcp::SockAddr &aPeerSockAddr)
{
    OT_UNUSED_VARIABLE(aPeerSockAddr);

    return static_cast<PoolListener &>(aListener).mTransport.Accept();
}

void TcpTransport::HandleEvent(Connection &aConnection, Connection::Event aEvent)
{
    PoolConnection &connection = static_cast<PoolConnection &>(aConnection);

    switch (aEvent)
    {
    case Connection::kEventReceive:
        connection.mTransport.ProcessReceive(connection);
        break;

    case Connection::kEventDisconnected:
        connection.mTransport.ProcessDisconnect(connection);
        break;

    case Connection::kEventConnected:
    case Connection::kEventSendDone:
        break;
    }
}

TcpTransport::Connection *TcpTransport::Accept(void) { return FindFreeConnection(); }

TcpTransport::PoolConnection *TcpTransport::FindFreeConnection(void)
{
    PoolConnection *freeConnection = nullptr;

    for (uint16_t i = 0; i < kMaxConnections; i++)
    {
        PoolConnection &connection = ConnectionAt(i);

        if (connection.GetState() == Connection::kStateUnused)
        {
            freeConnection = &connection;
            break;
        }
    }

    return freeConnection;
}

void TcpTransport::ProcessReceive(PoolConnection &aConnection)
{
    const Message *rxMessage;

    while ((rxMessage = aConnection.GetRxMessage()) != nullptr)
    {
        uint16_t available = rxMessage->GetLength();
        uint8_t  lengthPrefix[kLengthSize];
        uint16_t length;
        Message *query;

        // Wait until the two-byte length prefix and the full message are available.
        VerifyOrExit(available >= kLengthSize);
        IgnoreError(rxMessage->Read(0, lengthPrefix, kLengthSize));
        length = BigEndian::ReadUint16(lengthPrefix);

        if (length > mMaxMessageSize)
        {
            aConnection.Abort();
            ExitNow();
        }

        VerifyOrExit(available >= kLengthSize + length);

        query = Get<MessagePool>().Allocate(Message::kTypeOther);

        if (query != nullptr)
        {
            if (query->AppendBytesFromMessage(*rxMessage, kLengthSize, length) == kErrorNone)
            {
                mReceiveHandler(mContext, aConnection, *query);
            }

            query->Free();
        }

        // The receive handler may have closed or aborted the connection.
        VerifyOrExit(aConnection.GetState() == Connection::kStateConnected);

        aConnection.RemoveParsedLengthFromRxMessage(kLengthSize + length);
    }

exit:
    return;
}

void TcpTransport::ProcessDisconnect(PoolConnection &aConnection)
{
    if (mDisconnectHandler != nullptr)
    {
        mDisconnectHandler(mContext, aConnection);
    }
}

} // namespace Dns
} // namespace ot

#endif // OPENTHREAD_CONFIG_PLATFORM_TCP_ENABLE && OPENTHREAD_CONFIG_DNSSD_SERVER_OVER_TCP_ENABLE
