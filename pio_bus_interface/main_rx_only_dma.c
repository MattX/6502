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

// Line buffer for ASCII display
#define LINE_WIDTH 16

static void print_chunk(const uint8_t *data, uint len) {
    for (uint i = 0; i < LINE_WIDTH; i++) {
        if (i < len) {
            printf("%02X ", data[i]);
        } else {
            printf("   ");
        }
        if (i == 7) {
            printf(" ");
        }
    }

    printf(" |");
    for (uint i = 0; i < len; i++) {
        char c = (char)data[i];
        printf("%c", isprint(c) ? c : '.');
    }
    printf("|\n");
}

// RX callback: hex-dump received data
static void hexdump_callback(uint8_t device, const uint8_t *data, uint16_t len) {
    uint16_t offset = 0;
    while (offset < len) {
        uint16_t chunk = (len - offset > LINE_WIDTH) ? LINE_WIDTH : len - offset;
        printf("DEV %02X: ", device);
        print_chunk(data + offset, chunk);
        offset += chunk;
    }
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
        bus_rx_only_register_callback(i, hexdump_callback);
    }

    bus_rx_only_start();

    uint32_t last_report_time = to_ms_since_boot(get_absolute_time());

    while (1) {
        bus_rx_only_task();

        uint32_t now = to_ms_since_boot(get_absolute_time());
        if ((now - last_report_time) >= 5000) {
            bus_rx_only_stats_t stats = bus_rx_only_get_stats();
            printf("\n[RX: %lu bytes, DMA overruns: %lu, bankruptcies: %lu, read reqs: %lu]\n\n",
                   stats.rx_bytes,
                   stats.rx_dma_overruns,
                   stats.rx_bankruptcies,
                   stats.rx_read_requests);
            last_report_time = now;
        }
    }
}
