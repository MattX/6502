##############################################################################
## Constraints file for 6502-MCU Interface on Digilent Cmod A7
##
## Adjust pin assignments based on your wiring. The Cmod A7 has:
##   - 48 digital I/O pins (active I/O depends on DIP row position)
##   - Active-low buttons on pins BTN0, BTN1
##   - Active-high RGB LED, active-low LEDs
##
## IMPORTANT: Ensure all I/O pins are 3.3V compatible.
## The 65C02 and 74HC574 at 3.3V should be fine.
##
## Cmod A7 Pinout reference:
##   https://digilent.com/reference/programmable-logic/cmod-a7/reference-manual
##############################################################################

## Clock - not used by this design (purely async), but required for Vivado
## The Cmod A7 has a 12 MHz oscillator on pin L17
#set_property -dict { PACKAGE_PIN L17 IOSTANDARD LVCMOS33 } [get_ports { clk }]

##############################################################################
## 6502 Bus Inputs
##############################################################################

## PHI2 - 6502 system clock (directly from CPU, up to 5MHz)
set_property -dict { PACKAGE_PIN A17 IOSTANDARD LVCMOS33 } [get_ports { PHI2 }]

## CS_N - Chip select from address decoder (directly active low)
set_property -dict { PACKAGE_PIN B16 IOSTANDARD LVCMOS33 } [get_ports { CS_N }]

## RW - 6502 Read/Write signal
set_property -dict { PACKAGE_PIN B17 IOSTANDARD LVCMOS33 } [get_ports { RW }]

## A0 - Register select bit
set_property -dict { PACKAGE_PIN C15 IOSTANDARD LVCMOS33 } [get_ports { A0 }]

##############################################################################
## MCU Interface Inputs
##############################################################################

## TX_LOAD - MCU pulses to load TX latch
set_property -dict { PACKAGE_PIN C16 IOSTANDARD LVCMOS33 } [get_ports { TX_LOAD }]

## RX_ACK - MCU pulses to acknowledge RX data
set_property -dict { PACKAGE_PIN D15 IOSTANDARD LVCMOS33 } [get_ports { RX_ACK }]

##############################################################################
## Latch Control Outputs
##############################################################################

## TX_OE_N - Enable TX latch onto 6502 bus (directly active low)
set_property -dict { PACKAGE_PIN D16 IOSTANDARD LVCMOS33 } [get_ports { TX_OE_N }]

## RX_CLK - Clock CPU data into RX latch
set_property -dict { PACKAGE_PIN E15 IOSTANDARD LVCMOS33 } [get_ports { RX_CLK }]

## STATUS_OE_N - Enable status latch onto 6502 bus (directly active low)
set_property -dict { PACKAGE_PIN E16 IOSTANDARD LVCMOS33 } [get_ports { STATUS_OE_N }]

## STATUS_CLK - Clock status bits into status latch
set_property -dict { PACKAGE_PIN F15 IOSTANDARD LVCMOS33 } [get_ports { STATUS_CLK }]

##############################################################################
## Handshake Status Outputs
##############################################################################

## TX_AVAIL - Status bit to status latch D7
set_property -dict { PACKAGE_PIN G15 IOSTANDARD LVCMOS33 } [get_ports { TX_AVAIL }]

## RX_READY - Status bit to status latch D6
set_property -dict { PACKAGE_PIN H15 IOSTANDARD LVCMOS33 } [get_ports { RX_READY }]

## DATA_TAKEN - To MCU GPIO
set_property -dict { PACKAGE_PIN J15 IOSTANDARD LVCMOS33 } [get_ports { DATA_TAKEN }]

## DATA_WRITTEN - To MCU GPIO
set_property -dict { PACKAGE_PIN K15 IOSTANDARD LVCMOS33 } [get_ports { DATA_WRITTEN }]

##############################################################################
## Timing Constraints
##############################################################################

## This is a purely combinatorial/async design, but Vivado may want clocks defined.
## PHI2 is used as an async signal, not a synchronous clock, so we don't create
## a clock constraint for it. If Vivado complains, uncomment:
# create_clock -name PHI2 -period 200.0 [get_ports PHI2]

## Mark all flip-flop clocks as async (they use async set/reset style)
set_property ASYNC_REG true [get_cells TX_AVAIL_reg]
set_property ASYNC_REG true [get_cells RX_READY_reg]
set_property ASYNC_REG true [get_cells DATA_TAKEN_reg]
set_property ASYNC_REG true [get_cells DATA_WRITTEN_reg]

##############################################################################
## Configuration
##############################################################################

set_property CFGBVS VCCO [current_design]
set_property CONFIG_VOLTAGE 3.3 [current_design]
set_property BITSTREAM.CONFIG.SPI_BUSWIDTH 4 [current_design]
