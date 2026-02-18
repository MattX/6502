#!/usr/bin/env python3
"""
SPI Read Blast Test

Sends REQUEST/READ as fast as possible, ignoring IRQ.
The Pico continuously queues 1500-byte messages; the Zero pulls them
and verifies the pattern.

READ payload: [seq_BE(4)] [pattern] where pattern[i] = (seq*7 + i) & 0xFF
"""

import struct
import sys
import time

from spi_master import SpiMaster


PAYLOAD_SIZE = 1500
TARGET_BYTES = 4 * 1024 * 1024  # 4 MB


def verify_payload(data: bytes, expected_seq: int) -> str | None:
    """Verify READ response pattern. Returns error string or None if OK."""
    if len(data) < 4:
        return f"too short ({len(data)} bytes)"
    if len(data) == 0:
        return "empty response"

    got_seq = struct.unpack(">I", data[:4])[0]
    if got_seq != expected_seq:
        return f"seq mismatch: expected {expected_seq}, got {got_seq}"

    for i in range(4, len(data)):
        expected = ((expected_seq * 7) + (i - 4)) & 0xFF
        if data[i] != expected:
            return (f"byte[{i}] expected 0x{expected:02x} "
                    f"got 0x{data[i]:02x}")
    return None


def main():
    target_msgs = TARGET_BYTES // PAYLOAD_SIZE
    print(f"SPI Read Blast: {PAYLOAD_SIZE}-byte payloads, "
          f"~{TARGET_BYTES/1e6:.0f} MB target ({target_msgs} messages)")
    print()

    master = SpiMaster()

    # Wait for Pico
    print("Waiting for IRQ...", end=" ", flush=True)
    if not master.wait_for_irq(timeout_s=10.0):
        print("TIMEOUT")
        master.close()
        sys.exit(1)
    print("OK")

    print("Starting blast...\n")

    rx_bytes = 0
    rx_msgs = 0
    rx_errors = 0
    empty_reads = 0
    t0 = time.monotonic()

    seq = 0
    while rx_msgs < target_msgs:
        result = master.request_and_read(timeout_s=2.0)
        if result is None:
            print(f"ERR: timeout on REQUEST/READ after seq={seq}")
            rx_errors += 1
            break

        data, buf = result

        if len(data) == 0:
            # Pico had nothing queued yet, just retry
            empty_reads += 1
            continue

        if len(data) != PAYLOAD_SIZE:
            print(f"ERR: unexpected length {len(data)} at seq={seq}")
            rx_errors += 1
            seq += 1
            rx_msgs += 1
            continue

        err = verify_payload(data, seq)
        if err is not None:
            rx_errors += 1
            print(f"ERR: seq={seq}: {err}")

        rx_bytes += len(data)
        rx_msgs += 1
        seq += 1

        if rx_msgs % 500 == 0:
            elapsed = time.monotonic() - t0
            print(f"  [{rx_msgs}/{target_msgs}] {rx_bytes/1e6:.2f} MB, "
                  f"{empty_reads} empty, {rx_errors} errors, {elapsed:.1f}s")

    elapsed = time.monotonic() - t0

    print()
    print("=" * 52)
    print("  Results")
    print("=" * 52)
    print(f"  Messages:    {rx_msgs}")
    print(f"  RX bytes:    {rx_bytes:,} ({rx_bytes/1e6:.2f} MB)")
    print(f"  Empty reads: {empty_reads}")
    print(f"  RX errors:   {rx_errors}")
    print(f"  Time:        {elapsed:.1f}s")
    if elapsed > 0:
        print(f"  Throughput:  {rx_bytes/elapsed/1e3:.1f} KB/s")
    print("=" * 52)

    master.close()


if __name__ == "__main__":
    main()
