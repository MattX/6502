/*
 * 6502 <-> Zero SPI Bridge
 *
 * Connects the PIO-based 6502 bus interface with the SPI slave interface
 * to the Pi Zero. Data written by the 6502 is forwarded to the Zero over
 * SPI, and data sent by the Zero over SPI is made available to the 6502.
 *
 * Framing (both directions over SPI byte stream):
 *   [device_id (1)] [length (1)] [data ...]
 *
 * IRQ lines:
 *   GPIO 20 -> Zero:  "Pico has data" (managed by spi_slave)
 *   GPIO 3  -> 6502:  "Data available for read" (managed here)
 *
 * Reset:
 *   GPIO 4  -> 6502:  RESB (active-low, open-drain)
 *   The Pico holds RESB low on boot and releases after initialization.
 *   Reset can be triggered by:
 *     - External falling edge on RESB (pushbutton / RC circuit)
 *     - 6502 writing to Device 1 (soft reset)
 *   On reset, the Pico notifies the Zero via a Device 0 TLV ('R'),
 *   waits for the Zero to read it, then reboots via watchdog.
 */

#include <stdio.h>
#include <string.h>

#include "hardware/pwm.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/watchdog.h"
#include "pico/stdlib.h"
#include "bus_interface.h"
#include "spi_slave.h"

// Stats
static uint32_t bus_to_spi_msgs = 0;
static uint32_t bus_to_spi_bytes = 0;
static uint32_t spi_to_bus_msgs = 0;
static uint32_t spi_to_bus_bytes = 0;
static uint32_t spi_to_bus_drops = 0;

// Reset
static volatile bool reset_requested = false;

// Startup banner (deferred until USB is ready)
static bool startup_banner_printed = false;

// ============================================================================
// Device 0: local status register (not forwarded over SPI)
// ============================================================================

static uint8_t device0_tx_callback(uint8_t *data, uint8_t max_len) {
    if (max_len < 2) return 0;

    // Byte 0: bitmask of devices with data available (always 0 for device 0)
    uint8_t avail = 0;
    for (uint8_t i = 1; i < BUS_MAX_DEVICES; i++) {
        if (bus_device_tx_count(i) > 0) {
            avail |= (1 << i);
        }
    }
    data[0] = avail;

    // Byte 1: SPI bridge connected (at least 1 command received)
    data[1] = spi_slave_is_connected() ? 1 : 0;

    return 2;
}

// ============================================================================
// 6502 -> Zero: bus RX callback forwards to SPI TX queue
// ============================================================================

static void bus_to_spi_callback(uint8_t device, const uint8_t *data, uint16_t len) {
    // Bus transfers are max 255 bytes, so len fits in uint8_t
    uint8_t header[2] = { device, (uint8_t)len };

    // Check space upfront to avoid orphaning a header in the queue
    uint free = spi_slave_tx_queue_free();
    DBG_PRINTF("bus->spi: dev=%d len=%d free=%d\n", device, len, free);
    if (free < len + 2) {
        printf("bus->spi: queue full, dropping\n");
        return;
    }
    spi_slave_tx_queue(header, 2);
    spi_slave_tx_queue(data, len);

    bus_to_spi_msgs++;
    bus_to_spi_bytes += len;
}

// ============================================================================
// Device 1: system control (soft reset)
// ============================================================================

static void device1_rx_callback(uint8_t device, const uint8_t *data, uint16_t len) {
    (void)device; (void)data; (void)len;
    reset_requested = true;
}

// ============================================================================
// Zero -> 6502: SPI RX callback parses TLV and writes to bus device buffers
// ============================================================================

static void spi_rx_callback(const uint8_t *data, uint16_t len) {
    uint16_t pos = 0;
    while (pos + 2 <= len) {
        uint8_t device = data[pos];
        uint8_t tlv_len = data[pos + 1];
        DBG_PRINTF("SPI RX: device=%d, tlv_len=%d\n", device, tlv_len);
        if (pos + 2 + tlv_len > len) break;
        if (device > 0 && device < BUS_MAX_DEVICES && tlv_len > 0) {
            uint16_t written = bus_device_write(device, &data[pos + 2], tlv_len);
            DBG_PRINTF("dev%d after spi_rx: written=%d, buf_count=%d\n",
                        device, written, bus_device_tx_count(device));
            if (written < tlv_len) {
                spi_to_bus_drops++;
            }
            spi_to_bus_msgs++;
            spi_to_bus_bytes += written;
        }
        pos += 2 + tlv_len;
    }
}

