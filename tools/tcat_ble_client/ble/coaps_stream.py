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

import asyncio
import logging
import os
import socket
from typing import Optional, Callable

from aiocoap import Message
from aiocoap.numbers.codes import Code
from aiocoap.numbers.types import Type
from cryptography.hazmat.primitives.serialization import Encoding, PublicFormat
from OpenSSL import SSL

from constants import (
    DTLS_HANDSHAKE_TIMEOUT_SEC,
    DTLS_MAX_DATAGRAM_SIZE,
    TCAT_RESPONSE_TIMEOUT_SEC,
    TLS_BUFFER_SIZE,
    TLS_CLOSE_TIMEOUT_SEC,
)
from tlv.tlv import TLV
from tlv.tcat_tlv import TcatTLVType
import utils

logger = logging.getLogger(__name__)

TCAT_URI_PATH = ('c', 'tc')  # the CoAP resource of a TCAT Device to which TCAT commands are posted
TCAT_CIPHER_SUITE = b'ECDHE-ECDSA-AES128-GCM-SHA256'  # the same cipher suite as used for TCAT over BLE (TLS)

# OpenSSL constants that pyOpenSSL does not define
DTLS1_2_VERSION = 0xFEFD
SSL_MODE_NO_AUTO_CHAIN = 0x8

DTLS_RECORD_HEADER_SIZE = 13  # DTLS 1.2 record header: type, version, epoch, sequence number, length
DTLS_MIN_TIMER_SEC = 0.001


def _split_into_datagrams(records: bytes, max_size: int) -> list[bytes]:
    """Splits a sequence of DTLS 1.2 records into datagrams of at most `max_size` bytes, if possible.

    OpenSSL writes its output to a memory buffer, in which the boundaries between the datagrams that it
    intended to send are lost. A record is never split up: a datagram can only exceed `max_size` if it
    consists of a single record that is larger.
    """
    datagrams = []
    datagram = b''
    offset = 0
    while offset < len(records):
        header_end = offset + DTLS_RECORD_HEADER_SIZE
        if header_end > len(records):
            raise RuntimeError('incomplete DTLS record header')
        record_end = header_end + int.from_bytes(records[header_end - 2:header_end], 'big')
        if record_end > len(records):
            raise RuntimeError('incomplete DTLS record')
        if datagram and len(datagram) + record_end - offset > max_size:
            datagrams.append(datagram)
            datagram = b''
        datagram += records[offset:record_end]
        offset = record_end
    if datagram:
        datagrams.append(datagram)
    return datagrams


class _UdpProtocol(asyncio.DatagramProtocol):
    """Queues the UDP datagrams received from the TCAT Device."""

    def __init__(self):
        self.datagrams = asyncio.Queue()

    def datagram_received(self, data, addr):
        self.datagrams.put_nowait(data)

    def error_received(self, exc):
        logger.debug(f'UDP error: {exc}')


