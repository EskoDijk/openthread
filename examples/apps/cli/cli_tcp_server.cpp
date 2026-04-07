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

#include "cli_tcp_server.hpp"

#if OPENTHREAD_CONFIG_TCP_ENABLE && OPENTHREAD_POSIX_CONFIG_CLI_TCP_SERVER_ENABLE

#include <stdio.h>
#include <string.h>

#include <openthread/cli.h>
#include <openthread/logging.h>
#include <openthread/tcp.h>
#include <openthread/tcp_ext.h>

#include "cli/cli_config.h"

namespace {

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

static constexpr uint16_t kPort          = OPENTHREAD_POSIX_CONFIG_CLI_TCP_SERVER_PORT;
static constexpr size_t   kSendBufSize   = OPENTHREAD_POSIX_CONFIG_CLI_TCP_SEND_BUFFER_SIZE;
static constexpr size_t   kMaxLineLength = OPENTHREAD_CONFIG_CLI_MAX_LINE_LENGTH;

static const char kBanner[] = "OpenThread CLI (TCP)\r\n";

// ---------------------------------------------------------------------------
// Server state
// ---------------------------------------------------------------------------

struct CliTcpServer
{
    otInstance             *mInstance;

    otTcpListener           mListener;
    otTcpEndpoint           mEndpoint;
    bool                    mConnected;

    uint8_t                 mReceiveBuf[OT_TCP_RECEIVE_BUFFER_SIZE_FEW_HOPS];

    otTcpCircularSendBuffer mSendBuf;
    uint8_t                 mSendBufBytes[kSendBufSize];

