#include <stdint.h>
#include "mattbrew.h"

#define IO_PORT (*(volatile uint8_t *)RPI_BASE)

extern volatile uint8_t io_busy;

// Core read — returns bytes read (0 = no data)
uint8_t io_read(uint8_t device_id, uint8_t *buf) {
    ++io_busy;
    IO_PORT = device_id | 0x80;

    uint8_t len;
    while ((len = IO_PORT) == 0xFF);

    for (uint8_t i = 0; i < len; i++) {
        buf[i] = IO_PORT;
    }
    --io_busy;
    return len;
}

// Core write
void io_write(uint8_t device_id, const uint8_t *buf, uint8_t len) {
    IO_PORT = device_id;
    IO_PORT = len;
    for (uint8_t i = 0; i < len; i++) {
        IO_PORT = buf[i];
    }
}

// Convenience: write a single byte
void io_write1(uint8_t device_id, uint8_t data) {
    IO_PORT = device_id;
    IO_PORT = 1;
    IO_PORT = data;
}