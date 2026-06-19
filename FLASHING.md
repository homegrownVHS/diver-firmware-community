# Diver Firmware Flashing Guide

This guide flashes the community firmware onto an LZX Diver module. It's
written for a non-developer audience — pick your platform section below.

## What you'll need

- A computer (macOS, Linux, or Windows 10/11)
- USB cable to the Diver (Type-B mini/micro depending on revision)
- The firmware files from the latest GitHub Release page:
  - `diver_firmware.dfu` *(recommended for most users — simplest)*
  - `diver_firmware_padded.bin` *(alternate, used by the troubleshooting
    fallback below)*

Both files are produced from the same source; either flashes the same
firmware. The `.dfu` format is preferred because it embeds the flash
address so the command line is shorter.

## Step 1 — Verify the downloads

The Release page lists a SHA-256 for each file. Confirm yours matches
before flashing — this protects against a corrupted download.

**macOS / Linux:**
```bash
shasum -a 256 diver_firmware.dfu
```

**Windows (PowerShell):**
```powershell
Get-FileHash diver_firmware.dfu -Algorithm SHA256
```

The hash in your terminal must match the Release page exactly.

## Step 2 — Install dfu-util

`dfu-util` is the standard tool for flashing STM32 chips over USB.

### macOS

```bash
brew install dfu-util
```

### Linux (Debian/Ubuntu/etc.)

```bash
sudo apt install dfu-util
```

### Windows

1. Download the dfu-util Windows build from one of:
   - PlatformIO installation (if you have it): the binary is at
     `%USERPROFILE%\.platformio\packages\tool-dfuutil\bin\dfu-util.exe`
   - Or download the official Windows binary from
     https://dfu-util.sourceforge.net/

2. Put `dfu-util.exe` somewhere on your `PATH`, or just call it by full
   path in the commands below.

3. **One-time WinUSB driver install for the STM32 bootloader.** Windows
   does not ship a default driver for the STM32 DFU mode device (USB ID
   `0483:DF11`), so `dfu-util` cannot open it until you install one. Use
   **Zadig** (a tiny utility that installs the generic WinUSB driver):

   1. Download `zadig-2.9.exe` (or newer) from
      https://zadig.akeo.ie/
   2. Put the Diver in DFU mode (see Step 3 below).
   3. Run Zadig as Administrator.
   4. In Zadig, you should see `STM32 BOOTLOADER` selected. The "Driver"
      dropdown should say `WinUSB`. If you don't see the device, use
      menu **Options → List All Devices**.
   5. Click **Install Driver** and wait ~10 seconds.
   6. Close Zadig. From this point on `dfu-util` will be able to talk
      to the Diver. This step is one-time per Windows machine.

## Step 3 — Put the Diver in DFU mode

Unplug the Diver's USB, then plug it back in. The Diver will enumerate
as USB device `0483:DF11` ("STM32 BOOTLOADER"). No LEDs light during DFU
mode — that's normal.

Confirm the OS sees it:

```bash
dfu-util -l
```

You should see a line that includes `0483:df11`. If you don't, unplug
and replug, or check the troubleshooting section below.

## Step 4 — Flash

From the folder containing `diver_firmware.dfu`:

```bash
dfu-util -d 0483:df11 -w -a 0 -D diver_firmware.dfu
```

You should see progress like:

```
Opening DFU capable USB device...
Device ID 0483:df11
...
Erase    [=========================] 100%
Download [=========================] 100%
File downloaded successfully
```

If you see `File downloaded successfully`, **unplug and replug the
Diver**. It will boot the new firmware.

## Step 5 — Verify (optional)

After re-plugging, the LED bargraph should light up and the module
should produce video output as normal. If you have a sync source
connected, the module should lock to it within ~1 second.

## Troubleshooting

### `LIBUSB_ERROR_NOT_SUPPORTED` on Windows

The WinUSB driver isn't installed for the bootloader. Re-run **Step 2,
sub-step 3** (Zadig) with the Diver in DFU mode.

### `Cannot open DFU device 0483:df11 found on devnum N (LIBUSB_ERROR_ACCESS)`

Another process is holding the device. Close any other software that
might have grabbed it (STM32CubeProgrammer, etc.), unplug/replug, and
retry.

### Flash fails mid-transfer

Unplug/replug USB, re-enter DFU mode, retry. Avoid USB hubs — connect
direct to the host port. Keep the cable short.

### Padded-BIN fallback flow

If the `.dfu` flow misbehaves on your setup, the padded `.bin` version
works the same way with one extra flag:

```bash
dfu-util -d 0483:df11 -w -a 0 --dfuse-address 0x08000000:leave -D diver_firmware_padded.bin
```

### Module doesn't boot after flash

Re-flash. If it persists, your DFU upload may have been incomplete. If
re-flashing also fails, post on the LZX community forum thread linked
in the README.
