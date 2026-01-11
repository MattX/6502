# Keyboard interface for RPi Pico series

Originally ported from https://github.com/raspberrypi/pico-examples/tree/4c3a3dc0196dd426fddd709616d0da984e027bab/usb/host/host_cdc_msc_hid

Build with

```
cmake --fresh -DPICO_BOARD=adafruit_feather_rp2040 -DPICO_SDK_PATH=/Users/matthieu/Code/pico-sdk -DPICO_TOOLCHAIN_PATH=/opt/homebrew/Cellar/arm-gcc-bin@10/10.3-2021.10_1/ -S . -B build
cmake --build build
```

