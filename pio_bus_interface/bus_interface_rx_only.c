/*
 * PIO-based 6502 Bus Interface Driver Implementation (RX-only)
 *
 * Simplified protocol without A0/status register:
 *   Write: [device] [length] [data...]      (device bit 7 = 0)
 *   Read:  [device|0x80]                    (ignored in RX-only mode)
 */

#include "bus_interface_rx_only.h"
#include "bus_interface_rx_only.pio.h"

#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/dma.h"

#include <string.h>

// PIO and state machine configuration
static PIO bus_pio = pio0;
static uint bus_sm = 0;
static uint bus_program_offset;

// DMA channel
static int dma_rx_chan = -1;

// DMA buffer (circular)
#define DMA_BUFFER_SIZE 1024
static uint32_t __attribute__((aligned(4096))) dma_rx_buffer[DMA_BUFFER_SIZE];
static volatile uint dma_rx_read_idx = 0;
static uint32_t dma_rx_total_read = 0;

// Device buffers
typedef struct {
    uint8_t data[BUS_RX_ONLY_MAX_BUFFER_SIZE];
    uint16_t head;  // Write position
    uint16_t tail;  // Read position
    uint16_t count; // Bytes in buffer
} device_buffer_t;

static device_buffer_t device_rx_buffers[BUS_RX_ONLY_MAX_DEVICES];  // CPU -> MCU

// Protocol state machine
typedef enum {
    PROTO_IDLE,
    PROTO_GOT_DEVICE,
    PROTO_RECEIVING
} proto_state_t;

static proto_state_t proto_state = PROTO_IDLE;
static uint8_t current_device = 0;
static uint16_t transfer_remaining = 0;

// Statistics
static bus_rx_only_stats_t stats = {0};

// Forward declarations
static void setup_dma(void);
static void process_rx_data(void);

bool bus_rx_only_init(void) {
    // Clear device buffers
    memset(device_rx_buffers, 0, sizeof(device_rx_buffers));
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
        proto_state = PROTO_IDLE;
        transfer_remaining = 0;
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
                    proto_state = PROTO_RECEIVING;
                }
                break;

            case PROTO_RECEIVING:
                // Store data in device buffer
                {
                    device_buffer_t *buf = &device_rx_buffers[current_device];
                    if (buf->count < BUS_RX_ONLY_MAX_BUFFER_SIZE) {
                        buf->data[buf->head] = byte;
                        buf->head = (buf->head + 1) % BUS_RX_ONLY_MAX_BUFFER_SIZE;
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
        }
    }
}

uint16_t bus_rx_only_device_available(uint8_t device) {
    if (device >= BUS_RX_ONLY_MAX_DEVICES) return 0;
    return device_rx_buffers[device].count;
}

uint16_t bus_rx_only_device_read(uint8_t device, uint8_t *buffer, uint16_t max_len) {
    if (device >= BUS_RX_ONLY_MAX_DEVICES) return 0;
    device_buffer_t *buf = &device_rx_buffers[device];
    uint16_t to_read = (buf->count < max_len) ? buf->count : max_len;

    for (uint16_t i = 0; i < to_read; i++) {
        buffer[i] = buf->data[buf->tail];
        buf->tail = (buf->tail + 1) % BUS_RX_ONLY_MAX_BUFFER_SIZE;
    }
    buf->count -= to_read;

    return to_read;
}

void bus_rx_only_device_clear(uint8_t device) {
    if (device >= BUS_RX_ONLY_MAX_DEVICES) return;
    device_rx_buffers[device].head = 0;
    device_rx_buffers[device].tail = 0;
    device_rx_buffers[device].count = 0;
}

bus_rx_only_stats_t bus_rx_only_get_stats(void) {
    return stats;
}

void bus_rx_only_clear_stats(void) {
    memset(&stats, 0, sizeof(stats));
}
