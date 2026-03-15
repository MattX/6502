# Communication Protocol

The Pi Pico (RP2350) acts as a bridge between the 6502 bus and the Pi Zero. A
Raspberry Pi Zero provides display, network, and USB.

## Constraints

* Anything that requires the CPU on the Pico has effectively no guaranteed
  timing, so the protocol must be electrically safe and logically correct with
  timing-dependent parts using only PIO and DMA.
* Incoming data on most devices can generally be generated with no 6502
  involvement: input events, communication/network data, RTC interrupts.
  Similarly, the 6502 may decide to read from any device, so the system should
  not assume that the 6502 will e.g. drain all network data before trying to
  read a keyboard event.
* The Pico - Zero protocol is relatively high latency, so data should be pushed
  to the Pico when possible, instead of polled. In particular, the sequence
  "6502 requests data on device N" -> "Pico asserts IRQ to Zero" -> "Zero sends
  REQUEST" -> "Pico sets up DMA" -> "Pico asserts READY" -> "Zero sends READ"
  should not block the 6502 busy waiting on the response to become non-0xFF.
* Data streams (especially network and video) may be very large compared to
  typical 8-bit / 6502 sizes (100 KB+).

## 6502 - Pico Protocol

Communication between the 6502 and the Pico uses a simple TLV
(Type-Length-Value) protocol over the parallel bus.

### Write (6502 -> Pico)

```
[device ID] [len] [data...]
```

Device ID and len are 1 byte each. Max len = 255 (0xFF).

### Read (Pico -> 6502)

```
6502 writes: [device ID | 0x80]
6502 polls:  reads 0xFF until a non-0xFF byte appears
6502 reads:  [len] [data...]     (the non-0xFF byte IS len)
```

Max len = 254 (0xFE). 0xFF is reserved as the "not ready" sentinel.

It is an error for the 6502 to stop polling before a non-0xFF byte is received;
in particular, interrupts should be disabled during this time. This constraint
exists to avoid the following race condition:

1. 6502 requests data on device N
2. 6502 polls, gets 0xFF, stops
3. Pico sets up DMA on device N
4. 6502 requests data on device M
5. (Pico does not have time to set up new DMA)
6. 6502 reads, gets device N data instead of M

If no data is available, len will be 0. The intent is for the Pico to respond as
fast as possible, even when it has no data available, and let the 6502 retry as
needed; 0xFF is provided as a sentinel primarily so that the Pico has time to
set up the DMA channel.

## Devices

| ID | Name | Description |
|----|------|-------------|
| 0 | Status | Handled on the Pico itself. Returns a byte with each bit set if the corresponding device has data. Second byte is 1 if the Zero is connected. Device 0 is also used for Pico -> Zero communication in case of error; errors are sent as plain strings. |
| 1 | System | Handled on Pico. 6502 writes trigger a system reset. Pico sends reset notification (`'R'`) to Zero before rebooting. |
| 2 | Video / Keyboard | Writes go to video, reads come from keyboard. |
| 3 | Netboot | Downloads program from Zero. |
| 4 | Network | |
| 5, 6 | Free | |
| 7 | Echo | Anything written here is written back by the Zero. For testing. |

### Video

For now it's 40x25 text. We maintain a cursor, which is auto-moved right and to
the next line. The screen is automatically scrolled, and the first line is lost,
if the cursor moves below the bottom line of the screen.

ANSI escape codes for color, bold, italic, etc are recognized. Others are not.

Curses-style mode and pixel mode to come at some point.

### Keyboard

Just a stream of characters, no keyup/keydown events. Printable keys /
characters sent as-is, others (arrows, escape, etc) sent as ANSI escapes.

Will switch to a more raw mode later to support stuff like games, where modifier
keys and keyup / keydown are useful.

### Netboot

The 6502 writes a filename (as raw bytes, no null terminator) to device 3. The
Zero looks up the file and responds with a 2-byte big-endian length prefix
followed by the file contents:

```
6502 writes: [device 3] [name_len] [filename...]
6502 reads:  [len_hi] [len_lo] [data...]
```

If the file is not found, the Zero responds with length 0x0000. Data is
delivered across multiple TLV reads (max 254 bytes each); the 6502 reads
repeatedly until it has received `len` bytes total.

This is intended for the ROM bootloader to load programs to RAM (at $0400)
then jump to them, avoiding constant ROM reflashes.

## Pico - Zero SPI Protocol

Zero is the SPI master, so all communication is Zero-initiated over SPI. TLV
packets from the 6502 - Pico protocol are transferred over this SPI link. TLV
packets are not allowed to straddle SPI boundaries, but there can be more than
one TLV packet per SPI frame.

### Why SPI?

| Option  | Max practical throughput | Bidirectional  | Linux support          | Notes              |
|---------|-------------------------|----------------|------------------------|--------------------|
| **SPI** | **~1-2 MB/s**           | **Full-duplex** | **Excellent (master)** | **Best fit**       |
| UART    | ~300 KB/s (3 Mbps)      | Full-duplex    | Good                   | Too slow           |
| I2C     | ~125 KB/s (1 MHz FM)    | Half-duplex    | Good                   | Way too slow       |
| USB     | High                    | Complex        | Complex                | Reserved for other use |
| WiFi    | High                    | Full-duplex    | Good                   | Wired requirement  |

SPI wins because:

* **Speed**: 8 MHz SPI clock = 1 MB/s raw, easily hitting 500 KB-800 KB/s after
  protocol overhead. Can increase to 20 MHz if needed.
* **DMA on both sides**: The BCM2835 SPI master and RP2350 SPI slave both have
  DMA support. Transfers can run without CPU intervention.
* **Simplicity**: 4 signal wires + 2 control wires. Well-understood, no
  negotiation.
* **Linux**: The Pi Zero's SPI master driver (`spidev`) is mature and reliable.
  Linux SPI *slave* support is poor, which makes the choice of roles obvious.

### Physical Layer

#### Wiring

```
  Pi Zero (Master)             Pi Pico (Slave)
  ================             ================
  GPIO 11 (SCLK)  ----------> GPIO 18 (SPI0 SCK)
  GPIO 10 (MOSI)  ----------> GPIO 16 (SPI0 RX)
  GPIO  9 (MISO)  <---------- GPIO 19 (SPI0 TX)
  GPIO  8 (CE0)   ----------> GPIO 17 (SPI0 CSn)
  GPIO 25         <---------- GPIO 20 (IRQ)
  GPIO 24         <---------- GPIO 21 (READY)
  GND             ----------- GND
```

6 signal wires + ground.

#### Signal Descriptions

| Signal | Direction    | Description |
|--------|-------------|-------------|
| SCLK   | Zero -> Pico | SPI clock, 8 MHz default |
| MOSI   | Zero -> Pico | Master Out, Slave In |
| MISO   | Pico -> Zero | Master In, Slave Out |
| CSn    | Zero -> Pico | Chip select, active low. One SPI transaction per CS assertion. |
| IRQ    | Pico -> Zero | Advisory: Pico asserts (drives low) when it has data for Zero to read. The Zero should respond by sending a REQUEST. |
| READY  | Pico -> Zero | Synchronization: Pico asserts (drives low) after loading TX DMA in response to a REQUEST. The Zero must wait for READY before sending a READ. |

#### SPI Mode

Use **Motorola SPI, Mode 3** (CPOL=1, CPHA=1):

* Clock idles high.
* Data sampled on rising edge, changed on falling edge.
* CS is active low.

Mode 3 is required because the PL022 SPI peripheral on the RP2350 only
processes one frame per CS assertion in Mode 0, which breaks multi-byte
transfers. Mode 3 does not have this limitation.

#### Electrical

