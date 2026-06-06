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
 * @brief
 *   This file includes the platform TCP driver.
 *
 *   It implements the `otPlatTcp` platform abstraction on top of the host's
 *   native (BSD socket) TCP stack. TCP segments for Thread mesh addresses flow
 *   through the host kernel and the `wpan0` tun interface (the platform netif),
 *   mirroring how the platform UDP driver (`udp.cpp`) reuses the host UDP stack.
 */

#include "openthread-posix-config.h"
#include "platform-posix.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <openthread/platform/tcp.h>

#include "common/code_utils.hpp"

#if OPENTHREAD_CONFIG_PLATFORM_TCP_ENABLE

#if !OPENTHREAD_CONFIG_PLATFORM_NETIF_ENABLE
#error "Platform TCP (OPENTHREAD_CONFIG_PLATFORM_TCP_ENABLE) requires the platform netif \
(OPENTHREAD_CONFIG_PLATFORM_NETIF_ENABLE) so the host kernel can route Thread-addressed TCP."
#endif

#include "posix/platform/ip6_utils.hpp"
#include "posix/platform/mainloop.hpp"
#include "posix/platform/tcp.hpp"
#include "posix/platform/utils.hpp"

// `MSG_NOSIGNAL` is not defined on every platform (e.g., macOS). Falling back to
// 0 is safe here; a write to a closed connection then surfaces via the error or
// receive path in `Process()` instead of as a signal.
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

using namespace ot::Posix::Ip6Utils;
using ot::Posix::kSocketNonBlock;
using ot::Posix::SocketWithCloseExec;
using ot::Posix::Tcp;

namespace {

constexpr int      kListenBacklog = 8;
constexpr uint16_t kMaxRecvLength = 1536;

int  GetFd(const otPlatTcpListener *aListener) { return aListener->mData.mDescriptor; }
int  GetFd(const otPlatTcpConnection *aConn) { return aConn->mData.mDescriptor; }
void SetFd(otPlatTcpListener *aListener, int aFd) { aListener->mData.mDescriptor = aFd; }
void SetFd(otPlatTcpConnection *aConn, int aFd) { aConn->mData.mDescriptor = aFd; }

void MakeSockAddrIn6(const otPlatTcpSockAddr &aSockAddr, struct sockaddr_in6 &aSin6)
{
    memset(&aSin6, 0, sizeof(aSin6));
    aSin6.sin6_family   = AF_INET6;
    aSin6.sin6_port     = htons(aSockAddr.mSockAddr.mPort);
    aSin6.sin6_scope_id = aSockAddr.mIfIndex;
    CopyIp6AddressTo(aSockAddr.mSockAddr.mAddress, &aSin6.sin6_addr);
}

void ReadSockAddrIn6(const struct sockaddr_in6 &aSin6, otPlatTcpSockAddr &aSockAddr)
{
    memset(&aSockAddr, 0, sizeof(aSockAddr));
    aSockAddr.mSockAddr.mPort = ntohs(aSin6.sin6_port);
    aSockAddr.mIfIndex        = aSin6.sin6_scope_id;
    ReadIp6AddressFrom(&aSin6.sin6_addr, aSockAddr.mSockAddr.mAddress);
}

otPlatTcpDisconnectReason MapErrno(int aErrno)
{
    otPlatTcpDisconnectReason reason;

    switch (aErrno)
    {
    case 0:
        reason = OT_PLAT_TCP_DISCONNECT_REASON_CLOSED;
        break;
    case ECONNREFUSED:
        reason = OT_PLAT_TCP_DISCONNECT_REASON_REFUSED;
        break;
    case ETIMEDOUT:
        reason = OT_PLAT_TCP_DISCONNECT_REASON_TIMEOUT;
        break;
    case ECONNRESET:
    case EPIPE:
        reason = OT_PLAT_TCP_DISCONNECT_REASON_RESET;
        break;
    default:
        reason = OT_PLAT_TCP_DISCONNECT_REASON_ERROR;
        break;
    }

    return reason;
}

int GetSoError(int aFd)
{
    int       soError = 0;
    socklen_t length  = sizeof(soError);

    if (getsockopt(aFd, SOL_SOCKET, SO_ERROR, &soError, &length) != 0)
    {
        soError = errno;
    }

    return soError;
}

void SetNonBlockingCloseExec(int aFd)
{
    int flags;

    flags = fcntl(aFd, F_GETFL, 0);
    VerifyOrExit(flags != -1);
    VerifyOrExit(fcntl(aFd, F_SETFL, flags | O_NONBLOCK) != -1);

    flags = fcntl(aFd, F_GETFD, 0);
    VerifyOrExit(flags != -1);
    VerifyOrExit(fcntl(aFd, F_SETFD, flags | FD_CLOEXEC) != -1);

exit:
    return;
}

} // namespace

