/*
 * PIO-based 6502 Bus Interface Driver Implementation
 *
 * Protocol (no A0/status register):
 *   Write (CPU -> MCU): [device] [length] [data...]
 *   Read  (MCU -> CPU): [device|0x80] -> poll for != 0xFF, then [length] [data...]
 *
 * RX data is dispatched to per-device callbacks directly from the DMA
 * ring buffer.  DMA runs in TRIGGER_SELF mode for endless operation;
 * an epoch counter (maintained via DMA IRQ) tracks total bytes written
 * for overrun detection.  A post-callback check detects the case where
 * DMA overwrites data while a callback is executing ("bankruptcy").
 */

#include "bus_interface.h"
#include "bus_interface.pio.h"

_Static_assert(BUS_PIN_PHI2 == PIN_6502_PHI2,
               "PHI2 pin mismatch between PIO (bus_interface.pio) and bridge_defs.h");

#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/irq.h"

#include <stdio.h>
#include <string.h>

// PIO and state machine configuration
static PIO bus_pio = pio0;
static uint bus_sm = 0;
static uint bus_program_offset;

// DMA channels
static int dma_rx_chan = -1;
static int dma_tx_chan = -1;

// DMA RX ring buffer
static uint8_t __attribute__((aligned(BUS_DMA_RING_SIZE))) dma_rx_buffer[BUS_DMA_RING_SIZE];
static volatile uint dma_rx_read_idx = 0;
static uint32_t dma_rx_total_read = 0;

// Epoch counter: incremented by DMA IRQ each time the transfer count
// wraps (every BUS_DMA_RING_SIZE bytes).  Volatile because it's written
// from the IRQ handler and read from the main loop.
static volatile uint32_t dma_rx_epoch = 0;

// One-shot TX DMA staging buffer.  Each byte is widened to a 32-bit word
// because PIO TX FIFO entries are 32 bits and DMA uses DMA_SIZE_32.
static uint32_t tx_staging[256];

// Per-device TX buffers (MCU -> CPU)
typedef struct {
    uint8_t data[BUS_MAX_BUFFER_SIZE];
    uint16_t head;  // Write position
    uint16_t tail;  // Read position
    uint16_t count; // Bytes in buffer
} device_buffer_t;

static device_buffer_t device_tx_buffers[BUS_MAX_DEVICES];

// Per-device RX callbacks
static bus_rx_callback_t rx_callbacks[BUS_MAX_DEVICES];

// Per-device TX callbacks (bypass circular buffer when set)
static bus_tx_callback_t tx_callbacks[BUS_MAX_DEVICES];

// Temp buffer for assembling wrapped DMA ring data (max transfer = 255)
static uint8_t rx_transaction_buf[255];

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
static uint8_t pending_read_device = 0;   // device ID saved when read request is received
static bool read_underflow_recorded = false;

// RX transaction tracking for callback dispatch + overrun detection
static uint rx_transaction_start_idx = 0;
static uint16_t rx_transaction_len = 0;
static uint32_t rx_transaction_total_read_start = 0;

// Statistics
static bus_stats_t stats = {0};

// Forward declarations
static void setup_dma(void);
static void process_rx_data(void);
static void feed_tx_fifo(void);
static void dma_rx_irq_handler(void);

void bus_register_rx_callback(uint8_t device, bus_rx_callback_t callback) {
    if (device < BUS_MAX_DEVICES) {
        rx_callbacks[device] = callback;
    }
}

void bus_register_tx_callback(uint8_t device, bus_tx_callback_t callback) {
    if (device < BUS_MAX_DEVICES) {
        tx_callbacks[device] = callback;
    }
}

