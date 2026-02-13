/*
 * PIO-based 6502 Bus Interface Driver (RX-only)
 *
 * DMA-backed receive path using the RX-only PIO program.
 * The MCU never drives the bus, but the protocol parser and
 * device buffering are exercised for validation.
 */

#ifndef BUS_INTERFACE_RX_ONLY_H
#define BUS_INTERFACE_RX_ONLY_H

#include <stdint.h>
#include <stdbool.h>

// Maximum number of devices (channels)
#define BUS_RX_ONLY_MAX_DEVICES     8

// Maximum buffer size per device
#define BUS_RX_ONLY_MAX_BUFFER_SIZE 256

// Initialize the RX-only bus interface (PIO + DMA)
bool bus_rx_only_init(void);

// Start the RX-only bus interface (enables PIO state machine)
void bus_rx_only_start(void);

// Stop the RX-only bus interface (disables PIO state machine)
void bus_rx_only_stop(void);

// Process incoming data (call regularly from main loop)
void bus_rx_only_task(void);

// Get the number of bytes available to read from a device
uint16_t bus_rx_only_device_available(uint8_t device);

// Read data from a device buffer
uint16_t bus_rx_only_device_read(uint8_t device, uint8_t *buffer, uint16_t max_len);

// Clear a device's buffer
void bus_rx_only_device_clear(uint8_t device);

// Get statistics
typedef struct {
    uint32_t rx_bytes;         // Total bytes received from CPU
    uint32_t rx_overflows;     // RX buffer overflow count
    uint32_t rx_dma_overruns;  // RX DMA ring overrun count
    uint32_t rx_read_requests; // Read requests observed (ignored)
} bus_rx_only_stats_t;

bus_rx_only_stats_t bus_rx_only_get_stats(void);
void bus_rx_only_clear_stats(void);

#endif // BUS_INTERFACE_RX_ONLY_H
