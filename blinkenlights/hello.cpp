#include <cstdint>
#include <mattbrew.h>

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
    lcd_putnum('5');
}
