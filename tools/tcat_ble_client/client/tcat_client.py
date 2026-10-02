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

from ble.ble_stream_secure import BleStreamSecure, CloseReason
from client.transport import Transport, TransportClosed
from tlv.tcat_tlv import TcatTLVType
from tlv.tlv import TLV
from utils import hexdump_ot

logger = logging.getLogger(__name__)


class TcatLinkClosed(Exception):
    """
    The link to the TCAT Device turned out to be closed during an operation, independent of the link type
    (BLE, or UDP for simulation). The client has already closed the session and reported this to the user.
    """
    pass


class TcatClient:
    """
    TCAT Commissioner client: owns the session (TLS over a Transport, e.g. BLE) with a TCAT Device.

    There is at most one session at a time. The session itself receives unsolicited events from the TCAT
    Device (reported via _handle_unsolicited_event()) and detects the end of the session (peer close, or BLE
    link loss). All ways of ending a session go through BleStreamSecure.close(), which reports the end once
    via _on_session_closed().
    """

    def __init__(self, cert_path: str = 'auth'):
        self._cert_path = cert_path
        self._session: Optional[BleStreamSecure] = None

    @property
    def session(self) -> Optional[BleStreamSecure]:
        """The connected session, or None if not connected."""
        if self._session is not None and self._session.is_connected:
            return self._session
        return None

    @property
    def is_connected(self) -> bool:
        return self.session is not None

    async def connect(self, transport: Transport) -> bool:
        """
        Establishes a new session with a TCAT device over a connected transport, by performing the
        TLS handshake. Any previous session is closed first.

        Args:
            transport: The connected transport to the TCAT device. The client takes ownership of it:
                       it is disconnected when the session ends, or when the handshake fails.

        Returns:
            True if connection was successful, False otherwise.
        """
        await self.disconnect()

        session = BleStreamSecure(transport)
        session.on_event = _handle_unsolicited_event
        session.on_closed = lambda reason: self._on_session_closed(session, reason)

        ok = False
        try:
            session.load_cert(
                certfile=path.join(self._cert_path, 'commissioner_cert.pem'),
                keyfile=path.join(self._cert_path, 'commissioner_key.pem'),
                cafile=path.join(self._cert_path, 'ca_cert.pem'),
            )
            logger.info(f"Certificates and key loaded from '{self._cert_path}'")

            print('Setting up secure channel...')
            is_debug = logger.getEffectiveLevel() <= logging.DEBUG
            ok = await session.do_handshake(progress_callback=None if is_debug else _handshake_progress_bar,
                                            timeout=transport.handshake_timeout)
        except Exception as e:
            logger.error(e)
        finally:
            if not ok:
                await session.close(CloseReason.HANDSHAKE_FAILED)

        if ok:
            self._session = session
            print('Done')
        return ok

    async def disconnect(self) -> None:
        """Closes the current session, if any, by local request."""
        session = self._session
        if session is None:
            return
        if session.is_connected:
            print('Disconnecting...')
            await session.close(CloseReason.LOCAL)
            print('Done')
        else:
            # Session already ended: tear down (if not done yet) without any over-the-link traffic.
            await session.close(CloseReason.LOCAL_ABORT)

    async def abort(self) -> None:
        """Closes the current session, if any, abruptly: no Disconnect TLV and no TLS close-notify."""
        if self._session is not None:
            await self._session.close(CloseReason.LOCAL_ABORT)

    async def send_with_resp(self, data: bytes) -> bytes:
        """
        Sends data (a command) to the TCAT Device and waits for the response.

        Returns:
            The response data, or empty b'' if no response was received within the timeout.

        Raises:
            TcatLinkClosed: If not connected, or if the link was found closed during the operation.
        """
        session = self.session
        if session is None:
            raise TcatLinkClosed('TCAT Device not connected')
        try:
            return await session.send_with_resp(data)
        except TransportClosed as e:
            raise TcatLinkClosed(str(e)) from e  # the session has closed itself, and was reported

    def _on_session_closed(self, session: BleStreamSecure, reason: CloseReason) -> None:
        # Called exactly once per session, by BleStreamSecure.close().
        if self._session is session:
            self._session = None
        if reason == CloseReason.PEER_CLOSED:
            print('TCAT Device closed the connection.')
        elif reason == CloseReason.LINK_LOST:
            print('TCAT Device disconnected: the BLE connection was closed unexpectedly.')
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