All signals are 3.3V. Both the Pi Zero and RP2350 are 3.3V native, so no level
shifting is needed. Keep wires short (< 15 cm) for reliable operation, or use
series termination resistors (33 ohm) on SCLK and MOSI if the connection is
longer.

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

| Name              | Value | Description |
|-------------------|-------|-------------|
| `MAX_PAYLOAD`     | 1542  | Maximum payload in a READ response. Room for 6 max-size TLV packets (6 x 257 = 1542). |
| `READ_SIZE`       | 1552  | Fixed transfer size for READ transactions (8-byte buffer status + 2-byte length + 1542-byte payload). Both sides must agree on this value. |

### Transaction Types

#### WRITE (Zero -> Pico)

Single SPI transaction. MISO is ignored.

```
CS low ─────────────────────────────────────── CS high
  MOSI: [0x01] [LEN_HI] [LEN_LO] [payload...]
  MISO: (don't care)
```

* `LEN` (big-endian uint16): number of payload bytes following the 3-byte
  header.
* The payload contains one or more TLV packets.
* The Pico's RX DMA writes the entire transaction (header + payload) into a
  ring buffer. Software parses messages after CS goes high.
* Zero should only send a WRITE if it believes the Pico has enough per-device
  buffer space (tracked via the BUF fields from the most recent READ response).
  The Zero skips devices whose buffer estimate is too low, avoiding
  head-of-line blocking.

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
   and buffer status information.
2. Configuring SPI TX DMA to send from that buffer.
3. Asserting READY (driving low).

#### READ (Zero -> Pico, after READY)

Single SPI transaction of exactly `READ_SIZE` bytes. Only sent **after
READY is asserted** (in response to a prior REQUEST).

```
CS low ────────────────────────────────────────────────── CS high
  MOSI: [0x03] [0x00 x1551]                      (READ_SIZE = 1552 bytes)
  MISO: [BUF x8] [LEN_HI] [LEN_LO] [payload ... zero-padded]
        |<-- simultaneous full-duplex, READ_SIZE bytes ----->|
```

SPI is full-duplex: while the master sends `[0x03]` + dummy bytes, it
simultaneously receives the Pico's pre-loaded response from byte 0. No
negotiation needed -- both sides know the transfer is exactly `READ_SIZE`
bytes.

* `BUF[0..7]` (8 bytes): free RX buffer space on Pico for each of the 8
  devices, in 16-byte units (0xFF = 4080 bytes free, 0x00 = full). Zero uses
  these to decide how much it can WRITE to each device next.
* `LEN` (big-endian uint16): actual valid payload bytes (0 = no data, just
  status). The Zero reads LEN bytes of payload; the rest is zero-padding.
  The payload contains TLV packets. Only complete TLV packets are included;
  partial packets remain in the TX queue for the next READ.

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
  | MISO: [BUF x8][0x00][0x00][0x00 ...]  |
  |=== CS high ===========================>
  |                                       |
  |  Synchronized. Normal operation.      |
