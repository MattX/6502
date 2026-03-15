use std::collections::{HashMap, VecDeque};

use crate::via::Via6522;
use mos6502::memory::Bus;

const RAM_SIZE: usize = 0x8000;
const ROM_SIZE: usize = 0x2000;
const ROM_START: u16 = 0xE000;

const TERM_COLS: usize = 40;
const TERM_ROWS: usize = 25;

pub struct TextGrid {
    cells: [u8; TERM_COLS * TERM_ROWS],
    cursor_row: usize,
    cursor_col: usize,
}

impl TextGrid {
    fn new() -> Self {
        Self {
            cells: [b' '; TERM_COLS * TERM_ROWS],
            cursor_row: 0,
            cursor_col: 0,
        }
    }

    fn clear(&mut self) {
        self.cells.fill(b' ');
        self.cursor_row = 0;
        self.cursor_col = 0;
    }

    fn put_char(&mut self, c: u8) {
        match c {
            0x08 => {
                // Backspace
                if self.cursor_col > 0 {
                    self.cursor_col -= 1;
                    self.cells[self.cursor_row * TERM_COLS + self.cursor_col] = b' ';
                }
            }
            0x0A | 0x0D => {
                // Newline / carriage return
                self.cursor_col = 0;
                self.cursor_row += 1;
                if self.cursor_row >= TERM_ROWS {
                    self.scroll_up();
                }
            }
            _ => {
                self.cells[self.cursor_row * TERM_COLS + self.cursor_col] = c;
                self.cursor_col += 1;
                if self.cursor_col >= TERM_COLS {
                    self.cursor_col = 0;
                    self.cursor_row += 1;
                    if self.cursor_row >= TERM_ROWS {
                        self.scroll_up();
                    }
                }
            }
        }
    }

    fn scroll_up(&mut self) {
        self.cells.copy_within(TERM_COLS.., 0);
        self.cells[TERM_COLS * (TERM_ROWS - 1)..].fill(b' ');
        self.cursor_row = TERM_ROWS - 1;
    }

    /// Return the grid as a string with newlines between rows, trailing spaces trimmed.
    pub fn as_string(&self) -> String {
        let mut s = String::with_capacity(TERM_COLS * TERM_ROWS + TERM_ROWS);
        for row in 0..TERM_ROWS {
            if row > 0 {
                s.push('\n');
            }
            let start = row * TERM_COLS;
            let line = &self.cells[start..start + TERM_COLS];
            let trimmed = match line.iter().rposition(|&b| b != b' ') {
                Some(last) => &line[..=last],
                None => &[],
            };
            // Safe: all bytes are ASCII printable or space
            for &b in trimmed {
                s.push(b as char);
            }
        }
        s
    }
}

/// Fixed-size buffer for bridge I/O — zero heap allocation on the hot path.
const BRIDGE_BUF_CAP: usize = 256;

struct BridgeBuf {
    data: [u8; BRIDGE_BUF_CAP],
    len: u8,
}

impl BridgeBuf {
    const fn new() -> Self {
        Self {
            data: [0; BRIDGE_BUF_CAP],
            len: 0,
        }
    }

    fn push(&mut self, byte: u8) {
        if (self.len as usize) < BRIDGE_BUF_CAP {
            self.data[self.len as usize] = byte;
            self.len += 1;
        }
    }

    fn as_slice(&self) -> &[u8] {
        &self.data[..self.len as usize]
    }
}

enum PortState {
    Idle,
    WriteLen {
        device: u8,
    },
    WriteData {
        device: u8,
        remaining: u8,
        buf: BridgeBuf,
    },
    ReadData {
        buf: BridgeBuf,
        pos: u8,
    },
}

struct NetbootState {
    data: Vec<u8>, // 2-byte BE length prefix + file contents
    offset: usize,
}

pub struct BridgePort {
    state: PortState,
    pub terminal: TextGrid,
    pub keyboard_in: VecDeque<u8>,
    pub echo: VecDeque<u8>,
    pub reset_requested: bool,
    pub uploaded_files: HashMap<String, Vec<u8>>,
    netboot: Option<NetbootState>,
}

impl BridgePort {
    fn new() -> Self {
        Self {
            state: PortState::Idle,
            terminal: TextGrid::new(),
            keyboard_in: VecDeque::new(),
            echo: VecDeque::new(),
            reset_requested: false,
            uploaded_files: HashMap::new(),
            netboot: None,
        }
    }

    pub fn clear(&mut self) {
        self.state = PortState::Idle;
        self.terminal.clear();
        self.keyboard_in.clear();
        self.reset_requested = false;
        self.netboot = None;
    }

    fn write_byte(&mut self, value: u8) {
        self.state = match std::mem::replace(&mut self.state, PortState::Idle) {
            PortState::Idle => {
                if value & 0x80 != 0 {
                    self.start_read(value & 0x7F)
                } else {
                    PortState::WriteLen { device: value }
                }
            }
            PortState::WriteLen { device } => {
                if value == 0 {
                    self.dispatch_write(device, &[]);
                    PortState::Idle
                } else {
                    PortState::WriteData {
                        device,
                        remaining: value,
                        buf: BridgeBuf::new(),
                    }
                }
            }
            PortState::WriteData {
                device,
                remaining,
                mut buf,
            } => {
                buf.push(value);
                if remaining == 1 {
                    self.dispatch_write(device, buf.as_slice());
                    PortState::Idle
                } else {
                    PortState::WriteData {
                        device,
                        remaining: remaining - 1,
                        buf,
                    }
                }
            }
            PortState::ReadData { .. } => {
                if value & 0x80 != 0 {
                    self.start_read(value & 0x7F)
                } else {
                    PortState::WriteLen { device: value }
                }
            }
        };
    }

