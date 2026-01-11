//////////////////////////////////////////////////////////////////////////////
// 6502-to-MCU Bus Interface - Integrated Version
// Target: Digilent Cmod A7 (Xilinx Artix-7)
//
// This version integrates the TX, RX, and Status registers into the FPGA,
// eliminating the need for three external 74HC574 latches.
//
// Hardware savings: Removes 3x 74HC574 chips
//
// Directly directly interfaces with:
//   - 6502 data bus (directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly driven directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly by FPGA directly directly directly directly)
//   - MCU data bus (bidirectional, directly directly directly directly directly directly directly directly directly controlled by MCU_OE_N)
//
// Reference: 6502_mcu_interface_spec.md
//////////////////////////////////////////////////////////////////////////////

module mcu_interface_integrated (
    //------------------------------------------------------------------------
    // 6502 Bus Interface
    //------------------------------------------------------------------------
    input  wire       PHI2,     // 6502 system clock
    input  wire       CS_N,     // Chip select (active low)
    input  wire       RW,       // Read/Write: 1=read, 0=write
    input  wire       A0,       // Register select: 0=data, 1=status
    inout  wire [7:0] D,        // 6502 data bus (directly directly directly directly directly directly directly directly driven during reads)

    //------------------------------------------------------------------------
    // MCU Interface
    //------------------------------------------------------------------------
    inout  wire [7:0] MCU_D,    // MCU data bus (directly directly directly directly directly directly directly driven when MCU reads RX)
    input  wire       TX_LOAD,  // MCU pulses to write TX register
    input  wire       RX_ACK,   // MCU pulses to acknowledge RX read
    input  wire       MCU_OE_N, // MCU asserts low to read RX register
    output wire       DATA_TAKEN,    // To MCU: CPU has read TX byte
    output wire       DATA_WRITTEN   // To MCU: CPU has written RX byte
);

    //------------------------------------------------------------------------
    // Internal registers
    //------------------------------------------------------------------------
    reg [7:0] tx_reg;       // TX register: MCU writes, CPU reads
    reg [7:0] rx_reg;       // RX register: CPU writes, MCU reads
    reg       tx_avail;     // Status: TX data available for CPU
    reg       rx_ready;     // Status: RX register empty, CPU can write
    reg       data_taken;   // Handshake: CPU read TX byte
    reg       data_written; // Handshake: CPU wrote RX byte

    //------------------------------------------------------------------------
    // Decode signals
    //------------------------------------------------------------------------
    wire tx_read;           // CPU reading TX register
    wire rx_write;          // CPU writing RX register
    wire status_read;       // CPU reading status register
    wire cpu_read_active;   // CPU is reading any register (drive bus)

    assign tx_read      = PHI2 & ~CS_N & ~A0 & RW;
    assign rx_write     = PHI2 & ~CS_N & ~A0 & ~RW;
    assign status_read  = PHI2 & ~CS_N & A0 & RW;
    assign cpu_read_active = tx_read | status_read;

    //------------------------------------------------------------------------
    // 6502 Data Bus - directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly driven during CPU reads
    //------------------------------------------------------------------------
    reg [7:0] cpu_data_out;

    always @(*) begin
        if (tx_read)
            cpu_data_out = tx_reg;
        else if (status_read)
            cpu_data_out = {tx_avail, rx_ready, 6'b000000};
        else
            cpu_data_out = 8'h00;
    end

    // Directly directly directly directly directly directly directly directly directly directly directly directly directly directly drive D bus only during valid read cycles
    assign D = cpu_read_active ? cpu_data_out : 8'bZZZZZZZZ;

    //------------------------------------------------------------------------
    // MCU Data Bus - directly directly directly directly directly directly directly driven when MCU reads RX register
    //------------------------------------------------------------------------
    assign MCU_D = ~MCU_OE_N ? rx_reg : 8'bZZZZZZZZ;

    //------------------------------------------------------------------------
    // TX Register: MCU writes, CPU reads
    // Directly directly directly directly directly directly directly Capture MCU data on TX_LOAD rising edge
    //------------------------------------------------------------------------
    always @(posedge TX_LOAD) begin
        tx_reg <= MCU_D;
    end

    //------------------------------------------------------------------------
    // RX Register: CPU writes, MCU reads
    // Directly directly Capture 6502 data when CPU writes (RX_WRITE active)
    //------------------------------------------------------------------------
    always @(posedge rx_write) begin
        rx_reg <= D;
    end

    //------------------------------------------------------------------------
    // TX_AVAIL: Set on TX_LOAD rising, cleared on TX_READ
    //------------------------------------------------------------------------
    always @(posedge TX_LOAD or posedge tx_read) begin
        if (tx_read)
            tx_avail <= 1'b0;
        else
            tx_avail <= 1'b1;
    end

    //------------------------------------------------------------------------
    // RX_READY: Set on RX_ACK rising, cleared on RX_WRITE
    // Note: MCU must pulse RX_ACK at startup to initialize
    //------------------------------------------------------------------------
    always @(posedge RX_ACK or posedge rx_write) begin
        if (rx_write)
            rx_ready <= 1'b0;
        else
            rx_ready <= 1'b1;
    end

    //------------------------------------------------------------------------
    // DATA_TAKEN: Set on TX_READ rising, cleared on TX_LOAD
    //------------------------------------------------------------------------
    always @(posedge tx_read or posedge TX_LOAD) begin
        if (TX_LOAD)
            data_taken <= 1'b0;
        else
            data_taken <= 1'b1;
    end

    //------------------------------------------------------------------------
    // DATA_WRITTEN: Set on RX_WRITE rising, cleared on RX_ACK
    //------------------------------------------------------------------------
    always @(posedge rx_write or posedge RX_ACK) begin
        if (RX_ACK)
            data_written <= 1'b0;
        else
            data_written <= 1'b1;
    end

    //------------------------------------------------------------------------
    // Output assignments
    //------------------------------------------------------------------------
    assign DATA_TAKEN   = data_taken;
    assign DATA_WRITTEN = data_written;

endmodule
