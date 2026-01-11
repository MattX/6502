/*
 * 6522 VIA Interface for Keyboard Adapter
 *
 * Implements handshaking protocol with 6522 VIA chip
 * - 8 data pins (GPIO 26, 27, 28, 29, 24, 25, 18, 19)
 * - CA1 (GPIO 1) - output to signal CPU (active low)
 * - CA2 (GPIO 2) - input from CPU (falling edge indicates data read)
 */

#ifndef VIA_INTERFACE_H
#define VIA_INTERFACE_H

#include <stdint.h>
#include <stdbool.h>

// Initialize the VIA interface (GPIO pins, interrupts, buffer)
void via_init(void);

// Add a keystroke to the buffer (returns false if buffer is full)
bool via_add_keystroke(uint8_t key);

// Process the VIA handshaking (call this in main loop)
void via_task(void);

// Get number of keystrokes in buffer
uint8_t via_buffer_count(void);

#endif // VIA_INTERFACE_H
