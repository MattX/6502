# 6502-to-MCU Bus Interface CPLD Specification

## Overview

This document specifies the logic for an ATF750LVC CPLD that interfaces a 65C02 CPU (3.3V, up to 5MHz) with an RP2040 microcontroller. The interface provides two 8-bit data registers (TX and RX) with full handshaking, allowing asynchronous communication between the CPU and MCU without requiring the MCU to meet 6502 bus timing.

## Design Goals

1. Electrically safe if MCU is slower than CPU bus cycle
2. No data loss: full handshake protocol prevents overrun in either direction
3. Simple polling interface for the 6502 (memory-mapped registers)
4. Minimize MCU pin count (current design: 13 GPIO)

## System Components

| Component | Function |
|-----------|----------|
| ATF750LVC CPLD | Bus control logic, handshake state |
| 74HC574 #1 (TX Latch) | Holds data from MCU for CPU to read |
| 74HC574 #2 (RX Latch) | Holds data from CPU for MCU to read |
| 74HC574 #3 (Status Latch) | Drives status bits onto data bus |

---

## Signal Definitions

### 6502 Bus Inputs (to CPLD)

| Signal | Dir | Active | Description |
|--------|-----|--------|-------------|
| PHI2 | in | — | 6502 system clock |
| CS_N | in | low | Chip select from address decoder |
| RW | in | — | Read/Write: 1=read, 0=write |
| A0 | in | — | Register select: 0=data (RX/TX), 1=status |

### MCU Interface Signals

| Signal | Dir | Active | Description |
|--------|-----|--------|-------------|
| MCU_DATA[7:0] | bidir | — | Shared 8-bit data bus to MCU |
| TX_LOAD | in | rising | MCU pulses to load TX latch |
| RX_ACK | in | rising | MCU pulses to acknowledge RX read |
| DATA_TAKEN | out | high | TX data has been read by CPU |
| DATA_WRITTEN | out | high | RX data is available for MCU |

### Control Outputs (to latches/buffers)

| Signal | Dir | Active | Description |
|--------|-----|--------|-------------|
| TX_OE_N | out | low | Enable TX latch outputs onto 6502 data bus |
| RX_CLK | out | rising | Clock CPU write data into RX latch |
| STATUS_OE_N | out | low | Enable status latch outputs onto 6502 data bus |
| STATUS_CLK | out | rising | Clock status bits into status latch |
| TX_AVAIL | out | high | Internal flag: TX register has data for CPU |
| RX_READY | out | high | Internal flag: RX register is empty, CPU can write |

---

## Register Map (CPU View)

Base address determined by external address decoder driving CS_N.

| A0 | R/W | Name | Description |
|----|-----|------|-------------|
| 0 | R | TX | Read byte from MCU (clears TX_AVAIL) |
| 0 | W | RX | Write byte to MCU (clears RX_READY until MCU ACKs) |
| 1 | R | STATUS | Bit 7: TX_AVAIL, Bit 6: RX_READY, Bits 5-0: 0 |
| 1 | W | — | No effect (reserved) |

---

## Status Register Format

```
Bit 7 (D7): TX_AVAIL  — 1 = MCU has sent a byte, CPU can read TX register
Bit 6 (D6): RX_READY  — 1 = RX register is empty, CPU may write
Bits 5-0:   Reserved  — Always read as 0 (tied low on status latch inputs)
```

---

## Handshake Protocol

### MCU → CPU (TX Path)

```
MCU                         CPLD                        CPU
 │                            │                           │
 │  1. Place byte on bus      │                           │
 │  2. Pulse TX_LOAD ────────>│                           │
 │                            │ (latch captures data)     │
 │                            │ Set TX_AVAIL=1            │
 │                            │ Set DATA_TAKEN=0          │
 │                            │                           │
 │                            │              ┌────────────│ 3. Poll STATUS
 │                            │              │            │    until TX_AVAIL=1
 │                            │<─────────────┘            │
 │                            │                           │
 │                            │              ┌────────────│ 4. Read TX register
 │                            │<─────────────┘            │    (A0=0, RW=1)
 │                            │ (TX_OE_N asserts)         │
 │                            │ Set DATA_TAKEN=1          │
 │                            │ Set TX_AVAIL=0            │
 │                            │                           │
 │     ┌──────────────────────│                           │
 │     │ 5. See DATA_TAKEN=1  │                           │
 │<────┘    (can load next)   │                           │
 │                            │                           │
 │  6. Pulse TX_LOAD ────────>│                           │
 │     (DATA_TAKEN clears)    │                           │
```

