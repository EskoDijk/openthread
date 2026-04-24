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
 *   Implements TCAT over UDP/DTLS server.
 */

#include "tcat_udp_server.hpp"

#if OPENTHREAD_CONFIG_TCAT_UDP_ENABLE

#include "instance/instance.hpp"
#include "meshcop/meshcop_tlvs.hpp"
#include "net/ip6_address.hpp"

#if OPENTHREAD_EXAMPLES_SIMULATION
#include <openthread/platform/tcat_udp.h>
#endif

namespace ot {
namespace MeshCoP {

RegisterLogModule("TcatUdp");

TcatUdpServer::TcatUdpServer(Instance &aInstance)
    : InstanceLocator(aInstance)
    , SecureTransport::Extension(mTransport)
    , mTransport(aInstance, kNoLinkSecurity)
    , mSession(mTransport)
    , mSendMessage(nullptr)
{
}

Error TcatUdpServer::Start(uint16_t aPort, TcatAgent::JoinCallback aJoinHandler)
{
    Error error;

    VerifyOrExit(!IsStarted(), error = kErrorAlready);

#if OPENTHREAD_EXAMPLES_SIMULATION
    OT_UNUSED_VARIABLE(aPort);
    SuccessOrExit(error = mTransport.Open(SimSendCallback, this));
#else
    SuccessOrExit(error = mTransport.Open(aPort));
#endif
    mTransport.SetExtension(*this); // register certificate/key config with the transport
    mTransport.SetAcceptCallback(HandleAccept, this);

    mSession.SetConnectCallback(HandleDtlsConnect, this);
    mSession.SetReceiveCallback(HandleDtlsReceive, this);

    SuccessOrExit(error = Get<TcatAgent>().Start(nullptr, aJoinHandler, nullptr));
    Get<TcatAgent>().SetStateChangeCallback(nullptr, nullptr); // no advertisement mechanism for UDP

exit:
    if (error != kErrorNone && error != kErrorAlready)
    {
        mTransport.Close();
    }

    return error;
}

void TcatUdpServer::Stop(void)
{
    VerifyOrExit(IsStarted());

    mSession.Disconnect();
    mTransport.Close();
    Get<TcatAgent>().Stop();

    FreeMessage(mSendMessage);
    mSendMessage = nullptr;

exit:
    return;
}

Error TcatUdpServer::SendApplicationTlv(TcatAgent::TcatApplicationProtocol aApplicationProtocol,
                                        uint8_t                            *aBuf,
                                        uint16_t                            aLength)
{
    Error    error = kErrorNone;
    Message *message;

    VerifyOrExit(IsConnected(), error = kErrorInvalidState);

    message = Get<MessagePool>().Allocate(Message::kTypeIp6);
    VerifyOrExit(message != nullptr, error = kErrorNoBufs);

    SuccessOrExit(error = Tlv::AppendTlv(*message, static_cast<uint8_t>(aApplicationProtocol), aBuf, aLength));
    SuccessOrExit(error = mSession.Send(*message));
    message = nullptr;

exit:
    FreeMessage(message);
    return error;
}

SecureSession *TcatUdpServer::HandleAccept(void *aContext, const Ip6::MessageInfo &aMessageInfo)
{
    OT_UNUSED_VARIABLE(aMessageInfo);
    TcatUdpServer &server = *static_cast<TcatUdpServer *>(aContext);

    // Accept only one session at a time.
    return server.mSession.IsConnectionActive() ? nullptr : &server.mSession;
}

void TcatUdpServer::HandleDtlsConnect(SecureSession::ConnectEvent aEvent, void *aContext)
{
    static_cast<TcatUdpServer *>(aContext)->HandleDtlsConnect(aEvent);
}

void TcatUdpServer::HandleDtlsConnect(SecureSession::ConnectEvent aEvent)
{
    if (aEvent == SecureSession::kConnected)
    {
        Error err = Get<TcatAgent>().Connected(*this);

        if (err != kErrorNone)
        {
            mSession.Disconnect();
            LogWarn("Rejected TCAT Commissioner: %s", ErrorToString(err));
        }
    }
    else
    {
        Get<TcatAgent>().Disconnected();

        FreeMessage(mSendMessage);
        mSendMessage = nullptr;
    }
}

void TcatUdpServer::HandleDtlsReceive(void *aContext, uint8_t *aBuf, uint16_t aLength)
{
    static_cast<TcatUdpServer *>(aContext)->HandleDtlsReceive(aBuf, aLength);
}

void TcatUdpServer::HandleDtlsReceive(uint8_t *aBuf, uint16_t aLength)
{
    Error    error = kErrorNone;
    ot::Tlv  tlv;
    uint32_t requiredBytes = sizeof(Tlv);

    VerifyOrExit(mSendMessage != nullptr || (mSendMessage = Get<MessagePool>().Allocate(Message::kTypeIp6)) != nullptr,
                 error = kErrorNoBufs);

    // Re-use the BLE receive framing logic: accumulate bytes into mSendMessage (used temporarily
    // as the receive buffer here) until a complete TLV is assembled, then dispatch it.
    // We use a local per-call receive message instead to keep state simple.
    {
        Message *recvMsg = Get<MessagePool>().Allocate(Message::kTypeIp6);
        VerifyOrExit(recvMsg != nullptr, error = kErrorNoBufs);

        SuccessOrExit(error = recvMsg->AppendBytes(aBuf, aLength));

        while (recvMsg->GetLength() >= requiredBytes)
        {
            IgnoreError(recvMsg->Read(0, tlv));

            if (tlv.IsExtended())
            {
                ot::ExtendedTlv extTlv;
                requiredBytes = sizeof(extTlv);

                if (recvMsg->GetLength() < requiredBytes)
                {
                    break;
                }

                IgnoreError(recvMsg->Read(0, extTlv));
                requiredBytes = extTlv.GetSize();
            }
            else
            {
                requiredBytes = tlv.GetSize();
            }

            if (recvMsg->GetLength() < requiredBytes)
            {
                break;
            }

            // Full TLV received — dispatch to TcatAgent if connected.
            if (Get<TcatAgent>().IsConnected())
            {
                Error tcatError = Get<TcatAgent>().HandleSingleTlv(*recvMsg, *mSendMessage);

                if (tcatError == kErrorAbort)
                {
                    LogInfo("Disconnecting TCAT client.");
                    mSession.Disconnect();
                    recvMsg->Free();
                    ExitNow();
                }
                else if (tcatError != kErrorNone)
                {
                    LogWarn("HandleSingleTlv: %s", ErrorToString(tcatError));
                    mSession.Disconnect();
                    recvMsg->Free();
                    ExitNow();
                }

                if (mSendMessage->GetLength() > 0)
                {
                    SuccessOrExit(error = mSession.Send(*mSendMessage));
                    mSendMessage = nullptr; // ownership transferred to Send()
                }
            }

            IgnoreError(recvMsg->SetLength(0));
            requiredBytes = sizeof(Tlv);
        }

        recvMsg->Free();
    }

exit:
    if (error != kErrorNone)
    {
        LogCritOnError(error, "HandleDtlsReceive");
        mSession.Disconnect();
    }
}

#if OPENTHREAD_EXAMPLES_SIMULATION

Error TcatUdpServer::SimSendCallback(void *aContext, ot::Message &aMessage, const Ip6::MessageInfo &aMessageInfo)
{
    TcatUdpServer &server = *static_cast<TcatUdpServer *>(aContext);
    uint8_t        buf[2048];
    uint16_t       len     = aMessage.GetLength();
    uint32_t       dstIp4  = 0;
    uint16_t       dstPort = aMessageInfo.GetPeerPort();

    if (aMessageInfo.GetPeerAddr().IsIp4Mapped())
    {
        // Extract the 32-bit IPv4 address from the last 4 bytes of the IPv6 address
        const uint8_t *addr = aMessageInfo.GetPeerAddr().mFields.m8;
        memcpy(&dstIp4, addr + 12, sizeof(dstIp4)); // already in network byte order
    }

    if (len > sizeof(buf))
    {
        len = sizeof(buf);
    }

    aMessage.ReadBytes(0, buf, len);
    aMessage.Free();

    otPlatTcatUdpSend(&server.GetInstance(), buf, len, dstIp4, dstPort);
    return kErrorNone;
}

void TcatUdpServer::SimInjectReceived(const uint8_t *aBuf, uint16_t aLen, uint32_t aSrcIp4, uint16_t aSrcPort)
{
    Message         *message = Get<MessagePool>().Allocate(Message::kTypeIp6);
    Ip6::MessageInfo msgInfo;
    Ip4::Address     ip4Src;

    VerifyOrExit(message != nullptr);
    SuccessOrExit(message->AppendBytes(aBuf, aLen));

    ip4Src.mFields.m32 = aSrcIp4;
    msgInfo.GetPeerAddr().SetToIp4Mapped(ip4Src);
    msgInfo.SetPeerPort(aSrcPort);

    mTransport.HandleReceive(*message, msgInfo);

exit:
    FreeMessage(message);
}

#endif // OPENTHREAD_EXAMPLES_SIMULATION

} // namespace MeshCoP
} // namespace ot

// Default weak no-op so non-simulation builds link cleanly. The simulation
// platform overrides this with the real host-socket send in tcat_udp.c.
OT_TOOL_WEAK void otPlatTcatUdpSend(otInstance    *aInstance,
                                    const uint8_t *aBuf,
                                    uint16_t       aLen,
                                    uint32_t       aDstIp4,
                                    uint16_t       aDstPort)
{
    OT_UNUSED_VARIABLE(aInstance);
    OT_UNUSED_VARIABLE(aBuf);
    OT_UNUSED_VARIABLE(aLen);
    OT_UNUSED_VARIABLE(aDstIp4);
    OT_UNUSED_VARIABLE(aDstPort);
}

#endif // OPENTHREAD_CONFIG_TCAT_UDP_ENABLE
