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

int main() {
    lcd_init();
    lcd_instruction(LCD_I_CLEAR);
    lcd_instruction(LCD_I_HOME);
    lcd_putstr("Waiting");
    asm volatile ("sei");
    while (true) {
        asm volatile ("wai");
        lcd_instruction(LCD_I_CLEAR);
        lcd_instruction(LCD_I_HOME);
        lcd_putstr("Dev ");
        for (int i = 0; i < 8; i++) {
            uint8_t len = read_dev(i, data_buffer);
            if (len != 0) {
                lcd_putnum(i);
                lcd_putchar(',');
                write_dev(i, len, data_buffer);
            }
        }
    }
}