**Summary:**
1. MCU places data on bus, pulses TX_LOAD
2. CPLD sets TX_AVAIL=1, DATA_TAKEN=0
3. CPU polls status, sees TX_AVAIL=1
4. CPU reads TX register → CPLD sets DATA_TAKEN=1, TX_AVAIL=0
5. MCU sees DATA_TAKEN=1, knows byte was received
6. MCU's next TX_LOAD clears DATA_TAKEN

### CPU → MCU (RX Path)

```
CPU                         CPLD                        MCU
 │                            │                           │
 │  1. Poll STATUS ──────────>│                           │
 │     until RX_READY=1       │                           │
 │                            │                           │
 │  2. Write RX register ────>│                           │
 │     (A0=0, RW=0)           │                           │
 │                            │ (RX_CLK pulses, latch     │
 │                            │  captures data)           │
 │                            │ Set RX_READY=0            │
 │                            │ Set DATA_WRITTEN=1        │
 │                            │                           │
 │                            │     ┌─────────────────────│ 3. See DATA_WRITTEN=1
 │                            │<────┘                     │
 │                            │                           │ 4. Read byte from
 │                            │                           │    RX latch (MCU_DIR)
 │                            │              ┌────────────│ 5. Pulse RX_ACK
 │                            │<─────────────┘            │
 │                            │ Set DATA_WRITTEN=0        │
 │                            │ Set RX_READY=1            │
 │                            │                           │
 │  6. Poll STATUS ──────────>│                           │
 │     RX_READY=1, can write  │                           │
```

**Summary:**
1. CPU polls status until RX_READY=1
2. CPU writes RX register → CPLD clocks RX latch, sets DATA_WRITTEN=1, RX_READY=0
3. MCU sees DATA_WRITTEN=1
4. MCU enables RX latch output (MCU_DIR), reads byte
5. MCU pulses RX_ACK → CPLD clears DATA_WRITTEN, sets RX_READY=1
6. CPU can now write again

---

## Detailed Logic Behavior

### Internal State Registers

| Register | Set Condition | Clear Condition | Power-On State |
|----------|---------------|-----------------|----------------|
| TX_AVAIL | Rising edge of TX_LOAD | CPU reads TX register | 0 |
| RX_READY | Rising edge of RX_ACK | CPU writes RX register | 1 |
| DATA_TAKEN | CPU reads TX register | Rising edge of TX_LOAD | 0 |
| DATA_WRITTEN | CPU writes RX register | Rising edge of RX_ACK | 0 |

### Derived Signals for State Changes

```
TX_READ (internal) = CS_N=0 AND A0=0 AND RW=1 AND PHI2=1
    "CPU is reading the TX data register right now"

RX_WRITE (internal) = CS_N=0 AND A0=0 AND RW=0 AND PHI2=1
    "CPU is writing the RX data register right now"
```

### Output Signal Equations

```
TX_OE_N active (low) when:
    PHI2=1 AND CS_N=0 AND A0=0 AND RW=1
    (CPU reading TX data register)

RX_CLK:
    Rising edge when PHI2 rises while (CS_N=0 AND A0=0 AND RW=0)
    Implementation: RX_CLK = PHI2 AND !CS_N AND !A0 AND !RW
    (The latch clocks on the rising edge of this signal)
    
STATUS_OE_N active (low) when:
    PHI2=1 AND CS_N=0 AND A0=1 AND RW=1
    (CPU reading status register)

STATUS_CLK:
    Can be tied directly to PHI2, or to inverted PHI2
    Status latch captures TX_AVAIL and RX_READY continuously
    Using PHI2 falling edge ensures status is stable before next CPU read

TX_AVAIL:
    D flip-flop: SET on TX_LOAD rising, CLEAR on TX_READ
    Output drives status latch D7 input

RX_READY:
    D flip-flop: SET on RX_ACK rising, CLEAR on RX_WRITE
    Output drives status latch D6 input
    Must power-on to 1 (CPU can write immediately)

DATA_TAKEN:
    D flip-flop: SET on TX_READ, CLEAR on TX_LOAD rising
    Output to MCU GPIO

DATA_WRITTEN:
    D flip-flop: SET on RX_WRITE, CLEAR on RX_ACK rising
    Output to MCU GPIO
```

### Bus Contention Prevention

