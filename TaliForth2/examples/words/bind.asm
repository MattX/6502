;----------------------------------------------------------------------
; bind native assembly subroutines to forth words
;----------------------------------------------------------------------


#nt_header bind, "bind", CO+IM

; ## BIND ( addr "<picture>" -- xt | 0 ) "Bind a native assembly subroutine to the Data Stack"
; ## "bind"  contrib
        ; addr is a native subroutine expecting inputs and outputs in CPU registers
        ; <picture> is a string like "XY-AP" showing how to bind register inputs and outputs to
        ; words on the data stack.
        ; For example the Forth definition
        ;       : foo [ $1234 ] bind a-x ;
        ; creates a word "foo" that pulls (the LSB of) TOS to the A register; calls JSR $1234;
        ; and pushes the X register back to the data stack.
        ; Legal registers are AXYS with at least one hyphen separating inputs and outputs (even if empty).
        ; Other valid picture examples: "-A", "X-", "Y-P"
        ; Invalid pictures: "A - X" (no spaces), "A" (no hyphen), "AA-X" (no duplication), "AB-C" (only AXYP)

xt_bind:   ; ( tgt -- )
w_bind:

.comment

First convert the stack picture to a mapping of registers to data stack offsets.
For example the picture "xy-ap" becomes the mapping:

     in  out
    AXYP AXYP
    -10- 1--0

This makes it easy to generate code that pulls
data stack entries to the CPU stack and then into registers
before calling the native code and reversing the process.

The mapping 'xy-ap' generates code like this:

    ; scan the mapping right to left so P goes on the stack first
    lda 0,x     ; put Y on the cpu stack
    pha
    lda 2,x     ; put X on the cpu stack
    pha
    stx tmpdsp  ; stash stack pointer
    ; scan the mapping from left to right so P comes off the stack last
    plx
    ply
    jsr native_code
    ; scan the mapping from left to right (order is arbitrary here)
    pha
    php
    ; recover data stack pointer adjusted with net stack effect
    lda tmpdsp
    clc
    adc #0      ; 2 * (#in - #out)
    tax
    ; scan the mapping from right to left (opposite to the prior scan)
    pla
    sta 0,x     ; bind P return value
    stz 1,x
    pla
    sta 2,x     ; bind A return value
    stz 3,x

.endcomment

        ; local variables
_picture = tmp1
_regmap = scratch               ; 8 bytes
_regmap_offset = tmp2
_stack_depth = tmp2+1
_param = tmp2                   ; template parameter

        jsr w_parse_name        ; ( tgt addr n )

        lda 2,x
        sta _picture            ; store address of first hyphen
        lda 3,x
        sta _picture+1

        ; clear the register mapping by marking all with negative flag
        ldy #7
        lda #$80
-
        sta _regmap,y
        dey
        bpl -

        lda #4                  ; first map outputs in _regmap+4..7
        sta _regmap_offset
        stz _stack_depth

        ; process stack picture in reverse order
        ldy 0,x                 ; string length
        beq _fail
        dey                     ; start with last character

        jsr w_two_drop          ; drop the string

        stx tmpdsp              ; stash data stack pointer

_map_loop:
        lda (_picture),y
        cmp #'-'
        bne +
        stz _regmap_offset      ; switch to mapping inputs
        stz _stack_depth        ; reset stack depth
        bra _map_next
+
        ora #$20                ; downcase
        ldx #3                  ; look for register name
-
        cmp _s_reg,x
        beq +
        dex
        bpl -
_fail:
        lda #err_noname         ; fail on unrecognized character
        jmp error
+
        txa                     ; found register index 0..3
        ora _regmap_offset      ; input (+0) or output (+4)
        tax

        lda _stack_depth        ; store register stack depth in the map
        asl
        sta _regmap,x
        inc _stack_depth
_map_next:
        dey
        bpl _map_loop

        ; ----------------   code generation  ----------------

        ; generate data stack to cpu stack code

        ldx #_template_in
        ldy #3
        jsr _compile_in_out

        ; emit STX tmpdsp
        lda #$86
        ldy #tmpdsp
        jsr cmpl_word_ya

        ; generate register pull, native call, register push
        ldy #0
        ldx #0                  ; X tracks net stack effect
-
        lda _regmap,y           ; is this register mapped?
        bmi +

        lda _op_reg,y
        jsr cmpl_a
        dex                     ; count registers used
+
        iny
        cpy #4
        bne +

        ; half-way point, flip stack effects and compile JSR target

        txa                     ; flip sign of stack effect
        eor #$ff
        ina
        pha                     ; so - #inputs => + #inputs

        phy

        ; emit JSR target leaving compile stack empty ( )
        ldx tmpdsp
        jsr cmpl_call_tos
        stx tmpdsp

        ply
        plx
+
        cpy #8
        bne -

        ; net stack effect is  2 * (# inputs - outputs)
        txa
        asl
        sta _param              ; save as placeholder

        ldx #_template_lddsp
        jsr _compile_template

        ; generate cpu stack to data stack code
        ldx #_template_out
        ldy #7
        jsr _compile_in_out

        ldx tmpdsp
        rts

_compile_in_out:
        ; scan input or output map from right to left to generate data stack <=> cpu stack code
        ; call with y/x = 3/#_template_in for inputs, 7/#_template_out for outputs
-
        lda _regmap,y
        bmi _cmpl_skip
        phx                     ; remember template offset
        sta _param
        jsr _compile_template
        plx
_cmpl_skip:
        tya                     ; stop if Y=0 or 4
        dey
        and #3
        bne -
        rts


_compile_template:
    ; copy a template from templates+x ... .byte 0
    ; fill placeholder value(s) from stack
-
        lda _templates,x
        beq _tmpl_done          ; end of template?
        cmp #$ff                ; placeholder?
        bne +
        lda _param
        inc _param              ; for output which needs <depth>, <depth+1>
+
        jsr cmpl_a
        inx
        bra -
_tmpl_done:
        rts


_s_reg:
        .text 'axyp'
_op_reg:
        pla
        plx
        ply
        plp
        pha
        phx
        phy
        php

_templates:
_template_in = * - _templates
        lda $ff,x               ; copy from data stack offset (placeholder) to cpu stack
        pha
        brk                     ; end of template
_template_lddsp = * - _templates
        ; recover data stack pointer adjusted with net stack effect
        lda tmpdsp
        clc
        adc #$ff                ; placeholder for 2 * (#in - #out)
        tax
        brk
_template_out = * - _templates
        pla
        sta $ff,x               ; put register value on stack (two placeholders)
        stz $ff,x
        brk

z_bind:

