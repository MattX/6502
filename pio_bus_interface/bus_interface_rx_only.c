/*
 * PIO-based 6502 Bus Interface Driver Implementation (RX-only)
 *
 * Simplified protocol without A0/status register:
 *   Write: [device] [length] [data...]      (device bit 7 = 0)
 *   Read:  [device|0x80]                    (ignored in RX-only mode)
 *
 * RX data is dispatched to per-device callbacks directly from the DMA
 * ring buffer.  A post-callback transfer_count check detects the case
 * where DMA overwrites data while a callback is executing ("bankruptcy").
 */

#include "bus_interface_rx_only.h"
#include "bus_interface_rx_only.pio.h"

#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/dma.h"

#include <stdio.h>
#include <string.h>

// PIO and state machine configuration
static PIO bus_pio = pio0;
static uint bus_sm = 0;
static uint bus_program_offset;

// DMA channel
static int dma_rx_chan = -1;

// DMA RX ring buffer - 4K gives callbacks plenty of headroom
#define DMA_BUFFER_SIZE      4096
#define DMA_BUFFER_RING_BITS 12    // 2^12 = 4096
static uint8_t __attribute__((aligned(DMA_BUFFER_SIZE))) dma_rx_buffer[DMA_BUFFER_SIZE];
static volatile uint dma_rx_read_idx = 0;
static uint32_t dma_rx_total_read = 0;

// Per-device RX callbacks
static bus_rx_only_callback_t rx_callbacks[BUS_RX_ONLY_MAX_DEVICES];

// Temp buffer for assembling wrapped DMA ring data (max transfer = 255)
static uint8_t rx_transaction_buf[255];

// Protocol state machine
typedef enum {
    PROTO_IDLE,
    PROTO_GOT_DEVICE,
    PROTO_RECEIVING
} proto_state_t;

static proto_state_t proto_state = PROTO_IDLE;
static uint8_t current_device = 0;
static uint16_t transfer_remaining = 0;

// RX transaction tracking for callback dispatch + overrun detection
static uint rx_transaction_start_idx = 0;
static uint16_t rx_transaction_len = 0;
static uint32_t rx_transaction_total_read_start = 0;

// Statistics
static bus_rx_only_stats_t stats = {0};

// Forward declarations
static void setup_dma(void);
static void process_rx_data(void);

void bus_rx_only_register_callback(uint8_t device, bus_rx_only_callback_t callback) {
    if (device < BUS_RX_ONLY_MAX_DEVICES) {
        rx_callbacks[device] = callback;
    }
}

bool bus_rx_only_init(void) {
    memset(rx_callbacks, 0, sizeof(rx_callbacks));
    memset(&stats, 0, sizeof(stats));

    // Load PIO program
    if (!pio_can_add_program(bus_pio, &bus_interface_rx_only_program)) {
        return false;
    }
    bus_program_offset = pio_add_program(bus_pio, &bus_interface_rx_only_program);

    // Initialize PIO state machine
    bus_interface_rx_only_program_init(bus_pio, bus_sm, bus_program_offset);

    // Set up DMA
    setup_dma();

    return true;
}

static void setup_dma(void) {
    // Claim DMA channel
    dma_rx_chan = dma_claim_unused_channel(true);

    // === RX DMA: PIO RX FIFO -> RAM buffer ===
    dma_channel_config rx_config = dma_channel_get_default_config(dma_rx_chan);
    channel_config_set_transfer_data_size(&rx_config, DMA_SIZE_8);
    channel_config_set_read_increment(&rx_config, false);
    channel_config_set_write_increment(&rx_config, true);
    channel_config_set_ring(&rx_config, true, DMA_BUFFER_RING_BITS);
    channel_config_set_dreq(&rx_config, pio_get_dreq(bus_pio, bus_sm, false));

    dma_channel_configure(
        dma_rx_chan,
        &rx_config,
        dma_rx_buffer,
        &bus_pio->rxf[bus_sm],
        0xFFFFFFFF,
        false
    );

    dma_rx_read_idx = 0;
    dma_rx_total_read = 0;
}

void bus_rx_only_start(void) {
    dma_channel_start(dma_rx_chan);
    bus_interface_rx_only_enable(bus_pio, bus_sm);
}

void bus_rx_only_stop(void) {
    bus_interface_rx_only_disable(bus_pio, bus_sm);
    dma_channel_abort(dma_rx_chan);
}