```

### Reset

Three events can trigger a system reset:

1. **Power-on**: External supervisory IC (e.g., DS1813) or pull-up with
   pushbutton holds RESB. The Pico boots, drives RESB low via open-drain,
   completes initialization, then releases RESB.
2. **Pushbutton**: External button pulls RESB low. The Pico detects the
   falling edge via GPIO interrupt.
3. **Soft reset**: The 6502 writes any data to Device 1.

For pushbutton and soft reset, the Pico:

1. Drives RESB low (open-drain)
2. Sends a reset notification TLV to the Zero over SPI
3. Waits for the Zero to read the notification (or times out after 1s)
4. Triggers a watchdog reboot

The Pico then reboots as if from power-on: drives RESB low on startup,
initializes all subsystems, and releases RESB when ready. PHI2 (PWM)
stops during reboot; this is harmless because the W65C02S is fully static.
RESB may briefly float high during the ~50ms reboot window, but since
PHI2 is also stopped, the 6502 cannot execute any instructions.

#### RESB Pin

GPIO 4. Open-drain, active low:
- Input mode (default) = released. External pull-up holds RESB high.
- Output low = asserted. Pico drives RESB low.

The same open-drain pattern as the 6502 IRQ pin. An external reset
generator can be substituted with no protocol changes.

#### Reset Notification (Pico -> Zero)

Before rebooting, the Pico sends a reset notification to the Zero over
the existing SPI link:

```
Device 1, length 1, data: 'R' (0x52)
```

This is a standard TLV on Device 1 (system control). The Pico
waits for the Zero to read the notification (TX queue drains) before
rebooting, with a 1-second timeout.

The Zero handles the notification by clearing all TX queues, resetting
terminal state, and setting buffer estimates to full capacity (the Pico
is guaranteed to have empty buffers after rebooting).

On power-on, no notification is sent (the Zero isn't running yet). The
existing startup handshake handles this case.

#### Reset Sequence

```
6502                Pico                     Zero
 |                   |                        |
 |      RESB goes low (external or Pico)      |
 |  <-- RESB low --- |                        |
 |  (enters reset)   |                        |
 |                   |                        |
 |                   | -- reset TLV --------> |
 |                   |    (Device 1, 'R')     |
 |                   |                        | clears TX queues
 |                   |                        | resets terminal
 |                   |                        | sets BUF to max
 |                   |                        |
 |                   | <-- Zero reads TLV --- |
 |                   | (TX queue drained)     |
 |                   |                        |
 |                   | watchdog reboot        |
 |                   | (PHI2 stops, GPIOs     |
 |                   |  float ~50ms)          |
 |                   |                        |
 |                   | --- Pico boots ---     |
 |                   | drives RESB low        |
 |                   | starts PWM (PHI2)      |
 |                   | inits PIO, DMA, SPI    |
 |                   | asserts IRQ            |
 |                   |                        |
 |                   |                        | sees IRQ
 |                   | <== REQUEST ========== |
 |                   | === READY ===========> |
 |                   | <== READ ============= |
 |                   |    (BUF, LEN=0)        |
 |                   |                        | re-synced
 |                   |                        |
 |  <-- RESB high -- |                        |
 |  (reads $FFFC/D)  |                        |
 |  (jumps to ROM)   |                        |
 |                   |                        |
 |  polls Device 0   |                        |
 |  byte 1 = 0       |                        |
 |  ...              |                        |
 |  byte 1 = 1       |  (SPI connected)       |
 |  (proceeds)       |                        |
```

#### After Reset

After RESB goes high, the 6502 reads its reset vector ($FFFC/$FFFD) and
jumps to ROM. The ROM bootloader polls Device 0:

- Byte 0: device data availability bitmask
- Byte 1: 1 if the Zero is connected (at least one SPI transaction
  completed), 0 otherwise

The 6502 loops until byte 1 is 1, then proceeds (e.g., netboot via
Device 3). This polling approach requires no IRQ handling and is robust
against the Zero taking an arbitrary amount of time to boot.

### Transaction Flows

#### Zero sends data to Pico (e.g., network packet for 6502)

```
Zero                                    Pico
  |                                       |
  |  (check per-device BUF >= payload)   |
  |                                       |
  |=== SPI transaction (CS low) ==========>
  | MOSI: [0x01][LEN_HI][LEN_LO]         |
  | MOSI: [TLV payload, LEN bytes]        |
  | MISO: (ignored)                       |
  |=== CS high ===========================>
  |                                       |
  |                       DMA writes into Pico RX ring buffer
  |                       Pico parses TLV, routes to device buffers
