/*
 * PIO-based 6502 Bus Interface Driver Implementation (RX-only)
 *
 * Simplified protocol without A0/status register:
 *   Write: [device] [length] [data...]      (device bit 7 = 0)
 *   Read:  [device|0x80]                    (ignored in RX-only mode)
 *
 * RX data is dispatched to per-device callbacks directly from the DMA
 * ring buffer.  DMA runs in TRIGGER_SELF mode for endless operation;
 * an epoch counter (maintained via DMA IRQ) tracks total bytes written
 * for overrun detection.  A post-callback check detects the case where
 * DMA overwrites data while a callback is executing ("bankruptcy").
 */

#include "bus_interface_rx_only.h"
#include "bus_interface_rx_only.pio.h"

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

// DMA channel
static int dma_rx_chan = -1;

// DMA RX ring buffer
#define DMA_BUFFER_RING_BITS 15    // 2^15 = 32768
#define DMA_BUFFER_SIZE      (1 << DMA_BUFFER_RING_BITS)
static uint8_t __attribute__((aligned(DMA_BUFFER_SIZE))) dma_rx_buffer[DMA_BUFFER_SIZE];
static volatile uint dma_rx_read_idx = 0;
static uint32_t dma_rx_total_read = 0;

/*
 * RP2350 DMA TRANS_COUNT register layout (different from RP2040!):
 *   Bits [31:28] = MODE   (4 bits)
 *   Bits [27:0]  = COUNT  (28 bits, max 0x0FFFFFFF)
 *
 * MODE values:
 *   0x0 = NORMAL       - count decrements, channel stops at 0
 *   0x1 = TRIGGER_SELF - count decrements, channel re-triggers at 0
 *   0xF = ENDLESS      - count NEVER decrements (runs forever)
 *
 * The old code wrote 0xFFFFFFFF which sets MODE=0xF (ENDLESS).  In ENDLESS
 * mode the transfer_count register never changes, so our overrun detection
 * formula (total_written = initial - remaining) always returned 0.  This
 * caused dma_rx_total_read to drift ahead of total_written, wrapping the
 * unsigned subtraction to a huge value and triggering false overruns on
 * every poll.
 *
 * Fix: use TRIGGER_SELF mode (MODE=1) with COUNT=DMA_BUFFER_SIZE.  The DMA
 * counts down each byte transfer and re-triggers itself at 0, running
 * forever.  Each time it re-triggers, a DMA IRQ fires and we increment an
 * epoch counter.  Total bytes written = epoch * DMA_BUFFER_SIZE + consumed,
 * giving reliable overrun detection.
 */
#define DMA_TRANS_COUNT_MODE_TRIGGER_SELF (1u << 28)
#define DMA_TRANS_COUNT_COUNT_MASK       0x0FFFFFFFu

// Epoch counter: incremented by DMA IRQ each time the transfer count
// wraps (every DMA_BUFFER_SIZE bytes).  Volatile because it's written
// from the IRQ handler and read from the main loop.
static volatile uint32_t dma_rx_epoch = 0;

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
static void dma_rx_irq_handler(void);

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
    dma_rx_chan = dma_claim_unused_channel(true);

    // === RX DMA: PIO RX FIFO -> RAM ring buffer ===
    dma_channel_config rx_config = dma_channel_get_default_config(dma_rx_chan);
    channel_config_set_transfer_data_size(&rx_config, DMA_SIZE_8);
    channel_config_set_read_increment(&rx_config, false);
    channel_config_set_write_increment(&rx_config, true);
    channel_config_set_ring(&rx_config, true, DMA_BUFFER_RING_BITS);
    channel_config_set_dreq(&rx_config, pio_get_dreq(bus_pio, bus_sm, false));
    channel_config_set_high_priority(&rx_config, true);

    // Use TRIGGER_SELF mode: DMA counts down DMA_BUFFER_SIZE transfers,
    // then re-triggers itself (endless operation).  Each re-trigger fires
    // an IRQ so we can track total bytes via an epoch counter.
    uint32_t trans_count = DMA_BUFFER_SIZE | DMA_TRANS_COUNT_MODE_TRIGGER_SELF;

    dma_channel_configure(
        dma_rx_chan,
        &rx_config,
        dma_rx_buffer,
        &bus_pio->rxf[bus_sm],
        trans_count,
        false
    );

    // DMA IRQ: fires each time transfer count reaches 0 (every
    // DMA_BUFFER_SIZE bytes).  We use this to maintain the epoch counter
    // for total-bytes-written tracking.
    dma_channel_set_irq0_enabled(dma_rx_chan, true);
    irq_set_exclusive_handler(DMA_IRQ_0, dma_rx_irq_handler);
    irq_set_enabled(DMA_IRQ_0, true);

    dma_rx_epoch = 0;
    dma_rx_read_idx = 0;
    dma_rx_total_read = 0;
}

// DMA IRQ handler: called each time the transfer count reaches 0 and the
// channel re-triggers.  This happens every DMA_BUFFER_SIZE bytes.
static void __isr dma_rx_irq_handler(void) {
    dma_channel_acknowledge_irq0(dma_rx_chan);
    dma_rx_epoch++;
}

void bus_rx_only_start(void) {
    dma_channel_start(dma_rx_chan);
    bus_interface_rx_only_enable(bus_pio, bus_sm);
}

void bus_rx_only_stop(void) {
    bus_interface_rx_only_disable(bus_pio, bus_sm);
    dma_channel_abort(dma_rx_chan);
    dma_channel_set_irq0_enabled(dma_rx_chan, false);
}

void bus_rx_only_task(void) {
    process_rx_data();
}

static inline uint get_dma_rx_write_idx(void) {
    uint32_t write_addr = dma_channel_hw_addr(dma_rx_chan)->write_addr;
    return write_addr - (uint32_t)dma_rx_buffer;
}

// Compute total bytes written by DMA since start.
//
// With TRIGGER_SELF mode, the DMA counts down from DMA_BUFFER_SIZE and
// re-triggers at 0.  The epoch counter (incremented by IRQ) tracks how
// many full rounds have completed.  We read epoch and remaining count
// with a consistency check to handle the race where the IRQ fires
// between the two reads.
//
// There is a second, subtler race: when the DMA re-triggers (hardware,
// instantaneous) the transfer_count resets to DMA_BUFFER_SIZE, but the
// epoch IRQ hasn't fired yet (~80ns latency).  If we read during this
// window, we get the OLD epoch with the NEW (reset) count, making
// total_written appear DMA_BUFFER_SIZE too low.  We detect this because
// total_written must never be less than dma_rx_total_read.
static inline uint32_t get_dma_rx_total_written(void) {
    uint32_t epoch, remaining;
    do {
        epoch = dma_rx_epoch;
        __compiler_memory_barrier();  // ensure epoch is read before transfer_count
        remaining = dma_channel_hw_addr(dma_rx_chan)->transfer_count
                    & DMA_TRANS_COUNT_COUNT_MASK;
        __compiler_memory_barrier();
    } while (epoch != dma_rx_epoch);
    uint32_t total = epoch * DMA_BUFFER_SIZE + (DMA_BUFFER_SIZE - remaining);
    if ((int32_t)(total - dma_rx_total_read) < 0) {
        total += DMA_BUFFER_SIZE;
    }
    return total;
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

    stats.rx_dispatched++;
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
                    // printf("Inv %x\n", current_device);
                    stats.rx_invalid_device++;
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

const uint8_t *bus_rx_only_get_dma_buffer(void) {
    return dma_rx_buffer;
}
