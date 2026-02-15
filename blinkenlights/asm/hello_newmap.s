; Memory map for this implementation:
; $0000-$7fff: RAM
; $8000-$9fff: VIA
; $a000-$bfff: RPI
; $c000-$ffff: ROM

.segment "CODE"

PORTB = $8000
PORTA = $8001
DDRB = $8002
DDRA = $8003

E  = %10000000
RW = %01000000
RS = %00100000


reset:
  ldx #$ff
  txs

  lda #%11111111  ; Set all pins of ports A and B to output
  sta DDRB
  sta DDRA

  lda #%00111000  ; Set 8-bit mode; two-line display; 5x8 font
  jsr lcd_instruction
  lda #%00001110  ; Display on; cursor on; blink off
  jsr lcd_instruction
  lda #%00000110  ; Increment and shift cursor, don't shift display
  jsr lcd_instruction
  lda #%00000001  ; Clear display
  jsr lcd_instruction

  ldx #0
print:
  lda message, x
  beq loop
  jsr print_char
  inx
  jmp print


message: .asciiz "Hello, world!"

loop:             ; Busy loop when done
  jmp loop

lcd_instruction:
  jsr lcd_wait
  sta PORTB       ; Send contents of A to port B
  lda #0          ; Clear RS/RW/E bits
  sta PORTA 
  lda #E          ; Set E bit to send instruction
  sta PORTA
  lda #0          ; Clear control bits again
  sta PORTA
  rts

lcd_wait:
  pha
  lda #%00000000  ; Set all pins of port B to input
  sta DDRB
lcd_busy:
  lda #RW         ; Enable RW bit on port A
  sta PORTA
  lda #(RW | E)   ; Enable RW + chip enable on port A
  sta PORTA
  lda PORTB
  and #%10000000  ; Check if topmost bit (busy) is set
  bne lcd_busy

  lda #RW         ; Clear chip enable
  sta PORTA
  lda #%11111111  ; Set all pins of port B to input
  sta DDRB
  pla
  rts


print_char:
  jsr lcd_wait
  sta PORTB
  lda #RS         ; Set RS; clear RW/E bits
  sta PORTA
  lda #(RS | E)   ; Set E bit in addition to RS
  sta PORTA
  lda #RS         ; Clear E bit again
  sta PORTA
  rts


.segment "VECTORS"
  .word $0000      ; NMI
  .word reset      ; RESET
  .word $0000      ; IRQ
