
;----------------------------------------------------------------------
; string extensions
;----------------------------------------------------------------------

#nt_header tolower

; ## TOLOWER ( addr u -- addr u ) "convert ascii to lower case in place; uses tmp1"
; ## "tolower"  contrib

xt_tolower:
                jsr underflow_2
w_tolower:
                jsr w_two_dup
_loop:
                lda 0,x
                ora 1,x
                beq _done

                lda (2,x)
                cmp #'A'
                bcc +
                cmp #'Z'+1
                bcs +
                ora #$20
                sta (2,x)
+
                jsr slash_string_1
                bra _loop
_done:
                jsr w_two_drop

z_tolower:      rts


#nt_header asciiz, "asciiz>"

; ## ASCIIZ> ( c-addr -- addr u ) "Convert a zero-terminated string to counted; uses tmp1"
; ## "asciiz"  contrib

xt_asciiz:
        jsr underflow_1
w_asciiz:
        lda 0,x
        sta tmp1
        lda 1,x
        sta tmp1+1
        pha             ; save original high byte
        dex             ; push uint16 len
        dex
        stz 1,x

        ldy #0
-
        lda (tmp1),y
        beq +
        iny
        bne -
        inc tmp1+1      ; count a page
        inc 1,x
        bra -
+
        sty 0,x         ; low byte of len
        pla             ; starting page
        sta tmp1+1      ; reset original addr
z_asciiz:
        rts