//---------------------------------------------------------------------------------------------------------------------
// otPlatTcp functions

otError otPlatTcpEnableListener(otPlatTcpListener *aListener, const otPlatTcpSockAddr *aLocalSockAddr)
{
    otError             error = OT_ERROR_NONE;
    int                 fd    = -1;
    int                 on    = 1;
    struct sockaddr_in6 sin6;

    VerifyOrExit(aLocalSockAddr->mSockAddr.mPort != 0, error = OT_ERROR_FAILED);

    fd = SocketWithCloseExec(AF_INET6, SOCK_STREAM, IPPROTO_TCP, kSocketNonBlock);
    VerifyOrExit(fd >= 0, error = OT_ERROR_FAILED);

    VerifyOrExit(setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) == 0, error = OT_ERROR_FAILED);

    MakeSockAddrIn6(*aLocalSockAddr, sin6);

    if (bind(fd, reinterpret_cast<struct sockaddr *>(&sin6), sizeof(sin6)) != 0)
    {
        error = (errno == EADDRINUSE) ? OT_ERROR_ALREADY : OT_ERROR_FAILED;
        ExitNow();
    }

    VerifyOrExit(listen(fd, kListenBacklog) == 0, error = OT_ERROR_FAILED);

    SetFd(aListener, fd);

exit:
    if (error != OT_ERROR_NONE)
    {
        if (error != OT_ERROR_ALREADY)
        {
            Tcp::LogWarn("Failed to enable listener on [%s]:%u: %s",
                         Ip6AddressString(&aLocalSockAddr->mSockAddr.mAddress).AsCString(),
                         aLocalSockAddr->mSockAddr.mPort, strerror(errno));
        }

        if (fd >= 0)
        {
            close(fd);
        }
    }

    return error;
}

void otPlatTcpDisableListener(otPlatTcpListener *aListener)
{
    int fd = GetFd(aListener);

    if (fd > 0)
    {
        close(fd);
    }

    SetFd(aListener, -1);
}

otError otPlatTcpConnect(otPlatTcpConnection     *aConn,
                         const otPlatTcpSockAddr *aPeerSockAddr,
                         const otPlatTcpSockAddr *aLocalSockAddr)
{
    otError             error = OT_ERROR_NONE;
    int                 fd    = -1;
    struct sockaddr_in6 peer;

    fd = SocketWithCloseExec(AF_INET6, SOCK_STREAM, IPPROTO_TCP, kSocketNonBlock);
    VerifyOrExit(fd >= 0, error = OT_ERROR_FAILED);

    if (aLocalSockAddr != nullptr)
    {
        struct sockaddr_in6 local;

        MakeSockAddrIn6(*aLocalSockAddr, local);
        VerifyOrExit(bind(fd, reinterpret_cast<struct sockaddr *>(&local), sizeof(local)) == 0,
                     error = OT_ERROR_FAILED);
    }

    MakeSockAddrIn6(*aPeerSockAddr, peer);

    // A link-local destination requires a scope (interface) id. Use the Thread
    // netif when the caller did not provide one.
    if ((peer.sin6_scope_id == 0) && IsIp6AddressLinkLocal(aPeerSockAddr->mSockAddr.mAddress))
    {
        peer.sin6_scope_id = gNetifIndex;
    }

    // With a non-blocking socket, `connect()` returns `EINPROGRESS`; completion
    // (or failure) is reported later when the socket becomes writable.
    if (connect(fd, reinterpret_cast<struct sockaddr *>(&peer), sizeof(peer)) != 0)
    {
        VerifyOrExit(errno == EINPROGRESS, error = OT_ERROR_FAILED);
    }

    SetFd(aConn, fd);

exit:
    if (error != OT_ERROR_NONE)
    {
        Tcp::LogWarn("Failed to connect to [%s]:%u: %s",
                     Ip6AddressString(&aPeerSockAddr->mSockAddr.mAddress).AsCString(), aPeerSockAddr->mSockAddr.mPort,
                     strerror(errno));

        if (fd >= 0)
        {
            close(fd);
        }
    }

    return error;
}

