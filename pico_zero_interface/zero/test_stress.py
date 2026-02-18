#!/usr/bin/env python3
"""
SPI Stress Test

Sends ~1MB in each direction with pattern verification.
Each iteration: WRITE (with verifiable pattern) -> REQUEST/READ (verify response).
Payload sizes cycle through various lengths. Reports only errors and final stats.

WRITE payload:  [seq_BE(4)] [pattern] where pattern[i] = (seq + i) & 0xFF
READ response:  [seq_BE(4)] [pattern] where pattern[i] = (seq*7 + i) & 0xFF
"""

import struct
import sys
import time

from spi_master import SpiMaster, READ_SIZE, SPI_CMD_WRITE


SIZES = [10, 50, 100, 256, 500, 1000, 1500]
NUM_CYCLES = 300  # ~1MB each direction


def make_write_payload(seq: int, length: int) -> bytearray:
    """Create WRITE payload: 4-byte seq (BE) + pattern bytes."""
    buf = bytearray(length)
    struct.pack_into(">I", buf, 0, seq)
    for i in range(4, length):
        buf[i] = (seq + (i - 4)) & 0xFF
    return buf


def verify_read_response(seq: int, data: bytes, expected_len: int) -> str | None:
    """Verify READ response pattern. Returns error string or None if OK."""
    if len(data) < 4:
        return f"too short ({len(data)} bytes)"
    got_seq = struct.unpack(">I", data[:4])[0]
    if got_seq != seq:
        return f"seq mismatch: expected {seq}, got {got_seq}"
    check_len = min(len(data), expected_len)
    for i in range(4, check_len):
        expected = ((seq * 7) + (i - 4)) & 0xFF
        if data[i] != expected:
            return f"byte[{i}] expected 0x{expected:02x} got 0x{data[i]:02x}"
    return None


def main():
    total_msgs = NUM_CYCLES * len(SIZES)
    total_bytes = sum(SIZES) * NUM_CYCLES
    print(f"SPI Stress Test: {total_msgs} messages, ~{total_bytes/1e6:.1f} MB each direction")
    print()

    master = SpiMaster()

    # Wait for Pico
    print("Waiting for IRQ...", end=" ", flush=True)
    if not master.wait_for_irq(timeout_s=10.0):
        print("TIMEOUT")
        master.close()
        sys.exit(1)
    print("OK")

    # Initial sync: REQUEST/READ to clear any stale state
    result = master.request_and_read(timeout_s=2.0)
    if result is None:
        print("TIMEOUT on initial sync")
        master.close()
        sys.exit(1)
    print("Synced. Starting test...\n")

    tx_bytes = 0
    rx_bytes = 0
    tx_errors = 0
    rx_errors = 0
    t0 = time.monotonic()
    seq = 0

    for cycle in range(NUM_CYCLES):
        for size in SIZES:
            # WRITE
            payload = make_write_payload(seq, size)
            header = struct.pack(">BH", SPI_CMD_WRITE, len(payload))
            master._spi.xfer2(list(header) + list(payload))
            tx_bytes += size

            # REQUEST/READ
            result = master.request_and_read(timeout_s=2.0)
            if result is None:
                rx_errors += 1
                print(f"ERR: timeout seq={seq} size={size}")
                seq += 1
                continue

            data, buf = result
            rx_bytes += len(data)

            err = verify_read_response(seq, data, size)
            if err is not None:
                rx_errors += 1
                print(f"ERR: seq={seq} size={size}: {err}")

            seq += 1

        if (cycle + 1) % 50 == 0:
            elapsed = time.monotonic() - t0
            print(f"  [{cycle+1}/{NUM_CYCLES}] {seq} msgs, "
                  f"{tx_bytes/1e6:.2f} MB tx, {rx_bytes/1e6:.2f} MB rx, "
                  f"{elapsed:.1f}s")

    elapsed = time.monotonic() - t0

    print()
    print("=" * 52)
    print("  Results")
    print("=" * 52)
    print(f"  Messages:   {seq}")
    print(f"  TX:         {tx_bytes:,} bytes ({tx_bytes/1e6:.2f} MB)")
    print(f"  RX:         {rx_bytes:,} bytes ({rx_bytes/1e6:.2f} MB)")
    print(f"  TX errors:  {tx_errors}")
    print(f"  RX errors:  {rx_errors}")
    print(f"  Time:       {elapsed:.1f}s")
    if elapsed > 0:
        print(f"  Throughput: {(tx_bytes+rx_bytes)/elapsed/1e3:.1f} KB/s")
    print("=" * 52)

    master.close()


if __name__ == "__main__":
    main()
