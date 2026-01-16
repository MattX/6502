# PIO-Based 6502 Bus Interface

This project implements a byte-stream interface between a 6502 CPU and an RP2350 microcontroller using PIO and DMA, eliminating the need for external CPLD and latches.

## Overview

The traditional approach (see `mcu_interface/`) requires:
- 1x CPLD (ATF750LVC) for bus timing and handshake logic
- 3x 74HC574 latches (TX, RX, Status)

This PIO-based approach requires:
- **No external logic ICs** - just direct GPIO connections
- External address decode logic (e.g., 74HC138) to generate CS_N

## Target Hardware

- **MCU:** RP2350 (Raspberry Pi Pico 2)
- **Tested on:** Adafruit Feather RP2350

## Pin Mapping

| GPIO | Signal | Direction | Description |
|------|--------|-----------|-------------|
| 0 | CS_N | Input | Chip select (directly checked via JMP PIN or extraction) |
| 1 | PHI2 | Input | 6502 system clock |
| 6 | RW | Input | Read/Write (1=read, 0=write) |
| 22-29 | D[7:0] | Bidirectional | 8-bit data bus |

**Notes:**
- Data bus uses GPIO 22-29 (consecutive pins required for PIO `in pins`/`out pins`)
- Control pins at GPIO 0, 1, 6 due to Feather RP2350 board constraints
- A0 is NOT used - status is implicit in the data stream (0xFF = not ready)

### Pin Extraction Overhead

Because control pins (GPIO 0, 1, 6) are not adjacent and data pins (GPIO 22-29) are far from GPIO 0, the PIO program must:
1. Read all 30 GPIO pins at once (`in pins, 30`)
2. Extract CS_N from bit 0
3. Extract RW from bit 6
4. Extract data from bits 22-29

This costs **6 extra PIO cycles** compared to an ideal pin arrangement.

### Optimal Pin Arrangement (For Reference)

If your board allows, this arrangement minimizes PIO overhead:

| GPIO | Signal | Why |
|------|--------|-----|
| 0 | RW | Bit 0 of combined CS_N/RW check |
| 1 | CS_N | Bit 1 of combined CS_N/RW check |
| 2+ | PHI2 | Can be anywhere |
| 22-29 | D[7:0] | Consecutive for PIO |

With RW at GPIO 0 and CS_N at GPIO 1, both can be extracted as a single 2-bit value, saving 4 cycles on writes and 3 cycles on reads.

## Protocol

### Writing Data (6502 → MCU)

```
CPU writes: [device] [length] [data...]
  - device: 0-127 (bit 7 must be 0)
  - length: 1-255 bytes
  - data: the payload
```

### Reading Data (MCU → 6502)

```
CPU writes: [device | 0x80]    (bit 7 = read request)
CPU reads:  [byte]
  - If byte == 0xFF: not ready, keep reading
  - If byte == 0x00-0xFE: this is the length, proceed
CPU reads:  [data...] (length bytes)
```

The 0xFF sentinel eliminates the need for a separate status register. The CPU simply polls by reading until it gets a non-0xFF value.

### 6502 Assembly Example

```asm
DATA = $DF00        ; Single register (directly memory-mapped)

; Write 4 bytes to device 5
write_example:
    LDA #5          ; Device 5, write mode (bit 7 = 0)
    STA DATA
    LDA #4          ; Length = 4 bytes
    STA DATA
    LDA #$01        ; Data byte 1
    STA DATA
    LDA #$02        ; Data byte 2
    STA DATA
    LDA #$03        ; Data byte 3
    STA DATA
    LDA #$04        ; Data byte 4
    STA DATA
    RTS

; Read from device 5
read_example:
    LDA #$85        ; Device 5 | 0x80 (read request)
    STA DATA

.wait_ready:
    LDA DATA        ; Read length (or 0xFF if not ready)
    CMP #$FF
    BEQ .wait_ready ; Keep polling until ready

    ; A = length (0-254)
    TAX
    BEQ .done       ; Zero length = nothing to read

.read_loop:
    LDA DATA
    STA (buffer),Y
    INY
    DEX
    BNE .read_loop

.done:
    RTS
```

## Building

```bash
cd pio_bus_interface
mkdir build && cd build
cmake ..
make
```

This produces two firmware images:
- `pio_bus_interface.uf2` - Full bidirectional loopback test
- `pio_bus_interface_rx_only.uf2` - Receive-only safe test (never drives bus)

Flash by holding BOOTSEL while connecting USB, then copy the .uf2 file.

## Timing Analysis

At 150MHz PIO clock (RP2350 default):

| Operation | PIO Cycles | Time | Notes |
|-----------|-----------|------|-------|
| Write (current) | 16 | 107ns | Includes pin extraction overhead |
| Read to data valid (current) | 15 | 100ns | Includes pin extraction overhead |
| Write (optimal pins) | 12 | 80ns | With RW@GPIO0, CS_N@GPIO1 |
| Read to data valid (optimal) | 12 | 80ns | With RW@GPIO0, CS_N@GPIO1 |

