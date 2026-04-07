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
 *   Telnet-style TCP CLI server (demo, no security).
 *
 *   Accepts a single TCP connection and bridges it to the OpenThread CLI
 *   session.  The existing stdio/UART CLI continues to work concurrently.
 *
 *   Guarded by OPENTHREAD_CONFIG_TCP_ENABLE and
 *   OPENTHREAD_POSIX_CONFIG_CLI_TCP_SERVER_ENABLE.  The latter is set to 1
 *   via cmake target_compile_definitions when OT_TCP is ON.
 */

#ifndef CLI_TCP_SERVER_HPP_
#define CLI_TCP_SERVER_HPP_

#include "openthread-core-config.h"

#if OPENTHREAD_CONFIG_TCP_ENABLE && OPENTHREAD_POSIX_CONFIG_CLI_TCP_SERVER_ENABLE

#include <stdarg.h>

#include <openthread/instance.h>

// ---------------------------------------------------------------------------
// Per-platform config defaults (can be overridden before including this file)
// ---------------------------------------------------------------------------

#ifndef OPENTHREAD_POSIX_CONFIG_CLI_TCP_SERVER_PORT
#define OPENTHREAD_POSIX_CONFIG_CLI_TCP_SERVER_PORT 23
#endif

#ifndef OPENTHREAD_POSIX_CONFIG_CLI_TCP_SEND_BUFFER_SIZE
#define OPENTHREAD_POSIX_CONFIG_CLI_TCP_SEND_BUFFER_SIZE 2048
#endif

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initializes the TCP CLI server and starts listening.
 *
 * Must be called after otCliInit().  Listens on all IPv6 addresses (::) on
 * OPENTHREAD_POSIX_CONFIG_CLI_TCP_SERVER_PORT.
 *
 * @param[in]  aInstance  The OpenThread instance.
 */
void otAppCliTcpServerInit(otInstance *aInstance);

/**
 * Forwards formatted CLI output to any currently connected TCP client.
 *
 * Call from the otCliOutputCallback with a va_copy of the argument list so
 * the original is still usable for the UART/stdio path.
 *
 * @param[in]  aFormat     printf-style format string.
 * @param[in]  aArguments  Argument list for @p aFormat.
 */
void otAppCliTcpServerOutput(const char *aFormat, va_list aArguments);

#ifdef __cplusplus
}
#endif

#endif // OPENTHREAD_CONFIG_TCP_ENABLE && OPENTHREAD_POSIX_CONFIG_CLI_TCP_SERVER_ENABLE

#endif // CLI_TCP_SERVER_HPP_