```

If the Zero is unsure about buffer space, it does a REQUEST/READ first to
get fresh BUF values.

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
  | MOSI: [0x03][0x00 x1551]             |  (READ_SIZE = 1552 bytes)
  | MISO: [BUF x8][LEN_HI][LEN_LO][payload...pad]
  |       (full-duplex, simultaneous)     |
  |=== CS high ===========================>
  |                                       |
  |  Zero reads LEN valid payload bytes   |
  |  Zero notes per-device BUF            |
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
  | MOSI: [0x03][0x00 x1551]             |
  | MISO: [BUF x8][0x00][0x00][0x00 ...] |  (LEN=0, just status)
  |=== CS high ===========================>
  |                                       |
  |  Zero notes per-device BUF            |
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

The 8 `BUF` bytes in every READ response tell the Zero how much free space the
Pico has per device, in 16-byte units (0x00 = full, 0xFF = 4080 bytes free).

The Zero tracks per-device BUF estimates. Before sending a WRITE, it checks
that each device included in the payload has enough estimated buffer space. It
skips devices whose estimate is too low, avoiding head-of-line blocking.

After each WRITE, the Zero decrements the relevant per-device estimates by the
amount sent. It refreshes all BUF values from the next READ response.

If any estimate reaches zero (or if the Zero hasn't communicated recently), it
does a REQUEST/READ poll before sending more data.

### DMA Strategy

#### Pico (RP2350) -- SPI Slave

The RP2350 hardware SPI slave supports DMA on both TX and RX channels.

**RX (incoming WRITEs and commands from Zero):**

* DMA channel configured to write from SPI RX FIFO into an 8 KB ring buffer.
* Uses `TRIGGER_SELF` with a ring buffer and epoch-based overrun detection.
* After each transaction (CS deasserted, detected via GPIO interrupt on CS pin),
  software scans the ring buffer for the command byte and dispatches accordingly.

**TX (outgoing READs to Zero):**

* Triggered by a REQUEST command. The Pico copies messages from its TX queue
  into a `READ_SIZE`-byte staging buffer:
  `[BUF x8][LEN_HI][LEN_LO][payload...zero-padded to 1542]`.
* Configures SPI TX DMA to send from that buffer (one-shot, not ring).
* Asserts READY. The Zero will clock out exactly `READ_SIZE` bytes.
* After CS rises, the Pico deasserts READY and frees the staging buffer.
* **No race condition**: the REQUEST/READY handshake guarantees the master
  won't clock until DMA is loaded.

#### Pi Zero -- SPI Master

The BCM2835 SPI controller has DMA support. The Linux `spidev` kernel driver
uses DMA for transfers above a configurable threshold (typically 96 bytes).

The Zero-side userspace code (Rust, using `spidev` crate):

* Uses full-duplex `SpidevTransfer::read_write` for READ transactions.
* Batches multiple TLV messages into a single SPI WRITE transaction.

IRQ handling on the Zero side:

* Configure GPIO 25 as input with falling-edge interrupt using `gpiocdev`.
* When IRQ fires, send REQUEST, poll GPIO 24 for READY, then send READ.
* READY polling uses busy-poll with 100 us sleep intervals (the delay is
  typically only a few microseconds).

### Clock Speed Selection

| SPI Clock | Raw throughput | Effective (~80% utilization) | Notes |
|-----------|---------------|------------------------------|-------|
| 5 MHz     | 625 KB/s      | ~500 KB/s                    | Conservative, works with longer wires |
| 8 MHz     | 1 MB/s        | ~800 KB/s                    | Current default |
| 20 MHz    | 2.5 MB/s      | ~2 MB/s                      | Needs short wires or PCB traces |

Start at 8 MHz. The RP2350 SPI slave can handle it comfortably at 150 MHz
system clock. The Pi Zero's BCM2835 SPI master can generate it exactly
(core clock / divisor).

### Error Handling

* **OVERRUN**: If the Pico's RX ring buffer is overwritten before the CPU
  can process it, data is lost. The Zero can detect this indirectly if the
  Pico stops responding to expected messages. The BUF fields help prevent
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

## Design Notes

See [protocol-design-notes.md](protocol-design-notes.md) for protocol
comparison with USB, Ethernet, SD Card, I2C, and ESP32, plus future extension
ideas.
