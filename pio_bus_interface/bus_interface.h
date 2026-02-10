/*
 * PIO-based 6502 Bus Interface Driver
 *
 * This driver manages the PIO state machine and DMA transfers for
 * bidirectional communication with a 6502 CPU.
 */

#ifndef BUS_INTERFACE_H
#define BUS_INTERFACE_H

#include <stdint.h>
#include <stdbool.h>

// Maximum number of devices (channels)
#define BUS_MAX_DEVICES     256

// Maximum buffer size per device
#define BUS_MAX_BUFFER_SIZE 256

// Initialize the bus interface (PIO + DMA)
// Returns true on success, false on failure
bool bus_init(void);

// Start the bus interface (enables PIO state machine)
void bus_start(void);

// Stop the bus interface (disables PIO state machine)
void bus_stop(void);

// Process incoming/outgoing data (call regularly from main loop)
// This handles the protocol layer and device buffer management
void bus_task(void);

// Get the number of bytes available to read from a device
uint16_t bus_device_rx_available(uint8_t device);

// Read data from a device buffer
// Returns number of bytes actually read
uint16_t bus_device_read(uint8_t device, uint8_t *buffer, uint16_t max_len);

// Write data to a device buffer (for CPU to read)
// Returns number of bytes actually written
uint16_t bus_device_write(uint8_t device, const uint8_t *data, uint16_t len);

// Clear a device's buffers
void bus_device_clear(uint8_t device);

// Get statistics
typedef struct {
    uint32_t rx_bytes;      // Total bytes received from CPU
    uint32_t tx_bytes;      // Total bytes sent to CPU
    uint32_t rx_overflows;  // RX buffer overflow count
    uint32_t rx_dma_overruns; // RX DMA ring overrun count
    uint32_t tx_underflows; // TX FIFO underflow count (reads when empty)
} bus_stats_t;

void bus_get_stats(bus_stats_t *stats);
void bus_clear_stats(void);

#endif // BUS_INTERFACE_H
