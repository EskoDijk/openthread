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
import logging
import socket

from client.transport import TransportClosed

logger = logging.getLogger(__name__)


class UdpStream:
    """Simulated BLE transport (a client.transport.Transport) to a simulated TCAT device, over UDP."""
    BASE_PORT = 10000
    MAX_DATAGRAM_SIZE = 65535

    handshake_timeout = 5.0

    def __init__(self, address, node_id):
        self.__connected = True
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.socket.setblocking(False)
        self.address = (address, self.BASE_PORT + node_id)

    def __str__(self):
        return f"UdpStream[{self.address[0]}:{self.address[1]}]"

    async def send(self, data: bytes) -> None:
        logger.debug(f'tx {len(data)} bytes')
        if not self.__connected:
            raise TransportClosed('BLE connection (simulation) was closed')
        self.socket.sendto(data, self.address)

    async def recv(self) -> bytes:
        """Waits until a datagram is received and returns it. Raises TransportClosed when the link is closed."""
        if not self.__connected:
            raise TransportClosed('BLE connection (simulation) was closed')
        data = await asyncio.get_running_loop().sock_recv(self.socket, self.MAX_DATAGRAM_SIZE)
        # A received 0-byte datagram simulates the peer dropping the BLE link
        if len(data) == 0:
            logger.debug('rx: BLE link disconnection was simulated (0-byte UDP packet)')
            self.__connected = False
            raise TransportClosed('BLE connection (simulation) was closed')
        logger.debug(f'rx {len(data)} bytes')
        return data

    async def simulation_ble_disconnect(self):
        # Simulate a BLE link break (e.g. peer out of range) by sending a zero-length UDP
        # datagram. Unlike `disconnect`, this does not send a Disconnect TLV and does not
        # perform a clean TLS shutdown.
        self.socket.sendto(b'', self.address)

    async def disconnect(self) -> None:
        self.__connected = False
        self.socket.close()

    @property
    def is_connected(self) -> bool:
        return self.__connected