bool bus_init(void) {
    memset(device_tx_buffers, 0, sizeof(device_tx_buffers));
    memset(rx_callbacks, 0, sizeof(rx_callbacks));
    memset(tx_callbacks, 0, sizeof(tx_callbacks));
    memset(&stats, 0, sizeof(stats));

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

    // === RX DMA: PIO RX FIFO -> RAM ring buffer ===
    dma_channel_config rx_config = dma_channel_get_default_config(dma_rx_chan);
    channel_config_set_transfer_data_size(&rx_config, DMA_SIZE_8);
    channel_config_set_read_increment(&rx_config, false);
    channel_config_set_write_increment(&rx_config, true);
    channel_config_set_ring(&rx_config, true, BUS_DMA_RING_BITS);
    channel_config_set_dreq(&rx_config, pio_get_dreq(bus_pio, bus_sm, false));
    channel_config_set_high_priority(&rx_config, true);

    // Use TRIGGER_SELF mode: DMA counts down BUS_DMA_RING_SIZE transfers,
    // then re-triggers itself (endless operation).  Each re-trigger fires
    // an IRQ so we can track total bytes via an epoch counter.
    uint32_t trans_count = BUS_DMA_RING_SIZE | DMA_TRANS_COUNT_MODE_TRIGGER_SELF;

    dma_channel_configure(
        dma_rx_chan,
        &rx_config,
        dma_rx_buffer,
        &bus_pio->rxf[bus_sm],
        trans_count,
        false
    );

    // DMA IRQ: fires each time transfer count reaches 0 (every
    // BUS_DMA_RING_SIZE bytes).  We use this to maintain the epoch counter
    // for total-bytes-written tracking.
    dma_channel_set_irq0_enabled(dma_rx_chan, true);
    irq_set_exclusive_handler(DMA_IRQ_0, dma_rx_irq_handler);
    irq_set_enabled(DMA_IRQ_0, true);

    dma_rx_epoch = 0;
    dma_rx_read_idx = 0;
    dma_rx_total_read = 0;
}

// DMA IRQ handler: called each time the transfer count reaches 0 and the
// channel re-triggers.  This happens every BUS_DMA_RING_SIZE bytes.
static void __isr dma_rx_irq_handler(void) {
    dma_channel_acknowledge_irq0(dma_rx_chan);
    dma_rx_epoch++;
}

void bus_start(void) {
    dma_channel_start(dma_rx_chan);
    bus_interface_enable(bus_pio, bus_sm);
}

void bus_stop(void) {
    bus_interface_disable(bus_pio, bus_sm);
    dma_channel_abort(dma_rx_chan);
    dma_channel_abort(dma_tx_chan);
    dma_channel_set_irq0_enabled(dma_rx_chan, false);
    proto_state = PROTO_IDLE;
}

void bus_task(void) {
    process_rx_data();
    feed_tx_fifo();
}

static inline uint get_dma_rx_write_idx(void) {
    uint32_t write_addr = dma_channel_hw_addr(dma_rx_chan)->write_addr;
    return write_addr - (uint32_t)dma_rx_buffer;
}

// Compute total bytes written by DMA since start.
//
// total = epoch * BUS_DMA_RING_SIZE + (BUS_DMA_RING_SIZE - remaining)
//
// Two race conditions to handle:
//
// 1) Epoch/count tear: the IRQ fires between reading epoch and
//    transfer_count, giving inconsistent values.  Solved by re-reading
//    epoch after transfer_count and retrying if it changed.
//
// 2) Re-trigger latency: when DMA re-triggers, transfer_count resets
//    to BUS_DMA_RING_SIZE instantly (hardware), but the epoch IRQ hasn't
//    fired yet (~80ns latency).  Reading during this window gives the
//    OLD epoch with the NEW (reset) count, making total appear one
//    BUS_DMA_RING_SIZE too low.  Detected because total must never be
//    less than dma_rx_total_read; corrected by adding BUS_DMA_RING_SIZE.
static inline uint32_t get_dma_rx_total_written(void) {
    uint32_t epoch, remaining;
    do {
        epoch = dma_rx_epoch;
        __compiler_memory_barrier();
        remaining = dma_channel_hw_addr(dma_rx_chan)->transfer_count
                    & DMA_TRANS_COUNT_COUNT_MASK;
        __compiler_memory_barrier();
    } while (epoch != dma_rx_epoch);
    uint32_t total = epoch * BUS_DMA_RING_SIZE + (BUS_DMA_RING_SIZE - remaining);
    if (total < dma_rx_total_read) {
        total += BUS_DMA_RING_SIZE;
    }
    return total;
}

