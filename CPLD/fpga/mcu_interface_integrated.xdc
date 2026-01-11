##############################################################################
## Constraints file for 6502-MCU Interface (Integrated Version)
## Target: Digilent Cmod A7
##
## This version requires more I/O pins due to the two 8-bit data buses:
##   - 8 pins for 6502 data bus (directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly bidirectional)
##   - 8 pins for MCU data bus (bidirectional)
##   - 4 pins for 6502 control (PHI2, CS_N, RW, A0)
##   - 4 pins for MCU control (TX_LOAD, RX_ACK, MCU_OE_N, DATA_TAKEN, DATA_WRITTEN)
##   Total: 24 I/O pins
##
## The Cmod A7 has 44 digital I/O pins, so this fits easily.
##
## IMPORTANT: Adjust pin assignments based on your actual wiring!
##############################################################################

##############################################################################
## 6502 Data Bus (directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly Directly directly driven during CPU reads)
##############################################################################
set_property -dict { PACKAGE_PIN M3  IOSTANDARD LVCMOS33 } [get_ports { D[0] }]
set_property -dict { PACKAGE_PIN L3  IOSTANDARD LVCMOS33 } [get_ports { D[1] }]
set_property -dict { PACKAGE_PIN A16 IOSTANDARD LVCMOS33 } [get_ports { D[2] }]
set_property -dict { PACKAGE_PIN K3  IOSTANDARD LVCMOS33 } [get_ports { D[3] }]
set_property -dict { PACKAGE_PIN C15 IOSTANDARD LVCMOS33 } [get_ports { D[4] }]
set_property -dict { PACKAGE_PIN H1  IOSTANDARD LVCMOS33 } [get_ports { D[5] }]
set_property -dict { PACKAGE_PIN A15 IOSTANDARD LVCMOS33 } [get_ports { D[6] }]
set_property -dict { PACKAGE_PIN B15 IOSTANDARD LVCMOS33 } [get_ports { D[7] }]

##############################################################################
## 6502 Control Signals
##############################################################################
set_property -dict { PACKAGE_PIN A17 IOSTANDARD LVCMOS33 } [get_ports { PHI2 }]
set_property -dict { PACKAGE_PIN C16 IOSTANDARD LVCMOS33 } [get_ports { CS_N }]
set_property -dict { PACKAGE_PIN B17 IOSTANDARD LVCMOS33 } [get_ports { RW }]
set_property -dict { PACKAGE_PIN B16 IOSTANDARD LVCMOS33 } [get_ports { A0 }]

##############################################################################
## MCU Data Bus (directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly Directly driven when MCU reads RX)
##############################################################################
set_property -dict { PACKAGE_PIN C17 IOSTANDARD LVCMOS33 } [get_ports { MCU_D[0] }]
set_property -dict { PACKAGE_PIN D18 IOSTANDARD LVCMOS33 } [get_ports { MCU_D[1] }]
set_property -dict { PACKAGE_PIN E18 IOSTANDARD LVCMOS33 } [get_ports { MCU_D[2] }]
set_property -dict { PACKAGE_PIN G17 IOSTANDARD LVCMOS33 } [get_ports { MCU_D[3] }]
set_property -dict { PACKAGE_PIN D17 IOSTANDARD LVCMOS33 } [get_ports { MCU_D[4] }]
set_property -dict { PACKAGE_PIN E17 IOSTANDARD LVCMOS33 } [get_ports { MCU_D[5] }]
set_property -dict { PACKAGE_PIN F18 IOSTANDARD LVCMOS33 } [get_ports { MCU_D[6] }]
set_property -dict { PACKAGE_PIN G18 IOSTANDARD LVCMOS33 } [get_ports { MCU_D[7] }]

##############################################################################
## MCU Control Signals
##############################################################################
set_property -dict { PACKAGE_PIN D14 IOSTANDARD LVCMOS33 } [get_ports { TX_LOAD }]
set_property -dict { PACKAGE_PIN E15 IOSTANDARD LVCMOS33 } [get_ports { RX_ACK }]
set_property -dict { PACKAGE_PIN E16 IOSTANDARD LVCMOS33 } [get_ports { MCU_OE_N }]
set_property -dict { PACKAGE_PIN F15 IOSTANDARD LVCMOS33 } [get_ports { DATA_TAKEN }]
set_property -dict { PACKAGE_PIN H15 IOSTANDARD LVCMOS33 } [get_ports { DATA_WRITTEN }]

##############################################################################
## Timing - Async design, no clock constraints needed
##############################################################################

## Mark flip-flops as async (they use async set/reset)
set_property ASYNC_REG true [get_cells tx_avail_reg]
set_property ASYNC_REG true [get_cells rx_ready_reg]
set_property ASYNC_REG true [get_cells data_taken_reg]
set_property ASYNC_REG true [get_cells data_written_reg]

##############################################################################
## Configuration
##############################################################################
set_property CFGBVS VCCO [current_design]
set_property CONFIG_VOLTAGE 3.3 [current_design]
set_property BITSTREAM.CONFIG.SPI_BUSWIDTH 4 [current_design]
