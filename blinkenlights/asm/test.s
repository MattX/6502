.segment "CODE"

reset:
    ; Initialize VIA - set port to output
    lda #$ff
    sta $6002
    
    ; Clear LEDs initially
    lda #$00
    sta $6000
    
    ; Initialize test parameters
    lda #$00
    sta $00         ; Low byte of RAM address to test
    lda #$10        ; Start testing at $1000
    sta $01         ; High byte of RAM address
    
    lda #$00
    sta $02         ; Test pattern value

test_loop:
    ; Write test pattern to current address
    ldy #$00
    lda $02
    sta ($00),y
    
    ; Display current pattern on LEDs
    sta $6000
    
    ; Read back and verify
    lda ($00),y
    cmp $02
    bne error       ; If not equal, jump to error
    
    ; Increment test pattern
    inc $02
    
    ; Increment address (16-bit)
    inc $00
    bne test_loop   ; If low byte didn't wrap, continue
    
    inc $01         ; Increment high byte
    
    ; Check if we've reached $8000 (ROM start)
    lda $01
    cmp #$80
    bne test_loop   ; If not at ROM, continue testing
    
    ; Test complete - show success pattern (alternating)
success:
    lda #$aa
    sta $6000
    jsr delay
    lda #$55
    sta $6000
    jsr delay
    jmp success

error:
    ; Clear all LEDs on error
    lda #$00
    sta $6000
    
error_loop:
    jmp error_loop  ; Halt on error

; Simple delay routine
delay:
    ldx #$00
delay1:
    ldy #$00
delay2:
    dey
    bne delay2
    dex
    bne delay1
    rts

.segment "VECTORS"
    .word $0000      ; NMI
    .word reset      ; RESET
    .word $0000      ; IRQ

