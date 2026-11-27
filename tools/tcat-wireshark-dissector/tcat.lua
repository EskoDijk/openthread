--
--  Copyright (c) 2026, The OpenThread Authors.
--  All rights reserved.
--
--  Redistribution and use in source and binary forms, with or without
--  modification, are permitted provided that the following conditions are met:
--  1. Redistributions of source code must retain the above copyright
--     notice, this list of conditions and the following disclaimer.
--  2. Redistributions in binary form must reproduce the above copyright
--     notice, this list of conditions and the following disclaimer in the
--     documentation and/or other materials provided with the distribution.
--  3. Neither the name of the copyright holder nor the
--     names of its contributors may be used to endorse or promote products
--     derived from this software without specific prior written permission.
--
--  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
--  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
--  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
--  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
--  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
--  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
--  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
--  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
--  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
--  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
--  POSSIBILITY OF SUCH DAMAGE.
--

-- Wireshark dissector for TCAT (Thread Commissioning over Authenticated TLS)
-- command and response TLVs, as defined in the Thread specification chapter
-- "TCAT", section "TCAT Commands and TLVs".
--
-- It dissects the TCAT TLVs that are carried as the payload of CoAP messages
-- to and from the resource /c/tc: the experimental "TCAT over CoAPS"
-- transport of OpenThread. See README.md for how to install and use it.
--
-- The script only uses language features that Lua 5.2 (Wireshark 4.2 and
-- older) and Lua 5.4 (Wireshark 4.4 and newer) have in common.

local tcat = Proto("tcat", "Thread Commissioning over Authenticated TLS")

-------------------------------------------------------------------------------
-- Constants
-------------------------------------------------------------------------------

-- The CoAP resource that carries the TCAT TLVs.
local TCAT_URI_PATH = "/c/tc"

local MEDIA_TYPE = "application/octet-stream"

-- A Length octet with this value announces the Extended TLV format, with a
-- Length of two octets.
local EXTENDED_LENGTH_ESCAPE = 0xFF

local TLV_RESPONSE_WITH_STATUS = 0x01
local TLV_RESPONSE_WITH_PAYLOAD = 0x02
local TLV_RESPONSE_EVENT = 0x03
local TLV_GET_NETWORK_NAME = 0x08
local TLV_DISCONNECT = 0x09
local TLV_PING = 0x0A
local TLV_GET_DEVICE_ID = 0x0B
local TLV_GET_EXTENDED_PAN_ID = 0x0C
local TLV_GET_PROVISIONING_URL = 0x0D
local TLV_PRESENT_PSKD_HASH = 0x10
local TLV_PRESENT_PSKC_HASH = 0x11
local TLV_PRESENT_INSTALL_CODE_HASH = 0x12
local TLV_REQUEST_RANDOM_NUMBER_CHALLENGE = 0x13
local TLV_SET_ACTIVE_OPERATIONAL_DATASET = 0x20
local TLV_SET_ACTIVE_OPERATIONAL_DATASET_ALT = 0x21
local TLV_GET_COMMISSIONER_CERTIFICATE = 0x25
local TLV_GET_DIAGNOSTIC_TLVS = 0x26
local TLV_START_THREAD_NETWORK = 0x27
local TLV_STOP_THREAD_NETWORK = 0x28
local TLV_GET_ACTIVE_OPERATIONAL_DATASET = 0x40
local TLV_GET_ACTIVE_OPERATIONAL_DATASET_ALT = 0x41
local TLV_DECOMMISSION = 0x60
local TLV_GET_APPLICATION_LAYERS = 0x80
local TLV_SEND_APPLICATION_DATA_1 = 0x81
local TLV_SEND_APPLICATION_DATA_2 = 0x82
local TLV_SEND_APPLICATION_DATA_3 = 0x83
local TLV_SEND_APPLICATION_DATA_4 = 0x84
local TLV_SERVICE_NAME_UDP = 0x89
local TLV_SERVICE_NAME_TCP = 0x8A
local TLV_VENDOR_SPECIFIC = 0x9F
local TLV_SET_LDEVID_OPERATIONAL_CERTIFICATE = 0xA0
local TLV_SET_LDEVID_PRIVATE_KEY = 0xA1
local TLV_SET_DOMAIN_CA_CERTIFICATE = 0xA2

