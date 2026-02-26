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
 */

#include <stdio.h>
#include <string.h>

#include "hardware/pwm.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"
#include "bus_interface.h"
#include "spi_slave.h"

// 6502 PHI2 clock output pin (PWM)
#define PIN_6502_PHI2 2
// 6502 IRQ pin (active-low output)
#define PIN_6502_IRQ 3

// Target clock frequency for the 6502.
#define CLK_SPEED_6502 1000000  // 1 MHz target clock frequency for 6502

// Stats
static uint32_t bus_to_spi_msgs = 0;
static uint32_t bus_to_spi_bytes = 0;
static uint32_t spi_to_bus_msgs = 0;
static uint32_t spi_to_bus_bytes = 0;
static uint32_t spi_to_bus_drops = 0;

// ============================================================================
// 6502 -> Zero: bus RX callback forwards to SPI TX queue
// ============================================================================

static void bus_to_spi_callback(uint8_t device, const uint8_t *data, uint16_t len) {
    // Bus transfers are max 255 bytes, so len fits in uint8_t
    uint8_t header[2] = { device, (uint8_t)len };

    // Queue header + payload (both calls are in main-loop context, no preemption)
    if (!spi_slave_tx_queue(header, 2) || !spi_slave_tx_queue(data, len)) {
        // TX queue full -- data lost
        return;
    }

    bus_to_spi_msgs++;
    bus_to_spi_bytes += len;
}

// ============================================================================
// Zero -> 6502: SPI RX callback parses TLV and writes to bus device buffers
// ============================================================================

static void spi_rx_callback(const uint8_t *data, uint16_t len) {
    uint16_t pos = 0;
    while (pos + 2 <= len) {
        uint8_t device = data[pos];
        uint8_t tlv_len = data[pos + 1];
        if (pos + 2 + tlv_len > len) break;
        if (device < BUS_MAX_DEVICES && tlv_len > 0) {
            uint16_t written = bus_device_write(device, &data[pos + 2], tlv_len);
            if (written < tlv_len) {
                spi_to_bus_drops++;
            }
            spi_to_bus_msgs++;
            spi_to_bus_bytes += tlv_len;
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
    for (uint8_t i = 0; i < BUS_MAX_DEVICES; i++) {
        if (bus_device_tx_count(i) > 0) {
            any_data = true;
            break;
        }
    }

    if (any_data && !irq_6502_asserted) {
        gpio_put(PIN_6502_IRQ, 0);  // Active low
        irq_6502_asserted = true;
    } else if (!any_data && irq_6502_asserted) {
        gpio_put(PIN_6502_IRQ, 1);  // Idle high
        irq_6502_asserted = false;
    }
}

// ============================================================================
// 6502 Clock management
// ============================================================================

void setup_6502_clock() {
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
    printf("System clock: %lu Hz, PWM wrap: %lu, level: %lu\n", (unsigned long)f_sys, (unsigned long)wrap, (unsigned long)level);

    pwm_set_enabled(slice_num, true);
}

// ============================================================================
// Main
// ============================================================================

int main(void) {
    stdio_init_all();
    sleep_ms(2000);

    printf("\n6502 <-> Zero SPI Bridge\n");
    printf("  6502 bus: GPIO 0-2 (ctrl), 6-13 (data)\n");
    printf("  SPI:      GPIO 16-19 (SPI0), 20 (IRQ), 21 (READY)\n");
    printf("  6502 IRQ: GPIO %d\n\n", PIN_6502_IRQ);

    // --- 6502 PHI2 clock ---
    setup_6502_clock();

    // --- 6502 IRQ pin (set value BEFORE direction to avoid glitch) ---
    gpio_init(PIN_6502_IRQ);
    gpio_put(PIN_6502_IRQ, 1);  // Latch high before enabling output
    gpio_set_dir(PIN_6502_IRQ, GPIO_OUT);

    // --- Bus interface ---
    if (!bus_init()) {
        printf("ERROR: bus_init failed\n");
        return 1;
    }

    for (uint8_t d = 0; d < BUS_MAX_DEVICES; d++) {
        bus_register_rx_callback(d, bus_to_spi_callback);
    }

    bus_start();

    // --- SPI slave ---
    if (!spi_slave_init()) {
        printf("ERROR: spi_slave_init failed\n");
        return 1;
    }
    spi_slave_set_rx_callback(spi_rx_callback);

    printf("Ready.\n\n");

    uint32_t last_stats = to_ms_since_boot(get_absolute_time());

    while (1) {
        bus_task();
        spi_slave_task();
        update_6502_irq();

        // Periodic stats
        uint32_t now = to_ms_since_boot(get_absolute_time());
        if (now - last_stats >= 5000) {
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

            last_stats = now;
        }
    }
}
