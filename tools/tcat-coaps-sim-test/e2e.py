#!/usr/bin/env python3
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
"""
End-to-end test of TCAT over CoAPS, in simulation or with real hardware. To be started by run.sh, which sets up the
namespaces that this needs. See README.txt.
"""

import atexit
import os
import shutil
import subprocess
import sys
import termios
import threading
import time
import tty

REPO_DIR = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
COMMISSIONER_DIR = REPO_DIR + '/tools/tcat_ble_client'
BUILD_DIR = os.environ.get('BUILD_DIR', REPO_DIR + '/build/tcat-coaps')
WORK_DIR = os.environ.get('WORK_DIR', '/tmp/tcat-coaps-sim-test')
VENV = os.environ['VENV']
PANID = os.environ.get('PANID', '0xface')
CHANNEL = os.environ.get('CHANNEL', '11')
PCAP = os.environ.get('PCAP')

# Hardware mode: the serial ports of a real RCP and of the CLI of a real TCAT Device
RCP_PORT = os.environ.get('RCP_PORT')
DEVICE_PORT = os.environ.get('DEV_PORT')

DEVICE_BIN = os.environ.get('DEV_BIN', BUILD_DIR + '/sim-coaps/examples/apps/cli/ot-cli-ftd')
RCP_BIN = BUILD_DIR + '/sim-coaps/examples/apps/ncp/ot-rcp'
DAEMON_BIN = BUILD_DIR + '/posix/src/posix/ot-daemon'
CTL_BIN = BUILD_DIR + '/posix/src/posix/ot-ctl'
DAEMON_SOCKET = '/run/openthread-wpan0.sock'

TCAT_PORT = 5684
DEVICE_NODE_ID = 1
RCP_NODE_ID = 2
HANDSHAKE_WAIT_SEC = 6
COMMAND_INTERVAL_SEC = 1.5
DEVICE_COMMAND_PREFIX = '@dev '


class DeviceCli:
    """The CLI of the TCAT Device. A subclass provides the transport to the device."""

    PROMPT = '> '

    def __init__(self):
        self.lines = []
        threading.Thread(target=self._read_output, daemon=True).start()

    def _read_output(self):
        raise NotImplementedError

    def _add_line(self, line):
        line = line.rstrip('\r\n')
        while line.startswith(self.PROMPT):
            line = line[len(self.PROMPT):]
        if line:
            self.lines.append(line)

    def _write(self, text):
        raise NotImplementedError

    def close(self):
        raise NotImplementedError

    def cmd(self, command, timeout=5):
        start = len(self.lines)
        self._write(command + '\n')
        end_time = time.time() + timeout
        while time.time() < end_time:
            if any(line.strip() == 'Done' or line.startswith('Error') for line in self.lines[start:]):
                break
            time.sleep(0.05)
        # Leave out the echo of the command.
        output = [line for line in self.lines[start:] if line != command]
        print(f'[device] {command} -> {output}')
        return output


class SimulatedDeviceCli(DeviceCli):
    """The CLI of a simulated TCAT Device: a process."""

    def __init__(self, args, cwd):
        # The device gets its own session, so that Ctrl-C in the terminal does not reach it.
        self.process = subprocess.Popen(args,
                                        cwd=cwd,
                                        stdin=subprocess.PIPE,
                                        stdout=subprocess.PIPE,
                                        stderr=subprocess.STDOUT,
                                        text=True,
                                        bufsize=1,
                                        start_new_session=True)
        atexit.register(self.process.kill)
        super().__init__()

    def _read_output(self):
        for line in self.process.stdout:
            self._add_line(line)

    def _write(self, text):
        self.process.stdin.write(text)
        self.process.stdin.flush()

    def close(self):
        self.process.kill()


class SerialDeviceCli(DeviceCli):
    """The CLI of a real TCAT Device, on a serial port."""

    def __init__(self, port):
        self.fd = os.open(port, os.O_RDWR | os.O_NOCTTY)
        tty.setraw(self.fd)
        attributes = termios.tcgetattr(self.fd)
        attributes[4] = attributes[5] = termios.B115200
        termios.tcsetattr(self.fd, termios.TCSANOW, attributes)
        super().__init__()

        # End a partial line that the device may still have in its input buffer.
        self._write('\n')
        time.sleep(0.3)

    def _read_output(self):
        pending = ''
        while True:
            try:
                data = os.read(self.fd, 4096)
            except OSError:
                break
            if not data:
                break
            pending += data.decode(errors='replace')
            *lines, pending = pending.split('\n')
            for line in lines:
                self._add_line(line)

    def _write(self, text):
        os.write(self.fd, text.replace('\n', '\r\n').encode())

    def close(self):
        os.close(self.fd)


def host_cmd(command):
    """Runs an ot-ctl command on the host side, i.e. on the ot-daemon that provides wpan0."""
    result = subprocess.run([CTL_BIN] + command.split(), capture_output=True, text=True, timeout=10)
    print(f'[host] {command} -> {result.stdout.splitlines()} {result.stderr.strip()}')


def set_device_parameter(device, name, value):
    """Sets the PAN ID or the channel of the TCAT Device, unless the device already uses the value."""
    # Setting these erases the Active Operational Dataset of the device. This check keeps the dataset of a real
    # device that was commissioned in an earlier run, with the same PAN ID and channel.
    for line in device.cmd(name):
        try:
            if int(line, 0) == int(value, 0):
                return
        except ValueError:
            pass
    device.cmd(f'{name} {value}')


def start_device():
    if DEVICE_PORT:
        device = SerialDeviceCli(DEVICE_PORT)
        # Unlike a simulated device, a real one still has the state of its previous use.
        for command in ['tcat stop', 'thread stop']:
            device.cmd(command)
    else:
        device = SimulatedDeviceCli([DEVICE_BIN, str(DEVICE_NODE_ID)], WORK_DIR + '/dev')
    set_device_parameter(device, 'panid', PANID)
    set_device_parameter(device, 'channel', CHANNEL)
    for command in ['ifconfig up', 'tcat start']:
        device.cmd(command)
    return device


def start_host():
    daemon_log = open(WORK_DIR + '/daemon.log', 'w')
    daemon_cmd = [DAEMON_BIN, '-v', '--data-path', WORK_DIR + '/daemon']
    if RCP_PORT:
        daemon_cmd += [f'spinel+hdlc+uart://{RCP_PORT}']
    else:
        daemon_cmd += [f'spinel+hdlc+forkpty://{RCP_BIN}?forkpty-arg={RCP_NODE_ID}']
    daemon = subprocess.Popen(daemon_cmd,
                              cwd=WORK_DIR + '/daemon',
                              stdout=daemon_log,
                              stderr=subprocess.STDOUT,
                              start_new_session=True)
    atexit.register(daemon.terminate)
    for _ in range(50):
        if os.path.exists(DAEMON_SOCKET):
            break
        time.sleep(0.1)
    else:
        sys.exit(f'ot-daemon did not start: see {WORK_DIR}/daemon.log')

    # The host uses the channel and PAN ID of the TCAT Device, and allows link-layer unsecured communication on the
    # UDP port of TCAT, while Thread itself remains disabled.
    for command in [f'channel {CHANNEL}', f'panid {PANID}', 'ifconfig up', f'unsecureport add {TCAT_PORT}']:
        host_cmd(command)
    return daemon


def start_capture():
    """Captures the IPv6 packets on wpan0. This must run here, inside the network namespace that has wpan0."""
    capture_cmd = ['dumpcap', '-q', '-i', 'wpan0', '-w', os.path.abspath(PCAP)]
    capture = subprocess.Popen(capture_cmd,
                               stdout=subprocess.DEVNULL,
                               stderr=subprocess.DEVNULL,
                               start_new_session=True)
    atexit.register(capture.terminate)
    return capture


def run_commissioner_interactive(commissioner_cmd):
    # The commissioner uses the terminal directly: everything typed goes to its CLI, until 'exit'.
    print('---- commissioner (interactive, type \'exit\' to end) ----', flush=True)
    commissioner = subprocess.Popen(commissioner_cmd, cwd=COMMISSIONER_DIR)
    while True:
        try:
            commissioner.wait()
            break
        except KeyboardInterrupt:
            # The commissioner does not end by itself on Ctrl-C, while it waits for a line of input.
            commissioner.terminate()
    print(f'---- commissioner ended (rc={commissioner.returncode}) ----')


