/*
 * PIO-based 6502 Bus Interface Driver
 *
 * This driver manages the PIO state machine and DMA transfers for
 * bidirectional communication with a 6502 CPU.
 *
 * RX data is delivered via per-device callbacks when a complete write
 * transaction is received.  The callback receives a pointer into the
 * DMA ring buffer (or a contiguous copy when the data wraps).  Fast
 * callbacks can process inline; slow ones should memcpy the data into
 * their own structure before returning.
 */

#ifndef BUS_INTERFACE_H
#define BUS_INTERFACE_H

#include <stdint.h>
#include <stdbool.h>

// Maximum number of devices (channels)
#define BUS_MAX_DEVICES     8

// Maximum TX buffer size per device
#define BUS_MAX_BUFFER_SIZE 1024

// RX callback: called when a complete write transaction is received.
// |data| points into the DMA ring buffer and is only valid for the
// duration of the callback.  Copy it if you need it to persist.
typedef void (*bus_rx_callback_t)(uint8_t device, const uint8_t *data, uint16_t len);

// Register a callback for a device (NULL to unregister)
void bus_register_rx_callback(uint8_t device, bus_rx_callback_t callback);

// Initialize the bus interface (PIO + DMA)
// Returns true on success, false on failure
bool bus_init(void);

// Start the bus interface (enables PIO state machine)
void bus_start(void);

// Stop the bus interface (disables PIO state machine)
void bus_stop(void);

// Process incoming/outgoing data (call regularly from main loop)
// This handles the protocol layer and dispatches RX callbacks
void bus_task(void);

// Write data to a device buffer (for CPU to read)
// Returns number of bytes actually written
uint16_t bus_device_write(uint8_t device, const uint8_t *data, uint16_t len);

// Clear a device's TX buffer
void bus_device_clear(uint8_t device);

// Get statistics
typedef struct {
    uint32_t rx_bytes;          // Total bytes received from CPU
    uint32_t tx_bytes;          // Total bytes sent to CPU
    uint32_t rx_dma_overruns;   // DMA overruns (data lost before processing)
    uint32_t rx_bankruptcies;   // DMA overruns during callback (data may be corrupt)
    uint32_t tx_underflows;     // TX FIFO underflow count (reads when empty)
} bus_stats_t;

bus_stats_t bus_get_stats(void);
void bus_clear_stats(void);

#endif // BUS_INTERFACE_H