local TLV_NAMES = {
    [TLV_RESPONSE_WITH_STATUS] = "Response With Status",
    [TLV_RESPONSE_WITH_PAYLOAD] = "Response With Payload",
    [TLV_RESPONSE_EVENT] = "Response Event",
    [TLV_GET_NETWORK_NAME] = "Get Network Name",
    [TLV_DISCONNECT] = "Disconnect",
    [TLV_PING] = "Ping",
    [TLV_GET_DEVICE_ID] = "Get Device ID",
    [TLV_GET_EXTENDED_PAN_ID] = "Get Extended PAN ID",
    [TLV_GET_PROVISIONING_URL] = "Get Provisioning URL",
    [TLV_PRESENT_PSKD_HASH] = "Present PSKd Hash",
    [TLV_PRESENT_PSKC_HASH] = "Present PSKc Hash",
    [TLV_PRESENT_INSTALL_CODE_HASH] = "Present Install-code Hash",
    [TLV_REQUEST_RANDOM_NUMBER_CHALLENGE] = "Request Random Number Challenge",
    [TLV_SET_ACTIVE_OPERATIONAL_DATASET] = "Set Active Operational Dataset",
    [TLV_SET_ACTIVE_OPERATIONAL_DATASET_ALT] = "Set Active Operational Dataset - alternative 1",
    [TLV_GET_COMMISSIONER_CERTIFICATE] = "Get Commissioner Certificate",
    [TLV_GET_DIAGNOSTIC_TLVS] = "Get Diagnostic TLVs",
    [TLV_START_THREAD_NETWORK] = "Start Thread Network",
    [TLV_STOP_THREAD_NETWORK] = "Stop Thread Network",
    [TLV_GET_ACTIVE_OPERATIONAL_DATASET] = "Get Active Operational Dataset",
    [TLV_GET_ACTIVE_OPERATIONAL_DATASET_ALT] = "Get Active Operational Dataset - alternative 1",
    [TLV_DECOMMISSION] = "Decommission",
    [TLV_GET_APPLICATION_LAYERS] = "Get Application Layers",
    [TLV_SEND_APPLICATION_DATA_1] = "Send Application Data 1",
    [TLV_SEND_APPLICATION_DATA_2] = "Send Application Data 2",
    [TLV_SEND_APPLICATION_DATA_3] = "Send Application Data 3",
    [TLV_SEND_APPLICATION_DATA_4] = "Send Application Data 4",
    [TLV_SERVICE_NAME_UDP] = "Service Name UDP",
    [TLV_SERVICE_NAME_TCP] = "Service Name TCP",
    [TLV_VENDOR_SPECIFIC] = "Vendor Specific",
    [TLV_SET_LDEVID_OPERATIONAL_CERTIFICATE] = "Set LDevID Operational Certificate",
    [TLV_SET_LDEVID_PRIVATE_KEY] = "Set LDevID Private Key",
    [TLV_SET_DOMAIN_CA_CERTIFICATE] = "Set Domain CA Certificate",
}

-- Command classes, indexed by the three most significant bits of the TLV type.
local COMMAND_CLASS_NAMES = {
    [0] = "General",
    [1] = "Commissioning",
    [2] = "Extraction",
    [3] = "Decommissioning",
    [4] = "Application",
    [5] = "Commissioning-CCM",
    [6] = "Reserved",
    [7] = "Reserved",
}

local STATUS_SUCCESS = 0x00

local STATUS_NAMES = {
    [STATUS_SUCCESS] = "Success",
    [0x01] = "Unsupported",
    [0x02] = "Parse Error",
    [0x03] = "Value Error",
    [0x04] = "General Error",
    [0x05] = "Busy",
    [0x06] = "Undefined",
    [0x07] = "Hash Error",
    [0x08] = "Invalid State",
    [0x10] = "Unauthorized",
    -- The lower three bits are reserved for details on the authorization that is needed.
    [0x11] = "Unauthorized",
    [0x12] = "Unauthorized",
    [0x13] = "Unauthorized",
    [0x14] = "Unauthorized",
    [0x15] = "Unauthorized",
    [0x16] = "Unauthorized",
    [0x17] = "Unauthorized",
}