// ============================================================================
// 6502 IRQ management
// ============================================================================

static bool irq_6502_asserted = false;

static void update_6502_irq(void) {
    bool any_data = false;
    for (uint8_t i = 1; i < BUS_MAX_DEVICES; i++) {
        if (bus_device_tx_count(i) > 0) {
            any_data = true;
            break;
        }
    }

    if (any_data && !irq_6502_asserted) {
        gpio_put(PIN_6502_IRQ, 0);
        gpio_set_dir(PIN_6502_IRQ, GPIO_OUT);  // Drive low
        irq_6502_asserted = true;
    } else if (!any_data && irq_6502_asserted) {
        gpio_set_dir(PIN_6502_IRQ, GPIO_IN);   // Tristate (external pull-up)
        irq_6502_asserted = false;
    }
}

// ============================================================================
// 6502 Clock management
// ============================================================================

static void setup_6502_clock(void) {
    gpio_set_function(PIN_6502_PHI2, GPIO_FUNC_PWM);
    uint slice_num = pwm_gpio_to_slice_num(PIN_6502_PHI2);

    // Calculate the wrap value for the target frequency.
    // Assuming divider = 1.0: wrap = (f_sys / f_pwm) - 1
    uint32_t f_sys = clock_get_hz(clk_sys);
    uint32_t wrap = (f_sys / CLK_SPEED_6502) - 1;
    assert(wrap <= 0xffff);  // Wrap is 16 bits, so max PWM frequency is f_sys / 65536

    // Configure the PWM slice
    pwm_config config = pwm_get_default_config();
    pwm_config_set_clkdiv(&config, 1.0f); // Fixed 1.0 divider for zero jitter
    pwm_config_set_wrap(&config, (uint16_t)wrap);
    pwm_init(slice_num, &config, /*start=*/false);

    // Set a 50% duty cycle
    uint32_t level = (wrap + 1) / 2;
    pwm_set_gpio_level(PIN_6502_PHI2, level);

    pwm_set_enabled(slice_num, true);
}

// ============================================================================
// RESB management
// ============================================================================

static void resb_fall_callback(uint gpio, uint32_t events) {
    (void)events;
    if (gpio == PIN_6502_RESB) {
        reset_requested = true;
    }
}

static void bridge_reset(void) {
    printf("Reset requested.\n");

    // Drive RESB low (may already be low if external reset).
    // Disable the falling-edge interrupt so our own drive doesn't re-trigger.
    gpio_set_irq_enabled(PIN_6502_RESB, GPIO_IRQ_EDGE_FALL, false);
    gpio_set_dir(PIN_6502_RESB, GPIO_OUT);  // Drive low

    // Notify Zero via SPI (SPI is still running).
    uint8_t reset_msg[] = { 0x01, 0x01, 'R' };  // Device 1 (system), len 1, 'R'
    spi_slave_tx_queue(reset_msg, sizeof(reset_msg));

    // Keep running the SPI slave task until the Zero has read the
    // notification (TX queue drains), or timeout after 1s.
    uint32_t deadline = to_ms_since_boot(get_absolute_time()) + 1000;
    while (spi_slave_tx_queue_len() > 0) {
        spi_slave_task();
        if (to_ms_since_boot(get_absolute_time()) > deadline) {
            printf("Reset: Zero did not read notification (timeout).\n");
            break;
        }
    }

    // Hold RESB low long enough to discharge the external RC cap.
    // With R=100k, C=2.2uF (RC=220ms), 500ms gives >2 time constants,
    // ensuring the cap voltage is below any CMOS threshold.
    // This guarantees the RC circuit will hold RESB low during the
    // Pico's reboot (~50ms).
    printf("Reset: discharging RC cap...\n");
    sleep_ms(500);

    // Reboot via watchdog.
    // GPIO pins go to input/high-Z during boot. The RC circuit
    // holds RESB low. On reboot, main() drives RESB low again,
    // initializes everything, then releases RESB.
    printf("Reset: rebooting.\n");
    watchdog_reboot(0, 0, 0);

    // Should not reach here
    while (1) tight_loop_contents();
}

// ============================================================================
// Main
// ============================================================================

