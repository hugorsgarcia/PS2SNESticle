# SNESticle (PlayStation 2)

<div align="center">
  <p>A high-performance SNES emulator originally developed circa 2004, modernized and revitalized for contemporary PlayStation 2 homebrew development.</p>
</div>

## 📌 Overview

SNESticle is a highly optimized Super Nintendo Entertainment System (SNES) emulator tailored for the PlayStation 2. Originally written in the early days of PS2 homebrew, this project has undergone significant modernizations to make it a stable, clean, and developer-friendly showcase of modern PS2SDK standards.

## ✨ Features

* **High-Speed Emulation**: Heavily optimized MIPS assembly and inline 128-bit EE instructions ensuring rock-solid frame rates.
* **Native Memory Card Saving**: SRAM (`.srm`) and Save Icons (`icon.sys`, `icon.icn`) are safely persisted to the PS2 Memory Card (`mc0: / mc1:`) utilizing pure `libmc` APIs, bypassing bug-prone legacy BIOS ROM hooks.
* **Modern Networking Stack**: Legacy `IRX` modules have been completely gutted and replaced with `lwIP` and `NetMan`. 
* **Single-ELF Architecture**: All necessary modules (`ps2dev9`, `smap`, `netman`, `ps2ip`, `audsrv`) are embedded directly into the executable via `bin2c`, eliminating the need for external `.irx` files on your disk/USB.
* **GCC 15 / N32 ABI Ready**: Codebase refactored to comply with modern GCC constraints, fixing historical graphics rendering glitches tied to the older toolchain's handling of `Uint128` registers.

## 🛠️ Building

To build SNESticle, you will need a modern installation of the [ps2dev toolchain](https://github.com/ps2dev/ps2dev) running the latest `PS2SDK` and `GCC`.

1. Clone the repository:
   ```bash
   git clone https://github.com/your-username/SNESticle.git
   ```
2. Navigate to the PS2 project directory:
   ```bash
   cd SNESticle/SNESticle/Project/ps2
   ```
3. Compile the standalone ELF:
   ```bash
   make -f Makefile.ps2sdk
   ```
4. The compiled output will be generated as `SNESticle.elf`.

## 📜 Modernization Notes

If you are curious about the evolution of the SNESticle PS2 port or want to learn about the architectural challenges encountered while moving a 2004 homebrew project to 2026 standards, refer to our comprehensive [PORTING.md](PORTING.md).

It covers deep technical dives on:
* Replacing custom RPC servers with native PS2SDK modules (`libmc`).
* Diagnosing and fixing graphical striped glitches caused by GCC `mode(TI)` ABI changes in inline Assembly.
* Consolidating the project directory structure by removing thousands of legacy artifacts.

## 📄 License

SNESticle is open-source software. Please see the [LICENSE](LICENSE) file for more information.

> *"You guys (~~me too~~) have way too much free time."* — Original Developer