-- Thread Network Diagnostic TLV types, as requested with Get Diagnostic TLVs.
local DIAGNOSTIC_TLV_NAMES = {
    [0] = "MAC Extended Address",
    [1] = "MAC Address (16-bit)",
    [2] = "Mode",
    [3] = "Timeout",
    [4] = "Connectivity",
    [5] = "Route64",
    [6] = "Leader Data",
    [7] = "Network Data",
    [8] = "IPv6 Address List",
    [9] = "MAC Counters",
    [14] = "Battery Level",
    [15] = "Supply Voltage",
    [16] = "Child Table",
    [17] = "Channel Pages",
    [18] = "Type List",
    [19] = "Max Child Timeout",
    [23] = "EUI-64",
    [24] = "Version",
    [25] = "Vendor Name",
    [26] = "Vendor Model",
    [27] = "Vendor SW Version",
    [28] = "Thread Stack Version",
    [29] = "Child",
    [30] = "Child IPv6 Address List",
    [31] = "Router Neighbor",
    [32] = "Answer",
    [33] = "Query ID",
    [34] = "MLE Counters",
    [35] = "Vendor App URL",
    [36] = "Non-Preferred Channels Mask",
    [37] = "Enhanced Route",
    [38] = "Border Router State",
    [39] = "Border Router Infra Interface Addresses",
    [40] = "Border Router Local OMR Prefix",
    [41] = "Border Router DHCPv6-PD OMR Prefix",
    [42] = "Border Router Local On-link Prefix",
    [43] = "Border Router Favored On-link Prefix",
}

-------------------------------------------------------------------------------
-- Protocol fields
-------------------------------------------------------------------------------

local fields = {
    tlv = ProtoField.none("tcat.tlv", "TLV"),
    type = ProtoField.uint8("tcat.type", "Type", base.HEX, TLV_NAMES),
    command_class = ProtoField.string("tcat.command_class", "Command Class"),
    length = ProtoField.uint16("tcat.length", "Length", base.DEC),
    value = ProtoField.bytes("tcat.value", "Value"),
    response_to = ProtoField.uint8("tcat.response_to", "Response To Command", base.HEX, TLV_NAMES),
    status = ProtoField.uint8("tcat.status", "Status", base.HEX, STATUS_NAMES),
    payload = ProtoField.bytes("tcat.payload", "Payload"),
    network_name = ProtoField.string("tcat.network_name", "Network Name", base.UNICODE),
    device_id = ProtoField.bytes("tcat.device_id", "Device ID"),
    extended_pan_id = ProtoField.bytes("tcat.extended_pan_id", "Extended PAN ID"),
    provisioning_url = ProtoField.string("tcat.provisioning_url", "Provisioning URL", base.UNICODE),
    ping_payload = ProtoField.bytes("tcat.ping_payload", "Ping Payload"),
    hash = ProtoField.bytes("tcat.hash", "Hash"),
    random_challenge = ProtoField.bytes("tcat.random_challenge", "Random Number Challenge"),
    dataset = ProtoField.bytes("tcat.dataset", "Active Operational Dataset"),
    commissioner_certificate = ProtoField.bytes("tcat.commissioner_certificate", "Commissioner Certificate"),
    diagnostic_tlv_type = ProtoField.uint8("tcat.diagnostic_tlv_type", "Diagnostic TLV Type", base.DEC,
                                           DIAGNOSTIC_TLV_NAMES),
    diagnostic_tlvs = ProtoField.bytes("tcat.diagnostic_tlvs", "Diagnostic TLVs"),
    service_name = ProtoField.string("tcat.service_name", "Service Name"),
    application_data = ProtoField.bytes("tcat.application_data", "Application Data"),
    vendor_data = ProtoField.bytes("tcat.vendor_data", "Vendor Specific Data"),
}

tcat.fields = fields

local experts = {
    malformed = ProtoExpert.new("tcat.malformed", "Malformed TCAT TLV", expert.group.MALFORMED, expert.severity.ERROR),
    error_status = ProtoExpert.new("tcat.error_status", "TCAT command was not successful", expert.group.RESPONSE_CODE,
                                   expert.severity.NOTE),
}

tcat.experts = experts

