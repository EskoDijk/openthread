#!/bin/bash
# Run ot-daemon with a real RCP USB dongle for TCAT commissioning.
# The daemon stays off-network (ifconfig up, thread stop) so 6LoWPAN frames
# are MAC-unsecured, matching the uncommissioned TCAT device's state.
#
# Usage: ./run-ot-daemon.sh [device] [baudrate]

DEVICE=${1:-/dev/ttyACM0}
BAUD=${2:-115200}

# --- settable params ---
CHANNEL=11
PANID=0xface
TCAT_PORT=5684
# -----------------------

DAEMON=./build/posix/src/posix/ot-daemon
CTL=./build/posix/src/posix/ot-ctl

URL="spinel+hdlc+uart://${DEVICE}?uart-baudrate=${BAUD}"

echo "Starting ot-daemon on ${DEVICE} at ${BAUD} baud"
sudo "${DAEMON}" "${URL}" &
DAEMON_PID=$!

# Wait for daemon socket to be ready
sleep 1

echo "Configuring: channel=${CHANNEL} panid=${PANID} tcat-port=${TCAT_PORT}"
sudo "${CTL}" channel ${CHANNEL}
sudo "${CTL}" panid ${PANID}
sudo "${CTL}" ifconfig up
sudo "${CTL}" unsecureport add ${TCAT_PORT}

echo ""
echo "ot-daemon ready. wpan0 is up, MAC-unsecured on port ${TCAT_PORT}."
echo "Press Ctrl+C to stop."
wait ${DAEMON_PID}
