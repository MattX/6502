#!/usr/bin/env python3
"""
Bridge Terminal

Interactive tool for the 6502 <-> Zero SPI bridge.
Displays received TLV messages (one per line) and lets you send messages
from a prompt.

Display format:  device_id: hex hex hex ...
Input format:    device_id: hex hex hex ...   (same)

Example:
  RX  0: 48 65 6c 6c 6f
  > 1: de ad be ef
"""

import datetime
import readline  # noqa: F401 — enables line editing in input()
import struct
import sys
import threading

from spi_master import MAX_PAYLOAD, SpiMaster


def parse_tlv(data: bytes) -> list[tuple[int, bytes]]:
    """Parse concatenated TLV frames from a payload.

    Returns list of (device_id, data) tuples.
    """
    msgs = []
    i = 0
    while i + 2 <= len(data):
        device = data[i]
        length = data[i + 1]
        i += 2
        if i + length > len(data):
            break
        msgs.append((device, data[i:i + length]))
        i += length
    return msgs


def format_msg(device: int, data: bytes) -> str:
    """Format a TLV message for display."""
    hex_str = " ".join(f"{b:02x}" for b in data)
    return f"{device}: {hex_str}"


def parse_input(line: str) -> tuple[int, bytes] | None:
    """Parse user input 'device: hex hex ...' into (device_id, data).

    Returns None on parse error.
    """
    line = line.strip()
    if not line:
        return None

    if ":" not in line:
        return None

    device_str, hex_str = line.split(":", 1)

    try:
        device = int(device_str.strip())
    except ValueError:
        return None

    if device < 0 or device > 7:
        print(f"  device must be 0-7, got {device}")
        return None

    hex_str = hex_str.strip()
    if not hex_str:
        return None

    try:
        data = bytes(int(b, 16) for b in hex_str.split())
    except ValueError:
        return None

    if len(data) > 255:
        print(f"  max 255 bytes, got {len(data)}")
        return None

    return device, data


def rx_thread_func(master: SpiMaster, spi_lock: threading.Lock,
                    stop_event: threading.Event):
    """Background thread: REQUEST/READ when IRQ asserted or payload was full."""
    while not stop_event.is_set():
        # Wait for IRQ (Pico has data)
        if not master._irq_is_asserted():
            # Block until falling edge or 500ms timeout (to check stop_event)
            if master._gpio.wait_edge_events(
                datetime.timedelta(milliseconds=500)
            ):
                master._gpio.read_edge_events()
            continue

        # Drain loop: keep reading while payloads are full-size
        while not stop_event.is_set():
            with spi_lock:
                try:
                    result = master.request_and_read(timeout_s=0.5)
                except Exception as e:
                    print(f"\n  SPI error: {e}")
                    return

            if result is None:
                break

            payload, buf = result
            if len(payload) > 0:
                msgs = parse_tlv(payload)
                for device, data in msgs:
                    print(f"\r\033[KRX  {format_msg(device, data)}")
                    print("> ", end="", flush=True)

            # If payload was max size, there may be more data queued
            if len(payload) < MAX_PAYLOAD:
                break


def main():
    print("Bridge Terminal")
    print("  Format: device: hex hex hex ...")
    print("  Example: 0: 48 65 6c 6c 6f")
    print("  Ctrl-C to quit")
    print()

    master = SpiMaster()

    # Wait for Pico
    print("Waiting for IRQ...", end=" ", flush=True)
    if not master.wait_for_irq(timeout_s=10.0):
        print("TIMEOUT")
        master.close()
        sys.exit(1)
    print("OK")

    # Initial sync to get BUF value
    result = master.request_and_read(timeout_s=2.0)
    if result is None:
        print("TIMEOUT on initial sync")
        master.close()
        sys.exit(1)
    print(f"Connected (BUF={master.buf})\n")

    spi_lock = threading.Lock()
    stop_event = threading.Event()
    rx = threading.Thread(target=rx_thread_func,
                          args=(master, spi_lock, stop_event), daemon=True)
    rx.start()

    try:
        while True:
            try:
                line = input("> ")
            except EOFError:
                break

            parsed = parse_input(line)
            if parsed is None:
                if line.strip():
                    print("  format: device: hex hex hex ...")
                continue

            device, data = parsed

            # Pack as TLV and send via SPI WRITE
            tlv = struct.pack("BB", device, len(data)) + data
            with spi_lock:
                if not master.write(tlv):
                    # Buffer exhausted, refresh via REQUEST/READ
                    result = master.request_and_read(timeout_s=2.0)
                    if result is None:
                        print("  TIMEOUT refreshing buffer")
                        continue
                    if not master.write(tlv):
                        print(f"  send failed (BUF={master.buf})")
                        continue

    except KeyboardInterrupt:
        print()

    stop_event.set()
    rx.join(timeout=2.0)
    master.close()


if __name__ == "__main__":
    main()
