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

from abc import abstractmethod
import asyncio
from hashlib import sha256
import hmac
from secrets import token_bytes

from bleak import BLEDevice

from ble import ble_scanner
from ble.ble_stream import BleStream
from ble.udp_stream import UdpStream
from client.tcat_client import TcatClient
from cli.command import Command, CommandResultNone, CommandResultTLV, CommandResult, CommandResultError
from dataset.dataset import ThreadDataset
from tlv.tlv import TLV
from tlv.diagnostic_tlv import DiagnosticTLVType
from tlv.tcat_tlv import TcatTLVType
from utils import select_device_by_user_input

CHALLENGE_SIZE = 8


class HelpCommand(Command):

    def get_help_string(self) -> str:
        return 'Display help and return.'

    async def execute_default(self, args, context) -> CommandResult:
        commands = context['commands']
        for name, command in commands.items():
            print(f'{name}')
            command.print_help(indent=1)
        return CommandResultNone()


class DataNotPrepared(Exception):
    pass


class BleCommand(Command):

    @abstractmethod
    def get_log_string(self) -> str:
        pass

    @abstractmethod
    def prepare_data(self, args, context) -> bytes:
        pass

    async def execute_default(self, args, context) -> CommandResult:
        client: TcatClient = context['client']
        if not client.is_connected:
            return CommandResultError("TCAT Device not connected.")

        print(self.get_log_string())
        try:
            data = self.prepare_data(args, context)
            response = await client.send_with_resp(data)
            if not response:
                return CommandResultNone()
            tlv_response = TLV.from_bytes(response)
            self.process_response(tlv_response, context)
            return CommandResultTLV(tlv_response)
        except DataNotPrepared as err:
            return CommandResultError(f'Command failed: {err}')

    def process_response(self, tlv_response, context):
        pass


class HelloCommand(BleCommand):

    def get_log_string(self) -> str:
        return 'Sending hello world...'

    def get_help_string(self) -> str:
        return 'Send round trip "Hello world!" message.'

    def prepare_data(self, args, context) -> bytes:
        return TLV(TcatTLVType.VENDOR_APPLICATION.value, bytes('Hello world!', 'ascii')).to_bytes()


class GetApplicationLayersCommand(BleCommand):

    def get_log_string(self) -> str:
        return 'Getting application layers....'

    def get_help_string(self) -> str:
        return 'Get supported application layer service names from device.'

    def prepare_data(self, args, context) -> bytes:
        return TLV(TcatTLVType.GET_APPLICATION_LAYERS.value, bytes()).to_bytes()

    def process_response(self, tlv_response, context):
        if tlv_response.type == TcatTLVType.RESPONSE_W_PAYLOAD.value:
            payload = tlv_response.value
            i = 0
            print('Service names:')
            while payload:
                tlv_application = TLV.from_bytes(payload)
                payload = payload[2 + len(tlv_application.value):]
                i += 1
                if (tlv_application.type == TcatTLVType.SERVICE_NAME_UDP.value):
                    print(f"\tApplication {i} is UDP service: {tlv_application.value.decode('ascii')}")
                elif (tlv_application.type == TcatTLVType.SERVICE_NAME_TCP.value):
                    print(f"\tApplication {i} is TCP service: {tlv_application.value.decode('ascii')}")
                else:
                    print('\tUnknown service type.')
        else:
            print('Dataset extraction error.')


class SendApplicationData1(BleCommand):

    def get_log_string(self) -> str:
        return 'Sending data to application layer 1....'

    def get_help_string(self) -> str:
        return 'Send hex encoded data to application layer 1.'

    def prepare_data(self, args, context) -> bytes:
        payload = bytes.fromhex(args[0])
        return TLV(TcatTLVType.APPLICATION_DATA_1.value, payload).to_bytes()


class SendApplicationData2(BleCommand):

    def get_log_string(self) -> str:
        return 'Sending data to application layer 2....'

    def get_help_string(self) -> str:
        return 'Send hex encoded data to application layer 2.'

    def prepare_data(self, args, context) -> bytes:
        payload = bytes.fromhex(args[0])
        return TLV(TcatTLVType.APPLICATION_DATA_2.value, payload).to_bytes()


