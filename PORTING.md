# SNESticle PS2SDK Port — Complete Documentation

## Table of Contents

1. [Overview](#overview)
2. [Background](#background)
3. [Build Environment](#build-environment)
4. [Architecture & Design Decisions](#architecture--design-decisions)
5. [File-by-File Change Reference](#file-by-file-change-reference)
6. [New Files Created](#new-files-created)
7. [Embedded IOP Modules (Single-ELF Pattern)](#embedded-iop-modules-single-elf-pattern)
8. [IOP Reset Strategy](#iop-reset-strategy)
9. [Known Issues & TODO](#known-issues--todo)
10. [GCC 15 Uint128 Inline Assembly Issue](#gcc-15-uint128-inline-assembly-issue)
11. [Audio System — Full Investigation](#audio-system--full-investigation)
12. [How to Build](#how-to-build)
12. [How to Test (PCSX2)](#how-to-test-pcsx2)
13. [How to Create an ISO](#how-to-create-an-iso)
14. [Glossary](#glossary)

---

## Overview

This document describes the complete port of **SNESticle** (a Super Nintendo emulator for PlayStation 2, circa ~2004) from **Sony's proprietary ps2lib SDK** to the **open-source PS2SDK** ([ps2dev/ps2sdk](https://github.com/ps2dev/ps2sdk)).

The original SNESticle was written by Icer Addis using Sony's licensed development tools which are not publicly available. This port makes the emulator compilable with the free, community-maintained PS2 homebrew toolchain.

### What Works

- Boots and displays the SNESticle UI
- File browser (navigates `cdfs:`, `mass:`, `host:`, `mc0:`, `mc1:`)
- ROM loading from CD/DVD via `cdfs:` filesystem
- ROM loading from USB via `mass:` filesystem
- SNES emulation (CPU, PPU, SPC700)
- Controller input (DualShock 2)
- **Audio** ✅ (April 24, 2026) — SPC700 DSP audio output via `audsrv.irx` (PS2SDK). 12 bugs tracked and resolved across SJPCM2, audsrv, and GCC 15.2 optimization. Key fixes: (1) fixed per-frame generation bypassing broken `audsrv_queued()`, (2) `snspcmix.cpp` compiled with `-O1` to work around GCC 15.2 `-O2` corrupting inline MIPS assembly.
- **Memory card saves** ✅ (April 26, 2026) — Migrated from custom `MCSAVE.IRX` to native PS2SDK `libmc` + `fileio` calls.
- **Network** ✅ (April 26, 2026) — Migrated from custom `NETPLAY.IRX` IOP module to EE-native networking using PS2SDK `ps2ip` (lwIP). Server/client logic runs directly on the Emotion Engine via BSD sockets.

> **Milestone:** All 4 custom IOP modules (`SJPCM`, `MCSAVE`, `CDVD`, `NETPLAY`) have been fully replaced by PS2SDK equivalents. The build is now fully standalone and open-source.

### What Doesn't Work Yet

- **Graphics glitches**: Some PPU rendering bugs present (pre-existing or caused by type width changes)

---

## Background

### What is SNESticle?

SNESticle is a SNES (Super Nintendo Entertainment System) emulator for the PlayStation 2 console. It was developed around 2003-2004 by Icer Addis. The emulator consists of:

- **Gep/** — A generic emulation platform library (handles I/O, graphics, input, memory)
- **SNESticle/** — The SNES-specific emulation code (65816 CPU, SPC700 APU, PPU graphics)
- **NESticle/** — References to NES emulation code (not fully included in this repo)

The codebase targets multiple platforms: PS2, Dreamcast (dc), Windows (win32), and console (con32). This port only affects the PS2 target.

### Why Port to PS2SDK?

Sony's ps2lib SDK (also known as the "official" or "licensed" SDK) requires access to proprietary Sony libraries and headers that are not legally distributable. The PS2SDK (ps2dev project) is a fully open-source reimplementation of PS2 development tools, allowing anyone to compile and modify the code.

### Key Differences Between ps2lib and PS2SDK

| Feature | ps2lib (Original) | PS2SDK (Port) |
|---------|-------------------|---------------|
| `long` type size | 8 bytes (64-bit) | 4 bytes (32-bit, n32 ABI) |
| File I/O | `fioOpen/fioRead/fioWrite/fioClose` | POSIX `open/read/write/close` |
| Directory I/O | `fio_dirent_t`, `FIO_ATTR_SUBDIR` | `io_dirent_t`, `FIO_SO_IFDIR` |
| IOP modules | External `.IRX` files on disc | Embedded in ELF via `bin2c` |
| Module loading | `SifLoadModule(path)` | `SifExecModuleBuffer(data, size)` |
| CD/DVD access | Custom `CDVD.IRX` + `cdvd_rpc.h` | PS2SDK `cdvdman.irx` + `cdfs.irx` |
| USB storage | Not in original | PS2SDK `usbd` + `bdm` + `usbmass_bd` |
| Pad input | `padStatus.btns[0]<<8 \| btns[1]` | `padStatus.btns` (single u16) |
| Startup | Custom `crt0.o` | PS2SDK-provided startup |
| Compiler | GCC ~3.x (ps2dev era) | GCC 15.2.0 |
| `fileio.h` | Direct inclusion | Requires `#define NEWLIB_PORT_AWARE` |

---

## Build Environment

### Required Toolchain

```
PS2DEV=/usr/local/ps2dev
PS2SDK=/usr/local/ps2dev/ps2sdk

Compiler: mips64r5900el-ps2-elf-gcc (GCC) 15.2.0
Target:   MIPS R5900 (Emotion Engine), N32 ABI, Little-Endian
```

### Installing ps2dev Toolchain

```bash
# On Ubuntu/WSL2:
git clone https://github.com/ps2dev/ps2dev.git
cd ps2dev
./setup.sh   # This builds everything; takes 30-60 minutes

export PS2DEV=/usr/local/ps2dev
export PS2SDK=$PS2DEV/ps2sdk
export PATH=$PS2DEV/bin:$PS2DEV/ee/bin:$PS2DEV/iop/bin:$PS2DEV/dvp/bin:$PATH
```

---

## Architecture & Design Decisions

### 1. Single-ELF Homebrew Pattern

The PS2 homebrew standard is to produce a **single self-contained ELF** file. All IOP modules (`.IRX` files) are converted to C arrays using `bin2c` and linked directly into the ELF. At runtime, they are loaded into the IOP processor via `SifExecModuleBuffer()`.

This eliminates the need for external IRX files on disc, simplifying distribution.

### 2. Always-Reset IOP Strategy

The original code only reset the IOP when booted from memory card (`mc:`). When booted from CD/DVD, it relied on the BIOS-loaded modules. This doesn't work with PS2SDK because:

- The BIOS loads its own `cdvdman v1.01` and `cdvdfsv v1.01`
- PS2SDK's `cdvdman` cannot register its library entries when BIOS versions already exist
- Without PS2SDK's `cdvdman`, `cdfs.irx` cannot function

**Solution**: Always call `SifIopReset("", 0)` regardless of boot source. This clears all BIOS modules and gives us a clean IOP where embedded modules can register properly. This is the standard pattern used by virtually all PS2 homebrew.

### 3. POSIX File I/O Migration

The original code used Sony-specific `fioOpen/fioRead/fioWrite/fioClose` functions. PS2SDK's newlib provides POSIX-compliant `open/read/write/close` which are routed through the IOP's `ioman` driver. All file operations were migrated to POSIX equivalents.

### 4. Type Width Corrections

The most pervasive change. On the original ps2lib toolchain, `long` was 64 bits. On modern PS2SDK (GCC with n32 ABI), `long` is 32 bits. This broke:

- GS register definitions (`unsigned long int*` → `u64*`)
- GIF tag bitfield structures (`unsigned long` → `unsigned int` for 32-bit fields)
- 64-bit integer typedefs (`unsigned long int` → `unsigned long long`)
- Exception handler register dumps (`%016lX` → `%016llX`)

### 5. Stub Files for Missing NESticle Code

The SNESticle codebase references NESticle (NES emulator) types and functions that are not included in the repository. Minimal stub headers were created to satisfy the compiler without affecting SNES functionality.

---

## File-by-File Change Reference

### Gep (Platform Engine)

#### `Gep/Include/ps2/types.h`
**Problem**: `Uint64`/`Int64` defined as `unsigned long int`/`signed long int` — only 32 bits on PS2SDK.  
**Fix**: Changed to `unsigned long long`/`signed long long`.

#### `Gep/Include/ps2/gs.h`
**Problem**: GS privileged register pointers used `volatile unsigned long int*` (64-bit on ps2lib, 32-bit on PS2SDK). GIF tag bitfields used `unsigned long` expecting 32-bit fields but getting wrong sizes.  
**Fix**:
- Register pointers → `volatile u64*` for 64-bit GS registers, `volatile u32*` for 32-bit GIF registers
- GIF tag bitfields → `unsigned int` (explicitly 32-bit)

#### `Gep/Include/common/emusys.h`
**Problem**: Legacy code uses `CEmuSystem`, `EmuSysInputT` etc. as flat names, but modern headers put them in `Emu::` namespace.  
**Fix**: Added `typedef` aliases and `#define` macros after the namespace closing brace:
```cpp
typedef Emu::System CEmuSystem;
typedef Emu::SysInputT EmuSysInputT;
#define EMUSYS_STRING_SRAMEXT Emu::System::STRING_SRAMEXT
// ... etc
```

#### `Gep/Include/common/emurom.h`
**Fix**: Same pattern — added `typedef Emu::Rom CEmuRom;` and related aliases.

#### `Gep/Include/common/emumovie.h`
**Fix**: Added `typedef Emu::MovieClip CEmuMovieClip;`.

#### `Gep/Source/ps2/main.cpp`
**Major rewrite** of IOP initialization:
- Removed `#include <fileio.h>` → added `#include <unistd.h>`, `<fcntl.h>`, `<sbv_patches.h>`
- Removed `#include "cd.h"` (custom CDVD RPC)
- Removed `eeloadcnf`/`updateloader` variables (firmware image detection)
- `full_reset()`: Simplified to just `SifIopReset("", 0)` + SBV patches. Removed cdvdInit/cdvdExit calls.
- `main()`: Now **always** calls `full_reset()` instead of only on MC boot. Removed `cdvdInit(CDVD_INIT_NOWAIT)`.

#### `Gep/Source/ps2/file.cpp`
**Complete rewrite**: Replaced `fioOpen/fioRead/fioWrite/fioClose` with POSIX `open/read/write/close`. Removed dead `#if 0` code blocks.

#### `Gep/Source/ps2/excepHandler.c`
- `extern int _gp` → `extern void *_gp` (pointer type)
- `unsigned long uint64[2]` → `unsigned long long uint64[2]` (was only 32-bit)
- `__attribute((packed))` → `__attribute__((packed))` (GCC syntax fix)
- `%016lX` → `%016llX` (format specifier for 64-bit values)
- Added `#include <debug.h>` for `scr_printf`

#### `Gep/Source/ps2/console.cpp`
- Commented out `#include "msgnode.h"` (file not in repository, all usages already `#if 0`)

#### `Gep/Source/ps2/gpfifo.c`
- Added `#include <stdio.h>` (needed for `printf` on PS2SDK)

### SNESticle Application

#### `SNESticle/Source/ps2/mainloop.cpp`
**Largest change** — the heart of the port:
- Replaced `#include <fileio.h>` with `<unistd.h>`, `<fcntl.h>`, `<sys/stat.h>`
- Removed `#include "cdvd_rpc.h"` (custom CDVD module)
- Added `CDVD_FlushCache()` as static inline no-op (PS2SDK cdfs handles caching internally)
- Added `extern` declarations for 9 embedded IRX module arrays (`cdvdman_irx[]`, `cdvdfsv_irx[]`, `cdfs_irx[]`, `usbd_irx[]`, `bdm_irx[]`, `bdmfs_fatfs_irx[]`, `usbmass_bd_irx[]`, `audsrv_irx[]`, `mcsave_irx[]`)
- Added `IOPLoadEmbeddedModule()` function using `SifExecModuleBuffer()`
- Replaced all `fioOpen/fioRead/fioWrite/fioClose/fioLseek/fioMkdir` calls with POSIX equivalents
- Module loading sequence rewritten:
  - ROM modules (from BIOS): `XSIO2MAN`, `XMTAPMAN`, `XPADMAN`, `XMCMAN`, `XMCSERV`, `LIBSD`
  - Embedded PS2SDK modules: `cdvdman`, `cdvdfsv`, `cdfs`, `usbd`, `bdm`, `bdmfs_fatfs`, `usbmass_bd`
  - Audio module: `audsrv` (PS2SDK built-in, replaces custom SJPCM2)
  - Custom IOP modules: `MCSAVE` disabled (`#if 0`) due to ps2lib IOP incompatibility
- Removed old `CDVD.IRX` external loading and `CDVD_Init()` call
- Disabled `ps2ip_init()` call (API changed in PS2SDK)

#### `SNESticle/Source/ps2/uiBrowser.cpp`
- Replaced `fio_dirent_t` → `io_dirent_t`
- Added `#define NEWLIB_PORT_AWARE` before `#include "fileio.h"` (required by PS2SDK)
- Added `#include <iox_stat.h>` for `FIO_SO_IFDIR` definition
- Removed `#include "cdvd_rpc.h"`, added local `CDVD_FlushCache()` no-op
- Directory detection: Changed `dirent->stat.attr & FIO_ATTR_SUBDIR` to `(dirent->stat.mode & FIO_SO_IFDIR) || (dirent->stat.attr & FIO_SO_IFDIR)` — PS2SDK's `cdfs.irx` sets the directory flag in `stat.mode`, not `stat.attr`
- Drive list: Added `mass:` (USB) entry

#### `SNESticle/Source/ps2/input.cpp`
- `padStatus.btns[0] << 8 | padStatus.btns[1]` → `padStatus.btns` — PS2SDK's `libpad` provides `btns` as a single `u16` field, not a 2-byte array

#### `SNESticle/Source/ps2/memcard.cpp`
- Replaced `#include <fileio.h>` with `<unistd.h>`, `<fcntl.h>`, `<sys/stat.h>`
- Replaced all `fioOpen/fioRead/fioWrite/fioClose/fioMkdir` with POSIX equivalents
- Added `strcpy_sjis()` stub function (ASCII to SJIS 1:1 mapping for single-byte range)

#### `SNESticle/Source/ps2/uiNetwork.cpp`
- Removed unused `#include "fileio.h"`

#### `SNESticle/Source/common/snppurender8.cpp`
**Problem**: GCC 15 maps `Uint128` (`mode(TI)`) to a pair of 64-bit GPRs. Inline assembly using PS2 EE 128-bit instructions (`lq`, `qfsrv`, `pceqb`, etc.) only writes the low-half register. The C-level store generates two `sd` instructions (8 correct bytes + 8 garbage bytes), causing vertical stripes.  
**Fix**: Rewrote `_RenderBGData_O` (opaque) and `_RenderBGData` (non-opaque) inline asm blocks to use hardcoded registers (`$8`–`$12`) with explicit `sq` stores inside the asm. Removed all `Uint128` output operands from asm constraints. See [GCC 15 Uint128 Inline Assembly Issue](#gcc-15-uint128-inline-assembly-issue) for full details.

#### `SNESticle/Source/ps2/version.cpp`
- `return _Version_Info.ElfName` → `return (char *)_Version_Info.ElfName` (const-correctness cast)

#### `SNESticle/Modules/mcsave/ee/mcsave_ee.c`
- Added `#define NEWLIB_PORT_AWARE` before `#include <fileio.h>`
- `fio_dirent_t` → `io_dirent_t` in `MCSave_Dread()` and `SifWriteBackDCache()` calls

#### `SNESticle/Modules/mcsave/ee/mcsave_ee.h`
- Added `#include <io_common.h>` with `NEWLIB_PORT_AWARE` guard
- `fio_dirent_t` → `io_dirent_t` in function prototype

---

## New Files Created

### `SNESticle/Project/ps2/Makefile.ps2sdk`
Complete PS2SDK build system. Replaces the original Sony SDK Makefile. Key features:
- Uses `$(PS2SDK)/samples/Makefile.eeglobal_cpp` for build rules
- `-iquote` for project includes (avoids collision with PS2SDK's `font.h`)
- `bin2c` rules for converting IRX modules to C arrays
- DVP assembler rule for `.dsm` VU microcode files
- Links against: `-lpatches -lpad -lmc -laudsrv -lcdvd -ldebug -lstdc++ -lc -lm`

### `SNESticle/Source/ps2/version.h`
Version info struct declaration. The original was likely auto-generated or in a missing header.

### `SNESticle/Source/common/nes.h`
Stub: Defines `NesSystem` class (empty implementation extending `Emu::System`) and `NesMMU`. Required because `mainloop.cpp` references NES types for the multi-system menu.

### `SNESticle/Source/common/nesstate.h`
Stub: Defines `NesStateT` as a 64KB byte array. Used by the state save/restore code.

### `SNESticle/Source/common/ncpu_c.h`
Stub: Defines `NCpuT`, `N6502ExecuteFuncT`, and no-op inline functions. Referenced by NES CPU emulation code.

### `SNESticle/Source/common/nespal.h` / `nespal.cpp`
Stub: NES palette handling. Contains stock palette data (Chris Covell palette), empty composition functions. Required by the NES emulation references in the build.

---

## Embedded IOP Modules (Single-ELF Pattern)

The PS2 has two processors: the **EE** (Emotion Engine, main CPU) and the **IOP** (I/O Processor, handles devices). IOP modules (`.IRX` files) are loaded at runtime.

### How It Works

1. **Build time**: `bin2c` converts each `.irx` to a C file with a `unsigned char xxx_irx[]` array
2. **Compile time**: These C files are compiled and linked into the ELF
3. **Runtime**: `SifExecModuleBuffer(ptr, size, 0, NULL, &ret)` sends the module to the IOP

### Embedded Modules

| Module | Source | Purpose |
|--------|--------|---------|
| `cdvdman.irx` | PS2SDK | CD/DVD hardware driver (replaces BIOS version) |
| `cdvdfsv.irx` | PS2SDK | CD/DVD filesystem server (RPC bridge EE↔IOP) |
| `cdfs.irx` | PS2SDK | ISO 9660 filesystem driver (registers `cdfs:` device) |
| `usbd.irx` | PS2SDK | USB host controller driver |
| `bdm.irx` | PS2SDK | Block Device Manager (abstraction layer) |
| `bdmfs_fatfs.irx` | PS2SDK | FAT/exFAT filesystem for block devices |
| `usbmass_bd.irx` | PS2SDK | USB mass storage → BDM block device bridge |
| `audsrv.irx` | PS2SDK built-in | Audio streaming server — interleaved stereo PCM via SPU2 (replaces SJPCM2) |
| `mcsave.irx` | Custom (pre-compiled) | Memory card save helper (currently disabled) |

### Module Load Order (Runtime)

```
1. ROM modules (from BIOS flash):
   rom0:XSIO2MAN  → SIO2 bus manager
   rom0:XMTAPMAN  → Multitap manager
   rom0:XPADMAN   → Pad (controller) manager
   rom0:XMCMAN    → Memory card manager
   rom0:XMCSERV   → Memory card server
   rom0:LIBSD     → Sound library

2. Embedded PS2SDK modules:
   cdvdman        → CD/DVD hardware driver
   cdvdfsv        → CD/DVD filesystem server
   cdfs           → ISO 9660 filesystem (enables "cdfs:" device)
   usbd           → USB host controller
   bdm            → Block Device Manager
   bdmfs_fatfs    → FAT filesystem
   usbmass_bd     → USB mass storage driver

   audsrv          → PS2SDK built-in audio server (replaces custom SJPCM2)

3. Disabled (incompatible with PS2SDK IOP):
   mcsave         → Custom memcard (#if 0)
```

---

## IOP Reset Strategy

### The Problem

When the PS2 boots a disc, the BIOS loads its own IOP modules:
```
RegisterLibraryEntries: cdvdman version 1.01  ← BIOS
RegisterLibraryEntries: cdvdfsv version 1.01  ← BIOS
```

If we try to load PS2SDK's `cdvdman.irx` on top of this, it fails with error -200 because the library entries already exist. Without PS2SDK's `cdvdman`, `cdfs.irx` can't talk to the CD drive and the `cdfs:` device never registers:
```
Unknown device 'cdfs'
Known devices are  tty:(CONSOLE)  rom:(ROM/Flash)  cdrom:(CD-ROM)  mc:(Memory Card)
```

### The Solution

```c
// In main.cpp — always reset the IOP
int full_reset()
{
    SifExitIopHeap();
    SifLoadFileExit();
    SifExitRpc();

    SifIopReset("", 0);        // Reset IOP, clearing all BIOS modules
    while (!SifIopSync()) ;    // Wait for reset to complete

    SifInitRpc(0);             // Re-initialize EE↔IOP communication
    SifLoadFileInit();
    SifInitIopHeap();
    FlushCache(0);

    // SBV patches: allow loading modules from buffers and non-standard paths
    sbv_patch_enable_lmb();
    sbv_patch_disable_prefix_check();

    return 0;
}
```

After reset, the IOP is clean and our embedded modules can register successfully:
```
cdfs Filedriver v2.2
CDFS: Initializing 'cdfs' file driver.
USB Driver (Version 1.6.0)
Block Device Manager (BDM) v1.1
```

---

## Known Issues & TODO

### ~~Critical~~ — Audio ✅ FIXED (April 24, 2026)

**Two root causes identified and fixed:**

1. **Bug A2 — `audsrv_queued()` returns buffer capacity** → Fixed by using fixed per-frame generation (`m_uSampleRate / 60 ≈ 533 samples/frame`) instead of dynamic feedback.

2. **Bug A3 — GCC 15.2 `-O2` breaks SPC700 mixer** → Fixed by compiling `snspcmix.cpp` with `-O1` instead of `-O2` in `Makefile.ps2sdk`.

The `-O2` issue was discovered through systematic elimination: diagnostic `printf()` calls in `Mix()` made audio work, but removing them caused silence. Testing proved it was NOT compiler barriers (`__asm__ volatile` — failed), NOT DCache coherency (`FlushCache(0)` — failed), but specifically GCC 15.2's `-O2` optimization passes corrupting the 128-bit MIPS R5900 inline assembly in `_MixChannel`, `_MixChannelEcho`, and `_MixEcho`. Compiling with `-O1` restores correct behavior without any workarounds.

**Total bugs tracked: 12** (8 SJPCM + 3 audsrv + 1 GCC optimization).

---

#### audsrv Migration Summary (April 24, 2026)

After 8 tracked bug attempts with SJPCM2, none of which produced working audio on PCSX2, the audio backend was **completely replaced** with PS2SDK's built-in `audsrv.irx`.

**Files removed:**
- `Gep/Source/ps2/sjpcmbuffer.cpp` — old SJPCM mix buffer
- `Gep/Include/ps2/sjpcmbuffer.h` — old SJPCM mix buffer header
- `SNESticle/Modules/sjpcm/` — entire custom IOP module directory

**Files created:**
- `Gep/Source/ps2/audsrvbuffer.cpp` — new `AudsrvMixBuffer` class using `audsrv` API
- `Gep/Include/ps2/audsrvbuffer.h` — header for the new mix buffer

**Files modified:**
- `SNESticle/Source/ps2/mainloop.cpp` — replaced all SJPCM references with audsrv: `#include <audsrv.h>`, `audsrv_init()`, `audsrv_set_format()`, `AudsrvMixBuffer`
- `SNESticle/Project/ps2/Makefile.ps2sdk` — removed `sjpcm_rpc.o`, `sjpcmbuffer.o`, `sjpcm2_irx.o`; added `audsrvbuffer.o`, `audsrv_irx.o`, `-laudsrv`

**Buffer management strategy: Option 1 — Dynamic Feedback via `audsrv_queued()`**

Three options were evaluated:
1. **Dynamic Feedback (chosen)** — Uses `audsrv_queued()` to measure bytes in the IOP audio queue before each frame. Only generates the missing samples to fill the target buffer (4096 bytes). `audsrv_play_audio()` never blocks because data is only sent when there is queue space. Max 800 samples per frame to avoid SPC700 overwork.
2. **Fixed Chunking (rejected)** — Would generate exactly 800 samples/frame and let `audsrv_play_audio()` block as a natural framelimiter. Risk of hangs if PCSX2's IOP timing is off (same class of bugs as SJPCM).
3. **Large Async Buffer (rejected)** — Fire-and-forget with oversized IOP buffer. Unacceptable audio latency for real-time emulation.

---

#### audsrv Bug Log

**Bug A1 — Missing `audsrv_set_volume(MAX_VOLUME)` + Buffer overflow causing frame drops** ✅ FIXED (performance restored, audio still silent)

First audsrv build had two issues:

1. `audsrv` initializes with volume = 0. `audsrv_set_volume(MAX_VOLUME)` was never called, so even if audio data was being sent correctly, the SPU2 output was muted.

2. `GetOutputSamples()` had `nTarget = 3200 * 4 = 12800 bytes`. When `audsrv_queued()` returned 0 (first frame or empty IOP queue), the code requested 3200 stereo samples — **6× more** than a normal NTSC frame (~533 samples at 32kHz). This caused:
   - SPC700 DSP running 6× longer per frame → catastrophic frame drops
   - `audsrv_play_audio()` blocking when sending 12800 bytes to a ~4096-byte IOP ring buffer → additional frame stalls

Fix applied:
- Added `audsrv_set_volume(MAX_VOLUME)` after `audsrv_init()` and after each `audsrv_set_format()` call
- Reduced target buffer to 4096 bytes (matching IOP ring buffer capacity)
- Capped per-frame sample generation to 800 samples maximum (`AUDSRV_MAX_SAMPLES_PER_FRAME`)

Result: **Performance fully restored** (frame rate normal). **Audio still completely silent.**

| File | Change |
|------|--------|
| `Source/ps2/mainloop.cpp` | Added `audsrv_set_volume(MAX_VOLUME)` in two locations |
| `Gep/Source/ps2/audsrvbuffer.cpp` | `nTarget = 4096`, max 800 samples/frame |

**Bug A2 — `audsrv_queued()` returns buffer capacity, not queued bytes** ✅ FIXED — ROOT CAUSE OF ALL AUDIO SILENCE

With volume set to MAX and buffer sizing at 4096 bytes, audio was still silent. Diagnostic `printf` added to `SNSpcDspMixFull::Mix()` revealed:

```
[DIAG] frame=0 sizeof(Data)=10336 nTotalSamples=0 rate=32000 ch=2
[DIAG] MVOLL=127 MVOLR=127 EVOLL=0 EVOLR=0 FLG=0x20
```

**`nTotalSamples=0` on every frame** — the SPC700 DSP was never asked to generate any audio.

The cause: `audsrv_queued()` returns ≥ 4096 even when the IOP ring buffer is empty (likely returning the buffer capacity or an initialization value). The dynamic feedback formula `nSpace = 4096 - audsrv_queued()` always produced 0 or negative, capping to 0 samples.

This also explains why Bug A1's original target of 12800 bytes caused frame drops but not silence: `12800 - 4096 = 8704 → 2176 samples` — far too many per frame, but non-zero.

**Fix**: Replaced the `audsrv_queued()`-based dynamic feedback (Option 1) with **fixed per-frame generation**: `GetOutputSamples()` now returns `m_uSampleRate / 60` (≈ 533 samples at 32kHz/60fps). `audsrv_play_audio()` handles ring buffer management internally; if the buffer is full it blocks briefly, acting as a natural frame limiter.

| File | Change |
|------|--------|
| `Gep/Source/ps2/audsrvbuffer.cpp` | `GetOutputSamples()` returns `m_uSampleRate / 60` instead of queried value |

---

#### Root Cause Re-Analysis: Audio Silence is Backend-Independent

The following evidence conclusively proves the audio silence is upstream of the driver:

| Backend | Bugs Fixed | IOP Module OK? | Performance OK? | Audio? |
|---------|-----------|----------------|-----------------|--------|
| SJPCM2 (custom) | 8 (deadlocks, cache, DMA, semaphore) | Yes (TTY confirmed) | No → Yes (after sync fixes) | ❌ Silent |
| audsrv (PS2SDK) | 2 (volume, buffer sizing) | Yes (PS2SDK built-in) | No → Yes (after cap fix) | ❌ Silent |

**Both backends successfully receive data and send it to the IOP.** The problem must be in one of these upstream stages:

```
Stage 1: mainloop.cpp → pMixBuffer = _AudsrvMix (or _SJPCMMix)
Stage 2: _ExecuteSnes() → _pSystem->ExecuteFrame(pInput, pSurface, pMixBuffer)
Stage 3: snspcmix.cpp → SNSpcDspMixFull::Mix(pMixBuf)
  Stage 3a: pMixBuf->GetOutputSamples()         ← how many samples?
  Stage 3b: SPC700 DSP runs (envelope, BRR, mixing)
  Stage 3c: pMixBuf->OutputSamplesStereo(L, R, n) ← is data non-zero?
  Stage 3d: pMixBuf->Flush()                     ← does it send?
```

The silence could originate at any stage. Diagnostic prints are needed to identify which stage first produces zeros.

---

#### Deep Analysis: GCC 15.2 vs GCC 3.2 — Potential SPC700 Audio Breakage

The SNESticle audio pipeline was originally compiled with **GCC 3.2.2** (Sony ps2lib toolchain). The PS2SDK port uses **GCC 15.2.0** with the N32 ABI. We already discovered that GCC 15 breaks 128-bit inline assembly in the PPU renderer (the `Uint128` register pair issue documented above). **The SPC700 audio mixing code has the same class of PS2 EE 128-bit inline assembly**, and may have additional GCC 15 incompatibilities.

##### Hypothesis H1: 128-bit Inline Assembly in `_MixChannel` / `_MixChannelEcho` / `_MixEcho`

**Risk: HIGH.** The file `snspcmix.cpp` contains three PS2 EE inline assembly functions behind `#if SNSPCDSP_MIXASM` (which is `TRUE` on PS2):

1. **`_MixChannel()`** (line 668) — mixes a single voice into the main stereo buffer using `phmadh`, `pmulth`, `paddsw`, `sq`, `pcpyh`, `pcpyld`
2. **`_MixChannelEcho()`** (line 750) — same as above but also writes to the echo buffer, uses `ppach`, `paddsh`
3. **`_MixEcho()`** (line 992) — final mix of main + echo to output buffer using `pmulth`, `pmaddh`, `ppach`, `pminw`, `pmaxw`, `pcpyld`, `pcpyud`

All three functions use the same `\"+r\"` / `\"r\"` constraint pattern for `Int32` operands (like `iVolLeft`, `iVolRight`) with PS2-specific 128-bit multimedia instructions (`pcpyh`, `pcpyld`, `pmulth`, `phmadh`). The key difference from the PPU bug:

- In the PPU case, GCC 15 mapped `Uint128` to a **register pair**, breaking `lq`/`sq`. The fix used hardcoded registers.
- In the audio case, the operands are `Int32` — GCC 15 maps these to **single 32-bit GPRs**. But the PS2 EE multimedia instructions (`pcpyh`, `pmulth`, etc.) operate on the **full 128-bit width** of these registers. GCC 15 may not realize that the upper 96 bits of `iVolLeft`/`iVolRight` are significant after `pcpyh`/`pcpyld` broadcast them across the register.

**Specific concern in `_MixChannel` (line 670-677):**
```c
__asm__ (
    "pcpyh       %0,%0           \n"    // broadcast low 16 bits across 128-bit register
    "pcpyld      %0,%0,%0        \n"    // copy lower 64 bits to upper 64 bits
    "pcpyh       %1,%1           \n"
    "pcpyld      %1,%1,%1        \n"
    : "+r" (iVolLeft), "+r" (iVolRight)
);
```
This asm block broadcasts `iVolLeft` across all 8 halfwords of the 128-bit register. But GCC 15 thinks `iVolLeft` is just `Int32` (32 bits). After this asm block returns, GCC only preserves the low 32 bits of the register. In the **second** asm block (line 680), `%5` and `%6` reference `iVolLeft` and `iVolRight` — but GCC may have reloaded them from memory or a different register, losing the 128-bit broadcast.

**However**, examining more carefully: the first asm block uses `\"+r\"` (read-write) constraints, and the second block uses `\"r\"` (read-only) input constraints referencing the **same variables**. GCC should pass the same register. But under `-O2`, GCC 15 may spill the variable to the stack (as a 32-bit `Int32`) between the two asm blocks, then reload it — losing the upper 96 bits that `pcpyh`/`pcpyld` set up. GCC 3.2 may have kept the value in the register.

**If iVolLeft/iVolRight lose their 128-bit broadcast, all `pmulth` operations in the mixing loop produce wrong results** — potentially zero or near-zero, producing silence.

##### Hypothesis H2: `-fstrict-aliasing` Breaking SPC700 Memory Access

**Risk: MEDIUM.** The Makefile compiles with `-fstrict-aliasing` (enabled by `-O2`). The SPC700 code accesses the 64KB SPC RAM through different pointer types:
- `Uint8 *m_pMem` in `SNSpcDsp` (raw byte access)
- `Int16 *pBlockData` in `SNSpcDspMixFull::OutputSample()` (16-bit sample access)
- `Int16 *pEchoBuf` cast from `m_pMem + offset` in `FilterEcho()` (echo buffer access)

Under strict aliasing, GCC 15 is allowed to assume that `Int16*` and `Uint8*` don't alias. If the DSP writes voice data through `m_pMem` (byte pointer) and the mixer reads it through `pBlockData` (Int16 pointer), GCC 15 may cache the Int16 read in a register and never re-read from memory — seeing stale zeros instead of decoded BRR samples.

##### Hypothesis H3: `SNSpcDspDataT` Exceeds 16KB Scratchpad

**Risk: MEDIUM.** On PS2, `SNSPCDSP_INFOSCRATCHPAD` is `TRUE`, so `SNSpcDspMixFull::Mix()` places `SNSpcDspDataT` in the EE scratchpad (`PS2MEM_SCRATCHPAD = 0x70000000`, 16384 bytes). The struct contains:
```
Int16 iSampleData[544*2]    = 2176 bytes
Uint16 FracData[544]        = 1088 bytes
Uint8 EnvData[544]          =  544 bytes
Int32 Main[2][544]          = 4352 bytes
Int16 Echo[2][544]          = 2176 bytes
                    Total   ≈ 10336 bytes
```
This fits in 16KB. But `sizeof(SNSpcDspDataT)` may include padding that GCC 15 adds differently from GCC 3.2 (alignment requirements changed). Line 1315-1319 has an explicit check: if `sizeof(SNSpcDspDataT) > 16384` it prints the size and returns without mixing — **completely silently** (the `printf` output would only appear in PCSX2 TTY). This would cause total silence.

##### Hypothesis H4: Lookup Table Integer Overflow in `BuildLookupTables()`

**Risk: LOW-MEDIUM.** In `BuildLookupTables()` (line 116):
```c
uFactor = nSampleRate * 0x10000 / SNSPCDSP_SAMPLERATE;
m_AttackTicks[i] = _SNSpcDsp_AttackTimeMS[i] * (32 * uFactor / 64);
```
If `nSampleRate = 32000`: `uFactor = 32000 * 65536 / 32000 = 65536 = 0x10000`. Then `32 * uFactor = 32 * 65536 = 2097152`. For `_SNSpcDsp_AttackTimeMS[0] = 4100`: `4100 * (2097152 / 64) = 4100 * 32768 = 134,348,800`. This fits in `Uint32`.

But for decay/sustain: `_SNSpcDsp_SustainTimeMS[1] = 38000`: `38000 * 32768 = 1,245,184,000`. Still fits `Uint32` (max ~4.2 billion). No overflow here with `Uint32`, confirmed safe.

##### Hypothesis H5: Global Label Collision in Inline Assembly

**Risk: MEDIUM.** The inline assembly uses global labels like `_MixChannelPS2_Loop:`, `_MixChannelEchoPS2_Loop:`, `_MixEchoPS2_Loop:`. If `_MixChannel` is inlined by GCC 15 (or if Link-Time Optimization is used), having duplicate global labels would cause assembler errors or undefined branching. GCC 3.2 likely never inlined these functions. The functions are implicitly not `static`, but GCC 15 with `-O2` can still inline non-static functions within the same translation unit.

**Should use local labels** (e.g., `1:` with `b 1b`) instead of global labels, or mark the functions as `__attribute__((noinline))`.

##### Recommended Investigation Priority

1. ~~**Disable `SNSPCDSP_MIXASM`** (set to 0) — test H1~~ → **RULED OUT** (see below)

2. ~~**Add `-fno-strict-aliasing`** to the Makefile — test H2~~ → **RULED OUT** (see below)

3. ~~**Disable `SNSPCDSP_INFOSCRATCHPAD`** (set to FALSE) — test H3~~ → **RULED OUT** (see below)

4. ~~**Lookup table integer overflow in `BuildLookupTables()`** — test H4~~ → **ANALYTICALLY PROVEN SAFE** (see below)

5. **Add diagnostic `printf` in `SNSpcDspMixFull::Mix()`** to trace the exact point of silence in the pipeline.

##### Test Results Log

**Test H1 — Disable PS2 128-bit assembly mixer** ❌ RULED OUT (April 24, 2026)

Changed `SNSPCDSP_MIXASM` from `1` to `0` in `snspcmix.cpp` line 23. This disables all three PS2 EE inline assembly mixing functions (`_MixChannel`, `_MixChannelEcho`, `_MixEcho`) and uses the pure C fallback implementations (lines 852-963 and 1082-1115). Build succeeded (4.3MB ELF). Tested on PCSX2: audio still completely silent. Performance unchanged. **Conclusion: the 128-bit inline assembly in the mixer is NOT the cause of audio silence.** Change reverted.

**Test H2 — Disable strict aliasing** ❌ RULED OUT (April 24, 2026)

Changed `-fstrict-aliasing` to `-fno-strict-aliasing` in both `EE_CFLAGS` and `EE_CXXFLAGS` in `Makefile.ps2sdk`. Performed full rebuild (`rm -f *.o && make`) to ensure all object files were recompiled with the new flag. Build succeeded (4.2MB ELF). Tested on PCSX2: audio still completely silent. Performance unchanged. **Conclusion: GCC 15's strict aliasing optimizations are NOT causing stale reads in the SPC700 memory access paths.** Change reverted.

**Test H3 — Disable EE Scratchpad for mixing data** ❌ RULED OUT (April 24, 2026)

Changed `SNSPCDSP_INFOSCRATCHPAD` from `TRUE` to `FALSE` in `snspcmix.cpp` line 19. This forces `SNSpcDspDataT` (~10KB struct containing sample data, envelope, main/echo buffers) to be allocated on the stack instead of the EE scratchpad (`0x70000000`, 16KB). Tests whether the struct exceeds 16KB under GCC 15's alignment rules (which would trigger the silent `return` guard at line 1315-1319) or whether scratchpad memory access is broken. Build succeeded (4.2MB ELF). Tested on PCSX2: audio still completely silent. Performance unchanged. **Conclusion: the scratchpad is NOT the cause — the struct fits in 16KB and stack allocation produces the same silence.** Change reverted.

**Test H4 — Lookup table integer overflow** ✅ ANALYTICALLY PROVEN SAFE (April 24, 2026)

Mathematical analysis of all multiplications in `BuildLookupTables()` at `nSampleRate = 32000`:
- `uFactor = 32000 * 0x10000 / 32000 = 65536`
- `32 * uFactor / 64 = 32768`
- Largest attack: `4100 * 32768 = 134,348,800` → fits Uint32
- Largest sustain: `38000 * 32768 = 1,245,184,000` → fits Uint32 (max ~4.2 billion)
- All bent line values smaller. **No overflow possible with Uint32 at any sample rate ≤ 48kHz.** No code change needed.

**Test H5 — Compiler memory barriers (`__asm__ volatile`)** ❌ RULED OUT (April 24, 2026)

Replaced all diagnostic `printf()` in `Mix()` with `__asm__ __volatile__("" ::: "memory")` compiler barriers (generates zero machine code, forces register→DCache commit). Audio still silent. **Conclusion: GCC is NOT keeping mixing data in registers. The issue is not a compiler reordering problem.**

**Test H6 — DCache writeback (`FlushCache(0)`)** ❌ RULED OUT (April 24, 2026)

Added `FlushCache(0)` (writeback entire DCache to RAM) before `audsrv_play_audio()` in `Flush()`. This ensures SIF DMA reads real audio data from physical RAM. Audio still silent. **Conclusion: the DMA is reading correct data from RAM. The issue is not a cache coherency problem.**

**Test H7 — Compile `snspcmix.cpp` with `-O1`** ✅ FIXED (April 24, 2026)

Compiled ONLY `snspcmix.o` with `-O1` instead of `-O2` (all other files remain `-O2`). Audio works perfectly. **Root cause: GCC 15.2's `-O2` optimization passes corrupt the 128-bit MIPS R5900 inline assembly** in `_MixChannel`, `_MixChannelEcho`, and `_MixEcho`. The specific `-O2` pass responsible has not been identified (candidates: `-fschedule-insns2`, `-fcaller-saves`, `-fpeephole2`), but `-O1` is the definitive fix.

A custom Makefile rule was added to ensure `snspcmix.o` is always compiled with `-O1`:
```makefile
snspcmix.o: ../../Source/common/snspcmix.cpp
	$(EE_CXX) $(EE_CXXFLAGS) $(EE_INCS) -O1 -c $< -o $@
```

| File | Change |
|------|--------|
| `SNESticle/Project/ps2/Makefile.ps2sdk` | Per-file `-O1` override rule for `snspcmix.o` |



**Bug 1 — Missing `.iopmod` section in `sjpcm_new.irx`** ✅ CONFIRMED FIXED: The original IOP module was compiled with ps2lib, which generates a standard ELF executable. PS2SDK's IOP loader (`SifExecModuleBuffer`) requires a `.iopmod` section (type `0xff80`) that marks the file as a relocatable IOP module. Without it, module loading silently fails or hangs, and `SifBindRpc` never completes — the EE spins forever waiting for the IOP RPC endpoint to appear. After fix: IOP module loads correctly and prints TTY messages (`SjPCM v2.1 - by Sjeep`, `SjPCM: libsd initialised!`, `SjPCM: Entering playing thread.`).

**Bug 2 — SjPCM async path deadlock on PCSX2** ✅ CODE FIXED, audio still silent: After recompiling `sjpcm2.irx` correctly, audio was still silent with severe performance degradation. The original code used SjPCM's async buffer-query path (`SjPCM_BufferedAsyncStart` / `SjPCM_BufferedAsyncGet`), which relies on a SIF interrupt-driven callback (`_SjPCM_BufferedIntr`) to signal a semaphore. On PCSX2, this SIF interrupt does not fire reliably for async RPCs — `iSignalSema` is never called, `WaitSema` blocks forever, and `SjPCM_Init()` never returns. Additionally, `SjPCM_Init()` itself called `SjPCM_BufferedAsyncStart()` + `SjPCM_BufferedAsyncGet()` at the end, so the deadlock happened before emulation even started. Fix: switched entirely to the synchronous path — `SjPCM_Buffered()` (blocking RPC, no callback, no semaphore) and `SjPCM_Enqueue()` instead of `SjPCM_EnqueueAsync()`.

**Bug 3 — `SjPCM_Enqueue(wait=1)` SifDmaStat spin loop** ✅ CODE FIXED, audio still silent: The sync `SjPCM_Enqueue()` had a `while ((wait != 0) && (SifDmaStat(i) >= 0))` spin loop waiting for EE→IOP DMA completion before sending the RPC. On PCSX2, `SifDmaStat` may never return negative (DMA always appears in-progress) — the EE spins forever, the `SifCallRpc(SJPCM_ENQUEUE)` is never sent, `writetotal` on the IOP never advances, the play thread has no data, SPU2 outputs silence. The spin also explains the terrible performance. Fix: `SjPCM_Enqueue(..., 0)` (wait=0) in `sjpcmbuffer.cpp` to skip the spin entirely. Also added `SifWriteBackDCache(sbuff, 64*4)` before both sync RPCs (`SjPCM_Buffered()` and `SjPCM_Enqueue()`) to ensure the EE reads the IOP's response from RAM rather than stale cache.

**Bug 4 — `FlushCache(0)` does not invalidate cache: wrong `pcmbufl`/`pcmbufr` addresses** ✅ CODE FIXED, audio still silent: `FlushCache(0)` = constant `WRITEBACK_DCACHE` (value 0, defined in `kernel.h`) — it writes dirty cache lines to RAM but does **not** invalidate. After `SifCallRpc(SJPCM_INIT)`, the IOP writes the real `pcmbufl`/`pcmbufr` IOP memory addresses to EE RAM via SIF DMA. Because the DMA bypasses the EE cache and the post-RPC `FlushCache(0)` did not invalidate, the EE read stale cache: `sbuff[1] = numsamples = 24000` (0x5DC0) and `sbuff[2] = maxenqueuesamples = 4000` (0x0FA0) instead of the real buffer addresses. Every subsequent `SifSetDma` in `SjPCM_Enqueue()` sent audio data to IOP address 0x5DC0/0x0FA0 — not the actual SPU2 ring buffers — so the IOP play thread never received audio data. Additionally, the two `FlushCache(0)` calls in `SjPCM_Enqueue()` (one per stereo channel) flushed the entire 16 KB DCache via a kernel syscall every frame — expensive on PCSX2 where syscalls require JIT synchronization. Fix: replaced all `FlushCache(0)` in `SjPCM_Init()` with `SifWriteBackDCache(sbuff, 64)` (performs write-back AND invalidate on the specific buffer range). Replaced `FlushCache(0)` in both `SjPCM_Enqueue()` variants with `SifWriteBackDCache(left/right, size*2)` (targeted ~1 KB flush using CACHE instructions directly, no syscall).

**Files changed (cumulative)**:
- `SNESticle/Modules/sjpcm/iop/Makefile` — rewritten for PS2SDK IOP toolchain with 3-step build
- `SNESticle/Modules/sjpcm/ee/sjpcm_rpc.c` — removed async calls from `SjPCM_Init()`; replaced all `FlushCache(0)` with `SifWriteBackDCache` in init and both enqueue paths; added `SifWriteBackDCache(sbuff, 64*4)` before sync RPCs
- `SNESticle/Source/ps2/mainloop.cpp` — `new SJPCMMixBuffer(32000, FALSE)` (sync mode); removed per-frame `SjPCM_BufferedAsyncStart()` call
- `Gep/Source/ps2/sjpcmbuffer.cpp` — `SjPCM_Enqueue(..., 0)` (wait=0)
- `SNESticle/Project/ps2/Makefile.ps2sdk` — restored sjpcm_rpc.o/sjpcmbuffer.o, added sjpcm2_irx.o; `EE_DBGINFOFLAGS =` (empty) to strip debug info and reduce ELF size
- `SNESticle/Source/common/snspcmix.h` — fixed `SNSPCDSP_MAXSAMPLES` (was 68, should be 544)

### ~~Critical~~ — Memory Card Saves ✅ FIXED (April 24, 2026)

**MCSAVE.IRX** had a ps2lib IOP incompatibility. This module was completely stripped out.

**Solution implemented**:
Rewrote `mcsave_ee.c` and `memcard.cpp` to use PS2SDK's `libmc` directly. The ROM BIOS `XMCMAN`/`XMCSERV` modules are already natively handled. The application code now cleanly intercepts `mc0:/` or `mc1:/` paths and leverages native `mcMkDir()`, `mcOpen()`, `mcWrite()`, `mcRead()`, and `mcClose()` along with synchronization via `mcSync()`. Fallbacks accurately use `mkdir()`, `open()`, and `write()` for USB/Host saves. This completely bypasses the PS2 ROM's notoriously buggy `fio` implementation for memory cards, preventing save corruption or failed directory/icon creation. `mcsave_irx` was entirely removed from the `Makefile.ps2sdk` build chain.

**Solutions**:
1. **Use PS2SDK's `mcserv` directly** — Done. All saves (`.srm`) and icon metadata (`icon.sys`) use `libmc`.

### ~~Medium — Graphics Glitches~~ ✅ RESOLVED

**Symptom**: Vertical stripes (alternating correct/garbage 8-pixel columns) visible on all background layers during emulation.

**Root Cause**: GCC 15's N32 ABI maps `Uint128` (`unsigned int __attribute__((mode(TI)))`) to a **pair of 64-bit GPRs** (e.g., `$8`/`$9`). The PS2-specific inline assembly in the PPU renderer uses 128-bit EE instructions (`lq`, `sq`, `qfsrv`, `pceqb`, etc.) that operate on **single 128-bit registers**. When the `"=r"` output constraint was used for a `Uint128` variable, GCC allocated a register pair but the inline asm only wrote the low-half register. The subsequent C-level store `((Uint128 *)ptr)[0] = var` generated two `sd` (store-doubleword) instructions — 8 correct bytes + 8 garbage bytes — producing the 8-pixel-wide vertical stripes.

**Fix**: Rewrote all affected inline asm blocks in `snppurender8.cpp` (`_RenderBGData_O` and `_RenderBGData`) to use hardcoded register numbers (`$8`–`$12`) with explicit `sq` (store-quadword) instructions inside the asm, bypassing GCC's `Uint128` register allocation entirely. See [GCC 15 Uint128 Inline Assembly Issue](#gcc-15-uint128-inline-assembly-issue) for full technical details.

### ~~Medium — Black Backgrounds on Title Screens~~ ✅ FIXED (April 25, 2026)

**Symptom**: On Super Mario World's title screen, the gameplay demo playing inside the rectangular viewport renders as completely black. The decorative border frame (BG3) and sprites (text "SUPER MARIO WORLD") render correctly, but BG1+BG2 in the center are invisible.

**Root Cause**: A strict aliasing/memory barrier issue in `snmaskop.h` and `snmask128.cpp`. These files implement 128-bit mask operations (`SNMaskAND`, `SNMaskOR`, etc.) used extensively for window clipping (`DecodeWindows`). The inline assembly blocks utilized `sq` (store-quadword) to write back to memory, but **lacked the `"memory"` clobber constraint**. Without this, GCC 15.2 at `-O2` assumed the inline assembly had no side effects on memory, aggressively optimizing away the mask stores and passing stale or uninitialized mask data to the renderer.

**Fix**: Appended `"memory"` to the clobber list of every inline assembly block in `snmaskop.h` and `snmask128.cpp`. This forces GCC to serialize memory access around the inline assembly and prevents it from discarding the 128-bit stores. `snppurender8.cpp` can safely be compiled at `-O2` again.

### Low — Network

- **Network Modules**: The obsolete `PS2IPS`, `PS2IP`, and `PS2SMAP` modules were removed. The network initialization was rewritten using modern PS2SDK equivalents (`ps2dev9.irx`, `netman.irx`, `smap.irx`, `ps2ip.irx`) via lwIP. These modules are now statically embedded within the ELF file, making the emulator fully standalone without requiring external network modules on disk.

### Low — Linker Warnings

```
warning: linking abicalls files with non-abicalls files
warning: SNESticle.elf uses -msingle-float, hw.o uses -mdouble-float
```

`hw.o` is compiled from C (not C++) and gets different default flags. These warnings are harmless but could be cleaned up by adding explicit flags to the C compilation rule.

---

## GCC 15 Uint128 Inline Assembly Issue

This section documents the root cause and fix for the **vertical stripe rendering artifacts** that appeared when compiling SNESticle's PPU renderer with GCC 15.2.0 (ps2dev community toolchain). This was the hardest bug in the entire port — it only manifests at runtime, produces no compiler warnings, and the generated assembly *looks* correct at first glance.

### Symptom

During emulation, all background layers displayed **alternating vertical columns of correct pixels and garbage/zero pixels**, each column exactly 8 pixels wide. The pattern was consistent and reproducible across all games.

### Diagnostic Process

A binary-search isolation strategy was used:

1. **`-O0` test**: Stripes persisted → not a compiler optimization bug
2. **Zeroing `attrib8` buffer**: Stripes persisted → attribute data not the cause
3. **Uniform `main8` buffer** (memset to 0x42): Solid color on screen → GS texture upload pipeline is correct
4. **Gradient `main8` buffer**: Clean gradient bands → GS pipeline fully functional
5. **Conclusion**: The bug is in `RenderLine8()` → specifically in `_RenderBGData_O` and `_RenderBGData` in `snppurender8.cpp`, which write to the `main8` buffer using PS2 EE 128-bit inline assembly

### Root Cause

The PS2's Emotion Engine (EE) has 128-bit general-purpose registers. The original code used `Uint128` (defined as `unsigned int __attribute__((mode(TI)))`) to represent 128-bit values and passed them between C code and inline assembly via GCC's `"=r"` output constraints.

**On GCC 3.2.2 (original ps2lib toolchain)**: `Uint128` was mapped to a single 128-bit register. The inline assembly and C store worked correctly.

**On GCC 15.2.0 (modern ps2dev, N32 ABI)**: `Uint128` is mapped to a **pair of 64-bit GPRs** (e.g., `$8` + `$9`). This is because the N32 ABI treats 128-bit integers as two 64-bit halves.

The original code pattern was:

```cpp
Uint128 uSrc0;

// Load and shift 128-bit data
__asm__ __volatile__ (
    "lq        %0,0x00(%2)     \n"  // load 128-bit quadword into %0
    "lq        %1,0x10(%2)     \n"  // load next quadword
    "qfsrv     %0,%1,%0        \n"  // funnel shift right (scroll)
    : "=r" (uSrc0), "=r" (uSrc1)
    : "r" (pSrc8)
);

// Store result to scanline buffer
((Uint128 *)pLine8)[0] = uSrc0;
```

With GCC 15, the `"=r"(uSrc0)` constraint allocates registers `$8`/`$9` (a pair). The `lq` instruction loads 128 bits into `$8` alone (it's a single-register 128-bit instruction). Register `$9` is **never written** by the asm block. When GCC generates the C-level store, it emits:

```asm
sd  $8, 0($4)    # store low 8 bytes  — CORRECT (from lq/qfsrv)
sd  $9, 8($4)    # store high 8 bytes — GARBAGE (uninitialized $9)
```

This produces exactly 8 correct bytes + 8 garbage bytes per 16-byte quadword = **8-pixel-wide vertical stripes**.

### The Fix

All affected inline asm blocks were rewritten to:

1. **Use hardcoded register numbers** (`$8`–`$12`) instead of GCC-allocated `Uint128` operands
2. **Perform the `sq` (store-quadword) inside the asm block** instead of relying on a C-level store
3. **Declare clobbers properly** (`"$8", "$9", "memory"` etc.)

#### Before (broken with GCC 15):

```cpp
Uint128 uSrc0, uSrc1;

__asm__ __volatile__ (
    "lq   %0, 0x00(%2)\n"
    "lq   %1, 0x10(%2)\n"
    "qfsrv %0, %1, %0 \n"
    : "=r"(uSrc0), "=r"(uSrc1) : "r"(pSrc8)
);
((Uint128 *)pLine8)[0] = uSrc0;   // GCC emits sd+sd → BROKEN
```

#### After (correct):

```cpp
__asm__ __volatile__ (
    "lq   $8, 0x00(%0)\n"
    "lq   $9, 0x10(%0)\n"
    "qfsrv $8, $9, $8 \n"
    "sq   $8, 0x00(%1)\n"         // explicit 128-bit store inside asm
    : : "r"(pSrc8), "r"(pLine8)
    : "$8", "$9", "memory"
);
```

The same pattern was applied to both the **opaque** (`_RenderBGData_O`) and **non-opaque** (`_RenderBGData`) rendering paths, including the masked variants that use additional PS2 EE multimedia instructions (`pcpyld`, `pceqb`, `por`, `pxor`, `pand`).

### Affected Functions

| Function | File | Description |
|----------|------|-------------|
| `_RenderBGData_O` | `SNESticle/Source/common/snppurender8.cpp` | Opaque BG tile renderer (writes pixels, ignoring existing data) |
| `_RenderBGData` | `SNESticle/Source/common/snppurender8.cpp` | Non-opaque BG tile renderer (merges pixels with transparency mask) |

### Verification

The fix was verified by compiling to assembly (`-S` flag) and confirming:
- All 128-bit stores use `sq` (store-quadword) instructions inside the asm blocks
- No `sd` (store-doubleword) pairs are generated for the rendering output
- Hardcoded registers appear in clobber lists, preventing GCC from using them for other purposes

### Lessons Learned

1. **GCC's `mode(TI)` on MIPS N32 does NOT give you a single 128-bit register** — it gives you a pair of 64-bit registers. This is a fundamental ABI difference from the old ps2lib toolchain.
2. **Never use `"=r"` constraints for `Uint128` with PS2 EE 128-bit instructions** — the register pair semantics will silently corrupt data.
3. **Always perform 128-bit stores (`sq`) inside the asm block** when targeting the PS2 EE with modern GCC. Do not rely on C-level stores of `Uint128` variables.
4. **The symptom (8-byte stripes) directly maps to the cause** — 8 bytes = one half of a 16-byte quadword, exactly the boundary between the correct register and the garbage register in the pair.
5. **Other `Uint128` inline asm sites may need the same fix** — any code using `lq`/`sq`/`qfsrv`/`pceqb`/`pcpyld` with `Uint128` operands is potentially affected. Audit all PS2 EE inline asm for this pattern.

---

## Audio System — Full Investigation

This section documents every hypothesis, dead end, and confirmed root cause encountered while tracking down total audio silence in the PS2SDK port. The problem turned out to be two independent bugs stacked on top of each other.

### Background: How SNESticle Audio Works

The PS2 has no PCM audio mixing in hardware — all audio is produced on the EE CPU and streamed to the SPU2 (Sound Processing Unit) via DMA. SNESticle uses **SjPCM** ("Sjeep PCM", by Nick Van Veen, 2002) as its audio streaming backend. SjPCM is split across two processors:

- **IOP side** (`sjpcm2.irx`): An IOP module that receives PCM samples from the EE via SIF DMA, then streams them to SPU2 using `SdBlockTrans` (libsd). A callback fires on every SPU2 DMA completion (every 512 bytes / 256 stereo samples at 48 kHz), which wakes the IOP play thread to copy the next chunk.
- **EE side** (`sjpcm_rpc.c`): An RPC client that sends audio data and queries to the IOP module. Per-frame flow:
  1. `SjPCM_BufferedAsyncStart()` — fires async RPC query to IOP asking how many samples are buffered
  2. SNES frame is executed (CPU/PPU/SPC700 emulation)
  3. `SjPCM_BufferedAsyncGet()` — waits for the IOP response, returns buffered sample count
  4. `GetOutputSamples()` calculates how many samples to generate (target: keep IOP buffer ≥ 3200 samples)
  5. SPC700 DSP mixes audio → 32 kHz PCM → upsampled 2:3 to 48 kHz
  6. `SjPCM_EnqueueAsync()` — fires async SIF DMA transfer + RPC to IOP, returns immediately

### What Was Tried First: audsrv (now the current backend)

Before settling on SJPCM2, the port initially attempted to use **`audsrv.irx`** — the PS2SDK built-in audio streaming module. `audsrv` provides a simpler API: `audsrv_init()`, `audsrv_set_format()`, `audsrv_play_audio()`. The initial attempt was abandoned because:

- `audsrv` requires audio data in a specific interleaved stereo format, while SNESticle's mixer produces separate left/right PCM buffers
- `audsrv` uses a blocking streaming model ill-suited to per-frame async audio
- The original codebase already had full `SjPCM` integration in `sjpcmbuffer.cpp` and `sjpcm_rpc.c` with the correct per-frame async pattern

**Resolution (April 24, 2026):** After 8 bugs tracked with SJPCM2 and no working audio, **audsrv was successfully re-adopted** as the audio backend. The original objections were resolved as follows:

1. **Interleaved stereo** — Solved by creating `AudsrvMixBuffer` class (`audsrvbuffer.cpp`) which implements the `CMixBuffer` interface. The `OutputSamplesStereo()` method interleaves L/R buffers into a single LRLRLR buffer that `audsrv_play_audio()` expects.
2. **Blocking model** — Solved by using `audsrv_queued()` to query IOP buffer occupancy before each frame (Dynamic Feedback / Option 1). Audio is only generated when there is queue space, so `audsrv_play_audio()` never blocks. Per-frame generation is capped to 800 samples max.
3. **Volume** — `audsrv` starts with volume 0; `audsrv_set_volume(MAX_VOLUME)` must be called after init.
4. **Buffer sizing** — The IOP ring buffer is ~4096 bytes. Target must not exceed this to avoid `audsrv_play_audio()` blocking.

SJPCM2 was completely removed (files deleted, Makefile cleaned). audsrv is now the only audio backend. Audio silence persists, but the root cause is now proven to be **upstream of the audio backend** (see [Root Cause Re-Analysis](#root-cause-re-analysis-audio-silence-is-backend-independent)).

### Bug 1: `sjpcm_new.irx` Missing the `.iopmod` Section

#### Symptom
Total silence. The EE startup sequence appeared to complete (no error messages from `IOPLoadEmbeddedModule`), but `SjPCM_Init()` never returned.

#### Investigation
`SjPCM_Init()` calls `SifBindRpc(&cd0, SJPCM_IRX, 0)` in a loop, waiting for `cd0.server != NULL`. On the IOP side, `SjPCM_Thread()` should register the RPC endpoint via `sceSifRegisterRpc`. If the module never starts, `cd0.server` stays NULL forever and the EE loops indefinitely — appearing as a hang or freeze with no output.

Inspecting `sjpcm_new.irx` with `readelf`:
```
$ mipsel-none-elf-readelf -S sjpcm_new.irx | grep iopmod
(no output)
```

The `.iopmod` section was entirely absent. This section is how PS2SDK's IOP loader (`loadcore`) identifies a file as a loadable module and finds its entry point. Without it, `SifExecModuleBuffer` returns success (the ELF is valid), but `loadcore` silently discards the image and never calls `_start`.

The original `sjpcm_new.irx` was compiled with ps2lib circa 2002, which linked IOP modules as standard MIPS executables with `-Wl,-s`. That format worked with ps2lib's proprietary IOP loader but is incompatible with PS2SDK's open-source `loadcore`.

#### Fix: Recompile with PS2SDK IOP Toolchain + `srxfixup`

A new `Makefile` was written for `SNESticle/Modules/sjpcm/iop/` using the PS2SDK IOP toolchain (`mipsel-none-elf-gcc`, GCC 15.2.0 targeting MIPS-I R3000A). The build process has three explicit steps:

**Step 1 — Compile** (`-march=mips1 -msoft-float -mno-explicit-relocs -G0 -EL`):
- `-msoft-float`: IOP has no FPU; GCC must not emit FP instructions
- `-mno-explicit-relocs`: Required for GCC ≥ 5.3 targeting MIPS-I with `-G0`
- `-fno-builtin`: Prevents GCC from silently replacing `printf("str\n")` with `puts()` — IOP's `stdio` module exports `printf` but not `puts`
- `-fno-toplevel-reorder`: Preserves order of import/export table entries (required for IOP module ABI)

**Step 2 — Link as relocatable** (`-nostdlib -dc -r`):
- `-dc -r`: Produces a relocatable ELF (not a fully linked executable)
- Link against `-lkernel` (PS2SDK IOP kernel stubs for `CreateThread`, `WaitSema`, etc.)
- A `linkfile` from PS2SDK specifies the section layout expected by `loadcore`

**Step 3 — `srxfixup --rb --irx1`**:
- `srxfixup` is a PS2SDK utility that post-processes the relocatable ELF, injects the `.iopmod` header section (type `0xff80`), and produces a proper `.irx` file that `loadcore` accepts

After this fix, `sjpcm2.irx` correctly loaded and the IOP print appeared in TTY:
```
SjPCM v2.1 - by Sjeep
SjPCM: libsd initialised!
SjPCM: Memory Allocated. 63488 bytes left.
SjPCM: Entering playing thread.
```

However, audio was **still silent** after SNES emulation started.

### Bug 2: SjPCM Async Path Deadlock

#### Symptom
After the `.iopmod` fix, the IOP module loaded and initialized correctly (confirmed via TTY). Emulation ran, but:
- No audio output whatsoever
- Performance was severely degraded (much worse than the original Sony SDK build)

Both symptoms pointed to the EE being blocked somewhere in the audio path.

#### Investigation — First Attempt: mode=0 → mode=1

The original SjPCM code used an async buffer-query path. The per-frame call chain:
```
[end of frame N-1]  SjPCM_BufferedAsyncStart()   ← fires IOP query
[frame N]           GetOutputSamples()            ← needs IOP query result
                      → SjPCM_BufferedAsyncGet()
                          → SjPCM_Sync()
                              → WaitSema(_sjpcm_sema)  ← waits for callback
```

`_sjpcm_sema` is a semaphore with `init_count=1`. The intended flow:
1. `SjPCM_BufferedAsyncStart()` calls `WaitSema` (consumes token), fires async `SifCallRpc` with callback `_SjPCM_BufferedIntr`
2. IOP responds → SIF interrupt → `_SjPCM_BufferedIntr` → `iSignalSema(_sjpcm_sema)` (restores token)
3. `SjPCM_Sync()` → `WaitSema` succeeds

`SjPCM_BufferedAsyncStart()` called `SifCallRpc` with mode=`0`. On the **original ps2lib SDK**, mode `0` was async and mode `1` was sync. On **PS2SDK** the convention is reversed: mode `0` is **synchronous/blocking**, mode `1` is async with interrupt callback.

First fix attempt: change mode `0` → `1`. This change was correct in principle (mode=1 IS async on PS2SDK), but after testing on PCSX2 the problem persisted unchanged.

#### Investigation — Root Cause: PCSX2 SIF Async Interrupt

With mode=1, `SifCallRpc` fires the RPC and returns immediately — but the callback `_SjPCM_BufferedIntr` is only invoked when the PCSX2 SIF emulation delivers an EE interrupt signalling IOP completion. **PCSX2 does not reliably deliver this interrupt for async SIF RPCs** in the configurations tested. The result is identical to before: `iSignalSema` never fires, `WaitSema` in `SjPCM_Sync()` blocks forever.

Furthermore, `SjPCM_Init()` itself ended with:
```c
SjPCM_BufferedAsyncStart();  // fires async RPC, consumes semaphore token
SjPCM_BufferedAsyncGet();    // SjPCM_Sync() → WaitSema → deadlock immediately
```
So `SjPCM_Init()` **never returned** — the entire emulator was frozen from the first frame.

The async path (`SjPCM_EnqueueAsync` and `SjPCM_BufferedAsyncStart/Get`) is fundamentally unreliable in PCSX2 because it depends on SIF interrupt delivery that the emulator does not implement correctly.

#### Fix: Switch to Synchronous Path Entirely

The synchronous `SjPCM_Buffered()` and `SjPCM_Enqueue()` functions use `SifCallRpc` with mode=0, which blocks the EE until the IOP responds — no callback, no semaphore, no interrupt dependency.

Three changes:

**1. `sjpcm_rpc.c` — Remove async calls from `SjPCM_Init()`**:
```c
// BEFORE (deadlocks immediately on PCSX2):
sjpcm_inited = 1;
SjPCM_BufferedAsyncStart();
SjPCM_BufferedAsyncGet();
return 0;

// AFTER (clean return):
sjpcm_inited = 1;
return 0;
```

**2. `mainloop.cpp` — Use sync mix buffer**:
```cpp
// BEFORE:
_SJPCMMix = new SJPCMMixBuffer(32000, TRUE);   // TRUE = async path

// AFTER:
_SJPCMMix = new SJPCMMixBuffer(32000, FALSE);  // FALSE = sync path
```

**3. `mainloop.cpp` — Remove per-frame async start call**:
```cpp
// BEFORE (end of frame body):
SjPCM_BufferedAsyncStart();   // fires async RPC that PCSX2 never completes
}

// AFTER:
}   // (call removed)
```

With the sync path:
- `GetOutputSamples()` → `SjPCM_Buffered()` → `SifCallRpc(SJPCM_GETBUFFD, mode=0)` → blocks ~0.5 ms, returns sample count
- `Flush()` → `SjPCM_Enqueue()` → SIF DMA + `SifCallRpc(SJPCM_ENQUEUE, mode=0)` → blocks ~0.5 ms, updates write position
- No callbacks, no semaphores, no interrupt dependency — works correctly on PCSX2
- Overhead per frame: ~1 ms total (acceptable at 60 fps)

**Result: still no audio, still terrible performance.** (Tested April 2026)

### Bug 3: `SjPCM_Enqueue(wait=1)` — SifDmaStat Spin Loop

#### Symptom
After switching to the sync path (Bug 2 fix), audio was still silent and performance was still severely degraded.

#### Investigation
`SjPCM_Enqueue()` contains two spin loops, one per stereo channel:
```c
i = SifSetDma(&sdt, 1);                          // submit EE→IOP DMA
while ((wait != 0) && (SifDmaStat(i) >= 0));     // wait for DMA completion
```
`SifDmaStat(id)` returns negative when the transfer is complete, ≥0 while in-progress. On PCSX2, `SifDmaStat` for EE→IOP SIF transfers may **never** return negative — the transfer is always reported as in-progress. With `wait=1` (the default), the EE spins forever on the first channel's loop and never proceeds to the `SifCallRpc(SJPCM_ENQUEUE)` call. The IOP never receives the enqueue RPC, `writetotal` is never updated, the play thread has no data, the SPU2 outputs silence. The constant spin also explains the severe performance degradation — the EE burns all its cycles in the loop.

Additionally, both `FlushCache(0)` calls before the DMAs (one per channel) flushed the entire 16 KB DCache via a kernel syscall (`syscall 100`). On PCSX2, kernel syscalls require JIT synchronization and are expensive. These ran every frame even when the spin loop was not blocking.

#### Fix Attempted
- `Gep/Source/ps2/sjpcmbuffer.cpp`: Changed `SjPCM_Enqueue(left, right, nOutSamples, 1)` → `SjPCM_Enqueue(left, right, nOutSamples, 0)` — skips both spin loops entirely
- `SNESticle/Modules/sjpcm/ee/sjpcm_rpc.c`: Added `SifWriteBackDCache((void*)sbuff, 64*4)` before `SifCallRpc` in both `SjPCM_Buffered()` and `SjPCM_Enqueue()` — ensures EE reads IOP response from RAM
- `SNESticle/Modules/sjpcm/ee/sjpcm_rpc.c`: Replaced `FlushCache(0)` in both `SjPCM_Enqueue()` (sync) and `SjPCM_EnqueueAsync()` with `SifWriteBackDCache(left/right, size*2)` — targeted flush of only the audio data range using CACHE instructions, no kernel syscall

**Result: still no audio, still terrible performance.** (Tested April 2026)

### Bug 4: `FlushCache(0)` Does Not Invalidate the Cache — Wrong `pcmbufl`/`pcmbufr` Addresses

#### Symptom
After Bugs 1, 2, and 3 fixed: still total silence and bad performance.

#### Investigation
Reading `kernel.h` from PS2SDK:
```c
#define WRITEBACK_DCACHE  0
#define INVALIDATE_DCACHE 1
```

`FlushCache(0)` = `WRITEBACK_DCACHE`: iterates the data cache and writes dirty lines back to RAM, but the lines **remain valid in the cache** with their current values. It does NOT invalidate.

In `SjPCM_Init()`, the original code was:
```c
sbuff[0] = sync;                // EE writes: 0
sbuff[1] = numsamples;          // EE writes: 960*25 = 24000
sbuff[2] = maxenqueuesamples;   // EE writes: 800*5 = 4000

FlushCache(0);                  // WRITEBACK only — cache lines still valid with 24000/4000
SifCallRpc(&cd0, SJPCM_INIT, 0, sbuff, 64, sbuff, 64, 0, 0);
// IOP: allocates PCM buffers, writes real IOP addresses to sbuff[1/2/3] via SIF DMA
// SIF DMA writes directly to EE RAM, bypassing the EE cache
FlushCache(0);                  // WRITEBACK only — cache lines STILL valid with 24000/4000
pcmbufl = sbuff[1];             // reads from CACHE: 24000 (0x5DC0) — WRONG
pcmbufr = sbuff[2];             // reads from CACHE:  4000 (0x0FA0) — WRONG
```

Every subsequent `SifSetDma` in `SjPCM_Enqueue()` targeted `pcmbufl + bufpos` = `0x5DC0 + 0` and `pcmbufr + bufpos` = `0x0FA0 + 0`. These are random IOP RAM addresses, not the SPU2 ring buffers. The IOP play thread never received audio data at the correct addresses and output silence.

`FlushCache(1)` = `INVALIDATE_DCACHE` would have been correct, but the right tool is `SifWriteBackDCache(ptr, size)` which uses CACHE instructions directly and both writes back AND invalidates the specified range.

#### Fix Attempted
`SNESticle/Modules/sjpcm/ee/sjpcm_rpc.c` — `SjPCM_Init()`:
```c
// BEFORE:
FlushCache(0);
SifCallRpc(&cd0, SJPCM_INIT, 0, (void*)(&sbuff[0]), 64, (void*)(&sbuff[0]), 64, 0, 0);
FlushCache(0);
pcmbufl = sbuff[1];
pcmbufr = sbuff[2];
bufpos = sbuff[3];

// AFTER:
SifWriteBackDCache((void*)sbuff, 64);  // write-back + invalidate before RPC
SifCallRpc(&cd0, SJPCM_INIT, 0, (void*)(&sbuff[0]), 64, (void*)(&sbuff[0]), 64, 0, 0);
// cache is invalid for sbuff range; reads go to RAM where IOP wrote the real addresses
pcmbufl = sbuff[1];
pcmbufr = sbuff[2];
bufpos = sbuff[3];
```

**Result: still no audio, still terrible performance.** (Tested April 23, 2026)

### Performance Fix: Removing Debug Info from Release Builds

A separate performance issue: the ELF was **4.3 MB** for a release build. Investigation revealed PS2SDK's `Makefile.eeglobal_cpp` unconditionally adds:
```makefile
EE_DBGINFOFLAGS ?= -gdwarf-2 -gz
```

DWARF debug sections add ~1.5 MB to the ELF. They are not loaded at runtime, but they slow down PCSX2's ELF parsing and loading phase, and contribute to apparent startup latency. In `Makefile.ps2sdk`:
```makefile
# Override PS2SDK default: suppress DWARF debug sections in all builds
EE_DBGINFOFLAGS =
```

Result: ELF shrank from **4.3 MB → 2.8 MB** (−35%).

### Summary of Audio Fixes

| # | File | Change | Effect |
|---|------|--------|--------|
| 1 | `Modules/sjpcm/iop/Makefile` | Rewritten for PS2SDK IOP toolchain + `srxfixup` | `.iopmod` section present → IOP module loads correctly |
| 2 | `Modules/sjpcm/ee/sjpcm_rpc.c` | Removed `SjPCM_BufferedAsyncStart/Get` from `SjPCM_Init()` | Init no longer deadlocks on semaphore |
| 3 | `Source/ps2/mainloop.cpp` | `SJPCMMixBuffer(32000, FALSE)` + removed per-frame async start | Sync path used — no SIF interrupt dependency |
| 4 | `Project/ps2/Makefile.ps2sdk` | `EE_DBGINFOFLAGS =` (empty) | Debug sections stripped → ELF −35% smaller |
| 5 | `Gep/Source/ps2/sjpcmbuffer.cpp` | `SjPCM_Enqueue(..., 0)` (wait=0) | Eliminates `SifDmaStat` spin loop that blocks forever on PCSX2 |
| 6 | `Modules/sjpcm/ee/sjpcm_rpc.c` | `SifWriteBackDCache(sbuff, 64*4)` before sync RPCs in `SjPCM_Buffered()` and `SjPCM_Enqueue()` | Ensures EE reads IOP response from RAM, not stale cache |
| 7 | `Modules/sjpcm/ee/sjpcm_rpc.c` | `SifWriteBackDCache(sbuff, 64)` replacing `FlushCache(0)` in `SjPCM_Init()` | Cache invalidated → `pcmbufl`/`pcmbufr` read correctly from IOP response |
| 8 | `Modules/sjpcm/ee/sjpcm_rpc.c` | `SifWriteBackDCache(left/right, size*2)` replacing `FlushCache(0)` in both enqueue functions | Targeted ~1 KB flush via CACHE instructions instead of full 16 KB kernel syscall |
| 9 | `Modules/sjpcm/ee/sjpcm_rpc.c` | Added `printf` after `SjPCM_Init` RPC return and in `SjPCM_Enqueue` (first 5 calls) | Diagnostic: verify `pcmbufl`/`pcmbufr` addresses and that enqueue is being called |
| 10 | `Gep/Source/ps2/sjpcmbuffer.cpp` | Added `printf` in `GetOutputSamples` (first 3 calls) and `Flush` (first 3 calls) | Diagnostic: verify IOP buffer query returns sane value and that audio data is generated |
| 11 | `Modules/sjpcm/ee/sjpcm_rpc.c` | Reordered sync enqueue request flush (`sbuff[0]=size` before `SifWriteBackDCache`), added cache guards for `SjPCM_Buffered()`/`SjPCM_Available()`, forced async GETBUFFD mode=1, added enqueue async send-buffer flush | Intended to fix stale RPC payload/response behavior; build succeeded, but runtime result unchanged (silent + slow) |

**Current status (April 24, 2026, tested)**: After 11 tracked attempts (9 functional + 2 diagnostic), audio is **still silent** and performance is **still severely degraded** on PCSX2. The IOP side initializes correctly (TTY messages appear), but stable EE↔IOP audio streaming has not been achieved.

### Bug 5: Diagnostic Prints Added — Tracing the EE Audio Pipeline

#### Context
After Bug 4, all plausible single-point failures (module loading, async deadlocks, DMA spin loops, cache coherency) had been addressed in code, yet audio remained silent. The next step is to instrument the audio pipeline with `printf` calls to determine **which stage is the first to fail at runtime**.

The full per-frame audio pipeline (EE side) is:
```
1. mainloop.cpp: pMixBuffer = _SJPCMMix           ← assign mix buffer
2. _ExecuteSnes(): _pSystem->ExecuteFrame(pInput, pSurface, pMixBuffer)
   └─ snspcmix.cpp: pMixBuf->GetOutputSamples()   ← ask: how many samples needed?
   └─ SPC700 DSP runs, generates PCM data
   └─ snspcmix.cpp: pMixBuf->OutputSamplesStereo() ← push samples to SJPCMMixBuffer
   └─ snspcmix.cpp: pMixBuf->Flush()              ← submit to IOP via SjPCM_Enqueue
      └─ sjpcm_rpc.c: SjPCM_Enqueue()             ← SIF DMA + RPC to IOP
```

#### Diagnostic Prints Added (April 23, 2026)

Four `printf` calls were added, each guarded by a `static int` counter to fire only on the first N calls (avoiding log spam):

**`SNESticle/Modules/sjpcm/ee/sjpcm_rpc.c` — `SjPCM_Init()` (after RPC returns)**:
```c
pcmbufl = sbuff[1];
pcmbufr = sbuff[2];
bufpos = sbuff[3];
printf("SjPCM_Init: pcmbufl=0x%08X pcmbufr=0x%08X bufpos=%d\n", pcmbufl, pcmbufr, bufpos);
```
Purpose: Verify that `pcmbufl`/`pcmbufr` contain real IOP RAM addresses (should be ~`0x00100000`–`0x001FFFFF` range), not the original `numsamples`/`maxenqueuesamples` values (which would indicate the cache-invalidation fix in Bug 4 did not take effect).

**`SNESticle/Modules/sjpcm/ee/sjpcm_rpc.c` — `SjPCM_Enqueue()` (first 5 calls)**:
```c
{ static int _eq=0; if(_eq<5){ printf("SjPCM_Enqueue: size=%d bufpos=%d\n", size, bufpos); _eq++; } }
```
Purpose: Confirm that `SjPCM_Enqueue` is actually being called with non-zero `size` values. If this print never appears, `Flush()` is never reached — the audio data is not being generated by the SNES core.

**`Gep/Source/ps2/sjpcmbuffer.cpp` — `SJPCMMixBuffer::GetOutputSamples()` (first 3 calls)**:
```c
{ static int _gos=0; if(_gos<3){ printf("SJPCMMixBuffer::GetOutputSamples: nBuffered=%d -> nSamples=%d\n", nBuffered, nSamples); _gos++; } }
```
Purpose: Check `nBuffered` (the IOP's current buffered sample count) and the resulting `nSamples` request. If `nBuffered` is abnormally large (e.g., > 3200), `nSamples` would be 0 or negative → clamped to 0 → SNES core produces no audio samples → `Flush()` is called with `nOutSamples=0` → `SjPCM_Enqueue` is never called → silence. This would indicate `SjPCM_Buffered()` is returning stale or incorrect data.

**`Gep/Source/ps2/sjpcmbuffer.cpp` — `SJPCMMixBuffer::Flush()` (first 3 calls)**:
```c
{ static int _fl=0; if(_fl<3){ printf("SJPCMMixBuffer::Flush: nOutSamples=%d\n", nOutSamples); _fl++; } }
```
Purpose: Confirm `Flush()` is called with non-zero `nOutSamples`. If this print shows `nOutSamples=0` every time, the SNES core generated no samples — which means `GetOutputSamples()` returned 0 (see above). If this print never appears at all, `Flush()` is not being called — the SNES core execution is not triggering the audio pipeline (possible disconnection between `_pSystem->ExecuteFrame` and the mix buffer).

#### What to Look For in the PCSX2 TTY Log

| Print seen | What it means |
|---|---|
| `SjPCM_Init: pcmbufl=0x00000000` | Cache bug persists — `pcmbufl`/`pcmbufr` are zero, all DMA writes go to address 0 |
| `SjPCM_Init: pcmbufl=0x00005DC0` (= 24000) | Cache bug persists — reads `numsamples` instead of real IOP address |
| `SjPCM_Init: pcmbufl=0x001xxxxx` | ✅ Addresses look valid — cache fix worked |
| `GetOutputSamples: nBuffered=N -> nSamples=0` always | `SjPCM_Buffered()` returns too-high value → SNES core asked for 0 samples → silence |
| `GetOutputSamples: nBuffered=0 -> nSamples=NNN` | ✅ IOP buffer query works, samples are being requested |
| `Flush: nOutSamples=0` always | SNES core generated nothing — verify SPC700 DSP is running |
| `Flush: nOutSamples=NNN` (NNN > 0) | ✅ Audio data generated |
| `SjPCM_Enqueue: size=NNN ...` | ✅ Enqueue called — problem is in IOP, not EE |
| `Flush` print appears but `SjPCM_Enqueue` never appears | Enqueue call is silently failing (e.g., `sjpcm_inited=0`) |
| None of the `GetOutputSamples`/`Flush` prints appear | `ExecuteFrame` is not reaching the audio pipeline — `pMixBuffer` may be NULL |

#### Files Changed
- `SNESticle/Modules/sjpcm/ee/sjpcm_rpc.c` — added `printf` in `SjPCM_Init` (post-RPC) and `SjPCM_Enqueue` (first 5)
- `Gep/Source/ps2/sjpcmbuffer.cpp` — added `printf` in `GetOutputSamples` (first 3) and `Flush` (first 3)

**Current status (April 23, 2026, tested)**: Build compiled and loaded. Results:
- Audio still completely silent.
- **Severe performance regression introduced** — emulation runs at a fraction of normal speed, noticeable input lag and frame rate collapse.
- The diagnostic prints (all capped at 3–5 calls) are not the source of the slowdown. See Bug 6 below for the performance regression analysis.

**Revert (April 23, 2026)**: All diagnostic prints removed from `sjpcm_rpc.c` and `sjpcmbuffer.cpp`. All `SifWriteBackDCache` additions reverted to `FlushCache(0)`. Async mode restored with semaphore fix. See Bug 7.

### Bug 6: Severe Performance Regression from Sync RPC Per Frame + Possible IOP Corruption

#### Symptom
After the Bug 5 build (diagnostic prints + `SifWriteBackDCache` added to `SjPCM_Buffered()` and `SjPCM_Enqueue()`), emulation runs at a fraction of normal speed. Input is extremely laggy and the frame rate collapses. This is independent of the audio silence — the performance degradation happens continuously even after the first few frames when the debug prints have already stopped firing.

#### What Changed Between the Last "Normal" Build and the Bug 5 Build

Besides the capped `printf` calls (which stop after 3–5 calls and are not ongoing), one new per-frame overhead was added:

```c
// SjPCM_Buffered() — called EVERY FRAME via GetOutputSamples()
int SjPCM_Buffered()
{
    if (!sjpcm_inited) return 0;
    SifWriteBackDCache((void*)sbuff, 64*4);          // ← NEW: flushes 256 bytes (4 cache lines)
    SifCallRpc(&cd0, SJPCM_GETBUFFD, 0, ...);       // sync RPC, blocks ~0.5 ms
    return sbuff[3];
}
```

The `SifWriteBackDCache(sbuff, 64*4)` itself is very fast (4 cache lines). The `SifCallRpc` with mode=0 is a blocking synchronous call — it was already there before. So the new code alone should not add significant latency.

#### Primary Theory: Wrong `pcmbufl`/`pcmbufr` → SIF DMA Corrupts IOP Kernel RAM

If the Bug 4 cache-invalidation fix did not fully take effect, `pcmbufl` and `pcmbufr` contain wrong values. In particular:

- `pcmbufl = sbuff[1]` after stale cache read = `numsamples` = `960*25 = 24000 = 0x5DC0`
- `pcmbufr = sbuff[2]` after stale cache read = `maxenqueuesamples` = `800*5 = 4000 = 0x0FA0`

Every call to `SjPCM_Enqueue()` then submits two SIF DMA transfers:
- Left channel: `sdt.dest = pcmbufl + bufpos = 0x5DC0 + 0 = 0x5DC0` in IOP address space
- Right channel: `sdt.dest = pcmbufr + bufpos = 0x0FA0 + 0 = 0x0FA0` in IOP address space

**IOP address `0x0FA0`–`0x5DC0` is IOP kernel RAM** — this is where `loadcore`, `intrman`, the IOP kernel stack, and module tables reside. Writing ~1600 bytes of audio data to this region **corrupts the IOP kernel**.

After the first `SjPCM_Enqueue()` call corrupts IOP RAM:
- The IOP's kernel data structures (interrupt handlers, thread table, semaphore list) are overwritten with PCM data
- Subsequent SIF RPCs (`SjPCM_Buffered()`, `SjPCM_Enqueue()`, etc.) may never return — the IOP can no longer process SIF interrupts or schedule threads
- The EE spins in `SifCallRpc(mode=0)` waiting for a response that never arrives
- The EE appears frozen / running at ~0 fps

This would explain both symptoms simultaneously:
- **No audio**: IOP corrupted, play thread never runs
- **Terrible performance**: Every frame, `SjPCM_Buffered()` and/or `SjPCM_Enqueue()` hang for hundreds of milliseconds waiting for an IOP that is no longer functioning

#### Secondary Theory: `SjPCM_Buffered()` Returns 0 → SPC700 Overrun

If the IOP is corrupted and never responds to `SJPCM_GETBUFFD`, `SifCallRpc` may time out and return 0 (or whatever was in `sbuff[3]`). With `nBuffered = 0`, `GetOutputSamples()` computes the maximum sample count:
```
nSamples = (SJPCMMIXBUFFER_MAXBUFFERED - 0) * 32000 / 48000 = 3200 * 32000 / 48000 ≈ 2133
```
This is ~2.7× more 32kHz samples than a normal 60fps frame requires (~800 samples). The SPC700 DSP then mixes 2133 samples per frame instead of ~800, consuming roughly 3× normal CPU time. This alone could cause a significant but not catastrophic slowdown. In combination with the IOP hang theory, it would make performance completely unusable.

#### Why `SifWriteBackDCache(sbuff, 64)` in `SjPCM_Init()` May Not Have Fixed the Cache Bug

`SifWriteBackDCache(ptr, size)` on the PS2 EE executes a sequence of CACHE instructions that write-back and invalidate all cache lines in the specified range. After the call, the cache lines for `sbuff[0..15]` are invalid.

However, `SifCallRpc` with mode=0 is a **synchronous blocking call** implemented via a SIF interrupt and semaphore internally. The sequence is:
1. EE writes RPC parameters to `sbuff`
2. EE calls `SifCallRpc` → sends SIF command to IOP → blocks on internal semaphore
3. IOP processes RPC, writes response to `sbuff` area via SIF DMA (direct to RAM, bypasses EE cache)
4. SIF DMA completion fires EE interrupt → `SifCallRpc` unblocks
5. EE reads `sbuff[1/2/3]`

Between steps 3 and 5, the EE cache lines for `sbuff` are still **invalid** (from step 0's `SifWriteBackDCache`), so the reads in step 5 should go to RAM and pick up the IOP-written values. This logic is correct.

But: if `SifWriteBackDCache(sbuff, 64)` (note: `64` bytes = 1 cache line, not `64*4 = 256` bytes) only invalidates **the first 64 bytes** and `sbuff[1]`/`sbuff[2]`/`sbuff[3]` happen to lie in a different cache line that was NOT invalidated, the reads still return stale cached values. `sbuff` is 256 bytes; `sbuff[1]` is at offset 4 (same cache line as `sbuff[0]`); `sbuff[2]` at offset 8; `sbuff[3]` at offset 12. All three are in the first 64-byte cache line — so a `SifWriteBackDCache(sbuff, 64)` flush of 1 cache line covers all of them. This should be correct.

The actual reason the bug persisted is unknown without PCSX2 TTY diagnostics (which the Bug 5 build provides, but performance now collapses before the diagnostic output can be read).

#### Files Identified as Responsible
- `SNESticle/Modules/sjpcm/ee/sjpcm_rpc.c` — `pcmbufl`/`pcmbufr` wrong → `SjPCM_Enqueue()` DMA writes to IOP kernel RAM
- `SNESticle/Modules/sjpcm/iop/sjpcm_irx.c` — `int readtotal;` uninitialized (see suspect from Bug 5)

**Current status (April 23, 2026)**: Build confirmed to produce severe performance regression in addition to persistent silence. The primary suspect is corrupted `pcmbufl`/`pcmbufr` values causing IOP kernel RAM corruption on every `SjPCM_Enqueue()` call.

### Bug 7: Semaphore `init_count` Uninitialized — Async Path Always Deadlocked

#### Root Cause
In `sjpcm_rpc.c`, `SjPCM_Init()` creates the EE semaphore that serializes the async RPC pipeline:

```c
ee_sema_t compSema;
compSema.max_count = 1;
compSema.option = 0;
_sjpcm_sema = CreateSema(&compSema);
```

The struct field `init_count` was **never set**. In C, local struct variables are NOT zero-initialized — `compSema.init_count` is garbage (typically 0 from stack). With `init_count = 0`, the semaphore starts with count 0.

In PS2SDK's EE kernel, `WaitSema(sema)` decrements the count and blocks if it reaches below 0. With `init_count = 0`, the very first call to `SjPCM_EnqueueAsync()` → `WaitSema(_sjpcm_sema)` blocks immediately and waits for a `SignalSema` that never comes — permanent deadlock on the first frame.

This is why the async path was observed to "not work" in all prior investigations. The async RPC callbacks (`_SjPCM_EnqueueIntr`, `_SjPCM_BufferedIntr`) call `iSignalSema`, but they're never reached because the EE is already blocked in `WaitSema` before the RPC can fire.

#### Why the Sync Path Was Slower
Switching to `bAsync=FALSE` (sync path) eliminated the semaphore deadlock, but introduced two blocking `SifCallRpc(mode=0)` calls per frame:
1. `SjPCM_Buffered()` — blocks until IOP responds (~0.5ms per call)
2. `SjPCM_Enqueue()` — blocks until IOP responds (~0.5ms per call)

If PCSX2's IOP emulation runs significantly slower than real hardware, these could block for much longer, degrading per-frame performance.

#### Performance Regression from Bug 5/6 Build
Additionally, the Bug 5 build added `SifWriteBackDCache(sbuff, 64*4)` to `SjPCM_Buffered()` which uses CACHE 0x18 instructions (writeback, no invalidate) via `sync.l` barriers. If PCSX2's JIT handles `sync.l` or CACHE instructions with higher overhead than expected, this per-frame addition could contribute to slowdown.

#### Fix Applied (April 23, 2026)

**`sjpcm_rpc.c` — `SjPCM_Init()`**: Added the two missing fields:
```c
compSema.init_count = 1;  // ← was missing; async path requires semaphore count=1 on start
compSema.attr = 0;
compSema.max_count = 1;
compSema.option = 0;
_sjpcm_sema = CreateSema(&compSema);
```

**`sjpcm_rpc.c`**: Reverted all `SifWriteBackDCache` additions back to `FlushCache(0)` (original pattern). Removed all diagnostic `printf` statements. `SjPCM_Buffered()` and `SjPCM_Enqueue()` no longer have extra flush calls.

**`sjpcmbuffer.cpp`**: Removed all diagnostic `printf` statements.

**`mainloop.cpp`**:
- `new SJPCMMixBuffer(32000, FALSE)` → `new SJPCMMixBuffer(32000, TRUE)` (restore async mode)
- After `SjPCM_Init()`: added `SjPCM_BufferedAsyncStart()` to prime the async query pipeline for the first frame
- Uncommented `SjPCM_BufferedAsyncStart()` at end of each frame in the main loop

#### Expected Behavior with Semaphore Fixed
With `init_count=1`, the per-frame async flow works correctly:
1. End of frame N: `SjPCM_BufferedAsyncStart()` → `WaitSema` (count: 1→0) → fires IOP query
2. IOP responds: `_SjPCM_BufferedIntr` callback → `iSignalSema` (count: 0→1)
3. Frame N+1: `SjPCM_BufferedAsyncGet()` → `SjPCM_Sync()` → `WaitSema` (count: 1→0), `SignalSema` (count: 0→1) → returns `_Buffered`
4. End of frame N+1: `SjPCM_EnqueueAsync()` → `WaitSema` (count: 1→0) → fires SIF DMA + async RPC
5. IOP processes RPC: `_SjPCM_EnqueueIntr` callback → updates `bufpos`, `iSignalSema` (count: 0→1)
This entire sequence is non-blocking: the EE fires each async operation and returns immediately, checking results next frame.

**Critical dependency**: Steps 2 and 5 require PCSX2 to deliver async SIF RPC callbacks. If PCSX2 does not deliver them, `WaitSema` in steps 1 and 4 blocks forever. If this happens, revert to `bAsync=FALSE` and investigate alternative non-blocking approaches.

| File | Change |
|------|--------|
| `Modules/sjpcm/ee/sjpcm_rpc.c` | `compSema.init_count = 1; compSema.attr = 0;` added to `SjPCM_Init()` |
| `Modules/sjpcm/ee/sjpcm_rpc.c` | All `SifWriteBackDCache` additions reverted to `FlushCache(0)`; diagnostic prints removed |
| `Gep/Source/ps2/sjpcmbuffer.cpp` | Diagnostic prints removed |
| `Source/ps2/mainloop.cpp` | `bAsync=TRUE`; bootstrap `SjPCM_BufferedAsyncStart()`; per-frame call uncommented |

**Build**: April 23, 2026 19:30 — 4.2 MB ELF. Awaiting PCSX2 test result.

### Bug 8: RPC Payload Ordering + Cache Guards in EE Audio RPC Path

#### Context
After Bug 7, the project was still in the same failure state reported by the user: no audio output and severe slowdown. A new EE-side RPC correctness pass was attempted to eliminate stale request/response cache-line behavior and possible incorrect RPC mode usage in the async helper path.

#### Changes Applied (April 24, 2026)

**`SNESticle/Modules/sjpcm/ee/sjpcm_rpc.c`**:
- `compSema.attr = 0;` explicitly set in `SjPCM_Init()`.
- In sync `SjPCM_Enqueue()`, reordered request preparation:
  - `sbuff[0] = size;`
  - then `SifWriteBackDCache(sbuff, 64);`
  - then `SifCallRpc(... SJPCM_ENQUEUE ...)`
  This avoids flushing the cache line before writing the current enqueue size.
- Added `SifWriteBackDCache(enqueue_sbuff, 64);` before async enqueue RPC call.
- Changed `SjPCM_BufferedAsyncStart()` `SJPCM_GETBUFFD` call from mode `0` to mode `1` for callback-based async behavior.
- Added `SifWriteBackDCache(sbuff, 64);` before sync `SjPCM_Available()` and `SjPCM_Buffered()` RPC calls.

#### Build/Test Outcome
- Build completed successfully (new `SNESticle.elf` generated, April 24, 2026).
- Runtime result on PCSX2 remained unchanged:
  - **No audio output**
  - **Severe slowdown still present**

#### Conclusion
This correction has been **attempted and tested** and did **not** resolve either symptom. Root cause remains unresolved.

</details>

---

### How to Diagnose Audio Silence (Reference)

If audio stops working in a future build, check the PCSX2 TTY log for these messages in order:

| Message | Status | Meaning |
|---------|--------|---------|
| `SjPCM v2.1 - by Sjeep` | Expected | `sjpcm2.irx` `_start` ran, RPC thread started |
| `SjPCM: libsd initialised!` | Expected | `SdInit(0)` succeeded, SPU2 driver ready |
| `SjPCM: Memory Allocated. NNNN bytes left.` | Expected | IOP heap had enough memory |
| `SjPCM: Entering playing thread.` | Expected | `SdBlockTrans` started, DMA is running |
| `SjPCM: Failed to initialise libsd!` | Error | `rom0:LIBSD` not loaded, or SPU2 not accessible |
| `IOP Fail: rom0:LIBSD -N` | Error | BIOS LIBSD not available on this BIOS version |
| *(none of the SjPCM messages)* | Error | `sjpcm2.irx` failed to load — check `.iopmod` section |
| *(messages appear but no audio)* | Error | SJPCM audio path was fundamentally broken on PCSX2. Migrated to `audsrv` — see above |

> [!NOTE]
> The SJPCM diagnostic guide above is archived for historical reference. The current audio backend uses `audsrv`. To diagnose audsrv issues, check PCSX2 TTY for `audsrv` initialization messages and verify `audsrv_queued()` returns vary between frames.

---

## How to Build

### Prerequisites

- Linux environment (WSL2 on Windows works fine)
- ps2dev toolchain installed at `/usr/local/ps2dev`

### Build Command

```bash
export PS2DEV=/usr/local/ps2dev
export PS2SDK=$PS2DEV/ps2sdk
export PATH=$PS2DEV/bin:$PS2DEV/ee/bin:$PS2DEV/iop/bin:$PS2DEV/dvp/bin:$PATH

cd SNESticle/Project/ps2
make -f Makefile.ps2sdk
```

### Output

- `SNESticle/Project/ps2/SNESticle.elf` — ~4.2 MB self-contained ELF
- `SNESticle/Project/ps2/debug_ps2sdk/SNESticle.map` — Linker map

### Clean Build

```bash
make -f Makefile.ps2sdk clean
rm -f *_irx.c   # Remove generated bin2c files
```

---

## How to Test (PCSX2)

1. Create an ISO image containing:
   ```
   /SYSTEM.CNF        — PS2 boot configuration
   /SLUS_220.03;1     — SNESticle.elf (renamed to match SYSTEM.CNF)
   /ROMS/             — Directory with .smc/.sfc ROM files
   ```

2. `SYSTEM.CNF` content:
   ```
   BOOT2 = cdrom0:\SLUS_220.03;1
   VER = 1.10
   VMODE = NTSC
   ```

3. Open the ISO in PCSX2 v2.6.3+
4. The emulator should boot to the file browser
5. Navigate to `cdfs:` → `ROMS` → select a ROM

---

## How to Create an ISO

```bash
# Create directory structure
mkdir -p iso_root/ROMS
cp SNESticle/Project/ps2/SNESticle.elf iso_root/SLUS_220.03

# Create SYSTEM.CNF
cat > iso_root/SYSTEM.CNF << 'EOF'
BOOT2 = cdrom0:\SLUS_220.03;1
VER = 1.10
VMODE = NTSC
EOF

# Copy ROMs
cp /path/to/roms/*.smc iso_root/ROMS/

# Create ISO (using mkisofs/genisoimage)
mkisofs -o SNESticle.iso \
  -V "SNESTICLE" \
  -sysid "PLAYSTATION" \
  -A "SNESticle" \
  -l -d -N \
  iso_root/
```

---

## Glossary

| Term | Description |
|------|-------------|
| **EE** | Emotion Engine — PS2's main 128-bit CPU (MIPS R5900 @ 294 MHz) |
| **IOP** | I/O Processor — PS2's secondary CPU (MIPS R3000A @ 37 MHz), handles all I/O |
| **IRX** | IOP Relocatable eXecutable — Loadable module for the IOP processor |
| **ELF** | Executable and Linkable Format — The PS2 executable format |
| **GS** | Graphics Synthesizer — PS2's GPU |
| **GIF** | Graphics InterFace — DMA channel from EE to GS |
| **SIF** | Sub-processor InterFace — Communication channel between EE and IOP |
| **RPC** | Remote Procedure Call — SIF-based function calls between EE and IOP |
| **SBV** | Sub Bus Vector — Patches to the IOP kernel enabling buffer-based module loading |
| **bin2c** | Utility that converts binary files to C arrays for embedding |
| **ps2lib** | Sony's proprietary PS2 development SDK (not publicly available) |
| **PS2SDK** | Community open-source PS2 SDK (https://github.com/ps2dev/ps2sdk) |
| **cdfs** | ISO 9660 filesystem driver for reading CD/DVD |
| **BDM** | Block Device Manager — PS2SDK abstraction for storage devices |
| **audsrv** | PS2SDK's built-in audio server IOP module — current audio backend (replaced SJPCM) |
| **PCSX2** | PS2 emulator for PC, used for testing |
| **DualShock 2** | PS2 controller with analog sticks and pressure-sensitive buttons |
| **N32 ABI** | MIPS calling convention used by modern ps2dev GCC (`long` = 32 bits) |
| **Uint128** | 128-bit unsigned integer type (`__attribute__((mode(TI)))`). On GCC 15 N32, mapped to a GPR pair, not a single 128-bit register |
| **lq/sq** | Load Quadword / Store Quadword — PS2 EE instructions that load/store 128 bits from/to a single register |
| **qfsrv** | Quadword Funnel Shift Right Variable — PS2 EE instruction for 128-bit barrel shift (used for pixel scrolling) |
| **pceqb/pcpyld/por/pxor/pand** | PS2 EE 128-bit SIMD instructions for parallel byte compare, copy, OR, XOR, AND operations |
| **mode(TI)** | GCC attribute specifying Tetra-Integer (128-bit) mode. Behavior differs between GCC 3.x (single reg) and GCC 15 (register pair) on MIPS |
