# Keys

<!-- Written from src/app/help_text.cpp by the GUI test (run it with VCUBE_WRITE_DOCS=1): edit the table, not this file. -->

Every keyboard control, mode by mode. In the app, `F1` shows the same controls in a panel beside the page, the current mode's first, with your own turning keys. The [user guide](user-guide.md) says more about what each mode does.

A key only reaches the mode's page when no text field or dialog has the keyboard. A search box or count field gives the keys back with Enter (or Esc).

## Window

| Key | Action |
| --- | --- |
| Ctrl+1 … Ctrl+9, Ctrl+- | Learn, Drill, Library, Alg Lab, Timed, Untimed, Move Count, EO Cross, F2L, TSLE, in the sidebar's order (not while a solve or inspection is running) |
| Ctrl+0 | Today |
| Ctrl+K | command list: type part of any menu command or of a choice on the page (alg set, session, scramble type, cube, view, ...), then Enter |
| F1 | show or hide the keyboard controls |
| F9 | show or hide the mode's side panel (the case list, the session or the case browser; hidden by itself while the window is narrow) |
| F11 | full screen |
| F10 | focus mode (right-click outside the scramble, timer, cube and algs to switch it, full screen or the sidebar with the mouse) |
| Ctrl+B | show or hide the sidebar |
| Ctrl+, | settings |
| Ctrl+Shift+N | colour neutrality settings |
| Ctrl+Q | quit (asks first during a solve) |

## Turning keys