def run_commissioner_scripted(commissioner_cmd, commands, device):
    commissioner = subprocess.Popen(commissioner_cmd,
                                    cwd=COMMISSIONER_DIR,
                                    stdin=subprocess.PIPE,
                                    stdout=subprocess.PIPE,
                                    stderr=subprocess.STDOUT,
                                    text=True,
                                    bufsize=1)
    output = []

    def read_output():
        for line in commissioner.stdout:
            output.append(line.rstrip('\n'))

    threading.Thread(target=read_output, daemon=True).start()
    time.sleep(HANDSHAKE_WAIT_SEC)
    try:
        for command in commands:
            if command.startswith(DEVICE_COMMAND_PREFIX):
                # A command for the CLI of the TCAT Device, in between the commissioner commands
                device.cmd(command[len(DEVICE_COMMAND_PREFIX):])
            else:
                commissioner.stdin.write(command + '\n')
                commissioner.stdin.flush()
            time.sleep(COMMAND_INTERVAL_SEC)
        commissioner.stdin.write('exit\n')
        commissioner.stdin.flush()
    except BrokenPipeError:
        print('commissioner exited early')
    try:
        commissioner.wait(timeout=15)
    except subprocess.TimeoutExpired:
        commissioner.kill()
    time.sleep(1)
    print(f'---- commissioner output (rc={commissioner.returncode}) ----')
    print('\n'.join(output))


def main():
    interactive = len(sys.argv) > 1 and sys.argv[1] in ('-i', '--interactive')
    if interactive:
        commands = []
        commissioner_args = sys.argv[2:]
    else:
        commands = sys.argv[1].split(';') if len(sys.argv) > 1 else ['hello']
        commissioner_args = sys.argv[2].split() if len(sys.argv) > 2 else ['--info']

    if bool(RCP_PORT) != bool(DEVICE_PORT):
        sys.exit('Set both RCP_PORT and DEV_PORT to use real hardware, or neither to simulate.')

    shutil.rmtree(WORK_DIR, ignore_errors=True)
    os.makedirs(WORK_DIR + '/dev')
    os.makedirs(WORK_DIR + '/daemon')

    device = start_device()
    device_address = [line for line in device.cmd('ipaddr linklocal') if line.startswith('fe80')][0].strip()
    daemon = start_host()
    capture = start_capture() if PCAP else None
    time.sleep(1)
    print(subprocess.run(['ip', '-6', 'addr', 'show', 'dev', 'wpan0'], capture_output=True, text=True).stdout)

    # The commissioner runs in another directory: let it find a key log file with a relative name here.
    if os.environ.get('SSLKEYLOGFILE'):
        os.environ['SSLKEYLOGFILE'] = os.path.abspath(os.environ['SSLKEYLOGFILE'])

    commissioner_cmd = [VENV + '/bin/python', 'bbtc.py', '--coaps', device_address, '--interface', 'wpan0']
    commissioner_cmd += commissioner_args
    if interactive:
        run_commissioner_interactive(commissioner_cmd)
    else:
        run_commissioner_scripted(commissioner_cmd, commands, device)

    print('---- device ----')
    for command in os.environ.get('DEV_POST', 'state').split(';'):
        device.cmd(command)
    for command in os.environ.get('HOST_POST', 'state').split(';'):
        host_cmd(command)
    if os.environ.get('DEV_LOG'):
        print('---- device log ----')
        print('\n'.join(device.lines))
    if capture is not None:
        time.sleep(0.5)
        capture.terminate()
        capture.wait()
        print(f'---- packets captured in {PCAP} ----')

    device.close()
    daemon.terminate()
    time.sleep(0.5)
    print('---- daemon log tail ----')
    with open(WORK_DIR + '/daemon.log') as daemon_log:
        print(''.join(daemon_log.readlines()[-25:]))


if __name__ == '__main__':
    main()
