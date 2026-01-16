# PIO-Based 6502 Bus Interface

This project implements a byte-stream interface between a 6502 CPU and an RP2040 microcontroller using only PIO and DMA, eliminating the need for external CPLD and latches.

## Overview

The traditional approach (see `mcu_interface/`) requires:
- 1x CPLD (ATF750LVC) for bus timing and handshake logic
- 3x 74HC574 latches (TX, RX, Status)

This PIO-based approach requires:
- **No external logic ICs** - just direct GPIO connections
- External address decode logic (e.g., 74HC138) to generate CS_N

## Pin Mapping

| GPIO | Signal | Direction | Description |
|------|--------|-----------|-------------|
| 0-7 | D[7:0] | Bidirectional | 8-bit data bus |
| 8 | PHI2 | Input | 6502 system clock |
| 9 | CS_N | Input | Chip select |
| 10 | RW | Input | Read/Write (1=read, 0=write) |
| 16 | UART TX | Output | Debug console |
| 17 | UART RX | Input | Debug console |

**Note:** A0 is NOT used. Status is implicit in the data stream (0xFF = not ready).

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

## Loopback Test

The included firmware implements a loopback test:
- Data written to device N is stored in that device's buffer
- When reading from device N, the stored data is returned

## Building

```bash
cd pio_bus_interface
mkdir build && cd build
cmake ..
make
```

Flash `pio_bus_interface.uf2` to a Pico by holding BOOTSEL while connecting USB.

## Timing Analysis

At 125MHz PIO clock:

| Operation | PIO Cycles | Time | Notes |
|-----------|-----------|------|-------|
| Read | 10 | 80ns | Includes output enable |
| Write | 8 | 64ns | Fastest path |

**Works reliably at 5MHz 6502** (100ns PHI2 high, 20ns margin for reads).

| 6502 Clock | PHI2 High | Read Margin |
|------------|-----------|-------------|
| 5MHz | 100ns | 20ns |
| 4MHz | 125ns | 45ns |
| 8MHz | 62.5ns | -17.5ns (too fast) |

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

### No Status Register Needed

The protocol embeds status in the data stream:
- **Write path**: CPU writes freely (DMA drains FIFO faster than 6502 can fill it)
- **Read path**: CPU polls by reading; 0xFF means "not ready", anything else is the response length

This eliminates:
- The A0 address bit
- The status register PIO path
- The `mov osr, ~status` mechanism

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                         RP2040                               │
│                                                              │
│   ┌─────────────┐    ┌─────────────┐    ┌─────────────┐    │
│   │  PIO SM0    │◄──►│  DMA RX     │───►│  Device     │    │
│   │  (18 instr) │    │  Channel    │    │  Buffers    │    │
│   │             │    ├─────────────┤    │  [0..127]   │    │
│   │  Read/Write │◄──►│  DMA TX     │◄───│             │    │
│   │  only       │    │  Channel    │    │             │    │
│   └──────┬──────┘    └─────────────┘    └─────────────┘    │
│          │                                                   │
└──────────┼───────────────────────────────────────────────────┘
           │
    ┌──────┴──────┐
    │  6502 Bus   │
    │  D[7:0]     │
    │  PHI2, CS_N │
    │  RW         │
    └─────────────┘
```

## Comparison to Original Design

| Aspect | CPLD + Latches | PIO (with A0) | PIO (simplified) |
|--------|----------------|---------------|------------------|
| External chips | 4 | 1 (addr decode) | 1 (addr decode) |
| GPIO pins | 13 | 12 | **11** |
| PIO instructions | — | 29 | **18** |
| Read path (cycles) | — | 12 | **10** |
| Status mechanism | Hardware | `~status` | **Implicit (0xFF)** |
| Max response size | Unlimited | Unlimited | **254 bytes** |

## Limitations

- Maximum response length is 254 bytes (0xFF reserved for "not ready")
- 128 device addresses (bit 7 used for read/write flag)
- Single PIO state machine (one bus interface per PIO block)
