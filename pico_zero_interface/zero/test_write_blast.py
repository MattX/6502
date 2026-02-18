#!/usr/bin/env python3
"""
SPI Write Blast Test

Sends 1500-byte WRITEs as fast as possible, using BUF flow control.
Blasts WRITEs until the local buffer estimate is too low, then does
a REQUEST/READ to get the actual free space from the Pico.

WRITE payload: [seq_BE(4)] [pattern] where pattern[i] = (seq + i) & 0xFF
"""

import struct
import sys
import time

from spi_master import SpiMaster


PAYLOAD_SIZE = 1500
TARGET_BYTES = 4 * 1024 * 1024  # 4 MB


def make_write_payload(seq: int) -> bytes:
    buf = bytearray(PAYLOAD_SIZE)
    struct.pack_into(">I", buf, 0, seq)
    for i in range(4, PAYLOAD_SIZE):
        buf[i] = (seq + (i - 4)) & 0xFF
    return bytes(buf)


def main():
    target_msgs = TARGET_BYTES // PAYLOAD_SIZE
    print(f"SPI Write Blast: {PAYLOAD_SIZE}-byte payloads, "
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

    # Initial sync: get starting BUF value
    result = master.request_and_read(timeout_s=2.0)
    if result is None:
        print("TIMEOUT on initial sync")
        master.close()
        sys.exit(1)
    print(f"Initial BUF={master.buf}")
    print("Starting blast...\n")

    tx_bytes = 0
    tx_msgs = 0
    refreshes = 0
    t0 = time.monotonic()

    seq = 0
    while tx_msgs < target_msgs:
        payload = make_write_payload(seq)

        if master.write(payload):
            tx_bytes += PAYLOAD_SIZE
            tx_msgs += 1
            seq += 1
        else:
            # Buffer estimate exhausted -- REQUEST/READ to refresh
            result = master.request_and_read(timeout_s=2.0)
            if result is None:
                print(f"ERR: timeout on REQUEST/READ after seq={seq}")
                break
            refreshes += 1

            if refreshes % 100 == 0:
                elapsed = time.monotonic() - t0
                print(f"  [{tx_msgs}/{target_msgs}] {tx_bytes/1e6:.2f} MB, "
                      f"BUF={master.buf}, {refreshes} refreshes, {elapsed:.1f}s")
            # Retry the write on next iteration

    elapsed = time.monotonic() - t0

    print()
    print("=" * 52)
    print("  Results")
    print("=" * 52)
    print(f"  Messages:    {tx_msgs}")
    print(f"  TX bytes:    {tx_bytes:,} ({tx_bytes/1e6:.2f} MB)")
    print(f"  BUF refresh: {refreshes}")
    print(f"  Time:        {elapsed:.1f}s")
    if elapsed > 0:
        print(f"  Throughput:  {tx_bytes/elapsed/1e3:.1f} KB/s")
    print("=" * 52)

    master.close()


if __name__ == "__main__":
    main()
