#![allow(dead_code)]

use std::collections::{HashMap, VecDeque};

use mos6502::cpu::CPU;
use mos6502::instruction::Cmos6502;
use mos6502::memory::Bus;

use crate::bus::{BridgeBuf, DeviceHandler, MattbrewBus, PacketEntry, ROM_SIZE};

/// Mock device handler for testing — queues pre-programmed responses
/// and captures all writes via the TlvBridge packet log.
pub struct MockDevices {
    responses: HashMap<u8, VecDeque<Vec<u8>>>,
}

impl MockDevices {
    fn new() -> Self {
        Self {
            responses: HashMap::new(),
        }
    }
}

impl DeviceHandler for MockDevices {
    fn dispatch_write(&mut self, _device: u8, _data: &[u8]) {
        // No-op — writes are captured by TlvBridge::packet_log
    }

    fn prepare_read(&mut self, device: u8, buf: &mut BridgeBuf) {
        if let Some(queue) = self.responses.get_mut(&device) {
            if let Some(response) = queue.pop_front() {
                buf.push(response.len() as u8);
                for &b in &response {
                    buf.push(b);
                }
                return;
            }
        }
        // No response queued — return length 0
        buf.push(0);
    }

    fn clear(&mut self) {
        self.responses.clear();
    }
}

/// Test harness for running 6502 programs with mock devices.
pub struct TestHarness {
    pub cpu: CPU<MattbrewBus<MockDevices>, Cmos6502>,
}

impl TestHarness {
    /// Create a new harness with empty RAM/ROM.
    pub fn new() -> Self {
        let mut cpu = CPU::new(MattbrewBus::new(MockDevices::new()), Cmos6502);
        cpu.reset();
        Self { cpu }
    }

    /// Load a ROM binary and reset the CPU.
    pub fn load_rom(&mut self, data: &[u8]) {
        self.cpu.memory.load_rom(data);
        self.cpu.memory.clear_ram();
        self.cpu.reset();
    }

    /// Load a ROM from a file path and reset the CPU.
    pub fn load_rom_file(&mut self, path: &str) {
        let data = std::fs::read(path)
            .unwrap_or_else(|e| panic!("Failed to read ROM file '{}': {}", path, e));
        self.load_rom(&data);
    }

    /// Load a small program at $E100 with the reset vector pointing to it.
    pub fn load_program(&mut self, code: &[u8]) {
        let mut rom = vec![0xFF; ROM_SIZE];
        rom[..code.len()].copy_from_slice(code);
        // Reset vector at $FFFC-$FFFD → rom offset 0x1EFC-0x1EFD
        rom[0x1EFC] = 0x00; // low byte of $E100
        rom[0x1EFD] = 0xE1; // high byte of $E100
        self.load_rom(&rom);
    }

    // --- Device mocking ---

    /// Queue a response that `device` will return on the next read.
    /// Multiple calls queue multiple responses (FIFO).
    pub fn mock_device_read(&mut self, device: u8, response: Vec<u8>) {
        self.cpu
            .memory
            .bridge
            .handler
            .responses
            .entry(device)
            .or_insert_with(VecDeque::new)
            .push_back(response);
    }

    // --- Execution ---

    /// Run for up to `max_cycles` cycles. Returns actual cycles consumed.
    pub fn run(&mut self, max_cycles: u64) -> u64 {
        let start = self.cpu.cycles;
        let target = start + max_cycles;
        while self.cpu.cycles < target {
            if !self.cpu.single_step() {
                break;
            }
        }
        self.cpu.cycles - start
    }

    /// Run until PC reaches `addr` or `max_cycles` is exhausted.
    /// Panics on timeout.
    pub fn run_until_pc(&mut self, addr: u16, max_cycles: u64) {
        let start = self.cpu.cycles;
        let target = start + max_cycles;
        while self.cpu.cycles < target {
            if self.cpu.registers.program_counter == addr {
                return;
            }
            if !self.cpu.single_step() {
                panic!(
                    "CPU halted at PC={:#06X} after {} cycles (waiting for PC={:#06X})",
                    self.cpu.registers.program_counter,
                    self.cpu.cycles - start,
                    addr,
                );
            }
        }
        panic!(
            "Timed out after {} cycles waiting for PC={:#06X} (PC is {:#06X})",
            self.cpu.cycles - start,
            addr,
            self.cpu.registers.program_counter
        );
    }

    /// Execute a single instruction. Returns true if the CPU executed.
    pub fn step(&mut self) -> bool {
        self.cpu.single_step()
    }

    // --- Inspection ---

    /// Get all captured packet log entries (non-draining).
    pub fn packets(&self) -> &[PacketEntry] {
        &self.cpu.memory.bridge.packet_log
    }

    /// Drain all captured packet log entries.
    pub fn drain_packets(&mut self) -> Vec<PacketEntry> {
        self.cpu.memory.bridge.drain_packets()
    }

    /// Get the data payloads of all writes to a specific device.
    pub fn device_writes(&self, device: u8) -> Vec<&[u8]> {
        self.cpu
            .memory
            .bridge
            .packet_log
            .iter()
            .filter(|e| e.direction == 0 && e.device == device)
            .map(|e| e.data.as_slice())
            .collect()
    }

    /// Read a byte from memory without side effects.
    pub fn peek(&self, addr: u16) -> u8 {
        self.cpu.memory.peek(addr)
    }

    /// Write a byte directly through the bus (routes to RAM/VIA/bridge).
    pub fn poke(&mut self, addr: u16, value: u8) {
        self.cpu.memory.set_byte(addr, value);
    }

    // --- Registers ---

    pub fn pc(&self) -> u16 {
        self.cpu.registers.program_counter
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
    pub fn sp(&self) -> u8 {
        self.cpu.registers.stack_pointer.0
    }
    pub fn cycles(&self) -> u64 {
        self.cpu.cycles
    }
}
