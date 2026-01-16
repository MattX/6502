/*
 * PIO Bus Interface - Receive Only Test
 *
 * SAFE TEST MODE: MCU never drives the data bus.
 * All bytes written by the CPU are logged to UART as hex and ASCII.
 *
 * Use this to verify basic PIO timing and bus capture without
 * risking electrical damage from bus contention.
 */

#include <stdio.h>
#include <ctype.h>

#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"

#include "bus_interface_rx_only.pio.h"

// UART Console
#define UART_ID         uart0
#define UART_BAUD_RATE  115200
#define UART_TX_PIN     16
#define UART_RX_PIN     17

// Status LED
#ifndef PICO_DEFAULT_LED_PIN
#define PICO_DEFAULT_LED_PIN 25
#endif
#define LED_PIN         PICO_DEFAULT_LED_PIN

// PIO
static PIO pio = pio0;
static uint sm = 0;

// Statistics
static uint32_t total_bytes = 0;
static uint32_t last_report_time = 0;

// Line buffer for ASCII display
#define LINE_WIDTH 16
static uint8_t line_buffer[LINE_WIDTH];
static uint line_pos = 0;
static uint32_t line_addr = 0;

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

static void led_task(void) {
    static uint32_t last_toggle = 0;
    static bool activity = false;
    uint32_t now = to_ms_since_boot(get_absolute_time());

    // Fast blink when receiving data
    if (total_bytes > 0) {
        activity = true;
        if (now - last_toggle >= 50) {
            gpio_xor_mask(1u << LED_PIN);
            last_toggle = now;
        }
    } else {
        // Slow blink when idle
        if (now - last_toggle >= 500) {
            gpio_xor_mask(1u << LED_PIN);
            last_toggle = now;
        }
    }
}

int main(void) {
    stdio_init_all();

    // UART console
    uart_init(UART_ID, UART_BAUD_RATE);
    gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);

    // LED
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);

    sleep_ms(1000);

    printf("\n");
    printf("================================================\n");
    printf("  PIO Bus Interface - RECEIVE ONLY (Safe Test)\n");
    printf("================================================\n");
    printf("\n");
    printf("** MCU NEVER DRIVES THE BUS - SAFE FOR TESTING **\n");
    printf("\n");
    printf("Pin mapping:\n");
    printf("  GPIO 0-7:  D[7:0] (INPUT ONLY)\n");
    printf("  GPIO 8:    PHI2\n");
    printf("  GPIO 9:    CS_N\n");
    printf("  GPIO 10:   RW\n");
    printf("  GPIO 16/17: UART (%d baud)\n", UART_BAUD_RATE);
    printf("\n");
    printf("All CPU writes will be logged below:\n");
    printf("------------------------------------------------\n");

    // Load and start PIO
    uint offset = pio_add_program(pio, &bus_interface_rx_only_program);
    bus_interface_rx_only_program_init(pio, sm, offset);
    bus_interface_rx_only_enable(pio, sm);

    printf("PIO running. Waiting for data...\n\n");

    last_report_time = to_ms_since_boot(get_absolute_time());

    while (1) {
        // Check for data from PIO
        while (!pio_sm_is_rx_fifo_empty(pio, sm)) {
            uint32_t data = pio_sm_get(pio, sm);
            log_byte(data & 0xFF);
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

        led_task();
    }

    return 0;
}
