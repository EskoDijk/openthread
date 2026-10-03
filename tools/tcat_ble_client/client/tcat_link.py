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

import _ssl
import asyncio
import logging
import ssl
from enum import Enum, auto
from typing import Optional, Callable

from cryptography.x509 import load_der_x509_certificate
from cryptography.hazmat.primitives.serialization import (Encoding, PublicFormat)

from client.transport import Transport, TransportClosed
from tlv.tlv import TLV
from tlv.tcat_tlv import TcatTLVType
import utils

logger = logging.getLogger(__name__)


class CloseReason(Enum):
    """Reason why a TCAT link (TLS session and underlying transport) was closed."""
    LOCAL_CLOSED = auto()  # closed by local request: graceful, with Disconnect TLV and TLS close-notify
    LOCAL_ABORT = auto()  # closed by local request: abrupt, no over-the-link traffic
    PEER_CLOSED = auto()  # peer closed the TLS session (close-notify received)
    LINK_LOST = auto()  # underlying transport (e.g. BLE link) was lost
    TLS_ERROR = auto()  # fatal TLS error, e.g. a fatal alert received from the peer
    HANDSHAKE_FAILED = auto()  # TLS handshake did not succeed

    @property
    def description(self) -> str:
        """Human-readable description of the close reason."""
        return _CLOSE_REASON_DESCRIPTIONS[self]


_CLOSE_REASON_DESCRIPTIONS = {
    CloseReason.LOCAL_CLOSED: 'closed by local request',
    CloseReason.LOCAL_ABORT: 'aborted by local request',
    CloseReason.PEER_CLOSED: 'closed by the TCAT Device',
    CloseReason.LINK_LOST: 'link lost or closed unexpectedly',
    CloseReason.TLS_ERROR: 'TLS error',
    CloseReason.HANDSHAKE_FAILED: 'TLS handshake failed',
}


class TcatLinkClosed(Exception):
    """
    The TCAT link was closed, or turned out to be closed, during an operation. When this is raised, the
    link has been closed and its closure was reported via TcatLinkSecure.on_closed.
    """
    pass


