;----------------------------------------------------------------------
; Some simple hash functions for strings
; - srl3 is a simple but fast 8-bit hash
; - djb2 is a 16-bit hash, e.g. see http://www.cse.yorku.ca/~oz/hash.html
; - adler32 is a 32-bit hash, e.g. see https://en.wikipedia.org/wiki/Adler-32
;----------------------------------------------------------------------

.comment
\ Lorem ipsum tests

hex
s" Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea commodo consequat. Duis aute irure dolor in reprehenderit in voluptate velit esse cillum dolore eu fugiat nulla pariatur. Excepteur sint occaecat cupidatat non proident, sunt in culpa qui officia deserunt mollit anim id est laborum."

\ adler32, validate with python zlib.adler32

over $ff adler32 ud.    \ 1B485E14
over $100 adler32 ud.   \ 79CA5E82
2dup adler32 ud.        \ A05CA509

\ djb2, validate with python code below

over $ff djb2 u.        \ 7518
over $100 djb2 u.       \ 1886
2dup djb2 u.            \ 96ED

\ srl3, validate with python code below

over $ff srl3 u.        \ 50
over $100 srl3 u.       \ F5
2dup srl3 u.            \ 46

.endcomment



#nt_header srl3

; ## SRL3 ( addr n -- c ) "Calculate fast 8-bit hash of non-empty string"
; ## "SRL3"  contrib

        ; The algorithm simply sums the bytes in the string, rotating the
        ; the sum three bits leftward after each step.  In python:

        ; def rol(x, k):   # circular rotate not through carry
        ;     x = (x & 0xff) << k
        ;     return (x & 0xff) | (x >> 8)
        ;
        ; def hash(xs):
        ;     h = 0
        ;     for x in xs:
        ;         h = rol(h + x, 3)
        ;     return h

xt_srl3:
                jsr underflow_2
w_srl3:
                ; ( addr n )
                jsr paged_string        ; set up for string iteration
                lda #0
                clc
_loop:
                adc (tmp1),y            ; add next byte from string
                ; roll the hash bits three places left
                ; transforming abcd_efgh => defg_habc
                ; Use a few tricks to cope with hardware shift through carry
                            ; abcd_efgh  ?   (carry unknown)
                asl         ; bcde_fgh0  a   (msb 'a' shifts into carry)
                adc #$80    ; Bcde_fgha  b   (adc trick: 'a' -> LSB and b -> carry with MSB = "not b")
                rol         ; cdef_ghab  B   (rol cycles in 'b' and drops the unneeded B -> carry)
                asl         ; defg_hab0  c   (discard B and get c -> carry)
                ; we'd normally do adc #0 to finish the circular 3 bit roll
                ; adc #0    ; defg habc  0   (adc to set the LSB, with 0 -> carry)
                ; but we'll pick up the carry in the next iteration

                iny                     ; increment the address and continue
                bne _loop
                inc tmp1+1              ; next page
                dec tmpdsp
                bne _loop

                adc #0                  ; pick up the final carry bit
                jmp push_a_tos          ; return the result
                ; ( hash )
z_srl3:



#nt_header djb2

; ## DJB2 ( addr n -- hash ) "Calculate a 16-bit DJB2 hash of string"
; ## "djb2"  contrib

        ; The 16 bit version of this hash is implemented as:
        ;
        ; def djb2(s):
        ;    v = 5381
        ;    for c in s:
        ;        v = v*33 + ord(c)
        ;        v &= 0xffff
        ;    return v

xt_djb2:
                jsr underflow_2
w_djb2:
                ; ( addr n )
                jsr paged_string        ; set up for string iteration

                dex
                dex
                lda #<5381              ; set initial hash value
                sta 0,x
                lda #>5381
                sta 1,x

                ; ( hash ), tmp1 has adj addr, tmpdsp has # pages, Y set for iteration

_loop:
                jsr w_dup
                ; ( hash hash )

                ; multiply TOS by 32, aka left shift 5
                ; if we have bit pattern ABCD EFGH for MSB and abcd efgh for LSB
                ; then we want a result where MSB is FGHa bcde and LSB is fgh0 0000
                ; But it's faster to right-shift by 3 and then left shift a whole byte

                lda #%0000_0011         ; count three iterations with low bits
-
                lsr 1,x                 ; MSB becomes 0ABC DEFG  C=H
                ror 0,x                 ; LSB becomes Habc defg  C=h
                ror A                   ; Acc becomes h000 0001  C=0 on last iter
                bcs -

                ; finally we have:
                ;       MSB = 000A DEFG
                ;       LSB = FGHa bcde
                ;       A   = fgh0 0000
                ; so move LSB to MSB and A to LSB to complete multiply by 32
                ; but while carry clear and A has the LSB, first add the next char
                adc (tmp1),y
                pha
                lda 0,x                 ; the current LSB will be our new MSB
                bcc +
                ina                     ; handle carry from the addition
