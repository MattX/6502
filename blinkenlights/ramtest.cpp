#include <cstdint>

#include <mattbrew.h>

// Avoid writing to stack and zeropage
const uint16_t LOW_RAM = 0x2000;
const uint16_t HIGH_RAM = 0x7FFF;

void lcd_putstr(const char* msg) {
    while (*msg != 0) {
        lcd_putchar(*msg);
        msg++;
    }
}

void lcd_putnum(uint8_t num) {
    if (num > 9) {
        lcd_putchar('?');
    } else {
        lcd_putchar('0' + num);
    }
}

void lcd_reset() {
    lcd_instruction(LCD_I_CLEAR);
    lcd_instruction(LCD_I_HOME);
}

void write_pattern(uint8_t pattern) {
    for (uint16_t addr = LOW_RAM; addr <= HIGH_RAM; addr++) {
        *((volatile uint8_t*)addr) = pattern;
    }
}

// Returns 0 if OK, or the address of the first mismatch
uint16_t check_pattern(uint8_t pattern) {
    for (uint16_t addr = LOW_RAM; addr <= HIGH_RAM; addr++) {
        uint8_t value = *((volatile uint8_t*)addr);
        if (value != pattern) {
            return addr;
        }
    }
    return 0;
}

int main() {
    lcd_init();
    lcd_reset();
    lcd_putstr("Ram test");

    lcd_instruction(LCD_I_DDRAM | 0x40);  // Move to second line
    write_pattern(0xAA);
    uint16_t error_addr = check_pattern(0xAA);
    if (error_addr != 0) {
        lcd_putstr("Err at 0x");
        lcd_putnum((error_addr >> 8) & 0xFF);
        lcd_putnum(error_addr & 0xFF);
    } else {
        lcd_putstr("0xAA OK");
    }

    lcd_instruction(LCD_I_DDRAM | 0x40);  // Move to second line
    write_pattern(0x55);
    error_addr = check_pattern(0x55);
    if (error_addr != 0) {
        lcd_putstr("Err at 0x");
        lcd_putnum((error_addr >> 8) & 0xFF);
        lcd_putnum(error_addr & 0xFF);
    } else {
        lcd_putstr("0x55 OK");
    }
}
