# Pico <-> Zero Interface

The Pi Pico (RP2350) acts as a bridge between the 6502 bus and the Pi Zero. This
document describes the SPI-based protocol between the Pico and Zero.

## Why SPI?

| Option | Max practical throughput | Bidirectional | Linux support | Notes |
|--------|------------------------|---------------|---------------|-------|
| **SPI** | **~1-2 MB/s** | **Full-duplex** | **Excellent (master)** | **Best fit** |
| UART | ~300 KB/s (3 Mbps) | Full-duplex | Good | Too slow |
| I2C | ~125 KB/s (1 MHz FM) | Half-duplex | Good | Way too slow |
| USB | High | Complex | Complex | Reserved for other use |
| WiFi | High | Full-duplex | Good | Wired requirement |

SPI wins because:

* **Speed**: 10 MHz SPI clock = 1.25 MB/s raw, easily hitting 500 KB-1 MB/s after
  protocol overhead. Can increase to 20 MHz if needed.
* **DMA on both sides**: The BCM2835 SPI master and RP2350 SPI slave both have DMA
  support. Transfers can run without CPU intervention.
* **Simplicity**: 4 signal wires + 2 control wires. Well-understood, no negotiation.
* **Linux**: The Pi Zero's SPI master driver (`spidev`) is mature and reliable.
  Linux SPI *slave* support is poor, which makes the choice of roles obvious.

## Physical Layer

### Wiring

```
  Pi Zero (Master)             Pi Pico (Slave)
  ================             ================
  GPIO 11 (SCLK)  ----------> SPI SCK
  GPIO 10 (MOSI)  ----------> SPI RX
  GPIO  9 (MISO)  <---------- SPI TX
  GPIO  8 (CE0)   ----------> SPI CSn
  GPIO 25         <---------- IRQ (active low)
  GPIO 24         <---------- READY (active high)
  GND             ----------- GND
```

6 signal wires + ground.

The exact Pico GPIOs depend on which SPI peripheral and pins are still free after
the 6502 bus interface (which uses GPIOs 0-2, 6-13). Any of the remaining GPIOs
can be assigned to `spi0` or `spi1` on the RP2350.

### Signal Descriptions

| Signal | Direction | Description |
|--------|-----------|-------------|
| SCLK | Zero -> Pico | SPI clock, 10 MHz default |
| MOSI | Zero -> Pico | Master Out, Slave In |
| MISO | Pico -> Zero | Master In, Slave Out |
| CSn | Zero -> Pico | Chip select, active low. One SPI transaction per CS assertion. |
| IRQ | Pico -> Zero | Pico asserts (drives low) when it has data for Zero to read. Directly wired to a Zero GPIO configured with edge interrupt. |
| READY | Pico -> Zero | Pico drives high when it can accept data. Deasserted (low) = backpressure, Zero must not send WRITE commands. |

### Electrical

All signals are 3.3V. Both the Pi Zero and RP2350 are 3.3V native, so no level
shifting is needed. Keep wires short (< 15 cm) for 10 MHz+ operation, or use
series termination resistors (33 ohm) on SCLK and MOSI if the connection is longer.

## Protocol

### Roles

* **Pi Zero = SPI Master.** It generates the clock and initiates every SPI
  transaction. This is the natural role because Linux's SPI master driver is
  mature, while SPI slave support barely exists.
* **Pi Pico = SPI Slave.** It responds to transactions initiated by the Zero.
  The RP2350's hardware SPI peripheral supports slave mode with DMA natively.
* **Pico initiates via IRQ.** When the Pico has data to send (received from the
  6502), it asserts the IRQ line. The Zero's IRQ handler then initiates a READ
  transaction to retrieve it.

### Transaction Format

Every SPI transaction begins with a **4-byte header** exchanged simultaneously
(full-duplex), optionally followed by a **payload**.

```
CS asserted ──────────────────────────────────────────── CS deasserted
             [---- Header (4 bytes) ----] [-- Payload (0..N bytes) --]
  MOSI:      [CMD] [LEN_HI] [LEN_LO] [SEQ]  [data bytes...]
  MISO:      [STS] [AVL_HI] [AVL_LO] [BUF]  [data bytes...]
```

The header bytes are exchanged simultaneously on MOSI/MISO, so each side sends
and receives header information in a single 4-byte exchange.

#### Header: Zero -> Pico (MOSI)

