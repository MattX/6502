#ifndef _MATTBREW_H_
#define _MATTBREW_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif


// VIA base address. Also update crt0/systick.S if this changes.
#define VIA_BASE    0xE000

// Get the value of the system millisecond tick counter.
unsigned long millis(void);

// Wait for a specific number of milliseconds.
void delay(unsigned ms);

// LCD instructions.
#define LCD_I_CLEAR     0x01    // Clear the display.
#define LCD_I_HOME      0x02    // Move the cursor to the home position.
#define LCD_I_MODE_INC  0x06    // Set mode to increment, no display shift.
#define LCD_I_DISP_OFF  0x08    // Display off, cursor off, blinking off.
#define LCD_I_DISP_ON   0x0E    // Display on, cursor on, blinking off.
#define LCD_I_MOVE_L    0x10    // Move the cursor left one position.
#define LCD_I_MOVE_R    0x14    // Move the cursor right one position.
#define LCD_I_SHIFT_L   0x18    // Move the display left one position.
#define LCD_I_SHIFT_R   0x1C    // Move the display right one position.
#define LCD_I_FUNC_SET  0x38    // 8-bit mode, 2 lines, 5x8 font.
#define LCD_I_CGRAM     0x40    // Set CGRAM address (offset in low bits).
#define LCD_I_DDRAM     0x80    // Set DDRAM address (offset in low bits).

// Initialize the LCD in 8-bit mode, 2 lines, 5x8 font and clear the display.
void lcd_init(void);

// Send an instruction to the LCD.
void lcd_instruction(unsigned char insn);

// Put a character to the LCD.
void lcd_putchar(unsigned char c);

// Put a string to the LCD.
void lcd_puts(const char *str);

// Address of the Pi Pico bridge.
#define RPI_BASE 0xE040

// Core read — returns bytes read (0 = no data)
uint8_t io_read(uint8_t device_id, uint8_t *buf);

// Core write
void io_write(uint8_t device_id, const uint8_t *buf, uint8_t len);

// Write a single byte
void io_write1(uint8_t device_id, uint8_t data);

#ifdef __cplusplus
}
#endif

#endif // not _MATTBREW_H_
