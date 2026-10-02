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

import logging
from os import path
from typing import Optional

from client.tcat_link import CloseReason, TcatLinkClosed, TcatLinkSecure
from client.transport import Transport
from tlv.tcat_tlv import TcatTLVType
from tlv.tlv import TLV
from utils import hexdump_ot

logger = logging.getLogger(__name__)


class TcatClient:
    """
    TCAT Commissioner client: owns the TCAT link (TLS over a Transport, e.g. BLE) with a TCAT Device.

    There is at most one link at a time. The link itself receives unsolicited events from the TCAT Device
    (reported via _handle_unsolicited_event()) and detects its own end (peer close, or transport loss).
    All ways of ending a link go through TcatLinkSecure.close(), which reports the end once via
    _on_link_closed().
    """

    def __init__(self, cert_path: str = 'auth'):
        self._cert_path = cert_path
        self._link: Optional[TcatLinkSecure] = None

    @property
    def link(self) -> Optional[TcatLinkSecure]:
        """The connected TCAT link, or None if not connected."""
        if self._link is not None and self._link.is_connected:
            return self._link
        return None

    @property
    def is_connected(self) -> bool:
        return self.link is not None

    async def connect(self, transport: Transport) -> bool:
        """
        Establishes a new TCAT link with a TCAT device over a connected transport, by performing the
        TLS handshake. Any previous link is closed first.

        Args:
            transport: The connected transport to the TCAT device. The client takes ownership of it:
                       it is disconnected when the link ends, or when the handshake fails.

        Returns:
            True if connection was successful, False otherwise.
        """
        await self.disconnect()

        link = TcatLinkSecure(transport)
        link.on_event = _handle_unsolicited_event
        link.on_closed = lambda reason: self._on_link_closed(link, reason)

        ok = False
        try:
            link.load_cert(
                certfile=path.join(self._cert_path, 'commissioner_cert.pem'),
                keyfile=path.join(self._cert_path, 'commissioner_key.pem'),
                cafile=path.join(self._cert_path, 'ca_cert.pem'),
            )
            logger.info(f"Certificates and key loaded from '{self._cert_path}'")

            print('Setting up secure channel...')
            is_debug = logger.getEffectiveLevel() <= logging.DEBUG
            ok = await link.do_handshake(progress_callback=None if is_debug else _handshake_progress_bar,
                                         timeout=transport.handshake_timeout)
        except Exception as e:
            logger.error(e)
        finally:
            if not ok:
                await link.close(CloseReason.HANDSHAKE_FAILED)

        if ok:
            self._link = link
            print('Done')
        return ok

    async def disconnect(self) -> None:
        """Closes the current link, if any, by local request."""
        link = self._link
        if link is None:
            return
        if link.is_connected:
            print('Disconnecting...')
            await link.close(CloseReason.LOCAL)
            print('Done')
        else:
            # Link already ended: tear down (if not done yet) without any over-the-link traffic.
            await link.close(CloseReason.LOCAL_ABORT)

    async def abort(self) -> None:
        """Closes the current link, if any, abruptly: no Disconnect TLV and no TLS close-notify."""
        if self._link is not None:
            await self._link.close(CloseReason.LOCAL_ABORT)

    async def send_with_resp(self, data: bytes) -> bytes:
        """
        Sends data (a command) to the TCAT Device and waits for the response.

        Returns:
            The response data, or empty b'' if no response was received within the timeout.

        Raises:
            TcatLinkClosed: If not connected, or if the link was found closed during the operation.
        """
        link = self.link
        if link is None:
            raise TcatLinkClosed('TCAT Device not connected')
        return await link.send_with_resp(data)

    def _on_link_closed(self, link: TcatLinkSecure, reason: CloseReason) -> None:
        # Called exactly once per link, by TcatLinkSecure.close().
        if self._link is link:
            self._link = None
        if reason == CloseReason.PEER_CLOSED:
            print('TCAT Device closed the connection.')
        elif reason == CloseReason.LINK_LOST:
            print('TCAT Device disconnected: the connection was closed unexpectedly.')
        elif reason == CloseReason.TLS_ERROR:
            print('TCAT Device disconnected: TLS error.')


def _handle_unsolicited_event(data: bytes) -> None:
    logger.info('Received event data from TCAT Device:\n' + hexdump_ot("Event", data))
    try:
        tlv = TLV.from_bytes(data)
    except Exception as e:
        logger.error(f'Error: Malformed unsolicited data from TCAT Device: {e}')
        return
    if tlv.type in [
            TcatTLVType.APPLICATION_DATA_1.value, TcatTLVType.APPLICATION_DATA_2.value,
            TcatTLVType.APPLICATION_DATA_3.value, TcatTLVType.APPLICATION_DATA_4.value
    ]:
        num = tlv.type - TcatTLVType.APPLICATION_DATA_1.value + 1
        logger.info(f"  - Send Application Data {num} {hex(tlv.type)}")
    elif tlv.type in [TcatTLVType.RESPONSE_EVENT.value]:
        logger.info(f"  - Response Event {hex(tlv.type)}")
    else:
        logger.error(f"Error: Illegal unsolicited TLV type sent by TCAT Device: {hex(tlv.type)}")


def _handshake_progress_bar(is_concluded: bool) -> None:
    if is_concluded:
        print('')
    else:
        print('.', end='', flush=True)
