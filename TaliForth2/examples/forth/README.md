## Forth examples

1. **vibe22tali.fth**: this is a version of Samuel Falvo's [VIBE editor]( http://tinyurl.com/vibe22)
   that's been modified to work with Tali Forth 2. It's made available
   under the Mozilla version 2 license. It is a vi-inspired modal editor.
   
   It supports the following keystrokes:

   Mode Commands | Keystroke 
   -----|-----------
   Command mode| c
   Insert mode (at cursor location)| i
   Insert mode (Beginning of line)| I
   Replace (overwrite) mode| R
   Return to command mode| Esc


   Movement commands | Keystroke
   --------| ---------
   left     | h
   right    | l
   down     | j
   up       | k
   end of line | $
   start of line | 0
   Go 2 blocks forward | ]
   Go 2 blocks back | [
   toggle current and next block | \ 

   Edit commands |Keystroke
   --------|---------
   Delete line | D
   Insert line above| O
   Insert line below| o
   Wipe block| Z
   Exit and save| Q

Characters can be deleted with Ctl-D, backspace, or DEL, depending on your terminal. 

**Additional notes** There is no way to exit without saving edits. Also, be careful with the wipe block command. Although it asks for confirmation, it will completely erase the block if you tell it to do so. Also, because of the way that the editor code redraws the screen, screens may update a bit slowly. Be patient. Finally, there is currently no way to cut/copy/paste.