class SendApplicationData3(BleCommand):

    def get_log_string(self) -> str:
        return 'Sending data to application layer 3....'

    def get_help_string(self) -> str:
        return 'Send hex encoded data to application layer 3.'

    def prepare_data(self, args, context) -> bytes:
        payload = bytes.fromhex(args[0])
        return TLV(TcatTLVType.APPLICATION_DATA_3.value, payload).to_bytes()


class SendApplicationData4(BleCommand):

    def get_log_string(self) -> str:
        return 'Sending data to application layer 4....'

    def get_help_string(self) -> str:
        return 'Send hex encoded data to application layer 4.'

    def prepare_data(self, args, context) -> bytes:
        payload = bytes.fromhex(args[0])
        return TLV(TcatTLVType.APPLICATION_DATA_4.value, payload).to_bytes()


class SendVendorData(BleCommand):

    def get_log_string(self) -> str:
        return 'Sending data to vendor specific application layer....'

    def get_help_string(self) -> str:
        return 'Send hex encoded data to vendor specific application layer.'

    def prepare_data(self, args, context) -> bytes:
        payload = bytes.fromhex(args[0])
        return TLV(TcatTLVType.VENDOR_APPLICATION.value, payload).to_bytes()


class CommissionCommand(BleCommand):

    def get_log_string(self) -> str:
        return 'Commissioning...'

    def get_help_string(self) -> str:
        return 'Update the connected device with current dataset.'

    def prepare_data(self, args, context) -> bytes:
        dataset: ThreadDataset = context['dataset']
        dataset_bytes = dataset.to_bytes()
        return TLV(TcatTLVType.ACTIVE_DATASET.value, dataset_bytes).to_bytes()


class DecommissionCommand(BleCommand):

    def get_log_string(self) -> str:
        return 'Disabling Thread and decommissioning device...'

    def get_help_string(self) -> str:
        return 'Stop Thread interface and decommission device from current network.'

    def prepare_data(self, args, context) -> bytes:
        return TLV(TcatTLVType.DECOMMISSION.value, bytes()).to_bytes()


class DisconnectCommand(Command):

    def get_help_string(self) -> str:
        return 'Disconnect client from TCAT device'

    async def execute_default(self, args, context) -> CommandResult:
        await context['client'].disconnect()
        return CommandResultNone()


class SimulationBleDisconnectCommand(Command):

    def get_help_string(self) -> str:
        return 'Simulate a BLE link break to a simulated TCAT device (no Disconnect TLV, no TLS shutdown).'

    async def execute_default(self, args, context) -> CommandResult:
        client: TcatClient = context['client']
        link = client.link
        if link is None or not isinstance(link.transport, UdpStream):
            return CommandResultError('only available for a simulation connection (use \'simulation <id>\' first).')

        print('Disconnecting simulated BLE link...')
        # Signal the abrupt link break to the device, then close the TCAT link without a TLS
        # shutdown (no close-notify), mirroring a real abrupt disconnect on the client side too.
        await link.transport.simulation_ble_disconnect()
        await client.abort()
        print('Done')
        return CommandResultNone()


class ExtractDatasetCommand(BleCommand):

    def get_log_string(self) -> str:
        return 'Getting active dataset.'

    def get_help_string(self) -> str:
        return 'Get active dataset from device.'

    def prepare_data(self, args, context) -> bytes:
        return TLV(TcatTLVType.GET_ACTIVE_DATASET.value, bytes()).to_bytes()

    def process_response(self, tlv_response, context) -> None:
        if tlv_response.type == TcatTLVType.RESPONSE_W_PAYLOAD.value:
            dataset = ThreadDataset()
            dataset.set_from_bytes(tlv_response.value)
            dataset.print_content()
        else:
            print('Dataset extraction error.')


class GetCommissionerCertificate(BleCommand):

    def get_log_string(self) -> str:
        return 'Getting commissioner certificate.'

    def get_help_string(self) -> str:
        return 'Get last-store TCAT commissioner certificate from TCAT device.'

    def prepare_data(self, args, context) -> bytes:
        return TLV(TcatTLVType.GET_COMMISSIONER_CERTIFICATE.value, bytes()).to_bytes()


