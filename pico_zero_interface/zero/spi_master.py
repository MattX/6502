"""
SPI Master Interface for Pico <-> Zero communication.

Implements the Zero (master) side of the protocol described in
pico_zero_interface/README.md.

Uses spidev for SPI transfers and gpiod for IRQ/READY GPIO handling.
"""

import datetime
import struct
import time

import gpiod
import spidev
from gpiod.line import Bias, Direction, Edge

# --- Protocol constants (must match Pico side) ---

SPI_CMD_WRITE = 0x01
SPI_CMD_REQUEST = 0x02
SPI_CMD_READ = 0x03

READ_SIZE = 1503        # 3-byte header + 1500-byte payload
MAX_PAYLOAD = 1500

# --- Pin assignments (BCM numbering) ---

GPIO_CHIP = "/dev/gpiochip0"
PIN_IRQ = 25            # Input, active low ("Pico has data")
PIN_READY = 24          # Input, active low ("TX DMA loaded, safe to READ")

# --- SPI settings ---

SPI_BUS = 0
SPI_DEVICE = 0          # CE0
SPI_SPEED_HZ = 1_000_000
SPI_MODE = 3            # CPOL=1, CPHA=1 (PL022 slave requires Mode 1 or 3 for multi-byte)


class SpiMaster:
    """Zero-side SPI master for the Pico <-> Zero protocol."""

    def __init__(self, spi_speed_hz=SPI_SPEED_HZ):
        self._spi = spidev.SpiDev()
        self._spi.open(SPI_BUS, SPI_DEVICE)
        self._spi.mode = SPI_MODE
        self._spi.max_speed_hz = spi_speed_hz

        # Track last-known buffer space on Pico (in 64-byte units).
        # Start pessimistic -- do a REQUEST/READ to get the real value.
        self.buf = 0

        # gpiod line request for IRQ and READY
        self._gpio = gpiod.request_lines(
            GPIO_CHIP,
            consumer="spi_master",
            config={
                PIN_IRQ: gpiod.LineSettings(
                    direction=Direction.INPUT,
                    bias=Bias.PULL_UP,
                    edge_detection=Edge.FALLING,
                ),
                PIN_READY: gpiod.LineSettings(
                    direction=Direction.INPUT,
                    bias=Bias.PULL_UP,
                ),
            },
        )

    def close(self):
        """Release SPI and GPIO resources."""
        self._spi.close()
        self._gpio.release()

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.close()

    # --- Low-level helpers ---

    def _irq_is_asserted(self):
        """Check if IRQ is currently low (Pico has data)."""
        return self._gpio.get_value(PIN_IRQ) == gpiod.line.Value.INACTIVE

    def _wait_ready(self, timeout_s=1.0):
        """Wait for READY to go low (Pico has loaded TX DMA).

        Returns True if READY asserted, False on timeout.
        """
        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline:
            if self._gpio.get_value(PIN_READY) == gpiod.line.Value.INACTIVE:
                return True
            time.sleep(0.0001)  # 100us poll interval
        return False

    def _wait_ready_deasserted(self, timeout_s=1.0):
        """Wait for READY to go high after a READ completes.

        Per protocol: Zero must wait for READY high before new transactions.
        Returns True if READY deasserted, False on timeout.
        """
        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline:
            if self._gpio.get_value(PIN_READY) == gpiod.line.Value.ACTIVE:
                return True
            time.sleep(0.0001)
        return False

    # --- Protocol operations ---

    def write(self, payload):
        """Send a WRITE transaction to the Pico.

        Args:
            payload: bytes or bytearray, up to MAX_PAYLOAD bytes.

        Returns:
            True if sent, False if payload too large or insufficient buffer.
        """
        if len(payload) > MAX_PAYLOAD:
            return False

        # Check local buffer estimate
        buf_needed = (len(payload) + 63) // 64  # Round up to 64-byte units
        if buf_needed > self.buf:
            return False

        header = struct.pack(">BH", SPI_CMD_WRITE, len(payload))
        self._spi.xfer2(list(header) + list(payload))

        # Decrement local buffer estimate
        self.buf -= buf_needed
        if self.buf < 0:
            self.buf = 0

        return True

    def request_and_read(self, timeout_s=1.0):
        """Send REQUEST, wait for READY, then send READ.

        Returns:
            (payload_bytes, buf_value) on success, or None on timeout/error.
        """
        # Step 1: Send REQUEST (single byte)
        self._spi.xfer2([SPI_CMD_REQUEST])

        # Step 2: Wait for READY
        if not self._wait_ready(timeout_s):
            return None

        # Step 3: Send READ (READ_SIZE bytes)
        tx_data = [SPI_CMD_READ] + [0x00] * (READ_SIZE - 1)
        rx_data = self._spi.xfer2(tx_data)

        # Step 4: Wait for READY to deassert before allowing new transactions
        self._wait_ready_deasserted(timeout_s=0.1)

        # Parse response: [LEN_HI] [LEN_LO] [BUF] [payload...]
        if len(rx_data) < 3:
            return None

        payload_len = (rx_data[0] << 8) | rx_data[1]
        self.buf = rx_data[2]

        if payload_len > MAX_PAYLOAD:
            payload_len = MAX_PAYLOAD  # Defensive

        payload = bytes(rx_data[3:3 + payload_len])
        return payload, self.buf

    def wait_for_irq(self, timeout_s=None):
        """Wait for the Pico to assert IRQ (falling edge).

        Args:
            timeout_s: Timeout in seconds, or None to block indefinitely.

        Returns:
            True if IRQ detected, False on timeout.
        """
        # Check if already asserted
        if self._irq_is_asserted():
            return True

        # Wait for falling edge event
        timeout_ns = None
        if timeout_s is not None:
            timeout_ns = int(timeout_s * 1e9)
            if timeout_ns <= 0:
                return False

        # gpiod v2: wait_edge_events takes timeout as timedelta or seconds
        if self._gpio.wait_edge_events(
            datetime.timedelta(seconds=timeout_s) if timeout_s else None
        ):
            # Consume the event
            self._gpio.read_edge_events()
            return True
        return False
