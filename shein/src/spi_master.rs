use std::io::Write;
use std::time::{Duration, Instant};

use anyhow::{Context, Result};
use gpiocdev::line::{Bias, EdgeDetection, Value};
use gpiocdev::Request;
use spidev::{Spidev, SpidevOptions, SpidevTransfer, SpiModeFlags};

// Protocol commands (must match Pico side)
const SPI_CMD_WRITE: u8 = 0x01;
const SPI_CMD_REQUEST: u8 = 0x02;
const SPI_CMD_READ: u8 = 0x03;

// Transfer sizes
const READ_SIZE: usize = 1503; // 3-byte header + 1500-byte payload
pub const MAX_PAYLOAD: usize = 1500;

// GPIO (BCM numbering)
const GPIO_CHIP: &str = "/dev/gpiochip0";
const PIN_IRQ: u32 = 25; // Input, active-low, falling-edge ("Pico has data")
const PIN_READY: u32 = 24; // Input, active-low, polled ("TX DMA loaded")

// SPI
const SPI_DEVICE: &str = "/dev/spidev0.0";
const SPI_SPEED_HZ: u32 = 8_000_000;

/// Watches the IRQ GPIO pin for falling edges.
///
/// Kept separate from `SpiMaster` so the RX thread can block on IRQ
/// without holding the SPI mutex.
pub struct IrqWatcher {
    req: Request,
}

impl IrqWatcher {
    pub fn new() -> Result<Self> {
        let req = Request::builder()
            .on_chip(GPIO_CHIP)
            .with_line(PIN_IRQ)
            .as_input()
            .with_bias(Bias::PullUp)
            .with_edge_detection(EdgeDetection::FallingEdge)
            .with_consumer("shein-irq")
            .request()
            .context("Failed to request IRQ GPIO")?;
        Ok(Self { req })
    }

    /// Check if IRQ is currently asserted (pin is low).
    pub fn is_asserted(&self) -> Result<bool> {
        let val = self.req.value(PIN_IRQ)?;
        // Active-low: INACTIVE means the active condition is met (pin low)
        Ok(val == Value::Inactive)
    }

    /// Wait for a falling edge on IRQ, up to `timeout`.
    /// Returns true if an edge was detected, false on timeout.
    pub fn wait_edge(&self, timeout: Duration) -> Result<bool> {
        Ok(self.req.wait_edge_event(timeout)?)
    }

    /// Consume a pending edge event from the kernel buffer.
    pub fn consume_edge(&self) -> Result<()> {
        self.req.read_edge_event()?;
        Ok(())
    }
}

/// SPI master for the Pico <-> Zero protocol.
pub struct SpiMaster {
    spi: Spidev,
    ready: Request,
    /// Last-known buffer space on Pico (in 64-byte units).
    pub buf: u8,
}

impl SpiMaster {
    pub fn new() -> Result<Self> {
        let mut spi = Spidev::open(SPI_DEVICE).context("Failed to open SPI device")?;
        let options = SpidevOptions::new()
            .bits_per_word(8)
            .max_speed_hz(SPI_SPEED_HZ)
            .mode(SpiModeFlags::SPI_MODE_3)
            .build();
        spi.configure(&options).context("Failed to configure SPI")?;

        let ready = Request::builder()
            .on_chip(GPIO_CHIP)
            .with_line(PIN_READY)
            .as_input()
            .with_bias(Bias::PullUp)
            .with_consumer("shein-ready")
            .request()
            .context("Failed to request READY GPIO")?;

        Ok(Self { spi, ready, buf: 0 })
    }

    /// Poll READY pin until it goes low (asserted) or timeout.
    fn wait_ready(&self, timeout: Duration) -> Result<bool> {
        let deadline = Instant::now() + timeout;
        while Instant::now() < deadline {
            if self.ready.value(PIN_READY)? == Value::Inactive {
                return Ok(true);
            }
            std::thread::sleep(Duration::from_micros(100));
        }
        Ok(false)
    }

    /// Poll READY pin until it goes high (deasserted) or timeout.
    fn wait_ready_deasserted(&self, timeout: Duration) -> Result<bool> {
        let deadline = Instant::now() + timeout;
        while Instant::now() < deadline {
            if self.ready.value(PIN_READY)? == Value::Active {
                return Ok(true);
            }
            std::thread::sleep(Duration::from_micros(100));
        }
        Ok(false)
    }

    /// Send a WRITE transaction to the Pico.
    ///
    /// Returns `Ok(true)` if sent, `Ok(false)` if insufficient buffer space.
    pub fn write(&mut self, payload: &[u8]) -> Result<bool> {
        if payload.len() > MAX_PAYLOAD {
            return Ok(false);
        }

        let buf_needed = ((payload.len() + 3 + 63) / 64) as u8;
        if buf_needed > self.buf {
            return Ok(false);
        }

        let len = payload.len() as u16;
        let mut tx = Vec::with_capacity(3 + payload.len());
        tx.push(SPI_CMD_WRITE);
        tx.push((len >> 8) as u8);
        tx.push((len & 0xFF) as u8);
        tx.extend_from_slice(payload);

        self.spi
            .write_all(&tx)
            .context("SPI WRITE transfer failed")?;

        self.buf = self.buf.saturating_sub(buf_needed);
        Ok(true)
    }

    /// Send REQUEST, wait for READY, then send READ.
    ///
    /// Returns `Ok(Some((payload, buf)))` on success, `Ok(None)` on timeout.
    pub fn request_and_read(&mut self, timeout: Duration) -> Result<Option<(Vec<u8>, u8)>> {
        // Step 1: Send REQUEST
        self.spi
            .write_all(&[SPI_CMD_REQUEST])
            .context("SPI REQUEST transfer failed")?;

        // Step 2: Wait for READY
        if !self.wait_ready(timeout)? {
            return Ok(None);
        }

        // Step 3: Full-duplex READ transfer
        let mut tx_buf = vec![0u8; READ_SIZE];
        tx_buf[0] = SPI_CMD_READ;
        let mut rx_buf = vec![0u8; READ_SIZE];

        let mut transfer = SpidevTransfer::read_write(&tx_buf, &mut rx_buf);
        self.spi
            .transfer(&mut transfer)
            .context("SPI READ transfer failed")?;

        // Step 4: Wait for READY to deassert
        let _ = self.wait_ready_deasserted(Duration::from_millis(100));

        // Parse response: [LEN_HI, LEN_LO, BUF, payload...]
        let payload_len = ((rx_buf[0] as usize) << 8) | (rx_buf[1] as usize);
        self.buf = rx_buf[2];

        let payload_len = payload_len.min(MAX_PAYLOAD);
        let payload = rx_buf[3..3 + payload_len].to_vec();

        Ok(Some((payload, self.buf)))
    }
}
