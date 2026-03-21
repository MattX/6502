
;----------------------------------------------------------------------
; block extensions
;----------------------------------------------------------------------

blk_loader = $400

.section zp
blk_rw  .byte ?                 ; 0 is read, 1 is write
blk_n   .byte ?                 ; number of blocks to read/write
.endsection


#nt_header block_write_n, "block-write-n"

; ## BLOCK-WRITE-N ( addr blk n -- ) "Loop over block-write n times (n <= 64)"
; ## "block-write-n"  contrib

xt_block_write_n:
        jsr underflow_3
w_block_write_n:
        sec
        bra blk_rw_n_common


#nt_header block_read_n, "block-read-n"

; ## BLOCK-READ-N ( addr blk n -- ) "Loop over block-read n times (n <= 64)"
; ## "block-read-n"  contrib

xt_block_read_n:
        jsr underflow_3
w_block_read_n:
        clc
blk_rw_n_common:
        stz blk_rw
        rol blk_rw

        lda 0,x
        sta blk_n               ; block count (unsigned byte)
        inx                     ; remove n from stack
        inx
        cmp #0                  ; any blocks to read?
        beq _cleanup

_loop:
        jsr w_two_dup           ; ( addr blk addr blk )
        ldy blk_rw
        beq _rd
        jsr w_block_write
        bra +
_rd:
        jsr w_block_read
+
        lda #4                  ; addr += 1024 = $400
        clc
        adc 3,x
        sta 3,x

        inc 0,x                 ; blk += 1
        bne +
        inc 1,x
+
        dec blk_n               ; n--
        bne _loop

_cleanup:
        jmp w_two_drop
z_block_read_n:
z_block_write_n:



#nt_header block_boot, "block-boot"

; ## BLOCK-BOOT ( -- ) "Loop over block-write n times (n <= 64)"
; ## "block-boot"  contrib

xt_block_boot:
w_block_boot:
.if TALI_ARCH == "c65"
        jsr w_block_c65_init
.else
        jsr w_block_sd_init
.endif

        inx                     ; pre-drop result
        inx
        lda $fe,x
        beq sd_eblkini

        dex
        dex
        lda #<blk_loader
        sta 0,x
        lda #>blk_loader
        sta 1,x
        jsr w_zero
        jsr w_block_read        ; <blk_loader> 0 block-read

        ; valid boot block looks like TF<length16><code...>
        lda blk_loader
        cmp #'T'
        bne sd_ebadblk
        lda blk_loader+1
        cmp #'F'
        bne sd_ebadblk

        dex
        dex
        dex
        dex
        lda #<blk_loader+4
        sta 2,x
        lda #>blk_loader+4
        sta 3,x
        lda blk_loader+2
        sta 0,x
        lda blk_loader+3
        sta 1,x
        jsr w_evaluate          ; code should produce an XT
        jmp w_execute           ; run it
z_block_boot:


sd_ebadblk:
        lda #<es_badblk
        ldy #>es_badblk
        bra +
sd_eblkini:
        lda #<es_blkini
        ldy #>es_blkini
        bra +
sd_enocard:
        lda #<es_nocard
        ldy #>es_nocard
+
        sta tmp3
        sty tmp3+1
        jsr print_shift_string
        jmp w_cr

es_blkini:
        .shift "EBLKINI"
es_badblk:
        .shift "EBADBLK"
es_nocard:
        .shift "ENOCARD"

