`timescale 1ns / 1ps
//////////////////////////////////////////////////////////////////////////////
// Testbench for 6502-MCU Interface (Integrated Version)
//////////////////////////////////////////////////////////////////////////////

module mcu_interface_integrated_tb;

    // 6502 Bus
    reg        PHI2;
    reg        CS_N;
    reg        RW;
    reg        A0;
    wire [7:0] D;
    reg  [7:0] D_drive;
    reg        D_oe;

    // MCU Interface
    wire [7:0] MCU_D;
    reg  [7:0] MCU_D_drive;
    reg        MCU_D_oe;
    reg        TX_LOAD;
    reg        RX_ACK;
    reg        MCU_OE_N;
    wire       DATA_TAKEN;
    wire       DATA_WRITTEN;

    // Directly directly directly directly directly directly directly directly directly Directly directly directly drive directly directly directly directly data buses
    assign D = D_oe ? D_drive : 8'bZZZZZZZZ;
    assign MCU_D = MCU_D_oe ? MCU_D_drive : 8'bZZZZZZZZ;

    // Instantiate UUT
    mcu_interface_integrated uut (
        .PHI2(PHI2),
        .CS_N(CS_N),
        .RW(RW),
        .A0(A0),
        .D(D),
        .MCU_D(MCU_D),
        .TX_LOAD(TX_LOAD),
        .RX_ACK(RX_ACK),
        .MCU_OE_N(MCU_OE_N),
        .DATA_TAKEN(DATA_TAKEN),
        .DATA_WRITTEN(DATA_WRITTEN)
    );

    // PHI2 clock generation (5 MHz = 200ns period)
    initial PHI2 = 0;
    always #100 PHI2 = ~PHI2;

    // Test sequence
    initial begin
        // Initialize
        CS_N = 1;
        RW = 1;
        A0 = 0;
        D_drive = 8'h00;
        D_oe = 0;
        MCU_D_drive = 8'h00;
        MCU_D_oe = 0;
        TX_LOAD = 0;
        RX_ACK = 0;
        MCU_OE_N = 1;

        $display("=== Integrated Interface Testbench ===");
        $display("");

        #100;

        //--------------------------------------------------------------------
        // Initialize RX_READY
        //--------------------------------------------------------------------
        $display("Initializing: MCU pulses RX_ACK");
        RX_ACK = 1; #50; RX_ACK = 0; #50;

        //--------------------------------------------------------------------
        // Test TX Path: MCU sends $A5 to CPU
        //--------------------------------------------------------------------
        $display("");
        $display("=== TX Path Test: MCU sends $A5 to CPU ===");

        // MCU writes to TX register
        $display("  MCU: Writing $A5 to TX register");
        MCU_D_oe = 1;
        MCU_D_drive = 8'hA5;
        #20;
        TX_LOAD = 1;
        #50;
        TX_LOAD = 0;
        #20;
        MCU_D_oe = 0;
        #50;

        $display("  DATA_TAKEN=%b (expect 0)", DATA_TAKEN);

        // CPU reads status
        @(posedge PHI2);
        #10;
        CS_N = 0; A0 = 1; RW = 1;
        #80;
        $display("  CPU: Status register = $%02X (expect $80 = TX_AVAIL)", D);
        @(negedge PHI2);
        CS_N = 1;
        #50;

        // CPU reads TX register
        @(posedge PHI2);
        #10;
        CS_N = 0; A0 = 0; RW = 1;
        #80;
        $display("  CPU: TX register = $%02X (expect $A5)", D);
        @(negedge PHI2);
        CS_N = 1;
        #50;

        $display("  DATA_TAKEN=%b (expect 1)", DATA_TAKEN);

        //--------------------------------------------------------------------
        // Test RX Path: CPU sends $5A to MCU
        //--------------------------------------------------------------------
        $display("");
        $display("=== RX Path Test: CPU sends $5A to MCU ===");

        // Ensure RX_READY
        RX_ACK = 1; #50; RX_ACK = 0; #50;

        // CPU reads status to verify RX_READY
        @(posedge PHI2);
        #10;
        CS_N = 0; A0 = 1; RW = 1;
        #80;
        $display("  CPU: Status register = $%02X (expect $40 = RX_READY)", D);
        @(negedge PHI2);
        CS_N = 1;
        #50;

        // CPU writes RX register
        @(posedge PHI2);
        #10;
        CS_N = 0; A0 = 0; RW = 0;
        D_oe = 1;
        D_drive = 8'h5A;
        #80;
        $display("  CPU: Writing $5A to RX register");
        @(negedge PHI2);
        CS_N = 1;
        D_oe = 0;
        #50;

        $display("  DATA_WRITTEN=%b (expect 1)", DATA_WRITTEN);

        // MCU reads RX register
        MCU_OE_N = 0;
        #50;
        $display("  MCU: RX register = $%02X (expect $5A)", MCU_D);
        MCU_OE_N = 1;
        #20;

        // MCU acknowledges
        RX_ACK = 1; #50; RX_ACK = 0; #50;
        $display("  DATA_WRITTEN=%b (expect 0)", DATA_WRITTEN);

        //--------------------------------------------------------------------
        // Test multiple bytes: MCU sends $12, $34, CPU sends $AB, $CD
        //--------------------------------------------------------------------
        $display("");
        $display("=== Multi-byte Transfer Test ===");

        // MCU sends $12
        MCU_D_oe = 1; MCU_D_drive = 8'h12;
        TX_LOAD = 1; #50; TX_LOAD = 0;
        MCU_D_oe = 0; #50;

        // CPU reads $12
        @(posedge PHI2); CS_N = 0; A0 = 0; RW = 1;
        #80;
        $display("  CPU read: $%02X (expect $12)", D);
        @(negedge PHI2); CS_N = 1; #50;

        // MCU sends $34
        MCU_D_oe = 1; MCU_D_drive = 8'h34;
        TX_LOAD = 1; #50; TX_LOAD = 0;
        MCU_D_oe = 0; #50;

        // CPU reads $34
        @(posedge PHI2); CS_N = 0; A0 = 0; RW = 1;
        #80;
        $display("  CPU read: $%02X (expect $34)", D);
        @(negedge PHI2); CS_N = 1; #50;

        // Reset RX_READY
        RX_ACK = 1; #50; RX_ACK = 0; #50;

        // CPU sends $AB
        @(posedge PHI2); CS_N = 0; A0 = 0; RW = 0;
        D_oe = 1; D_drive = 8'hAB;
        #80;
        @(negedge PHI2); CS_N = 1; D_oe = 0; #50;

        // MCU reads $AB
        MCU_OE_N = 0; #50;
        $display("  MCU read: $%02X (expect $AB)", MCU_D);
        MCU_OE_N = 1;
        RX_ACK = 1; #50; RX_ACK = 0; #50;

        // CPU sends $CD
        @(posedge PHI2); CS_N = 0; A0 = 0; RW = 0;
        D_oe = 1; D_drive = 8'hCD;
        #80;
        @(negedge PHI2); CS_N = 1; D_oe = 0; #50;

        // MCU reads $CD
        MCU_OE_N = 0; #50;
        $display("  MCU read: $%02X (expect $CD)", MCU_D);
        MCU_OE_N = 1;
        RX_ACK = 1; #50; RX_ACK = 0; #50;

        //--------------------------------------------------------------------
        // Done
        //--------------------------------------------------------------------
        $display("");
        $display("=== All tests complete ===");
        #200;
        $finish;
    end

    // Waveform dump
    initial begin
        $dumpfile("mcu_interface_integrated_tb.vcd");
        $dumpvars(0, mcu_interface_integrated_tb);
    end

endmodule
