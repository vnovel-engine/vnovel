; KiriKiri / KAG3 — text/choice layout probe.
;
; ENGINE IDENTIFIER: every line of text is prefixed "KRKR", and every page is a
; shade of RED. A screenshot alone says which engine and which case this is.
;
;   case 1  #440000      page 2A #770000      page 2B #AA0000
;   case 2  #CC0000      after 2 #EE0000      case 3  #330000
;   case 4  #550000      after 4 #660000
;
; Why this file looks different from the ONScripter/PONScripter ones: KAG3
; expresses a choice as INLINE TEXT —
;
;     [link target=*label]the choice text[endlink]
;
; — attaching a click target to ordinary message text, instead of ONScripter's
; modal `select` command which builds separate choice lines. So here the choice
; literally IS message text.
;
; Contrast: on ONScripter/PONScripter, `select` calls newPage(true) and so
; TERMINATES the page (verified in ONScripterLabel_command.cpp:1212 and :1247),
; which is why text can never continue below a choice there. KAG3 has no such
; rule, and this fixture is what shows the difference.
;
; THE THREE TAGS THIS FILE TURNS ON, and why they are not interchangeable:
;
;   [s]   execution stop: halt and wait for input. This is the one a CHOICE
;         needs. `[link]` on its own only makes text clickable — it does not
;         stop the script — so a link list with no [s] after it runs straight
;         on and the choice is never made.
;   [p]   page pause marker: halt at a page boundary. Correct at the end of a
;         page that has NO choice. Used after a [link] it pauses for a plain
;         click rather than a selection, which is not the same thing.
;   [cm]  clears the message layer, without resetting `current` the way [ct]
;         does (MainWindow.tjs:5013, clearMessageLayers(false)).
;
; [p] and [cm] are separable and the split is the point: [cm] alone clears
; without pausing (a scene scrolling several screens of text past at speed);
; [p] alone stops without clearing (a line held on screen). A page turn the
; reader controls is both. But a page whose ending is a CHOICE wants [s], not
; [p] — that is the distinction that matters here.
;
; [s] and [p] both leave the message layer alone. Neither clears it — so a page
; entered by falling through the [p] shows the previous page's text still on
; screen underneath unless something clears it, which is why every page label
; below begins with [cm].
;
; Where the clear belongs. A link JUMPS to its target label, skipping every tag
; between, so a [cm] written after the [s] is never reached when a choice is
; made. Putting [cm] at the HEAD of each label means every page is cleared
; however it was entered — fallen into, or jumped to.
;
; There is no `newpage` tag in this KAG build: no such handler exists in
; MainWindow.tjs. A page turn is exactly [p] (or [s]) plus [cm].
;
; `[r]` is a line break within a page; text after it continues on the next
; rendered line.
;
; Syntax notes, from the game's own scripts (not guessed):
;   - tags are written `[tag ...]`, commands `@tag ...`;
;   - choices are `[link target=*label]text[endlink]`, a line break is `[r]`;
;   - there is no colour form of [bg] — a plain colour page is a TJS fill via
;     `@eval exp="....fillRect(...)"`, which is how the game's own
;     CinemaScopePlugin.ks does it.

*start|スタート
[wait time=100]

; case 1: text, then a choice leading to one of two pages
@eval exp="kag.fore.base.fillRect(0, 0, kag.fore.base.width, kag.fore.base.height, 0xFF440000)"
KRKR CASE-1 line A: message text on a red page[r]
KRKR CASE-1 line B: then a choice follows[r]
[link target=*page_2a]KRKR CASE-1 choice: LEFT to page 2A[endlink][r]
[link target=*page_2b]KRKR CASE-1 choice: RIGHT to page 2B[endlink][s]

*page_2a
[cm]
@eval exp="kag.fore.base.fillRect(0, 0, kag.fore.base.width, kag.fore.base.height, 0xFF770000)"
KRKR PAGE-2A: the left branch of case 1[r]
[link target=*case2]KRKR CASE-1 result: continue to case 2[endlink][s]

*page_2b
[cm]
@eval exp="kag.fore.base.fillRect(0, 0, kag.fore.base.width, kag.fore.base.height, 0xFFAA0000)"
KRKR PAGE-2B: the right branch of case 1[r]
[link target=*case2]KRKR CASE-1 result: continue to case 2[endlink][s]

; case 2: text, a choice, then more text
*case2
[cm]
@eval exp="kag.fore.base.fillRect(0, 0, kag.fore.base.width, kag.fore.base.height, 0xFFCC0000)"
KRKR CASE-2 line A: message text before a choice[r]
KRKR CASE-2 choice in-line: [link target=*case2_after]go on, then read more text[endlink][s]

*case2_after
[cm]
@eval exp="kag.fore.base.fillRect(0, 0, kag.fore.base.width, kag.fore.base.height, 0xFFEE0000)"
KRKR CASE-2 after: did the choice end the page?[r]
KRKR CASE-2 after B: or does text continue below the choice?[r]
[link target=*case3]KRKR CASE-2 result: continue to case 3[endlink][s]

; case 3: a page that is only a choice
*case3
[cm]
@eval exp="kag.fore.base.fillRect(0, 0, kag.fore.base.width, kag.fore.base.height, 0xFF330000)"
[link target=*case3_a]KRKR CASE-3 choice A: this page has no message text[endlink][r]
[link target=*case3_b]KRKR CASE-3 choice B: nothing but choices here[endlink][s]

*case3_a
[cm]
[link target=*case4]KRKR CASE-3 result: from choice A, continue to case 4[endlink][s]
*case3_b
[cm]
[link target=*case4]KRKR CASE-3 result: from choice B, continue to case 4[endlink][s]

; case 4: a choice, then text
*case4
[cm]
@eval exp="kag.fore.base.fillRect(0, 0, kag.fore.base.width, kag.fore.base.height, 0xFF550000)"
KRKR CASE-4 choice in-line: [link target=*case4_after]pick this, then read text[endlink][s]

*case4_after
[cm]
@eval exp="kag.fore.base.fillRect(0, 0, kag.fore.base.width, kag.fore.base.height, 0xFF660000)"
KRKR CASE-4 after: text reached by taking a choice[r][p]

*end
[end]
