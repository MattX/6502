;----------------------------------------------------------------------
; core extensions
;----------------------------------------------------------------------

#nt_header le, "<="

; ## LESS_EQUAL ( n n -- f ) "Test if NOS <= TOS"
; ## "<="  contrib

xt_le:
        jsr underflow_2
w_le:
        jsr w_greater_than
        lda 0,x
        eor #$ff
        sta 0,x
        sta 1,x
z_le:
        rts


#nt_header ge, ">="

; ## GREATER_EQUAL ( n n -- f ) "Test if NOS >= TOS"
; ## ">="  contrib

xt_ge:
        jsr underflow_2
w_ge:
        jsr w_less_than
        lda 0,x
        eor #$ff
        sta 0,x
        sta 1,x
z_ge:
        rts