+
                sta 1,x                 ; write the new MSB
                pla
                sta 0,x                 ; and the new LSB
                ; ( hash hash*32+c )

                jsr w_plus
                ; ( hash*33+c )

                iny                     ; increment the address and continue
                bne _loop
                inc tmp1+1              ; next page
                dec tmpdsp
                bne _loop

z_djb2:
                rts



#nt_header adler32

; ## ADLER32 ( addr n -- lo hi ) "Calculate a 32-bit ADLER32 hash of non-empty string"
; ## "adler32"  contrib

        ; pseudo-code for the algorithm uses prime p = 65521 = $fff1:
        ;       adler = 1
        ;       adler2 = 0
        ;       for c in s:
        ;               adler += c mod p
        ;               adler2 += adler mod p
        ;       return adler2 << 16 | adler

xt_adler32:
                jsr underflow_2
w_adler32:
        ; the final result should leave ( adler adler2 ) on the stack
        ; representing the double word adler2 << 16 | adler in the usual NUXI order
        ; but we'll work internally with ( adler2 adler ) to make it
        ; easier to abuse the "+" word, and just swap at the end.

                jsr paged_string
                jsr w_zero              ; initialize ( adler2 adler ) as ( 0 1 )
                jsr w_one
_loop:
                clc
                lda (tmp1),y
                adc 0,x
                sta 0,x
                lda 1,x         ; get MSB for next check
                bcc +           ; no carry from LSB
                adc #0          ; add carry to MSB, setting carry on overflow
                sta 1,x
+
                jsr maybe_mod_fff1      ; A=MSB, C=overflow

                ; ( adler2 adler' )
                ; add TOS to NOS
                jsr w_plus      ; preserves carry, leaves A=MSB, and leaves adler' past TOS
                jsr maybe_mod_fff1
                ; ( adler2' )
                dex             ; restore adler' to the stack
                dex
                ; ( adler2' adler' )

                iny
                bne _loop
                inc tmp1+1      ; next page
                dec tmpdsp
                bne _loop

                jmp w_swap
                ; ( adler adler2 )
z_adler32:


maybe_mod_fff1:
                ; subtracts $fff1 from TOS if C set or TOS >= $fff1
                ; enter with A=MSB, C=overflow on word addition
                bcs _adjust     ; addition wrapped so need subtraction

                ; otherwise subtract if word >= $fff1
                ina             ; MSB = $ff ?
                bne _done       ; no adjustment needed
                lda 0,x         ; is word >= $fff1 ?
                cmp #$f1
                bcc _done
_adjust:
                ; subtract p=$fff1 from TOS
                ; this is equivalent to adding $f (ignoring overflow)
                ; since we arrive with C=1 we can use adc #$e
                lda #$e
                adc 0,x
                sta 0,x
                bcc _done
                inc 1,x
_done:
                rts


paged_string:           ; ( addr u -- )
        ; prepare to iterate over a string by pages indexed by Y
        ; if the string length isn't a whole number of 256 byte pages
        ; we'll adjust addr downward and set Y > 0 so that we can
        ; iterate the first partial page from Y... until 0, and then any
        ; remaining full pages are indexed with Y from 0... until 0.
        ;
        ; let k be the LSB of u, the size of the partial page. then:
        ; - set tmp1 = addr - (256 - k);
        ; - set tmpdsp = MSB of u (page count), plus one if k is non-zero (partial page)
        ; - set Y = 256 - k so that (tmp1),y is the first byte of the string
        ;
        ; then we can iterate over the string by incrementing Y in an inner loop
        ; until it wraps to zero, with an outer loop incrementing tmp1+1
        ; and decrementing tmpdsp until it hits zero.

                lda 1,x
                sta tmpdsp      ; MSB gives full page count
                lda 0,x         ; LSB
                beq _full       ; if 0 we only have full pages

                ; usually we need to put (256-n)%256 in Y and add a page
                inc tmpdsp      ; add a partial page

                eor #$ff
                inc a
                tay             ; Y = 1 + 255 - n%256
                sta 0,x
                stz 1,x         ; ( addr 256-n%256 )
                jsr w_minus
_addr:
                lda 0,x         ; save updated address in tmp1
                sta tmp1
                lda 1,x
                sta tmp1+1
                jmp w_drop      ; drop addr

_full:
                inx             ; drop n
                inx
                tay
                bra _addr
