/*
 * PIO Bus Interface - Receive Only Test
 *
 * SAFE TEST MODE: MCU never drives the data bus.
 * All bytes written by the CPU are logged to USB serial as hex and ASCII.
 *
 * Use this to verify basic PIO timing and bus capture without
 * risking electrical damage from bus contention.
 */

#include <stdio.h>
#include <ctype.h>

#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"

#include "bus_interface_rx_only.pio.h"

// PIO
static PIO pio = pio0;
static uint sm = 0;

// Receive buffer (filled by IRQ handler)
#define RX_BUFFER_SIZE 256
static volatile uint8_t rx_buffer[RX_BUFFER_SIZE];
static volatile uint rx_head = 0;
static volatile uint rx_tail = 0;

// Statistics
static uint32_t total_bytes = 0;
static uint32_t last_report_time = 0;

// Line buffer for ASCII display
#define LINE_WIDTH 16
static uint8_t line_buffer[LINE_WIDTH];
static uint line_pos = 0;
static uint32_t line_addr = 0;

// PIO IRQ handler - called when PIO signals a write cycle
static void pio_irq_handler(void) {
    // Check if IRQ 0 is set (write cycle detected)
    if (pio_interrupt_get(pio, 0)) {
        // Read data pins immediately
        uint8_t data = bus_read_data_pins();

        // Store in ring buffer
        uint next_head = (rx_head + 1) % RX_BUFFER_SIZE;
        if (next_head != rx_tail) {
            rx_buffer[rx_head] = data;
            rx_head = next_head;
        }

        // Clear the IRQ
        pio_interrupt_clear(pio, 0);
    }
}

static void print_line(void) {
    if (line_pos == 0) return;

    // Print address
    printf("%08lX: ", line_addr);

    // Print hex
    for (uint i = 0; i < LINE_WIDTH; i++) {
        if (i < line_pos) {
            printf("%02X ", line_buffer[i]);
        } else {
            printf("   ");
        }
        if (i == 7) printf(" ");  // Extra space in middle
    }

    // Print ASCII
    printf(" |");
    for (uint i = 0; i < line_pos; i++) {
        char c = line_buffer[i];
        printf("%c", isprint(c) ? c : '.');
    }
    printf("|\n");

    line_addr += line_pos;
    line_pos = 0;
}

static void log_byte(uint8_t byte) {
    line_buffer[line_pos++] = byte;
    total_bytes++;

    if (line_pos >= LINE_WIDTH) {
        print_line();
    }
}

int main(void) {
    stdio_init_all();

    sleep_ms(2000);  // Wait for USB enumeration

    printf("\n");
    printf("================================================\n");
    printf("  PIO Bus Interface - RECEIVE ONLY (Safe Test)\n");
    printf("================================================\n");
    printf("\n");
    printf("** MCU NEVER DRIVES THE BUS - SAFE FOR TESTING **\n");
    printf("\n");
    printf("Pin mapping:\n");
    printf("  GPIO 0:     CS_N\n");
    printf("  GPIO 1:     PHI2\n");
    printf("  GPIO 6:     RW\n");
    printf("  GPIO 26-29: D0-D3\n");
    printf("  GPIO 24-25: D4-D5\n");
    printf("  GPIO 18-19: D6-D7\n");
    printf("\n");
    printf("All CPU writes will be logged below:\n");
    printf("------------------------------------------------\n");

    // Load PIO program
    uint offset = pio_add_program(pio, &bus_interface_rx_only_program);
    bus_interface_rx_only_program_init(pio, sm, offset);

    // Set up IRQ handler
    pio_set_irq0_source_enabled(pio, pis_interrupt0, true);
    irq_set_exclusive_handler(PIO0_IRQ_0, pio_irq_handler);
    irq_set_enabled(PIO0_IRQ_0, true);

    // Start PIO
    bus_interface_rx_only_enable(pio, sm);

    printf("PIO running. Waiting for data...\n\n");

    last_report_time = to_ms_since_boot(get_absolute_time());

    while (1) {
        // Process data from ring buffer (filled by IRQ handler)
        while (rx_tail != rx_head) {
            uint8_t data = rx_buffer[rx_tail];
            rx_tail = (rx_tail + 1) % RX_BUFFER_SIZE;
            log_byte(data);
        }

        // Flush partial line after timeout
        uint32_t now = to_ms_since_boot(get_absolute_time());
        if (line_pos > 0 && (now - last_report_time) > 500) {
            print_line();
        }

        // Periodic stats
        if ((now - last_report_time) >= 5000) {
            if (total_bytes > 0) {
                printf("\n[Total: %lu bytes received]\n\n", total_bytes);
            }
            last_report_time = now;
        }
    }

    return 0;
}
