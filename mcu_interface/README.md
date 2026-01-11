# 6502-to-MCU Bus Interface

This module provides a bidirectional communication interface between a 65C02 CPU and an RP2040 microcontroller. It implements two 8-bit data registers (TX and RX) with full handshaking, allowing asynchronous communication without requiring the MCU to meet 6502 bus timing.

## Overview

| Direction | Register | Description |
|-----------|----------|-------------|
| MCU → CPU | TX | MCU writes data, CPU reads via memory-mapped register |
| CPU → MCU | RX | CPU writes via memory-mapped register, MCU reads data |

The interface uses a simple handshake protocol with status flags to prevent data loss in either direction.

## Register Map (CPU View)

| A0 | R/W | Name | Description |
|----|-----|------|-------------|
| 0 | R | TX | Read byte from MCU |
| 0 | W | RX | Write byte to MCU |
| 1 | R | STATUS | Bit 7: TX_AVAIL, Bit 6: RX_READY |

## Implementations

### CPLD/ - ATF750LVC / ATF22V10C

CUPL source for Microchip/Atmel SPLDs. Active development used the ATF750LVC in DIP24 package; the design also fits on an ATF22V10C.

**Files:**
- `mcu_interface.pld` - CUPL source
- `mcu_interface.si` - Simulation test vectors

**Requires external components:**
- 3x 74HC574 latches (TX, RX, Status)

**Pin assignments (DIP24):**
| Pin | Signal | Pin | Signal |
|-----|--------|-----|--------|
| 1 | CLK (tie low) | 14 | TX_OE_N |
| 2 | PHI2 | 15 | RX_CLK |
| 3 | CS_N | 16 | STATUS_OE_N |
| 4 | RW | 17 | STATUS_CLK |
| 5 | A0 | 18 | TX_AVAIL |
| 6 | TX_LOAD | 19 | RX_READY |
| 7 | RX_ACK | 20 | DATA_TAKEN |
| 12 | GND | 21 | DATA_WRITTEN |
| 24 | VCC | | |

### FPGA/ - Basic FPGA Version

Verilog implementation for Xilinx Artix-7 (tested on Digilent Cmod A7). Direct translation of the CPLD logic.

**Files:**
- `mcu_interface.v` - Verilog source
- `mcu_interface.xdc` - Constraints (adjust pin assignments)
- `mcu_interface_tb.v` - Testbench

**Requires external components:**
- 3x 74HC574 latches (TX, RX, Status)

**I/O count:** 14 pins

### FPGA_integrated/ - Integrated FPGA Version

Verilog implementation with TX, RX, and Status registers built into the FPGA. Eliminates all external latches.

**Files:**
- `mcu_interface_integrated.v` - Verilog source
- `mcu_interface_integrated.xdc` - Constraints (adjust pin assignments)
- `mcu_interface_integrated_tb.v` - Testbench

**Requires external components:**
- None

**I/O count:** 25 pins (two 8-bit data buses + control)

## MCU Interface Signals

| Signal | Dir | Description |
|--------|-----|-------------|
| MCU_D[7:0] | bidir | Shared data bus (integrated version only) |
| TX_LOAD | out | Pulse to load TX register |
| RX_ACK | out | Pulse to acknowledge RX read |
| MCU_OE_N | out | Assert low to read RX (integrated) / MCU_DIR (CPLD) |
| DATA_TAKEN | in | High = CPU read TX byte |
| DATA_WRITTEN | in | High = CPU wrote RX byte |

## Initialization

The RX_READY flag powers up as 0. MCU firmware must pulse RX_ACK at startup to initialize RX_READY to 1, allowing the CPU to write immediately.

## Documentation

See `6502_mcu_interface_spec.md` for the full specification including:
- Detailed handshake protocol diagrams
- Timing analysis
- External wiring tables
- Example 6502 assembly code
- Example MCU C code