    // Accumulates incoming bytes until a newline is received.
    char                    mLineBuf[kMaxLineLength];
    uint16_t                mLineLen;
};

CliTcpServer sServer;

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static void TcpSend(const char *aData, size_t aLen)
{
    if (!sServer.mConnected || aLen == 0)
    {
        return;
    }

    size_t written;
    // Excess bytes are silently dropped when the buffer is full.
    otTcpCircularSendBufferWrite(&sServer.mEndpoint, &sServer.mSendBuf, aData, aLen, &written, 0);
}

// ---------------------------------------------------------------------------
// TCP callbacks
// ---------------------------------------------------------------------------

static otTcpIncomingConnectionAction HandleAcceptReady(otTcpListener    *aListener,
                                                       const otSockAddr *aPeer,
                                                       otTcpEndpoint   **aAcceptInto)
{
    (void)aListener;
    (void)aPeer;

    if (sServer.mConnected)
    {
        return OT_TCP_INCOMING_CONNECTION_ACTION_DEFER;
    }

    *aAcceptInto = &sServer.mEndpoint;
    return OT_TCP_INCOMING_CONNECTION_ACTION_ACCEPT;
}

static void HandleAcceptDone(otTcpListener    *aListener,
                              otTcpEndpoint    *aEndpoint,
                              const otSockAddr *aPeer)
{
    (void)aListener;
    (void)aEndpoint;
    (void)aPeer;

    sServer.mConnected = true;
    sServer.mLineLen   = 0;

    TcpSend(kBanner, sizeof(kBanner) - 1);
}

static void HandleReceiveAvailable(otTcpEndpoint *aEndpoint,
                                    size_t         aBytesAvailable,
                                    bool           aEndOfStream,
                                    size_t         aBytesRemaining)
{
    (void)aBytesRemaining;

    if (aBytesAvailable > 0)
    {
        const otLinkedBuffer *buf;
        size_t                totalConsumed = 0;

        otTcpReceiveByReference(aEndpoint, &buf);

        for (; buf != nullptr; buf = buf->mNext)
        {
            for (size_t i = 0; i < buf->mLength; i++)
            {
                char c = static_cast<char>(buf->mData[i]);

                if (c == '\r')
                {
                    continue; // Telnet sends CRLF; skip CR.
                }

                if (c == '\n')
                {
                    sServer.mLineBuf[sServer.mLineLen] = '\0';
                    otCliInputLine(sServer.mLineBuf);
                    sServer.mLineLen = 0;
                }
                else if (sServer.mLineLen < static_cast<uint16_t>(kMaxLineLength - 1))
                {
                    sServer.mLineBuf[sServer.mLineLen++] = c;
                }
                // Bytes that overflow the line buffer are silently dropped.
            }

            totalConsumed += buf->mLength;
        }

        otTcpCommitReceive(aEndpoint, totalConsumed, 0);
    }

    if (aEndOfStream)
    {
        otTcpAbort(aEndpoint);
    }
}

static void HandleForwardProgress(otTcpEndpoint *aEndpoint, size_t aInSendBuffer, size_t aBacklog)
{
    (void)aEndpoint;
    (void)aBacklog;
    // Required by the circular send buffer to track acknowledged bytes.
    otTcpCircularSendBufferHandleForwardProgress(&sServer.mSendBuf, aInSendBuffer);
}

static void HandleDisconnected(otTcpEndpoint *aEndpoint, otTcpDisconnectedReason aReason)
{
    (void)aEndpoint;
    (void)aReason;

    otTcpCircularSendBufferForceDiscardAll(&sServer.mSendBuf);
    sServer.mConnected = false;
    sServer.mLineLen   = 0;
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

extern "C" {

void otAppCliTcpServerInit(otInstance *aInstance)
{
    otError    error;
    otSockAddr listenAddr;

    memset(&sServer, 0, sizeof(sServer));
    sServer.mInstance = aInstance;

    otTcpCircularSendBufferInitialize(&sServer.mSendBuf, sServer.mSendBufBytes, kSendBufSize);

    // --- Endpoint ---
    {
        otTcpEndpointInitializeArgs args;
        memset(&args, 0, sizeof(args));
        args.mContext                  = &sServer;
        args.mReceiveAvailableCallback = HandleReceiveAvailable;
        args.mForwardProgressCallback  = HandleForwardProgress;
        args.mDisconnectedCallback     = HandleDisconnected;
        args.mReceiveBuffer            = sServer.mReceiveBuf;
        args.mReceiveBufferSize        = sizeof(sServer.mReceiveBuf);

        error = otTcpEndpointInitialize(aInstance, &sServer.mEndpoint, &args);

        if (error != OT_ERROR_NONE)
        {
            otLogWarnPlat("TCP CLI: endpoint init failed: %d", error);
            return;
        }
    }

    // --- Listener ---
    {
        otTcpListenerInitializeArgs args;
        memset(&args, 0, sizeof(args));
        args.mContext             = &sServer;
        args.mAcceptReadyCallback = HandleAcceptReady;
        args.mAcceptDoneCallback  = HandleAcceptDone;

        error = otTcpListenerInitialize(aInstance, &sServer.mListener, &args);

        if (error != OT_ERROR_NONE)
        {
            otLogWarnPlat("TCP CLI: listener init failed: %d", error);
            otTcpEndpointDeinitialize(&sServer.mEndpoint);
            return;
        }
    }

    // --- Listen on [::]:port ---
    memset(&listenAddr, 0, sizeof(listenAddr));
    listenAddr.mPort = kPort;

    error = otTcpListen(&sServer.mListener, &listenAddr);

    if (error != OT_ERROR_NONE)
    {
        otLogWarnPlat("TCP CLI: listen on port %u failed: %d", kPort, error);
        otTcpListenerDeinitialize(&sServer.mListener);
        otTcpEndpointDeinitialize(&sServer.mEndpoint);
        return;
    }

    otLogInfoPlat("TCP CLI server listening on port %u", kPort);
}

void otAppCliTcpServerOutput(const char *aFormat, va_list aArguments)
{
    char buf[kSendBufSize];
    int  len;

    if (!sServer.mConnected)
    {
        return;
    }

    len = vsnprintf(buf, sizeof(buf), aFormat, aArguments);

    if (len > 0)
    {
        TcpSend(buf, static_cast<size_t>(len));
    }
}

} // extern "C"

#endif // OPENTHREAD_CONFIG_TCP_ENABLE && OPENTHREAD_POSIX_CONFIG_CLI_TCP_SERVER_ENABLE
