#!/bin/bash
# Run ot-daemon with a simulated RCP via forkpty for OTNS realtime mode.
# Configures channel/PAN/unsecured port, then drops into interactive ot-ctl
# so OTNS can take the steering wheel via the CLI.
#
# Usage: ./run-otns-ot-daemon.sh <node-id> [unix-socket-name]
#
# <node-id>          simulation node ID (required)
# [unix-socket-name] OTNS unix socket path (e.g. /tmp/otns_abc123)

NODE_ID=${1:?Usage: $0 <node-id> [unix-socket-name]}

# --- settable params ---
CHANNEL=11
PANID=0xface
TCAT_PORT=5684
# -----------------------

RCP=../ot-ns/ot-rfsim/ot-versions/ot-rcp
DAEMON=./build/posix/src/posix/ot-daemon
CTL=./build/posix/src/posix/ot-ctl

URL="spinel+hdlc+forkpty://${RCP}?forkpty-arg=${NODE_ID}"

if [ -n "$2" ]; then
    URL="${URL}&forkpty-arg=$2"
fi

echo "Starting ot-daemon with URL: ${URL}"
sudo "${DAEMON}" "${URL}" &
DAEMON_PID=$!
trap "sudo kill ${DAEMON_PID} 2>/dev/null" EXIT

# Wait for daemon socket to be ready
sleep 1

echo "Configuring: channel=${CHANNEL} panid=${PANID} tcat-port=${TCAT_PORT}"
sudo "${CTL}" channel ${CHANNEL}
sudo "${CTL}" panid ${PANID}
sudo "${CTL}" ifconfig up
sudo "${CTL}" unsecureport add ${TCAT_PORT}

echo ""
echo "ot-daemon ready. Entering interactive ot-ctl (exit to stop daemon)."
sudo "${CTL}"
