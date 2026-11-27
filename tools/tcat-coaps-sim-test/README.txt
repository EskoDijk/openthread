TCAT over CoAPS - end-to-end simulation test
============================================

This directory holds a manual smoke test for TCAT over CoAPS that runs without
root/sudo and without hardware:

    TCAT Commissioner        host interface                 TCAT Device
    bbtc.py --coaps   --->   wpan0                   --->   ot-cli-ftd
                             (ot-daemon + simulated         (simulated,
                              ot-rcp, node 2)                node 1)

It has two modes: a scripted one that types a given list of commands into the
commissioner, and an interactive one in which you use the commissioner CLI
yourself.

It is not a pass/fail test: it prints the output of all three parts and you
read the result. There are no assertions, so a failed command only shows up in
the printed output.

Files:

    run.sh    sets up the sandbox (namespaces), then runs e2e.py
    e2e.py    starts the device, the daemon and the commissioner, drives them


Prerequisites
-------------

Builds, by default expected under build/tcat-coaps (override with BUILD_DIR):

    OT_CMAKE_NINJA_TARGET="ot-cli-ftd ot-rcp" \
    OT_CMAKE_BUILD_DIR=build/tcat-coaps/sim-coaps \
        ./script/cmake-build simulation -DOT_TCAT_COAPS=ON -DOT_BLE_TCAT=OFF \
        -DOT_TREL=OFF -DOT_FULL_LOGS=OFF

    OT_CMAKE_BUILD_DIR=build/tcat-coaps/posix \
        ./script/cmake-build posix -DOT_DAEMON=ON -DOT_TREL=OFF

The Python virtual environment of the TCAT Commissioner, as created by Poetry:

    poetry --directory tools/tcat_ble_client install

run.sh uses this venv by default. Set VENV to use another venv that has the
dependencies listed in tools/tcat_ble_client/pyproject.toml.


Usage: scripted
---------------

    PANID=0xc64e CHANNEL=18 \
        ./tools/tcat-coaps-sim-test/run.sh "hello;commission;thread start"

Arguments:

    1st   commissioner commands, separated by ';' (default: "hello"). A command
          that starts with "@dev " is sent to the CLI of the TCAT Device
          instead, e.g. "hello;@dev tcat stop;hello".
    2nd   extra arguments for bbtc.py, as one string, replacing the default
          "--info"


Usage: interactive
------------------

    PANID=0xc64e CHANNEL=18 ./tools/tcat-coaps-sim-test/run.sh -i

The device and the host side are set up as in the scripted mode. Then the
commissioner runs in the terminal, already connected to the TCAT Device:
everything you type is input for its CLI, until you type "exit". After that the
device and host follow-up commands are run and everything is shut down. Ctrl-C
also ends the commissioner, without a clean disconnect from the device.

Any arguments after -i are passed to bbtc.py, for example "-i --debug". Without
those, the commissioner only prints its normal CLI output.

The CLI of the TCAT Device cannot be reached in this mode. Use DEV_POST to
query the device afterwards, or DEV_LOG to see its CLI output.


Environment variables
---------------------

    VENV        Python venv to use (default: the Poetry venv of
                tools/tcat_ble_client)
    PANID       PAN ID set on both the device and the host (default 0xface)
    CHANNEL     channel set on both the device and the host (default 11)
    BUILD_DIR   where the builds are (default <repo>/build/tcat-coaps)
    WORK_DIR    scratch directory (default /tmp/tcat-coaps-sim-test)
    DEV_BIN     alternative TCAT Device binary (default: sim-coaps ot-cli-ftd)
    DEV_POST    device CLI commands to run afterwards, ';'-separated
                (default "state")
    HOST_POST   ot-ctl commands to run afterwards, ';'-separated
                (default "state")
    DEV_LOG     if set, dump the full device CLI output at the end
    PCAP        if set, capture the packets on wpan0 into this file (pcapng),
                for Wireshark. See "Capturing packets" below.
    SSLKEYLOGFILE
                if set, the commissioner appends the secrets of its DTLS
                sessions to this file, for decryption in Wireshark.


Capturing packets
-----------------

wpan0 only exists inside the network namespace of the test, so Wireshark on
the desktop does not see it. Instead, let the test capture into a file, and
open that in Wireshark:

    PCAP=/tmp/wpan0.pcapng PANID=0xc64e CHANNEL=18 \
        ./tools/tcat-coaps-sim-test/run.sh "hello;commission"
    wireshark /tmp/wpan0.pcapng

