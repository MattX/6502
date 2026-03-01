mod spi_master;
mod terminal;
mod ui;

use std::collections::VecDeque;
use std::fs;
use std::io;
use std::time::Duration;

use anyhow::Result;
use crossterm::event::{self, Event, KeyCode, KeyEvent, KeyModifiers};
use crossterm::terminal::{disable_raw_mode, enable_raw_mode, EnterAlternateScreen, LeaveAlternateScreen};
use crossterm::ExecutableCommand;
use ratatui::backend::CrosstermBackend;

use spi_master::{IrqWatcher, SpiMaster, MAX_PAYLOAD, NUM_DEVICES};
use terminal::Terminal;
use ui::StatusInfo;

const NETBOOT_FILE: &str = "boot.bin";
const MAX_TLV_DATA: usize = 254; // 255 reserved for busy
const LOG_CAPACITY: usize = 1000;

/// Parse a SPI payload containing complete TLV packets (no straddling).
fn parse_tlv_payload(payload: &[u8]) -> Vec<(u8, Vec<u8>)> {
    let mut msgs = Vec::new();
    let mut pos = 0;
    while pos + 2 <= payload.len() {
        let device = payload[pos];
        let length = payload[pos + 1] as usize;
        if pos + 2 + length > payload.len() {
            break;
        }
        msgs.push((device, payload[pos + 2..pos + 2 + length].to_vec()));
        pos += 2 + length;
    }
    msgs
}

struct App {
    master: SpiMaster,
    irq: IrqWatcher,
    terminal: Terminal,
    log: Vec<String>,
    verbose: bool,
    status: StatusInfo,
    running: bool,
    /// Per-device outgoing TLV queues (already framed, ready to write).
    tx_queues: [VecDeque<Vec<u8>>; NUM_DEVICES],
}

impl App {
    fn new(master: SpiMaster, irq: IrqWatcher) -> Self {
        Self {
            status: StatusInfo {
                device_status: 0,
                buf: master.buf,
                connected: true,
                verbose: false,
            },
            master,
            irq,
            terminal: Terminal::new(),
            log: Vec::new(),
            verbose: false,
            running: true,
            tx_queues: Default::default(),
        }
    }

    fn log(&mut self, msg: String) {
        if self.log.len() >= LOG_CAPACITY {
            self.log.remove(0);
        }
        self.log.push(msg);
    }

    fn log_verbose(&mut self, msg: String) {
        if self.verbose {
            self.log(msg);
        }
    }

    /// Poll for and handle crossterm keyboard events.
    fn handle_input(&mut self) -> Result<()> {
        while event::poll(Duration::ZERO)? {
            if let Event::Key(key) = event::read()? {
                if key.kind != event::KeyEventKind::Press {
                    continue;
                }
                if key.modifiers.contains(KeyModifiers::CONTROL) && key.code == KeyCode::Char('c') {
                    self.running = false;
                    return Ok(());
                }
                if key.code == KeyCode::F(1) {
                    self.verbose = !self.verbose;
                    self.status.verbose = self.verbose;
                    let state = if self.verbose { "ON" } else { "off" };
                    self.log(format!("Verbose mode: {state}"));
                    continue;
                }
                if let Some(bytes) = key_to_bytes(&key) {
                    self.enqueue_tlv(2, &bytes);
                }
            }
        }
        Ok(())
    }

    /// Check IRQ and drain all pending SPI data.
    fn drain_spi(&mut self) -> Result<()> {
        if !self.irq.is_asserted()? {
            return Ok(());
        }

        loop {
            let result = self.master.request_and_read(Duration::from_millis(100))?;
            match result {
                Some((payload, _buf)) => {
                    self.status.buf = self.master.buf;
                    if !payload.is_empty() {
                        self.log_verbose(format!("SPI RX {} bytes", payload.len()));
                        for (device, data) in parse_tlv_payload(&payload) {
                            self.dispatch_rx(device, &data);
                        }
                    }
                    if payload.len() < MAX_PAYLOAD {
                        break;
                    }
                }
                None => break,
            }
        }
        Ok(())
    }

    /// Dispatch a received TLV message by device ID.
    fn dispatch_rx(&mut self, device: u8, data: &[u8]) {
        match device {
            0 => {
                // Status device
                if !data.is_empty() {
                    self.status.device_status = data[0];
                }
                if data.len() > 1 {
                    // Error string from Pico
                    let msg = String::from_utf8_lossy(&data[1..]);
                    self.log(format!("Pico: {msg}"));
                }
            }
            2 => {
                // Video output
                self.log_verbose(format!("Video RX {} bytes", data.len()));
                self.terminal.feed(data);
            }
            3 => {
                // Netboot request
                self.log("Netboot request received".to_string());
                self.send_netboot();
            }
            7 => {
                // Echo: log and send back
                let hex: Vec<String> = data.iter().map(|b| format!("{b:02x}")).collect();
                self.log(format!("Echo: {}", hex.join(" ")));
                self.enqueue_tlv(7, data);
            }
            _ => {
                self.log_verbose(format!("Device {device}: {} bytes", data.len()));
            }
        }
    }

    /// Enqueue a TLV message for transmission, splitting into chunks if needed.
    fn enqueue_tlv(&mut self, device: u8, data: &[u8]) {
        let dev = device as usize;
        if dev >= NUM_DEVICES {
            self.log(format!("enqueue_tlv: device {device} out of range, dropped"));
            return;
        }
        for chunk in data.chunks(MAX_TLV_DATA) {
            let mut payload = Vec::with_capacity(2 + chunk.len());
            payload.push(device);
            payload.push(chunk.len() as u8);
            payload.extend_from_slice(chunk);
            self.tx_queues[dev].push_back(payload);
        }
    }