// Dispatch the completed RX transaction to the device callback.
// Returns true on bankruptcy (caller must bail out of process_rx_data).
static bool dispatch_rx_callback(void) {
    bus_rx_callback_t cb = rx_callbacks[current_device];
    if (!cb) return false;

    const uint8_t *data;
    if (rx_transaction_start_idx + rx_transaction_len <= BUS_DMA_RING_SIZE) {
        // Contiguous in the ring - point directly into DMA buffer
        data = &dma_rx_buffer[rx_transaction_start_idx];
    } else {
        // Wraps around the ring boundary - assemble contiguous copy
        uint16_t first = BUS_DMA_RING_SIZE - rx_transaction_start_idx;
        memcpy(rx_transaction_buf,
               &dma_rx_buffer[rx_transaction_start_idx], first);
        memcpy(rx_transaction_buf + first,
               dma_rx_buffer, rx_transaction_len - first);
        data = rx_transaction_buf;
    }

    cb(current_device, data, rx_transaction_len);

    // Post-callback overrun check: if DMA has written more than a full
    // buffer since we started reading this transaction's data, the bytes
    // the callback just processed may have been overwritten mid-read.
    uint32_t total_written_now = get_dma_rx_total_written();
    if (total_written_now - rx_transaction_total_read_start > BUS_DMA_RING_SIZE) {
        printf("!!! FATAL: 6502 RX BANKRUPTCY: DMA overran data during callback "
               "(device %d, %d bytes)\n",
               current_device, rx_transaction_len);
        for (;;) tight_loop_contents();
    }

    return false;
}

static void process_rx_data(void) {
    uint32_t total_written = get_dma_rx_total_written();
    uint32_t unread = total_written - dma_rx_total_read;
    uint write_idx = get_dma_rx_write_idx();

    if (unread > BUS_DMA_RING_SIZE) {
        printf("!!! FATAL: 6502 RX DMA OVERRUN: %lu bytes lost\n",
               (unsigned long)(unread - BUS_DMA_RING_SIZE));
        for (;;) tight_loop_contents();
    }

    while (dma_rx_read_idx != write_idx) {
        uint8_t byte = dma_rx_buffer[dma_rx_read_idx];
        dma_rx_read_idx = (dma_rx_read_idx + 1) & (BUS_DMA_RING_SIZE - 1);
        dma_rx_total_read++;
        stats.rx_bytes++;

        DBG_PRINTF("RX byte=0x%02x dev=%d state=%d\n", byte,
                   byte & 0x7F, proto_state);

        switch (proto_state) {
            case PROTO_IDLE:
                // First byte: device number (bit 7 = read flag)
                current_device = byte & 0x7F;
                if (current_device >= BUS_MAX_DEVICES) {
                    printf("!!! Invalid device %d (byte=0x%02x) in IDLE\n",
                           current_device, byte);
                    break;
                }
                if (byte & 0x80) {
                    // Read request - save device and queue for feed_tx_fifo
                    pending_read_request = true;
                    pending_read_device = current_device;
                    read_underflow_recorded = false;
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

            case PROTO_SENDING:
                // Unexpected RX during send - treat as new command
                printf("!!! RX byte=0x%02x during SENDING (dma_busy=%d)\n",
                       byte, dma_channel_is_busy(dma_tx_chan));
                current_device = byte & 0x7F;
                if (current_device >= BUS_MAX_DEVICES) {
                    printf("!!! Invalid device %d in SENDING\n", current_device);
                    proto_state = PROTO_IDLE;
                    break;
                }
                if (byte & 0x80) {
                    pending_read_request = true;
                    pending_read_device = current_device;
                    read_underflow_recorded = false;
                    proto_state = PROTO_IDLE;
                } else {
                    proto_state = PROTO_GOT_DEVICE;
                }
                break;
        }
    }
}

// Start a one-shot DMA transfer from tx_staging to PIO TX FIFO.
static void start_tx_dma(uint count) {
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
        count,
        true  // Start immediately
    );
}

