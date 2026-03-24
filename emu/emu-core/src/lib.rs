mod bus;
mod disassemble;
mod via;

#[cfg(test)]
mod test_harness;
#[cfg(test)]
mod tests;

use std::collections::HashSet;

use bus::{MattbrewBus, RealDevices};
use mos6502::cpu::CPU;
use mos6502::instruction::Cmos6502;
use wasm_bindgen::prelude::*;

#[wasm_bindgen]
pub struct Emulator {
    cpu: CPU<MattbrewBus<RealDevices>, Cmos6502>,
    breakpoints: HashSet<u16>,
    breakpoint_hit: bool,
}

#[wasm_bindgen]
impl Emulator {
    #[wasm_bindgen(constructor)]
    pub fn new() -> Emulator {
        console_error_panic_hook::set_once();
        let mut cpu = CPU::new(MattbrewBus::new(RealDevices::new()), Cmos6502);
        cpu.reset();
        Emulator { cpu, breakpoints: HashSet::new(), breakpoint_hit: false }
    }

    pub fn reset(&mut self) {
        self.cpu.memory.clear_ram();
        self.cpu.memory.bridge.clear();
        self.cpu.memory.via.reset();
        self.cpu.reset();
    }

    /// Execute a single instruction. Returns true if the CPU executed
    /// (false if halted/waiting).
    pub fn step(&mut self) -> bool {
        self.cpu.single_step()
    }

    /// Execute instructions until the cycle budget is exhausted,
    /// a terminal write occurs, or a breakpoint is hit.
    /// Returns the number of cycles actually consumed.
    pub fn run_for_cycles(&mut self, budget: u32) -> u32 {
        self.cpu.memory.bridge.handler.terminal_dirty = false;
        self.breakpoint_hit = false;
        let start = self.cpu.cycles;
        let target = start + budget as u64;
        while self.cpu.cycles < target {
            if !self.cpu.single_step() {
                break;
            }
            if self.cpu.memory.bridge.handler.terminal_dirty {
                break;
            }
            if !self.breakpoints.is_empty()
                && self.breakpoints.contains(&self.cpu.registers.program_counter)
            {
                self.breakpoint_hit = true;
                break;
            }
        }
        (self.cpu.cycles - start) as u32
    }

    /// Load a ROM (max $1F00 / 7936 bytes) and reset the CPU.
    pub fn load_rom(&mut self, data: &[u8]) {
        self.cpu.memory.load_rom(data);
        self.cpu.memory.clear_ram();
        self.cpu.reset();
    }

    // --- Register accessors ---

    pub fn pc(&self) -> u16 {
        self.cpu.registers.program_counter
    }

    pub fn sp(&self) -> u8 {
        self.cpu.registers.stack_pointer.0
    }

    pub fn a(&self) -> u8 {
        self.cpu.registers.accumulator
    }

    pub fn x(&self) -> u8 {
        self.cpu.registers.index_x
    }

    pub fn y(&self) -> u8 {
        self.cpu.registers.index_y
    }

    pub fn status(&self) -> u8 {
        self.cpu.registers.status.bits()
    }

    /// Cycle count as f64 (u64 not supported in wasm-bindgen; safe up to 2^53).
    pub fn cycles(&self) -> f64 {
        self.cpu.cycles as f64
    }

    // --- Memory ---

    /// Read a 256-byte page. Page 0 = 0x0000-0x00FF, page 1 = 0x0100-0x01FF, etc.
    pub fn read_page(&self, page: u8) -> Vec<u8> {
        let base = (page as u16) << 8;
        (0..256u16)
            .map(|offset| self.cpu.memory.peek(base.wrapping_add(offset)))
            .collect()
    }

    // --- Terminal I/O (device 2) ---

    /// Return the 40×25 terminal grid as a string.
    pub fn terminal_text(&self) -> String {
        self.cpu.memory.bridge.handler.terminal.as_string()
    }

    /// Push keyboard input bytes for 6502 to read from device 2.
    pub fn send_keyboard_input(&mut self, data: &[u8]) {
        self.cpu.memory.bridge.handler.keyboard_in.extend(data);
    }

    // --- Netboot (device 3) ---

    /// Upload a named binary file for netboot.
    pub fn upload_file(&mut self, name: &str, data: &[u8]) {
        self.cpu
            .memory
            .bridge
            .handler
            .uploaded_files
            .insert(name.to_string(), data.to_vec());
    }

    // --- LCD ---

    /// Get LCD pixel buffer. Each byte: 0=off, 1=on, 255=background.
    pub fn lcd_pixels(&mut self, now_ms: f64) -> Vec<u8> {
        self.cpu.memory.via.lcd_pixels(now_ms as u128).to_vec()
    }

    pub fn lcd_width(&self) -> usize {
        self.cpu.memory.via.lcd_width()
    }

    pub fn lcd_height(&self) -> usize {
        self.cpu.memory.via.lcd_height()
    }

    // --- Packet inspector ---

    /// Drain captured TLV packets as a flat byte buffer.
    /// Format per entry: [direction: 1] [device: 1] [len: 1] [data: len bytes]
    pub fn drain_packets(&mut self) -> Vec<u8> {
        let entries = self.cpu.memory.bridge.drain_packets();
        let mut out = Vec::new();
        for e in entries {
            out.push(e.direction);
            out.push(e.device);
            let len = e.data.len().min(255) as u8;
            out.push(len);
            out.extend_from_slice(&e.data[..len as usize]);
        }
        out
    }

    // --- Disassembly ---

    /// Disassemble `lines` instructions starting at `addr`.
    pub fn disassemble_at(&self, addr: u16, lines: u32) -> String {
        disassemble::disassemble(&self.cpu.memory, addr, lines)
    }

    // --- Bridge status ---

    pub fn bridge_status(&self) -> String {
        self.cpu.memory.bridge.status_summary()
    }

    // --- Breakpoints ---

    pub fn add_breakpoint(&mut self, addr: u16) {
        self.breakpoints.insert(addr);
    }

    pub fn remove_breakpoint(&mut self, addr: u16) {
        self.breakpoints.remove(&addr);
    }

    pub fn breakpoints(&self) -> Vec<u16> {
        self.breakpoints.iter().copied().collect()
    }

    pub fn breakpoint_hit(&self) -> bool {
        self.breakpoint_hit
    }
}
