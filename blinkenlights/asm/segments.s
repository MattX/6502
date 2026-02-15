.segment "CODE"
reset:
    lda #$ff
    sta $6002

loop:
    lda #$55
    sta $6000

    lda #$aa
    sta $6000

    jmp loop    

.segment "VECTORS"
    .word $0000      ; NMI
    .word reset     ; RESET
    .word $0000      ; IRQ