-- Fields of the CoAP dissector, to find the resource and to match a response to its request.
local coap_code_field = Field.new("coap.code")
local coap_uri_path_field = Field.new("coap.opt.uri_path_recon")
local coap_response_to_field = Field.new("coap.response_to")

-- The description in the Info column of a TLV that cannot be dissected.
local MALFORMED_DESCRIPTION = "Malformed TLV"

-- The first CoAP response code. Lower codes are request methods (or the empty message).
local COAP_FIRST_RESPONSE_CODE = 64

-------------------------------------------------------------------------------
-- Helpers
-------------------------------------------------------------------------------

local function find_dissector(name)
    local ok, dissector = pcall(Dissector.get, name)

    if ok then
        return dissector
    end

    return nil
end

-- Dissectors of Wireshark for the data that TCAT carries. Any of these can be nil.
local meshcop_dissector = find_dissector("thread_meshcop")
local diagnostic_dissector = find_dissector("thread_diagnostic")
local certificate_dissector = find_dissector("pkix-cert")

-- The TLV type of the command in each CoAP request, indexed by frame number.
-- This tells how to interpret the payload of the matching response.
local request_types = {}

local function tlv_name(tlv_type)
    return TLV_NAMES[tlv_type] or string.format("Reserved (0x%02x)", tlv_type)
end

local function command_class_name(tlv_type)
    return COMMAND_CLASS_NAMES[math.floor(tlv_type / 0x20)]
end

local function byte_count(length)
    if length == 1 then
        return "1 byte"
    end

    return length .. " bytes"
end

