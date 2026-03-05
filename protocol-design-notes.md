# Protocol Design Notes

Design rationale and comparisons for the Pico <-> Zero SPI protocol.
See [protocol.md](protocol.md) for the actual protocol specification.

## Protocol Comparison

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
|---------|-------------|-----------------------------|
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
