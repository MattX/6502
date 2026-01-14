/*
 * 6522 VIA Interface Implementation
 */

#include "via_interface.h"
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include <stdio.h>

//--------------------------------------------------------------------+
// GPIO Pin Definitions
//--------------------------------------------------------------------+

// Data pins (D0-D7 in order)
static const uint8_t DATA_PINS[8] = {26, 27, 28, 29, 24, 25, 18, 19};

// Handshake pins (GPIO0/1 reserved for UART console output)
#define CA1_PIN 3  // Output: pulse low to signal CPU that data is ready
#define CA2_PIN 4  // Input: falling edge indicates CPU has read data

//--------------------------------------------------------------------+
// Keystroke Buffer (circular buffer)
//--------------------------------------------------------------------+

#define BUFFER_SIZE 64
static uint8_t keystroke_buffer[BUFFER_SIZE];
static volatile uint8_t buffer_head = 0;  // Write position
static volatile uint8_t buffer_tail = 0;  // Read position

//--------------------------------------------------------------------+
// Handshake State Machine
//--------------------------------------------------------------------+

typedef enum {
    VIA_IDLE,           // No data to send
    VIA_DATA_READY,     // Data on pins, waiting to pulse CA1
    VIA_WAITING_ACK     // Pulsed CA1, waiting for CA2 falling edge
} via_state_t;

static volatile via_state_t via_state = VIA_IDLE;
static volatile bool ca2_ack_received = false;
static uint32_t ca1_pulse_time = 0;

//--------------------------------------------------------------------+
// Helper Functions
//--------------------------------------------------------------------+

// Get number of items in buffer
static inline uint8_t buffer_count_internal(void) {
    return (buffer_head - buffer_tail) & (BUFFER_SIZE - 1);
}

// Check if buffer is empty
static inline bool buffer_empty(void) {
    return buffer_head == buffer_tail;
}

// Check if buffer is full
static inline bool buffer_full(void) {
    return buffer_count_internal() == (BUFFER_SIZE - 1);
}

// Get next byte from buffer (doesn't remove it)
static inline uint8_t buffer_peek(void) {
    return keystroke_buffer[buffer_tail];
}

// Remove byte from buffer
static inline void buffer_pop(void) {
    buffer_tail = (buffer_tail + 1) & (BUFFER_SIZE - 1);
}

//--------------------------------------------------------------------+
// CA2 Interrupt Handler
//--------------------------------------------------------------------+

void ca2_gpio_callback(uint gpio, uint32_t events) {
    if (gpio == CA2_PIN && (events & GPIO_IRQ_EDGE_FALL)) {
        // CPU has acknowledged receipt of data
        ca2_ack_received = true;
    }
}

//--------------------------------------------------------------------+
// Public Functions
//--------------------------------------------------------------------+

void via_init(void) {
    printf("Initializing VIA interface...\r\n");

    // Initialize data pins as outputs
    for (int i = 0; i < 8; i++) {
        gpio_init(DATA_PINS[i]);
        gpio_set_dir(DATA_PINS[i], GPIO_OUT);
        gpio_put(DATA_PINS[i], 0);
    }

    // Initialize CA1 as output (idle high, pulse low to signal)
    gpio_init(CA1_PIN);
    gpio_set_dir(CA1_PIN, GPIO_OUT);
    gpio_put(CA1_PIN, 1);  // Idle high

    // Initialize CA2 as input with pull-up
    gpio_init(CA2_PIN);
    gpio_set_dir(CA2_PIN, GPIO_IN);
    gpio_pull_up(CA2_PIN);

    // Set up interrupt on CA2 falling edge
    gpio_set_irq_enabled_with_callback(CA2_PIN, GPIO_IRQ_EDGE_FALL, true, &ca2_gpio_callback);

    // Initialize buffer
    buffer_head = 0;
    buffer_tail = 0;
    via_state = VIA_IDLE;

    printf("VIA interface initialized: Data pins ready, CA1=%d (out), CA2=%d (in)\r\n",
           CA1_PIN, CA2_PIN);
}

bool via_add_keystroke(uint8_t key) {
    if (buffer_full()) {
        printf("VIA buffer full! Dropping keystroke: 0x%02X\r\n", key);
        return false;
    }

    keystroke_buffer[buffer_head] = key;
    buffer_head = (buffer_head + 1) & (BUFFER_SIZE - 1);

    return true;
}

uint8_t via_buffer_count(void) {
    return buffer_count_internal();
}

void via_task(void) {
    uint32_t current_time = time_us_32();

    switch (via_state) {
        case VIA_IDLE:
            // Check if there's data to send
            if (!buffer_empty()) {
                uint8_t data = buffer_peek();

                // Set data pins
                for (int i = 0; i < 8; i++) {
                    gpio_put(DATA_PINS[i], (data >> i) & 1);
                }

                // Move to ready state
                via_state = VIA_DATA_READY;
                ca1_pulse_time = current_time;
            }
            break;

        case VIA_DATA_READY:
            // Wait a bit for data to settle, then pulse CA1
            if (current_time - ca1_pulse_time >= 10) {  // 10us settle time
                // Pulse CA1 low (active low signal)
                gpio_put(CA1_PIN, 0);
                sleep_us(1);  // Brief low pulse
                gpio_put(CA1_PIN, 1);  // Return to high

                // Wait for acknowledgment
                ca2_ack_received = false;
                via_state = VIA_WAITING_ACK;
                ca1_pulse_time = current_time;
            }
            break;

        case VIA_WAITING_ACK:
            // Check if CPU acknowledged (CA2 went low)
            if (ca2_ack_received) {
                // CPU has read the data, remove from buffer
                buffer_pop();
                via_state = VIA_IDLE;
            } else if (current_time - ca1_pulse_time > 1000000) {  // 1 second timeout
                // Timeout waiting for acknowledgment
                printf("VIA timeout waiting for CA2 ack, resetting\r\n");
                via_state = VIA_IDLE;
                buffer_pop();  // Discard the byte
            }
            break;
    }
}
