/*
 * PIO Bus Interface - Loopback Test
 *
 * Simplified protocol (no A0 pin, no status register):
 *   Write: [device] [length] [data...]
 *   Read:  [device|0x80] -> poll until != 0xFF, that's length, then read data
 *
 * Data written to device N is echoed back when reading from device N.
 */

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"

#include "bus_interface.h"

// UART Console (GPIO 16/17, not conflicting with bus)
#define UART_ID         uart0
#define UART_BAUD_RATE  115200
#define UART_TX_PIN     16
#define UART_RX_PIN     17

// Status LED
#ifndef PICO_DEFAULT_LED_PIN
#define PICO_DEFAULT_LED_PIN 25
#endif
#define LED_PIN         PICO_DEFAULT_LED_PIN

// Stats interval
#define STATS_INTERVAL_MS 5000

static uint32_t last_stats_time = 0;
static bus_stats_t last_stats = {0};

// Loopback: copy RX to TX for each device
static void loopback_task(void) {
    static uint8_t temp_buffer[BUS_MAX_BUFFER_SIZE];

    for (int device = 0; device < BUS_MAX_DEVICES; device++) {
        uint16_t available = bus_device_rx_available(device);
        if (available > 0) {
            uint16_t read = bus_device_read(device, temp_buffer, available);
            if (read > 0) {
                uint16_t written = bus_device_write(device, temp_buffer, read);
                if (written < read) {
                    printf("[Dev %d] TX full, dropped %d bytes\n", device, read - written);
                }
            }
        }
    }
}

static void print_stats(void) {
    bus_stats_t stats;
    bus_get_stats(&stats);

    uint32_t rx_delta = stats.rx_bytes - last_stats.rx_bytes;
    uint32_t tx_delta = stats.tx_bytes - last_stats.tx_bytes;

    if (rx_delta > 0 || tx_delta > 0 ||
        stats.rx_overflows > last_stats.rx_overflows) {
        printf("RX=%lu (+%lu) TX=%lu (+%lu) Overflows=%lu\n",
               stats.rx_bytes, rx_delta,
               stats.tx_bytes, tx_delta,
               stats.rx_overflows);
    }

    last_stats = stats;
}

static void led_task(void) {
    static uint32_t last_toggle = 0;
    uint32_t now = to_ms_since_boot(get_absolute_time());

    if (now - last_toggle >= 500) {
        gpio_xor_mask(1u << LED_PIN);
        last_toggle = now;
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
    printf("========================================\n");
    printf("  PIO Bus Interface - Loopback Test\n");
    printf("  (Simplified Protocol, No A0)\n");
    printf("========================================\n");
    printf("Pin mapping:\n");
    printf("  GPIO 0-7:  D[7:0] data bus\n");
    printf("  GPIO 8:    PHI2\n");
    printf("  GPIO 9:    CS_N\n");
    printf("  GPIO 10:   RW\n");
    printf("  GPIO 16/17: UART\n");
    printf("\n");
    printf("Protocol:\n");
    printf("  Write: [device] [length] [data...]\n");
    printf("  Read:  [device|0x80] -> [0xFF]* [len] [data...]\n");
    printf("         (0xFF = not ready, retry)\n");
    printf("\n");

    if (!bus_init()) {
        printf("ERROR: bus_init failed!\n");
        while (1) {
            gpio_xor_mask(1u << LED_PIN);
            sleep_ms(100);
        }
    }

    bus_start();
    printf("Bus interface running.\n\n");

    last_stats_time = to_ms_since_boot(get_absolute_time());

    while (1) {
        bus_task();
        loopback_task();
        led_task();

        uint32_t now = to_ms_since_boot(get_absolute_time());
        if (now - last_stats_time >= STATS_INTERVAL_MS) {
            print_stats();
            last_stats_time = now;
        }
    }

    return 0;
}