int main(void) {
    stdio_init_all();

    // --- RESB: drive low immediately to hold 6502 in reset ---
    gpio_init(PIN_6502_RESB);
    gpio_put(PIN_6502_RESB, 0);            // Latch low
    gpio_set_dir(PIN_6502_RESB, GPIO_OUT);  // Drive low = assert RESB

    // --- 6502 PHI2 clock ---
    setup_6502_clock();

    // --- 6502 IRQ pin (active-low, tristated on boot) ---
    gpio_init(PIN_6502_IRQ);
    gpio_put(PIN_6502_IRQ, 0);  // Latch low so asserting only needs dir change
    gpio_set_dir(PIN_6502_IRQ, GPIO_IN);  // Tristate until data arrives

    // --- Bus interface ---
    if (!bus_init()) {
        printf("ERROR: bus_init failed\n");
        return 1;
    }

    // Device 0: local status register (reads handled by TX callback)
    bus_register_tx_callback(0, device0_tx_callback);

    // Device 1: system control (handled locally)
    bus_register_rx_callback(1, device1_rx_callback);

    // Devices 2-7: forward to/from SPI
    for (uint8_t d = 2; d < BUS_MAX_DEVICES; d++) {
        bus_register_rx_callback(d, bus_to_spi_callback);
    }

    bus_start();

    // --- SPI slave ---
    if (!spi_slave_init()) {
        printf("ERROR: spi_slave_init failed\n");
        return 1;
    }
    spi_slave_set_rx_callback(spi_rx_callback);

    // --- Release RESB: 6502 can now start its reset sequence ---
    gpio_set_dir(PIN_6502_RESB, GPIO_IN);  // Tristate = release (external pull-up)

    // --- RESB falling-edge interrupt for external resets ---
    gpio_set_irq_enabled_with_callback(PIN_6502_RESB, GPIO_IRQ_EDGE_FALL, true,
                                       resb_fall_callback);

    uint32_t boot_time = to_ms_since_boot(get_absolute_time());
    uint32_t last_stats = boot_time;

    while (1) {
        if (reset_requested) {
            bridge_reset();
            // Not reached — watchdog reboots
        }

        bus_task();
        spi_slave_task();
        // Disabled - 6502 doesn't have a good IRQ handler yet.
        // update_6502_irq();

        // Periodic stats
        uint32_t now = to_ms_since_boot(get_absolute_time());

        // Print startup banner once USB is ready (after STARTUP_DELAY_MS)
        if (!startup_banner_printed && now - boot_time >= STARTUP_DELAY_MS) {
            startup_banner_printed = true;
            printf("\n6502 <-> Zero SPI Bridge%s\n",
                   watchdog_caused_reboot() ? " (after reset)" : "");
            printf("  6502 bus:  GPIO 0-2 (ctrl), 6-13 (data)\n");
            printf("  SPI:       GPIO 16-19 (SPI0), 20 (IRQ), 21 (READY)\n");
            printf("  6502 IRQ:  GPIO %d\n", PIN_6502_IRQ);
            printf("  6502 RESB: GPIO %d\n\n", PIN_6502_RESB);
        }

        if (now - last_stats >= STATS_INTERVAL_MS) {
            bus_stats_t bs = bus_get_stats();
            spi_slave_stats_t ss = spi_slave_get_stats();

            printf("[%lus] 6502->Z: %lu msgs (%lu B) | Z->6502: %lu msgs (%lu B, %lu drops)\n",
                   (unsigned long)(now / 1000),
                   (unsigned long)bus_to_spi_msgs,
                   (unsigned long)bus_to_spi_bytes,
                   (unsigned long)spi_to_bus_msgs,
                   (unsigned long)spi_to_bus_bytes,
                   (unsigned long)spi_to_bus_drops);

            printf("       bus: rx=%lu tx=%lu overruns=%lu bankrupt=%lu underflows=%lu\n",
                   (unsigned long)bs.rx_bytes,
                   (unsigned long)bs.tx_bytes,
                   (unsigned long)bs.rx_dma_overruns,
                   (unsigned long)bs.rx_bankruptcies,
                   (unsigned long)bs.tx_underflows);

            printf("       spi: wr=%lu rd=%lu req=%lu proto_err=%lu\n",
                   (unsigned long)ss.rx_writes,
                   (unsigned long)ss.tx_reads,
                   (unsigned long)ss.requests,
                   (unsigned long)ss.proto_errors);

#if BRIDGE_DEBUG
            bus_diagnose();
#endif

            last_stats = now;
        }
    }
}
