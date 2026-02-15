# Mattbrew 6502

A 6502 system with Internet access and other niceties.

This project tries to strike a middle ground between something like the very impressive [RP6502](https://github.com/picocomputer/rp6502) project, and modern Apple II / C64 style projects.

The current design is this:

* A 6502 at 3.3V with RAM, ROM, and VIA; this is the traditional old-school part.
* A Pi Pico communicating on the serial bus. The intent is for the rp2350 to act as a relatively dumb parallel bus to SPI bridge, to allow the 6502 to talk to the Pi Zero.
* A Pi Zero providing HDMI, USB, and networking, talking to the Pi Pico over SPI.

There are two big differences with RP6502:

* That project's Pi Pico takes on many more functions: extended RAM, self-modifying code for DMA, clock generation, etc. This project's Pi Pico is mostly to pass messages to and from other devices. In earlier times, DIP format parallel bus to I2C or SPI chips were availabe, but these are increasingly hard to find; the Pi Pico in this project should primarily be viewed as a replacement for those.
* Heavy-lift functions like video output and TLS network termination are performed on a Pi Zero. I know those can be done on a Pi Pico as well, but I don't think that challenge is particularly fun, so I have no problem ultimately having those done on a complete Linux system.

Either way, the goal is to view both the Pi Zero and Pi Pico as peripherals of the core 6502 system, and not the other way around.

The 6502<->Pico interface is documented and implemented in pio_bus_interface.