The CPLD guarantees that TX_OE_N and STATUS_OE_N are never simultaneously active:

```
TX_OE_N requires:      A0=0
STATUS_OE_N requires:  A0=1
```

These are mutually exclusive by definition.

The RX latch outputs are enabled by MCU_DIR from the RP2040 (not controlled by the CPLD). The MCU is responsible for not asserting MCU_DIR while it is loading the TX latch, which is straightforward in firmware.

---

## Timing Considerations

### 6502 Bus Timing (5MHz, 200ns cycle)

| Parameter | Value | Notes |
|-----------|-------|-------|
| PHI2 high time | ~100ns | Data must be valid, transactions occur |
| PHI2 low time | ~100ns | Address/RW setup |
| Data setup before PHI2↓ | 30ns min | For CPU reads |
| Data hold after PHI2↓ | 10ns min | For CPU reads |

### CPLD + Latch Timing Budget

| Path | Delay | Margin |
|------|-------|--------|
| PHI2↑ → CPLD decode → TX_OE_N↓ | ~10ns | — |
| TX_OE_N↓ → 74HC574 output valid | ~15ns | — |
| **Total** | ~25ns | 75ns margin in 100ns window ✓ |

### RX Write Timing

The CPU presents data before PHI2↑. The CPLD generates RX_CLK from (PHI2 AND decode). The 74HC574 captures data on RX_CLK rising edge. Data has been stable since PHI2↓ of the previous half-cycle (~100ns), far exceeding the ~15ns setup requirement.

---

## Tentative Pin Assignments (ATF750LVC, PLCC28)

*Adjust based on PCB routing. Pin numbers for PLCC28 package.*

### Inputs

| Pin | Signal | Notes |
|-----|--------|-------|
| 2 | PHI2 | System clock |
| 3 | CS_N | From address decoder |
| 4 | RW | 6502 R/W̅ signal |
| 5 | A0 | Register select |
| 6 | TX_LOAD | From MCU |
| 7 | RX_ACK | From MCU |

### Outputs

| Pin | Signal | Notes |
|-----|--------|-------|
| 15 | TX_OE_N | To TX latch pin 1 (OE̅) — combinatorial |
| 16 | RX_CLK | To RX latch pin 11 (CLK) — combinatorial |
| 17 | STATUS_OE_N | To status latch pin 1 (OE̅) — combinatorial |
| 18 | TX_AVAIL | To status latch pin 18 (D7) — registered |
| 19 | RX_READY | To status latch pin 17 (D6) — registered |
| 20 | DATA_TAKEN | To MCU GPIO — registered |
| 21 | DATA_WRITTEN | To MCU GPIO — registered |
| 26 | STATUS_CLK | To status latch pin 11 (CLK) — combinatorial |

### Power

| Pin | Signal |
|-----|--------|
| 14 | GND |
| 28 | VCC (3.3V) |

---

## External Wiring

### TX Latch (74HC574 #1 — MCU → CPU)

| 74HC574 Pin | Signal | Source |
|-------------|--------|--------|
| 1 (OE̅) | TX_OE_N | CPLD pin 15 |
| 2-9 (D0-D7) | MCU_DATA[0:7] | MCU GPIO |
| 11 (CLK) | TX_LOAD | MCU GPIO |
| 12-19 (Q0-Q7) | D[0:7] | 6502 data bus |
| 10 | GND | — |
| 20 | VCC | 3.3V |

### RX Latch (74HC574 #2 — CPU → MCU)

| 74HC574 Pin | Signal | Source |
|-------------|--------|--------|
| 1 (OE̅) | MCU_DIR | MCU GPIO (assert low to read) |
| 2-9 (D0-D7) | D[0:7] | 6502 data bus |
| 11 (CLK) | RX_CLK | CPLD pin 16 |
| 12-19 (Q0-Q7) | MCU_DATA[0:7] | MCU GPIO |
| 10 | GND | — |
| 20 | VCC | 3.3V |

### Status Latch (74HC574 #3)

| 74HC574 Pin | Signal | Source |
|-------------|--------|--------|
| 1 (OE̅) | STATUS_OE_N | CPLD pin 17 |
| 9 (D7) | TX_AVAIL | CPLD pin 18 |
| 8 (D6) | RX_READY | CPLD pin 19 |
| 2-7 (D0-D5) | GND | tied low |
| 11 (CLK) | STATUS_CLK | CPLD pin 26 |
| 12-19 (Q0-Q7) | D[0:7] | 6502 data bus |
| 10 | GND | — |
| 20 | VCC | 3.3V |

