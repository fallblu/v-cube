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

No other key changed.

## Window

- The **Mode** menu is now **Go**.
- **Edit > Undo** is in the menu in Learn, Drill, Timed, Untimed and Move
  Count too (it sends Ctrl+Z to the mode).
- **Import set / Export set** left the Set menu: File > Import alg set and
  Export alg set are the one pair.
- The focus-mode notice appears at the bottom of the window, clear of the
  counts and the scramble.
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

## Blind

- "Setups:" (the answer mode) is **Answer:**; the Blind solves cube picker
  is labelled **Cube:**.

## Library

- A toolbar above the lists: New set, Set properties, New case from alg,
  Paste algs, New group, Check algs. The New case and Paste algs buttons
  under the case tree went there.
- Buttons on the selected case: Learn, Drill, Alg Lab, Learn history,
  Drill history.
- Deleting cases or groups no longer asks first; the message says what
  went and that Ctrl+Z brings it back. Deleting a set still asks.
- "Make main" is "Make main alg" everywhere.

## Alg Lab

- "Paste…" is "Paste algs…"; the Test tab's "New" and "Delete" and the lab
  cases' "Delete" end in "…" as they open a dialog or ask first.
- The Test tab's timer ignores the same modifier and lock keys as Drill's.

## Today

- The two tables sit side by side and are no wider than they need to be.
- Each set has a Review, Learn or Open button, and **Review all sets**
  reviews every set's due cases together. "Practice" is **Activity**.
