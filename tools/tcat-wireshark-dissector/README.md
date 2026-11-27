# TCAT Wireshark Dissector

`tcat.lua` is a Lua plugin for Wireshark and tshark that dissects the command and response TLVs of TCAT (Thread Commissioning over Authenticated TLS).

It dissects the TCAT TLVs in the payload of CoAP messages to and from the resource `/c/tc`. This is how the experimental TCAT over CoAPS transport of OpenThread carries the TLVs. TCAT over BLE is not supported.

For each TLV, it shows:

- The type, the command class and the length, in the Base and in the Extended TLV format.
- The status of a Response With Status TLV.
- The value. The payload of a Response With Payload TLV is dissected based on the command that it responds to: for example as a network name, or as a list of Service Name TLVs.
- An Active Operational Dataset, Diagnostic TLVs and a TCAT Commissioner certificate in full detail, using the Thread MeshCoP, Thread Diagnostics and X.509 certificate dissectors of Wireshark.

The Info column shows a summary, such as `Ping: 10 bytes` or `Response With Status (Start Thread Network): Success`.

## Requirements

- Wireshark or tshark with Lua support. The plugin is tested with version 4.6.
- A decrypted DTLS session: TCAT TLVs are only visible if Wireshark can decrypt the DTLS records. To enable this, let the TCAT Commissioner write a key log file using the `SSLKEYLOGFILE` environment variable, as described in the [TCAT Commissioner README](../tcat_ble_client/README.md).
- To dissect a response, the capture must also include the CoAP request that it responds to.

## Install

Copy `tcat.lua` to the personal Lua plugins folder of Wireshark. On Linux and macOS:

```bash
mkdir -p ~/.local/lib/wireshark/plugins
cp tools/tcat-wireshark-dissector/tcat.lua ~/.local/lib/wireshark/plugins/
```

On Windows the folder is `%APPDATA%\Wireshark\plugins`. Wireshark shows the folder under "Help", "About Wireshark", "Folders" as "Personal Lua Plugins", and so does `tshark -G folders`.

Then restart Wireshark, or use "Analyze", "Reload Lua Plugins". Both Wireshark and tshark load the plugin from this folder.

To use the plugin without installing it, load it from the command line:

```bash
wireshark -X lua_script:tools/tcat-wireshark-dissector/tcat.lua \
    -o tls.keylog_file:/tmp/tcat-keys.log /tmp/wpan0.pcapng
```

## Use with Wireshark

Open the capture with the key log file, which can also be set in the preferences under "Protocols", "TLS", "(Pre)-Master-Secret log filename":

```bash
wireshark -o tls.keylog_file:/tmp/tcat-keys.log /tmp/wpan0.pcapng
```

The TCAT messages show `TCAT` in the Protocol column. Some display filters:

| Display filter             | Shows                                           |
| -------------------------- | ----------------------------------------------- |
| `tcat`                     | All TCAT messages                               |
| `tcat.type == 0x20`        | Set Active Operational Dataset commands         |
| `tcat.status != 0`         | Responses with an error status                  |
| `tcat.response_to == 0x0a` | Responses to Ping commands                      |
| `tcat.malformed`           | Messages with a TLV that could not be dissected |

## Use with tshark

tshark takes the same options. The examples assume that the plugin is installed; otherwise add the `-X lua_script:` option.

List the TCAT messages:

```bash
tshark -o tls.keylog_file:/tmp/tcat-keys.log -r /tmp/wpan0.pcapng -Y tcat
```

```
   14 8.089344349 fe80::540d:ab7b:ade8:1759 → fe80::3c6f:70dc:e7b6:4e25 TCAT 111 Ping: 10 bytes
   15 8.092409565 fe80::3c6f:70dc:e7b6:4e25 → fe80::540d:ab7b:ade8:1759 TCAT 106 Response With Payload (Ping): 10 bytes
   18 11.089638666 fe80::540d:ab7b:ade8:1759 → fe80::3c6f:70dc:e7b6:4e25 TCAT 101 Get Device ID
   19 11.095706718 fe80::3c6f:70dc:e7b6:4e25 → fe80::540d:ab7b:ade8:1759 TCAT 104 Response With Payload (Get Device ID): 18b4300000000001
```

Show the TLVs of each message in detail:

```bash
tshark -o tls.keylog_file:/tmp/tcat-keys.log -r /tmp/wpan0.pcapng -Y tcat -O tcat
```

Print selected fields, here of the responses with an error status:

```bash
tshark -o tls.keylog_file:/tmp/tcat-keys.log -r /tmp/wpan0.pcapng -Y 'tcat.status != 0' \
    -T fields -e frame.number -e tcat.response_to -e tcat.status
```

On Ubuntu, AppArmor can deny tshark access to files in the home directory. tshark then reports `You don't have permission to read the file` for the `-X lua_script:` option. Either load a copy of `tcat.lua` from `/tmp`, or allow tshark to read the folder with a rule in `/etc/apparmor.d/local/tshark`.

## Try it out

The [TCAT over CoAPS simulation test](../tcat-coaps-sim-test/README.txt) can produce both a capture and a key log file, without any hardware:

```bash
PCAP=/tmp/wpan0.pcapng SSLKEYLOGFILE=/tmp/tcat-keys.log PANID=0xc64e CHANNEL=18 \
    ./tools/tcat-coaps-sim-test/run.sh "hello;ping;commission;thread start"
```
