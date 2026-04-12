#include <cstdint>

#include <mattbrew.h>

// ===================
// Utility functions
// ===================

char nibble_char(uint8_t val) {
    val &= 0x0F;
    return val < 10 ? '0' + val : 'A' + val - 10;
}

uint16_t strlen(const char* str) {
    uint16_t len = 0;
    while (str[len] != 0) {
        len++;
    }
    return len;
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

// ===================
// LCD
// ===================

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

void lcd_puthex(uint8_t val) {
    lcd_putchar(nibble_char(val >> 4));
    lcd_putchar(nibble_char(val));
}

// ===================
// Terminal I/O
// ===================

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

void term_puthex(uint8_t val) {
    term_putchar(nibble_char(val >> 4));
    term_putchar(nibble_char(val));
}

void term_puthex16(uint16_t val) {
    term_puthex((val >> 8) & 0xFF);
    term_puthex(val & 0xFF);
}

// line_buf must be 128 bytes or more.
void term_getline(char* line_buf) {
    static uint8_t recv_buf[8];
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
                if (total_len < 128) {
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

// Checks that |buf| has at least |minimum| bytes remaining starting from |*idx|. If not,
// copies remaining data between |*idx| and |*len| to the start of |buf|, updates |*len| and |*idx| accordingly,
// and pulls another chunk of data from device 3.
bool check_len(uint8_t minimum, uint8_t* buf, uint8_t* idx, uint8_t* len) {
    if (*idx + minimum < *len) {
        return true;
    }

    // Move remaining data to start of buffer
    uint8_t remaining = *len - *idx;
    for (uint8_t i = 0; i < remaining; i++) {
        buf[i] = buf[*idx + i];
    }
    *len = remaining;
    *idx = 0;

    *len += io_read(3, buf + remaining);
    return *len >= minimum;
}

uint8_t blocking_read(uint8_t* buf) {
    uint8_t len;
    do {
        len = io_read(3, buf);
    } while (len == 0);
    return len;
}

// The netboot device on 3 returns data in chunks of up to 128 bytes, plus we need
// a little margin to store a previous header's worth of data.
static uint8_t buf[128 + 6];

// Load a program from device 3 (netboot) into RAM.
// name must be <255 chars (guaranteed by term_getline's uint8_t length).
void cmd_load(const char* name) {
    uint8_t name_len = strlen(name);
    if (name_len == 0) {
        term_putstr("Usage: load <name>\n");
        return;
    }

    // Send filename to device 3
    io_write(3, (const uint8_t*)name, (uint8_t)name_len);

    // Buffer holds 1 full chunk (128 bytes) plus 5 bytes for header parsing.
    uint8_t len = blocking_read(buf);
    uint8_t idx = 0;

    // For the header, we can get away with not using next_byte, since we know the 
    // first chunk will contain the entire header.
    if (len == 0) {
        term_putstr("not found\n");
        return;
    }

    if (len < 6) {
        term_putstr("response too short\n");
        return;
    }
    if (buf[idx] != 0x45 || buf[idx+1] != 0x69 || buf[idx+2] != 0x1) {
        term_putstr("invalid binary magic\n");
        return;
    }
    idx += 3;
    uint16_t entrypoint = buf[idx++];
    entrypoint |= buf[idx++] << 8;
    uint8_t section_count = buf[idx++];
    uint8_t current_section = 0;

    while (current_section < section_count) {
        check_len(5, buf, &idx, &len);
        uint16_t section_addr = buf[idx++];
        section_addr |= buf[idx++] << 8;
        term_putstr("Loading section ");
        term_puthex16(current_section);
        term_putstr(" to 0x");
        term_puthex16(section_addr);
        term_putstr("\n");
        if (section_addr < 0x0400) {
            term_putstr("invalid section address ");
            term_puthex16(section_addr);
            term_putstr("\n");
            return;
        }
        uint8_t bank = buf[idx++];
        if (bank != 0xFF) {
            (*(volatile uint8_t*)0xE040) = bank;
        }
        uint16_t section_len = buf[idx++];
        section_len |= buf[idx++] << 8;
        if (section_addr + section_len > 0xdfff) {
            term_putstr("section 0x");
            term_puthex16(current_section);
            term_putstr(" out of bounds\n");
            return;
        }
        uint8_t* section_data = (uint8_t*)section_addr;
        
        while (section_len > 0) {
            if (idx < len) {
                *section_data++ = buf[idx++];
                section_len--;
            } else {
                // Buffer is empty, read another chunk
                len = blocking_read(buf);
                idx = 0;
            }
        }

        current_section++;
    }

    term_putstr("Load complete, entrypoint=0x");
    term_puthex16(entrypoint);
    term_putstr("\n");
    ((void (*)(void))entrypoint)();
}

int main() {
    lcd_init();
    lcd_putstr("Waiting for Zero...");

    do {
        uint8_t len = io_read(0, buf);
        if (len != 2) {
            lcd_instruction(LCD_I_DDRAM | 0x40);  // Move to second line
            lcd_putstr("Bad len on dev 0");
            return 1;
        }
    } while (buf[1] == 0);

    lcd_reset();
    lcd_putstr("Ready");

    term_putstr("Mattbrew 6502 ready\n\n");
    while (true) {
        term_putstr("> ");
        term_getline((char*)buf);
        if (starts_with((char*)buf, "lcd ")) {
            lcd_reset();
            lcd_putstr((char*)buf + 4);
        } else if (starts_with((char*)buf, "load ")) {
            cmd_load((char*)buf + 5);
        } else if (starts_with((char*)buf, "peek ")) {
            uint16_t address;
            bool ok = parse_hex((char*)buf + 5, &address);
            if (ok) {
                uint8_t value = *(volatile uint8_t*)address;
                term_puthex16(address);
                term_putstr(": ");
                term_puthex(value);
                term_putstr("\n");
            } else {
                term_putstr("Invalid address ");
                term_putstr((char*)buf + 5);
                term_putstr("\n");
            }
        } else if (starts_with((char*)buf, "poke ")) {
            char* args = (char*)buf + 5;
            char* space = nullptr;
            for (char* p = args; *p != 0; p++) {
                if (*p == ' ') {
                    space = p;
                    break;
                }
            }
            if (space == nullptr) {
                term_putstr("Usage: poke <address> <value>\n");
                continue;
            }
            *space = 0;  // Split into two strings
            char* addr_str = args;
            char* value_str = space + 1;

            uint16_t address;
            uint8_t value;
            bool ok = parse_hex(addr_str, &address) && parse_hex(value_str, (uint16_t*)&value);
            if (ok) {
                *(volatile uint8_t*)address = value;
                term_puthex16(address);
                term_putstr(" <= ");
                term_puthex(value);
                term_putstr("\n");
            } else {
                term_putstr("Usage: poke <address> <value>\n");
            }
        } else if (starts_with((char*)buf, "jump ")) {
            uint16_t address;
            bool ok = parse_hex((char*)buf + 5, &address);
            if (ok) {
                term_putstr("=> ");
                term_puthex16(address);
                term_putstr("\n");
                ((void (*)(void))address)();
            } else {
                term_putstr("Invalid address ");
                term_putstr((char*)buf + 5);
                term_putstr("\n");
            }
        } else {
            term_putstr("Unknown command\n");
        }
    }
}
