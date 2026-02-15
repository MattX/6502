cmake_minimum_required(VERSION 3.13)

# Set path to Pico SDK (submodule at repository root)
set(PICO_SDK_PATH ${CMAKE_CURRENT_LIST_DIR}/../pico-sdk)

# Target RP2350 (Pico 2)
set(PICO_BOARD pico2)
set(PICO_PLATFORM rp2350)
set(PICO_TOOLCHAIN_PATH /opt/homebrew/Cellar/arm-gcc-bin@10/10.3-2021.10_1/)

# Pull in SDK (must be before project)
include(pico_sdk_import.cmake)

project(pio_bus_interface C CXX ASM)

# Set C/C++ standards
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Check SDK version
if (PICO_SDK_VERSION_STRING VERSION_LESS "2.0.0")
    message(FATAL_ERROR "Raspberry Pi Pico SDK version 2.0.0 (or later) required. Your version is ${PICO_SDK_VERSION_STRING}")
endif()

# Initialize the SDK
pico_sdk_init()

# Create the executable
add_executable(pio_bus_interface
    main.c
    bus_interface.c
)

# Generate PIO header from .pio file
pico_generate_pio_header(pio_bus_interface ${CMAKE_CURRENT_LIST_DIR}/bus_interface.pio)

# Include directories
target_include_directories(pio_bus_interface PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}
)

# Link libraries
target_link_libraries(pio_bus_interface PUBLIC
    pico_stdlib
    hardware_pio
    hardware_dma
    hardware_gpio
)

# Create map/bin/hex/uf2 files
pico_add_extra_outputs(pio_bus_interface)

# Enable USB stdio only
pico_enable_stdio_uart(pio_bus_interface 0)
pico_enable_stdio_usb(pio_bus_interface 1)

# ============================================================================
# RX-Only DMA + Parser Test Version
# MCU never drives the bus - exercises DMA + protocol parser
# ============================================================================

add_executable(pio_bus_interface_rx_only_dma
    main_rx_only_dma.c
    bus_interface_rx_only.c
)

pico_generate_pio_header(pio_bus_interface_rx_only_dma
    ${CMAKE_CURRENT_LIST_DIR}/bus_interface_rx_only.pio)

target_include_directories(pio_bus_interface_rx_only_dma PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(pio_bus_interface_rx_only_dma PUBLIC
    pico_stdlib
    hardware_pio
    hardware_dma
    hardware_gpio
)

pico_add_extra_outputs(pio_bus_interface_rx_only_dma)

# Enable USB stdio only
pico_enable_stdio_uart(pio_bus_interface_rx_only_dma 0)
pico_enable_stdio_usb(pio_bus_interface_rx_only_dma 1)

# Phi2 tester

add_executable(minimal_phi2_test minimal_phi2_test.c)
target_include_directories(minimal_phi2_test PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(minimal_phi2_test PUBLIC
    pico_stdlib
)

pico_add_extra_outputs(minimal_phi2_test)

# Enable USB stdio only
pico_enable_stdio_uart(minimal_phi2_test 0)
pico_enable_stdio_usb(minimal_phi2_test 1)
