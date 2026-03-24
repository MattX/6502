use crate::test_harness::TestHarness;

#[test]
fn write_and_read_with_mock() {
    let mut h = TestHarness::new();

    // Pre-program device 7 to respond with "ABC"
    h.mock_device_read(7, vec![0x41, 0x42, 0x43]);

    // Write "XY" to device 7, then read the mocked response back.
    h.load_program(&[
        // Write: [device=7] [len=2] [X] [Y]
        0xA9, 0x07,       // LDA #$07
        0x8D, 0x40, 0xE0, // STA $E040
        0xA9, 0x02,       // LDA #$02
        0x8D, 0x40, 0xE0, // STA $E040
        0xA9, 0x58,       // LDA #'X'
        0x8D, 0x40, 0xE0, // STA $E040
        0xA9, 0x59,       // LDA #'Y'
        0x8D, 0x40, 0xE0, // STA $E040
        // Read from device 7: 7 | 0x80 = 0x87
        0xA9, 0x87,       // LDA #$87
        0x8D, 0x40, 0xE0, // STA $E040
        0xAD, 0x40, 0xE0, // LDA $E040     ; length
        0x85, 0x10,       // STA $10
        0xAD, 0x40, 0xE0, // LDA $E040     ; byte 1
        0x85, 0x11,       // STA $11
        0xAD, 0x40, 0xE0, // LDA $E040     ; byte 2
        0x85, 0x12,       // STA $12
        0xAD, 0x40, 0xE0, // LDA $E040     ; byte 3
        0x85, 0x13,       // STA $13
        0xDB,             // STP
    ]);
    h.run(1000);

    // Verify the mocked read response was received
    assert_eq!(h.peek(0x10), 3, "mock response length");
    assert_eq!(h.peek(0x11), b'A');
    assert_eq!(h.peek(0x12), b'B');
    assert_eq!(h.peek(0x13), b'C');

    // Verify the write was captured
    let writes = h.device_writes(7);
    assert_eq!(writes.len(), 1);
    assert_eq!(writes[0], b"XY");
}

#[test]
fn mock_device_read() {
    let mut h = TestHarness::new();

    // Pre-program device 5 (normally a stub) to return [0xDE, 0xAD]
    h.mock_device_read(5, vec![0xDE, 0xAD]);

    h.load_program(&[
        // Read from device 5: 5 | 0x80 = 0x85
        0xA9, 0x85,       // LDA #$85
        0x8D, 0x40, 0xE0, // STA $E040
        0xAD, 0x40, 0xE0, // LDA $E040     ; length
        0x85, 0x10,       // STA $10
        0xAD, 0x40, 0xE0, // LDA $E040     ; byte 1
        0x85, 0x11,       // STA $11
        0xAD, 0x40, 0xE0, // LDA $E040     ; byte 2
        0x85, 0x12,       // STA $12
        0xDB,             // STP
    ]);
    h.run(500);

    assert_eq!(h.peek(0x10), 2, "mock response length");
    assert_eq!(h.peek(0x11), 0xDE);
    assert_eq!(h.peek(0x12), 0xAD);
}

#[test]
fn device_write_capture() {
    let mut h = TestHarness::new();

    // Write "Hi" to device 2 (terminal), verify it's captured in the packet log.
    h.load_program(&[
        0xA9, 0x02,       // LDA #$02      ; device 2
        0x8D, 0x40, 0xE0, // STA $E040
        0xA9, 0x02,       // LDA #$02      ; length 2
        0x8D, 0x40, 0xE0, // STA $E040
        0xA9, 0x48,       // LDA #'H'
        0x8D, 0x40, 0xE0, // STA $E040
        0xA9, 0x69,       // LDA #'i'
        0x8D, 0x40, 0xE0, // STA $E040
        0xDB,             // STP
    ]);
    h.run(500);

    let writes = h.device_writes(2);
    assert_eq!(writes.len(), 1);
    assert_eq!(writes[0], b"Hi");
}

