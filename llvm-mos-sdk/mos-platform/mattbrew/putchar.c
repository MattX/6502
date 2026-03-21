/*
 * Route standard output to the LCD display.
 *
 * Licensed under the Apache License, Version 2.0 with LLVM Exceptions,
 * See https://github.com/llvm-mos/llvm-mos-sdk/blob/main/LICENSE for license
 * information.
 */

#include <stdio.h>

// Implemented in lcd.c.
void lcd_putchar(unsigned char c);

void __putchar(char c) { lcd_putchar((unsigned char)c); }
