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
#include "hardware/gpio.h"

#include "bus_interface.h"

// Stats interval
#define STATS_INTERVAL_MS 5000

static uint32_t last_stats_time = 0;
static bus_stats_t last_stats = {0};

// Loopback callback: echo received data back to the same device
static void loopback_callback(uint8_t device, const uint8_t *data, uint16_t len) {
    uint16_t written = bus_device_write(device, data, len);
    if (written < len) {
        printf("[Dev %d] TX full, dropped %d bytes\n", device, len - written);
    }
}

static void print_stats(void) {
    bus_stats_t stats = bus_get_stats();

    uint32_t rx_delta = stats.rx_bytes - last_stats.rx_bytes;
    uint32_t tx_delta = stats.tx_bytes - last_stats.tx_bytes;

    if (rx_delta > 0 || tx_delta > 0 ||
        stats.rx_dma_overruns > last_stats.rx_dma_overruns ||
        stats.rx_bankruptcies > last_stats.rx_bankruptcies) {
        printf("RX=%lu (+%lu) TX=%lu (+%lu) Overruns=%lu Bankruptcies=%lu\n",
               stats.rx_bytes, rx_delta,
               stats.tx_bytes, tx_delta,
               stats.rx_dma_overruns,
               stats.rx_bankruptcies);
    }

    last_stats = stats;
}

int main(void) {
    stdio_init_all();
    sleep_ms(2000);  // let USB settle

    printf("\n");
    printf("========================================\n");
    printf("  PIO Bus Interface - Loopback Test\n");
    printf("========================================\n");
    printf("Pin mapping:\n");
    printf("  GPIO 0:     RW\n");
    printf("  GPIO 1:     CS_N\n");
    printf("  GPIO 2:     PHI2\n");
    printf("  GPIO 6-13:  D[7:0] data bus\n");
    printf("\n");
    printf("Protocol:\n");
    printf("  Write: [device] [length] [data...]\n");
    printf("  Read:  [device|0x80] -> [0xFF]* [len] [data...]\n");
    printf("         (0xFF = not ready, retry)\n");
    printf("\n");

    if (!bus_init()) {
        printf("ERROR: bus_init failed!\n");
        while (1) {
            sleep_ms(1000);
        }
    }

    // Register loopback callback for all devices
    for (int i = 0; i < BUS_MAX_DEVICES; i++) {
        bus_register_rx_callback(i, loopback_callback);
    }

    bus_start();
    printf("Bus interface running.\n\n");

    last_stats_time = to_ms_since_boot(get_absolute_time());

    while (1) {
        bus_task();

        uint32_t now = to_ms_since_boot(get_absolute_time());
        if (now - last_stats_time >= STATS_INTERVAL_MS) {
            print_stats();
            last_stats_time = now;
        }
    }

    return 0;
}