---

## MCU GPIO Summary (RP2040)

| Signal | Direction | Description |
|--------|-----------|-------------|
| MCU_DATA[7:0] | bidir | Shared data bus |
| TX_LOAD | output | Pulse to load TX latch |
| RX_ACK | output | Pulse to acknowledge RX byte |
| MCU_DIR | output | Enable RX latch outputs (active low) |
| DATA_TAKEN | input | High = CPU read TX byte |
| DATA_WRITTEN | input | High = CPU wrote RX byte |

**Total: 8 (data) + 5 (control) = 13 GPIO**

---

## 6502 Software Interface

Assume: DATA = $xxxx (CS active, A0=0), STATUS = $xxxx+1 (CS active, A0=1)

### Receiving from MCU (TX Path)

```asm
; Wait for MCU to send a byte, then read it
receive_byte:
    LDA STATUS          ; Read status register
    BMI .got_data       ; Branch if bit 7 (TX_AVAIL) set
    BRA receive_byte    ; Keep polling (use JMP on NMOS)
.got_data:
    LDA DATA            ; Read TX register — clears TX_AVAIL, sets DATA_TAKEN
    RTS                 ; Byte in A
```

### Sending to MCU (RX Path)

```asm
; Send byte in A to MCU
send_byte:
    PHA                 ; Save byte
.wait_ready:
    LDA STATUS          ; Read status register
    AND #$40            ; Mask bit 6 (RX_READY)
    BEQ .wait_ready     ; Loop until ready
    PLA                 ; Restore byte
    STA DATA            ; Write RX register — clears RX_READY, sets DATA_WRITTEN
    RTS
```

### Non-Blocking Check

```asm
; Check if data available from MCU (non-blocking)
; Returns: C=1 if data available, A=byte; C=0 if no data
check_receive:
    LDA STATUS
    BMI .available
    CLC
    RTS
.available:
    LDA DATA
    SEC
    RTS
```

---

## MCU Firmware Interface (RP2040, Pseudocode)

```c
// Pin definitions
#define DATA_PINS       0   // GPIO 0-7
#define TX_LOAD_PIN     8
#define RX_ACK_PIN      9
#define MCU_DIR_PIN     10
#define DATA_TAKEN_PIN  11
#define DATA_WRITTEN_PIN 12

// Send byte to 6502
void send_to_cpu(uint8_t byte) {
    // Wait for previous byte to be taken
    while (!gpio_get(DATA_TAKEN_PIN)) { }
    
    // Set data bus to output, drive data
    gpio_set_dir_out_masked(0xFF);
    gpio_put_masked(0xFF, byte);
    
    // Pulse TX_LOAD
    gpio_put(TX_LOAD_PIN, 1);
    // Small delay if needed
    gpio_put(TX_LOAD_PIN, 0);
    
    // Set data bus back to input (high-Z)
    gpio_set_dir_in_masked(0xFF);
}

// Receive byte from 6502
uint8_t receive_from_cpu(void) {
    // Wait for data
    while (!gpio_get(DATA_WRITTEN_PIN)) { }
    
    // Enable RX latch outputs
    gpio_put(MCU_DIR_PIN, 0);  // Active low
    
    // Small delay for latch output to stabilize
    // Read data
    uint8_t byte = gpio_get_all() & 0xFF;
    
    // Disable RX latch outputs
    gpio_put(MCU_DIR_PIN, 1);
    
    // Acknowledge
    gpio_put(RX_ACK_PIN, 1);
    // Small delay
    gpio_put(RX_ACK_PIN, 0);
    
    return byte;
}
```

---

## Open Questions / Future Considerations

1. **Reducing to 8 MCU pins:** Would require multiplexing control signals or using a serial link between CPLD and MCU. Adds complexity.

2. **Interrupt support:** Could add an active-low IRQ output from CPLD that asserts when TX_AVAIL=1 or DATA_WRITTEN=1, allowing interrupt-driven I/O on the 6502 side.

3. **FIFO extension:** Could add external FIFOs (e.g., 74HC40105) for deeper buffering, though this significantly increases complexity.

4. **Reset synchronization:** Consider adding a reset input to initialize RX_READY=1 and clear other state.

---

## Revision History

| Rev | Date | Changes |
|-----|------|---------|
| 0.1 | — | Initial specification |
