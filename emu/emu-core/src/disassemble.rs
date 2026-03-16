use core::fmt::Write;

use mos6502::Variant;
use mos6502::instruction::{AddressingMode, Cmos6502, Instruction};

use crate::bus::MattbrewBus;

/// Format an instruction mnemonic. Strips trailing "nd" variants (ADCnd → ADC)
/// and lowercases the debug output that mos6502 provides.
fn format_mnemonic(instr: Instruction) -> &'static str {
    use Instruction::*;
    match instr {
        ADC | ADCnd => "ADC",
        AND => "AND",
        ASL => "ASL",
        BCC => "BCC",
        BCS => "BCS",
        BEQ => "BEQ",
        BIT => "BIT",
        BMI => "BMI",
        BNE => "BNE",
        BPL => "BPL",
        BRA => "BRA",
        BRK | BRKcld => "BRK",
        BVC => "BVC",
        BVS => "BVS",
        CLC => "CLC",
        CLD => "CLD",
        CLI => "CLI",
        CLV => "CLV",
        CMP => "CMP",
        CPX => "CPX",
        CPY => "CPY",
        DEC => "DEC",
        DEX => "DEX",
        DEY => "DEY",
        EOR => "EOR",
        INC => "INC",
        INX => "INX",
        INY => "INY",
        JMP => "JMP",
        JSR => "JSR",
        LDA => "LDA",
        LDX => "LDX",
        LDY => "LDY",
        LSR => "LSR",
        NOP | NOP1 | NOPI | NOPZ | NOPZX | NOPA | NOPAX | NOPAX8 => "NOP",
        ORA => "ORA",
        PHA => "PHA",
        PHX => "PHX",
        PHY => "PHY",
        PHP => "PHP",
        PLA => "PLA",
        PLX => "PLX",
        PLY => "PLY",
        PLP => "PLP",
        ROL => "ROL",
        ROR => "ROR",
        RTI => "RTI",
        RTS => "RTS",
        SBC | SBCnd => "SBC",
        SEC => "SEC",
        SED => "SED",
        SEI => "SEI",
        STA => "STA",
        STX => "STX",
        STY => "STY",
        STZ => "STZ",
        TAX => "TAX",
        TAY => "TAY",
        TRB => "TRB",
        TSB => "TSB",
        TSX => "TSX",
        TXA => "TXA",
        TXS => "TXS",
        TYA => "TYA",
        WAI => "WAI",
        STP => "STP",
        BBR(bit) => match bit {
            0 => "BBR0",
            1 => "BBR1",
            2 => "BBR2",
            3 => "BBR3",
            4 => "BBR4",
            5 => "BBR5",
            6 => "BBR6",
            7 => "BBR7",
            _ => "BBR?",
        },
        BBS(bit) => match bit {
            0 => "BBS0",
            1 => "BBS1",
            2 => "BBS2",
            3 => "BBS3",
            4 => "BBS4",
            5 => "BBS5",
            6 => "BBS6",
            7 => "BBS7",
            _ => "BBS?",
        },
        RMB(bit) => match bit {
            0 => "RMB0",
            1 => "RMB1",
            2 => "RMB2",
            3 => "RMB3",
            4 => "RMB4",
            5 => "RMB5",
            6 => "RMB6",
            7 => "RMB7",
            _ => "RMB?",
        },
        SMB(bit) => match bit {
            0 => "SMB0",
            1 => "SMB1",
            2 => "SMB2",
            3 => "SMB3",
            4 => "SMB4",
            5 => "SMB5",
            6 => "SMB6",
            7 => "SMB7",
            _ => "SMB?",
        },
        SAX => "SAX",
        _ => "???",
    }
}

/// Format the operand based on addressing mode and operand bytes.
fn format_operand(mode: AddressingMode, bus: &MattbrewBus, addr: u16) -> String {
    let b1 = || bus.peek(addr.wrapping_add(1));
    let w = || {
        let lo = bus.peek(addr.wrapping_add(1)) as u16;
        let hi = bus.peek(addr.wrapping_add(2)) as u16;
        (hi << 8) | lo
    };

    match mode {
        AddressingMode::Accumulator => "A".into(),
        AddressingMode::Implied => String::new(),
        AddressingMode::Immediate => format!("#${:02X}", b1()),
        AddressingMode::ZeroPage => format!("${:02X}", b1()),
        AddressingMode::ZeroPageX => format!("${:02X},X", b1()),
        AddressingMode::ZeroPageY => format!("${:02X},Y", b1()),
        AddressingMode::Relative => {
            let offset = b1() as i8;
            // Branch target = addr + 2 (instruction size) + signed offset
            let target = addr.wrapping_add(2).wrapping_add(offset as u16);
            format!("${:04X}", target)
        }
        AddressingMode::Absolute => format!("${:04X}", w()),
        AddressingMode::AbsoluteX => format!("${:04X},X", w()),
        AddressingMode::AbsoluteY => format!("${:04X},Y", w()),
        AddressingMode::Indirect | AddressingMode::BuggyIndirect => format!("(${:04X})", w()),
        AddressingMode::IndexedIndirectX => format!("(${:02X},X)", b1()),
        AddressingMode::IndirectIndexedY => format!("(${:02X}),Y", b1()),
        AddressingMode::ZeroPageIndirect => format!("(${:02X})", b1()),
        AddressingMode::AbsoluteIndexedIndirect => format!("(${:04X},X)", w()),
        AddressingMode::ZeroPageRelative => {
            let zp = b1();
            let offset = bus.peek(addr.wrapping_add(2)) as i8;
            // Branch target = addr + 3 (instruction size) + signed offset
            let target = addr.wrapping_add(3).wrapping_add(offset as u16);
            format!("${:02X},${:04X}", zp, target)
        }
    }
}

/// Disassemble `lines` instructions starting at `start_addr`.
/// Returns a multi-line string with one instruction per line.
pub fn disassemble(bus: &MattbrewBus, start_addr: u16, lines: u32) -> String {
    let mut result = String::new();
    let mut addr = start_addr;

    for i in 0..lines {
        let opcode = bus.peek(addr);
        let decoded = Cmos6502::decode(opcode);

        if i > 0 {
            result.push('\n');
        }

        match decoded {
            Some((instr, mode)) => {
                let mnemonic = format_mnemonic(instr);
                let operand = format_operand(mode, bus, addr);
                if operand.is_empty() {
                    let _ = write!(result, "{:04X}  {}", addr, mnemonic);
                } else {
                    let _ = write!(result, "{:04X}  {} {}", addr, mnemonic, operand);
                }
                addr = addr.wrapping_add(1 + mode.extra_bytes());
            }
            None => {
                let _ = write!(result, "{:04X}  .byte ${:02X}", addr, opcode);
                addr = addr.wrapping_add(1);
            }
        }
    }

    result
}
