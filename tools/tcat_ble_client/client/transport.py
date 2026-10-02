"""
  Copyright (c) 2026, The OpenThread Authors.
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

from typing import Protocol


class TransportClosed(Exception):
    """The transport underlying a TCAT link was closed, or was found to be closed."""
    pass


class Transport(Protocol):
    """
    A connected, reliable and ordered byte transport underlying a TCAT link: for example BLE,
    or the BLE simulation over UDP.
    """

    @property
    def is_connected(self) -> bool:
        ...

    @property
    def handshake_timeout(self) -> float:
        """Timeout in seconds for the TLS handshake over this transport."""
        ...

    async def send(self, data: bytes) -> None:
        """Sends data. Raises TransportClosed if the transport is closed."""
        ...

    async def recv(self) -> bytes:
        """
        Waits until data is received, then returns all data received so far.
        Raises TransportClosed when the transport is closed and all received data was returned.
        """
        ...

    async def disconnect(self) -> None:
        """Closes the transport. Calling it again, or on an already closed transport, is allowed."""
        ...
