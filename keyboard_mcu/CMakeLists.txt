cmake_minimum_required(VERSION 3.13)

# Pull in SDK (must be before project)
include(pico_sdk_import.cmake)

# Set project name - you can change this to your preferred name
project(keyboard_mcu C CXX ASM)

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
add_executable(host_cdc_msc_hid)

# Example source
target_sources(host_cdc_msc_hid PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}/hid_app.c
        ${CMAKE_CURRENT_LIST_DIR}/main.c
        ${CMAKE_CURRENT_LIST_DIR}/via_interface.c
        )

# Make sure TinyUSB can find tusb_config.h
target_include_directories(host_cdc_msc_hid PUBLIC
        ${CMAKE_CURRENT_LIST_DIR})

# In addition to pico_stdlib required for common PicoSDK functionality, add dependency on tinyusb_host
# for TinyUSB device support and tinyusb_board for the additional board support library used by the example
target_link_libraries(host_cdc_msc_hid PUBLIC pico_status_led pico_stdlib hardware_gpio hardware_uart tinyusb_host tinyusb_board)

# Create map/bin/hex/uf2 files
pico_add_extra_outputs(host_cdc_msc_hid)

# Disable stdio over USB, since we're going to use it for the keyboard
pico_enable_stdio_uart(host_cdc_msc_hid 1)
pico_enable_stdio_usb(host_cdc_msc_hid 0)

