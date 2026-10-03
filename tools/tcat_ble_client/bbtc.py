"""
  Copyright (c) 2024-2026, The OpenThread Authors.
  All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:
  1. Redistributions of source code must retain the above copyright
     notice, this list of conditions and the following disclaimer.
  2. Redistributions in binary form must reproduce the above copyright
     notice, this list of conditions and the following disclaimer in the
     documentation and/or other materials provided with the distribution.
  3. Neither the name of the copyright holder nor the
     names of its contributors may be used to endorse or promote products
     derived from this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
  POSSIBILITY OF SUCH DAMAGE.
"""

import asyncio
import argparse
import logging

from bleak import BLEDevice

from ble import ble_scanner
from cli.base_commands import connect_ble_device, connect_simulation
from cli.cli import CLI
from client.tcat_client import TcatClient
from client.tcat_link import TcatLinkClosed
from dataset.dataset import ThreadDataset
from cli.command import CommandResult
from utils import select_device_by_user_input, quit_with_reason

logger = logging.getLogger(__name__)
logged_modules = ['ble', 'cli', 'client', 'dataset', 'tlv', 'utils']


async def main():
    log_level = logging.WARNING
    logging.basicConfig(level=log_level)

    parser = argparse.ArgumentParser(description='Device parameters')
    parser.add_argument('-a', '--adapter', help='Select HCI adapter')
    parser.add_argument('--debug', help='Enable debug logs', action='store_true')
    parser.add_argument('--info', help='Enable info logs', action='store_true')
    parser.add_argument('--cert_path', help='Path to certificate chain and key', action='store', default='auth')
    group = parser.add_mutually_exclusive_group()
    group.add_argument('--mac', type=str, help='Device MAC address', action='store')
    group.add_argument('--name', type=str, help='Device name', action='store')
    group.add_argument('--scan', help='Scan all available devices', action='store_true')
    group.add_argument('--simulation', help='Connect to simulation node id', action='store')
    args = parser.parse_args()

    if args.debug:
        log_level = logging.DEBUG
    elif args.info:
        log_level = logging.INFO
    logger.setLevel(log_level)
    for module in logged_modules:
        logging.getLogger(module).setLevel(log_level)

    # create client and CLI, and (if selected) connect to TCAT device
    client = TcatClient(cert_path=args.cert_path)
    cli = CLI(ThreadDataset(), client)
    ok = True
    if args.simulation:
        ok = await connect_simulation(client, int(args.simulation))
    else:
        device = await get_ble_device_by_args(args)
        if device is not None:
            ok = await connect_ble_device(client, device)
    if not ok:
        quit_with_reason('Failed to connect to TCAT device: TLS handshake failed.')

    # run the CLI, until 'exit', Ctrl-D (EOF) or Ctrl-C
    print('Enter \'help\' to see available commands or \'exit\' to exit the application.')
    loop = asyncio.get_running_loop()
    try:
        while True:
            try:
                user_input = await loop.run_in_executor(None, lambda: input('> '))
            except EOFError:
                print()
                break
            if user_input.lower() == 'exit':
                break
            try:
                result: CommandResult = await cli.evaluate_input(user_input)
                result.pretty_print()
            except TcatLinkClosed as e:
                logger.debug(f'Command ended: {e}')  # link closure already reported by the client
            except Exception as e:
                logger.error(e)
                logger.debug(e, exc_info=True)

    except asyncio.CancelledError:
        # Ctrl-C: asyncio.run() cancels this main task. Handle it as a normal exit.
        asyncio.current_task().uncancel()
        print()

    finally:
        # Disconnect from TCAT device (if needed)
        await client.disconnect()


async def get_ble_device_by_args(args) -> BLEDevice | None:
    device = None
    if args.mac:
        device = await ble_scanner.find_first_by_mac(args.mac)
    elif args.name:
        device = await ble_scanner.find_first_by_name(args.name)
    elif args.scan:
        tcat_devices = await ble_scanner.scan_tcat_devices(adapter=args.adapter)
        device = select_device_by_user_input(tcat_devices)

    return device


if __name__ == '__main__':
    asyncio.run(main())
