# CLAUDE.md — Mattbrew 6502 Project Guide

This document describes the codebase structure, development workflows, and conventions for AI assistants working on this project.

## Project Overview

A retro computing system combining a real 6502 CPU (1 MHz, 3.3V) with modern peripherals. Three hardware components communicate through a layered architecture:

```
6502 CPU ←→ Pi Pico 2 (RP2350) ←→ Pi Zero
  parallel bus     SPI slave/master    HDMI, USB, network
```

- **6502**: The main processor running 6502 assembly programs
- **Pi Pico 2 (RP2350)**: Translates between the 6502 parallel bus and SPI — a "dumb bridge"
- **Pi Zero**: Provides video/keyboard terminal (over SSH), networking, and netbooting via SPI master

---

## Repository Structure

```
6502/
├── bridge/          # Pi Pico firmware (C + CMake + PIO)
├── shein/           # Pi Zero terminal software (Rust)
├── emu/             # Web-based 6502 emulator (TypeScript + React)
├── blinkenlights/   # Test programs (C++ host tests, 6502 assembly)
│   └── asm/         # 6502 assembly programs built with ca65/ld65
├── pico-sdk/        # Submodule: Raspberry Pi Pico SDK v2.0+
├── README.md        # Project overview and architecture
├── protocol.md      # Complete SPI protocol specification (detailed)
└── protocol-design-notes.md  # Protocol design rationale
```

---

## Component Details

### `bridge/` — Pi Pico Firmware (C)

Runs on the RP2350 at 150 MHz. Translates between the 6502 parallel bus and SPI to the Pi Zero.

**Key files:**
| File | Purpose |
|------|---------|
| `main.c` | Entry point, device routing, IRQ management |
| `bus_interface.c` | 6502 parallel bus protocol, ring buffer DMA, device callbacks |
| `bus_interface.h` | Bus interface API |
| `bus_interface.pio` | RP2350 PIO state machine for timing-critical 6502 bus access |
| `spi_slave.c` | SPI slave mode, RX/TX DMA, REQUEST/READY handshake |
| `spi_slave.h` | SPI slave API |
| `bridge_defs.h` | Shared constants (device IDs, buffer sizes, GPIO pins) |
| `CMakeLists.txt` | Build configuration |

**Build:**
```bash
# Requires pico-sdk submodule and ARM GCC toolchain
cd bridge
mkdir build && cd build
cmake ..
make
# Flash bridge.uf2 to the Pico via USB mass storage
```

**Debug builds:**
```bash
cmake -DBRIDGE_DEBUG=1 ..
make
# Enables DBG_PRINTF() output over USB serial
```

**Device map (bridge_defs.h):**
| Device ID | Name | Description |
|-----------|------|-------------|
| 0 | Status | Device availability, SPI connection status |
| 1 | Control | Reserved for system control |
| 2 | Video/KB | 40×25 text terminal with ANSI colors |
| 3 | Netboot | Download programs from Pi Zero to 6502 RAM |
| 4 | Network | Ethernet data |
| 5–6 | — | Unassigned |
| 7 | Echo | Test/loopback device |

---

### `shein/` — Pi Zero Terminal Software (Rust)

Runs on the Pi Zero. Acts as SPI master to the Pico and renders a terminal TUI over SSH.

**Key files:**
| File | Purpose |
|------|---------|
| `src/main.rs` | App struct, event loop, TUI rendering, input/SPI polling |
| `src/spi_master.rs` | SPI master hardware interface (Linux), IRQ watcher |
| `src/terminal.rs` | 40×25 text terminal, VTE ANSI escape parser |
| `src/ui.rs` | Ratatui-based status bar and log display |
| `Cargo.toml` | Dependencies |

**Build:**
```bash
# For Pi Zero (cross-compile or build on Pi Zero):
cargo build --release

# For local development (SPI stubs on non-Linux):
cargo build
cargo run
```

**Key dependencies:**
- `ratatui` — TUI framework
- `crossterm` — Terminal raw mode and events
- `vte` — VT100/ANSI escape sequence parser
- `spidev`, `gpiocdev` — Linux SPI/GPIO hardware (Linux only)

---

### `emu/` — Web Emulator (TypeScript + React)

Browser-based 6502 CPU emulator with debugger UI.

---

### `blinkenlights/` — Test Programs

**C++ host tests** (`blinkenlights/*.cpp`): Simple programs for testing bus behavior.

**6502 Assembly** (`blinkenlights/asm/`): Test programs for the real hardware.

