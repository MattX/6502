mod bus;
mod disassemble;
mod via;

use bus::MattbrewBus;
use mos6502::cpu::CPU;
use mos6502::instruction::Cmos6502;
use wasm_bindgen::prelude::*;

#[wasm_bindgen]
pub struct Emulator {
    cpu: CPU<MattbrewBus, Cmos6502>,
}

#[wasm_bindgen]
impl Emulator {
    #[wasm_bindgen(constructor)]
    pub fn new() -> Emulator {
        console_error_panic_hook::set_once();
        let mut cpu = CPU::new(MattbrewBus::new(), Cmos6502);
        cpu.reset();
        Emulator { cpu }
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

    /// Execute instructions until the cycle budget is exhausted.
    /// Returns the number of cycles actually consumed.
    pub fn run_for_cycles(&mut self, budget: u32) -> u32 {
        let start = self.cpu.cycles;
        let target = start + budget as u64;
        while self.cpu.cycles < target {
            if !self.cpu.single_step() {
                break;
            }
        }
        (self.cpu.cycles - start) as u32
    }

    /// Load a ROM (max 8KB) and reset the CPU.
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
        self.cpu.memory.bridge.terminal.as_string()
    }

    /// Push keyboard input bytes for 6502 to read from device 2.
    pub fn send_keyboard_input(&mut self, data: &[u8]) {
        self.cpu.memory.bridge.keyboard_in.extend(data);
    }

    // --- Netboot (device 3) ---

    /// Upload a named binary file for netboot.
    pub fn upload_file(&mut self, name: &str, data: &[u8]) {
        self.cpu
            .memory
            .bridge
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

    // --- Disassembly ---

    /// Disassemble `lines` instructions starting at `addr`.
    pub fn disassemble_at(&self, addr: u16, lines: u32) -> String {
        disassemble::disassemble(&self.cpu.memory, addr, lines)
    }
}
