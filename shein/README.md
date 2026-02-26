# Shein

Terminal software for the 6502 system running on a Raspberry Pi Zero. Talks to the Pico over SPI as the SPI master.

## SPI Protocol (Zero ↔ Pico)

Zero is the SPI master; all communication is Zero-initiated.

### Commands

**WRITE (Zero → Pico):** `[0x01] [len_hi] [len_lo] [payload...]`

The payload contains one or more complete TLV packets (see below). TLV packets must not straddle SPI frame boundaries. The Zero maintains per-device estimates of buffer space on the Pico and will not send more data than a device's buffer can hold.

**REQUEST:** `[0x02]` (one byte)

Asks the Pico to prepare data. The Zero then waits for the READY signal.

**READ:** `[0x03] [0x00...]` (1552 bytes total)

Sent after READY is asserted. The Pico responds with:

| Offset | Size | Description |
|--------|------|-------------|
| 0-7 | 8 | Per-device buffer free space (in 16-byte units, capped at 255) |
| 8-9 | 2 | Payload length (big-endian) |
| 10+ | up to 1542 | Payload: complete TLV packets |

The max payload (1542 bytes) fits 6 max-size TLV packets (257 bytes each).

### TLV Packet Format

Same as the 6502-Pico protocol: `[device_id] [length] [data...]` where device_id and length are 1 byte each, and length max is 255.

### Flow Control

The Pico reports per-device free buffer space in each READ response (8 bytes, one per device, in 16-byte units). The Zero tracks these estimates locally and decrements them on each WRITE. When an estimate reaches zero, the Zero issues a REQUEST/READ to refresh it.

### Handshake Signals

- **IRQ** (Pico → Zero, active low): Pico has data to send.
- **READY** (Pico → Zero, active low): Pico has loaded TX DMA, safe to READ.

Between REQUEST and READY, the Zero must not initiate another SPI transaction (gives the Pico time to prepare the DMA buffer without racing).

## Devices

| ID | Name | Direction | Description |
|----|------|-----------|-------------|
| 0 | Status | Pico → Zero | Bit field: which devices have data. Second byte = Zero online. Errors as plain strings. |
| 1 | System | Reserved | Soft shutdown, reset, clock control |
| 2 | Video/KB | Bidirectional | Writes to video, reads from keyboard |
| 3 | Netboot | Zero → Pico | Downloads program from Zero |
| 4 | Network | Bidirectional | Network data |
| 5-6 | Free | — | Unassigned |
| 7 | Echo | Bidirectional | Echo test device |

## Building

```bash
cargo build                    # Debug build (stub SPI on non-Linux)
cargo build --release          # Release build (for Pi Zero)
```
