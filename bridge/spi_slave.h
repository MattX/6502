/*
 * SPI Slave Interface for Pico <-> Zero communication
 *
 * Protocol: see pico_zero_interface/README.md
 *
 * Three commands: WRITE (Zero->Pico), REQUEST (ask Pico to prepare),
 * READ (fetch Pico's response after READY).
 *
 * Pin assignments (SPI0, chosen to avoid 6502 bus GPIOs 0-2, 6-13):
 *   GPIO 16 = SPI0 RX  (MOSI from Zero)
 *   GPIO 17 = SPI0 CSn (directly usable as SPI0_CS_n)
 *   GPIO 18 = SPI0 SCK (clock from Zero)
 *   GPIO 19 = SPI0 TX  (MISO to Zero)
 *   GPIO 20 = IRQ       (active-low output, "I have data")
 *   GPIO 21 = READY     (active-low output, "TX DMA loaded, safe to READ")
 */

#ifndef SPI_SLAVE_H
#define SPI_SLAVE_H

#include <stdint.h>
#include <stdbool.h>

// --- Protocol constants (must match Zero side) ---

#define SPI_SLAVE_READ_SIZE     1503    // 3-byte header + 1500-byte payload
#define SPI_SLAVE_MAX_PAYLOAD   1500

// Command bytes (first byte of MOSI)
#define SPI_CMD_WRITE   0x01
#define SPI_CMD_REQUEST 0x02
#define SPI_CMD_READ    0x03

// --- Pin assignments ---

#define SPI_SLAVE_SPI       spi0
#define SPI_SLAVE_PIN_RX    16      // MOSI (input from Zero)
#define SPI_SLAVE_PIN_CSN   17      // CS (input from Zero)
#define SPI_SLAVE_PIN_SCK   18      // Clock (input from Zero)
#define SPI_SLAVE_PIN_TX    19      // MISO (output to Zero)
#define SPI_SLAVE_PIN_IRQ   20      // IRQ (output to Zero, active low)
#define SPI_SLAVE_PIN_READY 21      // READY (output to Zero, active low)

// --- RX ring buffer (DMA) ---

#define SPI_SLAVE_RX_RING_BITS  13              // 2^13 = 8192 bytes
#define SPI_SLAVE_RX_RING_SIZE  (1 << SPI_SLAVE_RX_RING_BITS)

// --- API ---

// Initialize SPI slave hardware, DMA, and GPIO.
bool spi_slave_init(void);

// Queue data for the Zero to READ. Copies data into internal TX queue.
// Returns false if the TX queue is full.
// If IRQ is not already asserted, this will assert it.
bool spi_slave_tx_queue(const uint8_t *data, uint16_t len);

// Drain up to max_bytes from the RX queue into dst.
// Returns the number of bytes actually copied.
uint16_t spi_slave_rx_drain(uint8_t *dst, uint16_t max_bytes);

// Returns the number of bytes currently in the RX queue.
uint16_t spi_slave_rx_available(void);

// Call regularly from main loop. Processes completed RX transactions,
// handles REQUEST by preparing TX DMA and asserting READY, and
// manages IRQ/READY state after READ completes.
void spi_slave_task(void);

// Returns current free space for WRITE payloads, in 64-byte units (BUF field).
// Accounts for both RX queue usage and unprocessed DMA ring data.
uint8_t spi_slave_get_buf(void);

// --- Stats ---

typedef struct {
    uint32_t rx_writes;         // WRITE transactions received
    uint32_t rx_bytes;          // Total payload bytes received via WRITE
    uint32_t rx_overflows;      // WRITE payloads dropped (RX queue full)
    uint32_t tx_reads;          // READ transactions completed
    uint32_t tx_bytes;          // Total payload bytes sent via READ
    uint32_t requests;          // REQUEST commands handled
    uint32_t proto_errors;      // Protocol errors (bad CMD, etc.)
} spi_slave_stats_t;

spi_slave_stats_t spi_slave_get_stats(void);
void spi_slave_clear_stats(void);

#endif // SPI_SLAVE_H
