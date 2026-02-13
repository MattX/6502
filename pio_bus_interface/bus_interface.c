/*
 * PIO-based 6502 Bus Interface Driver Implementation
 *
 * Simplified protocol without A0/status register:
 *   Write: [device] [length] [data...]      (device bit 7 = 0)
 *   Read:  [device|0x80] -> [length] [data...] or 0xFF if not ready
 */

#include "bus_interface.h"
#include "bus_interface.pio.h"

#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/dma.h"

#include <string.h>

// PIO and state machine configuration
static PIO bus_pio = pio0;
static uint bus_sm = 0;
static uint bus_program_offset;

// DMA channels
static int dma_rx_chan = -1;
static int dma_tx_chan = -1;

// DMA buffers
#define DMA_BUFFER_SIZE 256
static uint32_t __attribute__((aligned(1024))) dma_rx_buffer[DMA_BUFFER_SIZE];
static volatile uint dma_rx_read_idx = 0;
static uint32_t dma_rx_total_read = 0;

// One-shot TX DMA staging buffer (each byte widened to a 32-bit word)
static uint32_t tx_staging[256];

// Device buffers
typedef struct {
    uint8_t data[BUS_MAX_BUFFER_SIZE];
    uint16_t head;  // Write position
    uint16_t tail;  // Read position
    uint16_t count; // Bytes in buffer
} device_buffer_t;

static device_buffer_t device_rx_buffers[BUS_MAX_DEVICES];  // CPU -> MCU
static device_buffer_t device_tx_buffers[BUS_MAX_DEVICES];  // MCU -> CPU

// Protocol state machine
typedef enum {
    PROTO_IDLE,
    PROTO_GOT_DEVICE,
    PROTO_RECEIVING,
    PROTO_SENDING
} proto_state_t;

static proto_state_t proto_state = PROTO_IDLE;
static uint8_t current_device = 0;
static uint16_t transfer_remaining = 0;
static bool pending_read_request = false;
static bool read_underflow_recorded = false;

// Statistics
static bus_stats_t stats = {0};

// Forward declarations
static void setup_dma(void);
static void process_rx_data(void);
static void feed_tx_fifo(void);

bool bus_init(void) {
    // Clear device buffers
    memset(device_rx_buffers, 0, sizeof(device_rx_buffers));
    memset(device_tx_buffers, 0, sizeof(device_tx_buffers));

    // Load PIO program
    if (!pio_can_add_program(bus_pio, &bus_interface_program)) {
        return false;
    }
    bus_program_offset = pio_add_program(bus_pio, &bus_interface_program);

    // Initialize PIO state machine
    bus_interface_program_init(bus_pio, bus_sm, bus_program_offset);

    // Set up DMA
    setup_dma();

    return true;
}

static void setup_dma(void) {
    // Claim DMA channels
    dma_rx_chan = dma_claim_unused_channel(true);
    dma_tx_chan = dma_claim_unused_channel(true);

    // === RX DMA: PIO RX FIFO -> RAM buffer ===
    dma_channel_config rx_config = dma_channel_get_default_config(dma_rx_chan);
    channel_config_set_transfer_data_size(&rx_config, DMA_SIZE_32);
    channel_config_set_read_increment(&rx_config, false);
    channel_config_set_write_increment(&rx_config, true);
    channel_config_set_ring(&rx_config, true, 10);  // Wrap at 1024 bytes
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
}

void bus_start(void) {
    dma_channel_start(dma_rx_chan);
    bus_interface_enable(bus_pio, bus_sm);
}

void bus_stop(void) {
    bus_interface_disable(bus_pio, bus_sm);
    dma_channel_abort(dma_rx_chan);
    dma_channel_abort(dma_tx_chan);
    proto_state = PROTO_IDLE;
}

void bus_task(void) {
    process_rx_data();
    feed_tx_fifo();
}

static inline uint get_dma_rx_write_idx(void) {
    uint32_t write_addr = dma_channel_hw_addr(dma_rx_chan)->write_addr;
    return (write_addr - (uint32_t)dma_rx_buffer) / sizeof(uint32_t);
}

static inline uint32_t get_dma_rx_total_written(void) {
    uint32_t remaining = dma_channel_hw_addr(dma_rx_chan)->transfer_count;
    return 0xFFFFFFFFu - remaining;
}