class GetDeviceIdCommand(BleCommand):

    def get_log_string(self) -> str:
        return 'Retrieving device id.'

    def get_help_string(self) -> str:
        return 'Get unique identifier for the TCAT device.'

    def prepare_data(self, args, context) -> bytes:
        return TLV(TcatTLVType.GET_DEVICE_ID.value, bytes()).to_bytes()


class GetExtPanIDCommand(BleCommand):

    def get_log_string(self) -> str:
        return 'Retrieving extended PAN ID.'

    def get_help_string(self) -> str:
        return 'Get extended PAN ID that is commissioned in the active dataset.'

    def prepare_data(self, args, context) -> bytes:
        return TLV(TcatTLVType.GET_EXT_PAN_ID.value, bytes()).to_bytes()


class GetProvisioningUrlCommand(BleCommand):

    def get_log_string(self) -> str:
        return 'Retrieving provisioning url.'

    def get_help_string(self) -> str:
        return 'Get a URL for an application suited to commission the TCAT device.'

    def prepare_data(self, args, context) -> bytes:
        return TLV(TcatTLVType.GET_PROVISIONING_URL.value, bytes()).to_bytes()


class GetNetworkNameCommand(BleCommand):

    def get_log_string(self) -> str:
        return 'Retrieving network name.'

    def get_help_string(self) -> str:
        return 'Get the Thread network name that is commissioned in the active dataset.'

    def prepare_data(self, args, context) -> bytes:
        return TLV(TcatTLVType.GET_NETWORK_NAME.value, bytes()).to_bytes()


class GetRandomNumberChallenge(BleCommand):

    def get_log_string(self) -> str:
        return 'Retrieving random challenge.'

    def get_help_string(self) -> str:
        return 'Get the device random number challenge.'

    def prepare_data(self, args, context) -> bytes:
        return TLV(TcatTLVType.GET_RANDOM_NUMBER_CHALLENGE.value, bytes()).to_bytes()

    def process_response(self, tlv_response, context) -> None:
        link = context['client'].link
        if link is not None and tlv_response.value is not None:
            if len(tlv_response.value) == CHALLENGE_SIZE:
                link.peer_challenge = tlv_response.value
            else:
                print('Challenge format invalid.')


class PingCommand(Command):

    def get_help_string(self) -> str:
        return 'Send echo request to TCAT device.'

    async def execute_default(self, args, context) -> CommandResult:
        client: TcatClient = context['client']
        if not client.is_connected:
            return CommandResultError("TCAT Device not connected.")
        payload_size = 10
        max_payload = 512
        if len(args) > 0:
            payload_size = int(args[0])
            if payload_size > max_payload:
                return CommandResultError(f'Payload size too large. Maximum supported value is {max_payload}')
        to_send = token_bytes(payload_size)
        data = TLV(TcatTLVType.PING.value, to_send).to_bytes()
        start_time = asyncio.get_running_loop().time()
        response = await client.send_with_resp(data)
        elapsed_time = 1e3 * (asyncio.get_running_loop().time() - start_time)
        if not response:
            return CommandResultNone()

        tlv_response = TLV.from_bytes(response)
        if tlv_response.value != to_send:
            print("Error: Ping response payload mismatch.")

        print(f"Roundtrip time: {elapsed_time} ms")

        return CommandResultTLV(tlv_response)


class PresentHash(BleCommand):

    def get_log_string(self) -> str:
        return 'Presenting hash.'

    def get_help_string(self) -> str:
        return 'Present calculated hash.'

    def prepare_data(self, args, context) -> bytes:
        type = args[0]
        code = None
        tlv_type = None
        if type == "pskd":
            code = bytes(args[1], 'utf-8')
            tlv_type = TcatTLVType.PRESENT_PSKD_HASH.value
        elif type == "pskc":
            code = bytes.fromhex(args[1])
            tlv_type = TcatTLVType.PRESENT_PSKC_HASH.value
        elif type == "install":
            code = bytes(args[1], 'utf-8')
            tlv_type = TcatTLVType.PRESENT_INSTALL_CODE_HASH.value
        else:
            raise DataNotPrepared("Hash code name incorrect.")
        link = context['client'].link
        if link is None or link.peer_public_key is None:
            raise DataNotPrepared("Peer certificate not present.")

        if link.peer_challenge is None:
            raise DataNotPrepared("Peer challenge not present.")

        hash = hmac.new(code, digestmod=sha256)
        hash.update(link.peer_challenge)
        hash.update(link.peer_public_key)

        data = TLV(tlv_type, hash.digest()).to_bytes()
        return data


