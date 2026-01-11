`timescale 1ns / 1ps
//////////////////////////////////////////////////////////////////////////////
// Testbench for 6502-MCU Interface
//////////////////////////////////////////////////////////////////////////////

module mcu_interface_tb;

    // Inputs
    reg PHI2;
    reg CS_N;
    reg RW;
    reg A0;
    reg TX_LOAD;
    reg RX_ACK;

    // Outputs
    wire TX_OE_N;
    wire RX_CLK;
    wire STATUS_OE_N;
    wire STATUS_CLK;
    wire TX_AVAIL;
    wire RX_READY;
    wire DATA_TAKEN;
    wire DATA_WRITTEN;

    // Instantiate the Unit Under Test (UUT)
    mcu_interface uut (
        .PHI2(PHI2),
        .CS_N(CS_N),
        .RW(RW),
        .A0(A0),
        .TX_LOAD(TX_LOAD),
        .RX_ACK(RX_ACK),
        .TX_OE_N(TX_OE_N),
        .RX_CLK(RX_CLK),
        .STATUS_OE_N(STATUS_OE_N),
        .STATUS_CLK(STATUS_CLK),
        .TX_AVAIL(TX_AVAIL),
        .RX_READY(RX_READY),
        .DATA_TAKEN(DATA_TAKEN),
        .DATA_WRITTEN(DATA_WRITTEN)
    );

    // PHI2 clock generation (5 MHz = 200ns period)
    initial PHI2 = 0;
    always #100 PHI2 = ~PHI2;

    // Test sequence
    initial begin
        // Initialize inputs to idle state
        CS_N = 1;
        RW = 1;
        A0 = 0;
        TX_LOAD = 0;
        RX_ACK = 0;

        $display("=== 6502-MCU Interface Testbench ===");
        $display("");

        // Wait for initial state to settle
        #50;

        //--------------------------------------------------------------------
        // Test 1: Power-on state
        //--------------------------------------------------------------------
        $display("Test 1: Power-on state");
        $display("  TX_AVAIL=%b (expect X), RX_READY=%b (expect X)", TX_AVAIL, RX_READY);
        $display("  DATA_TAKEN=%b, DATA_WRITTEN=%b", DATA_TAKEN, DATA_WRITTEN);
        #200;

        //--------------------------------------------------------------------
        // Test 2: MCU initialization - pulse RX_ACK to set RX_READY
        //--------------------------------------------------------------------
        $display("");
        $display("Test 2: MCU pulses RX_ACK to initialize RX_READY");
        TX_LOAD = 0; RX_ACK = 1;
        #50;
        RX_ACK = 0;
        #50;
        $display("  RX_READY=%b (expect 1)", RX_READY);

        //--------------------------------------------------------------------
        // Test 3: TX Path - MCU sends to CPU
        //--------------------------------------------------------------------
        $display("");
        $display("Test 3: TX Path - MCU sends byte to CPU");

        // MCU pulses TX_LOAD
        $display("  3a: MCU pulses TX_LOAD");
        TX_LOAD = 1;
        #50;
        TX_LOAD = 0;
        #50;
        $display("      TX_AVAIL=%b (expect 1), DATA_TAKEN=%b (expect 0)", TX_AVAIL, DATA_TAKEN);

        // Wait for PHI2 high, then CPU reads TX register
        @(posedge PHI2);
        #10;
        $display("  3b: CPU reads TX register (PHI2=1, CS_N=0, A0=0, RW=1)");
        CS_N = 0; A0 = 0; RW = 1;
        #10;
        $display("      TX_OE_N=%b (expect 0)", TX_OE_N);

        @(negedge PHI2);
        #10;
        CS_N = 1;
        $display("      After read: TX_AVAIL=%b (expect 0), DATA_TAKEN=%b (expect 1)", TX_AVAIL, DATA_TAKEN);

        //--------------------------------------------------------------------
        // Test 4: RX Path - CPU sends to MCU
        //--------------------------------------------------------------------
        $display("");
        $display("Test 4: RX Path - CPU sends byte to MCU");

        // Ensure RX_READY is set
        RX_ACK = 1; #50; RX_ACK = 0; #50;
        $display("  RX_READY=%b (expect 1)", RX_READY);

        // Wait for PHI2 high, then CPU writes RX register
        @(posedge PHI2);
        #10;
        $display("  4a: CPU writes RX register (PHI2=1, CS_N=0, A0=0, RW=0)");
        CS_N = 0; A0 = 0; RW = 0;
        #10;
        $display("      RX_CLK=%b (expect 1)", RX_CLK);

        @(negedge PHI2);
        #10;
        CS_N = 1; RW = 1;
        $display("      After write: RX_READY=%b (expect 0), DATA_WRITTEN=%b (expect 1)", RX_READY, DATA_WRITTEN);

        // MCU acknowledges
        $display("  4b: MCU pulses RX_ACK");
        RX_ACK = 1; #50; RX_ACK = 0; #50;
        $display("      RX_READY=%b (expect 1), DATA_WRITTEN=%b (expect 0)", RX_READY, DATA_WRITTEN);

        //--------------------------------------------------------------------
        // Test 5: Status register read
        //--------------------------------------------------------------------
        $display("");
        $display("Test 5: Status register read");

        @(posedge PHI2);
        #10;
        CS_N = 0; A0 = 1; RW = 1;
        #10;
        $display("  STATUS_OE_N=%b (expect 0), TX_OE_N=%b (expect 1)", STATUS_OE_N, TX_OE_N);

        @(negedge PHI2);
        CS_N = 1;
        #50;

        //--------------------------------------------------------------------
        // Test 6: STATUS_CLK tracks inverted PHI2
        //--------------------------------------------------------------------
        $display("");
        $display("Test 6: STATUS_CLK tracks inverted PHI2");
        @(negedge PHI2);
        #10;
        $display("  PHI2=%b, STATUS_CLK=%b (expect 1)", PHI2, STATUS_CLK);
        @(posedge PHI2);
        #10;
        $display("  PHI2=%b, STATUS_CLK=%b (expect 0)", PHI2, STATUS_CLK);

        //--------------------------------------------------------------------
        // Done
        //--------------------------------------------------------------------
        $display("");
        $display("=== All tests complete ===");
        #200;
        $finish;
    end

    // Optional: dump waveforms for GTKWave or Vivado
    initial begin
        $dumpfile("mcu_interface_tb.vcd");
        $dumpvars(0, mcu_interface_tb);
    end

endmodule
