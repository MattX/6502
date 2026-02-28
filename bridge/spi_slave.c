/*
 * SPI Slave Interface Implementation
 *
 * Handles the Pico side of the Pico <-> Zero SPI protocol.
 *
 * Architecture:
 *   - RX path: DMA continuously writes SPI RX FIFO into a ring buffer.
 *     A GPIO interrupt on CS rising edge signals end-of-transaction.
 *     spi_slave_task() then parses the received data and delivers WRITE
 *     payloads directly to the application via a registered callback.
 *
 *   - TX path: When a REQUEST is received, the Pico prepares a READ_SIZE
 *     staging buffer, configures TX DMA, and asserts READY. The Zero then
 *     sends a READ to clock out the data. After CS rises, READY is
 *     deasserted.
 *
 *   - Flow control: The READ response includes per-device buffer free
 *     space (8 bytes, in 16-byte units), so the Zero knows how much it
 *     can WRITE per device.
 *
 *   - No race conditions: the REQUEST/READY handshake guarantees the master
 *     won't start a READ until TX DMA is fully loaded.
 *
 * IMPORTANT: PL022 SPI slave requires Mode 3 (CPOL=1, CPHA=1) for
 * multi-byte transfers. Mode 0 only processes 1 frame per CS assertion.
 * See: https://github.com/raspberrypi/pico-sdk/issues/1116
 */

#include "spi_slave.h"
#include "bus_interface.h"

#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"

#include <stdio.h>
#include <string.h>

// ============================================================================
// State
// ============================================================================

// DMA channels
static int dma_rx_chan = -1;
static int dma_tx_chan = -1;

// RX ring buffer (DMA writes here continuously)
static uint8_t __attribute__((aligned(SPI_SLAVE_RX_RING_SIZE)))
    rx_ring[SPI_SLAVE_RX_RING_SIZE];

// Software read pointer into rx_ring
static volatile uint rx_read_idx = 0;

// Snapshot of DMA write pointer at CS rising edge (set by ISR)
static volatile uint rx_write_idx_snapshot = 0;
static volatile bool rx_transaction_ready = false;

// TX staging buffer (single buffer -- no double-buffering needed since
// we only prepare it in the safe window between REQUEST and READ)
static uint8_t tx_buf[SPI_SLAVE_READ_SIZE];

// TX queue: data waiting to be sent to Zero (Pico -> Zero direction).
#define TX_QUEUE_SIZE 4096
static uint8_t tx_queue[TX_QUEUE_SIZE];
static uint tx_queue_head = 0;
static uint tx_queue_tail = 0;
static uint tx_queue_len = 0;

// RX callback for WRITE payloads
static spi_slave_rx_callback_t rx_callback = NULL;

// Temp buffer for copying WRITE payloads out of the DMA ring
static uint8_t rx_temp[SPI_SLAVE_MAX_PAYLOAD];

// Protocol state
typedef enum {
    STATE_IDLE,         // Waiting for WRITE or REQUEST
    STATE_REQUESTED,    // REQUEST received, preparing response
    STATE_READY,        // READY asserted, waiting for READ
} slave_state_t;

static volatile slave_state_t state = STATE_IDLE;

// Stats
static spi_slave_stats_t stats = {0};

// ============================================================================
// Internal helpers
// ============================================================================

static inline uint get_dma_rx_write_idx(void) {
    uint32_t write_addr = dma_channel_hw_addr(dma_rx_chan)->write_addr;
    return write_addr - (uint32_t)rx_ring;
}

static inline void irq_pin_assert(void) {
    gpio_put(SPI_SLAVE_PIN_IRQ, 0);  // Active low
}

static inline void irq_pin_deassert(void) {
    gpio_put(SPI_SLAVE_PIN_IRQ, 1);  // Idle high
}

static inline void ready_pin_assert(void) {
    gpio_put(SPI_SLAVE_PIN_READY, 0);  // Active low
}

static inline void ready_pin_deassert(void) {
    gpio_put(SPI_SLAVE_PIN_READY, 1);  // Idle high
}

static inline uint8_t tx_queue_peek(uint offset) {
    return tx_queue[(tx_queue_head + offset) & (TX_QUEUE_SIZE - 1)];
}

// Drain exactly |count| bytes from tx_queue into dst.
static void tx_queue_drain(uint8_t *dst, uint count) {
    uint head = tx_queue_head;
    for (uint i = 0; i < count; i++) {
        dst[i] = tx_queue[head];
        head = (head + 1) & (TX_QUEUE_SIZE - 1);
    }
    tx_queue_head = head;
    tx_queue_len -= count;
}

// ============================================================================
// CS pin interrupt: fires on rising edge (end of transaction)
// ============================================================================

static void cs_rise_handler(uint gpio, uint32_t events) {
    (void)gpio;
    (void)events;

    // Snapshot DMA write pointer
    rx_write_idx_snapshot = get_dma_rx_write_idx();

    if (state == STATE_READY) {
        // READ just completed. Deassert READY.
        ready_pin_deassert();
        state = STATE_IDLE;
    }

    // Signal main loop
    rx_transaction_ready = true;
}