This runs dumpcap on wpan0 inside the namespace, in both the scripted and the
interactive mode. The file is complete when the test has ended.

wpan0 is a TUN interface, so the capture holds the IPv6 packets between the
host and the TCAT Device: the DTLS handshake and the encrypted DTLS records,
and ICMPv6. It does not hold 802.15.4 frames or 6LoWPAN fragments.

The TCAT commands are inside the encrypted DTLS records. To let Wireshark
decrypt these, also set SSLKEYLOGFILE. The commissioner then writes the secrets
of each DTLS session that it sets up to this file:

    PCAP=/tmp/wpan0.pcapng SSLKEYLOGFILE=/tmp/tcat-keys.log \
    PANID=0xc64e CHANNEL=18 \
        ./tools/tcat-coaps-sim-test/run.sh "hello;commission"

Or, for an interactive session:

    PCAP=/tmp/wpan0.pcapng SSLKEYLOGFILE=/tmp/tcat-keys.log \
    PANID=0xc64e CHANNEL=18 \
        ./tools/tcat-coaps-sim-test/run.sh -i

When the test has ended, open the capture together with the key log file:

    wireshark -o tls.keylog_file:/tmp/tcat-keys.log /tmp/wpan0.pcapng

The key log file can also be set in the Wireshark preferences, under Protocols,
TLS, "(Pre)-Master-Secret log filename". This TLS setting is used for DTLS too.
Wireshark then shows the CoAP requests to the resource /c/tc and their
responses, with the TCAT TLVs as CoAP payload.

The file is appended to, never truncated: delete it yourself now and then. It
only holds secrets of the sessions of these test runs.


What run.sh does
----------------

run.sh is a thin wrapper. It creates an unprivileged sandbox in which ot-daemon
can create wpan0 without sudo, and then hands over to e2e.py.

1. It re-executes itself in new namespaces with "unshare -Urnm" (user, network
   and mount namespaces, with your user mapped to root). Inside, the script is
   "root" for networking purposes only, which is enough to create a TUN
   interface and to configure addresses and routes. Nothing leaks to the real
   network stack of the machine.

2. It mounts a tmpfs on /run, because ot-daemon puts its control socket at
   /run/openthread-wpan0.sock and the real /run is not writable for a normal
   user.

3. It brings up "lo" with multicast enabled and adds a 224.0.0.0/4 route to it.
   The simulated radio exchanges 802.15.4 frames between processes over UDP
   multicast, and a fresh network namespace has "lo" down and no multicast
   route.

4. It runs e2e.py with the Python from $VENV, passing the arguments through.


What e2e.py does
----------------

1. Starts the TCAT Device: the simulated ot-cli-ftd as node 1, then the CLI
   commands "panid", "channel", "ifconfig up" and "tcat start", and reads the
   device's link-local address.

2. Starts the host side: ot-daemon with the simulated ot-rcp as node 2, which
   creates wpan0. Using ot-ctl it sets the same channel and PAN ID, runs
   "ifconfig up", and runs "unsecureport add 5684" so that the link-layer
   unsecured DTLS traffic passes the host's IPv6 filter.

3. Runs the commissioner: "bbtc.py --coaps <device link-local> --interface
   wpan0".

   Scripted: it waits 6 seconds for the DTLS handshake, then types each of the
   given commands 1.5 seconds apart, followed by "exit". Commands that start
   with "@dev " go to the CLI of the TCAT Device instead. The commissioner
   output is printed when it has ended.

   Interactive: the commissioner gets the terminal, until it ends.

4. Prints the output of the device and host follow-up commands, and the tail
   of the daemon log.


Things to be aware of
---------------------

- In scripted mode, timing is done with fixed sleeps rather than by waiting for
  prompts, so a slow command can overlap with the next one.

- WORK_DIR is wiped and recreated on every run. It holds the device and daemon
  settings and daemon.log.

- The host interface must be on the same channel and PAN ID as the TCAT Device.
  For "commission" they must also match the dataset being set (channel 18,
  PAN ID 0xc64e for the commissioner's default dataset): setting the dataset
  moves the device's radio immediately, so otherwise the response to
  "commission" is lost and the link drops.

- The commissioner binds its local UDP port to the device's TCAT port (5684).
  That is why a single "unsecureport add 5684" on the host covers both
  directions.
