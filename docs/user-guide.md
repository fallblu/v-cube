# User guide

What each of v-cube's modes does and how to use it. [keys.md](keys.md)
lists every key, and `F1` in the app shows the current mode's.

- [The window](#the-window)
- [Alg sets](#alg-sets): [Learn](#learn), [Drill](#drill),
  [case lists](#case-lists), [Library](#library),
  [getting algs into a set](#getting-algs-into-a-set), [Alg Lab](#alg-lab)
- [Solves](#solves): [Timed](#timed), [Untimed](#untimed),
  [Move Count](#move-count), [Review](#review), [scramble types](#scramble-types)
- [Practice](#practice): [EO Cross](#eo-cross), [F2L](#f2l), [TSLE](#tsle)
- [Colour neutrality](#colour-neutrality), [the virtual cube](#the-virtual-cube),
  [Settings](#settings)

## The window

The sidebar lists the modes in three groups, Algs (Learn, Drill, Library,
Alg Lab), Solves (Timed, Untimed, Move Count) and Practice (EO Cross, F2L,
TSLE), under **Today**. `Ctrl+1` to `Ctrl+9` and `Ctrl+-` switch to them
in the sidebar's order (Learn, Drill, Library, Alg Lab, Timed, Untimed,
Move Count, EO Cross, F2L, then TSLE on `Ctrl+-`) and `Ctrl+0` to Today;
the **Go** menu has them too. Switching is blocked while a solve or
inspection is running.

**Nothing timed starts by itself.** Wherever a case is timed from the
moment it is shown, the mode opens on a ready page and shows nothing until
you press `Space`: Learn (a case's recognition time), every drill of EO
Cross, F2L's Pair solutions, Pair choice and Full F2L, and TSLE's Next
trigger and Full TSLE. The ready page comes back when you return to
the mode, and after you pick a drill, a set or an option with the mouse;
a case whose clock was running when you left is put away, unrecorded.
Once started, the cases follow one another as before. The other timed
modes already waited for a key: Timed, Drill and Alg Lab start on Space
or the first turn.

- **Today** shows, for each alg set, the reviews due now or later today,
  the new cases Learn can still bring in today from the learn queue, the
  cases in the queue and the cases learned; your practice streak; and the solves, cases
  and answers recorded in each mode, with the time spent, today and over
  the last 7 days. Each set has a button to review or learn it, and
  **Review all sets** reviews every set's due cases together. The sidebar's Today and
  Learn buttons show how many reviews are due.
- **Keyboard controls**: `F1` (View or Help > Keyboard controls) shows the
  keys in a panel beside the page, the current mode's first, with your own
  turning keys. The panel can be undocked.
- **Command list**: `Ctrl+K` (Edit > Command list…) finds any menu command,
  or a choice on the page (alg set, session, scramble type, cube, view,
  ...): type part of it and press Enter.
- **Full screen**: `F11` (View > Full screen).
- **Focus mode**: `F10` (View > Focus mode) hides the sidebar, menus,
  status bar and each mode's lists and options, leaving the scramble,
  timer, cube and algs; messages, and the mode's name after `Ctrl+1`…`8`,
  show at the top of the window instead. Right-click anywhere outside them
  to switch full screen, focus mode or the sidebar with the mouse.
  `Ctrl+B` (View > Sidebar) hides just the sidebar.
- **Side panel**: `F9` (View > Side panel) hides or shows the current
  mode's side panel (the case list in Learn and Drill, the session and
  its attempts in the solve modes, the lists of EO Cross, F2L and TSLE,
  the case browser in Alg Lab), giving the cube the whole page. Each mode remembers it.
- **The mode's menu**: in Learn, Drill and Alg Lab the menu bar has a menu
  named after the mode with what its list offers on a right-click, for
  the case or alg selected.
- **Keys on the page**: under the cube each practice mode shows a short
  message and the keys that do something now, each with its action.
- The window reopens at the size and place you left it (moved back onto
  the screen if the screen or its scaling changed), with the lists' widths
  and the sidebar as you left them, but never in full screen or focus mode.
- **Small windows**: the window can be as narrow as half a 1080p screen;
  each mode's option row wraps onto a second line when it doesn't fit, and
  the chart gives up its room before the cube does.
- **One copy at a time**: starting v-cube while it is running brings the
  running window to the front, so two copies never save over each other.
- **File** imports and exports alg sets, exports all your data, and opens
  the data folder; see [file-formats.md](file-formats.md).

## Alg sets

An alg set is a list of cases in groups, each case with its algs. Learn and
Drill share each set's ticked cases and learn queue (cases added to a set
later come ticked). Only cases with at least one alg that solves them are
offered, and both modes show how many are skipped. Cases are set up for the
holds chosen under [colour neutrality](#colour-neutrality).

**Built-in sets**: PLL (21), OLL (57), 2-look OLL (10) and 2-look PLL (6).
They ship as case definitions only, with no algs, so you add your own (see
[getting algs into a set](#getting-algs-into-a-set)).

### Learn

Learn teaches the cases of an alg set on the virtual cube, untimed, and
schedules them by spaced repetition.

- Learn opens on a ready page: the first case is shown, and its
  recognition time starts, when you press `Space`. So does a case you pick
  in the list, and one that falls due while nothing was up.
- Each case is set up on the cube and you solve it from memory. The algs
  stay hidden until you press Tab. When the set has random U turns before
  and after its cases (as Drill uses), the case comes from a random side,
  so you learn to recognise it from any angle.
- Solving the case with one of its algs and without a hint grades it Good;
  a hint, giving up (Shift+Space, so that a double tap of Space after a
  case can't give up the next one) or leaving the algs grades it Again.
  Keys 1-4 change the grade afterwards. After a case the outcome is shown
  as a coloured badge, and each grade button shows the interval it would
  give. Ctrl+Z takes back the last grades, up to 10, and brings the case up
  again.
- **Following the alg**: U turns and rotations are free until your first
  turn of a layer other than U or D, so you can look at the case from any
  side and line it up (an alg's D turns may come before or after that AUF).
  From then on your moves must follow one of the case's algs. The same
  turns done another way still count: rotations, a half turn as two
  quarter turns either way, a move written with a 3 as those three quarter
  turns or one turn the other way (R3 as R R R or R'), a wide or slice turn
  as the turns it is made of (r as R M'), turns of opposite faces in either
  order, and a U turn at the end that the alg leaves out or adds. Once only
  U turns are left of the alg, any U turns count (its last U turn and the
  AUF done separately, or a U turn too far put right). A wrong turn, even
  one you undo, or starting over with Esc once the alg is begun leaves the
  algs. The status line says so straight away, and your moves are shown
  under the algs after the case, with the move where they left the algs
  marked (a turn the wrong way is marked even though the next turn is what
  gave it away).
- The case is finished when the cube reaches the step's end state. For a
  step that leaves nothing free, such as PLL, that is a solved cube, so a
  case that needs a U turn after its alg isn't done until you turn it. A
  step that leaves the last-layer permutation free (OLL) needs no U turn at
  the end. Once the case is solved the cube turns back upright, e.g. after
  an alg that solves the case before its last rotation.
- When the case is solved you see your algs with the AUFs it needed, e.g.
  `(U') R U R' U' ... (U2)`. A U turn written at the start or end of an alg
  joins the AUF on that side, so `(U2) R U R'` that needs a U first shows
  as `(U') R U R'`.
- Learned cases come up with the pieces the step leaves free scrambled (as
  in Drill), so an OLL isn't known by its permutation; a new case keeps the
  picture it is learned from.

**Scheduling.** Cases are scheduled Anki-style: cases in their learning
steps (minutes apart) first, then reviews due today (a review is due all
day on the day it falls due) in a random order, so that cases learned
together don't always come up in the same sequence, then new cases from
your learn queue, up to a daily limit (5) and only while fewer than 3
cases are in their learning steps, so that new cases don't arrive back to
back. Review intervals of three days or more are spread out by a few
percent. Settings > Learn holds the scheduler's choices: the learning
and relearning steps, the graduating and easy intervals, how much of its
interval a forgotten case keeps (20 %; earlier versions kept 50 %), the
ease a Good review regains (so a case once missed can space out again),
credit for reviews done late, whether Hard moves a learning case on a
step, how many new cases a day and how many learn at once, the learn-ahead
time (off; earlier versions took learning cases up to 20 minutes early),
when a case is a leech and when it is slow to recognise. Learn attempts
change the schedule; Drill attempts never do.

**The learn queue.** Under the list, one strip holds the queue: the next
cases and how many were introduced today, then **Add new cases**, which
adds the next N ticked cases to the queue (type N in the count field and
press Enter to do the same), and **Learn next**, which introduces the next
one now, whatever the limits. The **⋯** menu beside them takes the next
case out of the queue, chooses the **order of new cases** for the set (list
order, the order you queued them, shuffled, one from each group in turn,
shortest alg first, or fewest steps left first for sets solved a step at a
time) and ticks or unticks every case shown.

**With nothing due** the page says why, when the next review is, and how
the day went (reviewed, Again rate, due tomorrow), and offers what can be
done instead as buttons: the next learning case early, new cases from this
set, the reviews due in another set, or every set's reviews together. It
looks again by itself when a case falls due; Space looks again, and takes
the next learning case early when that is all there is.

**The header** above the cube (also in focus mode) counts the queued,
learning, due and done-today cases, and the set drop-down shows each set's
due reviews. **All sets: due reviews** at the top of the drop-down reviews
every set's due cases in one session, interleaved; the list shows the set
of the case at hand.

**The case list.** Right-click a case to start learning it, mark it as
learned (it then comes up as a review, without the walkthrough, and
doesn't count against the daily limit), remove it from the learn queue,
suspend it (left out of Learn but still ticked for Drill), bury it until
tomorrow, reset its learning progress (its card and Learn attempts; drill
times stay) or see its history (its card, and every attempt with its
recognition and execution times). Select several cases, or right-click a
group's row, to do the same to all of them at once. "Learn this case now"
on a learned case that isn't due yet is a practice run: you see how you
did, but the schedule doesn't change (also for the cases a step-at-a-time
set passes on the way).

The list's Recog column is each case's mean recognition time (from the
case being set up to your first turn of a layer other than U or D, in Learn
and on the virtual cube in Drill, for cases solved with their algs and
recognition reviews); Lapses and Ease (hidden at first) come from its card.
A case forgotten 6 times is flagged as a leech. The list can be filtered to
cases slow to recognise (over 1.5 s, in Settings), leeches, or suspended
and buried ones, and right-clicking the header hides or shows columns. F9
hides the list, and the splitters between the list, the cube and the algs
keep their place.

**The bar above the cube** has the alg set, what is practised, the view
of the cube and **Hide list** (`F9`). Under the cube the status line says
what is going on, and each key that does something now is shown under it
with its action.

**How reviews go.** **Practise** in the bar picks it:

- **Solve**: as above.
- **Recognise**: a learned case is only shown. Press Space as soon as you
  recall its alg, or Shift+Space if you can't; the algs are shown and you
  grade yourself 1-4. New and learning cases are still solved.
- **Which case?**: name the case among up to six cases of its group
  (keys 1-6), timed but not graded.

**Set properties that change Learn** (see [Library](#library)):

- **Guided learning** (on by default): the first time a case comes up, its
  main alg is a walkthrough. → and ← step through it on the cube (Home
  goes back to the start), then Space sets the case up again for you to do
  it with each next move highlighted. The walkthrough isn't graded: the
  case comes back at its first learning step to be done from memory (1-4
  grade it straight away instead). While the case is in its learning
  steps, Tab first shows only the next move (graded Hard, which moves it on
  a step but never finishes its steps), and a second Tab shows the algs
  (Again). Reviews work as before.
- **Learn by family** (off by default): each case is learned together with
  its mirror images, left-right, front-back and both, such as the four F2L
  slots' versions of a pair. The family has one review card; each time it
  comes up one of its ticked cases is set up at random, and afterwards all
  of them are shown with their main algs. The case list shows every case of
  a family with the family's status, and Drill counts them as learned
  together. Families are worked out from the cases themselves, not from
  their names.
- **A step at a time** (off by default) is for sets solved by reducing one
  case to an easier one, such as a last-slot set done with triggers: each
  case holds only its next step (`U (R U' R')`), which stops at another
  case of the set, and that case's step carries on until an alg finishes
  the step.
  - A case's card asks for its next step only. Any step that leaves fewer
    steps to go than before is Good, whether or not it is the set's; a step
    that doesn't, a hint or giving up is Again. A case with an alg that
    finishes the step (an OCLL at the end, say) needs an alg that does.
  - After the step the cube shows the case it stopped at and you carry on,
    case after case, to the end. Each case you pass is checked against its
    own card: a due one is graded, one that isn't due changes only if you
    miss its step, and one you haven't started learning has its step shown
    and isn't graded. A case is graded once per attempt.
  - Cases come up with the pieces the step leaves free anywhere, as they
    are after a step, and the status line says how many steps are left
    after each one. Esc goes back to where the current case came up.
  - Afterwards every case you went through is listed with the set's step,
    what you did instead where that differed, and what became of its card.
    Keys 1-4 change the grade of the case that came up.
- **Mirrored cases**: a set written for one F2L slot is also set up for the
  others. Each case then comes up as written or in one of the chosen mirror
  images (left-right, front-back or both), with the 3D cube seen from the
  front corner on that slot's side (a back slot is done from the front, as
  you hold the cube) and the algs mirrored to match: `R U R'` is `L' U' L`
  left-right, `R' U' R` front-back and `L U L'` both ways. A case that
  doesn't depend on the slot, such as an OCLL with the F2L whole, is never
  mirrored: it comes up as written and keeps its algs. When a mirrored step
  stops at such a case, the cube is seen from the front right again, the
  way its alg is done. A case has one card whichever way it comes up.

**Practise in context** (for sets of F2L pair cases, such as a ZZ F2L set):
a cube with EO Cross done and 1 to 4 slots open (the number next to the
button), set up from cases you have started learning where it can be.
Solve the pairs in any order; afterwards each pair you did is shown with
the set's case and alg for it, and the set's cheapest order for the whole
state (Tab shows it as a hint). It isn't graded; Space gives another, Esc
tries the same one again, and Backspace or "Back to learning" returns to
your cases.

### Drill

Drill times you on the cases, on the virtual cube (the default) or a
physical cube, chosen for each alg set under "Cube". The two keep separate
drill times and statistics, and the case list shows the chosen cube's. One
bar above the cube has the alg set, the cube, the cases and the order.
**Cases** is one choice in two parts, where they come from (the ticked
ones, those the list shows, or a group) and which of them (learned in
Learn, all, being learned, in the learn queue, or the weakest), and shows
how many that comes to.

- The pieces the step doesn't care about (e.g. the permutation when
  drilling OLL) are scrambled too, and there's a random U turn before and
  after (as the set allows).
- **On the virtual cube** the case is set up on the cube, which only turns
  as a whole until the timer starts: tap Space to arm it and the first face
  turn starts it (Settings > Timing and solves can have Space held and let go instead;
  a turn made while it is held and green starts it too). The timer stops by
  itself as soon as the cube shows the step done (solved for PLL, the last
  layer oriented for OLL, and so on); Space stops it before that, which is
  a DNF unless the step is done. A DNF says whether you were an AUF off or
  how many pieces were wrong, and shows your moves. Stopped before any face
  turn, the attempt is cancelled.
- **On a physical cube** a real random-state scramble sets the case up, so
  you can't tell the case from the scramble; hold Space and let go to
  start, and any key but Shift, Ctrl or Alt stops the timer. Drill has its
  own hold time (Settings > Timing and solves).
- **Which cases**, apart from the ticks Learn shares: the ticked cases, the
  cases the list shows (searched or filtered) or one group; and of those,
  the ones learned in Learn (the default), all of them, those being
  learned, those in Learn's queue, or the weakest few you have drilled
  (5, in Settings). **The order** is random (never the same case twice in a
  row), weighted (slow and failed cases come up more, by the median of each
  case's recent times so that one slow time can't take over), list order
  (the list can be sorted) or a recap: every case once, then a summary. A
  failed case comes back three cases later (Settings).
- **After each case** you see your time, coloured green at or under the
  set's target time, and the case diagram and algs with the AUFs this case
  needs. For a moment after the stop keys are ignored, so a second press
  doesn't skip it. Space goes on to the next case; Backspace toggles DNF
  (virtual) or marks a wrong alg (physical); Delete (or Ctrl+Z) deletes the
  attempt (one stopped by mistake, say); Esc tries the case again.
- **Giving up**: Shift+Space before or during the solve gives the case up,
  for a case you have forgotten: it is recorded as a DNF. Given up before
  the timer started, Delete takes it back and sets the case up again.
  Settings > Drill can make Backspace the give-up key instead, as in
  earlier versions.
- **Practice**: Tab after a case practises it, set up exactly as it was,
  with no timer and nothing recorded: first with the algs shown, then (Tab,
  or by itself once solved on the virtual cube) with them hidden, then the
  next case once you solve it without help. Space goes on to the next case
  at any point, Backspace still toggles the DNF and Delete still deletes
  the attempt. The virtual cube checks each practice solve and Esc sets the
  case up again; on a physical cube you scramble again. Settings > Drill
  can have Space practise after a DNF (Enter then goes on), and practice
  wait for a key at each stage.
- A set with mirrored cases has each case set up in a random one of its
  mirror images, and a set solved a step at a time is drilled from the
  case to the end of the step, with every step shown afterwards.
- **Statistics**: per case, the recent average, best time (kept for good,
  not only for the last 100 attempts kept), fail rate and weakness (how
  often Weighted picks the case), with each group's row showing its cases
  taken together. Click a column's header to sort by it (again for the
  other way, a third time for list order). Right-click a case for its drill
  history (a chart of its last 100 attempts, and every attempt summed up by
  day as a table and a chart) or to clear its drill times. A tally under
  the list counts the attempts, mean, DNFs and new bests since it was last
  reset (**Reset tally**) and names the slowest cases. On the virtual cube each solved attempt also
  records its recognition time (from the case being set up to the first
  turn of a layer other than U or D) and its execution time, which Learn's
  Recog column and the "slow to recognise" filter use. Drill attempts never
  change the spaced-repetition schedule.

### Case lists

The case lists in Learn, Drill and Library can be searched by case or group
name (so "BL" finds every case in a group named for the BL slot) and
filtered by status: ticked or not, new, in the learn queue, learning,
learned or due in Learn; learned, never drilled or slower than the target
in Drill; without algs, without a passing alg or with failing algs in
Library; slow to recognise in Learn and Drill. In Learn and Drill the
search also matches the cases' algs. While the list is filtered, "Tick
shown" and "Untick shown" change only the cases shown. Double-click a case
in Learn or Drill to practise it now (the keys go back to the cube). A case
is selected in the list only once it is revealed, so the list doesn't name
the case you are recognising.

### Library

Library creates and edits alg sets.

- The page shows the set list, the selected set's case tree and the
  selected case with its diagram, group, algs and Learn and Drill progress.
- **The toolbar** above the lists has the commands used most: New set,
  Set properties, New case from alg, Paste algs, New group and Check algs.
- **A case's buttons** (under its details) show it in **Learn** or
  **Drill** and open it in **Alg Lab**. Its details are on two tabs:
  **Algs**, with the selected alg's fingertricks under the list, and
  **History**, with its Learn card, its drill statistics on each cube and
  buttons for the full **Learn history** and **Drill history** (for sets
  learned by family, Learn's own History shows the family's shared
  schedule).
- **Menus**: in Library mode the menu bar has two more menus. **Set** (also
  on right-click in the set list) creates, duplicates and deletes sets,
  opens the set properties, checks the algs, and restores built-in sets
  and cases (**File** imports and exports sets as JSON). **Cases** (also on
  right-click in the case tree) makes a case from an alg, pastes algs, makes
  a group, and renames, moves (**Move to group**, Move up, Move down) and
  deletes the selected cases or groups. **Edit** gains Undo and Redo.
- Ctrl- or Shift-click selects several cases; drag them (or a group) to
  another place, or use **Move to group** or Alt+Up / Alt+Down.
- Every change is saved at once and can be undone (**Edit > Undo**, Ctrl+Z)
  and redone while v-cube is open, including deleted cases, groups and sets
  with their progress and per-set settings. Deleting a set asks first;
  deleting cases or groups doesn't, and says what went and that Ctrl+Z
  brings it back.
- **Set properties…** (or double-click a set, or Alt+Enter) edits, on three
  tabs with each option explained, the name, description, random U turns
  around cases in Learn and Drill, target time, guided learning, learning
  by family, solving a step at a time and the mirror images cases are also
  set up in (see [Learn](#learn)), and the step settings for the whole set
  or as a group's own override: what the algs may leave unsolved (e.g.
  "last-layer permutation" for OLL-like sets) and how diagrams look (full
  colour, top colour only or grey; from above, or in 3D from any top
  corner). "Follow the pieces" applies the last-layer settings to the
  last-layer pieces wherever they are, and to F2L pieces the step leaves
  free, so that F2L pieces in the top layer keep their colours (F2L sets
  use it). With "top colour only", a D-layer corner the step leaves free
  shows its D sticker instead, as it does when it sits in the last layer. A
  diagram shows a case of the set as the settings draw it, and a line says
  how many algs would stop (or start) passing. Pieces that trade places
  share what may be left unsolved, so a spec like `LLE:p UF:o` is checked
  as `LLE:po`, as drill scrambles have it.
- **Algs**: each case can have several algs (no two the same). Drag them to
  reorder (or Alt+Up / Alt+Down), double-click or F2 to edit, Delete to
  remove, and right-click to make one the main alg. Below the list, type an
  alg to add it.
- Every alg is checked against its case under the step settings in force,
  with a visible result: e.g. `(U') … (U2), 14 moves` for a pass, or
  `leaves 3 corners unsolved (U layer)` or `can't read at character 7` for
  a fail (moves in HTM, a slice turn counting two, as solves count them).
  OLL algs pass whatever last-layer permutation they leave, and algs may
  need a U turn before or after when the set allows it. In a set solved a
  step at a time an alg also passes when it stops at another case of the
  set, shown as `→ Sune, 4 moves`, and the case shows every step from there
  to the end. Failing algs are kept with a warning; the case tree marks
  cases with failing or no passing algs, the set list counts failing algs,
  and **Check algs…** lists them all.
- The main alg is the first passing alg. Each case is drawn, and set up in
  Learn and Drill, from the angle its main alg starts from: a leading `y`,
  `y'`, `y2` or U turn is done to the case, so the rest of the alg solves
  it directly. The stored case and algs don't change.
- **Paste algs…** (Ctrl+Shift+V) takes any list of algs. Lines may look
  like `Name: alg1 / alg2`, as in Alg-Trainer, or be tab-separated
  `name, alg, alg…` columns copied from a spreadsheet, and may end with a
  `// comment`. Each alg goes to the case its line names (by name or id)
  when it solves it; without a name, to the case it solves (any AUF the set
  allows, any cube rotations), so the order doesn't matter. Check shows a
  table of what each alg would do before anything changes: an alg that
  solves another case than the one named, or several cases, or none, isn't
  filed but can be corrected in place or given a case. New cases (when
  allowed) go to the group you pick; a set solved a step at a time gets
  none. Apply adds the rest, and algs left out come back the next time you
  paste. Algs copied from web pages are fine: curly or accented primes and
  no-break spaces are stored as plain ' and spaces, and repeats may be
  written `(R U R' U')2`, `x2`, `*2` or `×2`.
- **Import and export**: sets are JSON files ([the
  format](file-formats.md#alg-sets)), imported and exported from the Set
  menu or the File menu. Each set is saved back to the file it came from;
  if two files hold the same set id (such as a copy made in a file
  manager), only one is loaded and the other is reported.
- **Built-in sets**: **Set > Restore a built-in set** brings back a deleted
  built-in set, and **Restore built-in cases…** its shipped cases (their
  names and states, and any you deleted), keeping your algs, the cases and
  groups you added and the set's settings.

### Getting algs into a set

1. In **Library** mode, pick a set (e.g. PLL) and choose **Cases > Paste
   algs…** (Ctrl+Shift+V).
2. Paste your list and click **Check**. Each alg is matched to its case
   (the one its line names, or the one it solves), and the table shows
   where each goes; fix or assign the ones that aren't filed, then
   **Apply**. Each alg's check result is shown next to it in its case.
3. In a case's alg list, drag an alg to the top or right-click it and
   choose **Make main** to pick which alg Learn and Drill show first.

For your own sets, **Set > New set…** (Ctrl+N) asks what the algs solve,
then **Cases > New case from alg…** (Ctrl+Shift+A) or **Paste algs…** (with
"Make new cases" ticked) builds the cases. If you change a set's step
settings later, **Set > Check algs…** (Ctrl+Shift+K) lists the algs that no
longer pass.

### Alg Lab

Alg Lab finds, compares and times algs for a case, so that you can choose
the best one for your hands. Each alg keeps its own times.

**The case.** The drop-down at the top left picks an alg set (its cases
are listed in their groups) or **Lab cases**, Alg Lab's own cases for
states that aren't in any set. **New…** makes a lab case from a setup alg
(the moves that make the case from a solved cube) and what its algs must
solve: the whole cube, the last layer oriented, the F2L, the steps of one
of your sets, or your own free pieces (`LL:p`, `LLE:o UF:p`, as in the set
properties), and whether a U turn may come before or after the alg.
**Edit…** (or a double-click) changes it and **Delete** removes it with
its candidates and times. Library's alg list has **Open in Alg Lab** on a
case's algs.

**Search tab.** The case's candidate algs: the set's own algs (the main
alg in bold), then the ones you typed, pasted or generated. An alg that
doesn't solve the case is greyed out with the reason. For each:

- move counts in **HTM** (a slice counts 2), **STM** (any layer turn is
  1), **QTM** (half turns count 2) and **ETM** (every move, rotations too);
- an ergonomic **Score** (lower is easier, see below);
- its times on the cube chosen (physical or virtual): the number counted,
  mean, median, best, current ao5 and ao12, standard deviation and DNF
  rate, over all time, the current test session or the last 50 attempts.

Click a column header to sort by it. **Show** filters the list to the algs
of a move set (such as `<R, U>`), **No rotations** hides algs with x, y or
z, and the length box hides algs over that many moves (HTM). Type an alg
under the list and press Enter to add it, or **Paste…** a list (one alg a
line; `name: alg / alg` lines and `//` comments are fine, and algs that
don't solve the case are left out). **Add to set** adds the selected algs
to the case in its set and **Make main alg** puts one first; both are
Library edits, so Library's Undo takes them back. **Add to session** puts
them in the current test session. **Remove** forgets kept candidates (a
set's own algs are removed in Library) and **Clear times…** forgets the
selected algs' times. Right-click for the same, and Copy.

**The generator** (the **Generator** button with the arrow folds it away
or shows it) finds every alg of a move set that solves the case,
shortest first, and adds them as candidates while it runs:

- **Move set**: a preset (`<U, R>`, `<U, R, F>`, `<U, R, D>`, `<U, R, L>`,
  `<U, R, F, D>`, `<U, R, D, F2>`, `<U, R, r>`, `<U, R, M>`,
  `<U, R, F, r>`, `<U, R, F, L, B>`, all faces), one of your own, or
  **Custom…**: a grid of every face, wide turn and slice with its quarter
  and half turns, also typed as text (`R, U, F2`: a 2 allows half turns
  only, a 1 quarter turns only). Give it a name to keep it as a preset;
  **…** edits the move set or deletes one of your presets.
- **Shortest +** stops that many moves after the shortest alg found;
  **Every length** goes on to the limit. **Up to** is the longest alg it
  looks for, and **at most** stops it after that many algs. Lengths count
  every layer turn as one move (STM).
- A U turn before and after the alg is allowed as the case allows (the
  set's random U turns, or the lab case's), and an alg never starts or
  ends with a U turn that would only be that AUF. Algs never turn a layer
  twice in a row and turn the layers of one axis (R, L, r, M) in one order.
  Wide turns and slices are followed with the way they turn the cube, so
  after `r` a U turn turns the layer that was in front.
- **Find algs** searches the case; **Whole group…** every case of its group
  or of its set, one after another. **Stop** ends a search. The line under
  the controls says how far it got: the number of algs, the shortest, and
  the length to which every alg was found.
- The first search with a move set and step builds tables for it, which
  takes a moment for all six faces and a solved goal (about 3 seconds)
  and much less for smaller move sets; later searches with the same moves
  and step reuse them. Restricted move sets search deep quickly (a ZBLL in
  `<U, R>` to 14 moves takes well under a second); all six faces past 13
  or 14 moves can take minutes, so stop it or lower the limit.

**Ergonomic score.** The score comes from the profile chosen beside it.
The default, **Two-handed grips**, is a grip profile: the score is the
cheapest way to turn the alg with two hands, adding up a fingertrick for
each move and the regrips between them (see **Fingertricks** below).
**Right-handed 2H** and **One-handed (right)** are move profiles: each
move has a cost (`R`, `R'`, `R2`, `r`, `M'`, `x` ...), and there is a cost
for each turn on another axis than the turn before and rules for awkward
pairs of moves in a row (each side a move such as `R2`, a layer with any
turn such as `R*`, or any move `*`; `R2` then `F*` is a regrip). Their
score is the sum. **Profile…** edits the chosen profile (with a field to
try an alg against it), makes a new one from it, makes a new grip profile,
deletes it, or restores the built-in ones.

**Undo.** On the Search tab, Ctrl+Z (Edit > Undo) takes back an alg added
to the set or made its main alg there.

**Fingertricks.** Under the candidates, the selected alg is shown as it is
best turned with two hands, from the chosen grip profile (or the first
grip profile, when a move profile is chosen). A hand's grip is where its
thumb sits as the wrist turns it: **home**, **up** a quarter turn (the
right thumb after `R` from home, the left thumb after `L'`), **down**, or
**far up** or **far down**, a strain. The line at the top gives the grips
to start in, the cost (the tricks', then the regrips' and strain's) and
the number of regrips. Below it is a column per move, with the trick each
hand turns it with (`index flick`, `wrist up`, `ring pull` ...), where the
thumb ends up after a wrist turn, and **↻ regrip** where a hand moves to
another grip before the move. A regrip while the other hand turns costs
less than one that pauses the alg, so the plan puts regrips there when it
can. Click a move to list every trick that can turn it, with what the
whole alg costs that way: the best first, then how much more each other
one costs. Click one to use it, and the rest of the alg is planned round
it; **Reset picks** goes back to the best plan (picks last until another
alg is selected). **Copy** copies the plan as text. Library shows the
same under a case's algs, for the alg selected there.

A grip profile's tricks are its own to change: **Profile… > Edit this
profile** lists every trick, with the move it turns, the hand and finger,
how it turns it, the grips it starts from (`home, up`), the steps it
moves the hand's thumb (`R` is +1 for the right hand, `L` is -1 for the
left) and its cost. Turn off the tricks you don't use, change the costs to
suit your hands, or add tricks; **Standard tricks** puts back v-cube's.
The costs of a regrip, of one while the other hand turns, of a hand far up
or down, of starting away from home and of one finger turning two moves in
a row are set there too (where the hands end up after the last move is
never a strain). Moves no trick turns (`E`, `S`, wide `u` and the
like) cost a flat amount and show "no trick". The standard tricks are:

- U layer: right index flick (`U`, thumb home or up), left index flick
  (`U'`, thumb home or up), double flicks (`U2`, from home), left index
  push (`U`), right index push (`U'`, best with the thumb down);
- R and L: the wrists (`R2` up or down a half turn);
- D layer, from home: left ring pull (`D`), right ring pull (`D'`) and the
  pushes the other way, right ring double pull (`D2`);
- F layer: the right index from the top with the right thumb down (`F`
  and `F2`, as after `R'`), the right ring finger from below with the
  right thumb up (`F`, `F'` and `F2`, as after `R`), the thumbs from the
  front (left for `F` at home, right for `F'` at home or, better, with
  the thumb down), the left index from the top with the left thumb down
  (`F'` and `F2`), and a slower right index reach over the top (`F`);
- B layer: the right index from below with the right thumb up and the
  right ring finger from the top with the right thumb down (`B`, `B'` and
  `B2`), and the left index from below with the left thumb up;
- wide `r` and `l` (as the wrists), `M` with the right ring finger, and
  rotations, which let go with both hands and take the cube again home.

**Test tab.** Test sessions time algs one at a time. A session lists
algs, each with its case (from any cases and sets), and has:

- the **cube**: physical (a random-state scramble sets the case up, so
  the scramble never hints at an alg; hold Space and let go to start, any
  key stops) or virtual (the case is set up on the cube, your first turn
  starts the timer and it stops when the case is solved; Space stops it
  early, a DNF);
- the **order**: interleaved (rounds of every alg once, in a random order,
  never the same alg twice in a row) or blocked (every rep of one alg,
  then the next), and the **reps** of each alg;
- the **warm-up** reps of each alg left out of the statistics at the start
  of the session, and the **trim**, the share of the fastest and slowest
  times left out of the mean, median and SD (averages trim as the WCA
  does);
- **Random AUF**: off, each alg's case is set up from the side the alg
  starts from, so only the alg is timed; on, a random U turn comes first
  and the AUF is timed too.

Space starts the session. Each attempt names the alg to do and shows the
case and, on a physical cube, the scramble. On the virtual cube your moves
are checked against the alg (as Learn checks them): a time where they left
it is marked and not counted, and the Times tab can count it. After an
attempt Space goes on, Backspace toggles DNF, Delete deletes the time and
Esc does the same alg again; Esc before or during an attempt sets the case
up again. Double-click an alg in the session's list to time it next.
When every alg has its reps, raise the reps or add algs to go on.

Below, **Compare** has the session's algs side by side (this session or
all time): the statistics table and a chart, either the spread of each
alg's times (each time a dot, with its middle half as a box and the median
as a line) or the times in the order they were done. Hover a dot for its
time. **Times** lists the session's attempts, newest first; right-click
one to toggle DNF, count or stop counting a time that left the alg, or
delete it. Every alg's times are kept over all sessions (and deleting a
session keeps them), apart for each cube, and Library's alg list shows
each alg's mean and count.

## Solves

Timed, Untimed and Move Count each keep their own sessions, and each
session is for the virtual cube (the default) or a physical one: pick it
with the cube drop-down next to the scramble type. Sessions saved before
there was a choice are for a physical cube. A session's cube (and Move
Count step) is fixed once it has solves: start a new session for another.

**On the virtual cube** each scramble is put on the cube, which can only be
turned as a whole (to look at it from any side) until the solve starts, so
a slip can't turn a face early. Each solve keeps its scramble and its
solution, the moves as you did them with turns of the same layer joined (R
R is R2, R R' nothing), and its move count in HTM (a slice turn counts two,
rotations nothing). Counts are simplified by default: turns that cancel
across a turn of the opposite layer or a rotation about the same axis don't
count (U D U' is one move, R x R' none); Settings > Timing and solves turns this off
to count every turn as done.

**The solve list**: hover over a solve to see its date, scramble and
solution, right-click it to copy either or open **Details…**, and a solve
with a comment is marked `*`. **Delete last** (Delete) sits under the list,
and Ctrl+Z puts the last deleted solve back. Click a statistic to see its
solves (brackets mark the ones an average drops); a new best single, mo3 or
average is announced after the solve. Averages are rounded to the shown
decimals as the WCA does (singles truncated first). The chart's
right-click menu charts the last 12 to 500 solves; hover over it for a
solve's values.

### Timed

Hold Space to start (the hold time is in Settings > Timing and solves, any time up to
2 s).

- **Physical cube**: a small net next to the scramble shows the scrambled
  cube, white on top and green in front, and any key stops the timer.
- **Virtual cube**: the scrambled cube is shown, rotations only, until the
  timer starts. The timer stops when the cube is solved (Settings > Timing and solves
  can leave that to Space). Space stops it too; if the cube isn't solved
  then, the solve is a DNF, and the cube stays as you left it to look at
  until Space (or Enter or Esc) brings the next scramble. Esc during a
  solve cancels it and puts the scramble back. Each move's time is kept,
  which gives the **Moves**, **TPS**, **Cross**, **F2L** and **LL** columns
  (EO Cross for a ZZ solve; right-click the header to choose columns) and,
  from the chart's menu, cross/F2L/LL lines. After a solve its EO Cross
  count is shown with the optimal. **Review last** (Alt+R, or double-click
  a solve) opens the [review](#review).
- **Inspection**: optional WCA inspection (Settings > Timing and solves), with +2 and
  DNF applied automatically and optional beeps at 8 s and 12 s; tap Space
  to start inspecting, then hold Space to start. On the virtual cube the
  scrambled cube can be hidden until inspection starts (a setting).
- **Sessions**: ao5/ao12/ao100, mo3, best, mean, +2/DNF/comments per solve
  (Alt+1, Alt+2, Alt+3 and Alt+C for the last solve), and a trend chart.
  The session menu switches, adds, renames and deletes sessions, and
  imports and exports cstimer sessions. Imported sessions are for a
  physical cube; empty sessions and sessions for other puzzles are left
  out, and virtual and smart cube solutions come with their move times. The
  export keeps cstimer's scramble types and solutions; import it in cstimer
  with **Import session(s) from other timers**, which adds the sessions to
  yours (cstimer's "Import from file" would replace all your data).

### Untimed

Solves with no timer.

- **Virtual cube**: hold Space until the hint turns green and release it to
  start the solve, as in Timed; it ends when the cube is solved (or on
  Space, as a DNF if it isn't). No move count is shown during the solve;
  afterwards "Last: N moves" shows it, with your EO Cross count and the
  optimal. After the solve the cube turns freely, and the alg field applies
  a typed alg with animation; Esc resets the cube to solved, and Space or
  Enter gives the next scramble. During a solve Esc puts the scramble back.
- **Physical cube**: large scramble text with a net preview. Space or →
  keeps the scramble as done and shows the next, Backspace or ← goes back
  through the last 100 (and → forward again). Done scrambles are listed in
  the session with their dates, and the statistics count them (in all and
  today); right-click one to copy it.
- Sessions show mo3, ao5/ao12/ao100, best and mean over the move counts of
  virtual solves, and a trend chart.

### Move Count

Move efficiency for one step at a time, with no timer. Each session is for
one step: EO, EO+1 or EO+2 (EO with any one or two cross edges), EOLine (EO
with the two cross edges on the EO axis), EO Cross, XEOCross (EO Cross and
any one F2L pair), ZZ F2L (the first two layers with every edge oriented),
Cross, or CFOP F2L (the first two layers, edges in any orientation). New
installs start with one virtual-cube session per step.

- **Virtual cube**: hold Space until the count turns green and release it
  to start the solve, as in Timed; Space ends it. The move count is
  recorded if the step is done, and DNF if not, and the next scramble
  follows. The step may be done on any colour and any axis: EO is checked
  as ZZ does (edges that need no F or B quarter turns) with the cube held
  any way, and the cross may be on any face. Esc puts the scramble back.
- **Physical cube**: solve the step, then type how many moves your solution
  took and press Enter to record it and get the next scramble. Type the
  count with the digit keys (or keypad); Backspace corrects it and Esc
  clears it. **DNF** (Alt+3) records a scramble whose step went wrong.
- Sessions work as in Timed, with mo3, ao5/ao12/ao100, best, mean, DNF and
  comments per scramble, and a trend chart, all over move counts. **Skip
  scramble** (Ctrl+→) moves on without recording, and right-clicking a
  count edits it, toggles DNF, adds a comment or copies its scramble or
  solution.
- The **Opt** column has each scramble's optimal move count for the
  session's step (EO to EO Cross, and the cross) in any hold, as the step
  is checked (Settings > Timing and solves can limit it to the holds of your colour
  neutrality, either EO axis allowed), and the statistics add the mean
  moves over optimal and how often you were optimal.
- Each session keeps its own scramble type. Choosing a step picks a WCA
  scramble, EO Cross solved for ZZ F2L, or the cross solved for CFOP F2L.
  Only the scramble types that suit these steps are offered: WCA random
  state (for EO, EOLine, EO Cross or cross), EOLine solved, EOCross solved
  (ZZ F2L) and F2L (cross solved, for CFOP F2L). Colour neutrality applies,
  as everywhere.
- **Retry the worst 5** (session menu, Untimed and Move Count on the
  virtual cube) retries the DNFs and the solves furthest over the optimal
  (or longest) one after another, without recording them.

### Review

**Review** (double-click a solve, right-click > Review…, **Review last** or
Alt+R) works for Timed, Untimed and Move Count solves. It replays the solve
on a cube of its own (←/→, Home/End or the slider) with the moves where EO,
EO+1, EO+2, EO Cross and ZZ F2L were first done marked, and how many moves
each took and in which hold. For EO to EO Cross and the cross it lists the
optimal solutions in your holds and over every colour and axis ("show"
plays one).

For a ZZ F2L (or whole) solve it splits the F2L into pairs and compares
each with an alg set of F2L pair cases (your ZZ F2L set; any set whose
cases each solve one slot's pair, with its mirrors covering the other
slots): the case the pair was, your moves against your alg and against the
optimal <R, U, L> pair, the cheapest pair at that point, and the set's
cheapest order overall. A whole solve also gets its splits (cross, each
pair, last layer: moves, and time and TPS for a timed solve). **Copy
reconstruction** copies the solve a step per line, and **alg.cubing.net**
opens it there.

**Retry this scramble** (virtual cube) puts the scramble back on the cube
to solve again. The retry isn't recorded; you see its count against the
original and the optimal, and Space (or Skip scramble) goes back to new
scrambles.

### Scramble types

In Timed and Untimed:

- WCA random state.
- cstimer-style subsets: F2L/cross solved, XCross, EOLine, EOCross, LSLL,
  LL, ZBLL, CLL, ELL, PLL, EPLL, corners only, edges only.
- Any of your alg sets, using its ticked cases, in the set's mirror images
  too. Tab (or the Algs button) shows or hides the algs for the case each
  scramble sets up, with the AUFs it needs (every step to the end for a set
  solved a step at a time); nothing is graded or recorded.

## Practice

### EO Cross

The start of a ZZ solve practised a part at a time on the virtual cube.
Each drill has a tab at the top of the page; every one is timed from the
moment its state is shown, so each opens on the ready page.

**What counts.** A step (EO, EOLine, EO+1, EO+2 or EO Cross, chosen under
**Step**) is done in any hold your [colour neutrality](#colour-neutrality)
allows, on either EO axis: with white-or-yellow crosses that is four ways
to do EO Cross. The cube is first shown in your main hold; turn it as a
whole to solve from another. Your moves are counted in HTM and compared
with the **optimum**: the shortest solution over all of those holds,
searched for the state on the cube. A step done in a hold that isn't one
of yours doesn't count.

**The states** are random ones, grouped by **kind**: the number of edges
bad for the front-back axis as the cube is first shown (0, 2, ... 12). The
kinds come up roughly as often as a scramble would give them, the rare
ones a little more, and the kinds that have gone badly lately more often
still. **Bad edges** in the bar pins one kind instead.

- **Bad edges**: a state is shown in one of your holds. Type how many edges
  are bad for the F/B axis (the ZZ axis as held; the prompt names its two
  colours), then Enter; with **Axes** set to *F/B and R/L*, the R/L count
  follows. Rotations turn the cube to look at it. The counts are checked
  and the bad edges ringed; Tab rings the other axis's.
- **Axis choice**: turn the cube to the hold you would do the step from
  (the cross colour on the bottom, the EO axis front to back) and press
  Space, within the inspection time. Every hold is then listed with its
  optimum, yours marked: the try records how many moves your pick costs
  over the best hold.
- **Plan blind**: look at the cube (rotations only) within the inspection
  time and plan the step. Space hides the cube (so does the time running
  out; a Space just then is ignored), and you do the plan with the turning
  keys without seeing it. Space shows the cube again: the step is checked
  (a DNF if it isn't done, with how far it got), and the moves compared
  with the optimum. Esc while hidden starts the moves again; Space with no
  moves done doesn't finish. The time kept is the inspection time used.
- **Step solutions**: solve the step in view. The state ends by itself when
  the step is done in one of your holds, and your moves are shown beside
  the optimal solutions. Tab gives a hint: the hold nearest the step if
  the cube isn't in it, else the next move of an optimal solution from
  where the cube is (the try counts as hinted). Esc puts the cube back,
  Backspace gives up.
- **Full EO Cross**: a whole EO Cross, solved the same way, then **where
  the moves went**: the solve is cut where EO, EO+1, EO+2 and EO Cross
  were first done in the hold you solved in, and each part is shown with
  its moves, how far EO Cross then was at best, and what it **lost**: its
  moves less how much nearer EO Cross it brought the cube. An optimal EO
  followed by a long cross shows as a loss in the EO part, where the
  choice was made.

**Inspection** (Axis choice and Plan blind): WCA (15 s, the countdown
turning colour at 8 and 12 seconds, with the calls as beeps if Settings >
Timing and solves has them on), 5 to 30 s, or no limit.

**Afterwards** every hold is listed with its optimum and a few optimal
solutions that aren't reorderings of one another. Click one, or press its
number (Enter for the first), to play it from the start in its hold; the
turning keys turn the cube freely; Esc gives the same state again,
unrecorded; Space goes on.

**The list** on the left has the drills with their kinds for the step in
hand: tries, **Extra** (your moves over the optimum lately; for Bad edges
how far off the counts were, for Axis choice what your hold cost over the
best; a hint counts 2 more, giving up or a DNF 4) and mean time. Kinds
that go badly are marked. Double-click a kind to practise only it.

### F2L

The F2L practised a part at a time on the virtual cube, instead of as a
set of algs to memorise. Every state has the cross done; your moves are
compared with solutions searched for that very state, so open slots get
used and nothing depends on a case list.

**The F2L of your method** is chosen in the bar:

- **F2L**: *ZZ F2L - 1 (ZZ-CT)*, the default, leaves one slot for the
  last-slot step; *ZZ F2L (ZZ-OP, ZZ-A)* solves all four; *CFOP F2L* and
  *CFOP F2L - 1* are the same without EO. In the ZZ methods every edge
  starts oriented and no solution shown ever changes that.
- **Last slot** (F2L - 1): the slots that may be the one left, FR and FL
  by default. The F2L is done when all but one of those is solved.
- **Solutions use**: R, U and L always; **D turns** and **F2 and B2** as
  well by default (both keep EO). Without EO, F and B turn freely and
  **wide turns** can be allowed, at a cost of 1.0 each on top of what your
  profile says. No solution ever has a slice turn or a rotation.

The cube is shown in the **flat view** by default, which hides what a real
cube hides: the back slots and the back of the top layer. Pair choice
counts that.

Solutions are ranked by **cost**: the fingertrick cost of the score profile
chosen in Alg Lab, among the solutions within two moves of the shortest.
The matching alg of your ZZ F2L set (the set the solve review uses) is
listed with them and marked, so you can see when the alg you know is
already the best and when the state offers something better.

The four drills are the tabs at the top:

- **Pair solutions**: one pair to solve, ringed on the cube and named in
  the prompt. The clock runs from the moment the state is shown, which is
  when you press `Space` on the drill's ready page. Afterwards
  your moves and their cost stand beside the best solutions; click one, or
  press its number, to see it, `Enter` to play it and `←` `→` to step
  through it. `Tab` while solving shows the best solution's next move (the
  try then counts as hinted); `Backspace` gives up. **Pair** picks where in
  the solve the pair comes: *Any* mixes first pairs, with every slot open,
  up to last pairs.
- **Pair choice**: several pairs are open; solve the one you would do
  first. The pairs are then ranked by **total**: the pair's best solution,
  plus the cheapest way through the pairs still needed after it, plus a
  penalty for a pair whose pieces the flat view hides at the start (2.0
  for a piece that can't be seen at all, 1.0 for one that shows too little
  to tell which it is). Your pick and how you solved it are judged
  separately. `Tab` names the best pair; the pick then isn't counted.
- **Full F2L**: the whole F2L of the method. Afterwards each pair you
  solved is listed with your moves, the best solution for it from where
  you were, and the pair that would have been the best one to do then.
- **Case explorer**: the pair cases with their ranked solutions, set up in
  a slot of your choice with the other slots solved or open. `↑` `↓` move
  through the cases, `Space` gives another example. Nothing is recorded.

**The case list** on the left has the 83 pair cases of a ZZ F2L (167
without EO): where the corner and the edge are, seen for the front-right
slot; the other slots' cases are its mirror images. For each it shows how
many times you solved it in any drill, how far over the best solution you
were lately (a hint counts as 2.0 over, giving up as 4.0) and your time.
A case that is costly or slow against your other cases is **weak**: it is
marked, and Pair solutions brings it up more often, along with the cases
you haven't tried. Double-click a case to open it in the Case explorer.
Above the list are your Pair choice record (how often you picked the best
pair) and your Full F2L averages. `Esc` after a result gives the same
state again without recording it.

### TSLE

ZZ-CT's last-slot step, practised a trigger at a time on the virtual cube.
With EO done and three slots solved, TSLE puts the last slot's edge in and
orients the five corners that are left (the last layer's and the slot's),
wherever they are. It is done with the slot's three triggers (R U R',
R U' R' and R U2 R' for the front-right slot, each after a U turn if it
needs one) until the step is done or only an OCLL is left, which an OCLL
alg then finishes.

There are 104 cases (97 and the seven OCLLs). The mode doesn't hold one
trigger per case. From every state it knows **every good trigger**:

- A trigger is **good** when it is on a way to the end with the fewest
  steps, where a step is a trigger or the OCLL that ends a chain. No case
  needs more than three steps this way.
- Of the good ones, the **best** is the one with the fewest moves to the
  end from the AUF the cube is at: the trigger that needs no U turn first
  usually wins, and what it leaves matters too (an OCLL alg that cancels
  into the trigger, a short OCLL rather than a long one).
- Where a way with one more step has fewer moves in all, the list says so
  ("a step more, 2 moves fewer"); it isn't counted as good.

**Last slot** picks the slots practised: FR and FL by default, BL and BR
as well if you like. Each attempt has a random one of them, with that
slot's triggers (L' U L for the front-left slot, and so on); a case is
tracked once whatever the slot. OCLL algs are always shown as they are
written, with the U turn they need, never mirrored. **OCLL algs…** changes
them; when you solve, any alg that works counts. The cube is shown in the
flat view by default.

The drills are the tabs at the top:

- **Guided intro** brings the cases in one at a time, fewest steps first.
  For a new case each good trigger is shown in turn, with the cube turned
  so that it needs no AUF: `Enter` shows it on the cube, then you do it.
  After that you pick the best trigger from two random AUFs, and the case
  is introduced. `Space` skips a case you know.
- **Next trigger** (which, like Full TSLE, opens on a ready page and
  shows its first state on `Space`): a state at a random AUF; do the next step (the AUF and
  one trigger, or the OCLL alg). It is judged as the best trigger from
  that AUF, a good one that costs more moves, or not a good one, and every
  good trigger is listed with what it leaves and the moves to the end.
  `Tab` shows the best step, `Backspace` gives up.
- **Full TSLE**: the whole step, timed. Afterwards each trigger you did is
  listed with its verdict and the best one at that point.
- **Case explorer**: pick a case in the list (or `↑` `↓`); `Space` turns
  it to its next AUF. The list of good triggers follows the cube: do a
  trigger with `Enter`, its number, a click or the turning keys and the
  list is that of the case you land in, down to the end. Nothing is
  recorded.

**Cases** (Next trigger and Full TSLE) is *Introduced* by default: only
the cases Guided intro has been through come up, weak ones more often.
*All* opens every case at once. The cases with two steps left open in
Guided intro once the one-step cases are solid (all introduced, and three
quarters of them tried and not weak), and likewise the three-step ones.

**The case list** groups the cases by steps left and draws each for the
front-right slot with the edge at UF. A name such as `UF CW 0120` gives
the edge (UF, or FR for in the slot), the slot's corner place (N oriented,
CW, CCW) and the twists at URF, UFL, ULB and UBR (0 oriented, 1 clockwise,
2 anticlockwise); the OCLLs have their names. **Extra** is your moves over
the best way lately, where a trigger that isn't good counts as 3, a hint
as 2 more and giving up as 4.

## Colour neutrality

Edit > Colour neutrality… (`Ctrl+Shift+N`), or the Settings tab of the same
name, applies to every mode. Scrambles are always applied holding white on
top and green in front; you choose the holds you solve in, and each
scramble is set up for one of them, picked at random.

- Presets start from a main hold (e.g. yellow top, green front): fixed, y2
  (front colour or its opposite), y (any front), x2 (top colour or its
  opposite), x2 y2, x2 y (top or its opposite, any front) and full (any of
  the 24 holds). Or tick any set of holds in the top/front grid.
- For example, an EOCross scramble held x2 y neutral from white top, green
  front gives a yellow or a white cross (on the bottom once you turn the
  cube to the hold), with any front colour.
- Learn and virtual Drill show each case on the virtual cube in the colours
  of its hold, and physical Drill's scrambles set the case up for its hold.
- Case diagrams and thumbnails are drawn in the main hold.

## The virtual cube

- Choose the view in each mode: a flat view of the U and F faces with the
  adjacent L and R stickers (as in Alg-Trainer, and the default), animated
  3D, an unfolded net, or hidden. Learn remembers the view for each alg
  set, and its 3D view (and Drill's) looks from the corner the case's
  diagrams are drawn from (e.g. front left for left-slot F2L cases).
- It is turned from the keyboard with cstimer's face keys (Shift for wide
  moves), which you can remap in Settings > Cube and keys; see
  [keys.md](keys.md#turning-keys).

## Settings

Edit > Settings (`Ctrl+,`) has these tabs:

- **Appearance**: theme, sticker colours, the text sizes of the scramble,
  the timer and the hints and messages, the case pictures' size in Learn
  and Drill, and the decimal places of times.
- **Colour neutrality**: see [above](#colour-neutrality).
- **Timing and solves**: how every timer starts and stops, in one place.
  The hold time before the timer starts in the solve modes and in Drill
  (any time up to 2 s), how Drill's virtual-cube timer starts (tap Space
  then turn, or hold Space and let go) and whether it stops by itself, WCA
  inspection with optional beeps at 8 s and 12 s, whether the virtual cube
  stops the timer when solved (Timed and Untimed), hiding the scrambled
  cube until inspection starts, simplified move counts, and whether Move
  Count's Opt column is over any hold or your colour neutrality's.
- **Cube and keys**: the virtual cube's turn speed and the turning keys
  (the digits, arrows, function keys and the modes' own keys can't be
  bound).
- **Learn**: the spaced-repetition scheduler (see [Learn](#learn)).
- **Drill**: the give-up key (Shift+Space or Backspace), what Space does
  after a DNF, whether practice goes on by itself, how many cases later a
  failed case comes back, and how many cases "Weakest" drills.

Each option's explanation is written under it. **Apply** applies the
settings without closing the dialog.

Where the settings and the rest of your data are kept is in
[file-formats.md](file-formats.md).