// ============================================================================
// Prepare TX staging buffer and load DMA (called from task after REQUEST)
// ============================================================================

static void prepare_and_load_tx(void) {
    // --- Per-device buffer estimates (bytes 0..7) ---
    for (uint8_t d = 0; d < BUS_MAX_DEVICES; d++) {
        uint16_t free_bytes = bus_device_tx_free(d);
        uint units = free_bytes / 16;
        tx_buf[d] = (units > 255) ? 255 : (uint8_t)units;
    }

    // --- Payload: pack complete TLV packets only (bytes 10+) ---
    uint8_t *payload = &tx_buf[10];
    uint payload_len = 0;

    while (tx_queue_len >= 2) {
        uint8_t tlv_dev = tx_queue_peek(0);
        uint8_t tlv_len = tx_queue_peek(1);
        uint tlv_total = 2 + tlv_len;

        if (tlv_total > tx_queue_len) {
            // Incomplete TLV in queue (shouldn't happen, but be safe)
            break;
        }

        if (payload_len + tlv_total > SPI_SLAVE_MAX_PAYLOAD) {
            // Won't fit in this frame
            break;
        }

        // Drain this complete TLV into the payload
        tx_queue_drain(&payload[payload_len], tlv_total);
        payload_len += tlv_total;
    }

    // --- Length field (bytes 8..9, big-endian) ---
    tx_buf[8] = (uint8_t)(payload_len >> 8);
    tx_buf[9] = (uint8_t)(payload_len & 0xFF);

    // Zero-pad remainder
    if (payload_len < SPI_SLAVE_MAX_PAYLOAD) {
        memset(&payload[payload_len], 0, SPI_SLAVE_MAX_PAYLOAD - payload_len);
    }

    stats.tx_bytes += payload_len;

    // Configure one-shot TX DMA: staging buffer -> SPI TX FIFO
    dma_channel_config tx_config = dma_channel_get_default_config(dma_tx_chan);
    channel_config_set_transfer_data_size(&tx_config, DMA_SIZE_8);
    channel_config_set_read_increment(&tx_config, true);
    channel_config_set_write_increment(&tx_config, false);
    channel_config_set_dreq(&tx_config, spi_get_dreq(SPI_SLAVE_SPI, true));

    dma_channel_configure(
        dma_tx_chan,
        &tx_config,
        &spi_get_hw(SPI_SLAVE_SPI)->dr,
        tx_buf,
        SPI_SLAVE_READ_SIZE,
        true  // Start immediately -- FIFO will fill, DMA stalls on DREQ
    );

    // DMA is loaded. Assert READY -- master may now send READ.
    state = STATE_READY;
    ready_pin_assert();
}

// ============================================================================
// Process a completed transaction from the RX ring buffer
// ============================================================================

static void process_transaction(void) {
    uint rd = rx_read_idx;
    uint wr = rx_write_idx_snapshot;
    uint avail = (wr - rd) & (SPI_SLAVE_RX_RING_SIZE - 1);

    if (avail == 0) return;

    uint8_t cmd = rx_ring[rd];
    rd = (rd + 1) & (SPI_SLAVE_RX_RING_SIZE - 1);
    avail--;

    switch (cmd) {
        case SPI_CMD_WRITE: {
            if (avail < 2) {
                stats.proto_errors++;
                break;
            }

            uint8_t len_hi = rx_ring[rd];
            rd = (rd + 1) & (SPI_SLAVE_RX_RING_SIZE - 1);
            uint8_t len_lo = rx_ring[rd];
            rd = (rd + 1) & (SPI_SLAVE_RX_RING_SIZE - 1);
            avail -= 2;

            uint16_t payload_len = ((uint16_t)len_hi << 8) | len_lo;

            if (payload_len > avail || payload_len > SPI_SLAVE_MAX_PAYLOAD) {
                stats.proto_errors++;
                rd = wr;  // Discard
                break;
            }

            stats.rx_writes++;
            stats.rx_bytes += payload_len;

            // Deliver payload via callback
            if (rx_callback && payload_len > 0) {
                for (uint16_t i = 0; i < payload_len; i++) {
                    rx_temp[i] = rx_ring[(rd + i) & (SPI_SLAVE_RX_RING_SIZE - 1)];
                }
                rx_callback(rx_temp, payload_len);
            }

            rd = (rd + payload_len) & (SPI_SLAVE_RX_RING_SIZE - 1);
            break;
        }

        case SPI_CMD_REQUEST: {
            stats.requests++;
            state = STATE_REQUESTED;

            // Deassert IRQ: the Zero is handling our data request now.
            irq_pin_deassert();
            break;
        }

        case SPI_CMD_READ: {
            // READ transaction completed. The CS rise handler already
            // deasserted READY and moved state to IDLE.
            stats.tx_reads++;
            // Skip all received bytes (0x03 + dummy padding)
            rd = wr;
            break;
        }

        default: {
            stats.proto_errors++;
            rd = wr;
            break;
        }
    }

    rx_read_idx = rd;
}

