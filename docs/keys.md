# Keys

<!-- Written from src/app/help_text.cpp by the test "keys.md matches the key table": edit the table, not this file. -->

Every keyboard control, mode by mode. In the app, `F1` shows the same controls in a panel beside the page, the current mode's first, with your own turning keys. The [user guide](user-guide.md) says more about what each mode does.

A key only reaches the mode's page when no text field or dialog has the keyboard. A search box or count field gives the keys back with Enter (or Esc).

## Window

| Key | Action |
| --- | --- |
| Ctrl+1 … Ctrl+9, Ctrl+-, Ctrl+= | Learn, Drill, Library, Alg Lab, Timed, Untimed, Move Count, F2L, TSLE, Inspection, Blind, in the sidebar's order (not while a solve or inspection is running) |
| Ctrl+0 | Today |
| Ctrl+K | command list: type part of any menu command or of a choice on the page (alg set, session, scramble type, cube, view, ...), then Enter |
| F1 | show or hide the keyboard controls |
| F9 | show or hide the mode's side panel (the case list or the session) |
| F11 | full screen |
| F10 | focus mode (right-click outside the scramble, timer, cube and algs to switch it, full screen or the sidebar with the mouse) |
| Ctrl+B | show or hide the sidebar |
| Ctrl+, | settings |
| Ctrl+Shift+N | colour neutrality settings |
| Ctrl+Q | quit (asks first during a solve) |

## Turning keys

The virtual cube (Learn, Drill on the virtual cube, the Solves modes' virtual sessions, F2L, TSLE, Inspection, Blind, Alg Lab's virtual test sessions) is turned with cstimer's face keys. Shift and a face key turns the wide layer, e.g. Shift+I is `r` and Shift+G is `f'`. Remap them in Settings > Cube and keys: select a row and **Change** its key or move, or **Add** one. A turning key may be held with Shift but not Ctrl or Alt; the digits, arrows, function keys and the modes' own keys can't be bound.

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
| physical, waiting | hold Space, release | start, once the time turns green (Drill's own hold time, Settings > Drill) |
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
| before a solve | hold Space, release | start, once the time turns green (the hold time is in Settings > Solves). With inspection on, tap Space first to start inspecting. |
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

## F2L

| When | Key | Action |
| --- | --- | --- |
| solving | turning keys | any turn; the clock runs from the first look |
| Pair solutions, solving | Tab | show the best solution's next move, done from the start (the try counts as hinted) |
| Pair choice, solving | Tab | name the best pair to start with (the pick isn't counted) |
| solving | Backspace | give up and see the solutions |
| solving | Esc | put the cube back as it started |
| afterwards | Space | next |
| afterwards | Esc | the same state again, not recorded |
| afterwards, Case explorer | Enter | play the chosen solution from the start |
| afterwards, Case explorer | ← / → | step back / forward through it |
| afterwards, Case explorer | 1-9 | choose a solution of the list (or click it to play it) |
| afterwards, Case explorer | turning keys | turn the cube freely |
| Case explorer | ↑ / ↓ | the case before / after |
| Case explorer | Space | another example of the case |
| Case explorer | Esc | put the cube back |

Full F2L has no hint and nothing to step through: its pairs are compared in a table.

## TSLE

| When | Key | Action |
| --- | --- | --- |
| solving | turning keys | any turn; a step is judged when the trigger is finished |
| Guided intro, a trigger shown | Enter | show the trigger on the cube, then put the cube back to do it |
| Guided intro | Space | skip the case as known; after a case, the next one |
| Next trigger, Full TSLE, solving | Tab | show the best step from here (the try counts as hinted) |
| Next trigger, Full TSLE, solving | Backspace | give up and see the good triggers |
| solving | Esc | put the cube back as it started |
| afterwards | Space | next |
| afterwards | Enter / 1-9 | play the best trigger / the trigger with that number (or click it) |
| afterwards | Esc | the same state again, not recorded |
| Case explorer | Enter / 1-9 | do the best trigger / that trigger; the list follows the cube |
| Case explorer | turning keys | turn the cube freely; the list follows it |
| Case explorer | ↑ / ↓ | the case before / after |
| Case explorer | Space | the case at its next AUF |
| Case explorer | Esc | back to the case |

## Inspection

| When | Key | Action |
| --- | --- | --- |
| Plan | Space | start inspection (the cube is shown), hide the cube, reveal it, then the next scramble |
| Plan, inspecting | turning keys | rotations only |
| Plan, cube hidden | turning keys | any turn, unseen |
| Plan, afterwards | turning keys | rotations, to look at the cube |
| any | Esc | put the scramble back (nothing is recorded), or end a retry |
| Bad edges | Space | show the cube; afterwards, the next scramble |
| Bad edges | 0-9 / Backspace / Enter | type a count / correct it / give it (F/B first, then R/L if asked) |
| Bad edges, afterwards | Tab | ring the other axis's bad edges |
| between attempts | Delete / Ctrl+Z | delete the last attempt / put the last deleted attempt back |
| between attempts | Alt+R | review the last Plan attempt |
| between attempts | Ctrl+→ | skip the scramble |

A Space in the moment the time limit hides the cube is ignored, and Space with no moves done doesn't finish.

## Blind

| Drill | Key | Action |
| --- | --- | --- |
| Letter quiz | letter keys | answer |
| Letter quiz | Enter / Backspace | answer a longer label / correct |
| Letter quiz | click a sticker on the net | answer a letter → sticker question |
| Letter quiz, Setup moves | Space | next letter; before an answer, give the letter up or skip it (not for half a second after the quiz moved on by itself) |
| Setup moves | turning keys | do the setup |
| Setup moves | Esc | start the setup again (after it: try the letter again) |
| Setup moves, Type it | Esc outside the field | give the letter up |
| Memo tracing | Space | new scramble |
| Memo tracing | Enter (or Tab) | type the letters: Enter after the edges, Enter again to check |
| Memo tracing | turning keys | rotations, to look at the cube (also after the trace) |
| Memo tracing | Esc | give the scramble up |
| Blind solves, virtual | Space | start the memo and the timer, start the execution (hides the cube), stop |
| Blind solves, virtual | turning keys | rotations during the memo, any turn during the execution, rotations afterwards |
| Blind solves, physical | Space / any key | start, end the memo / stop |
| Blind solves, physical, after a solve | Backspace | toggle DNF |
| Blind solves, between solves | Delete / Ctrl+Z | delete the last solve / put the last deleted solve back |
| Blind solves | Esc | cancel |

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
