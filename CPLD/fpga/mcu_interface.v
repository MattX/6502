//////////////////////////////////////////////////////////////////////////////
// 6502-to-MCU Bus Interface
// Target: Digilent Cmod A7 (Xilinx Artix-7)
//
// Implements the same logic as the ATF750LVC/ATF22V10C CPLD version.
// Directly portable - no clock domain crossing needed since the design
// uses async set/reset flip-flops.
//
// Reference: 6502_mcu_interface_spec.md
//////////////////////////////////////////////////////////////////////////////

module mcu_interface (
    // 6502 Bus Inputs
    input  wire PHI2,       // 6502 system clock
    input  wire CS_N,       // Chip select (active low)
    input  wire RW,         // Read/Write: 1=read, 0=write
    input  wire A0,         // Register select: 0=data, 1=status

    // MCU Interface Inputs
    input  wire TX_LOAD,    // MCU pulses to load TX latch
    input  wire RX_ACK,     // MCU pulses to acknowledge RX read

    // Latch Control Outputs
    output wire TX_OE_N,    // Enable TX latch onto 6502 bus (active low)
    output wire RX_CLK,     // Clock CPU data into RX latch
    output wire STATUS_OE_N,// Enable status latch onto 6502 bus (active low)
    output wire STATUS_CLK, // Clock status bits into status latch

    // Handshake Status Outputs
    output reg  TX_AVAIL,   // Status bit: MCU data available for CPU
    output reg  RX_READY,   // Status bit: RX register empty, CPU can write
    output reg  DATA_TAKEN, // To MCU: CPU has read TX byte
    output reg  DATA_WRITTEN// To MCU: CPU has written RX byte
);

    //------------------------------------------------------------------------
    // Internal decode signals
    //------------------------------------------------------------------------
    wire TX_READ;       // CPU is reading TX data register
    wire RX_WRITE;      // CPU is writing RX data register
    wire STATUS_READ;   // CPU is reading status register

    assign TX_READ     = PHI2 & ~CS_N & ~A0 & RW;
    assign RX_WRITE    = PHI2 & ~CS_N & ~A0 & ~RW;
    assign STATUS_READ = PHI2 & ~CS_N & A0 & RW;

    //------------------------------------------------------------------------
    // Combinatorial outputs
    //------------------------------------------------------------------------

    // TX_OE_N: Enable TX latch outputs when CPU reads TX register
    assign TX_OE_N = ~TX_READ;

    // RX_CLK: Clock CPU data into RX latch during write
    assign RX_CLK = RX_WRITE;

    // STATUS_OE_N: Enable status latch outputs when CPU reads status
    assign STATUS_OE_N = ~STATUS_READ;

    // STATUS_CLK: Capture status on PHI2 falling edge (inverted PHI2)
    assign STATUS_CLK = ~PHI2;

    //------------------------------------------------------------------------
    // TX_AVAIL: Set on TX_LOAD rising edge, cleared when TX_READ is high
    //------------------------------------------------------------------------
    always @(posedge TX_LOAD or posedge TX_READ) begin
        if (TX_READ)
            TX_AVAIL <= 1'b0;
        else
            TX_AVAIL <= 1'b1;
    end

    //------------------------------------------------------------------------
    // RX_READY: Set on RX_ACK rising edge, cleared when RX_WRITE is high
    // Note: Powers up to 0; MCU must pulse RX_ACK at startup
    //------------------------------------------------------------------------
    always @(posedge RX_ACK or posedge RX_WRITE) begin
        if (RX_WRITE)
            RX_READY <= 1'b0;
        else
            RX_READY <= 1'b1;
    end

    //------------------------------------------------------------------------
    // DATA_TAKEN: Set on TX_READ rising edge, cleared when TX_LOAD is high
    //------------------------------------------------------------------------
    always @(posedge TX_READ or posedge TX_LOAD) begin
        if (TX_LOAD)
            DATA_TAKEN <= 1'b0;
        else
            DATA_TAKEN <= 1'b1;
    end

    //------------------------------------------------------------------------
    // DATA_WRITTEN: Set on RX_WRITE rising edge, cleared when RX_ACK is high
    //------------------------------------------------------------------------
    always @(posedge RX_WRITE or posedge RX_ACK) begin
        if (RX_ACK)
            DATA_WRITTEN <= 1'b0;
        else
            DATA_WRITTEN <= 1'b1;
    end

endmodule