// ============================================================================
// Public API
// ============================================================================

bool spi_slave_init(void) {
    // --- IRQ pin: output, idle high (deasserted) ---
    gpio_init(SPI_SLAVE_PIN_IRQ);
    gpio_set_dir(SPI_SLAVE_PIN_IRQ, GPIO_OUT);
    irq_pin_deassert();

    // --- READY pin: output, idle high (deasserted) ---
    gpio_init(SPI_SLAVE_PIN_READY);
    gpio_set_dir(SPI_SLAVE_PIN_READY, GPIO_OUT);
    ready_pin_deassert();

    // --- SPI slave mode, Mode 3 (CPOL=1, CPHA=1) ---
    // PL022 slave in Mode 0 only processes 1 frame per CS assertion.
    // Mode 3 allows continuous multi-byte transfers with CS held low.
    spi_init(SPI_SLAVE_SPI, 75 * 1000 * 1000);  // Max internal clock for slave
    spi_set_slave(SPI_SLAVE_SPI, true);
    spi_set_format(SPI_SLAVE_SPI, 8, SPI_CPOL_1, SPI_CPHA_1, SPI_MSB_FIRST);

    gpio_set_function(SPI_SLAVE_PIN_RX,  GPIO_FUNC_SPI);
    gpio_set_function(SPI_SLAVE_PIN_CSN, GPIO_FUNC_SPI);
    gpio_set_function(SPI_SLAVE_PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(SPI_SLAVE_PIN_TX,  GPIO_FUNC_SPI);

    // --- DMA: RX channel (ring buffer, runs forever) ---
    dma_rx_chan = dma_claim_unused_channel(true);

    dma_channel_config rx_config = dma_channel_get_default_config(dma_rx_chan);
    channel_config_set_transfer_data_size(&rx_config, DMA_SIZE_8);
    channel_config_set_read_increment(&rx_config, false);
    channel_config_set_write_increment(&rx_config, true);
    channel_config_set_ring(&rx_config, true, SPI_SLAVE_RX_RING_BITS);
    channel_config_set_dreq(&rx_config, spi_get_dreq(SPI_SLAVE_SPI, false));

    // TRIGGER_SELF mode for endless operation
    uint32_t trans_count = SPI_SLAVE_RX_RING_SIZE | (1u << 28);

    dma_channel_configure(
        dma_rx_chan,
        &rx_config,
        rx_ring,
        &spi_get_hw(SPI_SLAVE_SPI)->dr,
        trans_count,
        true  // Start immediately
    );

    // --- DMA: TX channel (configured per-REQUEST, not started yet) ---
    dma_tx_chan = dma_claim_unused_channel(true);

    // --- CS pin interrupt: rising edge (end of transaction) ---
    gpio_set_irq_enabled_with_callback(SPI_SLAVE_PIN_CSN,
                                       GPIO_IRQ_EDGE_RISE,
                                       true, cs_rise_handler);

    // --- Init state ---
    memset(&stats, 0, sizeof(stats));
    rx_read_idx = 0;
    tx_queue_head = 0;
    tx_queue_tail = 0;
    tx_queue_len = 0;
    rx_callback = NULL;
    state = STATE_IDLE;

    // Assert IRQ to signal "I'm ready" to the Zero.
    irq_pin_assert();

    return true;
}

void spi_slave_set_rx_callback(spi_slave_rx_callback_t cb) {
    rx_callback = cb;
}

bool spi_slave_tx_queue(const uint8_t *data, uint16_t len) {
    if (len == 0) return true;
    if (len > TX_QUEUE_SIZE - tx_queue_len) return false;

    uint tail = tx_queue_tail;
    for (uint16_t i = 0; i < len; i++) {
        tx_queue[tail] = data[i];
        tail = (tail + 1) & (TX_QUEUE_SIZE - 1);
    }
    tx_queue_tail = tail;
    tx_queue_len += len;

    // Assert IRQ if not already in a REQUEST/READ cycle
    if (state == STATE_IDLE) {
        irq_pin_assert();
    }

    return true;
}

void spi_slave_task(void) {
    // Process any completed transaction
    if (rx_transaction_ready) {
        rx_transaction_ready = false;
        process_transaction();
    }

    // If REQUEST was received, prepare TX and assert READY
    if (state == STATE_REQUESTED) {
        prepare_and_load_tx();
    }

    // After a READ completes (state returned to IDLE), check if more data
    // is queued and re-assert IRQ if so
    if (state == STATE_IDLE && tx_queue_len > 0) {
        irq_pin_assert();
    }
}

bool spi_slave_is_connected(void) {
    return (stats.rx_writes + stats.requests + stats.tx_reads) > 0;
}

spi_slave_stats_t spi_slave_get_stats(void) {
    return stats;
}

void spi_slave_clear_stats(void) {
    memset(&stats, 0, sizeof(stats));
}