void otPlatTcpNotifyTxPending(otPlatTcpConnection *aConn)
{
    // No work needed here: `Tcp::Update()` re-evaluates `otPlatTcpIsTxPending()`
    // on every mainloop pass and arms the socket for writability accordingly.
    // The pending tasklet that triggered this call also wakes the mainloop.
    OT_UNUSED_VARIABLE(aConn);
}

uint16_t otPlatTcpSend(otPlatTcpConnection *aConn, const uint8_t *aBuffer, uint16_t aLength)
{
    int      fd   = GetFd(aConn);
    uint16_t sent = 0;
    ssize_t  rval;

    VerifyOrExit(fd > 0);
    VerifyOrExit(aLength > 0);

    rval = send(fd, aBuffer, aLength, MSG_DONTWAIT | MSG_NOSIGNAL);

    // A short count (including 0 on `EAGAIN`) leaves the remainder queued in the
    // core, which re-arms transmission via `otPlatTcpNotifyTxPending()`. A hard
    // error also returns 0 here; the disconnect is detected and reported from
    // `Process()` (error or receive path), avoiding re-entrancy into the core's
    // `HandleTxReady()` send loop.
    if (rval > 0)
    {
        sent = static_cast<uint16_t>(rval);
    }

exit:
    return sent;
}

void otPlatTcpClose(otPlatTcpConnection *aConn)
{
    int fd = GetFd(aConn);

    // The core only calls this after all queued TX has been handed to the
    // platform, so `shutdown(SHUT_WR)` sends a FIN after the kernel flushes its
    // send buffer. The connection is kept tracked; the receive path completes
    // the closure on the peer's FIN (`recv()` returns 0).
    if ((fd <= 0) || (shutdown(fd, SHUT_WR) != 0))
    {
        SetFd(aConn, -1);

        if (fd > 0)
        {
            close(fd);
        }

        otPlatTcpHandleDisconnected(aConn, OT_PLAT_TCP_DISCONNECT_REASON_CLOSED);
    }
}

void otPlatTcpAbort(otPlatTcpConnection *aConn)
{
    int           fd = GetFd(aConn);
    struct linger lingerOpt;

    VerifyOrExit(fd > 0);

    // Force a RST (instead of a graceful FIN) on close, discarding unsent data.
    lingerOpt.l_onoff  = 1;
    lingerOpt.l_linger = 0;
    (void)setsockopt(fd, SOL_SOCKET, SO_LINGER, &lingerOpt, sizeof(lingerOpt));

    SetFd(aConn, -1);
    close(fd);

exit:
    // Per the API, no callbacks may be invoked after `otPlatTcpAbort()`.
    return;
}

