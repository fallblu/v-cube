# Overhaul plan (draft for approval)

Scope: organise and consolidate what the app already does, and streamline
its screens. No new training features, no changes to scheduling, solvers or
data files. Saved settings are migrated where a key moves.

Based on a read of `src/app` (and how it uses `src/train` and `src/io`) and
a walk through every screen on a scratch copy of the real data at 1920x1040
and 960x1040, including focus mode.

## What the review found

### Across the app

1. **Every mode builds its own page.** There are five different option-row
   layouts, five hand-built "View:" pickers, four copies of the session
   new/rename/delete code, six copies of the cube-look settings, and three
   kinds of status line. Timed is a near copy (about 150 lines of layout
   plus the delete/undo/comment code) of the page Untimed and Move Count
   share.
2. **The same intent has different keys per mode.** Toggle DNF is Backspace
   in Drill, Blind and Alg Lab but Alt+3 in Timed and Move Count. Ctrl+Z
   deletes in Drill, undeletes in the solve modes and takes back a grade in
   Learn. Backspace has five meanings and Tab four. Inspection has buttons
   but no keys for skip, delete and review, and its delete has no undo.
3. **Hints are long sentences that differ in style per mode**, e.g. "Space:
   practise the case with the algs. Enter: next case. Backspace: not a DNF.
   Delete: delete it. Esc: try the case again." The keys are written out in
   three places (docs/keys.md, the F1 panel, each page's status strings).
4. **Explanations are invisible.** Tooltips are switched off app-wide (on
   purpose, since ef2ee9b), but 147 explanations still live only in
   tooltips, including every Settings and Drill-settings explanation.
5. **Ctrl+digit doesn't follow the sidebar.** Alg Lab is fourth in the
   sidebar but Ctrl+9; Timed is fifth but Ctrl+4.
6. **Settings are grouped by history, not by subject.** Hold time and
   auto-stop each exist twice (Solves and Drill tabs); decimal places is
   app-wide but under Solves; timer text size is under Solves while
   scramble text size is under General; "Slow to recognise" is under
   Learning but drives a Drill filter; Inspection, Blind and Alg Lab have
   no tab.
7. **Menus.** Only Library adds menus. Undo works in Learn, Drill and the
   solve modes but Edit > Undo only shows in Library. File > Import/Export
   alg set duplicates Set > Import/Export set. Many actions exist only in a
   right-click menu.
8. **Wording drifts**: "set" / "alg set", "Avg" / "Mean", "solves" /
   "attempts", "session" (stored sessions in the solve modes, a throwaway
   counter in Drill), "Practice" (a sidebar group, Today's table and
   Drill's practice stage), "Make main" / "Make main alg", "-" / "–" for an
   empty value.

### Learn (priority)

9. At 1920 wide the cube is about 300 px across in a 1360 px stage; the
   bottom third of the stage is empty.
10. The case list is 400 px wide but shows "Ua p…", "Adj/Ad…": the name
    column is squeezed by Status, Due and Recog. At 960 wide most names
    are a single letter.
11. Controls are scattered over five places: set and new-case order (top
    left), view and practice kind (top of the stage), Hide list (far
    right), tick buttons and "Start learning" (under the list), queue line
    with "Learn next" and "Remove" (under that), plus a summary line that
    repeats the header's counts.
12. "All caught up" is a blank cube and one sentence, while another set
    has 104 reviews due.
13. Switching practice kind mid-list is an unlabelled drop-down.
14. In focus mode the "Focus mode: F10…" notice is drawn over the header
    counts.

### Drill (priority)

15. Five stacked form rows (set, cube, drill, cases, order) and then the
    list's own search and filter: four separate filters decide what gets
    drilled.
16. The result area wraps "13 pieces weren't where the step leaves them
    when you stopped the timer." into a 200 px column next to its buttons.
17. "New session" is a counter reset, unrelated to the stored sessions the
    other timed modes call sessions.

### Inspection and Blind (priority)

18. Inspection shows up to seven pickers in one row. Drill, Step, Then and
    Axes are fixed once a session has attempts, yet look free and answer
    with a refusal message. Before the first Space the stage is empty
    apart from "Space: start inspection".
19. Blind builds all four drills' controls into one row and toggles them by
    visibility; two "Pieces:" pickers, "Setups:" beside "Setup:".

### Solves

20. Timed, Untimed and Move Count look alike but are two code paths, so
    they differ in small ways (cube picker position, labels, focus stats).

### Library and Alg Lab

21. Library's case panel is a large disabled form until a case is picked.
    New group, properties, check, import/export are menu-only; "Make main"
    and "Open in Alg Lab" are right-click only. Undoable actions still ask
    for confirmation. Case and drill history open only from Learn/Drill.
22. Library and Alg Lab both list a case's algs, add, paste and make main,
    with two paste flows (Alg Lab's drops failures silently) and stats
    that can disagree (different trim).
23. Alg Lab's Test tab has its own hand-rolled timer: no arming on the
    virtual cube, auto-stop always on, its own modifier-key list. Its
    session row uses buttons where the other modes use a session menu.
24. Edits made from Alg Lab land on Library's undo stack, which can only
    be reached in Library.

### Today

25. Two small tables stretched across the whole window with most of the
    page empty; the only action is double-click.

### Code health

26. Oversized classes mixing several jobs: library_page.cpp (1881),
    learn_page.cpp (1852), inspection_page.cpp (1620), alglab_page.cpp
    (1405), drill_page.cpp (1222), timer_page.cpp (1204),
    lab_test_panel.cpp (1040).
27. Repeated helpers (`counted` x3, `secondsText` x3, pass/fail colours x2,
    stats cells x2, move-set edit lambda x3), about 14 raw `alglab/*`
    settings keys outside ui_state, splitter sizes saved by index.

## The plan

One branch, `refactor/overhaul`, a commit per numbered step, tests and an
offscreen walk-through after each. Steps within a stage can be tested
together.

### Stage 1: shared page parts (no visible change intended)

1.1 **One page frame** for every practice mode: side panel (list or
    session), option bar, stage (scramble, cube, timer), key-hint bar.
    One option-bar component with labelled pickers replaces the five
    layouts and the hand-built View pickers. F9 hides the side panel in
    every mode, not only Learn.
1.2 **One solve page**: Timed joins Untimed and Move Count on the shared
    base. One session controller (new, rename, delete, select, undo
    delete, comment) used by Timed, Untimed, Move Count, Inspection, Blind
    solves and Alg Lab's Test tab.
1.3 **Split the big classes** along the lines in finding 26: Learn into a
    shell plus solve, recognise, quiz and queue parts; Inspection into
    Plan and Bad-edges drills (as Blind already is); Library into set
    list, case tree, case detail and commands; Alg Lab into case browser,
    candidates, generator and profiles; a `LabFlow` in `src/train` so the
    Test tab's timing is unit-tested.
1.4 **Shared helpers**: time/date/count text, status colours, stats cells,
    one empty-value dash; every settings key named in ui_state; splitters
    saved by name (old sizes migrated).

### Stage 2: the window

2.1 **Shortcuts follow the sidebar**: Ctrl+1…9 = Learn, Drill, Library,
    Alg Lab, Timed, Untimed, Move Count, Inspection, Blind; Ctrl+0 Today.
    (Changes Ctrl+4…9.)
2.2 **One key table** drives the F1 panel, the hint bar and docs/keys.md,
    so they can't drift.
2.3 **Key-hint bar**: each page's status sentence becomes a message line
    plus short key chips ("Space next", "Tab algs", "Esc reset") that
    change with the phase. Same style in every mode.
2.4 **Same intent, same key** (old keys kept as aliases where free):
    - Delete = delete last; Ctrl+Z = undo the last action, in every mode
      (Drill's Ctrl+Z becomes undo, Delete stays delete).
    - Backspace toggles DNF after a result everywhere it's free (added to
      Timed and Move Count; Alt+1/2/3 stay).
    - Ctrl+Right skips, Alt+R reviews, Delete deletes in Inspection too,
      with undo.
2.5 **Menus**: Edit > Undo/Redo in every mode, named after what it undoes;
    a menu for the current mode holding its right-click-only actions; one
    Import/Export pair; "Mode" renamed "Go".
2.6 **Settings regrouped**, with every explanation shown as text under its
    option: Appearance (theme, colours, text sizes, case pictures, decimal
    places) · Cube and keys (turn speed, turning keys) · Colour neutrality
    · Timing (hold time, virtual start, auto-stop: one set of defaults,
    Drill may override) · Learn · Drill · Solves and Inspection. Adds an
    Apply button.
2.7 **Naming**: one word per concept across the app (list in finding 8);
    Today's "Practice" table becomes "Activity"; Drill's "New session"
    becomes "Reset tally".

### Stage 3: Learn

3.1 **Stage first**: the cube scales to the room it has; the answer area
    sits directly under it; no dead third.
3.2 **Case list as a proper side panel**: name column takes the spare
    width, other columns size to content; narrower by default; pictures
    can be turned off per list.
3.3 **One bar at the top**: set, practice kind as three labelled buttons
    (Solve · Recognise · Which case?), view. New-case order moves into the
    new-cases control.
3.4 **One "New cases" strip** replaces the three rows under the list:
    "Queue 3: Bar/Bar, Bar/Opp, … · [Add 3 ▾] · [Learn next] · [⋯]" with
    order, remove and tick all/none in the ⋯ menu. The duplicate summary
    line goes.
3.5 **Nothing-due screen**: says what is waiting elsewhere and offers it
    as buttons (review TSLE's 104, learn 3 new cases here, review ahead,
    drill this set).
3.6 Focus-mode notice no longer covers the counts.

### Stage 4: Drill

4.1 **One bar**: set · cube · cases · order. "Cases" is one picker that
    combines today's Drill and Cases rows ("Ticked · all", "Group Front
    Opp · learned", "Weakest 5") and shows the count.
4.2 **Result panel** laid out like Learn's: result line, reason on its own
    line, actions as buttons with their keys, case and algs beneath.
4.3 Uses the shared stage, so Learn and Drill look and behave alike.

### Stage 5: Solves, Inspection, Blind

5.1 Timed, Untimed and Move Count get identical bars and panels (falls out
    of 1.2).
5.2 **Inspection**: what a session fixes (drill, step, pair, axes) is
    chosen in a "New session" form and shown as a summary on the bar with
    a Change button, not as pickers that refuse; limit and view stay
    pickers. A ready screen that shows the session's last result and
    stats.
5.3 **Blind**: each drill owns its bar and side panel; consistent labels;
    delete and DNF keys as elsewhere.

### Stage 6: Library and Alg Lab

6.1 **Library toolbar** (New set, New case, Paste algs, New group,
    Properties) and a useful empty state. No confirmation for undoable
    actions. "Check algs" becomes the existing "Failing algs" filter.
6.2 **Case panel tabs**: Algs · Fingertricks · History (the Learn and
    Drill history that today opens only from those modes), with buttons
    for Learn this, Drill this, Open in Alg Lab.
6.3 **One alg list and one paste dialog** shared by Library and Alg Lab;
    stats computed one way.
6.4 **Alg Lab Search**: actions in one toolbar with the shared names;
    generator in a collapsible section; undo for set edits works from Alg
    Lab.
6.5 **Alg Lab Test**: shared timing (arming and auto-stop follow the
    Timing settings) and the shared session menu.

### Stage 7: Today, docs

7.1 **Today** as a start page: per-set rows with Review and Learn buttons,
    activity beside it instead of below, sized to content.
7.2 Docs: user guide, keys (generated), README screenshots/notes; a
    "what changed" list of every moved control and changed shortcut.

## Not in this overhaul

- New training features, analytics, scheduling changes.
- Data file formats (alg sets, sessions, alglab.json).
- The engine, solvers and `src/train` logic, apart from adding `LabFlow`.
- The grip names and fingertrick planner.

## Risks

- Stage 1 moves a lot of code. It lands first, on its own commits, with
  the existing 360 unit and 12 GUI tests plus new GUI tests per page, so
  later stages are small.
- Shortcut changes (2.1, 2.4) break habits; old keys stay as aliases
  where they don't clash, and the change list (7.2) names each one.
