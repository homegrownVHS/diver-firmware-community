# Diver Firmware Flashing Guide

This guide covers flashing prebuilt firmware from the GitHub release onto an LZX Diver.

## Files To Download

From the release page, download:
- diver_firmware_padded.bin
- diver_firmware.dfu

Use either format. On macOS/Linux with dfu-util, the padded .bin path below is the most field-tested flow.

## Requirements

- dfu-util
- USB connection to Diver in DFU mode

Check dfu-util install:

```bash
dfu-util --version
```

## Put Diver In DFU Mode

Connect the unit in DFU mode so it enumerates as USB device ID 0483:df11.

Optional check:

```bash
dfu-util -l
```

## Flash Using Padded BIN (Recommended)

From the folder containing diver_firmware_padded.bin:

```bash
dfu-util -d 0483:df11 -w -a 0 --dfuse-address 0x08000000:leave -D diver_firmware_padded.bin
```

## Flash Using DFU File (Alternative)

From the folder containing diver_firmware.dfu:

```bash
dfu-util -d 0483:df11 -w -D diver_firmware.dfu
```

## Verify Download Integrity

Expected SHA-256 for v0.1.0 padded binary:

f41acfa2514a79e493041d069375c4c93564907e8bad843e10d975aaaa4b58ce

Verify locally:

```bash
shasum -a 256 diver_firmware_padded.bin
```

## Troubleshooting

- If transfer fails intermittently, unplug/replug USB, re-enter DFU mode, and retry.
- Prefer the padded .bin command above when the standard .bin flow is unstable.
- Keep cable length short and avoid USB hubs if possible.