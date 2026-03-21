/*
 * LCD driver for mattbrew W65C02 homebrew computer.
 * Uses 8-bit mode with two VIA ports:
 *   PORTB (VIA_BASE+0): 8-bit data bus
 *   PORTA (VIA_BASE+1): control signals (E=0x80, RW=0x40, RS=0x20)
 *
 * Licensed under the Apache License, Version 2.0 with LLVM Exceptions,
 * See https://github.com/llvm-mos/llvm-mos-sdk/blob/main/LICENSE for license
 * information.
 */

#include <mattbrew.h>

#define VIA_PORTB   (*((volatile unsigned char *)(VIA_BASE + 0x00)))
#define VIA_PORTA   (*((volatile unsigned char *)(VIA_BASE + 0x01)))
#define VIA_DDRB    (*((volatile unsigned char *)(VIA_BASE + 0x02)))
#define VIA_DDRA    (*((volatile unsigned char *)(VIA_BASE + 0x03)))

// Control signals on PORTA.
#define LCD_E       0x80
#define LCD_RW      0x40
#define LCD_RS      0x20

void lcd_init(void)
{
    // Set PORTA control pins to outputs.
    VIA_DDRA = LCD_E | LCD_RW | LCD_RS;

    // Set PORTB data pins to outputs.
    VIA_DDRB = 0xFF;

    // Set up the display in 8-bit mode.
    lcd_instruction(LCD_I_FUNC_SET);
    lcd_instruction(LCD_I_DISP_ON);
    lcd_instruction(LCD_I_MODE_INC);
    lcd_instruction(LCD_I_CLEAR);
}

// Wait for the LCD to not be busy.
static void lcd_wait(void)
{
    unsigned char status;

    // Convert PORTB data pins to inputs.
    VIA_DDRB = 0x00;

    // Wait until the LCD reports that it is not busy.
    do {
        VIA_PORTA = LCD_RW;
        VIA_PORTA = LCD_RW | LCD_E;
        status = VIA_PORTB;
        VIA_PORTA = LCD_RW;
    } while ((status & 0x80) != 0);

    // Return PORTB data pins to outputs.
    VIA_DDRB = 0xFF;
}

void lcd_instruction(unsigned char insn)
{
    lcd_wait();
    VIA_PORTB = insn;
    VIA_PORTA = 0;
    VIA_PORTA = LCD_E;
    VIA_PORTA = 0;
}

void lcd_putchar(unsigned char c)
{
    lcd_wait();
    VIA_PORTB = c;
    VIA_PORTA = LCD_RS;
    VIA_PORTA = LCD_RS | LCD_E;
    VIA_PORTA = LCD_RS;
}

void lcd_puts(const char *str)
{
    char c;
    while ((c = *str++) != '\0')
        lcd_putchar((unsigned char)c);
}
