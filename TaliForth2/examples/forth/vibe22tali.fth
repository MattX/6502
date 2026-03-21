( Original source at: http://tinyurl.com/vibe22)
( VIBE Release 2.2 )
( Copyright {c} 2001-2016 Samuel A. Falvo II )
( All Rights Reserved. )
( Modified to run on Tali Forth 2024-02 )
( benchapman@fastmail.com )
( BJC: per communication with author 1/24, license should be)
(  considered MPL v2. )

( This Source Code Form is subject to the terms of the Mozilla)
( Public License, v. 2.0. If a copy of the MPL was not)
( distributed with this file, You can obtain)
( one at https://mozilla.org/MPL/2.0/.)
( Highly portable block editor )
( -- works under nearly every ANS Forth)
( I can think of, and with only a single screenful of words,)
( will work under Pygmy and FS/Forth too. )
( USAGE: vibe  n --  Edits block 'n'.  )
(        Sets SCR variable to 'n'.)
(        vi  --  From ed in Pygmy.  Re-edits last edited )
(        block. BJC renamed ed to vi b/c of conflict with )
(        Tali line editor )
( I use CREATE instead of VARIABLE because I can statically)
( initialize the variables at load-time with no overhead.)  
( Stole this idea from a7r in the #Forth IRC channel. )

( 2.2 -- Reworked how O works; added x and $, which behave as)
(  they do in VIM in command-mode.)
(  D now deletes a line in-place.)

( 2.1 -- Fixed stack overflow bugs; forgot to DROP in the)
(  non-default key handlers. )

( BJC adds for Tali Forth )
( BJC: all comments converted to parens. )
(      Tali does not support slash comments in blocks. )
marker *vibe*
char 0 constant '0
: not 0= ; allow-native
: >= < not ; allow-native
: <= > not ; allow-native
: u>= u< not ; allow-native
: u<= u> not ; allow-native
    
( Editor Constants )
CHAR i CONSTANT 'i   ( Insert mode )
CHAR r CONSTANT 'r   ( Replace mode )
CHAR c CONSTANT 'c   ( Command mode )

CHAR y CONSTANT 'y
CHAR n CONSTANT 'n

CHAR A CONSTANT 'A
CHAR Z CONSTANT 'Z
CHAR $ CONSTANT '$

( Editor State )
 1 scr !   	  ( Current block )
 0 CREATE x ,     ( Cursor X position 0..63 )
 0 CREATE y ,     ( Cursor Y position 0..15 )
'c CREATE mode ,  ( Change to bitmap later. )

( GForth-specific )
CREATE wordname 5 C, '$ C, '$ C, 0 C, 0 C, 0 C,

( Editor Display )
: mode. 63 0 AT-XY mode @ EMIT ;
: scr. 0 0 AT-XY ." Block: " scr @ . ."      " ;
: header scr. mode. ;
: 8-s ." --------" ;
: 64-s 8-s 8-s 8-s 8-s 8-s 8-s 8-s 8-s ;
: border SPACE 64-s CR ;
: row DUP 64 TYPE 64 + ;
: 1line ." |" row ." |" CR ;
: 4lines 1line 1line 1line 1line ;
: 16lines scr @ BLOCK 4lines 4lines 4lines 4lines DROP ;
: card 0 1 AT-XY border 16lines border ;
: cursor x @ 1+ y @ 2 + AT-XY ;
: screen header card cursor ;

( Editor State Control )
: insert 'i mode ! ;
: replace 'r mode ! ;
: cmd 'c mode ! ;
: 0bounds scr @ 0 MAX 32767 MIN scr ! ;
: prevblock -2 scr +! 0bounds ;
: nextblock  2 scr +! 0bounds ;
: toggleshadow 1 scr @ XOR scr ! ;

( Editor Cursor Control )
: 64* 2* 2* 2* 2* 2* 2* ;
: where scr @ BLOCK SWAP 64* + SWAP + ;
: wh x @ y @ where ;
: -space? 33 u>= ;
: flushLeft 0 x ! ;
: seekLeft begin x @ 0= if exit then wh c@ -space? 
    if exit then -1 x +! again ;
: flushRight 63 x ! seekLeft ;
: boundX x @ 0 MAX 63 MIN x ! ;
: boundY y @ 0 MAX 15 MIN y ! ;
: 1bounds boundX boundY ;
: left -1 x +! 1bounds ;
: right 1 x +! 1bounds ;
: up -1 y +! 1bounds ;
: down 1 y +! 1bounds ;
( fix by SamCoVT for stack issue with invalid cmds )
: beep drop 7 EMIT ;
: nextline y @ 15 < IF flushLeft down THEN ;
: next x @ 63 = IF nextline EXIT THEN right ;

( Editor Insert/Replace Text )
: curln 0 y @ where ;
: eol 63 y @ where ;
: place wh C! UPDATE next ;
: -eol? x @ 63 < ;
: openr wh DUP 1+ 63 x @ - MOVE ;
: openRight -eol? IF openr THEN ;
: inserting? mode @ 'i = ;
: chr inserting? IF openRight THEN place ;

( Editor Commands: Quit, cursor, block, et. al. )
: $$c24 DROP flushright ;            ( $ )
: $$c30 DROP flushLeft ;             ( 0 )
: $$c49 DROP flushLeft insert ;      ( I )
: $$c51 DROP 0 20 AT-XY R> R> DROP >R ; ( Q )
                    ( -- quits main loop )
: $$c52 DROP replace ;               ( R )
: $$c5B DROP prevblock ;             ( [ )
: $$c5C DROP toggleshadow ;          ( \ )
: $$c5D DROP nextblock ;             ( ] )
: $$c68 DROP left ;                  ( h )
: $$c69 DROP insert ;                ( i )
: $$c6A DROP down ;                  ( j )
: $$c6B DROP up ;                    ( k )
: $$c6C DROP right ;                 ( l )
: $$i1B DROP cmd ;                   ( escape )

( Editor Backspace/Delete )
: padding 32 eol C! UPDATE ;
: del wh DUP 1+ SWAP 63 x @ - MOVE UPDATE ;
: delete -eol? IF del THEN padding ;
: bs left delete ;
: backspace x @ 0 > IF bs THEN ;
: nextln eol 1+ ;
: #chrs scr @ BLOCK 1024 + nextln - ;
: delline nextln curln #chrs MOVE  0 15 where 64 32 FILL ;

( Editor Carriage Return )
: copydown y @ 14 < IF nextln DUP 64 + #chrs 64 - MOVE THEN ;
: blankdown nextln 64 32 FILL UPDATE ;
: splitdown wh nextln 2DUP SWAP - MOVE ;
: blankrest wh nextln OVER - 32 FILL ;
: opendown copydown blankdown ;
: splitline opendown splitdown blankrest ;
: retrn inserting? IF splitline THEN flushleft nextline ;
: return y @ 15 < IF retrn THEN ;
: vacate flushleft splitline ;

( Editor Wipe Block )
: msg 0 20 AT-XY ." Are you sure? (Y/N) " ;
: valid? DUP 'n = OVER 'y = OR ;
: uppercase? DUP 'A 'Z 1+ WITHIN ;
: lowercase DUP uppercase? IF 32 XOR THEN ;
: validkey BEGIN KEY lowercase valid? UNTIL ;
: clrmsg 0 20 AT-XY 64 SPACES ;
: no? msg validkey clrmsg 'n = ;
: ?confirm no? IF R> DROP THEN ;
: wipe ?confirm scr @ BLOCK 1024 32 FILL UPDATE 0 x ! 0 y ! ;

( Editor Commands: backspace, delete, et. al. )
: $$c44 DROP delline ;                   ( D )
: $$c4F DROP vacate insert ;             ( O )
: $$c5A DROP wipe ;                      ( Z )
: $$c6F opendown down $$c49 ;            ( o )
: $$c78 DROP delete ;                    ( x )
: $$i04 DROP delete ;                    ( CTRL-D )
: $$i08 DROP backspace ;                 ( bs )
: $$i0D DROP return ;                    ( cr )
: $$i7F DROP backspace ;                 ( DEL -- for Unix )

( Editor Keyboard Handler )
( Word name key: $ $ _ _ _ )
(                    | | | )
( c = command mode --+ | | )
( i = ins/repl mode    | | )
(                      | | )
( Key code hex#.  -----+-+ )
( Called with  k -- where k is the ASCII key code. )
: keyboard KEY ;
: cmd? mode @ 'c = ;
: ins? mode @ 'i = mode @ 'r = OR ;
: mode! ins? 'i AND cmd? 'c AND OR wordname 3 + C! ;
: >hex DUP 9 > IF 7 + THEN '0 + ;
: h! DUP 240 AND 2/ 2/ 2/ 2/ >hex wordname 4 + C! ;
: l! 15 AND >hex wordname 5 + C! ;
: name! mode! h! l! ;
: nomapping DROP ['] beep cmd? AND ['] chr ins? AND OR ;
: handlerword name! wordname FIND IF ELSE nomapping THEN ;
: handler DUP handlerword EXECUTE ;
: editor BEGIN keyboard handler screen AGAIN ;
: vi page screen editor ;
: vibe scr ! vi ;

