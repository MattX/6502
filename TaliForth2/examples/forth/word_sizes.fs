-------------------------------------------------------
\ WORDS&SIZES prints all known words and the sizes of their codes
\ in bytes. It can be used to test the effects of different native
\ compile parameters.  It only displays words from the current
\ wordlist (usually FORTH-WORDLIST).

: previousnt
    dup  2 + @  over  c@ 1 and 0= if
        $ff and over $ff00 and or 2dup > 0= if 256 - then
    then nip ;

: words&sizes
    latestnt begin
        dup 0<> while
        dup name>string
        type space  dup wordsize u. cr
        previousnt
    repeat drop ;
