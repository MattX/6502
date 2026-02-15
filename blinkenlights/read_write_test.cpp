#include <cstdint>
#include <mattbrew.h>

static volatile uint8_t* PI_ADDRESS = reinterpret_cast<std::uint8_t*>(0xA000);
static uint8_t data_buffer[255];

void write_dev(uint8_t device_id, uint8_t len, uint8_t* data) {
    *PI_ADDRESS = device_id;
    *PI_ADDRESS = len;
    for (uint8_t i = 0; i < len; i++) {
        *PI_ADDRESS = data[i];
    }
}

uint8_t read_dev(uint8_t device_id, uint8_t* data) {
    *PI_ADDRESS = device_id | 0x80;
    uint8_t len;
    do {
        len = *PI_ADDRESS;
    } while (len == 0xff);
    for (uint8_t i = 0; i < len; i++) {
        data[i] = *PI_ADDRESS;
    }
    return len;
}

void memset(uint8_t* buffer, uint8_t value, uint8_t len) {
    for (uint8_t i = 0; i < len; i++) {
        buffer[i] = value;
    }
}

bool check_eq(uint8_t* buffer, uint8_t value, uint8_t len) {
    for (uint8_t i = 0; i < len; i++) {
        if (buffer[i] != value) {
            return false;
        }
    }
    return true;
}

void lcd_putstr(const char* msg) {
    while (*msg != 0) {
        lcd_putchar(*msg);
        msg++;
    }
}

void lcd_putnum(uint8_t num) {
    if (num >= 8) {
        lcd_putchar('?');
    } else {
        lcd_putchar(num - '0');
    }
}

void error(uint8_t device, const char* msg) {
    lcd_instruction(LCD_I_CLEAR);
    lcd_instruction(LCD_I_HOME);

    static const char* err_msg = "Err ";
    lcd_putstr(err_msg);
    lcd_putnum(device);
    lcd_putchar(' ');
    lcd_putstr(msg);

    while (true) {}
}

int main() {
    lcd_init();

    lcd_instruction(LCD_I_CLEAR);
    lcd_instruction(LCD_I_HOME);
    lcd_putstr("Writing");
    // Write 255+128 bytes to each device
    for (uint8_t device = 0; device < 8; device++) {
        memset(data_buffer, device, 255);
        write_dev(device, 255, data_buffer);
        write_dev(device, 128, data_buffer);
    }

    lcd_instruction(LCD_I_CLEAR);
    lcd_instruction(LCD_I_HOME);
    lcd_putstr("Reading");
    for (uint8_t device = 0; device < 8; device++) {
        uint8_t len_1 = read_dev(device, data_buffer);
        if (len_1 != 254) {
            error(device, "len1");
        }
        if (!check_eq(data_buffer, device, len_1)) {
            error(device, "dat1");
        }
        uint8_t len_2 = read_dev(device, data_buffer);
        uint8_t expected_len_2 = 255+128-254;
        if (len_2 != expected_len_2) {
            error(device, "len2");
        }
        if (!check_eq(data_buffer, device, len_2)) {
            error(device, "dat2");
        }
    }

    lcd_instruction(LCD_I_CLEAR);
    lcd_instruction(LCD_I_HOME);
    lcd_putstr("Done!");
}
