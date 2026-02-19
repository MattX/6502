cmake_minimum_required(VERSION 3.13)

# Set path to Pico SDK (submodule at repository root)
set(PICO_SDK_PATH ${CMAKE_CURRENT_LIST_DIR}/../pico-sdk)

# Target RP2350 (Pico 2)
set(PICO_BOARD pico2)
set(PICO_PLATFORM rp2350)
set(PICO_TOOLCHAIN_PATH /opt/homebrew/Cellar/arm-gcc-bin@10/10.3-2021.10_1/)

# Pull in SDK (must be before project)
include(pico_sdk_import.cmake)

project(bridge C CXX ASM)

# Set C/C++ standards
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Check SDK version
if (PICO_SDK_VERSION_STRING VERSION_LESS "2.0.0")
    message(FATAL_ERROR "Raspberry Pi Pico SDK version 2.0.0 (or later) required. Your version is ${PICO_SDK_VERSION_STRING}")
endif()

# Initialize the SDK
pico_sdk_init()

# 6502 <-> Zero SPI Bridge
add_executable(bridge
    main.c
    bus_interface.c
    spi_slave.c
)

# Generate PIO header from .pio file
pico_generate_pio_header(bridge ${CMAKE_CURRENT_LIST_DIR}/bus_interface.pio)

# Include directories
target_include_directories(bridge PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}
)

# Link libraries
target_link_libraries(bridge PUBLIC
    pico_stdlib
    hardware_pio
    hardware_dma
    hardware_gpio
    hardware_spi
    hardware_irq
)

# Create map/bin/hex/uf2 files
pico_add_extra_outputs(bridge)

# Enable USB stdio only
pico_enable_stdio_uart(bridge 0)
pico_enable_stdio_usb(bridge 1)
