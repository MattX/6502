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

// Load a program from device 3 (netboot) into RAM at $0400 and jump to it.
// name must be <255 chars (guaranteed by term_getline's uint8_t length).
void cmd_load(const char* name) {
    uint16_t name_len = strlen(name);
    if (name_len == 0) {
        term_putstr("Usage: load <name>\n");
        return;
    }

    // Send filename to device 3
    io_write(3, (const uint8_t*)name, (uint8_t)name_len);

    // Read first chunk (spin until data available)
    static uint8_t load_buf[255];
    uint8_t len;
    do {
        len = io_read(3, load_buf);
    } while (len == 0);

    if (len < 2) {
        term_putstr("Error: bad response\n");
        return;
    }

    uint16_t total = ((uint16_t)load_buf[0] << 8) | load_buf[1];
    if (total == 0) {
        term_putstr("File not found: ");
        term_putstr(name);
        term_putstr("\n");
        return;
    }

    term_putstr("Loading ");
    term_puthex16(total);
    term_putstr(" bytes...\n");

    // Copy initial data (after 2-byte length header)
    uint8_t* dest = (uint8_t*)0x0400;
    uint16_t received = 0;
    for (uint8_t i = 2; i < len; i++) {
        *dest++ = load_buf[i];
        received++;
    }

    // Read remaining chunks
    while (received < total) {
        len = io_read(3, load_buf);
        if (len == 0) continue;
        for (uint8_t i = 0; i < len; i++) {
            *dest++ = load_buf[i];
            received++;
        }
    }

    term_putstr("OK, jumping to $0400\n");
    ((void (*)(void))0x0400)();
}

// --- ed: minimal line editor ---
// Buffer format: lines are \n-terminated, buffer ends with \0.

uint16_t ed_content_len(char* buf) {
    uint16_t len = 0;
    while (buf[len] != 0) len++;
    return len;
}

uint16_t ed_count_lines(char* buf) {
    uint16_t count = 0;
    while (*buf != 0) {
        if (*buf == '\n') count++;
        buf++;
    }
    return count;
}

// Returns pointer to start of line n (1-based), or null if out of range.
char* ed_find_line(char* buf, uint16_t n) {
    if (n == 0) return nullptr;
    char* p = buf;
    uint16_t cur = 1;
    while (*p != 0) {
        if (cur == n) return p;
        if (*p == '\n') cur++;
        p++;
    }
    return nullptr;
}

// Returns pointer to the \n at end of line n, or null.
char* ed_find_line_end(char* buf, uint16_t n) {
    char* start = ed_find_line(buf, n);
    if (!start) return nullptr;
    while (*start != '\n' && *start != 0) start++;
    return (*start == '\n') ? start : nullptr;
}

void term_putdec(uint16_t val) {
    char tmp[6];
    uint8_t i = 0;
    if (val == 0) {
        term_putchar('0');
        return;
    }
    while (val > 0) {
        tmp[i++] = '0' + (val % 10);
        val /= 10;
    }
    while (i > 0) {
        term_putchar(tmp[--i]);
    }
}

// Insert text+\n after line `after` (0 = insert at beginning).
// Returns true on success.
bool ed_insert_after(char* buf, uint16_t max_len, uint16_t after, const char* text) {
    uint16_t text_len = strlen(text);
    uint16_t insert_len = text_len + 1;  // text + \n
    uint16_t cur_len = ed_content_len(buf);
    if (cur_len + insert_len + 1 > max_len) {
        term_putstr("Buffer full\n");
        return false;
    }

    // Find insertion point
    char* pos;
    if (after == 0) {
        pos = buf;
    } else {
        pos = ed_find_line_end(buf, after);
        if (!pos) {
            // Append at end
            pos = buf + cur_len;
        } else {
            pos++;  // past the \n
        }
    }

    // Shift content forward
    uint16_t tail_len = cur_len - (uint16_t)(pos - buf);
    for (uint16_t i = tail_len + 1; i > 0; i--) {
        pos[i - 1 + insert_len] = pos[i - 1];
    }

    // Copy in the new line
    for (uint16_t i = 0; i < text_len; i++) {
        pos[i] = text[i];
    }
    pos[text_len] = '\n';

    return true;
}

// Delete lines from..to (inclusive, 1-based).
bool ed_delete_lines(char* buf, uint16_t from, uint16_t to) {
    char* start = ed_find_line(buf, from);
    if (!start) return false;
    char* end = ed_find_line_end(buf, to);
    if (!end) return false;
    end++;  // past the \n

    // Shift content back
    uint16_t remaining = ed_content_len(buf) - (uint16_t)(end - buf);
    for (uint16_t i = 0; i <= remaining; i++) {
        start[i] = end[i];
    }
    return true;
}