**Pin extraction overhead:** ~6 cycles (40ns) due to non-adjacent control pins.

### 6502 Compatibility

| 6502 Clock | PHI2 High | Write Margin (current) | Read Margin (current) |
|------------|-----------|------------------------|----------------------|
| 1 MHz | 500ns | 393ns | 400ns |
| 2 MHz | 250ns | 143ns | 150ns |
| 4 MHz | 125ns | 18ns | 25ns |
| 5 MHz | 100ns | -7ns ❌ | 0ns ⚠️ |

With optimal pin arrangement (40ns savings):
| 6502 Clock | PHI2 High | Write Margin (optimal) | Read Margin (optimal) |
|------------|-----------|------------------------|----------------------|
| 4 MHz | 125ns | 45ns | 45ns |
| 5 MHz | 100ns | 20ns | 20ns |
| 8 MHz | 62.5ns | -17.5ns ❌ | -17.5ns ❌ |

## How It Works

### Empty FIFO Returns 0xFF

The key insight: PIO's `pull noblock` instruction keeps OSR unchanged if the FIFO is empty. By initializing OSR to `0xFFFFFFFF`, empty reads naturally return `0xFF`.

```asm
do_read:
    pull noblock        ; Get from FIFO, or keep OSR if empty
    out pins, 8         ; Output byte (real data or 0xFF)
    ; ... enable outputs, wait for PHI2 low, disable outputs ...
    mov osr, ~null      ; Reset OSR to 0xFFFFFFFF for next empty read
```

### Pin Extraction (Current Implementation)

Because data is at GPIO 22-29 and control at GPIO 0, 1, 6:

```asm
    in pins, 30         ; Read GPIO 0-29 into ISR
    mov y, isr          ; Save for later
    in null, 32         ; Clear ISR

    ; Extract CS_N (bit 0)
    mov osr, y
    out x, 1            ; x = CS_N
    jmp x-- skip        ; Skip if not selected

    ; Extract RW (bit 6)
    mov osr, y
    out null, 6         ; Discard bits 0-5
    out x, 1            ; x = RW
    jmp x-- do_read     ; RW=1 -> read

do_write:
    ; Extract data (bits 22-29)
    mov osr, y
    out null, 22        ; Discard bits 0-21
    out isr, 8          ; Get data bits
    push
```

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                         RP2350                              │
│                                                             │
│   ┌─────────────┐    ┌─────────────┐    ┌─────────────┐   │
│   │  PIO SM0    │◄──►│  DMA RX     │───►│  Device     │   │
│   │             │    │  Channel    │    │  Buffers    │   │
│   │  Read/Write │    ├─────────────┤    │  [0..127]   │   │
│   │             │◄──►│  DMA TX     │◄───│             │   │
│   └──────┬──────┘    └─────────────┘    └─────────────┘   │
│          │                                                  │
└──────────┼──────────────────────────────────────────────────┘
           │
    ┌──────┴──────┐
    │  6502 Bus   │
    │  D[7:0]     │ ← GPIO 22-29
    │  PHI2       │ ← GPIO 1
    │  CS_N       │ ← GPIO 0
    │  RW         │ ← GPIO 6
    └─────────────┘
```

## Receive-Only Safe Test Mode

The `pio_bus_interface_rx_only` firmware is for safe initial testing:
- **Never drives the data bus** - only monitors CPU writes
- Logs all received bytes to USB serial as hex dump
- Use this first to verify wiring and timing without risk of bus contention

## Comparison to Original Design

| Aspect | CPLD + Latches | PIO (current) | PIO (optimal pins) |
|--------|----------------|---------------|-------------------|
| External chips | 4 | 1 (addr decode) | 1 (addr decode) |
| GPIO pins | 13 | 11 | 11 |
| Write cycles | — | 16 | **12** |
| Read cycles | — | 15 | **12** |
| Max 6502 clock | — | ~4 MHz | **~5 MHz** |
| Status mechanism | Hardware | Implicit (0xFF) | Implicit (0xFF) |
| Max response size | Unlimited | 254 bytes | 254 bytes |

## Limitations

- Maximum response length is 254 bytes (0xFF reserved for "not ready")
- 128 device addresses (bit 7 used for read/write flag)
- Single PIO state machine (one bus interface per PIO block)
- Current pin arrangement limits 6502 clock to ~4 MHz (vs ~5 MHz with optimal pins)

## Files

- `bus_interface.pio` - Full bidirectional PIO program
- `bus_interface_rx_only.pio` - Receive-only PIO program (safe test)
- `bus_interface.c/h` - C driver with device buffers
- `main.c` - Loopback test firmware
- `main_rx_only.c` - Receive-only test firmware
