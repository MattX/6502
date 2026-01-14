/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 Ha Thach (tinyusb.org)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/status_led.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"
#include "bsp/board_api.h"
#include "tusb.h"
#include "via_interface.h"

// UART Console Configuration
#define UART_ID uart0
#define UART_BAUD_RATE 115200
#define UART_TX_PIN 0
#define UART_RX_PIN 1

//--------------------------------------------------------------------+
// MACRO CONSTANT TYPEDEF PROTYPES
//--------------------------------------------------------------------+

// LED Colors
#define LED_COLOR_RED     PICO_COLORED_STATUS_LED_COLOR_FROM_RGB(255, 0, 0)
#define LED_COLOR_GREEN   PICO_COLORED_STATUS_LED_COLOR_FROM_RGB(0, 255, 0)
#define LED_COLOR_BLUE    PICO_COLORED_STATUS_LED_COLOR_FROM_RGB(0, 0, 255)
#define LED_COLOR_YELLOW  PICO_COLORED_STATUS_LED_COLOR_FROM_RGB(255, 255, 0)
#define LED_COLOR_CYAN    PICO_COLORED_STATUS_LED_COLOR_FROM_RGB(0, 255, 255)
#define LED_COLOR_MAGENTA PICO_COLORED_STATUS_LED_COLOR_FROM_RGB(255, 0, 255)
#define LED_COLOR_WHITE   PICO_COLORED_STATUS_LED_COLOR_FROM_RGB(255, 255, 255)
#define LED_COLOR_OFF     PICO_COLORED_STATUS_LED_COLOR_FROM_RGB(0, 0, 0)

// State tracking
static volatile bool keyboard_mounted = false;
static volatile bool keystroke_active = false;
static volatile uint32_t keystroke_time = 0;

void led_blinking_task(void);
void set_keyboard_mounted(bool mounted);
void signal_keystroke(void);
extern void hid_app_task(void);

#if CFG_TUH_ENABLED && CFG_TUH_MAX3421
// API to read/rite MAX3421's register. Implemented by TinyUSB
extern uint8_t tuh_max3421_reg_read(uint8_t rhport, uint8_t reg, bool in_isr);
extern bool tuh_max3421_reg_write(uint8_t rhport, uint8_t reg, uint8_t data, bool in_isr);
#endif

/*------------- MAIN -------------*/
int main(void) {
  board_init();

  // Initialize UART for console output on GPIO0 (TX) and GPIO1 (RX)
  uart_init(UART_ID, UART_BAUD_RATE);
  gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
  gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);

  printf("\r\n");
  printf("======================================\r\n");
  printf("  6502 Keyboard MCU - RP2040\r\n");
  printf("  UART Console on GPIO0/GPIO1\r\n");
  printf("  Baud: %d\r\n", UART_BAUD_RATE);
  printf("======================================\r\n");
  printf("TinyUSB Host HID Keyboard Example\r\n");

  // Initialize status LED - MUST be called before using LED functions
  status_led_init();

  // Initialize VIA interface for 6522 communication
  via_init();

  // init host stack on configured roothub port
  tuh_init(BOARD_TUH_RHPORT);

  if (board_init_after_tusb) {
    board_init_after_tusb();
  }

#if CFG_TUH_ENABLED && CFG_TUH_MAX3421
  // FeatherWing MAX3421E use MAX3421E's GPIO0 for VBUS enable
  enum { IOPINS1_ADDR  = 20u << 3, /* 0xA0 */ };
  tuh_max3421_reg_write(BOARD_TUH_RHPORT, IOPINS1_ADDR, 0x01, false);
#endif

  // Now entering main loop - LED will be controlled by led_blinking_task
  while (1) {
    // tinyusb host task
    tuh_task();

    // VIA handshaking task
    via_task();

    led_blinking_task();
    hid_app_task();
  }
}

//--------------------------------------------------------------------+
// State Management
//--------------------------------------------------------------------+

void set_keyboard_mounted(bool mounted) {
  keyboard_mounted = mounted;
}

void signal_keystroke(void) {
  keystroke_active = true;
  keystroke_time = board_millis();
}

//--------------------------------------------------------------------+
// TinyUSB Callbacks
//--------------------------------------------------------------------+

void tuh_mount_cb(uint8_t dev_addr) {
  // application set-up
  printf("A device with address %d is mounted\r\n", dev_addr);
}

void tuh_umount_cb(uint8_t dev_addr) {
  // application tear-down
  printf("A device with address %d is unmounted \r\n", dev_addr);
}


//--------------------------------------------------------------------+
// Blinking Task
//--------------------------------------------------------------------+
void led_blinking_task(void) {
  static uint32_t last_update_ms = 0;
  static bool blink_state = false;
  static uint8_t last_mode = 0; // 0=none, 1=keystroke, 2=mounted, 3=no keyboard
  static uint32_t current_led_color = 0; // Track what color is currently set
  uint32_t current_ms = board_millis();

  // Check if keystroke activity has expired (200ms timeout for better visibility)
  if (keystroke_active && (current_ms - keystroke_time > 200)) {
    keystroke_active = false;
  }

  // Determine current mode
  uint8_t current_mode = keystroke_active ? 1 : (keyboard_mounted ? 2 : 3);

  // Force update if mode changed
  if (current_mode != last_mode) {
    last_update_ms = current_ms;
    blink_state = false;
    last_mode = current_mode;
    current_led_color = 0; // Force color update
  }

  // Update LED state based on conditions
  if (keystroke_active) {
    // Solid blue when processing keystrokes
    if (current_led_color != LED_COLOR_BLUE) {
      colored_status_led_set_state(false);
      sleep_us(100); // 100us delay needed for Neopixel (see pico-sdk #2630)
      colored_status_led_set_on_with_color(LED_COLOR_BLUE);
      current_led_color = LED_COLOR_BLUE;
    }
  } else if (keyboard_mounted) {
    // Solid green when keyboard is connected
    if (current_led_color != LED_COLOR_GREEN) {
      colored_status_led_set_state(false);
      sleep_us(100); // 100us delay needed for Neopixel (see pico-sdk #2630)
      colored_status_led_set_on_with_color(LED_COLOR_GREEN);
      current_led_color = LED_COLOR_GREEN;
    }
  } else {
    // Blink red when no keyboard detected (500ms interval)
    if (current_ms - last_update_ms >= 500) {
      blink_state = !blink_state;
      colored_status_led_set_state(false);
      sleep_us(100); // 100us delay needed for Neopixel (see pico-sdk #2630)
      if (blink_state) {
        colored_status_led_set_on_with_color(LED_COLOR_RED);
        current_led_color = LED_COLOR_RED;
      } else {
        current_led_color = 0; // Off
      }
      last_update_ms = current_ms;
    }
  }
}
