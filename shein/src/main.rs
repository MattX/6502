mod spi_master;

use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex};
use std::time::Duration;

use anyhow::Result;
use rustyline::error::ReadlineError;

use spi_master::{IrqWatcher, SpiMaster, MAX_PAYLOAD};

/// RX reassembly buffer for TLV messages that may straddle SPI frame boundaries.
struct TlvRxBuf {
    buf: Vec<u8>,
}

impl TlvRxBuf {
    fn new() -> Self {
        Self { buf: Vec::new() }
    }

    /// Append raw SPI payload data and drain all complete TLV messages.
    fn push(&mut self, data: &[u8]) -> Vec<(u8, Vec<u8>)> {
        self.buf.extend_from_slice(data);
        let mut msgs = Vec::new();
        loop {
            if self.buf.len() < 2 {
                break;
            }
            let length = self.buf[1] as usize;
            if self.buf.len() < 2 + length {
                break; // Incomplete message, wait for more data
            }
            let device = self.buf[0];
            let data = self.buf[2..2 + length].to_vec();
            self.buf.drain(..2 + length);
            msgs.push((device, data));
        }
        msgs
    }
}

/// Format a TLV message for display: "device: aa bb cc ..."
fn format_msg(device: u8, data: &[u8]) -> String {
    let hex: Vec<String> = data.iter().map(|b| format!("{b:02x}")).collect();
    format!("{device}: {}", hex.join(" "))
}

/// Parse a single "device: hex hex ..." segment into (device_id, data).
fn parse_one_msg(s: &str) -> Option<(u8, Vec<u8>)> {
    let s = s.trim();
    if s.is_empty() {
        return None;
    }

    let (device_str, hex_str) = s.split_once(':')?;
    let device: u8 = device_str.trim().parse().ok()?;
    if device > 7 {
        eprintln!("  device must be 0-7, got {device}");
        return None;
    }

    let hex_str = hex_str.trim();
    if hex_str.is_empty() {
        return None;
    }

    let data: Result<Vec<u8>, _> = hex_str
        .split_whitespace()
        .map(|s| u8::from_str_radix(s, 16))
        .collect();
    let data = data.ok()?;

    if data.len() > 255 {
        eprintln!("  max 255 bytes, got {}", data.len());
        return None;
    }

    Some((device, data))
}

/// Parse user input, which may contain multiple messages separated by ';'.
/// Returns None if the entire line is unparseable.
fn parse_input(line: &str) -> Option<Vec<(u8, Vec<u8>)>> {
    let line = line.trim();
    if line.is_empty() {
        return None;
    }

    let msgs: Vec<(u8, Vec<u8>)> = line.split(';').filter_map(parse_one_msg).collect();

    if msgs.is_empty() { None } else { Some(msgs) }
}

/// Background RX thread: watches IRQ and drains incoming data.
fn rx_thread(master: Arc<Mutex<SpiMaster>>, irq: IrqWatcher, stop: Arc<AtomicBool>) {
    let mut rxbuf = TlvRxBuf::new();

    while !stop.load(Ordering::Relaxed) {
        // Check if IRQ is currently asserted
        if !irq.is_asserted().unwrap_or(false) {
            // Wait for falling edge (500ms timeout to check stop flag)
            match irq.wait_edge(Duration::from_millis(500)) {
                Ok(true) => {
                    irq.consume_edge().ok();
                }
                Ok(false) => continue, // Timeout, loop back to check stop
                Err(e) => {
                    eprintln!("\n  IRQ error: {e}");
                    return;
                }
            }
            continue;
        }

        // Drain loop: keep reading while payloads are full-size
        loop {
            if stop.load(Ordering::Relaxed) {
                break;
            }

            let result = {
                let mut m = master.lock().unwrap();
                m.request_and_read(Duration::from_millis(500))
            };

            match result {
                Ok(Some((payload, _buf))) => {
                    if !payload.is_empty() {
                        for (device, data) in rxbuf.push(&payload) {
                            eprint!("\r\x1b[KRX  {}\n> ", format_msg(device, &data));
                        }
                    }
                    if payload.len() < MAX_PAYLOAD {
                        break;
                    }
                }
                Ok(None) => break,
                Err(e) => {
                    eprintln!("\n  SPI error: {e}");
                    return;
                }
            }
        }
    }
}

/// Send a payload via SPI WRITE, refreshing the buffer if needed.
fn send_payload(master: &Arc<Mutex<SpiMaster>>, payload: &[u8]) -> Result<()> {
    let mut m = master.lock().unwrap();
    if !m.write(payload)? {
        // Buffer exhausted -- refresh
        if m.request_and_read(Duration::from_secs(2))?.is_some() {
            if !m.write(payload)? {
                eprintln!("  send failed (BUF={})", m.buf);
            }
        } else {
            eprintln!("  TIMEOUT refreshing buffer");
        }
    }
    Ok(())
}

fn main() -> Result<()> {
    println!("Bridge Terminal");
    println!("  Format: device: hex hex hex ... [; device: hex ...]");
    println!("  Example: 0: 48 65 6c 6c 6f");
    println!("  Multi:   0: cd ef; 4: 23 75");
    println!("  Ctrl-C to quit");
    println!();

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

    // Initial sync to get BUF value
    let result = master.request_and_read(Duration::from_secs(2))?;
    if result.is_none() {
        println!("TIMEOUT on initial sync");
        return Ok(());
    }
    println!("Connected (BUF={})\n", master.buf);

    let master = Arc::new(Mutex::new(master));
    let stop = Arc::new(AtomicBool::new(false));

    // Spawn RX thread
    let rx_handle = {
        let master = Arc::clone(&master);
        let stop = Arc::clone(&stop);
        std::thread::spawn(move || rx_thread(master, irq, stop))
    };

    // Main input loop
    let mut rl = rustyline::DefaultEditor::new()?;
    loop {
        match rl.readline("> ") {
            Ok(line) => {
                if let Some(msgs) = parse_input(&line) {
                    // Concatenate TLV messages, splitting into SPI frames at MAX_PAYLOAD
                    let mut payload = Vec::new();
                    for (device, data) in &msgs {
                        let tlv_len = 2 + data.len();
                        // Flush current payload if this TLV won't fit
                        if !payload.is_empty() && payload.len() + tlv_len > MAX_PAYLOAD {
                            send_payload(&master, &payload)?;
                            payload.clear();
                        }
                        payload.push(*device);
                        payload.push(data.len() as u8);
                        payload.extend_from_slice(data);
                    }
                    if !payload.is_empty() {
                        send_payload(&master, &payload)?;
                    }
                } else if !line.trim().is_empty() {
                    eprintln!("  format: device: hex hex hex ... [; device: hex ...]");
                }
            }
            Err(ReadlineError::Interrupted | ReadlineError::Eof) => break,
            Err(e) => return Err(e.into()),
        }
    }

    stop.store(true, Ordering::Relaxed);
    rx_handle.join().ok();
    Ok(())
}
