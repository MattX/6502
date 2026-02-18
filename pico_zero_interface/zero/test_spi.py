#!/usr/bin/env python3
"""
SPI Master Debug Test

Minimal test: waits for IRQ, does one REQUEST/READ, sends one WRITE,
then does another REQUEST/READ. Dumps raw hex at every step.

Usage:
  sudo python3 test_spi.py
"""

import time
from spi_master import SpiMaster, SPI_BUS, SPI_DEVICE, READ_SIZE


def hex_dump(data, max_bytes=32):
    """Print hex dump of first max_bytes."""
    show = data[:max_bytes]
    hex_str = " ".join(f"{b:02x}" for b in show)
    if len(data) > max_bytes:
        hex_str += " ..."
    return hex_str


def main():
    print()
    print("=" * 52)
    print("  SPI Debug Test")
    print("=" * 52)
    print()

    master = SpiMaster()
    print(f"SPI: /dev/spidev{SPI_BUS}.{SPI_DEVICE}, "
          f"speed {master._spi.max_speed_hz / 1e6:.1f} MHz")
    print(f"READ_SIZE={READ_SIZE}")
    print("-" * 52)

    # Step 1: Wait for IRQ
    print("\n[1] Waiting for IRQ...")
    while True:
        if master.wait_for_irq(timeout_s=1.0):
            print("    IRQ detected!")
            break
        print("    (still waiting...)")

    # Step 2: REQUEST/READ
    print("\n[2] Sending REQUEST (0x02)...")
    master._spi.xfer2([0x02])
    print("    REQUEST sent. Waiting for READY...")

    if not master._wait_ready(timeout_s=2.0):
        print("    TIMEOUT waiting for READY!")
        master.close()
        return
    print("    READY asserted!")

    print("    Sending READ (0x03 + zeros)...")
    tx_data = [0x03] + [0x00] * (READ_SIZE - 1)
    rx_data = master._spi.xfer2(tx_data)
    print(f"    Received {len(rx_data)} bytes")
    print(f"    First 32: {hex_dump(rx_data, 32)}")
    print(f"    rx[0:2] (LEN): {(rx_data[0] << 8) | rx_data[1]}")
    print(f"    rx[2]   (BUF): {rx_data[2]}")

    print("    Waiting for READY deassert...")
    if master._wait_ready_deasserted(timeout_s=1.0):
        print("    READY deasserted.")
    else:
        print("    TIMEOUT waiting for READY deassert!")

    time.sleep(0.5)

    # Step 3: WRITE
    msg = b"Hello from Zero!"
    print(f"\n[3] Sending WRITE: {msg}")
    header = bytes([0x01, 0x00, len(msg)])
    full = header + msg
    print(f"    TX ({len(full)} bytes): {hex_dump(full)}")
    master._spi.xfer2(list(full))
    print("    WRITE sent.")

    time.sleep(0.5)

    # Step 4: Another REQUEST/READ
    print("\n[4] Sending REQUEST (0x02)...")
    master._spi.xfer2([0x02])
    print("    Waiting for READY...")

    if not master._wait_ready(timeout_s=2.0):
        print("    TIMEOUT waiting for READY!")
        master.close()
        return
    print("    READY asserted!")

    print("    Sending READ...")
    tx_data2 = [0x03] + [0x00] * (READ_SIZE - 1)
    rx_data = master._spi.xfer2(tx_data2)
    print(f"    Received {len(rx_data)} bytes")
    print(f"    First 32: {hex_dump(rx_data, 32)}")
    print(f"    rx[0:2] (LEN): {(rx_data[0] << 8) | rx_data[1]}")
    print(f"    rx[2]   (BUF): {rx_data[2]}")

    print("    Waiting for READY deassert...")
    if master._wait_ready_deasserted(timeout_s=1.0):
        print("    READY deasserted.")
    else:
        print("    TIMEOUT waiting for READY deassert!")

    print("\nDone.")
    master.close()


if __name__ == "__main__":
    main()