The virtual cube (Learn, Drill on the virtual cube, the Solves modes' virtual sessions, EO Cross, F2L, TSLE, Alg Lab's virtual test sessions) is turned with cstimer's face keys. Shift and a face key turns the wide layer, e.g. Shift+I is `r` and Shift+G is `f'`. Remap them in Settings > Cube and keys: select a row and **Change** its key or move, or **Add** one. A turning key may be held with Shift but not Ctrl or Alt; the digits, arrows, function keys and the modes' own keys can't be bound.

| Key | Move | Key | Move | Key | Move | Key | Move |
| --- | --- | --- | --- | --- | --- | --- | --- |
| I | R | Shift+I | r | K | R' | Shift+K | r' |
| D | L | Shift+D | l | E | L' | Shift+E | l' |
| J | U | Shift+J | u | F | U' | Shift+F | u' |
| S | D | Shift+S | d | L | D' | Shift+L | d' |
| H | F | Shift+H | f | G | F' | Shift+G | f' |
| W | B | Shift+W | b | O | B' | Shift+O | b' |
| U | M' | M | M | R | x | V | x' |
| ; | y | A | y' | P | z | Q | z' |

Before a solve starts, the cube only turns as a whole (rotations), so that a slip can't turn a face early.

## Learn

| When | Key | Action |
| --- | --- | --- |
| ready | Space | show the first case; it is timed from then (on opening Learn or coming back to it, after picking a set, a way of practising or a case with the mouse, and when a case falls due) |
| solving | turning keys | turn the cube; solving the case finishes it |
| solving | Tab | show the algs (grades the case Again); in the learning steps of a guided set, the first Tab shows only the next move (Hard) |
| solving | Shift+Space | give up (grades the case Again). Space alone does nothing, so a double tap after a case can't give up the next one. |
| solving | Esc | reset the case (grades it Again once the alg is begun; in a set solved a step at a time, back to where the current case came up) |
| after a case | Space / Enter | next case |
| after a case | 1 / 2 / 3 / 4 | change the grade to Again / Hard / Good / Easy |
| after a case | Esc | reset the cube to practise the case |
| any time | Ctrl+Z | take back the last grade (up to 10) and do the case again |
| walkthrough | → / ← | step through the main alg / back |
| walkthrough | Home or Esc | back to the start |
| walkthrough | Space / Enter | do the case, with each next move shown |
| nothing due | Space / Enter | look again; takes the next learning case early when that is all there is |
| Recognise | Space | recalled the alg |
| Recognise | Shift+Space | can't recall it (the algs are shown) |
| Recognise, afterwards | 1-4 / Space or Enter | grade and go on / go on with the grade shown |
| Which case? | 1-6 | answer |
| Which case?, afterwards | Space / Enter | next case |
| Recognise, Which case? | turning keys | rotations and U and D turns, to look at the case |
| Recognise, Which case? | Esc | reset the cube |
| in context | turning keys | turn the cube |
| in context | Tab | show the set's cheapest order |
| in context | Esc | start again |
| in context | Shift+Space | give up |
| in context, afterwards | Space / Enter | another one |
| in context, afterwards | Esc | the same one again |
| in context | Backspace | back to learning |
| count field (next to Add new cases) | digits, then Enter | start learning that many cases |

## Drill

| When | Key | Action |
| --- | --- | --- |
| virtual, waiting | tap Space, then turn | arm the timer; the first face turn starts it (Settings > Timing and solves can have Space held and let go instead; a turn made while Space is held and green starts it too) |
| virtual, waiting | turning keys | rotations only until the timer starts |
| physical, waiting | hold Space, release | start, once the time turns green (Drill's own hold time, Settings > Timing and solves) |
| waiting | Esc | disarm the timer and set the case up as it was |
| waiting, running | Shift+Space | give the case up (a DNF). Settings > Drill can make Backspace the give-up key instead. |
| waiting | Backspace | says which key gives up (with Shift+Space as the give-up key) |
| virtual, running | turning keys | any turn; the timer stops by itself when the step is done (unless turned off in Settings > Timing and solves) |
| virtual, running | Space | stop (a DNF unless the step is done; before any face turn, the attempt is cancelled) |
| physical, running | any key but Shift, Ctrl or Alt | stop |
| running | Esc | cancel (nothing is recorded) |
| after a case | Space / Enter | next case. With "Space practises the case" after a DNF (Settings > Drill), Space practises and Enter goes on. |
| after a case | Tab | practise the case, with the algs shown |
| after a case | Backspace | toggle DNF (virtual) or "wrong alg" (physical); not for a case given up |
| after a case | Delete or Ctrl+Z | delete the attempt; one given up before the timer started is taken back and the case set up again |
| after a case | Esc | try the case again |
| practice | Space / Enter | next case |
| practice | Tab | switch between with the algs and without them |
| practice | turning keys | virtual: solve the case; each stage goes on by itself once solved (Settings > Drill) |
| practice | Esc | virtual: set the case up again for this stage |
| practice | Backspace | toggle DNF or "wrong alg" on the attempt |
| practice | Delete or Ctrl+Z | delete the attempt |

For about 0.3 s after the timer stops, keys are ignored, so that a second press doesn't skip the result.

## Library

| Key | Action |
| --- | --- |
| Ctrl+Z / Ctrl+Shift+Z or Ctrl+Y | undo / redo any change (Edit menu) |
| Ctrl+N | new set |
| Alt+Enter | set properties |
| Ctrl+Shift+K | check algs |
| Ctrl+Shift+V | paste algs |
| Ctrl+Shift+A | new case from alg |
| Ctrl+Shift+G | new group |
| Delete | delete the selected cases or group, or remove the selected alg in the alg list |
| F2 | rename the selected case or group, or edit the selected alg in the alg list |
| Alt+Up / Alt+Down | move the selected cases, group or alg up / down |

Ctrl- or Shift-click selects several cases.

## Case lists (Learn, Drill, Library)

| Where | Key | Action |
| --- | --- | --- |
| search box | Enter | give the keys back to the cube |
| search box | Esc | clear the search |

## Timed

| When | Key | Action |
| --- | --- | --- |
| before a solve | hold Space, release | start, once the time turns green (the hold time is in Settings > Timing and solves). With inspection on, tap Space first to start inspecting. |
| before a solve, virtual | turning keys | rotations only |
| inspection, hold, solve | Esc | cancel (the scramble is put back on the virtual cube) |
| physical, running | any key | stop |
| virtual, running | turning keys | any turn; the timer stops when the cube is solved (unless turned off in Settings > Timing and solves) |
| virtual, running | Space | stop (a DNF if the cube isn't solved) |
| after a virtual DNF | Space, Enter or Esc | next scramble |
| after a solve | Alt+1 / Alt+2 / Alt+3 | no penalty / toggle +2 / toggle DNF on the last solve |
| after a solve | Backspace | toggle DNF on the last solve, as in Drill |
| after a solve | Alt+C / Alt+R | comment on / review the last solve |
| after a solve | Delete / Ctrl+Z | delete the last solve / put the last deleted solve back |
| after a solve | Ctrl+→ | skip the scramble |
| alg set scramble | Tab | show or hide the algs for the case the scramble sets up |

## Untimed

| When | Key | Action |
| --- | --- | --- |
| virtual, before the solve | turning keys | rotations only |
| virtual, before the solve | hold Space, release | start the solve, once the hint turns green |
| virtual, solving | Space | end the solve (a DNF if the cube isn't solved); it also ends by itself when the cube is solved (Settings > Timing and solves) |
| virtual, solving | Esc | put the scramble back |
| virtual, after the solve | turning keys | any turn |
| virtual, after the solve | Space / Enter | next scramble |
| virtual, after the solve | Esc | reset the cube to solved |
| virtual, alg field | Enter / Esc | apply the alg / give the keys back to the cube |
| physical | Space or → | keep the scramble as done and show the next |
| physical | Backspace or ← | previous scramble (→ goes forward again) |
| any | Delete / Ctrl+Z | delete the last solve / put the last deleted solve back |
| any | Alt+C / Alt+R | comment on / review the last solve |
| any | Ctrl+→ | skip the scramble |
| alg set scramble | Tab | show or hide the algs for the case the scramble sets up |

## Move Count

| When | Key | Action |
| --- | --- | --- |
| virtual, before the solve | turning keys | rotations only |
| virtual, before the solve | hold Space, release | start the solve, once the count turns green |
| virtual, solving | Space | end the solve: the move count if the step is done, else a DNF; the next scramble follows |
| virtual, solving | Esc | put the scramble back |
| physical | 0-9 | type the move count |
| physical | Backspace / Esc | correct / clear the count |
| physical | Enter | record the count and show the next scramble |
| physical | Alt+3 | record the scramble as DNF |
| any | Delete / Ctrl+Z | delete the last solve / put the last deleted solve back |
| any | Alt+C / Alt+R | comment on / review the last solve |
| any | Ctrl+→ | skip the scramble |

## Review

In the solve review (Alt+R, or double-click a solve):

| Key | Action |
| --- | --- |
| ← / → | step the replay back / forward |
| Home / End | to the scramble / to the end |

## EO Cross

EO Cross, F2L and TSLE share their keys. EO Cross has the Inspection, Untimed and Timed tabs; F2L and TSLE Learn, Untimed and Timed. Timed is timed as Drill is.

| When | Key | Action |
| --- | --- | --- |
| Inspection, before a sitting's first state | Space | show a state and start the inspection clock |
| Inspection, planning | turning keys | rotations to look; the first face turn stops the clock and hides the cube |
| Inspection, blind | Space | show the cube again: only then is the plan judged |
| Inspection, planning or blind | Esc | the same state again, the clock too; not recorded |
| solving | turning keys | any turn; the solve ends by itself when it is done |
| solving | Backspace or Shift+Space | give up |
| solving | Esc | start again |
| Learn or Untimed, afterwards | Space | next |
| Learn or Untimed, afterwards | Enter | play the best solution from the start |
| Learn or Untimed, afterwards | Tab | show or hide the details (click a solution there to play it) |
| Learn or Untimed, afterwards | turning keys | turn the cube freely |
| Learn or Untimed, afterwards | Esc | the same state again, not recorded or graded |
| Timed, virtual, before the solve | turning keys | rotations only |
| Timed, virtual, before the solve | Space | arm the timer: the first face turn starts it (or hold Space and let go, if Settings > Drill says so) |
| Timed, virtual, solving | Space | stop now: a DNF unless the step is done (the timer stops by itself when it is) |
| Timed, physical | hold Space, release | start the timer, once the scramble is done |
| Timed, physical, solving | any key | stop the timer |
| Timed, before or during the solve | Shift+Space | give up: a DNF (Backspace instead, if Settings > Drill says so) |
| Timed, solving | Esc | cancel: nothing is recorded |
| Timed, afterwards | Space | next |
| Timed, afterwards | Enter | play the best solution from the start |
| Timed, afterwards | Tab | show or hide where the time went and the details |
| Timed, afterwards | Backspace | make it a DNF, or not |
| Timed, afterwards | Delete | delete the attempt |
| Timed, afterwards | Esc | the same state again, not recorded |

## F2L

| When | Key | Action |
| --- | --- | --- |
| Learn, choosing a case's solutions | 1-9 | tick or untick a solution (a click on one plays it) |
| Learn, choosing a case's solutions | Enter | keep the ones ticked |
| Learn, changing a case's solutions | Esc | leave them as they were |
| Learn, a new case | Enter | show its solution on the cube, which is then put back to do it |
| Learn, a new case | turning keys | do it: the case then comes back in a minute, to do from memory |
| Learn, a new case | Space | skip the showing: the case is known |
| solving | turning keys | any turn; the solve ends by itself when it is done |
| solving | Backspace or Shift+Space | give up |
| solving | Esc | start again |
| Learn, solving | Tab | a hint: a move more of your solution each time (the card is then graded Again) |
| Learn or Untimed, afterwards | Space | next |
| Learn or Untimed, afterwards | Enter | play the best solution from the start |
| Learn or Untimed, afterwards | Tab | show or hide the details (click a solution there to play it) |
| Learn or Untimed, afterwards | turning keys | turn the cube freely |
| Learn or Untimed, afterwards | Esc | the same state again, not recorded or graded |
| Learn, after a card's try | 1-4 | change the grade: Again, Hard, Good, Easy |
| Learn, nothing due | Space | look again, taking a card in its learning steps early |
| Learn, after a card's try | 0 | change the case's solutions |
| Learn, after a try as cheap as yours | + | add what you did to the case's solutions |
| Timed, virtual, before the solve | turning keys | rotations only |
| Timed, virtual, before the solve | Space | arm the timer: the first face turn starts it (or hold Space and let go, if Settings > Drill says so) |
| Timed, virtual, solving | Space | stop now: a DNF unless the step is done (the timer stops by itself when it is) |
| Timed, physical | hold Space, release | start the timer, once the scramble is done |
| Timed, physical, solving | any key | stop the timer |
| Timed, before or during the solve | Shift+Space | give up: a DNF (Backspace instead, if Settings > Drill says so) |
| Timed, solving | Esc | cancel: nothing is recorded |
| Timed, afterwards | Space | next |
| Timed, afterwards | Enter | play the best solution from the start |
| Timed, afterwards | Tab | show or hide where the time went and the details |
| Timed, afterwards | Backspace | make it a DNF, or not |
| Timed, afterwards | Delete | delete the attempt |
| Timed, afterwards | Esc | the same state again, not recorded |

## TSLE

| When | Key | Action |
| --- | --- | --- |
| Learn, a new case | Enter | show its solution on the cube, which is then put back to do it |
| Learn, a new case | turning keys | do it: the case then comes back in a minute, to do from memory |
| Learn, a new case | Space | skip the showing: the case is known |
| solving | turning keys | any turn; the solve ends by itself when it is done |
| solving | Backspace or Shift+Space | give up |
| solving | Esc | start again |
| solving | Tab | a hint: the best step from here (a card is then graded Again) |
| Learn or Untimed, afterwards | Space | next |
| Learn or Untimed, afterwards | Enter | play the best solution from the start |
| Learn or Untimed, afterwards | Tab | show or hide the details (click a solution there to play it) |
| Learn or Untimed, afterwards | turning keys | turn the cube freely |
| Learn or Untimed, afterwards | Esc | the same state again, not recorded or graded |
| Learn, after a card's try | 1-4 | change the grade: Again, Hard, Good, Easy |
| Learn, nothing due | Space | look again, taking a card in its learning steps early |
| Timed, virtual, before the solve | turning keys | rotations only |
| Timed, virtual, before the solve | Space | arm the timer: the first face turn starts it (or hold Space and let go, if Settings > Drill says so) |
| Timed, virtual, solving | Space | stop now: a DNF unless the step is done (the timer stops by itself when it is) |
| Timed, physical | hold Space, release | start the timer, once the scramble is done |
| Timed, physical, solving | any key | stop the timer |
| Timed, before or during the solve | Shift+Space | give up: a DNF (Backspace instead, if Settings > Drill says so) |
| Timed, solving | Esc | cancel: nothing is recorded |
| Timed, afterwards | Space | next |
| Timed, afterwards | Enter | play the best solution from the start |
| Timed, afterwards | Tab | show or hide where the time went and the details |
| Timed, afterwards | Backspace | make it a DNF, or not |
| Timed, afterwards | Delete | delete the attempt |
| Timed, afterwards | Esc | the same state again, not recorded |

## Alg Lab

The Search tab is used with the mouse: Enter in the field under the list adds the alg typed. These keys are for the Test tab.

| When | Key | Action |
| --- | --- | --- |
| Before the session or after its last rep | Space | start: the next alg is set up |
| Physical, case set up | Space held, then let go | start the timer (hold until the time turns green) |
| Physical, timer running | any key but Shift, Ctrl, Alt | stop |
| Virtual, case set up | turning keys | rotations look at the case; the first layer turn starts the timer, which stops when the case is solved |
| Virtual, timer running | Space | stop early (a DNF unless solved) |
| Case set up or timer running | Esc | set the case up again (nothing is recorded) |
| After an attempt | Space | next alg |
| After an attempt | Backspace | toggle DNF |
| After an attempt | Delete | delete the time and set the case up again |
| After an attempt | Esc | the same alg again |

Keys are ignored for a moment after the timer stops. Double-click an alg in the session's list to time it next.

## Today

Each set has a button to review or learn it; Review all sets reviews every set's due cases together.
