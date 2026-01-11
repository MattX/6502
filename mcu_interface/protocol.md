# CPU-MCU Communication Protocol

This document describes the message protocol used between the 65C02 CPU and the RP2040 MCU over the hardware interface defined in `6502_mcu_interface_spec.md`.

## Overview

The protocol multiplexes multiple logical devices (USB keyboard, SPI peripherals, etc.) over a single byte-oriented channel. All communication is initiated by the CPU, with the MCU responding to commands. Asynchronous events are handled via an interrupt mechanism.

## Transport Layer

The underlying hardware provides:
- **TX register**: MCU → CPU (CPU reads)
- **RX register**: CPU → MCU (CPU writes)
- **Status flags**: TX_AVAIL (data ready to read), RX_READY (safe to write)

All protocol bytes are transferred one at a time. The CPU must check the appropriate status flag before each byte transfer:
- Check RX_READY before writing
- Check TX_AVAIL before reading

## Command Format

### Write Command

CPU writes data to a device.

```
CPU sends: [0x00 | device_id] [length] [data bytes...]
```

| Byte | Description |
|------|-------------|
| 0 | `0x00 \| device_id` — Bit 7 = 0 (write), bits 6-0 = device ID (1-127) |
| 1 | Length N (1-254 bytes) |
| 2..N+1 | Data bytes |

The MCU processes the command after receiving all bytes. No response is sent for writes.

### Read Command

CPU reads data from a device.

```
CPU sends:    [0x80 | device_id]
MCU responds: [length] [data bytes...]
```

| Phase | Byte | Description |
|-------|------|-------------|
| Command | 0 | `0x80 \| device_id` — Bit 7 = 1 (read), bits 6-0 = device ID (1-127) |
| Response | 0 | Length N (0-254 bytes) |
| Response | 1..N | Data bytes (if N > 0) |

After sending the command byte, the CPU switches to reading. The MCU prepares the response and makes it available via the TX register.

## Special Length Values

| Length | Meaning |
|--------|---------|
| 0 | No data available (read) or empty message |
| 1-254 | Normal data length |
| 255 (0xFF) | Error indicator — next byte is error code |

### Error Codes

When length = 0xFF, the following byte contains an error code:

| Code | Meaning |
|------|---------|
| 0x01 | Unknown device ID |
| 0x02 | Device not ready |
| 0x03 | Invalid command |
| 0x04 | Buffer overflow |
| 0x05-0xFE | Reserved |
| 0xFF | Unspecified error |

## Device ID 0: Interrupt Source

Device ID 0 is reserved for interrupt management. When the MCU asserts an interrupt to the CPU, the CPU reads from device 0 to determine the source.

### Reading Interrupt Source

```
CPU sends:    [0x80]          ; Read from device 0
MCU responds: [0x01] [dev_id] ; One byte: the interrupting device ID
          or: [0x00]          ; No pending interrupts
```

If multiple devices have pending interrupts, each read returns one device ID (highest priority first). The CPU should loop until it receives length = 0:

```asm
poll_interrupts:
    LDA #$80            ; Read command, device 0
    JSR mcu_write_byte
    JSR mcu_read_byte   ; Get length
    BEQ .done           ; Length 0 = no more interrupts
    JSR mcu_read_byte   ; Get device ID
    TAX
    JSR handle_device_interrupt
    BRA poll_interrupts
.done:
    RTS
```

## Device IDs

| ID | Device | Description |
|----|--------|-------------|
| 0 | Interrupt controller | Returns pending interrupt sources |
| 1 | USB keyboard | HID keyboard input |
| 2 | SPI device 0 | First SPI peripheral |
| 3 | SPI device 1 | Second SPI peripheral |
| 4-127 | Reserved | For future expansion |

*Device assignments are application-specific. The above are examples.*

## Timing Considerations

### Turnaround Delay

After the CPU sends a read command, the MCU needs time to:
1. Receive and parse the command
2. Query the target device
3. Prepare the response
4. Assert TX_AVAIL

The CPU simply polls TX_AVAIL until data is ready. No explicit delay is needed.

### Flow Control

The hardware handshake provides flow control:
- MCU won't overflow: CPU checks RX_READY before each write
- CPU won't underflow: CPU checks TX_AVAIL before each read

If the MCU is busy, it simply delays asserting the ready flags.

## Example Transactions

### CPU Reads Keyboard Data

```
CPU → MCU: 0x81              ; Read from device 1 (keyboard)
MCU → CPU: 0x03              ; 3 bytes available
MCU → CPU: 0x1E 0x1F 0x20    ; Scan codes for 'A', 'S', 'D'
```

### CPU Writes to SPI Device

```
CPU → MCU: 0x02              ; Write to device 2 (SPI 0)
CPU → MCU: 0x04              ; 4 bytes follow
CPU → MCU: 0xDE 0xAD 0xBE 0xEF
```

### CPU Polls for Interrupts

```
CPU → MCU: 0x80              ; Read from device 0
MCU → CPU: 0x01 0x01         ; Device 1 (keyboard) has data

CPU → MCU: 0x80              ; Read from device 0 again
MCU → CPU: 0x00              ; No more pending interrupts
```

### Error Response

```
CPU → MCU: 0x99              ; Read from device 25 (doesn't exist)
MCU → CPU: 0xFF 0x01         ; Error: unknown device ID
```

## Implementation Notes

### CPU Side (6502 Assembly)

```asm
; Write byte to MCU (blocks until ready)
mcu_write_byte:
    PHA
.wait:
    LDA MCU_STATUS
    AND #$40            ; Check RX_READY (bit 6)
    BEQ .wait
    PLA
    STA MCU_DATA
    RTS

; Read byte from MCU (blocks until available)
mcu_read_byte:
.wait:
    LDA MCU_STATUS
    BMI .ready          ; Check TX_AVAIL (bit 7)
    BRA .wait
.ready:
    LDA MCU_DATA
    RTS
```

### MCU Side (Pseudocode)

```c
void handle_cpu_command() {
    uint8_t cmd = read_rx_byte();
    uint8_t device = cmd & 0x7F;
    bool is_read = cmd & 0x80;

    if (is_read) {
        if (device == 0) {
            send_pending_interrupt();
        } else {
            send_device_response(device);
        }
    } else {
        uint8_t len = read_rx_byte();
        for (int i = 0; i < len; i++) {
            buffer[i] = read_rx_byte();
        }
        process_device_write(device, buffer, len);
    }
}
```

## Future Extensions

The protocol reserves space for future expansion:
- Device IDs 4-127 are available
- Error codes 0x05-0xFE are reserved
- Bit 6 of the command byte could indicate a "single-byte write" shortcut
- Length = 0 could be redefined as "extended length follows" if >254 byte messages are needed
