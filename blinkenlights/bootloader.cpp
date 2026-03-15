#include <cstdint>

#include <mattbrew.h>

uint16_t strlen(const char* str) {
    uint16_t len = 0;
    while (str[len] != 0) {
        len++;
    }
    return len;
}

void lcd_putstr(const char* msg) {
    while (*msg != 0) {
        lcd_putchar(*msg);
        msg++;
    }
}

void lcd_reset() {
    lcd_instruction(LCD_I_CLEAR);
    lcd_instruction(LCD_I_HOME);
}

void term_putstr(const char* msg) {
    uint16_t len = strlen(msg);
    while (len > 0) {
        uint8_t chunk_len = len > 255 ? 255 : len;
        io_write(2, (const uint8_t*)msg, chunk_len);
        msg += chunk_len;
        len -= chunk_len;
    }
}

void term_putchar(char c) {
    io_write(2, (const uint8_t*)&c, 1);
}

char nibble_char(uint8_t val) {
    val &= 0x0F;
    return val < 10 ? '0' + val : 'A' + val - 10;
}

void lcd_puthex(uint8_t val) {
    lcd_putchar(nibble_char(val >> 4));
    lcd_putchar(nibble_char(val));
}

void term_puthex(uint8_t val) {
    term_putchar(nibble_char(val >> 4));
    term_putchar(nibble_char(val));
}

void term_puthex16(uint16_t val) {
    term_puthex((val >> 8) & 0xFF);
    term_puthex(val & 0xFF);
}

bool parse_hex(const char* str, uint16_t* out) {
    if (strlen(str) > 4) {
        return false;  // Too long for 16 bits
    }
    uint16_t result = 0;
    while (*str != 0) {
        char c = *str;
        uint8_t nibble;
        if (c >= '0' && c <= '9') {
            nibble = c - '0';
        } else if (c >= 'A' && c <= 'F') {
            nibble = c - 'A' + 10;
        } else if (c >= 'a' && c <= 'f') {
            nibble = c - 'a' + 10;
        } else {
            return false;  // Invalid character
        }
        result = (result << 4) | nibble;
        str++;
    }
    *out = result;
    return true;
}

// line_buf must be 255 bytes or more.
void term_getline(char* line_buf) {
    static uint8_t recv_buf[255];
    uint8_t total_len = 0;
    do {
        uint8_t len = io_read(2, recv_buf);
        if (len == 0) {
            continue;
        }
        for (uint8_t i = 0; i < len; i++) {
            uint8_t c = recv_buf[i];
            if (c == 0x08) {  // Backspace
                if (total_len > 0) {
                    total_len--;
                }
            } else {
                if (total_len < 255) {
                    line_buf[total_len] = c;
                    total_len++;
                }
            }
        }

        // Echo back the recv data
        io_write(2, recv_buf, len);
    } while (total_len < 1 || line_buf[total_len - 1] != '\n');
    line_buf[total_len - 1] = 0;
}

bool starts_with(const char* str, const char* prefix) {
    while (*prefix != 0) {
        if (*str != *prefix) {
            return false;
        }
        str++;
        prefix++;
    }
    return true;
}

int main() {
    static uint8_t recv_buf[255];
    lcd_init();
    lcd_putstr("Waiting for Zero...");

    do {
        uint8_t len = io_read(0, recv_buf);
        if (len != 2) {
            lcd_instruction(LCD_I_DDRAM | 0x40);  // Move to second line
            lcd_putstr("Bad len on dev 0");
            return 1;
        }
    } while (recv_buf[1] == 0);

    lcd_reset();
    lcd_putstr("Ready");

    term_putstr("Mattbrew 6502 ready\n\n");
    while (true) {
        term_putstr("> ");
        term_getline((char*)recv_buf);
        if (starts_with((char*)recv_buf, "lcd ")) {
            lcd_reset();
            lcd_putstr((char*)recv_buf + 4);
        } else if (starts_with((char*)recv_buf, "peek ")) {
            uint16_t address;
            bool ok = parse_hex((char*)recv_buf + 5, &address);
            if (ok) {
                uint8_t value = *(volatile uint8_t*)address;
                term_puthex16(address);
                term_putstr(": ");
                term_puthex(value);
                term_putstr("\n");
            } else {
                term_putstr("Invalid address ");
                term_putstr((char*)recv_buf + 5);
                term_putstr("\n");
            }
        } else {
            term_putstr("Unknown command\n");
        }
    }
}
