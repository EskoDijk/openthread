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
 *   This file implements the transport-agnostic OpenThread TCAT API.
 */

#include "openthread-core-config.h"

#if OPENTHREAD_CONFIG_TCAT_ENABLE

#include <openthread/tcat.h>

#include "common/as_core_type.hpp"
#include "common/code_utils.hpp"
#include "common/debug.hpp"
#include "instance/instance.hpp"
#include "meshcop/tcat_agent.hpp"

using namespace ot;

static Ble::BleSecure &GetTcatTransport(otInstance *aInstance) { return AsCoreType(aInstance).Get<Ble::BleSecure>(); }

otError otTcatSetVendorInfo(otInstance *aInstance, const otTcatVendorInfo *aVendorInfo)
{
    return GetTcatTransport(aInstance).SetTcatVendorInfo(AsCoreType(aVendorInfo));
}

void otTcatSetCertificate(otInstance    *aInstance,
                          const uint8_t *aX509Cert,
                          uint32_t       aX509Length,
                          const uint8_t *aPrivateKey,
                          uint32_t       aPrivateKeyLength)
{
    OT_ASSERT(aX509Cert != nullptr && aX509Length != 0 && aPrivateKey != nullptr && aPrivateKeyLength != 0);

    GetTcatTransport(aInstance).SetCertificate(aX509Cert, aX509Length, aPrivateKey, aPrivateKeyLength);
}

void otTcatSetCaCertificateChain(otInstance    *aInstance,
                                 const uint8_t *aX509CaCertificateChain,
                                 uint32_t       aX509CaCertChainLength)
{
    OT_ASSERT(aX509CaCertificateChain != nullptr && aX509CaCertChainLength != 0);

    GetTcatTransport(aInstance).SetCaCertificateChain(aX509CaCertificateChain, aX509CaCertChainLength);
}

void otTcatSetSslAuthMode(otInstance *aInstance, bool aVerifyPeerCertificate)
{
    GetTcatTransport(aInstance).SetSslAuthMode(aVerifyPeerCertificate);
}

otError otTcatStart(otInstance                        *aInstance,
                    uint16_t                           aUdpPort,
                    otHandleTcatApplicationDataReceive aAppDataHandler,
                    otHandleTcatJoin                   aJoinHandler)
{
    Error           error;
    Ble::BleSecure &bleSecure = GetTcatTransport(aInstance);

    OT_UNUSED_VARIABLE(aUdpPort);

    SuccessOrExit(error = bleSecure.Start(/* aConnectHandler */ nullptr, aAppDataHandler, /* aTlvMode */ true,
                                          /* aContext */ nullptr));
    error = bleSecure.TcatStart(aJoinHandler);

exit:
    return error;
}

void otTcatStop(otInstance *aInstance) { GetTcatTransport(aInstance).Stop(); }

bool otTcatIsAgentStarted(otInstance *aInstance) { return AsCoreType(aInstance).Get<MeshCoP::TcatAgent>().IsStarted(); }

otError otTcatSetAgentState(otInstance *aInstance, bool aActive, uint32_t aDelayMs, uint32_t aDurationMs)
{
    return GetTcatTransport(aInstance).TcatActive(aActive, aDelayMs, aDurationMs);
}

otError otTcatSendApplicationTlv(otInstance               *aInstance,
                                 otTcatApplicationProtocol aApplicationProtocol,
                                 uint8_t                  *aBuf,
                                 uint16_t                  aLength)
{
    return GetTcatTransport(aInstance).SendApplicationTlv(MapEnum(aApplicationProtocol), aBuf, aLength);
}

#endif // OPENTHREAD_CONFIG_TCAT_ENABLE