```bash
# Build assembly (requires CC65 toolchain: ca65, ld65)
cd blinkenlights/asm
ca65 hello.s -o hello.o
ld65 hello.o -C CONFIG.cfg -o hello
```

---

## Communication Protocols

### 6502 ↔ Pico (Parallel Bus, TLV framing)

**CPU Write (6502 → Pico):**
```
[device_id: 1 byte] [length: 1 byte] [data: 0–255 bytes]
```

**CPU Read (Pico → 6502):**
1. CPU writes `[device_id | 0x80]` to trigger a read
2. CPU polls the bus address until it sees a non-`0xFF` byte — that IS the length
3. CPU reads `length` bytes of data

### Pico ↔ Zero (SPI, Mode 3, 8 MHz)

Three transaction types (Zero initiates all):

| Command | Bytes | Description |
|---------|-------|-------------|
| WRITE `0x01` | `[0x01][LEN_HI][LEN_LO][payload…]` | Zero → Pico |
| REQUEST `0x02` | `[0x02]` | Ask if Pico has data |
| READ `0x03` | `[0x03][dummy…]` → `[BUF×8][LEN_HI][LEN_LO][payload…]` | Read after READY |

**Handshake signals (GPIO):**
- **IRQ** (GPIO 25, Pico → Zero): Pico has data pending
- **READY** (GPIO 24, Pico → Zero): TX DMA loaded, safe to clock

**TLV payload format:**
```
[device_id: 1 byte] [length: 1 byte] [data: 0–255 bytes]
(repeated, max 1542 bytes total = 6 × 257-byte packets)
```

---

## Code Conventions

### C (bridge/)
- `UPPER_SNAKE_CASE` for macros and constants
- `snake_case` for functions and variables
- Fixed-size ring buffers with epoch-based overrun detection
- DMA used everywhere for zero-copy transfers
- `DBG_PRINTF(...)` macro (compile-time disabled in release) for debug logging
- Conditional compilation: `#ifdef BRIDGE_DEBUG`

### Rust (shein/)
- Idiomatic Rust: `Result<T>` with `anyhow`, propagate errors via `?`
- `CamelCase` for types/structs, `snake_case` for functions/fields
- `#[cfg(target_os = "linux")]` for hardware-dependent code; stub impls elsewhere
- `VecDeque<Vec<u8>>` per-device TX queues for backpressure
- Event loop: TUI render → input poll → SPI drain → TX queue drain

### TypeScript (emu/)
- Functional React with hooks
- `CamelCase` for types/components, `camelCase` for functions
- External CPU state managed by `6502.ts` library; React state for UI
- Strict TypeScript; type declarations in `.d.ts` files for external libs

### 6502 Assembly (blinkenlights/asm/)
- Built with CC65 toolchain (`ca65` assembler, `ld65` linker)
- Linker config in `CONFIG.cfg` or `CONFIG_newmap.cfg`

---

## Key Design Patterns

1. **TLV framing everywhere** — All inter-component messages use `[device_id][length][data]`
2. **Ring buffers + DMA** — Zero-copy circular queues for high throughput
3. **Per-device isolation** — 8 devices each have independent RX/TX queues; no head-of-line blocking
4. **Flow control via BUF fields** — Each SPI read response includes per-device buffer availability
5. **IRQ/READY handshake** — Pico asserts IRQ when it has data; READY when DMA is loaded
6. **Platform stubs** — Rust SPI/GPIO code compiles on non-Linux with no-op stubs for dev

---

## Testing

There is no formal test suite. Testing is done manually:

- **Bridge**: Flash `.uf2` to Pico, connect to 6502, run assembly test programs
- **Shein**: `cargo run` on Pi Zero (or locally with stub SPI) and check TUI
- **Emulator**: `npm run dev`, load a ROM, step through CPU in browser debugger
- **Protocol/echo**: Device 7 (echo) on the bridge for bidirectional SPI ping-pong tests
- **Assembly tests**: `blinkenlights/asm/ramtest.s`, `rpitest.s`, etc.

---

## Development Tips

- The Pico SDK is a git submodule at `pico-sdk/`. Run `git submodule update --init` after cloning.
- `BRIDGE_DEBUG=1` enables verbose protocol logging on the Pico's USB serial port.
- The Rust `shein` app compiles on any OS but SPI/GPIO hardware is Linux-only; stub paths let you develop the TUI locally.
- The emulator's ROM can be loaded via the browser UI as a binary file.
- `protocol.md` is the canonical specification for all inter-component communication — read it before changing any message formats.
- `protocol-design-notes.md` explains why specific protocol choices were made (e.g., why not I2C, USB, etc.).
