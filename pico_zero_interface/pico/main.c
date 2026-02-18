/*
 * SPI Slave Test -- DMA-based operation via spi_slave library
 */

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "spi_slave.h"

static uint32_t write_count = 0;

static void on_write(const uint8_t *data, uint16_t len) {
    write_count++;
    printf("WRITE #%lu: %u bytes", (unsigned long)write_count, len);
    if (len > 0 && len <= 64) {
        printf(" [");
        for (uint16_t i = 0; i < len && i < 16; i++) {
            printf("%02x", data[i]);
            if (i < len - 1 && i < 15) printf(" ");
        }
        if (len > 16) printf(" ...");
        printf("]");
    }
    printf("\n");
}

int main(void) {
    stdio_init_all();
    sleep_ms(2000);

    printf("\n");
    printf("====================================================\n");
    printf("  Pico <-> Zero SPI Slave Test\n");
    printf("  Target: RP2350, SPI Mode 3\n");
    printf("====================================================\n");
    printf("\n");
    printf("Pin mapping:\n");
    printf("  GPIO %d: SPI0 RX  (MOSI from Zero)\n", SPI_SLAVE_PIN_RX);
    printf("  GPIO %d: SPI0 CSn (CS from Zero)\n",    SPI_SLAVE_PIN_CSN);
    printf("  GPIO %d: SPI0 SCK (clock from Zero)\n", SPI_SLAVE_PIN_SCK);
    printf("  GPIO %d: SPI0 TX  (MISO to Zero)\n",    SPI_SLAVE_PIN_TX);
    printf("  GPIO %d: IRQ      (to Zero, active low)\n", SPI_SLAVE_PIN_IRQ);
    printf("  GPIO %d: READY    (to Zero, active low)\n", SPI_SLAVE_PIN_READY);
    printf("\n");
    printf("Protocol: READ_SIZE=%d, MAX_PAYLOAD=%d\n",
           SPI_SLAVE_READ_SIZE, SPI_SLAVE_MAX_PAYLOAD);
    printf("RX ring buffer: %d bytes\n", SPI_SLAVE_RX_RING_SIZE);
    printf("----------------------------------------------------\n");

    if (!spi_slave_init()) {
        printf("ERROR: failed to initialize SPI slave.\n");
        return 1;
    }

    spi_slave_set_rx_callback(on_write);
    printf("SPI slave initialized. Waiting for transactions...\n\n");

    uint32_t last_stats_time = to_ms_since_boot(get_absolute_time());

    while (1) {
        spi_slave_task();

        uint32_t now = to_ms_since_boot(get_absolute_time());

        // Print stats every 5 seconds
        if (now - last_stats_time >= 5000) {
            spi_slave_stats_t s = spi_slave_get_stats();
            printf("\n[Stats: writes=%lu (%lu B), reads=%lu (%lu B), "
                   "requests=%lu, proto_err=%lu, BUF=%u]\n\n",
                   (unsigned long)s.rx_writes, (unsigned long)s.rx_bytes,
                   (unsigned long)s.tx_reads, (unsigned long)s.tx_bytes,
                   (unsigned long)s.requests,
                   (unsigned long)s.proto_errors,
                   spi_slave_get_buf());
            last_stats_time = now;
        }
    }
}
