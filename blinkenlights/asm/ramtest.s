; ramtest.s - RAM test for 6502 breadboard computer
;
; Memory map:
;   $0000-$7FFF  RAM
;   $8000-$9FFF  VIA (6522)
;   $A000-$BFFF  RPI
;   $C000-$FFFF  ROM
;
; Tests RAM $0200-$7FFF with four patterns ($00, $FF, $55, $AA).
; For each pattern: fills the whole range, then reads it all back.
; Progress is shown on the HD44780 LCD:
;   Line 1: "Test N/4:$XX"   (pattern number and value)
;   Line 2: "W:$XXXX" / "V:$XXXX"  (phase and current address)

.segment "CODE"

; --- VIA registers ---
PORTB = $8000
PORTA = $8001
DDRB  = $8002
DDRA  = $8003

; --- LCD control bits on port A ---
E  = %10000000
RW = %01000000
RS = %00100000

; --- Zero page variables ---
ptr      = $00          ; 2 bytes: pointer into RAM under test
pattern  = $02          ; current test pattern
pat_idx  = $03          ; index into patterns table (0-3)
readback = $04          ; value read back on failure
str_ptr  = $05          ; 2 bytes: pointer for print_str

; --- Constants ---
NUM_PATTERNS = 4
RAM_START_HI = $02      ; test begins at $0200
RAM_END_HI   = $80      ; test ends before $8000

; --- Macro: load string address into str_ptr and call print_str ---
.macro PRINT_STR addr
  lda #<(addr)
  sta str_ptr
  lda #>(addr)
  sta str_ptr+1
  jsr print_str
.endmacro


; ============================================================
;  MAIN PROGRAM
; ============================================================

reset:
  ldx #$ff
  txs

  ; Initialise VIA
  lda #$ff
  sta DDRB                ; port B all output
  sta DDRA                ; port A all output

  ; Initialise LCD
  lda #%00111000          ; 8-bit mode; 2-line; 5x8 font
  jsr lcd_instruction
  lda #%00001100          ; display on; cursor off; blink off
  jsr lcd_instruction
  lda #%00000110          ; increment cursor; no display shift
  jsr lcd_instruction
  lda #%00000001          ; clear display
  jsr lcd_instruction

  ; Start testing
  lda #0
  sta pat_idx

next_pattern:
  ldx pat_idx
  lda patterns, x
  sta pattern

  ; Show pattern info on line 1
  jsr show_test_info

  ; ---- Write phase ----
  lda #0
  sta ptr
  lda #RAM_START_HI
  sta ptr+1

write_loop:
  lda ptr
  bne @wr                 ; update display only at page boundaries
  lda #'W'
  jsr show_phase_addr
@wr:
  ldy #0
  lda pattern
  sta (ptr), y

  inc ptr
  bne write_loop
  inc ptr+1
  lda ptr+1
  cmp #RAM_END_HI
  bne write_loop

  ; ---- Verify phase ----
  lda #0
  sta ptr
  lda #RAM_START_HI
  sta ptr+1

verify_loop:
  lda ptr
  bne @vr
  lda #'V'
  jsr show_phase_addr
@vr:
  ldy #0
  lda (ptr), y
  cmp pattern
  bne test_fail

  inc ptr
  bne verify_loop
  inc ptr+1
  lda ptr+1
  cmp #RAM_END_HI
  bne verify_loop

  ; Pattern passed — advance to next
  inc pat_idx
  lda pat_idx
  cmp #NUM_PATTERNS
  bne next_pattern

  ; All patterns passed
  jsr show_pass
done:
  jmp done

; ---- Failure path ----
test_fail:
  sta readback
  jsr show_fail
fail_halt:
  jmp fail_halt


; ============================================================
;  DISPLAY ROUTINES
; ============================================================

; Show "Test N/4:$XX" on line 1
show_test_info:
  lda #$80                ; cursor -> line 1, col 0
  jsr lcd_instruction
  PRINT_STR msg_test      ; "Test "
  lda pat_idx
  clc
  adc #'1'
  jsr print_char          ; N
  PRINT_STR msg_of4       ; "/4:$"
  lda pattern
  jsr print_hex           ; XX
  ; pad to 16 chars (12 written, 4 spaces)
  ldx #4
@pad:
  lda #' '
  jsr print_char
  dex
  bne @pad
  rts


; Show "<phase>:$XXXX" on line 2
; Call with A = 'W' or 'V'
show_phase_addr:
  pha
  lda #$C0                ; cursor -> line 2, col 0
  jsr lcd_instruction
  pla
  jsr print_char          ; phase character
  lda #':'
  jsr print_char
  lda #'$'
  jsr print_char
  lda ptr+1
  jsr print_hex
  lda ptr
  jsr print_hex           ; 7 chars total
  rts


; Show pass result
show_pass:
  lda #%00000001          ; clear display
  jsr lcd_instruction
  PRINT_STR msg_pass1     ; "RAM test"
  lda #$C0
  jsr lcd_instruction
  PRINT_STR msg_pass2     ; "PASSED!"
  rts


; Show failure details
show_fail:
  lda #%00000001          ; clear display
  jsr lcd_instruction
  PRINT_STR msg_fail_at   ; "FAIL @$"
  lda ptr+1
  jsr print_hex
  lda ptr
  jsr print_hex
  lda #$C0
  jsr lcd_instruction
  PRINT_STR msg_w_col     ; "W:$"
  lda pattern
  jsr print_hex
  PRINT_STR msg_spc_r     ; " R:$"
  lda readback
  jsr print_hex
  rts


; ============================================================
;  STRING AND HEX PRINTING
; ============================================================

; Print null-terminated string pointed to by str_ptr
print_str:
  ldy #0
@loop:
  lda (str_ptr), y
  beq @done
  jsr print_char
  iny
  jmp @loop
@done:
  rts


; Print byte in A as two hex digits
print_hex:
  pha
  lsr
  lsr
  lsr
  lsr
  tax
  lda hex_chars, x
  jsr print_char
  pla
  and #$0f
  tax
  lda hex_chars, x
  jsr print_char
  rts


; ============================================================
;  LCD ROUTINES
; ============================================================

lcd_instruction:
  jsr lcd_wait
  sta PORTB
  lda #0                  ; clear RS/RW/E
  sta PORTA
  lda #E                  ; pulse E high
  sta PORTA
  lda #0                  ; E low again
  sta PORTA
  rts

lcd_wait:
  pha
  lda #%00000000          ; port B -> input
  sta DDRB
lcd_busy:
  lda #RW
  sta PORTA
  lda #(RW | E)
  sta PORTA
  lda PORTB
  and #%10000000
  bne lcd_busy
  lda #RW                 ; clear E
  sta PORTA
  lda #%11111111          ; port B -> output
  sta DDRB
  pla
  rts

print_char:
  jsr lcd_wait
  sta PORTB
  lda #RS
  sta PORTA
  lda #(RS | E)
  sta PORTA
  lda #RS
  sta PORTA
  rts


; ============================================================
;  DATA
; ============================================================

patterns:   .byte $00, $FF, $55, $AA
hex_chars:  .byte "0123456789ABCDEF"

msg_test:    .asciiz "Test "
msg_of4:     .asciiz "/4:$"
msg_pass1:   .asciiz "RAM test"
msg_pass2:   .asciiz "PASSED!"
msg_fail_at: .asciiz "FAIL @$"
msg_w_col:   .asciiz "W:$"
msg_spc_r:   .asciiz " R:$"


.segment "VECTORS"
  .word $0000             ; NMI
  .word reset             ; RESET
  .word $0000             ; IRQ
