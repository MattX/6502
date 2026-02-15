/*
 * PIO Bus Interface - Receive Only DMA Test
 *
 * Target: RP2350
 *
 * SAFE TEST MODE: MCU never drives the data bus.
 * Uses DMA + protocol parser to validate the full RX path.
 */

#include <stdio.h>
#include <ctype.h>

#include "pico/stdlib.h"

#include "bus_interface_rx_only.h"

static void callback(uint8_t device, const uint8_t *data, uint16_t len) {
    if (device != 0) {
        // printf("Expected device 0, got %d", device);
    }
    static uint8_t last_pattern = 1;
    uint8_t pattern = last_pattern - 1;
    if (pattern == 0) {
        pattern = 255;
    }
    if (len != pattern) {
        // printf("Expected length %d next, got %d\n", pattern, len);
        pattern = len;
    } else {
        for (uint16_t i = 0; i < len; i++) {
            if (data[i] != pattern) {
                // printf("Intruder found in pattern %d[%d]: %d\n", pattern, i, data[i]);
                goto end;
            }
        }
    }
    if (len == 1) {
        printf("End iteration\n");
    }

end:
    last_pattern = pattern;
}

int main(void) {
    stdio_init_all();
    sleep_ms(2000);  // let USB settle

    printf("\n");
    printf("====================================================\n");
    printf("  PIO Bus Interface - RX ONLY DMA (Safe Test)\n");
    printf("  Target: RP2350\n");
    printf("====================================================\n");
    printf("\n");
    printf("** MCU NEVER DRIVES THE BUS - SAFE FOR TESTING **\n");
    printf("\n");
    printf("Pin mapping:\n");
    printf("  GPIO 0:     RW\n");
    printf("  GPIO 1:     CS_N\n");
    printf("  GPIO 2:     PHI2\n");
    printf("  GPIO 6-13:  D[7:0] data bus\n");
    printf("\n");
    printf("DMA + protocol parser enabled (read requests ignored).\n");
    printf("----------------------------------------------------\n");

    if (!bus_rx_only_init()) {
        printf("ERROR: failed to initialize RX-only DMA interface.\n");
        return 1;
    }

    // Register hex-dump callback for all devices
    for (int i = 0; i < BUS_RX_ONLY_MAX_DEVICES; i++) {
        bus_rx_only_register_callback(i, callback);
    }

    bus_rx_only_start();

    uint32_t last_report_time = to_ms_since_boot(get_absolute_time());

    while (1) {
        bus_rx_only_task();

        uint32_t now = to_ms_since_boot(get_absolute_time());
        if ((now - last_report_time) >= 5000) {
            bus_rx_only_stats_t stats = bus_rx_only_get_stats();
            printf("\n[RX: %lu bytes, DMA overruns: %lu, bankruptcies: %lu, "
                   "read reqs: %lu, invalid dev: %lu, dispatched: %lu]\n\n",
                   stats.rx_bytes,
                   stats.rx_dma_overruns,
                   stats.rx_bankruptcies,
                   stats.rx_read_requests,
                   stats.rx_invalid_device,
                   stats.rx_dispatched);
            last_report_time = now;
        }
    }
}
