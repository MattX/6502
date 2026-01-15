cmake_minimum_required(VERSION 3.13)

# Set path to Pico SDK (submodule at repository root)
set(PICO_SDK_PATH ${CMAKE_CURRENT_LIST_DIR}/../pico-sdk)

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
    hardware_uart
)

# Create map/bin/hex/uf2 files
pico_add_extra_outputs(pio_bus_interface)

# Enable UART stdio, disable USB stdio (to avoid conflicts)
pico_enable_stdio_uart(pio_bus_interface 0)  # Disabled - we set up UART manually
pico_enable_stdio_usb(pio_bus_interface 1)   # Enable USB for debugging
