; Platform file for the mattbrew 6502 homebrew computer
;
; Netbooted via TLV protocol over Pi Pico bridge.
; Binary is loaded at $0400 and jumped to by the ROM bootloader.
;
; Memory map:
;   $0000-$007F  Zero page: Tali variables + data stack
;   $0080-$0091  Zero page: keyboard ring buffer (kernel)
;   $0100-$01FF  Return stack (hardware stack)
;   $0200-$02FF  Input buffer
;   $0400+       Tali code + kernel (this binary)
;   cp0+         Dictionary (grows upward to ram_end)
;   $9FFF        End of main RAM
;   $E040        Pi Pico bridge I/O port

        .cpu "65c02"
        .enc "none"

; =====================================================================
; Memory configuration

ram_end = $9FFF
cp0 = $3C00                     ; dictionary starts after code
                                ; must be past end of all assembled code
                                ; (assertion at bottom verifies this)

; =====================================================================
; I/O constants

RPI_PORT = $E040                 ; single-byte I/O port to Pi Pico bridge
DEV_STATUS   = 0                 ; device 0: status/availability
DEV_VIDEO_KB = 2                 ; device 2: video (write) / keyboard (read)

; Keyboard ring buffer in upper zero page
kb_head  = $80                   ; buffer read index (0-15)
kb_count = $81                   ; number of bytes in buffer
kb_buf   = $82                   ; 16-byte ring buffer ($82-$91)
KB_SIZE  = 16

; =====================================================================
; Entry point — netboot loads binary here and jumps to $0400

        * = $0400

; =====================================================================
; Build options (minimal for RAM conservation)

TALI_OPTIONAL_WORDS := [ ]
TALI_OPTION_CR_EOL := [ "lf" ]
TALI_OPTION_HISTORY := 0
TALI_OPTION_TERSE := 1
TALI_OPTION_MAX_COLS := 40       ; 40-column terminal

; =====================================================================
; Kernel routines

kernel_init:
        ; Called at entry. No hardware init needed (bootloader did it).
        ; Initialize keyboard buffer, print banner, start Forth.
                sei

                stz kb_head
                stz kb_count

                ldx #0
-               lda s_kernel_id,x
                beq _done
                jsr kernel_putc
                inx
                bra -
_done:
                jmp forth


kernel_bye:
        ; Exit Forth — jump through reset vector to restart bootloader
                jmp ($FFFC)


kernel_putc:
        ; Print character in A to video (Device 2).
        ; TLV write: [device=2] [len=1] [char]
        ; Preserves X and Y (only uses A and stack).
                pha
                lda #DEV_VIDEO_KB
                sta RPI_PORT
                lda #1
                sta RPI_PORT
                pla
                sta RPI_PORT
                rts


kernel_getc:
        ; Get one character from keyboard (Device 2), blocking.
        ; Returns character in A. Preserves X and Y.
        ;
        ; Uses a 16-byte ring buffer to handle multi-byte TLV responses
        ; (ANSI escape sequences, buffered keystrokes).
                phx
                phy

                ; Check if we have buffered characters
                lda kb_count
                bne _from_buf

                ; Buffer empty — do TLV read from Device 2
_read_device:
                lda #(DEV_VIDEO_KB | $80)
                sta RPI_PORT

                ; Poll until non-0xFF (Pico preparing response)
_poll:
                lda RPI_PORT
                cmp #$FF
                beq _poll

                ; A = length of response
                tay                     ; Y = length (also sets Z if len=0)
                beq _read_device        ; len=0: no data, retry

                ; Calculate write position: (head + count) mod 16
                ; At this point count is 0, so write starts at head
                ldx kb_head

_read_loop:
                lda RPI_PORT            ; read one byte from bridge
                sta kb_buf,x
                inc kb_count
                inx
                txa
                and #(KB_SIZE - 1)      ; wrap index mod 16
                tax
                dey
                bne _read_loop

                ; Fall through to return first buffered byte

_from_buf:
                ; Return byte at buf[head], advance head, decrement count
                ldx kb_head
                lda kb_buf,x
                pha                     ; save character
                inx
                txa
                and #(KB_SIZE - 1)
                sta kb_head
                dec kb_count
                pla                     ; restore character

                ply
                plx
                rts


kernel_kbhit:
        ; Check if a character is available. Returns non-zero in A if yes.
        ; Preserves X and Y.
                lda kb_count
                bne _has_key

                ; Buffer empty — check Device 0 status for pending keyboard data
                phx

                lda #(DEV_STATUS | $80)
                sta RPI_PORT
_poll_status:
                lda RPI_PORT
                cmp #$FF
                beq _poll_status

                ; A = length of status response
                tax                     ; X = bytes to read
                beq _no_data            ; shouldn't happen, but handle

                ; First byte: device availability bitmask
                lda RPI_PORT
                pha                     ; save bitmask
                dex

                ; Drain remaining status bytes
_drain:
                beq _drain_done
                lda RPI_PORT
                dex
                bra _drain

_drain_done:
                pla                     ; restore bitmask
                and #(1 << DEV_VIDEO_KB) ; bit 2 = keyboard has data
                plx
                rts

_no_data:
                lda #0
                plx
_has_key:
                rts


; Kernel identification string — displayed after successful boot

s_kernel_id:
        .text "Tali Forth 2 on mattbrew", AscLF, 0

; =====================================================================
; Include Tali Forth 2

.include "../../taliforth.asm"

; =====================================================================
; Verify that code fits below cp0.
; If this fails, increase cp0 above.

.cerror * > cp0, "Code exceeds cp0 — increase cp0 in memory configuration"

; No interrupt vectors — this is a RAM program.
; The ROM bootloader owns $FFFA-$FFFF.

; END
