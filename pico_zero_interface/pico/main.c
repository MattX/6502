/*
 * SPI Slave Read Blast Test -- DMA-based, Mode 3
 *
 * Queues 1500-byte messages for the Zero to READ as fast as possible.
 * Does NOT use IRQ -- the Zero polls with REQUEST/READ.
 *
 * TX payload format: [seq_BE(4)] [pattern(1496)]
 *   pattern[i] = (seq*7 + i) & 0xFF
 */

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "spi_slave.h"

#define PAYLOAD_SIZE 1500
#define PATTERN_OFFSET 4

static uint32_t tx_seq = 0;
static uint32_t tx_count = 0;

static uint8_t payload_buf[PAYLOAD_SIZE];

static void build_payload(uint32_t seq) {
    payload_buf[0] = (uint8_t)(seq >> 24);
    payload_buf[1] = (uint8_t)(seq >> 16);
    payload_buf[2] = (uint8_t)(seq >> 8);
    payload_buf[3] = (uint8_t)(seq);
    for (uint16_t i = PATTERN_OFFSET; i < PAYLOAD_SIZE; i++) {
        payload_buf[i] = ((seq * 7) + (i - PATTERN_OFFSET)) & 0xFF;
    }
}

int main(void) {
    stdio_init_all();
    sleep_ms(2000);

    printf("\nSPI Slave Read Blast Test (DMA, Mode 3)\n");

    if (!spi_slave_init()) {
        printf("ERROR: init failed\n");
        return 1;
    }

    printf("Ready.\n\n");

    uint32_t last_stats = to_ms_since_boot(get_absolute_time());

    while (1) {
        spi_slave_task();

        // Try to queue the next message
        build_payload(tx_seq);
        if (spi_slave_tx_queue(payload_buf, PAYLOAD_SIZE)) {
            tx_seq++;
            tx_count++;
        }

        uint32_t now = to_ms_since_boot(get_absolute_time());
        if (now - last_stats >= 2000) {
            spi_slave_stats_t s = spi_slave_get_stats();
            printf("[%lus] queued=%lu rd=%lu req=%lu tx_bytes=%lu proto_err=%lu\n",
                   (unsigned long)(now / 1000),
                   (unsigned long)tx_count,
                   (unsigned long)s.tx_reads,
                   (unsigned long)s.requests,
                   (unsigned long)s.tx_bytes,
                   (unsigned long)s.proto_errors);
            last_stats = now;
        }
    }
}
