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
 *   Implements transport-agnostic TCAT public API functions.
 */

#include "openthread-core-config.h"

#if OPENTHREAD_CONFIG_TCAT_ENABLE

#include <openthread/tcat.h>

#include "common/as_core_type.hpp"
#include "instance/instance.hpp"
#include "meshcop/tcat_agent.hpp"

#if OPENTHREAD_CONFIG_BLE_TCAT_ENABLE
#include "radio/ble_secure.hpp"
#endif
#if OPENTHREAD_CONFIG_TCAT_UDP_ENABLE
#include "meshcop/tcat_udp_server.hpp"
#endif

using namespace ot;

otError otTcatSetVendorInfo(otInstance *aInstance, const otTcatVendorInfo *aVendorInfo)
{
    return AsCoreType(aInstance).Get<MeshCoP::TcatAgent>().SetTcatVendorInfo(AsCoreType(aVendorInfo));
}

otError otTcatSetAgentState(otInstance *aInstance, bool aActive, uint32_t aDelayMs, uint32_t aDurationMs)
{
    if (aActive)
    {
        return AsCoreType(aInstance).Get<MeshCoP::TcatAgent>().Activate(aDelayMs, aDurationMs);
    }
    else
    {
        return AsCoreType(aInstance).Get<MeshCoP::TcatAgent>().Standby();
    }
}

otError otTcatSendApplicationTlv(otInstance               *aInstance,
                                 otTcatApplicationProtocol aApplicationProtocol,
                                 uint8_t                  *aBuf,
                                 uint16_t                  aLength)
{
#if OPENTHREAD_CONFIG_BLE_TCAT_ENABLE
    return AsCoreType(aInstance).Get<Ble::BleSecure>().SendApplicationTlv(MapEnum(aApplicationProtocol), aBuf, aLength);
#elif OPENTHREAD_CONFIG_TCAT_UDP_ENABLE
    return AsCoreType(aInstance).Get<MeshCoP::TcatUdpServer>().SendApplicationTlv(
        MapEnum(aApplicationProtocol), aBuf, aLength);
#endif
}

#endif // OPENTHREAD_CONFIG_TCAT_ENABLE