| Byte | Name | Description |
|------|------|-------------|
| 0 | CMD | Command byte (see below) |
| 1-2 | LEN | Payload length in bytes (big-endian). For WRITE: number of bytes that follow. For READ: max bytes Zero is willing to accept. For STATUS: 0. |
| 3 | SEQ | Sequence number (0-255, wrapping). Incremented per transaction. Used to detect dropped/duplicate transactions. |

#### Header: Pico -> Zero (MISO)

| Byte | Name | Description |
|------|------|-------------|
| 0 | STS | Status flags (see below) |
| 1-2 | AVL | Bytes available to read from Pico (big-endian). If CMD is READ, the actual transfer length is `min(LEN, AVL)`. |
| 3 | BUF | Free buffer space on Pico, in units of 64 bytes. 0xFF = 16 KB+ free. 0x00 = full. |

#### Commands (CMD byte)

| Value | Name | Payload direction | Description |
|-------|------|-------------------|-------------|
| 0x00 | STATUS | None | Poll Pico status. No payload follows; just exchange headers. |
| 0x01 | WRITE | MOSI (Zero -> Pico) | Zero sends LEN bytes of payload to Pico. |
| 0x02 | READ | MISO (Pico -> Zero) | Zero clocks out `min(LEN, AVL)` bytes. Zero sends dummy 0x00 on MOSI. |

#### Status Flags (STS byte)

| Bit | Name | Description |
|-----|------|-------------|
| 0 | DATA_AVAIL | 1 = Pico has data to send (mirrors IRQ pin state) |
| 1 | BUSY | 1 = Pico is processing a previous transaction, retry later |
| 2 | OVERRUN | 1 = Pico's RX buffer overflowed since last STATUS read (sticky, cleared on read) |
| 3 | PROTO_ERR | 1 = Pico detected a protocol error (bad CMD, length mismatch, etc.) |
| 7-4 | | Reserved (0) |

### Payload Framing: Messages

The payload carries **messages** between Zero and Pico. Each message corresponds
to a 6502 bus transaction (a write to or read from a virtual device). Messages
are framed identically to the 6502-side protocol:

```
[DEVICE] [LENGTH] [DATA ...]
```

* `DEVICE`: 0-127 for data heading toward the 6502 (Zero->Pico->6502),
  or `DEVICE | 0x80` for data originating from the 6502 (6502->Pico->Zero).
* `LENGTH`: 1-255 bytes of data following.
* `DATA`: The message payload.

A single SPI WRITE or READ payload may contain **multiple concatenated messages**
to amortize SPI transaction overhead. The receiver parses them sequentially
using the length field.

### Transaction Flows

#### Zero sends data to Pico (e.g., network packet for 6502)

```
Zero                                    Pico
  |                                       |
  |--- check READY pin is high ---------->|
  |                                       |
  |=== SPI transaction (CS low) ==========>
  | MOSI: [CMD=WRITE][LEN=N][SEQ]        |
  | MISO: [STS][AVL][BUF]                |  <-- Zero can note AVL for later
  | MOSI: [payload, N bytes]             |
  | MISO: (ignored)                      |
  |=== CS high ===========================>
  |                                       |
  |                       DMA writes payload into Pico RX ring buffer
  |                       Pico processes messages, forwards to 6502
```

#### Pico sends data to Zero (e.g., 6502 wrote to a device)

```
Zero                                    Pico
  |                                       |
  |                       6502 writes to device, data enters Pico TX buffer
  |                       Pico loads SPI TX DMA from TX buffer
  |                       Pico asserts IRQ (drives low)
  |                                       |
  |<-- IRQ triggers GPIO interrupt -------|
  |                                       |
  |=== SPI transaction (CS low) ==========>
  | MOSI: [CMD=READ][LEN=max][SEQ]       |
  | MISO: [STS][AVL=M][BUF]              |
  | MOSI: [dummy 0x00, M bytes]          |
  | MISO: [payload, M bytes]             |
  |=== CS high ===========================>
  |                                       |
  |                       Pico deasserts IRQ if TX buffer is now empty
```

The actual transfer length is `min(LEN, AVL)`. If `AVL` is 0 (Pico had no data
despite IRQ -- possible race), the transaction ends after the header with no
payload.

#### Status poll

```
Zero                                    Pico
  |                                       |
  |=== SPI transaction (CS low) ==========>
  | MOSI: [CMD=STATUS][0][0][SEQ]        |
  | MISO: [STS][AVL][BUF]                |
  |=== CS high ===========================>
```

