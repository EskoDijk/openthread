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

#include "tcat_coaps_server.hpp"

#if OPENTHREAD_CONFIG_TCAT_COAPS_ENABLE

#include "coap/coap_message.hpp"
#include "instance/instance.hpp"
#include "meshcop/meshcop_tlvs.hpp"

namespace ot {
namespace MeshCoP {

RegisterLogModule("TcatCoaps");

TcatCoapServer::TcatCoapServer(Instance &aInstance)
    : Dtls::Transport(aInstance, kNoLinkSecurity)
    , Dtls::Transport::Extension(static_cast<Dtls::Transport &>(*this))
    , Coap::SecureSession(aInstance, static_cast<Dtls::Transport &>(*this))
    , mTcatResource(kTcatDataUri, HandleTcatPost, this)
    , mResponseMessage(nullptr)
{
    Dtls::Transport::SetExtension(static_cast<Dtls::Transport::Extension &>(*this));
    Dtls::Transport::SetAcceptCallback(HandleAccept, this);
    SetConnectCallback(HandleCoapsConnect, this);
}

Error TcatCoapServer::Start(uint16_t                          aPort,
                           TcatAgent::AppDataReceiveCallback  aAppDataCallback,
                           TcatAgent::JoinCallback            aJoinHandler)
{
    Error error;

    VerifyOrExit(!IsStarted(), error = kErrorAlready);

    SuccessOrExit(error = Dtls::Transport::Open(aPort));
    AddResource(mTcatResource);

    SuccessOrExit(error = Get<TcatAgent>().Start(aAppDataCallback, aJoinHandler, nullptr));
    Get<TcatAgent>().SetStateChangeCallback(nullptr, nullptr);

exit:
    if (error != kErrorNone && error != kErrorAlready)
    {
        Dtls::Transport::Close();
    }

    return error;
}

void TcatCoapServer::Stop(void)
{
    VerifyOrExit(IsStarted());

    Disconnect();
    RemoveResource(mTcatResource);
    Dtls::Transport::Close();
    Get<TcatAgent>().Stop();

    mResponseMessage = nullptr;

exit:
    return;
}

Error TcatCoapServer::SendApplicationTlv(TcatAgent::TcatApplicationProtocol aApplicationProtocol,
                                          uint8_t                           *aBuf,
                                          uint16_t                           aLength)
{
    Error error = kErrorNone;

    VerifyOrExit(mResponseMessage != nullptr, error = kErrorInvalidState);
    error = Tlv::AppendTlv(*mResponseMessage, static_cast<uint8_t>(aApplicationProtocol), aBuf, aLength);

exit:
    return error;
}

MeshCoP::SecureSession *TcatCoapServer::HandleAccept(void *aContext, const Ip6::MessageInfo &aMessageInfo)
{
    OT_UNUSED_VARIABLE(aMessageInfo);
    TcatCoapServer &server = *static_cast<TcatCoapServer *>(aContext);

    return server.IsSessionInUse() ? nullptr : &static_cast<Coap::SecureSession &>(server);
}

void TcatCoapServer::HandleCoapsConnect(MeshCoP::SecureSession::ConnectEvent aEvent, void *aContext)
{
    static_cast<TcatCoapServer *>(aContext)->HandleCoapsConnect(aEvent);
}

void TcatCoapServer::HandleCoapsConnect(MeshCoP::SecureSession::ConnectEvent aEvent)
{
    if (aEvent == MeshCoP::SecureSession::kConnected)
    {
        Error err = Get<TcatAgent>().Connected(*this);

        if (err != kErrorNone)
        {
            Disconnect();
            LogWarn("Rejected TCAT Commissioner: %s", ErrorToString(err));
        }
    }
    else
    {
        Get<TcatAgent>().Disconnected();
        mResponseMessage = nullptr;
    }
}

void TcatCoapServer::HandleTcatPost(void *aContext, otMessage *aMessage, const otMessageInfo *aMessageInfo)
{
    static_cast<TcatCoapServer *>(aContext)->HandleTcatPost(
        *static_cast<Coap::Message *>(aMessage),
        *static_cast<const Ip6::MessageInfo *>(aMessageInfo));
}

void TcatCoapServer::HandleTcatPost(Coap::Message &aRequest, const Ip6::MessageInfo &aMessageInfo)
{
    Error          error    = kErrorNone;
    Coap::Message *response = nullptr;

    VerifyOrExit(Get<TcatAgent>().IsConnected(), error = kErrorInvalidState);

    // Allocate 2.04 piggy-backed response (ACK with token + message ID copied from request).
    response = AllocateAndInitResponseFor(aRequest);
    VerifyOrExit(response != nullptr, error = kErrorNoBufs);

    mResponseMessage = response;

    // Iterate TLVs in the CoAP payload.  Each TLV is extracted into a scratch
    // message (single-TLV) that HandleSingleTlv reads from offset 0.
    // TODO: check if this could be replaced by GetOffset(), so that HandleSingleTlv reads with given offset from
    // original message.
    {
        uint16_t offset = aRequest.GetOffset(); // start of CoAP payload

        while (offset < aRequest.GetLength())
        {
            Tlv      tlv;
            uint32_t tlvSize;

            VerifyOrExit((error = aRequest.Read(offset, tlv)) == kErrorNone);

            if (tlv.IsExtended())
            {
                ExtendedTlv extTlv;
                VerifyOrExit((error = aRequest.Read(offset, extTlv)) == kErrorNone);
                tlvSize = extTlv.GetSize();
            }
            else
            {
                tlvSize = tlv.GetSize();
            }

            // Guard against malformed payload that overruns the message.
            VerifyOrExit(offset + tlvSize <= aRequest.GetLength(), error = kErrorParse);

            {
                Message *tlvMsg = Get<MessagePool>().Allocate(Message::kTypeIp6);
                VerifyOrExit(tlvMsg != nullptr, error = kErrorNoBufs);

                if ((error = tlvMsg->AppendBytesFromMessage(aRequest, offset, static_cast<uint16_t>(tlvSize))) != kErrorNone)
                {
                    tlvMsg->Free();
                    ExitNow();
                }

                error = Get<TcatAgent>().HandleSingleTlv(*tlvMsg, *mResponseMessage);
                tlvMsg->Free();

                if (error != kErrorNone)
                {
                    if (error == kErrorAbort) // the unique disconnection signal
                    {
                        LogInfo("Disconnecting TCAT client.");
                    }
                    else
                    {
                        LogWarn("HandleSingleTlv: %s", ErrorToString(error)); // unrecoverable error
                    }
                    ExitNow();
                }
            }

            offset += static_cast<uint16_t>(tlvSize);
        }
    }

exit:
    if (response != nullptr)
    {
        // Send response (2.04 with accumulated TLV payload, or an error in unrecoverable error cases).
        OT_UNUSED_VARIABLE(aMessageInfo);
        if (error != kErrorNone && error != kErrorAbort)
        {
            response->WriteCode(Coap::kCodeInternalError);
        }
        IgnoreError(Coap::SecureSession::SendMessage(*response));
    }
    mResponseMessage = nullptr;
    FreeMessage(response);

    if (error != kErrorNone)
    {
        if (error != kErrorAbort)
        {
            LogWarnOnError(error, "HandleTcatPost");
        }
        Disconnect();
    }
}

} // namespace MeshCoP
} // namespace ot

#endif // OPENTHREAD_CONFIG_TCAT_COAPS_ENABLE
