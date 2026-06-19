# diver-firmware-community

Community-maintained firmware source for LZX Diver hardware (STM32F411-based),
with a set of fixes for long-standing artifacts and reliability issues reported
on the LZX community forums.

Fixes landed in this repo include:
- horizontal seam / ramp discontinuity (~3/4 across screen),
- right-edge flicker from ADC / phase-CV timing jitter,
- frame tearing caused by mid-session config-sector erase,
- boot jitter from a premature `EXTI15_10` enable,
- state not surviving power cycle (wear-leveled persistence in flash),
- vwave mirror-boundary missing sample.

See [ISSUES.md](ISSUES.md) for the full list of tracked issues, root-cause
notes, source-thread links, and per-issue fix status.

This repo is based on the open LZX source archive
(<https://github.com/lzxindustries/lzxsoftware>) and adds a standalone build
script plus a DFU flash flow.

## Clone

```bash
git clone https://github.com/homegrownVHS/diver-firmware-community.git
cd diver-firmware-community
```

## Flash a release (most users)

If you just want to put the firmware on your module, download the prebuilt
files from the GitHub Release page and follow **[FLASHING.md](FLASHING.md)**.
It walks through macOS, Linux, and Windows step by step. The short version:

```bash
dfu-util -d 0483:df11 -w -a 0 -D diver_firmware.dfu
```

then power-cycle the module. (A `diver_firmware_padded.bin` fallback flow is
documented in [FLASHING.md](FLASHING.md) for setups where the `.dfu` path
misbehaves.)

## Build from source (developers)

The build is driven by a standalone Python script — there is **no Makefile**:

```bash
python3 build.py
```

Requirements:
- `python3`
- An `arm-none-eabi` GCC toolchain and the STM32CubeF4 HAL/framework.
  [build.py](build.py) currently sources both from a PlatformIO install and
  has the package paths hardcoded near the top of the file (`PIO`, `TC`, `HAL`)
  for a Windows PlatformIO layout. **Edit those paths to match your machine**
  before building on macOS/Linux or a different PlatformIO location.
- `dfu-util` (for flashing).

The script compiles the firmware, links `diver.elf`, and writes the flashable
artifacts into `build/`:
- `diver_firmware.bin` — raw binary
- `diver_firmware_padded.bin` — padded to the DfuSe erase-block alignment
- `diver_firmware.dfu` — DfuSe wrapper (recommended for flashing)

It also prints SHA-256 hashes for each artifact and the linker size summary.
(`build/` and the generated binaries are gitignored.)

## Firmware source

The Diver firmware source lives here:
- [hardware/diver/main/Core/Src/main.cpp](hardware/diver/main/Core/Src/main.cpp)
- [hardware/diver/main/Core/Src/stm32f4xx_it.c](hardware/diver/main/Core/Src/stm32f4xx_it.c)
- Linker script: [hardware/diver/main/STM32F411RC_flash.lds](hardware/diver/main/STM32F411RC_flash.lds)

## Notes

The original LZX projects were developed in VisualGDB/Visual Studio. This
repository adds a script-driven build plus a documented DFU flash flow to make
local tinkering and contributions practical. Discussion and bug reports are on
the LZX community forum (threads linked in [ISSUES.md](ISSUES.md)).
