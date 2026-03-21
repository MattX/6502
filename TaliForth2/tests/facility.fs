\ ------------------------------------------------------------------------
testing facility words: at-xy page

T{ capture-output 13 21 at-xy restore-output s\" \e[22;14H" compare -> 0 }T
T{ capture-output page restore-output s\" \e[2J\e[1;1H" compare -> 0 }T