Useful for checking buffer space before a large WRITE, or as a heartbeat.

### Backpressure

Two complementary mechanisms:

1. **READY pin (hardware, fast).** Directly readable by Zero without an SPI
   transaction. Pico deasserts READY when its RX free buffer drops below a
   low-water mark (e.g., 256 bytes). Zero must not start a WRITE while READY
   is low. Pico reasserts READY when free space rises above a high-water mark
   (e.g., 512 bytes). Hysteresis prevents oscillation.

2. **BUF field (software, precise).** The BUF byte in every response header
   gives exact free space (in 64-byte units). Zero can use this to choose
   how much data to send in the next WRITE, packing the payload right up to
   the available space.

The READY pin provides a fast, always-available guard rail. The BUF field
allows the Zero to make smarter batching decisions.

### DMA Strategy

#### Pico (RP2350) -- SPI Slave

The RP2350 hardware SPI slave supports DMA on both TX and RX channels.

**RX (incoming writes from Zero):**
* DMA channel configured to write from SPI RX FIFO into a ring buffer (e.g., 4-8 KB).
* Similar to the existing 6502 bus interface DMA strategy: use `TRIGGER_SELF`
  with a ring buffer and epoch-based overrun detection.
* After each transaction (CS deasserted), software scans the ring buffer for
  complete messages and dispatches them.

**TX (outgoing reads to Zero):**
* When the Pico has data to send, it copies messages into a staging buffer and
  configures the SPI TX DMA to send from that buffer.
* Then asserts IRQ. The Zero will clock the data out.
* One-shot DMA (not ring buffer) because the Pico knows the exact size and content.

#### Pi Zero -- SPI Master

The BCM2835 SPI controller has DMA support. The Linux `spidev` kernel driver
uses DMA for transfers above a configurable threshold (typically 96 bytes).

For maximum throughput, the Zero-side userspace code should:
* Use `ioctl(SPI_IOC_MESSAGE)` with a single `spi_ioc_transfer` struct
  containing both TX and RX buffers (to get the simultaneous header exchange).
* Batch multiple messages into a single SPI transaction when possible.

An IRQ-driven approach on the Zero side:
* Configure GPIO 25 as input with falling-edge interrupt (`/sys/class/gpio` or
  `libgpiod`).
* On interrupt, perform a READ transaction.
* Optionally, use a dedicated thread that blocks on `poll()` for the GPIO edge
  event, then performs the SPI transfer.

## Clock Speed Selection

| SPI Clock | Raw throughput | Effective (~80% utilization) | Notes |
|-----------|---------------|------------------------------|-------|
| 5 MHz | 625 KB/s | ~500 KB/s | Conservative, works with longer wires |
| 10 MHz | 1.25 MB/s | ~1 MB/s | Good default |
| 20 MHz | 2.5 MB/s | ~2 MB/s | Needs short wires or PCB traces |

Start at 10 MHz. The RP2350 SPI slave can handle it comfortably at 150 MHz
system clock. The Pi Zero's BCM2835 SPI master can generate it exactly
(core clock / divisor).

## Error Handling

* **Sequence numbers**: The SEQ field allows both sides to detect gaps (missed
  transactions) or duplicates (retransmitted transactions after a timeout).
* **OVERRUN flag**: If the Pico's RX ring buffer is overwritten before the CPU
  can process it, the OVERRUN bit is set. The Zero should back off and may need
  to retransmit.
* **CRC (optional)**: For added reliability, append a CRC-8 to each message
  inside the payload. The 4-byte header is small enough that corruption there
  would typically cause a detectable protocol error (bad CMD, impossible length,
  etc.) rather than silent data corruption.
* **Timeouts**: If the Zero sends a READ but the Pico's DMA wasn't ready, AVL
  will be 0 and no payload is exchanged. The Zero simply retries on the next IRQ.

## Future Extensions

* **Full-duplex data**: During a WRITE, the Pico could simultaneously return
  data on MISO (instead of ignoring it). This would double effective throughput
  for bidirectional workloads. Adds complexity to DMA setup on both sides.
* **Multi-CS**: If multiple Picos are used (e.g., one per function), the Zero
  can use CE0/CE1 to address different slaves.
* **Bulk DMA transfers**: For large payloads (e.g., loading a ROM image), a
  WRITE_STREAM command could skip per-message framing and send raw data.
