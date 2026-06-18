# diver-firmware-community

Community-maintained firmware source for LZX Diver hardware, including fixes for:
- vertical seam artifact in wrapped DMA output,
- right-edge flicker caused by ADC timing jitter.

This repo is based on the open LZX source archive and includes a practical local build + flash flow for macOS.

## Clone

```bash
git clone https://github.com/homegrownVHS/diver-firmware-community.git
cd diver-firmware-community
```

## Build

Requirements:
- `arm-none-eabi-gcc`
- `make`
- `python3`
- `dfu-util`

Build outputs are written to `build/`:

```bash
make all
```

## Flash (DFU)

Connect Diver in DFU mode, then run:

```bash
make flash
```

The known-good direct command (for troubleshooting) is:

```bash
dfu-util -d 0483:df11 -w -a 0 --dfuse-address 0x08000000:leave -D build/diver_firmware_padded.bin
```

## Firmware Location

The Diver firmware source is here:
- `hardware/diver/main/Core/Src/main.cpp`
- `hardware/diver/main/Core/Src/stm32f4xx_it.c`

## Notes

Original projects were developed in VisualGDB/Visual Studio. This repository adds a Makefile-driven flow to make local tinkering and contributions practical on modern setups.
