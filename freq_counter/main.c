/**
 * PWM Frequency Counter for Raspberry Pi Pico
 *
 * Measures the frequency of a PWM signal on a GPIO pin and outputs
 * the measured frequency to USB serial (stdout).
 *
 * Connect the PWM signal to GPIO 15 (or change PWM_INPUT_PIN below)
 */

#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include "hardware/gpio.h"
#include "hardware/clocks.h"

// Configuration
#define PWM_INPUT_PIN 15        // GPIO pin for PWM input
#define MEASUREMENT_INTERVAL_MS 100  // How often to report frequency (100ms for up to 1MHz)
#define MAX_SAFE_FREQUENCY 655000   // Maximum safe frequency with 16-bit counter

// PWM slice and channel for the input pin
static uint slice_num;
static uint channel;

// Counter values
static volatile uint32_t last_count = 0;

void setup_pwm_input() {
    // Get the PWM slice and channel for this GPIO
    slice_num = pwm_gpio_to_slice_num(PWM_INPUT_PIN);
    channel = pwm_gpio_to_channel(PWM_INPUT_PIN);

    // Configure the GPIO for PWM input
    gpio_set_function(PWM_INPUT_PIN, GPIO_FUNC_PWM);

    // Configure PWM slice for counting
    pwm_config config = pwm_get_default_config();

    // Set divider to 1 (count every rising edge)
    pwm_config_set_clkdiv(&config, 1.0f);

    // Set wrap value to maximum to avoid overflow
    pwm_config_set_wrap(&config, 0xFFFF);

    // Free-running counting mode
    pwm_init(slice_num, &config, true);

    // Enable PWM slice
    pwm_set_enabled(slice_num, true);
}

uint32_t read_counter() {
    return pwm_get_counter(slice_num);
}

int main() {
    // Initialize stdio for USB output
    stdio_init_all();

    // Wait for USB connection (timeout after 5 seconds)
    absolute_time_t timeout = make_timeout_time_ms(5000);
    while (!stdio_usb_connected() && !time_reached(timeout)) {
        sleep_ms(100);
    }

    printf("\n=== PWM Frequency Counter ===\n");
    printf("Measuring frequency on GPIO %d\n", PWM_INPUT_PIN);
    printf("Measurement interval: %d ms\n", MEASUREMENT_INTERVAL_MS);
    printf("Max measurable frequency: ~%.0f kHz\n\n", MAX_SAFE_FREQUENCY / 1000.0f);

    // Setup PWM input
    setup_pwm_input();

    // Get initial counter value
    last_count = read_counter();

    while (true) {
        // Wait for measurement interval
        sleep_ms(MEASUREMENT_INTERVAL_MS);

        // Read current counter value
        uint32_t current_count = read_counter();

        // Calculate difference (accounting for wrap-around)
        uint32_t count_diff;
        if (current_count >= last_count) {
            count_diff = current_count - last_count;
        } else {
            // Counter wrapped around once
            count_diff = (0xFFFF - last_count) + current_count + 1;
        }

        // Calculate frequency (counts per second)
        float frequency = (float)count_diff * (1000.0f / MEASUREMENT_INTERVAL_MS);

        // Check if we might be exceeding safe limits
        if (count_diff > 0xFFFF) {
            printf("WARNING: Multiple wraps detected! Frequency may be inaccurate.\n");
        }

        // Print result with appropriate units
        if (frequency >= 1000000.0f) {
            printf("Frequency: %.3f MHz (count: %lu)\n", frequency / 1000000.0f, count_diff);
        } else if (frequency >= 1000.0f) {
            printf("Frequency: %.3f kHz (count: %lu)\n", frequency / 1000.0f, count_diff);
        } else {
            printf("Frequency: %.2f Hz (count: %lu)\n", frequency, count_diff);
        }

        // Update last count
        last_count = current_count;
    }

    return 0;
}
