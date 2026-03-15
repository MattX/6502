use vr_emu_lcd::{CharacterRom, VrEmuLcd};

const RS_BIT: u8 = 0x20; // Port A bit 5
const RW_BIT: u8 = 0x40; // Port A bit 6
const E_BIT: u8 = 0x80; // Port A bit 7

pub struct Via6522 {
    port_b_out: u8,
    port_a_out: u8,
    ddr_b: u8,
    ddr_a: u8,
    lcd: VrEmuLcd,
    lcd_read_latch: u8,
}

impl Via6522 {
    pub fn new() -> Self {
        Self {
            port_b_out: 0,
            port_a_out: 0,
            ddr_b: 0,
            ddr_a: 0,
            lcd: VrEmuLcd::new(16, 2, CharacterRom::A00),
            lcd_read_latch: 0,
        }
    }

    pub fn reset(&mut self) {
        self.port_b_out = 0;
        self.port_a_out = 0;
        self.ddr_b = 0;
        self.ddr_a = 0;
        self.lcd_read_latch = 0;
        // LCD retains state on VIA reset (matching real hardware)
    }

    pub fn read(&self, reg: u8) -> u8 {
        match reg & 0x0F {
            // ORB: output bits where DDR=1, LCD read latch where DDR=0
            0x00 => (self.port_b_out & self.ddr_b) | (self.lcd_read_latch & !self.ddr_b),
            // ORA
            0x01 | 0x0F => self.port_a_out,
            // DDRB
            0x02 => self.ddr_b,
            // DDRA
            0x03 => self.ddr_a,
            // All other registers (timers, shift, IRQ) — unimplemented
            _ => 0,
        }
    }

    pub fn write(&mut self, reg: u8, value: u8) {
        match reg & 0x0F {
            // ORB
            0x00 => {
                self.port_b_out = value;
            }
            // ORA
            0x01 | 0x0F => {
                let old = self.port_a_out;
                self.port_a_out = value;
                self.handle_lcd_strobe(old, value);
            }
            // DDRB
            0x02 => {
                self.ddr_b = value;
            }
            // DDRA
            0x03 => {
                self.ddr_a = value;
            }
            // All other registers — ignored
            _ => {}
        }
    }

    fn handle_lcd_strobe(&mut self, old: u8, new: u8) {
        let old_e = old & E_BIT;
        let new_e = new & E_BIT;

        if old_e == 0 && new_e != 0 {
            // E rising edge — latch read if RW=1
            if new & RW_BIT != 0 {
                self.lcd_read_latch = if new & RS_BIT != 0 {
                    self.lcd.read_byte()
                } else {
                    self.lcd.read_address()
                };
            }
        } else if old_e != 0 && new_e == 0 {
            // E falling edge — write to LCD if RW=0
            if new & RW_BIT == 0 {
                if new & RS_BIT != 0 {
                    self.lcd.write_byte(self.port_b_out);
                } else {
                    self.lcd.send_command(self.port_b_out);
                }
            }
        }
    }

    pub fn lcd_pixels(&mut self, now_ms: u128) -> &[u8] {
        self.lcd.update_pixels(now_ms);
        self.lcd.pixels()
    }

    pub fn lcd_width(&self) -> usize {
        self.lcd.num_pixels_x()
    }

    pub fn lcd_height(&self) -> usize {
        self.lcd.num_pixels_y()
    }
}