-- Returns the last occurrence of a field in the packet: the one of the innermost CoAP message.
local function last_field(...)
    local infos = {...}

    return infos[#infos]
end

-- Runs a dissector of Wireshark on a TLV value. The columns are restored by the caller.
local function call_dissector(dissector, range, pinfo, tree)
    if dissector ~= nil then
        pcall(dissector.call, dissector, range:tvb(), pinfo, tree)
    end
end

-------------------------------------------------------------------------------
-- TLV value dissectors
--
-- Each takes the TvbRange of a value (nil if the value is empty), the packet
-- info and the tree of the TLV. Each returns a short description of the value
-- for the Info column, or nil.
-------------------------------------------------------------------------------

local dissect_tlvs

local function bytes_value(field)
    return function(range, _, tree)
        if range == nil then
            return byte_count(0)
        end

        tree:add(field, range)

        return byte_count(range:len())
    end
end

-- A short value, of which the hex string is shown in the Info column.
local function identifier_value(field)
    return function(range, _, tree)
        if range == nil then
            return nil
        end

        tree:add(field, range)

        return range:bytes():tohex(true)
    end
end

local function string_value(field)
    return function(range, _, tree)
        if range == nil then
            return '""'
        end

        tree:add_packet_field(field, range, ENC_UTF_8 + ENC_NA)

        return range:string(ENC_UTF_8)
    end
end

local function dataset_value(range, pinfo, tree)
    if range == nil then
        return byte_count(0)
    end

    local item = tree:add(fields.dataset, range)

    call_dissector(meshcop_dissector, range, pinfo, item)

    return byte_count(range:len())
end

local function diagnostic_tlvs_value(range, pinfo, tree)
    if range == nil then
        return byte_count(0)
    end

    local item = tree:add(fields.diagnostic_tlvs, range)

    call_dissector(diagnostic_dissector, range, pinfo, item)

    return byte_count(range:len())
end

local function certificate_value(range, pinfo, tree)
    if range == nil then
        return byte_count(0)
    end

    local item = tree:add(fields.commissioner_certificate, range)

    call_dissector(certificate_dissector, range, pinfo, item)

    return byte_count(range:len())
end

local function diagnostic_tlv_types_value(range, _, tree)
    local types = {}

    if range ~= nil then
        for offset = 0, range:len() - 1 do
            tree:add(fields.diagnostic_tlv_type, range:range(offset, 1))
            types[#types + 1] = tostring(range:range(offset, 1):uint())
        end
    end

    return table.concat(types, ", ")
end

local function service_names_value(range, pinfo, tree)
    if range == nil then
        return "none"
    end

    return dissect_tlvs(range:tvb(), pinfo, tree, nil)
end

-- Dissectors of the value of a command TLV. A command that is not listed has no value defined.
local COMMAND_VALUES = {
    [TLV_PING] = bytes_value(fields.ping_payload),
    [TLV_PRESENT_PSKD_HASH] = bytes_value(fields.hash),
    [TLV_PRESENT_PSKC_HASH] = bytes_value(fields.hash),
    [TLV_PRESENT_INSTALL_CODE_HASH] = bytes_value(fields.hash),
    [TLV_SET_ACTIVE_OPERATIONAL_DATASET] = dataset_value,
    [TLV_SET_ACTIVE_OPERATIONAL_DATASET_ALT] = dataset_value,
    [TLV_GET_DIAGNOSTIC_TLVS] = diagnostic_tlv_types_value,
    [TLV_SEND_APPLICATION_DATA_1] = bytes_value(fields.application_data),
    [TLV_SEND_APPLICATION_DATA_2] = bytes_value(fields.application_data),
    [TLV_SEND_APPLICATION_DATA_3] = bytes_value(fields.application_data),
    [TLV_SEND_APPLICATION_DATA_4] = bytes_value(fields.application_data),
    [TLV_SERVICE_NAME_UDP] = string_value(fields.service_name),
    [TLV_SERVICE_NAME_TCP] = string_value(fields.service_name),
    [TLV_VENDOR_SPECIFIC] = bytes_value(fields.vendor_data),
}

-- Dissectors of the value of a Response With Payload TLV, indexed by the TLV type of the command.
local RESPONSE_PAYLOADS = {
    [TLV_GET_NETWORK_NAME] = string_value(fields.network_name),
    [TLV_PING] = bytes_value(fields.ping_payload),
    [TLV_GET_DEVICE_ID] = identifier_value(fields.device_id),
    [TLV_GET_EXTENDED_PAN_ID] = identifier_value(fields.extended_pan_id),
    [TLV_GET_PROVISIONING_URL] = string_value(fields.provisioning_url),
    [TLV_REQUEST_RANDOM_NUMBER_CHALLENGE] = identifier_value(fields.random_challenge),
    [TLV_GET_COMMISSIONER_CERTIFICATE] = certificate_value,
    [TLV_GET_DIAGNOSTIC_TLVS] = diagnostic_tlvs_value,
    [TLV_GET_ACTIVE_OPERATIONAL_DATASET] = dataset_value,
    [TLV_GET_ACTIVE_OPERATIONAL_DATASET_ALT] = dataset_value,
    [TLV_GET_APPLICATION_LAYERS] = service_names_value,
    [TLV_SEND_APPLICATION_DATA_1] = bytes_value(fields.application_data),
    [TLV_SEND_APPLICATION_DATA_2] = bytes_value(fields.application_data),
    [TLV_SEND_APPLICATION_DATA_3] = bytes_value(fields.application_data),
    [TLV_SEND_APPLICATION_DATA_4] = bytes_value(fields.application_data),
    [TLV_VENDOR_SPECIFIC] = bytes_value(fields.vendor_data),
}

-------------------------------------------------------------------------------
-- TLV dissection
-------------------------------------------------------------------------------

-- Dissects the value of one TLV and returns the description of the TLV for the Info column.
local function dissect_value(tlv_type, range, pinfo, tree, request_type)
    local name = tlv_name(tlv_type)
    local is_response = tlv_type == TLV_RESPONSE_WITH_STATUS or tlv_type == TLV_RESPONSE_WITH_PAYLOAD
    local dissect = COMMAND_VALUES[tlv_type]
    local description

    if is_response and request_type ~= nil then
        tree:add(fields.response_to, request_type):set_generated()
        name = name .. " (" .. tlv_name(request_type) .. ")"
    end

    if tlv_type == TLV_RESPONSE_WITH_STATUS then
        if range == nil then
            tree:add_proto_expert_info(experts.malformed, "Response With Status TLV without a status")
            return name
        end

        local status = range:range(0, 1):uint()

        tree:add(fields.status, range:range(0, 1))

        if status ~= STATUS_SUCCESS then
            tree:add_proto_expert_info(experts.error_status)
        end

        return name .. ": " .. (STATUS_NAMES[status] or string.format("Unknown status 0x%02x", status))
    end

    if tlv_type == TLV_RESPONSE_WITH_PAYLOAD then
        dissect = RESPONSE_PAYLOADS[request_type] or bytes_value(fields.payload)
    end

    if dissect ~= nil then
        description = dissect(range, pinfo, tree)
    elseif range ~= nil then
        tree:add(fields.value, range)
        description = byte_count(range:len())
    end

    if description ~= nil and description ~= "" then
        name = name .. ": " .. description
    end

    return name
end

-- Dissects a sequence of TLVs, in the Base or the Extended TLV format. `request_type` is the TLV
-- type of the command that these TLVs respond to, or nil. Returns the description of the TLVs for
-- the Info column, and the type of the first TLV.
dissect_tlvs = function(tvb, pinfo, tree, request_type)
    local total = tvb:len()
    local offset = 0
    local descriptions = {}
    local first_type

    while offset < total do
        local header_length = 2

        if total - offset < header_length then
            tree:add_proto_expert_info(experts.malformed, "Truncated TLV header")
            descriptions[#descriptions + 1] = MALFORMED_DESCRIPTION
            break
        end

        local tlv_type = tvb(offset, 1):uint()
        local length = tvb(offset + 1, 1):uint()
        local is_extended = length == EXTENDED_LENGTH_ESCAPE

        if is_extended then
            header_length = 4

            if total - offset < header_length then
                tree:add_proto_expert_info(experts.malformed, "Truncated Extended TLV header")
                descriptions[#descriptions + 1] = MALFORMED_DESCRIPTION
                break
            end

            length = tvb(offset + 2, 2):uint()
        end

        local available = total - offset - header_length
        local value_length = math.min(length, available)
        local tlv_tree = tree:add(fields.tlv, tvb(offset, header_length + value_length))
        local range

        tlv_tree:set_text(tlv_name(tlv_type) .. " TLV")
        tlv_tree:add(fields.type, tvb(offset, 1))
        tlv_tree:add(fields.command_class, command_class_name(tlv_type)):set_generated()

        if is_extended then
            tlv_tree:add(fields.length, tvb(offset + 2, 2)):append_text(" (Extended TLV format)")
        else
            tlv_tree:add(fields.length, tvb(offset + 1, 1))
        end

        if length > available then
            tlv_tree:add_proto_expert_info(experts.malformed, "TLV length exceeds the available data")
        end

        if value_length > 0 then
            range = tvb(offset + header_length, value_length)
        end

        descriptions[#descriptions + 1] = dissect_value(tlv_type, range, pinfo, tlv_tree, request_type)
        first_type = first_type or tlv_type
        offset = offset + header_length + value_length
    end

    return table.concat(descriptions, ", "), first_type
end

-------------------------------------------------------------------------------
-- Dissector of a CoAP payload
-------------------------------------------------------------------------------

local media_type_table = DissectorTable.get("media_type")

-- The dissector that handled this media type before, if any: used for all other CoAP resources.
local previous_dissector = media_type_table:get_dissector(MEDIA_TYPE)

function tcat.init()
    request_types = {}
end

function tcat.dissector(tvb, pinfo, tree)
    local uri_path = last_field(coap_uri_path_field())
    local code = last_field(coap_code_field())

    if uri_path == nil or code == nil or uri_path.value ~= TCAT_URI_PATH then
        if previous_dissector ~= nil then
            return previous_dissector:call(tvb, pinfo, tree)
        end

        return 0
    end

    local request_type

    if code.value >= COAP_FIRST_RESPONSE_CODE then
        local request_frame = last_field(coap_response_to_field())

        if request_frame ~= nil then
            request_type = request_types[request_frame.value]
        end
    end

    local tcat_tree = tree:add(tcat, tvb())
    local description, first_type = dissect_tlvs(tvb, pinfo, tcat_tree, request_type)

    if code.value < COAP_FIRST_RESPONSE_CODE then
        request_types[pinfo.number] = first_type
    end

    -- Set the columns last: the dissectors of the TLV values may have changed these.
    pinfo.cols.protocol = "TCAT"
    pinfo.cols.info = description

    return tvb:len()
end

-- TCAT over CoAPS does not use a CoAP Content-Format option, so that the CoAP dissector looks for
-- a payload dissector of the default media type.
media_type_table:add(MEDIA_TYPE, tcat)
