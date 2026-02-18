cmake_minimum_required(VERSION 3.13)

# Set path to Pico SDK (submodule at repository root)
set(PICO_SDK_PATH ${CMAKE_CURRENT_LIST_DIR}/../../pico-sdk)

# Target RP2350 (Pico 2)
set(PICO_BOARD pico2)
set(PICO_PLATFORM rp2350)
set(PICO_TOOLCHAIN_PATH /opt/homebrew/Cellar/arm-gcc-bin@10/10.3-2021.10_1/)

# Pull in SDK (must be before project)
include(pico_sdk_import.cmake)

project(pico_zero_spi C CXX ASM)

# Set C/C++ standards
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Check SDK version
if (PICO_SDK_VERSION_STRING VERSION_LESS "2.0.0")
    message(FATAL_ERROR "Raspberry Pi Pico SDK version 2.0.0 (or later) required. Your version is ${PICO_SDK_VERSION_STRING}")
endif()

# Initialize the SDK
pico_sdk_init()

# SPI slave test
add_executable(spi_slave_test
    main.c
    spi_slave.c
)

target_include_directories(spi_slave_test PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(spi_slave_test PUBLIC
    pico_stdlib
    hardware_spi
    hardware_dma
    hardware_gpio
    hardware_irq
)

pico_add_extra_outputs(spi_slave_test)

# Enable USB stdio only
pico_enable_stdio_uart(spi_slave_test 0)
pico_enable_stdio_usb(spi_slave_test 1)
