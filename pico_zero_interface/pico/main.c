/*
 * SPI Slave Stress Test -- DMA-based, Mode 3
 *
 * Receives WRITE payloads with verifiable patterns.
 * Does NOT queue responses (for write-blast testing, REQUEST/READ
 * is only used by the master to poll BUF free space).
 *
 * WRITE payload format: [seq_BE(4)] [pattern(len-4)]
 *   pattern[i] = (seq + i) & 0xFF
 */

#include <stdio.h>

#include "pico/stdlib.h"
#include "spi_slave.h"

static uint32_t rx_count = 0;
static uint32_t rx_errors = 0;
static uint32_t rx_bytes = 0;

static void on_write(const uint8_t *data, uint16_t len) {
    rx_count++;
    rx_bytes += len;

    if (len < 4) {
        rx_errors++;
        printf("ERR: WRITE too short (%u)\n", len);
        return;
    }

    uint32_t seq = ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
                   ((uint32_t)data[2] << 8) | data[3];

    // Verify pattern
    for (uint16_t i = 4; i < len; i++) {
        uint8_t expected = (seq + (i - 4)) & 0xFF;
        if (data[i] != expected) {
            rx_errors++;
            printf("ERR: seq=%lu len=%u @%u exp=0x%02x got=0x%02x\n",
                   (unsigned long)seq, len, i, expected, data[i]);
            break;
        }
    }
}

int main(void) {
    stdio_init_all();
    sleep_ms(2000);

    printf("\nSPI Slave Stress Test (DMA, Mode 3)\n");

    if (!spi_slave_init()) {
        printf("ERROR: init failed\n");
        return 1;
    }

    spi_slave_set_rx_callback(on_write);
    printf("Ready.\n\n");

    uint32_t last_stats = to_ms_since_boot(get_absolute_time());

    while (1) {
        spi_slave_task();

        uint32_t now = to_ms_since_boot(get_absolute_time());
        if (now - last_stats >= 2000) {
            spi_slave_stats_t s = spi_slave_get_stats();
            printf("[%lus] wr=%lu (%lu KB) rd=%lu req=%lu rx_err=%lu proto_err=%lu\n",
                   (unsigned long)(now / 1000),
                   (unsigned long)s.rx_writes,
                   (unsigned long)(rx_bytes / 1024),
                   (unsigned long)s.tx_reads,
                   (unsigned long)s.requests,
                   (unsigned long)rx_errors,
                   (unsigned long)s.proto_errors);
            last_stats = now;
        }
    }
}
