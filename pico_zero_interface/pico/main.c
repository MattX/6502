/*
 * SPI Slave Stress Test -- DMA-based, Mode 3
 *
 * Receives WRITE payloads with verifiable patterns, queues responses
 * with a different pattern for the Zero to verify. Reports only errors
 * and periodic stats.
 *
 * WRITE payload format: [seq_BE(4)] [pattern(len-4)]
 *   pattern[i] = (seq + i) & 0xFF
 *
 * Response payload format: [seq_BE(4)] [pattern(len-4)]
 *   pattern[i] = (seq*7 + i) & 0xFF
 */

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "spi_slave.h"

static uint32_t rx_errors = 0;

static void on_write(const uint8_t *data, uint16_t len) {
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
            printf("ERR: seq=%lu @%u exp=0x%02x got=0x%02x\n",
                   (unsigned long)seq, i, expected, data[i]);
            break;
        }
    }

    // Queue response with same seq, different pattern
    static uint8_t resp[SPI_SLAVE_MAX_PAYLOAD];
    resp[0] = data[0]; resp[1] = data[1]; resp[2] = data[2]; resp[3] = data[3];
    for (uint16_t i = 4; i < len; i++) {
        resp[i] = ((seq * 7) + (i - 4)) & 0xFF;
    }
    if (!spi_slave_tx_queue(resp, len)) {
        printf("ERR: TX queue full seq=%lu\n", (unsigned long)seq);
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
        if (now - last_stats >= 5000) {
            spi_slave_stats_t s = spi_slave_get_stats();
            printf("[%lus] wr=%lu rd=%lu req=%lu rx_err=%lu proto_err=%lu\n",
                   (unsigned long)(now / 1000),
                   (unsigned long)s.rx_writes,
                   (unsigned long)s.tx_reads,
                   (unsigned long)s.requests,
                   (unsigned long)rx_errors,
                   (unsigned long)s.proto_errors);
            last_stats = now;
        }
    }
}