class TcatLinkSecure:
    """
    A TCAT link: a TLS session with a TCAT Device over an underlying transport (e.g. BLE).

    After a successful handshake, a single reader task is the only reader of the transport. It delivers
    each received TLS record either as the response to the pending request (see send_with_resp()), or
    otherwise as an unsolicited event (on_event). When it detects the end of the link (TLS close-notify,
    TLS error, or transport loss) it closes the link. All ways of ending the link go through close().
    """

    RECORD_BUFFER_SIZE = 4096

    def __init__(self, transport: Transport):
        self.transport = transport
        self.ssl_context = ssl.create_default_context(ssl.Purpose.SERVER_AUTH)
        self.incoming = ssl.MemoryBIO()
        self.outgoing = ssl.MemoryBIO()
        self.ssl_object = None
        self.cert = ''
        self.peer_challenge = None
        self._peer_public_key = None
        self._send_lock = asyncio.Lock()  # keeps encrypted output of concurrent senders in order
        self._request_lock = asyncio.Lock()  # at most one outstanding request
        self._pending_response: Optional[asyncio.Future] = None
        self._reader_task: Optional[asyncio.Task] = None
        self._reader_done = asyncio.Event()
        self._closing = False
        self._closed = asyncio.Event()
        self.close_reason: Optional[CloseReason] = None
        # Called with the data of each received unsolicited event.
        self.on_event: Optional[Callable[[bytes], None]] = None
        # Called exactly once, with the CloseReason, when the link is fully closed.
        self.on_closed: Optional[Callable[[CloseReason], None]] = None

    def load_cert(self, certfile='', keyfile='', cafile=''):
        if certfile and keyfile:
            self.ssl_context.load_cert_chain(certfile=certfile, keyfile=keyfile)
            self.cert = utils.load_cert_pem(certfile)
        elif certfile:
            self.ssl_context.load_cert_chain(certfile=certfile)
            self.cert = utils.load_cert_pem(certfile)

        if cafile:
            self.ssl_context.load_verify_locations(cafile=cafile)

    async def do_handshake(self,
                           timeout: float = 30.0,
                           progress_callback: Optional[Callable[[bool], None]] = None) -> bool:
        """
        Performs a TLS handshake with a TCAT Device, reporting progress via an optional callback.
        On success, the reader task is started.

        Args:
            timeout: The maximum time in seconds to wait for the handshake to complete.
            progress_callback: A function that accepts one boolean argument:
                               - False (is_concluded=False): The handshake attempt is ongoing.
                               - True (is_concluded=True): The handshake attempt has concluded. This call is made
                                 before any results/errors in do_handshake are logged.

        Returns:
            True if the TLS handshake was successful, False otherwise.
        """
        self.ssl_object = self.ssl_context.wrap_bio(
            incoming=self.incoming,
            outgoing=self.outgoing,
            server_side=False,
            server_hostname=None,
        )

        error = None
        try:
            async with asyncio.timeout(timeout):
                while True:
                    if progress_callback:
                        progress_callback(False)
                    try:
                        self.ssl_object.do_handshake()
                        break
                    except ssl.SSLWantReadError:
                        await self._flush()
                        self.incoming.write(await self.transport.recv())
                await self._flush()  # final handshake message(s), if any
        except TimeoutError:
            error = f'TLS Connection timed out (timeout={timeout}s).'
        except ssl.SSLCertVerificationError as err:
            error = f'SSLCertVerificationError reason={err.reason} verify_code={err.verify_code} ' \
                    f'verify_msg="{err.verify_message}"'
        except ssl.SSLError as err:
            error = f'SSLError reason={err.reason}'
        finally:
            if progress_callback:
                progress_callback(True)

        if error:
            logger.error(error)
            return False

        cert = self.ssl_object.getpeercert(True)
        cert_obj = load_der_x509_certificate(cert)
        self._peer_public_key = cert_obj.public_key().public_bytes(Encoding.DER, PublicFormat.SubjectPublicKeyInfo)
        self.log_cert_identities()
        self._reader_task = asyncio.create_task(self._reader())
        return True

    async def send_with_resp(self, data: bytes, timeout: float = 5.0) -> bytes:
        """
        Send data (a command) to the TCAT Device over the secure TLS connection and wait for response data.

        Args:
            data: The data to send.
            timeout: The maximum time in seconds to wait for the response data. Defaults to 5.0 seconds.

        Returns:
            bytes: The received response data, or empty b'' if no response was received within the timeout.

        Raises:
            TcatLinkClosed: If the link is closed, or gets closed while waiting for the response.
        """
        async with self._request_lock:
            if not self.is_connected:
                raise TcatLinkClosed('TCAT link is closed')
            response = asyncio.get_running_loop().create_future()
            self._pending_response = response
            try:
                await self._send(data)
                async with asyncio.timeout(timeout):
                    return await response
            except TimeoutError:
                logger.error(f'No response when response TLV/line expected (timeout={timeout}s).')
                return b''
            except TransportClosed as err:
                response.cancel()  # no longer awaited
                await self.close(CloseReason.LINK_LOST)  # no-op if the link is already closing
                raise TcatLinkClosed(str(err)) from err
            finally:
                self._pending_response = None

    async def close(self, reason: CloseReason = CloseReason.LOCAL_CLOSED, timeout: float = 5.0) -> None:
        """
        Closes the TCAT link: the TLS session and the underlying transport.

        This is the single teardown path for all cases (local request, peer close, link loss,
        handshake failure). It is idempotent: only the first call performs the teardown and
        determines the close reason; any later or concurrent calls just wait for it to complete.

        Args:
            reason: Why the link is closed. Only CloseReason.LOCAL performs a graceful close
                    (Disconnect TLV and TLS close-notify); the other reasons drop the transport directly.
            timeout: The maximum time in seconds for the graceful TLS close.
        """
        if self._closing:
            await self._closed.wait()
            return

        # Determine and record this before any await, so concurrent callers see the link as closing.
        graceful = reason == CloseReason.LOCAL_CLOSED and self.is_connected
        self._closing = True
        self.close_reason = reason
        logger.debug(f'Closing TCAT link: {reason.name} ({reason.description})')

        try:
            if graceful:
                try:
                    async with asyncio.timeout(timeout):
                        logger.debug('sending Disconnect command TLV')
                        await self._send(TLV(TcatTLVType.DISCONNECT.value, bytes()).to_bytes())
                        await self._send_close_notify()
                        await self._reader_done.wait()  # until the peer's close-notify, or link loss
                except TimeoutError:
                    logger.warning(f'TLS closing procedure timed out (timeout={timeout} s).')
                except Exception as err:
                    logger.warning(f'TLS closing procedure incomplete: {err}')
                    logger.debug(err, exc_info=True)
            elif reason == CloseReason.PEER_CLOSED:
                try:
                    await self._send_close_notify()  # reply to the peer's close-notify
                except Exception as err:
                    logger.debug(f'Could not reply with close-notify: {err}')

        finally:
            if self._pending_response is not None and not self._pending_response.done():
                self._pending_response.set_exception(TcatLinkClosed('TCAT link was closed'))
            await self._stop_reader()
            self._peer_public_key = None
            self.peer_challenge = None
            self.ssl_object = None
            try:
                await self.transport.disconnect()
            except asyncio.CancelledError:
                raise
            except Exception as err:
                logger.warning(f'Failed to disconnect TCAT transport: {err}')
                logger.debug(err, exc_info=True)
            finally:
                self._closed.set()
                if self.on_closed:
                    self.on_closed(reason)

    @property
    def is_connected(self) -> bool:
        """True if the TLS session is established and has not ended."""
        return not self._closing and self._reader_task is not None and not self._reader_done.is_set()

    async def _reader(self) -> None:
        """The single reader of the transport while the link is established; closes the link at its end."""
        try:
            reason = await self._read_records()
        finally:
            self._reader_done.set()
        await self.close(reason)  # no-op if the link is already closing

    async def _read_records(self) -> CloseReason:
        """Reads and delivers received TLS records, until the link ends. Returns the reason of the end."""
        try:
            while True:
                self.incoming.write(await self.transport.recv())
                while True:
                    try:
                        record = self.ssl_object.read(self.RECORD_BUFFER_SIZE)
                    except ssl.SSLWantReadError:
                        break  # need more data for a complete record
                    except ssl.SSLZeroReturnError:
                        record = b''
                    if not record:  # close-notify received from peer
                        if not self._closing:
                            logger.warning('TLS connection closed by peer.')
                        return CloseReason.PEER_CLOSED
                    self._deliver(record)
                await self._flush()  # in case reading produced TLS output, e.g. a key update

        except TransportClosed as err:
            logger.debug(f'Transport closed: {err}')
            return CloseReason.LINK_LOST
        except ssl.SSLError as err:
            if not self._closing:
                logger.error(f'TLS error: {err}')
            return CloseReason.TLS_ERROR
        except Exception as err:
            if not self._closing:
                logger.error(f'Transport error: {err}')
                logger.debug(err, exc_info=True)
            return CloseReason.LINK_LOST

    async def _stop_reader(self) -> None:
        task = self._reader_task
        if task is None or task is asyncio.current_task():
            return
        task.cancel()
        try:
            await task
        except asyncio.CancelledError:
            pass

    def _deliver(self, record: bytes) -> None:
        logger.debug(f"rx {len(record)} bytes\n{utils.hexdump_ot('Rx', record)}")
        if self._pending_response is not None and not self._pending_response.done():
            self._pending_response.set_result(record)
        elif self.on_event:
            self.on_event(record)
        else:
            logger.warning(f'Dropped unsolicited data ({len(record)} bytes)')

    # Precondition: caller must handle all exceptions raised
    async def _send(self, data: bytes) -> None:
        hexdump_str = utils.hexdump_ot("Tx", data) if len(data) > 0 else ''
        logger.debug(f"tx {len(data)} bytes\n{hexdump_str}")
        self.ssl_object.write(data)
        await self._flush()

    async def _send_close_notify(self) -> None:
        try:
            self.ssl_object.unwrap()
        except ssl.SSLWantReadError:
            pass  # close-notify sent; the peer's close-notify is to be received by the reader
        await self._flush()

    async def _flush(self) -> None:
        async with self._send_lock:
            while self.outgoing.pending > 0:
                await self.transport.send(self.outgoing.read(self.RECORD_BUFFER_SIZE))

    @property
    def peer_public_key(self):
        return self._peer_public_key

    @property
    def peer_challenge(self):
        return self._peer_challenge

    @peer_challenge.setter
    def peer_challenge(self, value):
        self._peer_challenge = value

    def log_cert_identities(self):
        # using the internal object of the ssl library is necessary to see the cert data in
        # case of handshake failure - see:
        # https://sethmlarson.dev/experimental-python-3.10-apis-and-trust-stores
        try:
            cc = self.ssl_object._sslobj.get_unverified_chain()
            if cc is None:
                logger.warning('No TCAT Device cert chain was received (yet).')
                return
            logger.info(f'TCAT Device cert chain: {len(cc)} certificates received.')
            for cert in cc:
                logger.info(f'  cert info:\n{cert.get_info()}')
                peer_cert_der_hex = utils.base64_string(cert.public_bytes(_ssl.ENCODING_DER))
                logger.info(f'  base64: (paste in https://lapo.it/asn1js/ to decode)\n{peer_cert_der_hex}')
            logger.info(f'TCAT Commissioner cert, PEM:\n{self.cert}')

        except asyncio.CancelledError:
            raise
        except Exception as e:
            logger.warning(f'Could not display TCAT cert info: {e}')
