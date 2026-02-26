pub const MAX_PAYLOAD: usize = 1542; // 257*6: room for 6 max-size TLV packets
pub const NUM_DEVICES: usize = 8;

// ── Linux (real hardware) ───────────────────────────────────────────────────

#[cfg(target_os = "linux")]
mod hw {
    use std::io::Write;
    use std::time::{Duration, Instant};

    use anyhow::{Context, Result};
    use gpiocdev::line::{Bias, EdgeDetection, Value};
    use gpiocdev::Request;
    use spidev::{Spidev, SpidevOptions, SpidevTransfer, SpiModeFlags};

    const SPI_CMD_WRITE: u8 = 0x01;
    const SPI_CMD_REQUEST: u8 = 0x02;
    const SPI_CMD_READ: u8 = 0x03;

    const READ_SIZE: usize = super::MAX_PAYLOAD + 10; // 8 buf + 2 len + payload

    const GPIO_CHIP: &str = "/dev/gpiochip0";
    const PIN_IRQ: u32 = 25;
    const PIN_READY: u32 = 24;

    const SPI_DEVICE: &str = "/dev/spidev0.0";
    const SPI_SPEED_HZ: u32 = 8_000_000;

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

        pub fn is_asserted(&self) -> Result<bool> {
            let val = self.req.value(PIN_IRQ)?;
            Ok(val == Value::Inactive)
        }

        pub fn wait_edge(&self, timeout: Duration) -> Result<bool> {
            Ok(self.req.wait_edge_event(timeout)?)
        }

        pub fn consume_edge(&self) -> Result<()> {
            self.req.read_edge_event()?;
            Ok(())
        }
    }

    pub struct SpiMaster {
        spi: Spidev,
        ready: Request,
        pub buf: [u8; super::NUM_DEVICES],
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

            Ok(Self { spi, ready, buf: [0u8; super::NUM_DEVICES] })
        }

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

        pub fn write(&mut self, payload: &[u8]) -> Result<bool> {
            if payload.len() > super::MAX_PAYLOAD {
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

            Ok(true)
        }

        pub fn request_and_read(&mut self, timeout: Duration) -> Result<Option<(Vec<u8>, [u8; super::NUM_DEVICES])>> {
            self.spi
                .write_all(&[SPI_CMD_REQUEST])
                .context("SPI REQUEST transfer failed")?;

            if !self.wait_ready(timeout)? {
                return Ok(None);
            }

            let mut tx_buf = vec![0u8; READ_SIZE];
            tx_buf[0] = SPI_CMD_READ;
            let mut rx_buf = vec![0u8; READ_SIZE];

            let mut transfer = SpidevTransfer::read_write(&tx_buf, &mut rx_buf);
            self.spi
                .transfer(&mut transfer)
                .context("SPI READ transfer failed")?;

            let _ = self.wait_ready_deasserted(Duration::from_millis(100));

            // Bytes 0..8: per-device buffer estimates (in 16-byte units)
            self.buf.copy_from_slice(&rx_buf[0..super::NUM_DEVICES]);

            // Bytes 8..10: payload length (big-endian)
            let payload_len = ((rx_buf[8] as usize) << 8) | (rx_buf[9] as usize);
            let payload_len = payload_len.min(super::MAX_PAYLOAD);
            let payload = rx_buf[10..10 + payload_len].to_vec();

            Ok(Some((payload, self.buf)))
        }
    }
}

// ── Stub (non-Linux, for development/testing) ───────────────────────────────

#[cfg(not(target_os = "linux"))]
mod hw {
    use std::time::Duration;
    use anyhow::Result;

    pub struct IrqWatcher;

    impl IrqWatcher {
        pub fn new() -> Result<Self> {
            Ok(Self)
        }

        pub fn is_asserted(&self) -> Result<bool> {
            Ok(false)
        }

        pub fn wait_edge(&self, timeout: Duration) -> Result<bool> {
            std::thread::sleep(timeout);
            Ok(false)
        }

        pub fn consume_edge(&self) -> Result<()> {
            Ok(())
        }
    }

    pub struct SpiMaster {
        pub buf: [u8; super::NUM_DEVICES],
    }

    impl SpiMaster {
        pub fn new() -> Result<Self> {
            Ok(Self { buf: [255u8; super::NUM_DEVICES] })
        }

        pub fn write(&mut self, payload: &[u8]) -> Result<bool> {
            if payload.len() > super::MAX_PAYLOAD {
                return Ok(false);
            }
            Ok(true)
        }

        pub fn request_and_read(&mut self, _timeout: Duration) -> Result<Option<(Vec<u8>, [u8; super::NUM_DEVICES])>> {
            self.buf = [255u8; super::NUM_DEVICES];
            Ok(Some((Vec::new(), self.buf)))
        }
    }
}

pub use hw::{IrqWatcher, SpiMaster};