class CoapsStream:
    """TCAT over CoAPS (CoAP over DTLS over UDP) connection to a TCAT Device.

    It provides the same interface as the combination of `BleStreamSecure` and its underlying link, so that it
    can be used as both the secure stream and the link by the CLI.

    TCAT command TLVs are sent as the payload of a confirmable CoAP POST request to the TCAT resource of the
    TCAT Device. The TCAT response TLVs are received in the payload of the piggybacked 2.04 (Changed) response.
    """

    def __init__(self, host: str, port: int, interface: str = ''):
        self.address = (host, port)
        self.cert = ''
        self._interface = interface
        self._family = socket.AF_INET6 if ':' in host else socket.AF_INET
        self._ssl_context = SSL.Context(SSL.DTLS_METHOD)
        self._ssl_context.set_min_proto_version(DTLS1_2_VERSION)
        self._ssl_context.set_max_proto_version(DTLS1_2_VERSION)
        self._ssl_context.set_cipher_list(TCAT_CIPHER_SUITE)
        # Only send the TCAT Commissioner certificate and not also the CA certificate, which the TCAT Device has
        # already: this keeps the DTLS handshake message small enough for the TCAT Device.
        self._ssl_context.set_mode(SSL_MODE_NO_AUTO_CHAIN)
        # The size of the DTLS datagrams is set explicitly: it cannot be discovered from a memory buffer.
        self._ssl_context.set_options(SSL.OP_NO_QUERY_MTU)
        self._ssl_context.set_verify(SSL.VERIFY_PEER)
        # Like the Python `ssl` module does for TLS: log the secrets of the DTLS session to the file that is named
        # by the SSLKEYLOGFILE environment variable, if set. With this file, a tool like Wireshark can decrypt a
        # packet capture of the session.
        self._keylog_file = os.environ.get('SSLKEYLOGFILE')
        if self._keylog_file:
            self._ssl_context.set_keylog_callback(self._log_key)
        self._conn = None
        self._transport = None
        self._protocol = None
        self._peer_public_key = None
        self._peer_challenge = None
        self._close_notify_sent = False
        self._close_notify_received = False
        self._message_id = int.from_bytes(os.urandom(2), 'big')
        self._lock = asyncio.Lock()

    def __str__(self):
        host = f'{self.address[0]}%{self._interface}' if self._interface else self.address[0]
        return f'CoAPS [{host}]:{self.address[1]}'

    def _log_key(self, conn, line: bytes) -> None:
        with open(self._keylog_file, 'ab') as keylog:
            keylog.write(line + b'\n')

    def load_cert(self, certfile: str = '', keyfile: str = '', cafile: str = ''):
        if certfile:
            self._ssl_context.use_certificate_chain_file(certfile)
            self.cert = utils.load_cert_pem(certfile)
        if keyfile:
            self._ssl_context.use_privatekey_file(keyfile)
            self._ssl_context.check_privatekey()
        if cafile:
            self._ssl_context.load_verify_locations(cafile)

    async def do_handshake(self,
                           buffersize: int = TLS_BUFFER_SIZE,
                           timeout: float = DTLS_HANDSHAKE_TIMEOUT_SEC,
                           progress_callback: Optional[Callable[[bool], None]] = None) -> bool:
        """
        Performs a DTLS handshake with a TCAT Device, reporting progress via an optional callback.

        Args:
            timeout: The maximum time in seconds to wait for the handshake to complete.
            progress_callback: A function that accepts one boolean argument:
                               - False (is_concluded=False): The handshake attempt is ongoing.
                               - True (is_concluded=True): The handshake attempt has concluded. This call is made
                                 before any results/errors in do_handshake are logged.

        Returns:
            True if the DTLS handshake was successful, False otherwise.
        """
        self._close_notify_sent = False
        self._close_notify_received = False
        self._peer_public_key = None
        if progress_callback:
            progress_callback(False)

        try:
            await self._open_udp()
            self._conn = SSL.Connection(self._ssl_context, None)
            self._conn.set_ciphertext_mtu(DTLS_MAX_DATAGRAM_SIZE)
            self._conn.set_connect_state()
            await self._handshake(timeout, progress_callback)

            peer_cert = self._conn.get_peer_certificate(as_cryptography=True)
            self._peer_public_key = peer_cert.public_key().public_bytes(Encoding.DER,
                                                                        PublicFormat.SubjectPublicKeyInfo)

        except Exception as err:
            if progress_callback:
                progress_callback(True)
            logger.error(f'DTLS handshake failed: {err!r}')
            logger.debug(err, exc_info=True)
            await self.disconnect()
            return False

        if progress_callback:
            progress_callback(True)
        logger.debug(f'DTLS cipher suite: {self._conn.get_cipher_name()}')
        self.log_cert_identities()
        return True

    async def _open_udp(self) -> None:
        udp_sock = socket.socket(self._family, socket.SOCK_DGRAM)
        try:
            # Use the port of the TCAT Device also as local port: the Thread interface of the host then only needs
            # to allow this single UDP port for its link-layer unsecured communication with the TCAT Device.
            udp_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            udp_sock.bind(('', self.address[1]))
            if self._family == socket.AF_INET6 and self._interface:
                udp_sock.connect((self.address[0], self.address[1], 0, socket.if_nametoindex(self._interface)))
            else:
                udp_sock.connect(self.address)
            udp_sock.setblocking(False)
            loop = asyncio.get_running_loop()
            self._transport, self._protocol = await loop.create_datagram_endpoint(_UdpProtocol, sock=udp_sock)
        except Exception:
            udp_sock.close()
            raise

    # Precondition: caller must handle all exceptions raised
    async def _handshake(self, timeout: float, progress_callback: Optional[Callable[[bool], None]]) -> None:
        loop = asyncio.get_running_loop()
        end_time = loop.time() + timeout
        while True:
            try:
                self._conn.do_handshake()
                self._flush()
                return
            except SSL.WantReadError:
                self._flush()

            if progress_callback:
                progress_callback(False)
            remaining = end_time - loop.time()
            if remaining <= 0:
                raise TimeoutError(f'no response from the TCAT Device (timeout={timeout}s)')
            # DTLS retransmits its last flight of handshake messages if no response to it is received in time.
            retransmit_time = self._conn.DTLSv1_get_timeout()
            if retransmit_time is not None:
                remaining = min(remaining, retransmit_time)
            if not await self._receive_datagram(remaining):
                self._conn.DTLSv1_handle_timeout()
                self._flush()

    def _flush(self) -> None:
        """Sends the DTLS records that OpenSSL produced to the TCAT Device."""
        records = b''
        while True:
            try:
                records += self._conn.bio_read(TLS_BUFFER_SIZE)
            except SSL.WantReadError:
                break
        for datagram in _split_into_datagrams(records, DTLS_MAX_DATAGRAM_SIZE):
            self._transport.sendto(datagram)

    async def _receive_datagram(self, timeout: float) -> bool:
        """Waits for a datagram from the TCAT Device and passes it to OpenSSL. Returns False on a timeout."""
        try:
            datagram = await asyncio.wait_for(self._protocol.datagrams.get(), max(timeout, DTLS_MIN_TIMER_SEC))
        except asyncio.TimeoutError:
            return False
        self._conn.bio_write(datagram)
        return True

    def _read_application_data(self) -> Optional[bytes]:
        """Reads the application data of one DTLS record that was received, or returns None if there is none."""
        try:
            return self._conn.recv(TLS_BUFFER_SIZE)
        except SSL.WantReadError:
            return None
        except SSL.ZeroReturnError:
            reason = 'close-notify alert received'
        except SSL.Error as err:
            reason = repr(err)
        finally:
            self._flush()

        if not self._close_notify_received:
            self._close_notify_received = True
            if self._close_notify_sent:
                logger.debug(f'DTLS connection closed by local, {reason} from peer.')
            else:
                logger.warning(f'DTLS connection closed by peer: {reason}.')
        return None

    def _encode_request(self, payload: bytes) -> tuple[bytes, bytes]:
        """Encodes a confirmable CoAP POST request carrying the given TLV payload. Returns (request, token)."""
        self._message_id = (self._message_id + 1) & 0xFFFF
        token = os.urandom(4)
        msg = Message(mtype=Type.CON, code=Code.POST, mid=self._message_id, token=token, payload=payload)
        msg.opt.uri_path = TCAT_URI_PATH
        return msg.encode(), token

    @staticmethod
    def _decode_response(data: bytes, token: bytes) -> Optional[bytes]:
        """Decodes a CoAP response. Returns its TLV payload, or None if it is not the response for the token."""
        msg = Message.decode(data)
        if msg.token != token or not msg.code.is_response():
            logger.debug(f'Ignoring unexpected CoAP message: {msg}')
            return None
        if msg.code != Code.CHANGED:
            raise RuntimeError(f'CoAP error response: {msg.code}')
        return msg.payload

    # Precondition: caller must handle all exceptions raised
    def _send(self, data: bytes) -> None:
        max_size = self._conn.get_cleartext_mtu()
        if len(data) > max_size:
            raise RuntimeError(f'CoAP request of {len(data)} bytes is too large for TCAT over CoAPS '
                               f'(max {max_size} bytes).')
        self._conn.send(data)
        self._flush()

    async def send_with_resp(self, data: bytes, timeout: float = TCAT_RESPONSE_TIMEOUT_SEC) -> bytes:
        """
        Send TCAT command TLV(s) to the TCAT Device in a CoAP request and wait for the CoAP response.

        Args:
            data: The TLV data to send to the TCAT Device.
            timeout: The maximum time in seconds to wait for the response. Defaults to TCAT_RESPONSE_TIMEOUT_SEC.

        Returns:
            bytes: The TLV data of the response, or empty b'' if no (successful) response was received within
                   the timeout.
        """
        async with self._lock:
            if not self.is_connected:
                logger.error('Not connected to a TCAT Device.')
                return b''
            logger.debug(f'tx {len(data)} bytes\n' + (utils.hexdump_ot('Tx', data) if data else ''))
            request, token = self._encode_request(data)
            try:
                self._send(request)
            except Exception as err:
                logger.error(err)
                return b''

            loop = asyncio.get_running_loop()
            end_time = loop.time() + timeout
            while self.is_connected:
                response = self._read_application_data()
                if response is None:
                    remaining = end_time - loop.time()
                    if remaining <= 0 or not await self._receive_datagram(remaining):
                        logger.error(f'No response when response TLV/line expected (timeout={timeout}s).')
                        break
                    continue
                try:
                    payload = self._decode_response(response, token)
                except Exception as err:
                    logger.error(err)
                    break
                if payload is not None:
                    logger.debug(f'rx {len(payload)} bytes\n' + (utils.hexdump_ot('Rx', payload) if payload else ''))
                    return payload
            return b''

    async def recv_unsolicited_event(self) -> bytes:
        """
        Processes what was received from the TCAT Device while no TCAT command is ongoing, such as the alert
        that it sends when it closes the DTLS connection.

        A TCAT Device cannot send unsolicited events over CoAPS: always returns empty b''.
        """
        if self._lock.locked():
            return b''
        async with self._lock:
            while self.is_connected and not self._protocol.datagrams.empty():
                self._conn.bio_write(self._protocol.datagrams.get_nowait())
                while self.is_connected:
                    data = self._read_application_data()
                    if data is None:
                        break
                    logger.debug(f'Ignoring unexpected data from TCAT Device:\n{utils.hexdump_ot("Rx", data)}')
        return b''

    async def close(self, timeout: float = TLS_CLOSE_TIMEOUT_SEC) -> None:
        """Requests the TCAT Device to disconnect and closes the DTLS connection."""
        if self.is_connected:
            try:
                logger.debug('sending Disconnect command TLV')
                data = TLV(TcatTLVType.DISCONNECT.value, bytes()).to_bytes()
                async with self._lock:
                    request, _ = self._encode_request(data)
                    self._send(request)
            except asyncio.CancelledError:
                raise
            except Exception as err:
                logger.warning(f'Failed to send Disconnect command TLV: {err}')
                logger.debug(err, exc_info=True)

        await self.disconnect()

    async def disconnect(self) -> None:
        """Closes the DTLS connection and the underlying UDP socket."""
        conn = self._conn
        transport = self._transport
        was_connected = self.is_connected
        self._conn = None
        self._transport = None
        self._protocol = None
        self._peer_public_key = None
        self._peer_challenge = None
        if was_connected:
            try:
                # Send a close-notify alert, without waiting for the one that the TCAT Device sends in return.
                self._close_notify_sent = True
                conn.shutdown()
                for datagram in _split_into_datagrams(conn.bio_read(TLS_BUFFER_SIZE), DTLS_MAX_DATAGRAM_SIZE):
                    transport.sendto(datagram)
            except Exception as err:
                logger.debug(err, exc_info=True)
        if transport is not None:
            transport.close()

    @property
    def is_connected(self) -> bool:
        return self._conn is not None and self._peer_public_key is not None and not self._close_notify_received

    @property
    def close_notify_received(self) -> bool:
        return self._close_notify_received

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
        try:
            peer_cert = self._conn.get_peer_certificate(as_cryptography=True)
            logger.info(f'TCAT Device cert: {peer_cert.subject.rfc4514_string()}')
            logger.info('  base64: (paste in https://lapo.it/asn1js/ to decode)\n' +
                        utils.base64_string(peer_cert.public_bytes(Encoding.DER)))
            logger.info(f'TCAT Commissioner cert, PEM:\n{self.cert}')
        except Exception:
            logger.warning('Could not display TCAT Device cert info.')