class ScanCommand(Command):

    def get_help_string(self) -> str:
        return 'Perform scan for TCAT devices.'

    async def execute_default(self, args, context) -> CommandResult:
        if context['client'].is_connected:
            return CommandResultError('already connected to a TCAT device. Use \'disconnect\' first.')

        print('Scanning for BLE TCAT devices...')
        tcat_devices = await ble_scanner.scan_tcat_devices()
        device = select_device_by_user_input(tcat_devices)
        if device is not None:
            await connect_ble_device(context['client'], device)

        return CommandResultNone()


class SimulationCommand(Command):

    def get_help_string(self) -> str:
        return 'Connect to a simulated TCAT device over UDP.'

    async def execute_default(self, args, context) -> CommandResult:
        if len(args) != 1:
            return CommandResultError('need index number of simulated TCAT device as first argument.')
        if context['client'].is_connected:
            return CommandResultError('already connected to a TCAT device. Use \'disconnect\' first.')

        await connect_simulation(context['client'], int(args[0]))
        return CommandResultNone()


class DiagnosticTlvsCommand(BleCommand):

    def get_log_string(self) -> str:
        return 'Retrieving diagnostic information.'

    def get_help_string(self) -> str:
        return 'Get diagnostic TLVs from the TCAT device.'

    def prepare_data(self, args, context) -> bytes:
        num_args = DiagnosticTLVType.names_to_numbers(args)
        try:
            if not num_args:
                raise ValueError()
            vals = [int(x) for x in num_args]
            tlvs = bytes(vals)
        except ValueError:
            print('Please provide a list of diagnostic TLV types as names or numbers')
            print('TLV Types:')
            for key, value in DiagnosticTLVType.get_dict().items():
                print(f'{key} = {value},')
            raise DataNotPrepared()

        return TLV(TcatTLVType.GET_DIAGNOSTIC_TLVS.value, tlvs).to_bytes()


class ThreadStartCommand(BleCommand):

    def get_log_string(self) -> str:
        return 'Enabling Thread...'

    def get_help_string(self) -> str:
        return 'Enable thread interface.'

    def prepare_data(self, args, context) -> bytes:
        return TLV(TcatTLVType.THREAD_START.value, bytes()).to_bytes()


class ThreadStopCommand(BleCommand):

    def get_log_string(self) -> str:
        return 'Disabling Thread...'

    def get_help_string(self) -> str:
        return 'Disable thread interface.'

    def prepare_data(self, args, context) -> bytes:
        return TLV(TcatTLVType.THREAD_STOP.value, bytes()).to_bytes()


class ThreadStateCommand(Command):

    def __init__(self):
        super().__init__()
        self._subcommands = {'start': ThreadStartCommand(), 'stop': ThreadStopCommand()}

    def get_help_string(self) -> str:
        return 'Manipulate state of the Thread interface of the connected device.'

    async def execute_default(self, args, context) -> CommandResult:
        print('Invalid usage. Provide a subcommand.')
        return CommandResultNone()


async def connect_ble_device(client: TcatClient, device: BLEDevice) -> bool:
    """Connects the client to a TCAT device over BLE. Returns True if successful."""
    print(f'Connecting to {device}')
    return await client.connect(await BleStream.create(device.address))


async def connect_simulation(client: TcatClient, node_id: int) -> bool:
    """Connects the client to a simulated TCAT device (simulation node) over UDP. Returns True if successful."""
    transport = UdpStream('127.0.0.1', node_id)
    print(f'Connecting to {transport}')
    return await client.connect(transport)
