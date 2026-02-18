# Pico <-> Zero Interface

The Pi Pico (RP2350) acts as a bridge between the 6502 bus and the Pi Zero. This
document describes the SPI-based protocol between the Pico and Zero.

## Why SPI?

| Option  | Max practical throughput | Bidirectional  | Linux support          | Notes              |
|---------|-------------------------|----------------|------------------------|--------------------|
| **SPI** | **~1-2 MB/s**           | **Full-duplex** | **Excellent (master)** | **Best fit**       |
| UART    | ~300 KB/s (3 Mbps)      | Full-duplex    | Good                   | Too slow           |
| I2C     | ~125 KB/s (1 MHz FM)    | Half-duplex    | Good                   | Way too slow       |
| USB     | High                    | Complex        | Complex                | Reserved for other use |
| WiFi    | High                    | Full-duplex    | Good                   | Wired requirement  |

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
  GPIO 25         <---------- IRQ   (active low, "I have data")
  GPIO 24         <---------- READY (active low, "TX DMA loaded, safe to READ")
  GND             ----------- GND
```

6 signal wires + ground.

The exact Pico GPIOs depend on which SPI peripheral and pins are still free after
the 6502 bus interface (which uses GPIOs 0-2, 6-13). Any of the remaining GPIOs
can be assigned to `spi0` or `spi1` on the RP2350.

### Signal Descriptions

| Signal | Direction    | Description |
|--------|-------------|-------------|
| SCLK   | Zero -> Pico | SPI clock, 10 MHz default |
| MOSI   | Zero -> Pico | Master Out, Slave In |
| MISO   | Pico -> Zero | Master In, Slave Out |
| CSn    | Zero -> Pico | Chip select, active low. One SPI transaction per CS assertion. |
| IRQ    | Pico -> Zero | Advisory: Pico asserts (drives low) when it has data for Zero to read. The Zero should respond by sending a REQUEST. |
| READY  | Pico -> Zero | Synchronization: Pico asserts (drives low) after loading TX DMA in response to a REQUEST. The Zero must wait for READY before sending a READ. |

### SPI Mode

Use **Motorola SPI, Mode 0** (CPOL=0, CPHA=0):

* Clock idles low.
* Data sampled on rising edge, changed on falling edge.
* CS is active low.

This is the default on both the BCM2835 and RP2350, and what the vast majority
of SPI devices use. The other SPI "standards" (TI SSI, Microwire) are entirely
different protocols and are not relevant here.

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
* **Bidirectional initiation.** When the Pico has data to send, it asserts IRQ.
  The Zero responds with a REQUEST/READ handshake to retrieve it.

### The Core Problem: SPI Slave TX Timing

In SPI slave mode, the TX FIFO must be loaded **before** the master starts
clocking. The slave cannot react to an incoming transaction and prepare a
response mid-transfer. The PL022 has no "default TX byte" register -- if the
TX FIFO is empty when the master clocks, the output is undefined.

The master can start a transaction at any time. This means the slave cannot
safely set up TX DMA at arbitrary moments -- the master might start clocking
in the middle of the DMA configuration, producing corrupt output.

**The REQUEST/READY handshake solves this.** When the master sends a REQUEST,
it promises not to start another transaction until READY is asserted. This
gives the slave a guaranteed window to configure TX DMA without any race
condition. Once READY is asserted, the TX DMA is stable and the master can
safely clock out the response.

For WRITEs, this problem doesn't exist -- MISO is ignored.

### Constants

| Name        | Value | Description |
|-------------|-------|-------------|
| `READ_SIZE` | 1503  | Fixed transfer size for READ transactions (3-byte header + 1500-byte payload). Payload matches Ethernet MTU so a full network packet fits in one READ. Both sides must agree on this value. |

### Transaction Types

#### WRITE (Zero -> Pico)

Single SPI transaction. MISO is ignored.

```
CS low ─────────────────────────────────────── CS high
  MOSI: [0x01] [LEN_HI] [LEN_LO] [payload...]
  MISO: (don't care)
```

* `LEN` (big-endian uint16): number of payload bytes following the 3-byte header.
* The Pico's RX DMA writes the entire transaction (header + payload) into a ring
  buffer. Software parses messages after CS goes high.
* Zero should only send a WRITE if it believes the Pico has enough buffer space
  (tracked via the BUF field from the most recent READ response).

#### REQUEST (Zero -> Pico)

Single 1-byte SPI transaction. Asks the Pico to prepare a response.

```
CS low ──── CS high
  MOSI: [0x02]
  MISO: (don't care)
```

After CS goes high, the **master must not start any SPI transaction** until
READY is asserted by the Pico. The next transaction must be a READ.

The Pico handles the REQUEST by:
1. Preparing a `READ_SIZE`-byte response buffer with current data (if any)
   and status information.
2. Configuring SPI TX DMA to send from that buffer.
3. Asserting READY (driving low).

#### READ (Zero -> Pico, after READY)

Single SPI transaction of exactly `READ_SIZE` bytes. Only sent **after
READY is asserted** (in response to a prior REQUEST).

```
CS low ──────────────────────────────────────────────── CS high
  MOSI: [0x03] [0x00 x1502]                   (READ_SIZE = 1503 bytes)
  MISO: [LEN_HI] [LEN_LO] [BUF] [payload ... zero-padded]
        |<-- simultaneous full-duplex, READ_SIZE bytes ->|
```

SPI is full-duplex: while the master sends `[0x03]` + dummy bytes, it
simultaneously receives the Pico's pre-loaded response from byte 0. No
negotiation needed -- both sides know the transfer is exactly `READ_SIZE`
bytes.

* `LEN` (big-endian uint16): actual valid payload bytes (0 = no data, just
  status). The Zero reads LEN bytes of payload; the rest is zero-padding.
* `BUF`: free RX buffer space on Pico, in 64-byte units (0xFF = 16 KB+ free,
  0x00 = full). Zero uses this to decide how much it can WRITE next.

After CS goes high, the Pico deasserts READY. The Zero must observe READY
going high before sending a new REQUEST.

### Startup Sequence

The Pico boots faster than the Zero (bare-metal vs Linux). The startup
handshake ensures the Zero doesn't send transactions before the Pico is ready:

1. Pico boots, initializes SPI slave and DMA, asserts IRQ.
2. Zero boots, starts SPI master, waits for IRQ low.
3. Zero sees IRQ, sends REQUEST/READ.
4. Pico responds with `LEN=0, BUF=current`. Both sides are now synchronized.
5. Normal operation begins.

This also handles **Pico reboots**: the Zero sees a new IRQ falling edge
and can re-sync with a REQUEST/READ.

```
Zero                                    Pico
  |                                       |
  |                       Pico boots, inits SPI slave
  |                       Pico asserts IRQ
  |                                       |
  |  Zero boots (Linux)                   |
  |  Zero waits for IRQ...                |
  |                                       |
  |<-- IRQ (low) -------------------------|
  |                                       |
  |=== REQUEST ===========================>
  |=== (wait READY) ==== READ ============>
  | MISO: [0x00][0x00][BUF][0x00 ...]    |
  |=== CS high ===========================>
  |                                       |
  |  Synchronized. Normal operation.      |
```

### Transaction Flows

#### Zero sends data to Pico (e.g., network packet for 6502)

```
Zero                                    Pico
  |                                       |
  |  (check last-known BUF >= payload)    |
  |                                       |
  |=== SPI transaction (CS low) ==========>
  | MOSI: [0x01][LEN_HI][LEN_LO]         |
  | MOSI: [payload, LEN bytes]            |
  | MISO: (ignored)                       |
  |=== CS high ===========================>
  |                                       |
  |                       DMA writes into Pico RX ring buffer
  |                       Pico parses messages, forwards to 6502
```

If the Zero is unsure about buffer space, it does a REQUEST/READ first to
get a fresh BUF value.

#### Pico sends data to Zero (e.g., 6502 wrote to a device)

```
Zero                                    Pico
  |                                       |
  |                       6502 writes to device, data enters Pico TX queue
  |                       Pico asserts IRQ (drives low)
  |                                       |
  |<-- IRQ (falling edge) ---------------|
  |                                       |
  |=== REQUEST (CS low) =================>
  | MOSI: [0x02]                          |
  | MISO: (ignored)                       |
  |=== CS high ===========================>
  |                                       |
  |                       Pico sees REQUEST in RX ring
  |                       Pico prepares READ_SIZE response buffer
  |                       Pico loads SPI TX DMA
  |                       Pico asserts READY (drives low)
  |                       Pico deasserts IRQ (data is being handled)
  |                                       |
  |<-- READY (low) ----------------------|
  |                                       |
  |=== READ (CS low) ====================>
  | MOSI: [0x03][0x00 x1502]             |  (READ_SIZE = 1503 bytes)
  | MISO: [LEN_HI][LEN_LO][BUF][payload...pad]
  |       (full-duplex, simultaneous)     |
  |=== CS high ===========================>
  |                                       |
  |  Zero reads LEN valid payload bytes   |
  |  Zero notes BUF for future WRITEs     |
  |                                       |
  |                       Pico deasserts READY
  |                       If more data in TX queue: assert IRQ again
```

#### Zero checks Pico buffer space (REQUEST/READ poll)

```
Zero                                    Pico
  |                                       |
  |=== REQUEST (CS low) =================>
  | MOSI: [0x02]                          |
  | MISO: (ignored)                       |
  |=== CS high ===========================>
  |                                       |
  |                       Pico prepares response (LEN=0, BUF=current)
  |                       Pico loads SPI TX DMA
  |                       Pico asserts READY
  |                                       |
  |<-- READY (low) ----------------------|
  |                                       |
  |=== READ (CS low) ====================>
  | MOSI: [0x03][0x00 x1502]             |
  | MISO: [0x00][0x00][BUF][0x00 ...]    |  (LEN=0, just status)
  |=== CS high ===========================>
  |                                       |
  |  Zero notes BUF for future WRITEs     |
  |                                       |
  |                       Pico deasserts READY
```

### Signal State Rules

The Zero must respect the following rules:

1. **READY high (deasserted)**: Zero may send WRITE or REQUEST. Zero must NOT
   send READ.
2. **READY low (asserted)**: Zero must send READ. Zero must NOT send WRITE or
   REQUEST.
3. After a READ completes (CS high), the Zero must wait for READY to go high
   before sending any new transaction.

The Pico must respect:

1. IRQ is asserted at boot to signal readiness (even with no data).
2. READY is only asserted after TX DMA is fully configured.
3. READY is deasserted after a READ completes (CS rising edge).
4. IRQ is deasserted when a REQUEST is received (the data is being handled).
   It is re-asserted if more data remains in the TX queue after the READ.

### Backpressure

The `BUF` field in every READ response tells the Zero how much free space the
Pico has, in 64-byte units (0x00 = full, 0xFF = 16 KB+ free).

The Zero tracks the last-known BUF value and does not send a WRITE whose payload
exceeds `BUF * 64` bytes. After each WRITE, the Zero decrements its local BUF
estimate by the amount sent. It refreshes BUF from the next READ response.

If the Zero's estimate reaches zero (or it hasn't communicated recently), it
does a REQUEST/READ poll before sending more data.

### DMA Strategy

#### Pico (RP2350) -- SPI Slave

The RP2350 hardware SPI slave supports DMA on both TX and RX channels.

**RX (incoming WRITEs and commands from Zero):**

* DMA channel configured to write from SPI RX FIFO into a ring buffer (e.g., 4-8 KB).
* Similar to the existing 6502 bus interface DMA strategy: use `TRIGGER_SELF`
  with a ring buffer and epoch-based overrun detection.
* After each transaction (CS deasserted, detected via GPIO interrupt on CS pin),
  software scans the ring buffer for the command byte and dispatches accordingly.

**TX (outgoing READs to Zero):**

* Triggered by a REQUEST command. The Pico copies messages from its TX queue
  into a `READ_SIZE`-byte staging buffer:
  `[LEN_HI][LEN_LO][BUF][payload...zero-padded to 1500]`.
* Configures SPI TX DMA to send from that buffer (one-shot, not ring).
* Asserts READY. The Zero will clock out exactly `READ_SIZE` bytes.
* After CS rises, the Pico deasserts READY and frees the staging buffer.
* **No race condition**: the REQUEST/READY handshake guarantees the master
  won't clock until DMA is loaded.

#### Pi Zero -- SPI Master

The BCM2835 SPI controller has DMA support. The Linux `spidev` kernel driver
uses DMA for transfers above a configurable threshold (typically 96 bytes).

For maximum throughput, the Zero-side userspace code should:

* Use `ioctl(SPI_IOC_MESSAGE)` with a `spi_ioc_transfer` struct containing
  both TX and RX buffers.
* Batch multiple messages into a single SPI WRITE transaction when possible.

IRQ handling on the Zero side:

* Configure GPIO 25 as input with falling-edge interrupt using `libgpiod`.
* When IRQ fires, send REQUEST, poll GPIO 24 for READY, then send READ.
* READY polling can use `libgpiod` level-wait or busy-poll (the delay is
  typically only a few microseconds).

## Clock Speed Selection

| SPI Clock | Raw throughput | Effective (~80% utilization) | Notes |
|-----------|---------------|------------------------------|-------|
| 5 MHz     | 625 KB/s      | ~500 KB/s                    | Conservative, works with longer wires |
| 10 MHz    | 1.25 MB/s     | ~1 MB/s                      | Good default |
| 20 MHz    | 2.5 MB/s      | ~2 MB/s                      | Needs short wires or PCB traces |

Start at 10 MHz. The RP2350 SPI slave can handle it comfortably at 150 MHz
system clock. The Pi Zero's BCM2835 SPI master can generate it exactly
(core clock / divisor).

## Error Handling

* **OVERRUN**: If the Pico's RX ring buffer is overwritten before the CPU
  can process it, data is lost. The Zero can detect this indirectly if the
  Pico stops responding to expected messages. The BUF field helps prevent
  this by letting the Zero self-throttle.
* **CRC (optional)**: For added reliability, append a CRC-8 to each message
  inside the payload. The SPI headers are small enough that corruption
  would typically cause a detectable protocol error (bad CMD, impossible length)
  rather than silent data corruption.
* **Timeouts**: If the Pico asserts IRQ but the Zero doesn't respond within a
  timeout (e.g., 100 ms), the Pico can deassert IRQ and re-queue the data.
  If the Pico doesn't assert READY within a timeout after REQUEST, the Zero
  can retry the REQUEST.
* **Protocol violations**: If the Zero sends READ without REQUEST, or sends
  a transaction while READY is asserted, the Pico's RX ring will contain
  unexpected data. The Pico discards unrecognized commands.

## Protocol Comparison and Design Notes

This protocol occupies an unusual niche: a point-to-point link between two known
devices, with a master/slave physical layer but bidirectional data flow. Here's
how it relates to other protocols and what we borrowed (or didn't) from each.

### USB

USB is the closest analogy in terms of architecture: a host (master) polls a
device (slave) that can't transmit unsolicited data.

**What USB does that we borrowed:**
* **Fixed transfer sizes.** USB bulk transfers use fixed max packet sizes
  (64 B for Full Speed, 512 B for High Speed). Our `READ_SIZE` is the same
  idea. Both sides agree on the size upfront; no per-transfer negotiation.
* **NAK as flow control.** When a USB device isn't ready, it responds NAK and
  the host retries later. Our BUF field serves a similar role -- the Zero
  checks available space before sending -- but shifted to a "check before you
  send" model rather than "try and get rejected." We can't do NAK because MISO
  is ignored during WRITEs.

**What USB does that we don't need:**
* **Enumeration and descriptors.** USB devices self-describe their capabilities
  through a multi-step enumeration process. We have a fixed, known topology --
  one Pico, one Zero, forever. Eliminating this removes enormous complexity.
* **Multiple endpoint types.** USB has Control, Bulk, Interrupt, and Isochronous
  transfer types for different latency/bandwidth tradeoffs. We have one
  transfer type (best-effort, similar to Bulk) because our single use case
  doesn't need the others.
* **Toggle bits.** USB uses DATA0/DATA1 alternation for simple duplicate
  detection. We could add this (1 bit in the header) if reliability becomes
  a concern, but over a 10 cm wire it's likely unnecessary.

**What USB does that we can't:**
* **Hardware protocol engine.** USB's framing, CRC, retries, and NAK handling
  are all in dedicated silicon. We're building on raw SPI with no protocol
  hardware, so every mechanism must be explicit and simple.

### Ethernet (Layer 2)

**What Ethernet does that influenced us:**
* **MTU = 1500 bytes.** Our `READ_SIZE` payload (1500 B) matches the Ethernet
  MTU so that a full network packet transits the SPI link without fragmentation.
  This is a deliberate choice given that Internet access is a primary use case.
* **Fire-and-forget at L2.** Ethernet frames have no acknowledgment at the link
  layer; reliability is TCP's job. Our WRITEs are similarly fire-and-forget.

**What Ethernet does that we don't need:**
* **CRC-32 on every frame.** Ethernet runs over cables in electrically noisy
  environments with connectors that can degrade. Our link is 10 cm of wire
  between two boards. CRC is optional and probably unnecessary.
* **Minimum frame size (64 bytes).** Ethernet pads small frames for collision
  detection. We have no collisions (point-to-point, master-initiated).
* **Preamble and SFD.** Ethernet needs 8 bytes of synchronization before each
  frame. SPI's CS line provides unambiguous frame boundaries for free.

### SD Card (SPI Mode)

The most directly comparable protocol -- an SPI slave device with a
command-response model.

**What SD does that we considered and rejected:**
* **0xFF busy polling.** The SD card sends 0xFF while processing a command; the
  host keeps clocking until it sees a non-0xFF response byte. Elegant, but
  relies on the SD controller hardware always outputting 0xFF when idle. The
  PL022 has no such guarantee -- its underrun behavior is implementation-defined.
  We use the READY GPIO handshake instead, which is unambiguous.

**What SD does that we share:**
* **Fixed block sizes.** SD cards transfer 512-byte blocks. Our `READ_SIZE` is
  the same concept at a different size.
* **Command-first, response-later.** The SD protocol sends a command, then
  the card responds. Our REQUEST/READ flow is the same pattern, with READY
  as the explicit "response is loaded" signal.

### I2C

**One interesting feature we can't have:**
* **Clock stretching.** An I2C slave can hold SCL low to say "I'm not ready
  yet." This elegantly solves the slave-not-ready problem at the hardware level.
  SPI slaves have no equivalent -- the master controls the clock unconditionally.
  Our READY pin serves the same purpose at the protocol level: it tells the
  master "don't clock yet, I'm preparing my response."

### ESP32 SPI Slave (WiFi offload)

This is perhaps the closest existing system to what we're building: an SPI slave
acting as a network bridge, using GPIO handshake lines.

**What ESP32 does similarly:**
* **GPIO handshake for "data ready."** The ESP32 SPI slave protocol uses a GPIO
  line (like our IRQ) to signal "I have data." Validates our approach.
* **~1600-byte transfers.** Sized to fit Ethernet frames, same reasoning as ours.
* **Two handshake pins** ("data ready" + "slave ready"). Our IRQ + READY design
  matches the ESP32 approach. This is a validated pattern for SPI slave protocols
  that need bidirectional initiation.

### Summary of Design Choices

| Concern | Our approach | Alternative (and why not) |
|---------|-------------|--------------------------|
| Slave initiation | IRQ pin | Polling (wasteful), clock stretching (SPI can't) |
| Slave TX timing | REQUEST/READY handshake | Two-phase STATUS (timing assumptions, stale TX FIFO), "always loaded" buffer (race during switch) |
| Flow control | BUF field in READ response | NAK (can't respond during WRITE), software-only STATUS poll (complex) |
| Transfer size | Fixed `READ_SIZE` | Negotiated per-transfer (requires header exchange before DMA setup) |
| Framing | CS line + length prefix | Sentinel bytes (PL022 underrun undefined), CRC delimiters (overhead) |
| Reliability | None (best-effort) | CRC + retransmit (overkill for 10 cm wire) |
| Enumeration | None (fixed topology) | Self-description (USB-style, massive complexity for no benefit) |

## Future Extensions

* **Full-duplex data**: During a WRITE, the Pico could simultaneously return
  data on MISO (instead of ignoring it). This would double effective throughput
  for bidirectional workloads. Adds complexity to DMA setup -- would need a
  REQUEST before the WRITE to give the Pico time to load TX DMA.
* **Multi-CS**: If multiple Picos are used (e.g., one per function), the Zero
  can use CE0/CE1 to address different slaves.
* **Bulk DMA transfers**: For large payloads (e.g., loading a ROM image), a
  WRITE_STREAM command could skip per-message framing and send raw data.