static void feed_tx_fifo(void) {
    // Check if a previous one-shot DMA has completed
    if (proto_state == PROTO_SENDING && !dma_channel_is_busy(dma_tx_chan)) {
        proto_state = PROTO_IDLE;
    }

    // Handle pending read request (only if no DMA in flight)
    if (pending_read_request && proto_state != PROTO_SENDING) {
        uint8_t len = 0;

        // If a TX callback is registered, use it instead of the device buffer
        bus_tx_callback_t tx_cb = tx_callbacks[pending_read_device];
        if (tx_cb) {
            uint8_t cb_data[254];
            len = tx_cb(cb_data, 254);
            tx_staging[0] = (uint32_t)len;
            for (uint8_t i = 0; i < len; i++) {
                tx_staging[1 + i] = (uint32_t)cb_data[i];
            }
        } else {
            device_buffer_t *buf = &device_tx_buffers[pending_read_device];
            DBG_PRINTF("dev%d buf: count=%d head=%d tail=%d\n",
                       pending_read_device, buf->count, buf->head, buf->tail);
            if (buf->count > 0) {
                len = (buf->count > 254) ? 254 : buf->count;
                tx_staging[0] = (uint32_t)len;
                for (uint16_t i = 0; i < len; i++) {
                    tx_staging[1 + i] = (uint32_t)buf->data[buf->tail];
                    buf->tail = (buf->tail + 1) & (BUS_MAX_BUFFER_SIZE - 1);
                    buf->count--;
                }
            }
        }

        DBG_PRINTF("TX to bus, device=%d, len=%d\n", pending_read_device, len);

        if (len > 0) {
            stats.tx_bytes += len;
            start_tx_dma(len + 1);
            proto_state = PROTO_SENDING;
            pending_read_request = false;
            read_underflow_recorded = false;
        } else {
            // No data available - send length=0 so the 6502 can move on
            // (otherwise it polls 0xFF forever)
            tx_staging[0] = 0;
            start_tx_dma(1);
            proto_state = PROTO_SENDING;
            pending_read_request = false;
            if (!read_underflow_recorded) {
                stats.tx_underflows++;
                read_underflow_recorded = true;
            }
        }
    }
}

uint16_t bus_device_write(uint8_t device, const uint8_t *data, uint16_t len) {
    if (device >= BUS_MAX_DEVICES) return 0;
    device_buffer_t *buf = &device_tx_buffers[device];
    uint16_t space = BUS_MAX_BUFFER_SIZE - buf->count;
    uint16_t to_write = (len < space) ? len : space;

    // Copy in up to two chunks (handles ring wrap)
    uint16_t first = BUS_MAX_BUFFER_SIZE - buf->head;
    if (first > to_write) first = to_write;
    memcpy(&buf->data[buf->head], data, first);
    if (to_write > first) {
        memcpy(buf->data, data + first, to_write - first);
    }
    buf->head = (buf->head + to_write) & (BUS_MAX_BUFFER_SIZE - 1);
    buf->count += to_write;

    return to_write;
}

void bus_device_clear(uint8_t device) {
    if (device >= BUS_MAX_DEVICES) return;
    device_tx_buffers[device].head = 0;
    device_tx_buffers[device].tail = 0;
    device_tx_buffers[device].count = 0;
}

uint16_t bus_device_tx_count(uint8_t device) {
    if (device >= BUS_MAX_DEVICES) return 0;
    return device_tx_buffers[device].count;
}

uint16_t bus_device_tx_free(uint8_t device) {
    if (device >= BUS_MAX_DEVICES) return 0;
    return BUS_MAX_BUFFER_SIZE - device_tx_buffers[device].count;
}

bus_stats_t bus_get_stats(void) {
    return stats;
}

void bus_clear_stats(void) {
    memset(&stats, 0, sizeof(stats));
}

void bus_diagnose(void) {
    // PIO state machine
    uint8_t pc = pio_sm_get_pc(bus_pio, bus_sm);
    uint tx_fifo = pio_sm_get_tx_fifo_level(bus_pio, bus_sm);
    uint rx_fifo = pio_sm_get_rx_fifo_level(bus_pio, bus_sm);

    // GPIO 6-13 pin directions (1=output, 0=input)
    uint8_t pindirs = 0;
    for (int i = 0; i < 8; i++) {
        if (gpio_get_dir(BUS_PIN_D0 + i))
            pindirs |= (1 << i);
    }

    // PIO pad output enables for data bus
    uint32_t padoe = bus_pio->dbg_padoe;
    uint8_t pio_oe = (padoe >> BUS_PIN_D0) & 0xFF;

    printf("       diag: pc=%d tx_fifo=%d rx_fifo=%d pindirs=0x%02x pio_oe=0x%02x\n",
           pc, tx_fifo, rx_fifo, pindirs, pio_oe);
    printf("             proto=%d pending_rd=%d rd_dev=%d dma_tx_busy=%d dev7_buf=%d\n",
           proto_state, pending_read_request, pending_read_device,
           dma_channel_is_busy(dma_tx_chan), device_tx_buffers[7].count);
}
