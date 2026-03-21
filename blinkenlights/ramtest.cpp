#include <cstdint>

#include <mattbrew.h>

// Avoid writing to stack and zeropage
const uint16_t LOW_RAM = 0x2000;
const uint16_t HIGH_RAM = 0x9BFF;  // Stop 1KB below stack at $A000

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

void lcd_puthex_nibble(uint8_t val) {
    val &= 0x0F;
    lcd_putchar(val < 10 ? '0' + val : 'A' + val - 10);
}

void lcd_puthex(uint8_t val) {
    lcd_puthex_nibble(val >> 4);
    lcd_puthex_nibble(val);
}

void lcd_puthex16(uint16_t val) {
    lcd_puthex((val >> 8) & 0xFF);
    lcd_puthex(val & 0xFF);
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

void ramtest() {
    lcd_reset();
    lcd_putstr("Ram test");

    lcd_instruction(LCD_I_DDRAM | 0x40);  // Move to second line
    write_pattern(0xAA);
    uint16_t error_addr = check_pattern(0xAA);
    if (error_addr != 0) {
        lcd_putstr("Err at 0x");
        lcd_puthex16(error_addr);
    } else {
        lcd_putstr("0xAA OK");
    }

    lcd_instruction(LCD_I_DDRAM | 0x40);  // Move to second line
    write_pattern(0x55);
    error_addr = check_pattern(0x55);
    if (error_addr != 0) {
        lcd_putstr("Err at 0x");
        lcd_puthex16(error_addr);
    } else {
        lcd_putstr("0x55 OK");
    }
}

// Global buffers — avoids soft stack overflow risk
uint8_t recv_buf[255];
uint8_t send_buf[255];

void devtest() {
    lcd_reset();
    lcd_putstr("Dev test");

    uint8_t i = 0;
    do {
        lcd_instruction(LCD_I_DDRAM | 0x40);  // Move to second line
        lcd_putstr("Waiting ");
        lcd_putnum(i);
        i++;

        uint8_t len = io_read(0, recv_buf);
        if (len != 2) {
            lcd_instruction(LCD_I_DDRAM | 0x40);  // Move to second line
            lcd_putstr("Bad len on dev 0");
            return;
        }
    } while (recv_buf[1] == 0);

    lcd_instruction(LCD_I_DDRAM | 0x40);  // Move to second line
    lcd_putstr("Got dev 0");

    for (uint8_t i = 0; i < 255; i++) {
        send_buf[i] = 0x42;
    }

    for (uint8_t i = 0; i < 3; i++) {
        io_write(7, send_buf, 255);
        lcd_instruction(LCD_I_DDRAM | 0x40);  // Move to second line
        lcd_putstr("Sent block ");
        lcd_putnum(i);
    }

    uint16_t total = 0;
    uint8_t loop = 0;
    while (total < 3 * 255) {
        uint8_t len = io_read(7, recv_buf);
        for (uint8_t i = 0; i < len; i++) {
            if (recv_buf[i] != 0x42) {
                lcd_instruction(LCD_I_DDRAM | 0x40);  // Move to second line
                lcd_putstr("Err ");
                lcd_puthex(recv_buf[i]);
                lcd_putstr(" @");
                lcd_puthex16(total + i);
                return;
            }
        }
        total += len;
        lcd_instruction(LCD_I_DDRAM | 0x40);  // Move to second line
        // "R:NN T:XXXX" - len and running total
        lcd_putstr("R:");
        lcd_puthex(len);
        lcd_putstr(" T:");
        lcd_puthex16(total);
        if (len > 0) loop++;
    }
    lcd_instruction(LCD_I_DDRAM | 0x40);  // Move to second line
    lcd_putstr("Received all OK");
}

int main() {
    lcd_init();

    ramtest();
    devtest();

    while (1) {}
}
