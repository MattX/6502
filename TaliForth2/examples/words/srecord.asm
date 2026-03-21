;----------------------------------------------------------------------
; Read and write SREC memory dumps (S0/1/5/9 records)
;
; See https://en.wikipedia.org/wiki/SREC_(file_format)
;
;----------------------------------------------------------------------


.comment

We can import records like:

        srec> S0... S1... S1... S5... S9...

and export them like:
        $800 64 >srec

For example:

        s" Hello World!" >srec
        S0030000FC
        S10F080048656C6C6F20576F726C6421AB
        S5030001FB
        S9030800F4

        srec> S0030000FC S10F080048656C6C6F20576F726C6421AB S5030001FB S9030800F4
        .s <2> 2048 -1  ok

A typical S-record looks like:

Stnnhhll<dd x n-3>cc
nn: count including address (llhh), data and checksum
cc: checksum of count, address, data = (sum(vals) & $ff) ^ $ff

S0: header
S1: data (16 bit address)
S5: 'addr' is 16 bit record count (# of S1 records)
S9: termination, with entrypoint

.endcomment


s19chk = tmp2
s19mode = tmp2+1
s19len = tmp2+1


#nt_header to_srec, ">srec"

; ## TO_SREC ( addr u -- ) "Import sequence of srecords until terminator"
; ## ">srec"  contrib

xt_to_srec:
        jsr underflow_2
w_to_srec:
        jsr w_cr
        jsr w_two_dup           ; keep an original copy and one to adjust
        ; ( addr u addr u )

        stz tmp1
        stz tmp1+1

        ldy #0
        stz s19len
        jsr _s19record

        stz 4,x                 ; count number of data records
        stz 5,x
        ; ( addr n addr u )

_data:
        lda 1,x                 ; decide how many bytes to write
        bne _max
        lda 0,x
        beq _footer             ; no bytes left?
        cmp #$20                ; at most 32 bytes
        bcc +
_max:
        lda #$20
+
        ldy 2,x                 ; copy current data pointer
        sty tmp1
        ldy 3,x
        sty tmp1+1

        jsr w_zero
        sta 0,x
        ; ( addr n addr' u' k )
        sta s19len
        jsr w_slash_string      ; consume len bytes
        ; ( addr n addr'' u'' )

        ldy #1                  ; write a data record
        jsr _s19record

        inc 4,x                 ; count the records
        bne _data
        inc 5,x
        bra _data

_footer:
        jsr w_two_drop          ; drop consumed string

        lda #%1001_0101         ; S5 and then S9
        sta tmpdsp
        ; ( addr n )
_loop:
        lda tmpdsp
        beq _done

        tay
        lsr
        lsr
        lsr
        lsr
        sta tmpdsp

        ; copy TOS to the data pointer
        ; on first pass this is record count for S5,
        ; then the original address for S9
        lda 0,x
        sta tmp1
        lda 1,x
        sta tmp1+1
        jsr w_drop

        stz s19len
        jsr _s19record
        bra _loop

_done:
        rts

_s19record:
        lda #'S'
        jsr emit_a
        tya
        jsr nibble_to_ascii

        stz s19chk              ; initialize the checksum, starting from count byte

        lda s19len              ; write record length
        clc
        adc #3                  ; data length + 2 address bytes + 1 checksum byte
        jsr _s19emit

        ldy #1                  ; write two address bytes (big-endian order)
-
        lda tmp1,y
        jsr _s19emit
        dey
        bpl -

        ldy #0                  ; write any data bytes
-
        dec s19len
        bmi +
        lda (tmp1),y
        jsr _s19emit
        iny
        bra -
+
        eor #$ff                ; compute and write the checksum
        jsr byte_to_ascii
        jsr w_cr
        rts

_s19emit:
        pha
        jsr byte_to_ascii
        pla
        clc
        adc s19chk
        sta s19chk
z_to_srec:
        rts


#nt_header srec_from, "srec>"

; ## SREC_FROM ( addr u -- ) "Dump memory range to srecords"
; ## "srec>"  contrib

xt_srec_from:   ; ( -- xt f )
w_srec_from:
        jsr w_parse_name        ; ( addr n )
        lda 0,x
        ora 1,x
        bne +                   ; non-zero length?

        inx                     ; drop string
        inx
        inx
        inx

        jsr w_refill            ; fetch another line
        inx                     ; drop flag either way
        inx
        lda $fe,x               ; bail out if refill failed
        beq _fail
        bra w_srec_from         ; otherwise get next word for SREC string
+
        lda 2,x                 ; stash the string pointer for easy access
        sta tmp1                ; we'll overwrite NOS with each SREC address field
        lda 3,x
        sta tmp1+1

        lda 1,x
        bne _fail               ; max 256 char input
        lsr 0,x                 ; we'll count pairs of characters
        bcs _fail               ; but must start with an even number of them

        lda (tmp1)              ; record should start with S
        cmp #'S'
        bne _fail

        stz s19chk              ; initialize the checksum

        ldy #1
        lda (tmp1),y            ; S0 -> S9
        iny
        sbc #'0'                ; carry still set from cmp S
        sta s19mode             ; save the mode, specifically mode 9 sets bit 4 (%1001)
        beq _s0

        cmp #10                 ; we'll handle S0,S1,S5 and S9: 0, %1, %101, %1001
        bcs _fail               ; not 0-9?

        lsr                     ; others are odd, so check lsb leaving %0, %10, or %100
        bcc _fail

        beq _s1                 ; S1 (with C=1 from above)

        lsr                     ; otherwise check zero lsb again leaving %1 or %10
        bcs _fail

        dea
        beq _s5                 ; S5 (with C=0 from above)

        dea
        bne _fail

        ; otherwise fall through for S9
_s0:
_s5:
_s9:
        clc
_s1:
        ; s19mode currently has the mode value 0-9.  The carry flag tells us whether
        ; we want to write data (for S1) but not until we've read count and address
        rol s19mode             ; temporarily stash carry in lsb leaving %000f_xxxw

        dec 0,x                 ; count the 'S#' pair

        jsr _s19byte            ; read number of encoded bytes, n
        cmp 0,x                 ; should match remaining pairs characters
        bne _fail

        jsr _s19byte            ; read and store S19 address at NOS,
        sta 3,x                 ; note: big endian order
        jsr _s19byte
        sta 2,x

        ; now move stashed carry to indicate whether
        ; we're writing data (mode S1), leaving %w000_0fxx
        lsr s19mode
        ror s19mode
-
        lda 0,x
        dea
        beq _chk                ; last byte?
        jsr _s19byte            ; read, checksum and maybe write remaining bytes
        bra -
_chk:
        lsr s19mode             ; disable write for checksum byte, leaving %0w00_00fx
        jsr _s19byte
        inc s19chk              ; checksum including itself should be #$ff
        bne _fail

        lda s19mode             ; check if this is the S9 record via f bit
        lsr
        lsr
        bcs +                   ; if we're done, fall through with A=0 from the checksum

        inx
        inx
        inx
        inx
        jmp w_srec_from

_fail:
        lda #1
+
        dea                     ; 1 -> 0 for failure, 0 -> $ff for success
        sta 0,x
        sta 1,x
        rts


_s19byte:
    ; parse two characters as a byte, dec count of pairs;
    ; if s19mode bit 7 is set, the result is written to addr++
    ; while bit 6 is clear the result is accumulated to the running checksum
    ; Returns A=value, C=0 on success
    ; Failure breaks out to top-level failure.
        lda (tmp1),y
        iny
        phy
        pha
        lda (tmp1),y
        ply
        jsr ascii_to_byte
        ply
        iny
        bcc _ok

        pla                     ; drop this return and fail above
        pla
        bra _fail
_ok:
        dec 0,x                 ; consumed a pair

        ; check if we should write data
        bit s19mode
        bpl +
        sta (2,x)               ; write data if bit 7 set
        inc 2,x
        bne +
        inc 3,x
+
        pha                     ; update checksum
        adc s19chk              ; note C=0 from _ok
        sta s19chk
        pla
z_srec_from:
        rts