static void process_rx_data(void) {
    uint32_t total_written = get_dma_rx_total_written();
    uint32_t unread = total_written - dma_rx_total_read;
    uint write_idx = get_dma_rx_write_idx();

    if (unread > DMA_BUFFER_SIZE) {
        stats.rx_dma_overruns++;
        dma_rx_read_idx = write_idx;
        dma_rx_total_read = total_written;
        return;
    }

    while (dma_rx_read_idx != write_idx) {
        uint8_t byte = dma_rx_buffer[dma_rx_read_idx] & 0xFF;
        dma_rx_read_idx = (dma_rx_read_idx + 1) % DMA_BUFFER_SIZE;
        dma_rx_total_read++;
        stats.rx_bytes++;

        switch (proto_state) {
            case PROTO_IDLE:
                // First byte: device number (bit 7 = read flag)
                current_device = byte & 0x7F;
                if (current_device >= BUS_MAX_DEVICES) {
                    // Invalid device - discard and stay idle
                    break;
                }
                if (byte & 0x80) {
                    // Read request - queue response
                    pending_read_request = true;
                    read_underflow_recorded = false;
                    proto_state = PROTO_IDLE;  // Stay idle, feed_tx_fifo handles response
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
                    proto_state = PROTO_RECEIVING;
                }
                break;

            case PROTO_RECEIVING:
                // Store data in device buffer
                {
                    device_buffer_t *buf = &device_rx_buffers[current_device];
                    if (buf->count < BUS_MAX_BUFFER_SIZE) {
                        buf->data[buf->head] = byte;
                        buf->head = (buf->head + 1) % BUS_MAX_BUFFER_SIZE;
                        buf->count++;
                    } else {
                        stats.rx_overflows++;
                    }
                }
                transfer_remaining--;
                if (transfer_remaining == 0) {
                    proto_state = PROTO_IDLE;
                }
                break;

            case PROTO_SENDING:
                // Unexpected RX during send - treat as new command
                current_device = byte & 0x7F;
                if (current_device >= BUS_MAX_DEVICES) {
                    // Invalid device - discard and return to idle
                    proto_state = PROTO_IDLE;
                    break;
                }
                if (byte & 0x80) {
                    pending_read_request = true;
                    read_underflow_recorded = false;
                    proto_state = PROTO_IDLE;
                } else {
                    proto_state = PROTO_GOT_DEVICE;
                }
                break;
        }
    }
}

static void feed_tx_fifo(void) {
    // Check if a previous one-shot DMA has completed
    if (proto_state == PROTO_SENDING && !dma_channel_is_busy(dma_tx_chan)) {
        proto_state = PROTO_IDLE;
    }

    // Handle pending read request (only if no DMA in flight)
    if (pending_read_request && proto_state != PROTO_SENDING) {
        device_buffer_t *buf = &device_tx_buffers[current_device];

        if (buf->count > 0) {
            // Build staging buffer: [length, data0, data1, ...]
            uint8_t len = (buf->count > 254) ? 254 : buf->count;
            tx_staging[0] = (uint32_t)len;

            for (uint16_t i = 0; i < len; i++) {
                tx_staging[1 + i] = (uint32_t)buf->data[buf->tail];
                buf->tail = (buf->tail + 1) % BUS_MAX_BUFFER_SIZE;
                buf->count--;
            }

            stats.tx_bytes += len;

            // Configure and start one-shot DMA transfer
            dma_channel_config tx_config = dma_channel_get_default_config(dma_tx_chan);
            channel_config_set_transfer_data_size(&tx_config, DMA_SIZE_32);
            channel_config_set_read_increment(&tx_config, true);
            channel_config_set_write_increment(&tx_config, false);
            channel_config_set_dreq(&tx_config, pio_get_dreq(bus_pio, bus_sm, true));

            dma_channel_configure(
                dma_tx_chan,
                &tx_config,
                &bus_pio->txf[bus_sm],
                tx_staging,
                len + 1,
                true  // Start immediately
            );

            proto_state = PROTO_SENDING;
            pending_read_request = false;
            read_underflow_recorded = false;
        } else if (!read_underflow_recorded) {
            // No data available - leave FIFO empty (sentinel handles it)
            stats.tx_underflows++;
            read_underflow_recorded = true;
        }
    }
}

uint16_t bus_device_rx_available(uint8_t device) {
    if (device >= BUS_MAX_DEVICES) return 0;
    return device_rx_buffers[device].count;
}

uint16_t bus_device_read(uint8_t device, uint8_t *buffer, uint16_t max_len) {
    if (device >= BUS_MAX_DEVICES) return 0;
    device_buffer_t *buf = &device_rx_buffers[device];
    uint16_t to_read = (buf->count < max_len) ? buf->count : max_len;

    for (uint16_t i = 0; i < to_read; i++) {
        buffer[i] = buf->data[buf->tail];
        buf->tail = (buf->tail + 1) % BUS_MAX_BUFFER_SIZE;
    }
    buf->count -= to_read;

    return to_read;
}

uint16_t bus_device_write(uint8_t device, const uint8_t *data, uint16_t len) {
    if (device >= BUS_MAX_DEVICES) return 0;
    device_buffer_t *buf = &device_tx_buffers[device];
    uint16_t space = BUS_MAX_BUFFER_SIZE - buf->count;
    uint16_t to_write = (len < space) ? len : space;

    for (uint16_t i = 0; i < to_write; i++) {
        buf->data[buf->head] = data[i];
        buf->head = (buf->head + 1) % BUS_MAX_BUFFER_SIZE;
    }
    buf->count += to_write;

    return to_write;
}

void bus_device_clear(uint8_t device) {
    if (device >= BUS_MAX_DEVICES) return;
    device_rx_buffers[device].head = 0;
    device_rx_buffers[device].tail = 0;
    device_rx_buffers[device].count = 0;

    device_tx_buffers[device].head = 0;
    device_tx_buffers[device].tail = 0;
    device_tx_buffers[device].count = 0;
}

void bus_get_stats(bus_stats_t *s) {
    *s = stats;
}

void bus_clear_stats(void) {
    memset(&stats, 0, sizeof(stats));
}
