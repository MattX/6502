/*
 * PIO-based 6502 Bus Interface Driver (RX-only)
 *
 * DMA-backed receive path using the RX-only PIO program.
 * The MCU never drives the bus.  RX data is delivered via callbacks.
 */

#ifndef BUS_INTERFACE_RX_ONLY_H
#define BUS_INTERFACE_RX_ONLY_H

#include <stdint.h>
#include <stdbool.h>

// Maximum number of devices (channels)
#define BUS_RX_ONLY_MAX_DEVICES     8

// RX callback: called when a complete write transaction is received.
// |data| points into the DMA ring buffer and is only valid for the
// duration of the callback.  Copy it if you need it to persist.
typedef void (*bus_rx_only_callback_t)(uint8_t device, const uint8_t *data, uint16_t len);

// Register a callback for a device (NULL to unregister)
void bus_rx_only_register_callback(uint8_t device, bus_rx_only_callback_t callback);

// Initialize the RX-only bus interface (PIO + DMA)
bool bus_rx_only_init(void);

// Start the RX-only bus interface (enables PIO state machine)
void bus_rx_only_start(void);

// Stop the RX-only bus interface (disables PIO state machine)
void bus_rx_only_stop(void);

// Process incoming data (call regularly from main loop)
void bus_rx_only_task(void);

// Get statistics
typedef struct {
    uint32_t rx_bytes;           // Total bytes received from CPU
    uint32_t rx_dma_overruns;    // DMA overruns (data lost before processing)
    uint32_t rx_bankruptcies;    // DMA overruns during callback (data may be corrupt)
    uint32_t rx_read_requests;   // Read requests observed (ignored)
    uint32_t rx_invalid_device;  // Bytes discarded (device ID >= MAX_DEVICES)
    uint32_t rx_dispatched;      // Successful transaction dispatches
} bus_rx_only_stats_t;

bus_rx_only_stats_t bus_rx_only_get_stats(void);
void bus_rx_only_clear_stats(void);

// Debug: direct access to DMA ring buffer for raw byte inspection
const uint8_t *bus_rx_only_get_dma_buffer(void);

#endif // BUS_INTERFACE_RX_ONLY_H
