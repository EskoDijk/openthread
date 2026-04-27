"""
  Copyright (c) 2024-2025, The OpenThread Authors.
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
from typing import Optional, Callable

from cryptography.x509 import load_der_x509_certificate
from cryptography.hazmat.primitives.serialization import Encoding, PublicFormat
from mbedtls import tls, x509, pk
from mbedtls._tls import WantReadError as MbedTLSWantReadError

from tlv.tlv import TLV
from tlv.tcat_tlv import TcatTLVType
import utils

logger = logging.getLogger(__name__)


def _load_private_key(keyfile: str):
    """Load an RSA or ECC private key from a PEM file."""
    key_pem = open(keyfile, 'rb').read()
    if b'EC' in key_pem:
        return pk.ECC.from_file(keyfile)
    return pk.RSA.from_file(keyfile)


class DtlsStream:
    """DTLS 1.2 client over UDP. Drop-in replacement for BleStreamSecure.

    Performs a DTLS handshake using python-mbedtls and provides the same
    send_with_resp / recv_unsolicited_event / close interface as BleStreamSecure
    so the CLI layer and receive_loop work without modification.
    """

    DTLS_BASE_PORT = 11000
    _POLL_TIMEOUT_SEC = 0.010  # non-blocking recv poll interval

    def __init__(self, host: str, port: int, interface: str = ''):
        self.address = (host, port)
        self._interface = interface
        self._family = socket.AF_INET6 if ':' in host else socket.AF_INET
        self._dtls_sock = None
        self._peer_public_key = None
        self._peer_challenge = None
        self._connected = False
        self._async_events_queue: asyncio.Queue = asyncio.Queue()
        self._recv_lock = asyncio.Lock()
        self.cert = ''
        self._certfile = ''
        self._keyfile = ''
        self._cafile = ''

    def __str__(self):
        return f"DtlsStream[{self.address[0]}:{self.address[1]}]"

    def load_cert(self, certfile: str = '', keyfile: str = '', cafile: str = ''):
        self._certfile = certfile
        self._keyfile = keyfile
        self._cafile = cafile
        if certfile:
            self.cert = utils.load_cert_pem(certfile)

    async def do_handshake(self,
                           buffersize: int = 4096,
                           timeout: float = 30.0,
                           progress_callback: Optional[Callable[[bool], None]] = None) -> bool:
        """Perform DTLS handshake. Returns True on success, False otherwise."""
        if progress_callback:
            progress_callback(False)

        try:
            crt = x509.CRT.from_file(self._certfile)
            key = _load_private_key(self._keyfile)
            ts = tls.TrustStore.from_pem_file(self._cafile)

            conf = tls.DTLSConfiguration(
                trust_store=ts,
                certificate_chain=([crt], key),
                validate_certificates=True,
                lowest_supported_version=tls.DTLSVersion.DTLSv1_2,
                highest_supported_version=tls.DTLSVersion.DTLSv1_2,
                handshake_timeout_min=1.0,
                handshake_timeout_max=min(timeout, 60.0),
            )
            ctx = tls.ClientContext(conf)

            udp_sock = socket.socket(self._family, socket.SOCK_DGRAM)
            udp_sock.settimeout(timeout)
            if self._family == socket.AF_INET6 and self._interface:
                scope_id = socket.if_nametoindex(self._interface)
                udp_sock.connect((self.address[0], self.address[1], 0, scope_id))
            else:
                udp_sock.connect(self.address)

            dtls_sock = ctx.wrap_socket(udp_sock, server_hostname=None)

            loop = asyncio.get_running_loop()
            await loop.run_in_executor(None, dtls_sock.do_handshake)

            peer_cert_der = dtls_sock._buffer.getpeercert(binary_form=True)
            cert_obj = load_der_x509_certificate(peer_cert_der)
            self._peer_public_key = cert_obj.public_key().public_bytes(Encoding.DER,
                                                                        PublicFormat.SubjectPublicKeyInfo)
            self._dtls_sock = dtls_sock
            self._connected = True
            self._log_cert_identities()

        except Exception as err:
            if progress_callback:
                progress_callback(True)
            logger.error(f'DTLS handshake failed: {err}')
            logger.debug(err, exc_info=True)
            return False

        if progress_callback:
            progress_callback(True)
        return True

    async def _send(self, data: bytes) -> None:
        hexdump_str = utils.hexdump_ot("Tx", data) if data else ''
        logger.debug(f"tx {len(data)} bytes\n{hexdump_str}")
        loop = asyncio.get_running_loop()
        await loop.run_in_executor(None, self._dtls_sock.send, data)

    async def _recv(self, bufsize: int = 4096, timeout: float = 0.0) -> bytes:
        if not self.is_connected:
            return b''

        loop = asyncio.get_running_loop()

        def _try_recv() -> bytes:
            self._dtls_sock.settimeout(self._POLL_TIMEOUT_SEC)
            try:
                return self._dtls_sock.recv(bufsize)
            except (socket.timeout, TimeoutError, OSError, MbedTLSWantReadError):
                return b''

        if timeout <= 0.0:
            return await loop.run_in_executor(None, _try_recv)

        slp_time = 0.020
        end_time = loop.time() + timeout
        while loop.time() < end_time:
            data = await loop.run_in_executor(None, _try_recv)
            if data:
                hexdump_str = utils.hexdump_ot("Rx", data)
                logger.debug(f"rx {len(data)} bytes\n{hexdump_str}")
                return data
            remaining = end_time - loop.time()
            if remaining > 0:
                await asyncio.sleep(min(slp_time, remaining))

        return b''

    async def send_with_resp(self, data: bytes, timeout: float = 5.0) -> bytes:
        """Send data and wait for a response TLV."""
        async with self._recv_lock:
            while True:
                pend = await self._recv(timeout=0.0)
                if not pend:
                    break
                await self._async_events_queue.put(pend)

            await self._send(data)
            res = await self._recv(timeout=timeout)
            if not res:
                logger.error(f'No response when response TLV/line expected (timeout={timeout}s).')
            return res

    async def recv_unsolicited_event(self) -> bytes:
        """Return queued or newly arrived unsolicited event data, or b'' if none."""
        try:
            return self._async_events_queue.get_nowait()
        except asyncio.QueueEmpty:
            pass

        if self.is_connected:
            async with self._recv_lock:
                data = await self._recv(timeout=0.0)
            if data:
                await self._async_events_queue.put(data)

        try:
            return self._async_events_queue.get_nowait()
        except asyncio.QueueEmpty:
            return b''

    async def close(self, timeout: float = 5.0) -> None:
        """Send DISCONNECT TLV and close the DTLS socket."""
        if self.is_connected:
            try:
                logger.debug('sending Disconnect command TLV')
                data = TLV(TcatTLVType.DISCONNECT.value, bytes()).to_bytes()
                await self._send(data)
            except asyncio.CancelledError:
                raise
            except Exception as err:
                logger.warning(f'Failed to send Disconnect command TLV: {err}')
                logger.debug(err, exc_info=True)

        await self.disconnect()

    async def disconnect(self) -> None:
        """Close the DTLS socket immediately."""
        self._connected = False
        self._peer_public_key = None
        self._peer_challenge = None
        if self._dtls_sock is not None:
            try:
                self._dtls_sock.close()
            except Exception:
                pass
            self._dtls_sock = None

    @property
    def is_connected(self) -> bool:
        return self._connected and self._dtls_sock is not None

    @property
    def peer_public_key(self):
        return self._peer_public_key

    @property
    def peer_challenge(self):
        return self._peer_challenge

    @peer_challenge.setter
    def peer_challenge(self, value):
        self._peer_challenge = value

    def _log_cert_identities(self):
        try:
            peer_cert_der = self._dtls_sock._buffer.getpeercert(binary_form=True)
            cert_obj = load_der_x509_certificate(peer_cert_der)
            logger.info(f'TCAT Device cert: {cert_obj.subject}')
            peer_cert_b64 = utils.base64_string(peer_cert_der)
            logger.info(f'  base64: (paste in https://lapo.it/asn1js/ to decode)\n{peer_cert_b64}')
        except Exception:
            logger.warning('Could not display TCAT Device cert info.')
        logger.info(f'TCAT Commissioner cert, PEM:\n{self.cert}')
