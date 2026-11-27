# TCAT Commissioner (BBTC) Client

## Overview

This is a Python implementation of a Bluetooth-Based Thread Commissioning (BBTC) client, based on Thread's TCAT (Thread Commissioning over Authenticated TLS) functionality.

## Installation

If you don't have the poetry module installed (check with `poetry --version`), install it first following the [official installation instructions](https://python-poetry.org/docs/#installation).

For example, if pipx is available:

```bash
pipx install poetry
```

If pipx is not available, it can be installed for Linux/Windows/MacOS following the [pipx installation instructions](https://pipx.pypa.io/stable/installation/).

Then, install this project using Poetry:

```
poetry install
```

This will install all the required modules to a virtual environment, which can be used by calling `poetry run <COMMAND>` from the project directory.

Note: Installation on Windows requires that [Build Tools for Visual Studio C/C++](https://visualstudio.microsoft.com/downloads/#build-tools-for-visual-studio-2022) be installed first.

## Usage

To see the supported commandline arguments of BBTC client, use:

```
poetry run python3 bbtc.py --help
```

In order to connect to a TCAT device, run:

```bash
poetry run python3 bbtc.py {<device specifier> | --scan}
```

where `<device specifier>` can be:

- `--name <NAME>` - name advertised by the device
- `--mac <ADDRESS>` - physical address of the device's Bluetooth interface

Using the `--scan` option will scan for every TCAT device and display them in a list, to allow selection of the target.

For example:

```
poetry run python3 bbtc.py --name 'Thread BLE'
```

The application will connect to the first matching device discovered and set up a secure TLS channel. The user is then presented with the CLI.

## Usage with a specific TCAT Commissioner identity

The TCAT Commissioner's certificate specifies what permissions it has obtained for specific features of managing a TCAT Device. By default, the identity in the `auth` directory is used. In order to use a different TCAT Commissioner certificate (identity), use the `--cert_path` argument, as follows:

```bash
poetry run python3 bbtc.py --cert_path <certs-path> {<device specifier> | --scan}
```

where `<certs-path>` is the directory where the private key, certificate, and CA certificate(s) of the TCAT Commissioner are stored.

For example to use a pre-configured identity `CommCert2` (related to Thread certification tests):

```
poetry run python3 bbtc.py --cert_path ./auth-cert/CommCert2 --name 'Thread BLE'
```

The `auth-cert` directory contains some other identities too, for testing purposes. Refer to Thread TCAT test plan documents for details.

See [GENERATING_CERTIFICATES.md](GENERATING_CERTIFICATES.md) for details on generating own certificates.

## Usage with TCAT over CoAPS

> Note: TCAT over CoAPS is experimental.

A TCAT Device that is built with the `OT_TCAT_COAPS` option hosts TCAT over CoAPS (CoAP over DTLS over UDP) on its Thread radio interface, instead of over BLE. The TCAT Commissioner connects to the link-local IPv6 address of such a device, using UDP messages that are sent without link-layer security.

This requires a network interface to the Thread radio link on the computer that runs the TCAT Commissioner, for example the `wpan0` interface that is created by `ot-daemon` with an RCP. This interface must use the same channel and PAN ID as the TCAT Device and must allow link-layer unsecured communication on the TCAT UDP port (5684 by default), while Thread itself remains disabled:

```bash
sudo ot-ctl channel 11
sudo ot-ctl panid 0xface
sudo ot-ctl ifconfig up
sudo ot-ctl unsecureport add 5684
```

Then connect to the TCAT Device using its link-local address and the name of the network interface:

```bash
poetry run python3 bbtc.py --coaps fe80::c0eb:eca9:13b4:3221 --interface wpan0
```

Use `--port <PORT>` if the TCAT Device hosts TCAT on another UDP port. After the `disconnect` command, the `simulation 1` command connects to the same TCAT Device again. Each TCAT command and its response need to fit in a single CoAP message, and the TCAT Device cannot send unsolicited events to the TCAT Commissioner.

To decrypt a packet capture of the DTLS session, for example in Wireshark, set the `SSLKEYLOGFILE` environment variable to the name of a file. The TCAT Commissioner then appends the secrets of each DTLS session to this file.

To try this out without hardware, see the simulation test in [tools/tcat-coaps-sim-test](../tcat-coaps-sim-test/README.txt).

## TCAT Commissioner CLI Commands

The application supports the following interactive CLI commands:

- `help` - Display available commands.
- `commission` - Commission the device with current dataset.
- `thread start` - Enable Thread interface.
- `thread stop` - Disable Thread interface.
- `hello` - Send "hello world" application data and read the response.
- `exit` - Close the connection and exit.
- `dataset` - View and manipulate current dataset. Use `dataset help` for more information.
