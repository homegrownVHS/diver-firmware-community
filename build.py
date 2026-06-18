#!/usr/bin/env python3
"""Standalone build script for diver firmware (no Makefile needed)."""
import os, sys, glob, subprocess, shutil, hashlib
from pathlib import Path

ROOT  = Path(__file__).parent.resolve()
PROJ  = ROOT / "hardware" / "diver" / "main"
BUILD = ROOT / "build"
PIO   = Path("C:/Users/User/.platformio/packages")
TC    = PIO / "toolchain-gccarmnoneeabi" / "bin"
HAL   = PIO / "framework-stm32cubef4" / "Drivers"

GCC      = str(TC / "arm-none-eabi-gcc.exe")
GPP      = str(TC / "arm-none-eabi-g++.exe")
OBJCOPY  = str(TC / "arm-none-eabi-objcopy.exe")
SIZE     = str(TC / "arm-none-eabi-size.exe")

MCU = ["-mcpu=cortex-m4", "-mthumb", "-mfloat-abi=hard", "-mfpu=fpv4-sp-d16"]
DEF = ["-DSTM32F411xE", "-DUSE_HAL_DRIVER"]
INC = [
    "-I" + str(PROJ / "Core" / "Inc"),
    "-I" + str(PROJ / "USB_DEVICE"),
    "-I" + str(HAL / "CMSIS" / "Device" / "ST" / "STM32F4xx" / "Include"),
    "-I" + str(HAL / "CMSIS" / "Include"),
    "-I" + str(HAL / "STM32F4xx_HAL_Driver" / "Inc"),
    "-I" + str(HAL / "STM32F4xx_HAL_Driver" / "Inc" / "Legacy"),
]
CFLAGS   = MCU + DEF + INC + ["-O1", "-g", "-ffunction-sections", "-fdata-sections", "-Wno-unused-but-set-variable", "-Wno-unused-variable", "-Wno-unused-function"]
CXXFLAGS = CFLAGS + ["-fno-exceptions", "-fno-rtti", "-std=gnu++11", "-fpermissive"]
LDFLAGS  = MCU + [
    "-T", str(PROJ / "STM32F411RC_flash.lds"),
    "-Wl,--gc-sections",
    "--specs=nano.specs", "--specs=nosys.specs",
]

def run(cmd, **kw):
    r = subprocess.run(cmd, **kw)
    if r.returncode != 0:
        print("FAILED:", " ".join(str(c) for c in cmd[:3]) + " ...")
        sys.exit(r.returncode)
    return r

def compile_one(src, obj, is_cpp, is_asm=False):
    obj.parent.mkdir(parents=True, exist_ok=True)
    if is_asm:
        cmd = [GCC] + MCU + ["-c", "-x", "assembler-with-cpp", str(src), "-o", str(obj)]
    elif is_cpp:
        cmd = [GPP] + CXXFLAGS + ["-c", str(src), "-o", str(obj)]
    else:
        cmd = [GCC] + CFLAGS + ["-c", str(src), "-o", str(obj)]
    print("  CC", src.relative_to(ROOT) if ROOT in src.parents else src.name)
    run(cmd)

def main():
    BUILD.mkdir(exist_ok=True)
    obj_dir = BUILD / "obj"
    if obj_dir.exists(): shutil.rmtree(obj_dir)
    obj_dir.mkdir()

    sources = []
    # project sources
    for p in (PROJ / "Core" / "Src").glob("*.c"):   sources.append((p, False, False))
    for p in (PROJ / "Core" / "Src").glob("*.cpp"): sources.append((p, True,  False))
    # HAL drivers (all of them; --gc-sections strips unused)
    for p in (HAL / "STM32F4xx_HAL_Driver" / "Src").glob("*.c"):
        if "template" in p.name.lower(): continue
        sources.append((p, False, False))
    # startup
    startup = HAL / "CMSIS" / "Device" / "ST" / "STM32F4xx" / "Source" / "Templates" / "gcc" / "startup_stm32f411xe.s"
    sources.append((startup, False, True))

    print(f"Compiling {len(sources)} sources...")
    objects = []
    for src, is_cpp, is_asm in sources:
        obj = obj_dir / (src.stem + ".o")
        compile_one(src, obj, is_cpp, is_asm)
        objects.append(str(obj))

    elf = BUILD / "diver.elf"
    print(f"Linking {elf.name}...")
    run([GPP] + LDFLAGS + objects + ["-o", str(elf), "-lm"])

    bin_out = BUILD / "diver_firmware.bin"
    print(f"objcopy -> {bin_out.name}...")
    run([OBJCOPY, "-O", "binary", str(elf), str(bin_out)])

    # Pad to nearest 2 KB to match DfuSe alt-0 erase block alignment
    raw = bin_out.read_bytes()
    pad_len = (len(raw) + 2047) & ~2047
    padded = raw + b"\xff" * (pad_len - len(raw))
    padded_path = BUILD / "diver_firmware_padded.bin"
    padded_path.write_bytes(padded)

    sha = hashlib.sha256(padded).hexdigest()
    print()
    run([SIZE, str(elf)])
    print()
    print(f"raw bin:     {len(raw):>6} bytes")
    print(f"padded bin:  {len(padded):>6} bytes  ({padded_path})")
    print(f"sha-256:     {sha}")

if __name__ == "__main__":
    main()