void bus_rx_only_task(void) {
    process_rx_data();
}

static inline uint get_dma_rx_write_idx(void) {
    uint32_t write_addr = dma_channel_hw_addr(dma_rx_chan)->write_addr;
    return write_addr - (uint32_t)dma_rx_buffer;
}

static inline uint32_t get_dma_rx_total_written(void) {
    uint32_t remaining = dma_channel_hw_addr(dma_rx_chan)->transfer_count;
    return 0xFFFFFFFFu - remaining;
}

// Dispatch the completed RX transaction to the device callback.
// Returns true on bankruptcy (caller must bail out of process_rx_data).
static bool dispatch_rx_callback(void) {
    bus_rx_only_callback_t cb = rx_callbacks[current_device];
    if (!cb) return false;

    const uint8_t *data;
    if (rx_transaction_start_idx + rx_transaction_len <= DMA_BUFFER_SIZE) {
        // Contiguous in the ring - point directly into DMA buffer
        data = &dma_rx_buffer[rx_transaction_start_idx];
    } else {
        // Wraps around the ring boundary - assemble contiguous copy
        uint16_t first = DMA_BUFFER_SIZE - rx_transaction_start_idx;
        memcpy(rx_transaction_buf,
               &dma_rx_buffer[rx_transaction_start_idx], first);
        memcpy(rx_transaction_buf + first,
               dma_rx_buffer, rx_transaction_len - first);
        data = rx_transaction_buf;
    }

    cb(current_device, data, rx_transaction_len);

    // Post-callback overrun check
    uint32_t total_written_now = get_dma_rx_total_written();
    if (total_written_now - rx_transaction_total_read_start > DMA_BUFFER_SIZE) {
        printf("!!! RX BANKRUPTCY: DMA overran data during callback "
               "(device %d, %d bytes) !!!\n",
               current_device, rx_transaction_len);
        stats.rx_bankruptcies++;
        proto_state = PROTO_IDLE;
        dma_rx_read_idx = get_dma_rx_write_idx();
        dma_rx_total_read = total_written_now;
        return true;
    }

    return false;
}

static void process_rx_data(void) {
    uint32_t total_written = get_dma_rx_total_written();
    uint32_t unread = total_written - dma_rx_total_read;
    uint write_idx = get_dma_rx_write_idx();

    if (unread > DMA_BUFFER_SIZE) {
        stats.rx_dma_overruns++;
        dma_rx_read_idx = write_idx;
        dma_rx_total_read = total_written;
        proto_state = PROTO_IDLE;
        return;
    }

    while (dma_rx_read_idx != write_idx) {
        uint8_t byte = dma_rx_buffer[dma_rx_read_idx];
        dma_rx_read_idx = (dma_rx_read_idx + 1) % DMA_BUFFER_SIZE;
        dma_rx_total_read++;
        stats.rx_bytes++;

        switch (proto_state) {
            case PROTO_IDLE:
                // First byte: device number (bit 7 = read flag)
                current_device = byte & 0x7F;
                if (current_device >= BUS_RX_ONLY_MAX_DEVICES) {
                    // Invalid device - discard and stay idle
                    break;
                }
                if (byte & 0x80) {
                    // Read request - ignore in RX-only mode
                    stats.rx_read_requests++;
                    proto_state = PROTO_IDLE;
                } else {
                    // Write request - expect length next
                    proto_state = PROTO_GOT_DEVICE;
                }
                break;

            case PROTO_GOT_DEVICE:
                // Second byte: length
                transfer_remaining = byte;
                if (transfer_remaining == 0) {
                    proto_state = PROTO_IDLE;
                } else {
                    // Record where the data payload starts in the DMA ring
                    rx_transaction_start_idx = dma_rx_read_idx;
                    rx_transaction_len = transfer_remaining;
                    rx_transaction_total_read_start = dma_rx_total_read;
                    proto_state = PROTO_RECEIVING;
                }
                break;

            case PROTO_RECEIVING:
                // Consume data bytes (no copy - callback reads from DMA buffer)
                transfer_remaining--;
                if (transfer_remaining == 0) {
                    if (dispatch_rx_callback()) return;
                    proto_state = PROTO_IDLE;
                }
                break;
        }
    }
}

bus_rx_only_stats_t bus_rx_only_get_stats(void) {
    return stats;
}

void bus_rx_only_clear_stats(void) {
    memset(&stats, 0, sizeof(stats));
}
