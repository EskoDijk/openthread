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
 * @brief
 *   Platform abstraction for TCAT UDP/DTLS outgoing packet delivery.
 *
 *   This header is only relevant for platforms that implement TCAT over UDP
 *   using the simulation callback transport mode (where OT core does not own
 *   the host socket and instead delegates TX to the platform layer).
 */

#ifndef OPENTHREAD_PLATFORM_TCAT_UDP_H_
#define OPENTHREAD_PLATFORM_TCAT_UDP_H_

#include <stdint.h>
#include <openthread/instance.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Sends an outgoing DTLS packet to the TCAT commissioner on the host.
 *
 * Called by OT core (`TcatUdpServer`) when the DTLS state machine produces
 * an outgoing record. The platform is responsible for delivering the raw
 * bytes to the destination via whatever host mechanism it uses (e.g. a
 * host-side UDP socket in simulation).
 *
 * A default weak no-op implementation is provided for platforms that use
 * the socket-based transport mode and never call this function.
 *
 * @param[in] aInstance  The OT instance.
 * @param[in] aBuf       Pointer to the raw DTLS record bytes.
 * @param[in] aLen       Length of @p aBuf in bytes.
 * @param[in] aDstIp4    Destination IPv4 address in network byte order.
 * @param[in] aDstPort   Destination UDP port in host byte order.
 */
void otPlatTcatUdpSend(otInstance    *aInstance,
                       const uint8_t *aBuf,
                       uint16_t       aLen,
                       uint32_t       aDstIp4,
                       uint16_t       aDstPort);

#ifdef __cplusplus
}
#endif

#endif // OPENTHREAD_PLATFORM_TCAT_UDP_H_
