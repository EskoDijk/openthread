#!/bin/bash
#
#  Copyright (c) 2026, The OpenThread Authors.
#  All rights reserved.
#
#  Redistribution and use in source and binary forms, with or without
#  modification, are permitted provided that the following conditions are met:
#  1. Redistributions of source code must retain the above copyright
#     notice, this list of conditions and the following disclaimer.
#  2. Redistributions in binary form must reproduce the above copyright
#     notice, this list of conditions and the following disclaimer in the
#     documentation and/or other materials provided with the distribution.
#  3. Neither the name of the copyright holder nor the
#     names of its contributors may be used to endorse or promote products
#     derived from this software without specific prior written permission.
#
#  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
#  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
#  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
#  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
#  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
#  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
#  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
#  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
#  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
#  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
#  POSSIBILITY OF SUCH DAMAGE.
#

# End-to-end simulation test of TCAT over CoAPS, without needing root:
#   TCAT Commissioner (bbtc.py --coaps) -> wpan0 (ot-daemon + simulated RCP) -> simulated TCAT Device (ot-cli-ftd).
#
# With RCP_PORT and DEV_PORT set to serial ports, it uses a real RCP and a real TCAT Device instead.
#
# Usage, scripted:
#   PANID=0xc64e CHANNEL=18 ./tools/tcat-coaps-sim-test/run.sh "hello;commission;thread start" ["bbtc args"]
#
# Usage, interactive: the TCAT Commissioner CLI runs in the terminal, until 'exit'.
#   PANID=0xc64e CHANNEL=18 ./tools/tcat-coaps-sim-test/run.sh -i [bbtc args]
#
# See README.txt for the builds that are needed, the environment variables and more details.

set -euo pipefail

TEST_DIR="$(dirname "$0")"
readonly TEST_DIR

if [[ -z ${TCAT_TEST_IN_NS:-} ]]; then
    if [[ -z ${VENV:-} ]]; then
        VENV=$(poetry --directory "${TEST_DIR}/../tcat_ble_client" env info --path) || true
    fi
    if [[ ! -x ${VENV:-}/bin/python ]]; then
        echo "No Python venv found: run 'poetry --directory tools/tcat_ble_client install' or set VENV" >&2
        exit 1
    fi
    export VENV

    # Continue in new user, network and mount namespaces, in which the current user is mapped to root.
    TCAT_TEST_IN_NS=1 exec unshare -Urnm "$0" "$@"
fi

# For the socket of ot-daemon
mount -t tmpfs tmpfs /run

# For the simulated radio, which uses UDP multicast
ip link set lo up
ip link set lo multicast on
ip route add 224.0.0.0/4 dev lo

exec "${VENV}/bin/python" "${TEST_DIR}/e2e.py" "$@"
