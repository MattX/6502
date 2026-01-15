/*
 * PIO Bus Interface - Loopback Test
 *
 * This program implements a simple loopback test where data written to
 * device N is echoed back when reading from device N.
 *
 * Protocol (from 6502 perspective):
 *   Write: [device] [length] [data...]
 *   Read:  [device | 0x80] [length] -> reads [data...] back
 *
 * UART console on GPIO 0/1 for debugging (directly usable without conflict
 * since we use GPIO 0-7 for data bus - but GPIO 0/1 will conflict!
 * Using GPIO 16/17 for UART instead).
 */

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"

#include "bus_interface.h"

// UART Console Configuration (avoid GPIO 0-11 used by bus interface)
#define UART_ID         uart0
#define UART_BAUD_RATE  115200
#define UART_TX_PIN     16
#define UART_RX_PIN     17

// Status LED (optional, use onboard LED if available)
#ifndef PICO_DEFAULT_LED_PIN
#define PICO_DEFAULT_LED_PIN 25
#endif
#define LED_PIN         PICO_DEFAULT_LED_PIN

// Statistics reporting interval (ms)
#define STATS_INTERVAL_MS 5000

static uint32_t last_stats_time = 0;
static bus_stats_t last_stats = {0};

// Loopback task: copy RX data to TX for each device
static void loopback_task(void) {
    static uint8_t temp_buffer[BUS_MAX_BUFFER_SIZE];

    for (int device = 0; device < BUS_MAX_DEVICES; device++) {
        uint16_t available = bus_device_rx_available(device);
        if (available > 0) {
            // Read from RX buffer
            uint16_t read = bus_device_read(device, temp_buffer, available);
            if (read > 0) {
                // Write to TX buffer (for CPU to read back)
                uint16_t written = bus_device_write(device, temp_buffer, read);
                if (written < read) {
                    printf("[Device %d] TX buffer full, dropped %d bytes\n",
                           device, read - written);
                }
            }
        }
    }
}

// Print statistics
static void print_stats(void) {
    bus_stats_t stats;
    bus_get_stats(&stats);

    uint32_t rx_delta = stats.rx_bytes - last_stats.rx_bytes;
    uint32_t tx_delta = stats.tx_bytes - last_stats.tx_bytes;

    if (rx_delta > 0 || tx_delta > 0 ||
        stats.rx_overflows > last_stats.rx_overflows ||
        stats.tx_underflows > last_stats.tx_underflows) {

        printf("Stats: RX=%lu (+%lu) TX=%lu (+%lu) Overflows=%lu Underflows=%lu\n",
               stats.rx_bytes, rx_delta,
               stats.tx_bytes, tx_delta,
               stats.rx_overflows, stats.tx_underflows);
    }

    last_stats = stats;
}

// LED heartbeat
static void led_task(void) {
    static uint32_t last_toggle = 0;
    uint32_t now = to_ms_since_boot(get_absolute_time());

    if (now - last_toggle >= 500) {
        gpio_xor_mask(1u << LED_PIN);
        last_toggle = now;
    }
}

int main(void) {
    // Initialize stdio (for USB serial if enabled)
    stdio_init_all();

    // Initialize UART for console (on GPIO 16/17, not conflicting with bus)
    uart_init(UART_ID, UART_BAUD_RATE);
    gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);

    // Initialize LED
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    gpio_put(LED_PIN, 0);

    // Wait a moment for USB/UART to initialize
    sleep_ms(1000);

    printf("\n");
    printf("========================================\n");
    printf("  PIO Bus Interface - Loopback Test\n");
    printf("========================================\n");
    printf("Pin mapping:\n");
    printf("  GPIO 0-7:   D[7:0] data bus\n");
    printf("  GPIO 8:     PHI2\n");
    printf("  GPIO 9:     CS_N\n");
    printf("  GPIO 10:    A0\n");
    printf("  GPIO 11:    RW\n");
    printf("  GPIO 16/17: UART console\n");
    printf("\n");
    printf("Protocol:\n");
    printf("  Write: [device] [length] [data...]\n");
    printf("  Read:  [device|0x80] [length] -> [data...]\n");
    printf("\n");

    // Initialize bus interface
    printf("Initializing bus interface...\n");
    if (!bus_init()) {
        printf("ERROR: Failed to initialize bus interface!\n");
        while (1) {
            gpio_xor_mask(1u << LED_PIN);
            sleep_ms(100);  // Fast blink = error
        }
    }
    printf("Bus interface initialized.\n");

    // Start bus interface
    printf("Starting bus interface...\n");
    bus_start();
    printf("Bus interface running.\n");
    printf("\n");

    last_stats_time = to_ms_since_boot(get_absolute_time());

    // Main loop
    while (1) {
        // Process bus communication
        bus_task();

        // Loopback: copy RX to TX
        loopback_task();

        // LED heartbeat
        led_task();

        // Periodic stats
        uint32_t now = to_ms_since_boot(get_absolute_time());
        if (now - last_stats_time >= STATS_INTERVAL_MS) {
            print_stats();
            last_stats_time = now;
        }
    }

    return 0;
}