    /// Drain per-device TX queues into a single SPI frame (up to MAX_PAYLOAD).
    /// Skips devices whose Pico buffer is full, avoiding head-of-line blocking.
    fn drain_tx_queue(&mut self) -> Result<()> {
        let mut frame = Vec::new();

        // Keep looping until no device can contribute a frame.
        loop {
            let mut progress = false;

            for dev in 0..NUM_DEVICES {
                while let Some(tlv) = self.tx_queues[dev].front() {
                    let data_len = tlv[1] as usize;
                    let tlv_len = 2 + data_len;

                    if frame.len() + tlv_len > MAX_PAYLOAD {
                        break;
                    }

                    // Cost in bytes (TLV header not stored in device buffer)
                    let cost = data_len as u16;
                    if cost > self.master.buf[dev] {
                        break; // This device is blocked, try the next one
                    }

                    let payload = self.tx_queues[dev].pop_front().unwrap();
                    frame.extend_from_slice(&payload);
                    self.master.buf[dev] = self.master.buf[dev].saturating_sub(cost);
                    progress = true;
                }
            }

            if !progress {
                break;
            }
        }

        if !frame.is_empty() {
            self.log_verbose(format!("SPI TX {} bytes", frame.len()));
            self.master.write(&frame)?;
            self.status.buf = self.master.buf;
        }

        Ok(())
    }

    /// Read boot.bin and enqueue it over device 3.
    fn send_netboot(&mut self) {
        match fs::read(NETBOOT_FILE) {
            Ok(data) => {
                self.log(format!("Netboot: sending {} bytes from {NETBOOT_FILE}", data.len()));
                self.enqueue_tlv(3, &data);
            }
            Err(e) => self.log(format!("Netboot: failed to read {NETBOOT_FILE}: {e}")),
        }
    }
}

/// Convert a crossterm KeyEvent to bytes for device 2 (keyboard).
fn key_to_bytes(key: &KeyEvent) -> Option<Vec<u8>> {
    match key.code {
        KeyCode::Char(c) => {
            if key.modifiers.contains(KeyModifiers::CONTROL) {
                // Ctrl+A = 0x01, Ctrl+B = 0x02, etc.
                let ctrl = (c.to_ascii_lowercase() as u8).wrapping_sub(b'a' - 1);
                if (1..=26).contains(&ctrl) {
                    return Some(vec![ctrl]);
                }
            }
            let mut buf = [0u8; 4];
            let s = c.encode_utf8(&mut buf);
            Some(s.as_bytes().to_vec())
        }
        KeyCode::Enter => Some(vec![b'\r']),
        KeyCode::Backspace => Some(vec![0x7f]),
        KeyCode::Tab => Some(vec![b'\t']),
        KeyCode::Esc => Some(vec![0x1b]),
        KeyCode::Up => Some(b"\x1b[A".to_vec()),
        KeyCode::Down => Some(b"\x1b[B".to_vec()),
        KeyCode::Right => Some(b"\x1b[C".to_vec()),
        KeyCode::Left => Some(b"\x1b[D".to_vec()),
        KeyCode::Home => Some(b"\x1b[H".to_vec()),
        KeyCode::End => Some(b"\x1b[F".to_vec()),
        KeyCode::Insert => Some(b"\x1b[2~".to_vec()),
        KeyCode::Delete => Some(b"\x1b[3~".to_vec()),
        KeyCode::PageUp => Some(b"\x1b[5~".to_vec()),
        KeyCode::PageDown => Some(b"\x1b[6~".to_vec()),
        _ => None,
    }
}

fn main() -> Result<()> {
    // Pre-TUI initialization: connect to SPI
    println!("Connecting to Pico...");

    let irq = IrqWatcher::new()?;
    let mut master = SpiMaster::new()?;

    // Wait for Pico
    print!("Waiting for IRQ... ");
    if !irq.is_asserted()? {
        if !irq.wait_edge(Duration::from_secs(10))? {
            println!("TIMEOUT");
            return Ok(());
        }
        irq.consume_edge()?;
    }
    println!("OK");

    // Initial sync
    if master.request_and_read(Duration::from_secs(2))?.is_none() {
        println!("TIMEOUT on initial sync");
        return Ok(());
    }
    println!("Connected (BUF={:?})", master.buf);

    // Set up TUI
    enable_raw_mode()?;
    io::stdout().execute(EnterAlternateScreen)?;
    let backend = CrosstermBackend::new(io::stdout());
    let mut tui = ratatui::Terminal::new(backend)?;

    let mut app = App::new(master, irq);
    app.log("Connected to Pico".to_string());

    // Main event loop
    let result = run_loop(&mut tui, &mut app);

    // Cleanup TUI
    disable_raw_mode()?;
    io::stdout().execute(LeaveAlternateScreen)?;

    result
}

fn run_loop(tui: &mut ratatui::Terminal<CrosstermBackend<io::Stdout>>, app: &mut App) -> Result<()> {
    while app.running {
        // Render
        tui.draw(|frame| {
            ui::draw(frame, &app.terminal, &app.status, &app.log);
        })?;

        // Poll crossterm events with a short timeout
        if event::poll(Duration::from_millis(10))? {
            app.handle_input()?;
        }

        // Check SPI
        app.drain_spi()?;

        // Drain TX queue
        app.drain_tx_queue()?;
    }
    Ok(())
}