#[test]
fn run_until_pc() {
    let mut h = TestHarness::new();

    h.load_program(&[
        0xA9, 0x42,       // LDA #$42      ; $E100
        0x85, 0x10,       // STA $10       ; $E102
        0xA9, 0x00,       // LDA #$00      ; $E104 — target
        0xDB,             // STP           ; $E106
    ]);

    // Run until we reach the second LDA
    h.run_until_pc(0xE104, 100);
    assert_eq!(h.a(), 0x42, "accumulator should still hold first LDA value");
    assert_eq!(h.peek(0x10), 0x42, "zero page should have been written");
}

// ---------------------------------------------------------------------------
// Bootloader integration tests
// ---------------------------------------------------------------------------

const BOOTLOADER_BIN: &[u8] = include_bytes!("../../../blinkenlights/bootloader.bin");

/// Concatenate all device 2 (terminal) write payloads into a string.
fn terminal_output(h: &TestHarness) -> String {
    let bytes: Vec<u8> = h
        .device_writes(2)
        .iter()
        .flat_map(|w| w.iter().copied())
        .collect();
    String::from_utf8_lossy(&bytes).into_owned()
}

/// Set up a bootloader test: load ROM, mock device 0 "connected" status,
/// and queue keyboard input for the first command.
fn bootloader_setup(h: &mut TestHarness, keyboard_input: &[u8]) {
    h.load_rom(BOOTLOADER_BIN);
    // Device 0 status: 2 bytes, second byte = 1 (Pi Zero connected)
    h.mock_device_read(0, vec![0x00, 0x01]);
    // Device 2: keyboard input for the REPL command
    h.mock_device_read(2, keyboard_input.to_vec());
}

#[test]
fn bootloader_peek() {
    let mut h = TestHarness::new();
    bootloader_setup(&mut h, b"peek 9000\n");
    // Plant a known value at $9000 (high RAM, not touched by BSS init)
    h.poke(0x9000, 0x42);

    h.run(2_000_000);

    let output = terminal_output(&h);
    assert!(
        output.contains("Mattbrew 6502 ready"),
        "expected boot message, got: {}",
        output
    );
    assert!(
        output.contains("9000: 42"),
        "expected peek output '9000: 42', got: {}",
        output
    );
}

#[test]
fn bootloader_lcd() {
    let mut h = TestHarness::new();
    bootloader_setup(&mut h, b"lcd Hello\n");

    h.run(2_000_000);

    let output = terminal_output(&h);
    assert!(
        output.contains("Mattbrew 6502 ready"),
        "expected boot message, got: {}",
        output
    );
    assert!(
        !output.contains("Unknown command"),
        "lcd command should be recognized, got: {}",
        output
    );
    // The echoed input confirms the REPL processed the line
    assert!(
        output.contains("lcd Hello"),
        "expected echoed input, got: {}",
        output
    );
}

#[test]
fn multiple_mock_responses_fifo() {
    let mut h = TestHarness::new();

    // Queue two responses for device 4
    h.mock_device_read(4, vec![0x01]);
    h.mock_device_read(4, vec![0x02]);

    h.load_program(&[
        // First read from device 4
        0xA9, 0x84,       // LDA #$84
        0x8D, 0x40, 0xE0, // STA $E040
        0xAD, 0x40, 0xE0, // LDA $E040     ; length
        0xAD, 0x40, 0xE0, // LDA $E040     ; data byte
        0x85, 0x10,       // STA $10
        // Second read from device 4
        0xA9, 0x84,       // LDA #$84
        0x8D, 0x40, 0xE0, // STA $E040
        0xAD, 0x40, 0xE0, // LDA $E040     ; length
        0xAD, 0x40, 0xE0, // LDA $E040     ; data byte
        0x85, 0x11,       // STA $11
        // Third read — no more responses queued, should get length 0
        0xA9, 0x84,       // LDA #$84
        0x8D, 0x40, 0xE0, // STA $E040
        0xAD, 0x40, 0xE0, // LDA $E040     ; length (should be 0)
        0x85, 0x12,       // STA $12
        0xDB,             // STP
    ]);
    h.run(1000);

    assert_eq!(h.peek(0x10), 0x01, "first response");
    assert_eq!(h.peek(0x11), 0x02, "second response");
    assert_eq!(h.peek(0x12), 0x00, "third read should return length 0");
}
