#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"

#define RW_PIN 0
#define CS_N_PIN 1
#define PHI2_PIN 2

int main() {
    stdio_init_all();
    sleep_ms(2000);  // let USB settle

    gpio_init(RW_PIN);
    gpio_init(CS_N_PIN);
    gpio_init(PHI2_PIN);
    gpio_set_dir(RW_PIN, GPIO_IN);
    gpio_set_dir(CS_N_PIN, GPIO_IN);
    gpio_set_dir(PHI2_PIN, GPIO_IN);

    printf("Watching GPIO%d for transitions...\n", PHI2_PIN);

    bool last = gpio_get(PHI2_PIN);
    uint32_t count = 0;

    while (1) {
        bool now = gpio_get(PHI2_PIN);
        if (now != last) {
            count++;
            // only print every 1000 transitions so we don't flood UART
            if ((count % 1000) == 0) {
                printf("transitions: %lu (now=%d)\n", count, now);
                printf("rw: %d, cs_n: %d\n", gpio_get(RW_PIN), gpio_get(CS_N_PIN));
            }
            last = now;
        }
    }
}
