;----------------------------------------------------------------------
; Some pseudo-random number routines
;----------------------------------------------------------------------

.section zp

rand16      .word ?

.endsection


; Kernel must initialize random seed to a non-zero word!
; A good way to increase randomness is to have kernel_getc or kernel_kbhit
; increment rand16 (avoiding zero!) or repeatedly call rng_798 while they're waiting.

random_seed:
        ; call with a non-zero value in A
        sta rand16
        stz rand16+1
        rts


#nt_header random

; ## RANDOM ( -- n ) "Return a non-zero random word"
; ## "random"  tested ad hoc

xt_random:
w_random:
        jsr rng_798
        dex
        dex
        lda rand16
        sta 0,x
        lda rand16+1
        sta 1,x
z_random:
        rts


rng_798:   ; (rand16) -> (a, rand16) const x, y
    ; randomize the non-zero two byte pair @ rand16 using a 16-bit xorshift generator
    ; on exit A contains the (random) high byte; X, Y are unchanged
    ; see https://en.wikipedia.org/wiki/Xorshift
    ; code adapted from https://codebase64.org/doku.php?id=base:16bit_xorshift_random_generator

    ; as a 16-bit value v (rand16) / (1, x), the calc is simply
    ;       v ^= v << 7;  v ^= v >> 9;  v ^= v << 8;
        lda rand16+1
        lsr             ; C = h0
        lda rand16
        ror             ; A is h0 l7..l1, C = l0
        eor rand16+1
        sta rand16+1    ; A is high part of v ^= v << 7, done
        ror             ; A now v >> 9 along with hi bit of lo byte of v << 7
        eor rand16
        sta rand16      ; both v ^= v >> 9 and low byte of v ^= v << 7 done
        eor rand16+1    ; A is v << 8
        sta rand16+1    ; v ^= v << 8 done
        rts


#nt_header randint

; ## RANDINT( n -- k ) "Return random unsigned k in [0, n) without modulo bias"
; ## "randint"  tested ad hoc
        ; Avoiding modulo bias is definitely overkill here...

xt_randint:
        jsr underflow_1
w_randint:
        txa                 ; set up stack for initial division
        sec
        sbc #6
        tax
        lda #$ff
        sta 5,x
        sta 4,x
        stz 3,x
        stz 2,x
        lda 7,x
        sta 1,x
        lda 6,x
        sta 0,x
        ; ( n {$ffff 0} n )
        jsr w_um_slash_mod         ; ( ud u -- rem quo )
        ; ( n rem quo )
_retry:
        jsr w_nip
        jsr w_over
        jsr w_random
        jsr w_one_minus            ; random is non-zero, so -1
        ; ( n quo n rand0 )
        jsr w_zero
        jsr w_rot
        ; ( n quo {rand0 0} n )
        ; use /mod to get the candidate remainder, but discard
        ; if the quotient rand0 // n == $ffff // n since not all
        ; potential results are equally represented at the tail end
        jsr w_um_slash_mod
        ; ( n quo rem quo' )
        jsr w_rot
        jsr w_tuck
        ; ( n rem quo quo' quo )
        inx                 ; 2drop and compare
        inx
        inx
        inx
        lda $fc,x
        cmp $fe,x
        bne _done
        lda $fd,x
        cmp $ff,x
        bne _done
        bra _retry
_done:
        ; ( n k quo )
        inx
        inx
        inx
        inx
        lda $fe,x
        sta 0,x
        lda $ff,x
        sta 1,x
z_randint:
        rts