// Parse "N", "N,M", or "," from str. Returns pointer past the parsed range.
// Sets from/to. If no number before comma, from=1. If no number after, to=last.
const char* ed_parse_range(const char* str, uint16_t total, uint16_t cur,
                           uint16_t* from, uint16_t* to, bool* has_comma) {
    *has_comma = false;
    *from = cur;
    *to = cur;

    // Parse first number
    bool has_first = false;
    uint16_t n = 0;
    while (*str >= '0' && *str <= '9') {
        n = n * 10 + (*str - '0');
        has_first = true;
        str++;
    }
    if (has_first) {
        *from = n;
        *to = n;
    }

    if (*str == ',') {
        *has_comma = true;
        str++;
        if (!has_first) *from = 1;
        // Parse second number
        bool has_second = false;
        n = 0;
        while (*str >= '0' && *str <= '9') {
            n = n * 10 + (*str - '0');
            has_second = true;
            str++;
        }
        *to = has_second ? n : total;
    }

    return str;
}

void cmd_ed(const char* args) {
    // Parse: <hex_addr> <hex_maxlen>
    // Find space separator
    const char* p = args;
    while (*p != ' ' && *p != 0) p++;
    if (*p == 0) {
        term_putstr("Usage: ed <addr> <maxlen>\n");
        return;
    }

    // Null-terminate the first arg temporarily by copying
    char addr_str[5];
    uint8_t addr_len = (uint8_t)(p - args);
    if (addr_len > 4) { term_putstr("Bad address\n"); return; }
    for (uint8_t i = 0; i < addr_len; i++) addr_str[i] = args[i];
    addr_str[addr_len] = 0;

    uint16_t addr, max_len;
    if (!parse_hex(addr_str, &addr)) { term_putstr("Bad address\n"); return; }
    p++;  // skip space
    if (!parse_hex(p, &max_len)) { term_putstr("Bad length\n"); return; }
    if (max_len < 2) { term_putstr("Buffer too small\n"); return; }

    char* buf = (char*)addr;
    // Initialize buffer if first byte isn't printable or \n
    // (assume empty if it looks like garbage)

    static char line_buf[255];
    uint16_t cur_line = 0;

    term_putstr("ed at $");
    term_puthex16(addr);
    term_putstr(" len $");
    term_puthex16(max_len);
    term_putstr("\n");

    while (true) {
        uint16_t total = ed_count_lines(buf);

        term_getline(line_buf);
        if (line_buf[0] == 0) continue;

        // Quit
        if (line_buf[0] == 'q' && line_buf[1] == 0) {
            return;
        }

        uint16_t from, to;
        bool has_comma;
        const char* cmd = ed_parse_range(line_buf, total, cur_line, &from, &to, &has_comma);

        char op = *cmd;

        if (op == 'p') {
            // Print
            if (from == 0) from = 1;
            if (to == 0) to = total;
            for (uint16_t i = from; i <= to; i++) {
                char* lp = ed_find_line(buf, i);
                if (!lp) break;
                term_putdec(i);
                term_putchar('\t');
                while (*lp != '\n' && *lp != 0) {
                    term_putchar(*lp);
                    lp++;
                }
                term_putchar('\n');
            }
        } else if (op == 'a') {
            // Append after line
            uint16_t after = from;
            while (true) {
                term_getline(line_buf);
                if (line_buf[0] == '.' && line_buf[1] == 0) break;
                if (ed_insert_after(buf, max_len, after, line_buf)) {
                    after++;
                    cur_line = after;
                }
            }
        } else if (op == 'd') {
            // Delete
            if (from == 0 || to == 0) {
                term_putstr("?\n");
            } else if (ed_delete_lines(buf, from, to)) {
                total = ed_count_lines(buf);
                if (cur_line > total) cur_line = total;
            } else {
                term_putstr("?\n");
            }
        } else if (op == 0 && from != cur_line) {
            // Bare number: set current line and print it
            if (from >= 1 && from <= total) {
                cur_line = from;
                char* lp = ed_find_line(buf, cur_line);
                if (lp) {
                    term_putdec(cur_line);
                    term_putchar('\t');
                    while (*lp != '\n' && *lp != 0) {
                        term_putchar(*lp);
                        lp++;
                    }
                    term_putchar('\n');
                }
            } else {
                term_putstr("?\n");
            }
        } else {
            term_putstr("?\n");
        }
    }
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
        } else if (starts_with((char*)recv_buf, "load ")) {
            cmd_load((char*)recv_buf + 5);
        } else if (starts_with((char*)recv_buf, "ed ")) {
            cmd_ed((char*)recv_buf + 3);
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
