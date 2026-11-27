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
 *   This file implements the TCAT over CoAPS server.
 */

#include "tcat_coaps_server.hpp"

#if OPENTHREAD_CONFIG_TCAT_COAPS_ENABLE

#include "instance/instance.hpp"

namespace ot {
namespace MeshCoP {

RegisterLogModule("TcatCoaps");

const char TcatCoapServer::kTcatUriPath[] = "c/tc";

TcatCoapServer::TcatCoapServer(Instance &aInstance)
    : Dtls::Transport(aInstance, kNoLinkSecurity)
    , Dtls::Transport::Extension(static_cast<Dtls::Transport &>(*this))
    , Coap::SecureSession(aInstance, static_cast<Dtls::Transport &>(*this))
    , mResource(kTcatUriPath, HandleRequest, this)
    , mResponse(nullptr)
    , mPort(0)
{
    // TCAT uses the same AES-GCM based cipher suite over DTLS as it
    // does over TLS, instead of the AES-CCM-8 based one that is the
    // default for DTLS.
    SetUseAesGcmCipherSuite();

    Dtls::Transport::SetExtension(static_cast<Dtls::Transport::Extension &>(*this));
    Dtls::Transport::SetAcceptCallback(HandleAccept, this);
    SetConnectCallback(HandleConnectEvent, this);
}

Error TcatCoapServer::Start(uint16_t                          aPort,
                            TcatAgent::AppDataReceiveCallback aAppDataCallback,
                            TcatAgent::JoinCallback           aJoinHandler)
{
    Error error;

    VerifyOrExit(!IsStarted(), error = kErrorAlready);

    SuccessOrExit(error = Dtls::Transport::Open(aPort));
    mPort = aPort;

    // The TCAT Commissioner is not part of the Thread Network, so
    // its messages arrive without link-layer (MAC) security.
    error = Get<Ip6::Filter>().AddUnsecurePort(mPort);

    if (error == kErrorNone)
    {
        error = Get<TcatAgent>().Start(aAppDataCallback, aJoinHandler, /* aContext */ nullptr);
    }

    if (error != kErrorNone)
    {
        IgnoreError(Get<Ip6::Filter>().RemoveUnsecurePort(mPort));
        Dtls::Transport::Close();
        ExitNow();
    }

    AddResource(mResource);

    LogInfo("Started on port %u", mPort);

exit:
    return error;
}

void TcatCoapServer::Stop(void)
{
    VerifyOrExit(IsStarted());

    Disconnect();
    RemoveResource(mResource);
    IgnoreError(Get<Ip6::Filter>().RemoveUnsecurePort(mPort));
    Dtls::Transport::Close();
    Get<TcatAgent>().Stop();

    LogInfo("Stopped");

exit:
    return;
}

Error TcatCoapServer::SetTcatVendorInfo(const TcatAgent::VendorInfo &aVendorInfo)
{
    return Get<TcatAgent>().SetTcatVendorInfo(aVendorInfo);
}

Error TcatCoapServer::TcatActive(bool aActive, uint32_t aDelayMs, uint32_t aDurationMs)
{
    return aActive ? Get<TcatAgent>().Activate(aDelayMs, aDurationMs) : Get<TcatAgent>().Standby();
}

Error TcatCoapServer::SendApplicationTlv(TcatAgent::TcatApplicationProtocol aApplicationProtocol,
                                         const uint8_t                     *aBuf,
                                         uint16_t                           aLength)
{
    Error error;
    bool  isResponse = (aApplicationProtocol == TcatAgent::kApplicationProtocolStatus ||
                       aApplicationProtocol == TcatAgent::kApplicationProtocolResponse);

    VerifyOrExit(mResponse != nullptr, error = kErrorInvalidState);
    VerifyOrExit(aApplicationProtocol != TcatAgent::kApplicationProtocolNone, error = kErrorRejected);
    VerifyOrExit(!isResponse || Get<TcatAgent>().GetApplicationResponsePending(), error = kErrorRejected);

    SuccessOrExit(error = Tlv::AppendTlv(*mResponse, static_cast<uint8_t>(aApplicationProtocol), aBuf, aLength));

    if (isResponse)
    {
        Get<TcatAgent>().NotifyApplicationResponseSent();
    }

exit:
    return error;
}

MeshCoP::SecureSession *TcatCoapServer::HandleAccept(void *aContext, const Ip6::MessageInfo &aMessageInfo)
{
    OT_UNUSED_VARIABLE(aMessageInfo);

    return static_cast<TcatCoapServer *>(aContext)->HandleAccept();
}

MeshCoP::SecureSession *TcatCoapServer::HandleAccept(void)
{
    return IsSessionInUse() ? nullptr : static_cast<Coap::SecureSession *>(this);
}

void TcatCoapServer::HandleConnectEvent(Dtls::Session::ConnectEvent aEvent, void *aContext)
{
    static_cast<TcatCoapServer *>(aContext)->HandleConnectEvent(aEvent);
}

void TcatCoapServer::HandleConnectEvent(Dtls::Session::ConnectEvent aEvent)
{
    if (aEvent == Dtls::Session::kConnected)
    {
        Error error = Get<TcatAgent>().Connected(*this);

        if (error != kErrorNone)
        {
            LogWarn("Rejected TCAT Commissioner: %s", ErrorToString(error));
            Disconnect();
        }
    }
    else
    {
        Get<TcatAgent>().Disconnected();
    }
}

void TcatCoapServer::HandleRequest(void *aContext, otMessage *aMessage, const otMessageInfo *aMessageInfo)
{
    OT_UNUSED_VARIABLE(aMessageInfo);

    static_cast<TcatCoapServer *>(aContext)->HandleRequest(AsCoapMessage(aMessage));
}

void TcatCoapServer::HandleRequest(Coap::Message &aRequest)
{
    Error                   error = kErrorNone;
    OwnedPtr<Coap::Message> response;

    VerifyOrExit(Get<TcatAgent>().IsConnected(), error = kErrorInvalidState);

    response.Reset(AllocateAndInitResponseFor(aRequest));
    VerifyOrExit(response != nullptr, error = kErrorNoBufs);

    // TCAT application TLVs, which the application sends from within
    // its receive callback using `SendApplicationTlv()`, are added to
    // the same response.
    mResponse = response.Get();
    error     = ProcessRequestTlvs(aRequest, *response);
    mResponse = nullptr;

    if (error != kErrorNone && error != kErrorAbort)
    {
        // The request could not be processed: the response TLVs
        // collected so far, if any, are not sent.
        response.Reset(AllocateAndInitResponseFor(aRequest));
        VerifyOrExit(response != nullptr);
        response->WriteCode(Coap::kCodeInternalError);
    }

    IgnoreError(SendMessage(*response.Release()));

exit:
    // `kErrorAbort` signals that the TCAT Commissioner requested to
    // disconnect. Any other error is not recoverable for the session.
    if (error == kErrorAbort)
    {
        LogInfo("Disconnecting TCAT Commissioner");
        Disconnect();
    }
    else if (error != kErrorNone)
    {
        LogWarnOnError(error, "handle TCAT request");
        Disconnect();
    }
}

Error TcatCoapServer::ProcessRequestTlvs(const Coap::Message &aRequest, Coap::Message &aResponse)
{
    Error       error = kErrorNone;
    OffsetRange offsetRange;

    offsetRange.InitFromMessageOffsetToEnd(aRequest);

    while (!offsetRange.IsEmpty())
    {
        OwnedPtr<Message> tlvMessage;
        ExtendedTlv       extTlv;
        Tlv               tlv;
        uint32_t          size;

        SuccessOrExit(error = aRequest.Read(offsetRange, tlv));

        if (tlv.IsExtended())
        {
            SuccessOrExit(error = aRequest.Read(offsetRange, extTlv));
            size = extTlv.GetSize();
        }
        else
        {
            size = tlv.GetSize();
        }

        VerifyOrExit(offsetRange.Contains(size), error = kErrorParse);

        // The TCAT agent handles a single TLV at a time, which it
        // reads from the start of the message that is passed to it.
        tlvMessage.Reset(Get<MessagePool>().Allocate(Message::kTypeOther));
        VerifyOrExit(tlvMessage != nullptr, error = kErrorNoBufs);
        SuccessOrExit(
            error = tlvMessage->AppendBytesFromMessage(aRequest, offsetRange.GetOffset(), static_cast<uint16_t>(size)));

        SuccessOrExit(error = Get<TcatAgent>().HandleSingleTlv(*tlvMessage, aResponse));

        offsetRange.AdvanceOffset(size);
    }

exit:
    return error;
}

} // namespace MeshCoP
} // namespace ot

#endif // OPENTHREAD_CONFIG_TCAT_COAPS_ENABLE