    fn read_byte(&mut self) -> u8 {
        match &mut self.state {
            PortState::ReadData { buf, pos } => {
                let p = *pos as usize;
                if p < buf.len as usize {
                    let b = buf.data[p];
                    *pos += 1;
                    if *pos as usize >= buf.len as usize {
                        self.state = PortState::Idle;
                    }
                    b
                } else {
                    self.state = PortState::Idle;
                    0xFF
                }
            }
            _ => 0xFF,
        }
    }

    fn start_read(&mut self, device: u8) -> PortState {
        let mut buf = BridgeBuf::new();
        match device {
            0 => {
                let mut status: u8 = 0;
                if !self.keyboard_in.is_empty() {
                    status |= 1 << 2;
                }
                buf.push(2);
                buf.push(status);
                buf.push(1);
                println!(
                    "Bridge: Status read, keyboard_in len = {}",
                    self.keyboard_in.len()
                );
            }
            2 => {
                let available = self.keyboard_in.len().min(254);
                buf.push(available as u8);
                if available > 0 {
                    for _ in 0..available {
                        if let Some(b) = self.keyboard_in.pop_front() {
                            buf.push(b);
                        }
                    }
                }
            }
            3 => {
                if let Some(ref mut nb) = self.netboot {
                    let remaining = nb.data.len() - nb.offset;
                    let chunk = remaining.min(255);
                    buf.push(chunk as u8);
                    for i in 0..chunk {
                        buf.push(nb.data[nb.offset + i]);
                    }
                    nb.offset += chunk;
                    if nb.offset >= nb.data.len() {
                        self.netboot = None;
                    }
                } else {
                    buf.push(0);
                }
            }
            7 => {
                let echo_available = self.echo.len().min(254);
                buf.push(echo_available as u8);
                for _ in 0..echo_available {
                    if let Some(b) = self.echo.pop_front() {
                        buf.push(b);
                    }
                }
            }
            _ => {
                buf.push(0);
            }
        }
        PortState::ReadData { buf, pos: 0 }
    }

    fn dispatch_write(&mut self, device: u8, data: &[u8]) {
        match device {
            1 => {
                self.reset_requested = true;
            }
            2 => {
                for &c in data {
                    self.terminal.put_char(c);
                }
            }
            3 => {
                let name = String::from_utf8_lossy(data).to_string();
                if let Some(file_data) = self.uploaded_files.get(&name) {
                    let total_len = file_data.len() as u16;
                    let mut prefixed = Vec::with_capacity(2 + file_data.len());
                    prefixed.push((total_len >> 8) as u8);
                    prefixed.push((total_len & 0xFF) as u8);
                    prefixed.extend_from_slice(file_data);
                    self.netboot = Some(NetbootState {
                        data: prefixed,
                        offset: 0,
                    });
                } else {
                    self.netboot = Some(NetbootState {
                        data: vec![0, 0],
                        offset: 0,
                    });
                }
            }
            7 => {
                self.echo.extend(data);
            }
            _ => {}
        }
    }
}

pub struct MattbrewBus {
    ram: [u8; RAM_SIZE],
    rom: [u8; ROM_SIZE],
    pub via: Via6522,
    pub bridge: BridgePort,
}

impl MattbrewBus {
    pub fn new() -> Self {
        Self {
            ram: [0; RAM_SIZE],
            rom: [0xFF; ROM_SIZE],
            via: Via6522::new(),
            bridge: BridgePort::new(),
        }
    }

    /// Side-effect-free read for disassembly and memory inspection.
    pub fn peek(&self, address: u16) -> u8 {
        match address {
            0x0000..=0x7FFF => self.ram[address as usize],
            0xC000..=0xC7FF => self.via.read((address & 0x0F) as u8),
            0xC800..=0xCFFF => 0x00,
            0xE000..=0xFFFF => self.rom[(address - ROM_START) as usize],
            _ => 0xFF,
        }
    }

    pub fn load_rom(&mut self, data: &[u8]) {
        self.rom.fill(0xFF);
        let len = data.len().min(ROM_SIZE);
        self.rom[..len].copy_from_slice(&data[..len]);
    }

    pub fn clear_ram(&mut self) {
        self.ram.fill(0);
    }
}

impl Bus for MattbrewBus {
    fn get_byte(&mut self, address: u16) -> u8 {
        match address {
            0x0000..=0x7FFF => self.ram[address as usize],
            0xC000..=0xC7FF => self.via.read((address & 0x0F) as u8),
            0xC800..=0xCFFF => self.bridge.read_byte(),
            0xE000..=0xFFFF => self.rom[(address - ROM_START) as usize],
            _ => 0xFF,
        }
    }

    fn set_byte(&mut self, address: u16, value: u8) {
        match address {
            0x0000..=0x7FFF => self.ram[address as usize] = value,
            0xC000..=0xC7FF => self.via.write((address & 0x0F) as u8, value),
            0xC800..=0xCFFF => self.bridge.write_byte(value),
            _ => {}
        }
    }
}
