# What the overhaul changed

A list of every control that moved and every key that changed on the
`refactor/overhaul` branch, for finding things again. Data files are
unchanged.

## Keys

| Before | Now |
| --- | --- |
| Ctrl+4 … Ctrl+8: Timed, Untimed, Move Count, Inspection, Blind; Ctrl+9: Alg Lab | Ctrl+1 … Ctrl+9 follow the sidebar: Learn, Drill, Library, **Alg Lab (4), Timed (5), Untimed (6), Move Count (7), Inspection (8), Blind (9)** |
| Timed: DNF toggle only on Alt+3 | Backspace toggles it too, as in Drill, Blind and Alg Lab (Alt+1/2/3 stay) |
| Inspection: skip, delete and review only as buttons; delete couldn't be undone | Ctrl+→ skips, Delete deletes, Ctrl+Z puts the deleted attempt back, Alt+R reviews |
| F9 hid the case list in Learn only | F9 (View > Side panel) hides the side panel in every mode: the case list in Learn and Drill, the session and its attempts in the solve modes, Inspection and Blind, the case browser in Alg Lab. Remembered per mode. |
| Blind solves: deleting a solve only on right-click, no undo | Delete deletes the last solve, Ctrl+Z puts it back |
| Alg Lab: no undo for "Add to set" and "Make main alg" outside Library | Ctrl+Z on the Search tab takes back a set edit made there |

No other key changed.

## Window

- The **Mode** menu is now **Go**.
- **Edit > Undo** is in the menu in Learn, Drill, Timed, Untimed and Move
  Count too (it sends Ctrl+Z to the mode).
- **Import set / Export set** left the Set menu: File > Import alg set and
  Export alg set are the one pair.
- The focus-mode notice appears at the bottom of the window, clear of the
  counts and the scramble.
- **A menu named after the mode** (Learn, Drill, Alg Lab) sits in the menu
  bar with what the mode's list offers on a right-click, for the case or
  alg selected.
- Every practice mode shows a short message and, under it, the keys that
  do something now as chips; the long hint sentences are gone.
- `docs/keys.md` and the F1 panel are written from one table
  (`src/app/help_text.cpp`); the panel shows the keys as tables.

## Settings

| Before | Now |
| --- | --- |
| General | **Appearance**, which also has the timer text size and decimal places (from Solves) |
| Solves | **Timing and solves**, which also has Drill's hold time, virtual timer start and auto-stop (from Drill) |
| Keyboard | **Cube and keys**, which also has the virtual cube's turn speed (from General) |
| Learning | **Learn** |
| Drill | Drill, without the three timing options |

Every option's explanation is now written under it (they were tooltips,
which the app never shows). The dialog has an **Apply** button.

## Learn

- One bar above the cube: **Alg set** (from the top of the list),
  **Practise** as three buttons (Solve, Recognise, Which case?; it was an
  unlabelled drop-down), **View**, the context-practice controls and
  **Hide list**.
- Under the list, one strip: the queue, the count, **Add new cases** (was
  "Start learning"), **Learn next** and a **⋯** menu with "Take the next
  case out of the queue" (was the Remove button), **Order of new cases**
  (was the "New cases:" drop-down above the list) and Tick all / Tick none
  (were buttons).
- The line under the list only says how many cases are ticked; the counts
  it repeated are in the header.
- The status line is a short message; the keys that do something now are
  shown under it as chips.
- With nothing due, buttons offer the next learning case early, new cases,
  another set's reviews and every set's reviews.
- The case name column takes the list's spare width.

## Drill

- One bar above the cube: **Alg set**, **Cube**, **Cases**, **Order**,
  **View** (they were five rows above the list).
- **Cases** combines the old "Drill:" and "Cases:" drop-downs in one menu
  with two parts, and shows the count, e.g. "Ticked cases · All (12)".
- The DNF reason has a line of its own, with the DNF and Delete buttons
  under it and the keys as chips.
- "New session" is **Reset tally** and "This session:" is **Tally:** (it
  never was a stored session).

## Timed, Untimed, Move Count

- The three pages are built from one shared stage, so the bar, scramble,
  cube, readout, hint and buttons sit in the same places. In Timed the
  status line is in the muted hint style the other two use.

## Inspection

- The Drill, Step, Then and Axes pickers are greyed once the session has
  attempts, with **New session…** beside them, where they used to look
  free and answer with a message.
- **New session** is a form: the drill, the step (and the pair after EO
  Cross) or the axes, and a name that follows those choices until you
  type your own.
- While the cube waits to be shown, the stage says what the session
  practises and how the last attempt went.

## Blind

- "Setups:" (the answer mode) is **Answer:**; the Blind solves cube picker
  is labelled **Cube:**.

## Library

- A toolbar above the lists: New set, Set properties, New case from alg,
  Paste algs, New group, Check algs. The New case and Paste algs buttons
  under the case tree went there.
- Buttons on the selected case: Learn, Drill, Alg Lab.
- The case's details are on two tabs: **Algs** (the alg list, with the
  selected alg's fingertricks under it) and **History** (its Learn card
  and drill statistics on each cube, with the Learn history and Drill
  history buttons).
- Deleting cases or groups no longer asks first; the message says what
  went and that Ctrl+Z brings it back. Deleting a set still asks.
- "Make main" is "Make main alg" everywhere.

## Alg Lab

- "Paste…" is "Paste algs…"; the Test tab's "New" and "Delete" and the lab
  cases' "Delete" end in "…" as they open a dialog or ask first.
- The Test tab's timer ignores the same modifier and lock keys as Drill's.
- **Generator** is a button with an arrow that folds the generator away
  or shows it; the choice is remembered.

## Today

- The two tables sit side by side and are no wider than they need to be.
- Each set has a Review, Learn or Open button, and **Review all sets**
  reviews every set's due cases together. "Practice" is **Activity**.

## Under the hood

- The pages are built from shared parts (`src/app/page_parts.*`): the
  option bar, the solve stage, the hint bar, the segmented picker and the
  help label.
- The keys of Alg Lab's Test tab are decided by `LabFlow`
  (`src/train/lab_flow.*`), with unit tests, like the other modes' flows.
- The six largest page files are split by responsibility (for example
  `learn_page.cpp`, `learn_solve.cpp`, `learn_practice.cpp`); no file of
  the app is much over 1,000 lines now.
- Alg Lab's saved choices are named in `ui_state`, splitter sizes are
  saved by name, and the session rename and delete code is shared.

## Left as it was, on purpose

- **The cube's size.** It is drawn at two thirds of its space by an
  earlier decision.
- **Timed stays its own page class.** It shares the stage and the session
  helpers with Untimed and Move Count, but its store and timer flow are
  different, so it was not folded into their base class.
- **Check algs** stays a report: it lists each failing alg with the
  reason, which the "Failing algs" filter can't.
- **Library's and Alg Lab's paste** stay separate: one files algs under
  the cases of a set, the other adds candidates to one case.
- **Alg Lab's virtual timer** still starts with the first layer turn and
  stops when the case is solved, without tapping Space first: the alg to
  do is known, so arming would only add a key press.
- **Drill's hold time, start and auto-stop** sit beside the solve modes'
  on one Settings tab but remain their own values.
