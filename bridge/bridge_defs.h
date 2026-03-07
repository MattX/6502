/*
 * Shared definitions for the 6502 <-> Zero SPI Bridge firmware.
 *
 * Centralizes constants that are referenced by multiple source files:
 * pin assignments, buffer sizes, DMA mode bits, and debug toggles.
 */

#ifndef BRIDGE_DEFS_H
#define BRIDGE_DEFS_H

#include <stdio.h>

// ============================================================================
// Pin assignments — 6502 side (non-PIO)
// ============================================================================
// PIO bus pins (RW, CS_N, PHI2, D0-D7) are defined in bus_interface.pio.h
// via .define PUBLIC and the c-sdk block.  Only non-PIO pins go here.

#define PIN_6502_PHI2   2       // PHI2 clock output (PWM)
#define PIN_6502_IRQ    3       // IRQ to 6502 (active-low, open-drain)

// ============================================================================
// Clock configuration
// ============================================================================

#define CLK_SPEED_6502  1000000 // 1 MHz target clock for 6502

// ============================================================================
// Buffer / ring sizes
// ============================================================================

#define BUS_DMA_RING_BITS   15
#define BUS_DMA_RING_SIZE   (1u << BUS_DMA_RING_BITS)   // 32768

#define BUS_MAX_DEVICES     8
#define BUS_MAX_BUFFER_SIZE 4096

#define SPI_TX_QUEUE_SIZE   4096

// ============================================================================
// DMA TRANS_COUNT mode bits (RP2350)
// ============================================================================
// RP2350 TRANS_COUNT register: bits[31:28]=MODE, bits[27:0]=COUNT
//   MODE 0x0 = NORMAL       — count decrements, channel stops at 0
//   MODE 0x1 = TRIGGER_SELF — count decrements, channel re-triggers at 0
//   MODE 0xF = ENDLESS      — count never decrements

#define DMA_TRANS_COUNT_MODE_TRIGGER_SELF  (1u << 28)
#define DMA_TRANS_COUNT_COUNT_MASK         0x0FFFFFFFu

// ============================================================================
// Timing
// ============================================================================

#define STATS_INTERVAL_MS   5000
#define STARTUP_DELAY_MS    2000

// ============================================================================
// Debug output
// ============================================================================
// Set BRIDGE_DEBUG=1 (e.g. via -DBRIDGE_DEBUG=1) to enable verbose protocol
// debug prints.  WARNING: severely impacts throughput due to USB stdio blocking.

#ifndef BRIDGE_DEBUG
#define BRIDGE_DEBUG 0
#endif

#if BRIDGE_DEBUG
#define DBG_PRINTF(...) printf(__VA_ARGS__)
#else
#define DBG_PRINTF(...) ((void)0)
#endif

// ============================================================================
// Static asserts for power-of-two ring buffer sizes
// ============================================================================

_Static_assert((BUS_DMA_RING_SIZE & (BUS_DMA_RING_SIZE - 1)) == 0,
               "BUS_DMA_RING_SIZE must be a power of two");
_Static_assert((BUS_MAX_BUFFER_SIZE & (BUS_MAX_BUFFER_SIZE - 1)) == 0,
               "BUS_MAX_BUFFER_SIZE must be a power of two");
_Static_assert((SPI_TX_QUEUE_SIZE & (SPI_TX_QUEUE_SIZE - 1)) == 0,
               "SPI_TX_QUEUE_SIZE must be a power of two");

#endif // BRIDGE_DEFS_H
