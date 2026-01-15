# PIO-Based 6502 Bus Interface

This project implements a memory-mapped register interface between a 6502 CPU and an RP2040 microcontroller using only PIO (Programmable I/O) and DMA, eliminating the need for external CPLD and latches.

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
| 9 | CS_N | Input | Chip select (directly to JMP PIN) |
| 10 | A0 | Input | Address bit 0 (register select) |
| 11 | RW | Input | Read/Write (1=read, 0=write) |
| 16 | UART TX | Output | Debug console |
| 17 | UART RX | Input | Debug console |

## Register Map (6502 View)

| A0 | R/W | Name | Description |
|----|-----|------|-------------|
| 0 | R | DATA | Read byte from MCU |
| 0 | W | DATA | Write byte to MCU |
| 1 | R | STATUS | D7=TX_AVAIL (1=data available) |
| 1 | W | — | Reserved (no operation) |

## Protocol

### Writing Data (6502 → MCU)

```
6502 sends:
  [device]    - Device number 0-127 (bit 7 = 0)
  [length]    - Number of bytes to write (1-255)
  [data...]   - The data bytes
```

### Reading Data (MCU → 6502)

```
6502 sends:
  [device|0x80] - Device number with bit 7 set (read request)
  [length]      - Number of bytes to read

6502 reads:
  [data...]     - The requested bytes
```

### 6502 Assembly Example

```asm
DATA    = $DF00     ; Base address (CS active when A15-A8 = $DF)
STATUS  = $DF01

; Write 4 bytes to device 5
write_example:
    ; Wait for TX_AVAIL (optional, ensures link is ready)
    LDA #5              ; Device 5, write mode
    JSR send_byte
    LDA #4              ; Length = 4
    JSR send_byte
    LDA #$01            ; Data bytes
    JSR send_byte
    LDA #$02
    JSR send_byte
    LDA #$03
    JSR send_byte
    LDA #$04
    JSR send_byte
    RTS

; Read 4 bytes from device 5
read_example:
    LDA #$85            ; Device 5 | 0x80 (read mode)
    JSR send_byte
    LDA #4              ; Request 4 bytes
    JSR send_byte
    ; Now read 4 bytes back
    JSR recv_byte       ; First byte
    STA buffer+0
    JSR recv_byte
    STA buffer+1
    JSR recv_byte
    STA buffer+2
    JSR recv_byte
    STA buffer+3
    RTS

; Send byte (wait for ready, then write)
send_byte:
    PHA
.wait:
    ; For writes, we assume MCU can keep up (DMA drains fast)
    PLA
    STA DATA
    RTS

; Receive byte (wait for available, then read)
recv_byte:
.wait:
    LDA STATUS
    BPL .wait           ; Wait until bit 7 (TX_AVAIL) is set
    LDA DATA
    RTS
```

## Loopback Test

The included firmware implements a simple loopback test:
- Data written to device N is stored in that device's buffer
- When reading from device N, the stored data is returned

This allows testing the full data path without any external 6502 hardware.

## Building

```bash
cd pio_bus_interface
mkdir build
cd build
cmake ..
make
```

The output `pio_bus_interface.uf2` can be flashed to a Pico by holding BOOTSEL and connecting USB.

## Timing Analysis

At 125MHz PIO clock with a 5MHz 6502:

| Operation | PIO Cycles | Time | PHI2 High | Margin |
|-----------|-----------|------|-----------|--------|
| Data Read | 10 | 80ns | 100ns | 20ns |
| Status Read | 12 | 96ns | 100ns | 4ns |
| Data Write | 8 | 64ns | 100ns | 36ns |

For more conservative timing, consider:
- Running 6502 at 4MHz (125ns PHI2 high)
- Overclocking RP2040 to 150MHz+ (reduces PIO cycle time)

## Status Register Implementation

The key insight enabling this design is using PIO's `mov osr, ~status` instruction:

- Configure `EXECCTRL.STATUS_SEL = 0` (TX FIFO level)
- Configure `EXECCTRL.STATUS_N = 1` (threshold = 1)
- `status` = all-1s if TX FIFO empty, all-0s if TX has data
- `~status` = all-0s if empty, all-1s if has data

This maps directly to the TX_AVAIL semantics without any ARM core involvement.

The RX_READY signal is eliminated because DMA can drain the RX FIFO faster than the 6502 can fill it (125MB/s DMA vs ~1MB/s max 6502 write rate).

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                         RP2040                               │
│                                                              │
│   ┌─────────────┐    ┌─────────────┐    ┌─────────────┐    │
│   │  PIO SM0    │◄──►│  DMA RX     │───►│  Device     │    │
│   │             │    │  Channel    │    │  Buffers    │    │
│   │  Bus        │    ├─────────────┤    │  [0..255]   │    │
│   │  Interface  │◄──►│  DMA TX     │◄───│             │    │
│   │             │    │  Channel    │    │             │    │
│   └──────┬──────┘    └─────────────┘    └─────────────┘    │
│          │                                                   │
└──────────┼───────────────────────────────────────────────────┘
           │
    ┌──────┴──────┐
    │  6502 Bus   │
    │  D[7:0]     │
    │  PHI2       │
    │  CS_N       │
    │  A0, RW     │
    └─────────────┘
```

## Limitations

- Status register only reports TX_AVAIL (no RX_READY)
- Tight timing at 5MHz - may need margin testing
- Single PIO state machine limits to one bus interface per PIO block
- 4-word PIO FIFO depth (8 with joining, but we need both directions)

## Future Enhancements

- Interrupt output (directly from PIO sideset) when TX_AVAIL
- Multiple state machines for different address ranges
- RP2350 port with additional PIO features
