; rpitest.s - Tests for the RPI PIO interface in write-only mode
;
; Memory map:
;   $0000-$7FFF  RAM
;   $8000-$9FFF  VIA (6522)
;   $A000-$BFFF  RPI
;   $C000-$FFFF  ROM
;
; Iterates over n = 0xff - 0x00, writing [n; n] (in Rust notation) to the RPI register, device 0.
; Expected sequence:
;    0 0xff {0xff .. 0xff}
;    0 0xfe {0xfe .. 0xfe}
;    ...
;    0 0x2 0x2 0x2
;    0 0x1 0x1
;    0 0
;    0 0xff {0xff .. 0xff}
;    etc.

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

; --- RP2350 address
RPI = $a000

; --- Zero page variables ---
iteration = $00         ; 1 byte: number of full outer loops
str_ptr   = $02         ; 2 bytes: pointer for print_str

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

  ; Reset iteration counter
  stz iteration

start_full_pass:
  lda #$ff

write_series:
  stz RPI                 ; Device 0
  sta RPI                 ; Length n
  tax                     ; Counter = n
  beq skip_inner
write_byte:
  sta RPI                 ; Data = n
  dex
  bne write_byte
skip_inner:
  dea                     ; 65C02: Decrement A directly
  cmp #$ff                ; Check for underflow
  bne write_series

  inc iteration           ; Increment the pass counter
  lda iteration
  jsr show_test_info      ; Update LCD with "Iter XX"
  
  jmp start_full_pass     ; Repeat the whole sequence


; ============================================================
;  DISPLAY ROUTINES
; ============================================================

; Show "Iter N". N stored in register A
show_test_info:
  pha
  lda #$80                ; cursor -> line 1, col 0
  jsr lcd_instruction
  PRINT_STR msg_iter      ; "Iter "
  pla
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

hex_chars:  .byte "0123456789ABCDEF"

msg_iter:    .asciiz "Iter "


.segment "VECTORS"
  .word $0000             ; NMI
  .word reset             ; RESET
  .word $0000             ; IRQ
