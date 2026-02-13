# PWM Frequency Counter

A Raspberry Pi Pico program that measures the frequency of a digital clock signal and outputs the measurement to USB serial.

## Features

- Measures PWM/clock frequencies up to ~650 kHz using hardware PWM counter
- Optimized 100ms measurement interval for frequencies up to 1 MHz
- Outputs measurements to USB serial (stdout)
- Automatically displays results in Hz, kHz, or MHz
- Handles counter wrap-around automatically
- Warns if frequency exceeds safe measurement range

## Hardware Setup

Connect your clock signal to GPIO 15 (or modify `PWM_INPUT_PIN` in main.c).

## Building

```bash
mkdir build
cd build
cmake ..
make
```

Flash the resulting `freq_counter.uf2` file to your Pico.

## Usage

1. Connect to the Pico's USB serial port (e.g., using `screen`, `minicom`, or any serial terminal)
2. Connect your clock signal to GPIO 15
3. The frequency will be displayed every 100ms in appropriate units (Hz/kHz/MHz)

## Configuration

Edit `main.c` to change:
- `PWM_INPUT_PIN`: GPIO pin for input (default: GPIO 15)
- `MEASUREMENT_INTERVAL_MS`: How often to report frequency (default: 100ms for up to 1 MHz support)

## Frequency Range

- **Typical range**: 1 Hz to 650 kHz
- **With single wrap handling**: Up to ~1 MHz (100ms interval)
- **Lower frequencies**: Works fine, just less frequent updates

For frequencies above 650 kHz, the program will warn if multiple counter wraps are detected.
