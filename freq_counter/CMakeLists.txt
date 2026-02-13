cmake_minimum_required(VERSION 3.13)

# Set path to Pico SDK (submodule at repository root)
set(PICO_SDK_PATH ${CMAKE_CURRENT_LIST_DIR}/../pico-sdk)

# Pull in SDK (must be before project)
include(pico_sdk_import.cmake)

# Set project name
project(freq_counter C CXX ASM)

# Set C/C++ standards
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Check SDK version
if (PICO_SDK_VERSION_STRING VERSION_LESS "2.2.0")
    message(FATAL_ERROR "Raspberry Pi Pico SDK version 2.2.0 (or later) required. Your version is ${PICO_SDK_VERSION_STRING}")
endif()

# Initialize the SDK
pico_sdk_init()

# Create the executable
add_executable(freq_counter
    main.c
)

# Link libraries
target_link_libraries(freq_counter PUBLIC
    pico_stdlib
    hardware_pwm
    hardware_gpio
)

# Create map/bin/hex/uf2 files
pico_add_extra_outputs(freq_counter)

# Enable stdio over USB for output
pico_enable_stdio_uart(freq_counter 0)
pico_enable_stdio_usb(freq_counter 1)
