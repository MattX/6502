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
| 0 | RW | Input | Read/Write (1=read, 0=write) |
| 1 | CS_N | Input | Chip select (active low) |
| 8 | PHI2 | Input | 6502 system clock |
| 22-29 | D[7:0] | Bidirectional | 8-bit data bus |

**Optimization notes:**
- RW and CS_N are at GPIO 0-1 for fast 2-bit combined extraction
- Data bus uses GPIO 22-29 (consecutive pins required for PIO `in pins`/`out pins`)
- A0 is NOT used - status is implicit in the data stream (0xFF = not ready)

### Combined CS_N/RW Extraction

By placing RW at GPIO 0 and CS_N at GPIO 1, both signals can be extracted and checked with a single 2-bit operation:

```asm
out x, 2                ; x = {CS_N, RW} as 2-bit value
; x=0: CS_N=0, RW=0 -> write, selected
; x=1: CS_N=0, RW=1 -> read, selected
; x=2: CS_N=1, RW=0 -> not selected
; x=3: CS_N=1, RW=1 -> not selected
```

This saves 4 cycles compared to extracting each signal separately.

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
| Write | 12 | 80ns | From PHI2 high to FIFO push |
| Read to data valid | 12 | 80ns | From PHI2 high to data on bus |
| Not selected | 8 | 53ns | Skip to wait for PHI2 low |

### 6502 Compatibility

| 6502 Clock | PHI2 High | Write Margin | Read Margin |
|------------|-----------|--------------|-------------|
| 1 MHz | 500ns | 420ns ✓ | 420ns ✓ |
| 2 MHz | 250ns | 170ns ✓ | 170ns ✓ |
| 4 MHz | 125ns | 45ns ✓ | 45ns ✓ |
| 5 MHz | 100ns | 20ns ✓ | 20ns ✓ |
| 8 MHz | 62.5ns | -17.5ns ❌ | -17.5ns ❌ |

**Practical maximum: ~5-6 MHz** with comfortable margins.

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

### PIO Program Flow

```asm
wait_cycle:
    wait 1 gpio PHI2        ; 1 - Wait for PHI2 high
    in pins, 30             ; 2 - Read all GPIO pins
    mov y, isr              ; 3 - Save for later
    in null, 32             ; 4 - Clear ISR
    mov osr, y              ; 5
    out x, 2                ; 6 - x = {CS_N, RW}
    jmp x-- not_write       ; 7 - x=0 -> write

do_write:
    mov osr, y              ; 8
    out null, 22            ; 9  - Discard bits 0-21
    out isr, 8              ; 10 - Get D[7:0]
    push                    ; 11
    jmp wait_phi2_low       ; 12

not_write:
    jmp x-- wait_phi2_low   ; 8 - x>=2 -> not selected

do_read:
    pull noblock            ; 9
    out pins, 8             ; 10
    mov osr, ~null          ; 11
    out pindirs, 8          ; 12 - Enable outputs
    wait 0 gpio PHI2        ; Wait for PHI2 low
    ; ... disable outputs, reset sentinel ...
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
    │  PHI2       │ ← GPIO 8
    │  RW         │ ← GPIO 0
    │  CS_N       │ ← GPIO 1
    └─────────────┘
```

## Receive-Only Safe Test Mode

The `pio_bus_interface_rx_only` firmware is for safe initial testing:
- **Never drives the data bus** - only monitors CPU writes
- Logs all received bytes to USB serial as hex dump
- Use this first to verify wiring and timing without risk of bus contention

## Comparison to Original Design

| Aspect | CPLD + Latches | PIO |
|--------|----------------|-----|
| External chips | 4 | 1 (addr decode only) |
| GPIO pins | 13 | 11 |
| Write cycles | — | **12** |
| Read cycles | — | **12** |
| Max 6502 clock | — | **~5-6 MHz** |
| Status mechanism | Hardware | Implicit (0xFF sentinel) |
| Max response size | Unlimited | 254 bytes |

## Limitations

- Maximum response length is 254 bytes (0xFF reserved for "not ready")
- 128 device addresses (bit 7 used for read/write flag)
- Single PIO state machine (one bus interface per PIO block)

## Files

- `bus_interface.pio` - Full bidirectional PIO program
- `bus_interface_rx_only.pio` - Receive-only PIO program (safe test)
- `bus_interface.c/h` - C driver with device buffers
- `main.c` - Loopback test firmware
- `main_rx_only.c` - Receive-only test firmware