namespace ot {
namespace Posix {

const char Tcp::kLogModuleName[] = "Tcp";

//---------------------------------------------------------------------------------------------------------------------
// Mainloop processing helpers

static void FinalizeDisconnect(otPlatTcpConnection *aConn, otPlatTcpDisconnectReason aReason)
{
    int fd = GetFd(aConn);

    // Detach the fd before the callback: the consumer's disconnect handler may
    // immediately reconnect (re-using the `Connection`) and install a fresh fd.
    SetFd(aConn, -1);
    otPlatTcpHandleDisconnected(aConn, aReason);

    if (fd > 0)
    {
        close(fd);
    }
}

static void ProcessListener(otPlatTcpListener *aListener, int aFd)
{
    for (;;)
    {
        struct sockaddr_in6  sin6;
        socklen_t            length = sizeof(sin6);
        int                  connFd;
        otPlatTcpSockAddr    peerSockAddr;
        otPlatTcpConnection *conn;

        connFd = accept(aFd, reinterpret_cast<struct sockaddr *>(&sin6), &length);
        VerifyOrExit(connFd >= 0); // `EAGAIN` (no more pending) or an error: stop accepting for now.

        SetNonBlockingCloseExec(connFd);
        ReadSockAddrIn6(sin6, peerSockAddr);

        conn = otPlatTcpAccept(aListener, &peerSockAddr);

        if (conn == nullptr)
        {
            close(connFd);
            continue;
        }

        SetFd(conn, connFd);
        otPlatTcpHandleConnected(conn); // An accepted socket is already established.
    }

exit:
    return;
}

static void ProcessConnection(otPlatTcpConnection *aConn, const Mainloop::Context &aContext)
{
    int fd = GetFd(aConn);

    VerifyOrExit(fd > 0);

    if (Mainloop::HasFdErrored(fd, aContext))
    {
        FinalizeDisconnect(aConn, MapErrno(GetSoError(fd)));
        ExitNow();
    }

    if (Mainloop::IsFdWritable(fd, aContext))
    {
        if (otPlatTcpIsConnecting(aConn))
        {
            int soError = GetSoError(fd);

            if (soError == 0)
            {
                otPlatTcpHandleConnected(aConn);
            }
            else
            {
                FinalizeDisconnect(aConn, MapErrno(soError));
                ExitNow();
            }
        }
        else if (otPlatTcpIsTxPending(aConn))
        {
            otPlatTcpHandleTxReady(aConn);
        }

        // The handler above may have closed, aborted, or reconnected the socket.
        fd = GetFd(aConn);
        VerifyOrExit(fd > 0);
    }

    if (Mainloop::IsFdReadable(fd, aContext))
    {
        uint8_t buffer[kMaxRecvLength];
        ssize_t rval = recv(fd, buffer, sizeof(buffer), 0);

        if (rval > 0)
        {
            otPlatTcpHandleReceive(aConn, buffer, static_cast<uint16_t>(rval));
        }
        else if (rval == 0)
        {
            FinalizeDisconnect(aConn, OT_PLAT_TCP_DISCONNECT_REASON_CLOSED);
        }
        else if ((errno != EAGAIN) && (errno != EWOULDBLOCK))
        {
            FinalizeDisconnect(aConn, MapErrno(errno));
        }
    }

exit:
    return;
}

//---------------------------------------------------------------------------------------------------------------------
// Tcp

Tcp &Tcp::Get(void)
{
    static Tcp sInstance;

    return sInstance;
}

void Tcp::Init(const char *aIfName)
{
    // The Thread netif name/index (`gNetifName`/`gNetifIndex`) are owned and
    // populated by `netif.cpp` (`platformNetifInit`). Nothing to set up here; the
    // `Update`/`Process` passes guard on `gNetifIndex != 0`.
    OT_UNUSED_VARIABLE(aIfName);
}

void Tcp::SetUp(void) { Mainloop::Manager::Get().Add(*this); }

void Tcp::TearDown(void) { Mainloop::Manager::Get().Remove(*this); }

void Tcp::Deinit(void)
{
    // Active listeners and connections are torn down by the OpenThread stack,
    // which disables listeners and aborts connections. That routes through
    // `otPlatTcpDisableListener()` / `otPlatTcpAbort()`, closing the sockets.
}

void Tcp::Update(Mainloop::Context &aContext)
{
    otPlatTcpListener   *listener = nullptr;
    otPlatTcpConnection *conn     = nullptr;

    VerifyOrExit(gNetifIndex != 0);

    while ((listener = otPlatTcpIterateListeners(gInstance, listener)) != nullptr)
    {
        int fd = GetFd(listener);

        if (fd > 0)
        {
            Mainloop::AddToReadFdSet(fd, aContext);
        }
    }

    while ((conn = otPlatTcpIterateConnections(gInstance, conn)) != nullptr)
    {
        int fd = GetFd(conn);

        if (fd <= 0)
        {
            continue;
        }

        Mainloop::AddToReadFdSet(fd, aContext);
        Mainloop::AddToErrorFdSet(fd, aContext);

        if (otPlatTcpIsConnecting(conn) || otPlatTcpIsTxPending(conn))
        {
            Mainloop::AddToWriteFdSet(fd, aContext);
        }
    }

exit:
    return;
}

void Tcp::Process(const Mainloop::Context &aContext)
{
    otPlatTcpListener   *listener = nullptr;
    otPlatTcpConnection *conn     = nullptr;

    VerifyOrExit(gNetifIndex != 0);

    while ((listener = otPlatTcpIterateListeners(gInstance, listener)) != nullptr)
    {
        int fd = GetFd(listener);

        if ((fd > 0) && Mainloop::IsFdReadable(fd, aContext))
        {
            ProcessListener(listener, fd);
        }
    }

    // Iterating while processing is safe: a connection reported as disconnected
    // (via `FinalizeDisconnect`) is kept valid by the core until the iteration
    // completes (its removal is deferred to a tasklet).
    while ((conn = otPlatTcpIterateConnections(gInstance, conn)) != nullptr)
    {
        ProcessConnection(conn, aContext);
    }

exit:
    return;
}

} // namespace Posix
} // namespace ot

#endif // OPENTHREAD_CONFIG_PLATFORM_TCP_ENABLE
