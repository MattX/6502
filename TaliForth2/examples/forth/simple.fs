\ Some simple Forth words for Tali Forth 2 for the 65c02
\ Scot W. Stevenson <scot.stevenson@gmail.com>
\ This version: 18. Dec 2018

\ Note that these programs are not necessarily in the public domain,
\ see the original sources for details

\ -------------------------------------------------------
\ FIBONACCI, contributed by leepivonka at
\ http://forum.6502.org/viewtopic.php?f=9&t=2926&start=90#p58899
\ Prints fibonacci numbers up to and including 28657

: fib ( -- ) 0 1 begin dup . swap over + dup 0< until 2drop ;

\ -------------------------------------------------------
\ FACTORIAL from
\ https://www.complang.tuwien.ac.at/forth/gforth/Docs-html/Recursion-Tutorial.html

: fact ( n -- n! )
    dup 0> if
    dup 1- recurse * else
    drop 1 then ;

\ -------------------------------------------------------
\ PRIMES from
\ https://www.youtube.com/watch?v=V5VGuNTrDL8 (Forth Freak)

: primes ( n -- )
    2 . 3 .
    2 swap 5 do
         dup dup * i < if 1+ then
          1 over 1+ 3 do
                j i mod 0= if 1- leave then
          2 +loop
          if i . then
    2 +loop
  drop ;

