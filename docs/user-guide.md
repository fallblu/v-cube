# User guide

What each mode does and how to use it. [keys.md](keys.md) lists every key,
and `F1` in the app shows the current mode's.

- [The window](#the-window)
- [Alg sets](#alg-sets): [Learn](#learn), [Drill](#drill),
  [case lists](#case-lists), [Library](#library), [Alg Lab](#alg-lab)
- [Solves](#solves): [Timed](#timed), [Untimed](#untimed),
  [Move Count](#move-count), [Review](#review),
  [scramble types](#scramble-types)
- [Practice](#practice): [Learn](#learn-1), [Inspection](#inspection), [Untimed](#untimed-1) and [Timed](#timed-1) tabs, [EO Cross](#eo-cross), [F2L](#f2l), [TSLE](#tsle)
- [Colour neutrality](#colour-neutrality),
  [the virtual cube](#the-virtual-cube), [Settings](#settings)

## The window

The sidebar has **Today** and ten modes in three groups:

| Group | Modes | Keys |
| --- | --- | --- |
| | Today | `Ctrl+0` |
| Algs | Learn, Drill, Library, Alg Lab | `Ctrl+1` to `Ctrl+4` |
| Solves | Timed, Untimed, Move Count | `Ctrl+5` to `Ctrl+7` |
| Practice | EO Cross, F2L, TSLE | `Ctrl+8`, `Ctrl+9`, `Ctrl+-` |

The **Go** menu has them too. You can't switch modes while a solve or
inspection is running.

**Nothing timed starts by itself.** Learn times a case from the moment
it is shown, so it opens on a ready page and shows nothing until you press
`Space`, and so does EO Cross's Inspection tab. Timed, Drill, Alg Lab and
the Timed tabs of EO Cross, F2L and TSLE wait for Space or the first turn
anyway.

- **Today** shows, for each alg set, the reviews due today, the new cases
  Learn can still bring in today, the cases queued and the cases learned.
  The cards of F2L and TSLE mode are listed with the sets, with a button
  that opens the mode's Learn tab. It also shows your practice streak and what you did in each mode, with
  the time spent, today and over the last 7 days. The button at the top
  says what to do next: review every set's due cases together, else F2L or
  TSLE cards that are due, else learn new cases (or that nothing is due).
  Each row has a button to **Review due**, **Learn new** or **Open** it.
  From the keyboard, ↑ and ↓ pick a row, Enter opens it, R reviews every
  set and Space presses the button at the top. The sidebar's Today and
  Learn buttons show the number of reviews due on their right.
- **Keyboard controls** (`F1`): the keys, in a panel beside the page, with
  the current mode's first and your own turning keys. The panel can be
  undocked.
- **Command list** (`Ctrl+K`): type part of any menu command, or of a
  choice on the page (alg set, session, scramble type, cube, view), and
  press Enter. Commands are grouped under **Go** (the modes), **This
  page** (the page's choices and its menu) and **Menus**, with their keys
  on the right; with nothing typed, the last few commands you ran come
  first. Each mode's own menu (named after it) has what its lists offer
  on a right-click: in Learn and Drill also Tick all, Tick none and the
  list's columns (and Drill's Reset tally), in Timed, Untimed and Move
  Count the session table's columns and each statistic's solves.
- **Side panel** (`F9`): hides or shows the mode's side panel (the case
  list, the session, the case browser), giving the cube the whole page.
  Each mode remembers it. In a narrow window the panel hides by itself and
  comes back when the window is wide again; F9 shows it meanwhile.
- **Sidebar** (`Ctrl+B`), **full screen** (`F11`) and **focus mode**
  (`F10`). Focus mode hides the sidebar, menus, status bar and each mode's
  lists and options, leaving the scramble, timer, cube and algs; messages
  show at the top of the window instead. Right-click outside those to
  switch any of the three with the mouse.
- **The mode's menu**: Learn, Drill and Alg Lab add a menu named after the
  mode, with what a right-click in the mode's list offers.
- **Keys on the page**: under the cube, each practice mode shows a short
  message and the keys that do something right now. A key whose action is
  in the link colour can be clicked too, e.g. DNF and delete in Drill, or
  +2, DNF, delete and review in Timed.
- **Tips in the status bar**: nothing pops up beside the mouse; pointing
  at a control, a list column or a statistic writes what it does in the
  status bar instead.
- The window reopens where you left it, with the same list widths, but
  never in full screen or focus mode. It can be as narrow as half a 1080p
  screen; option rows wrap, and the chart gives up its room before the cube
  does.
- Starting v-cube while it is running brings the running window to the
  front.
- **File** imports and exports alg sets, exports all your data and opens
  the data folder. See [file-formats.md](file-formats.md).

## Alg sets

An alg set is a list of cases in groups, each case with its algs. The
built-in sets are PLL (21 cases), OLL (57), 2-look OLL (10) and 2-look PLL
(6). They ship as case definitions with no algs, so you
[add your own](#getting-algs-into-a-set).

Learn and Drill share each set's ticked cases and learn queue. They offer
only cases with at least one alg that solves them, and say how many are
skipped. Cases are set up for the holds chosen under
[colour neutrality](#colour-neutrality).

### Learn

Learn teaches a set's cases on the virtual cube, untimed, and schedules
them by spaced repetition.

**A case.** Press `Space` on the ready page for the first case. Each case
is set up on the cube and you solve it from memory; Tab shows the algs.

- **Grading**: solving the case with one of its algs and no hint is Good.
  A hint, giving up (Shift+Space) or leaving the algs is Again. Keys 1-4
  change the grade afterwards, and each grade button shows the interval it
  would give. Ctrl+Z takes back the last grade (up to 10) and brings the
  case up again.
- **Following the alg**: U turns and rotations are free until your first
  turn of a layer other than U or D, so you can look at the case and line
  it up. From then on your moves must follow one of the case's algs. The
  same turns done another way still count: rotations, a half turn as two
  quarter turns, `R3` as `R'`, a wide or slice turn as the turns it is made
  of (`r` as `R M'`), opposite faces in either order, and a final U turn
  the alg leaves out or adds. Once only U turns are left, any U turns
  count. A wrong turn (even one you undo), or Esc once the alg is begun,
  leaves the algs. The status line says so at once, and afterwards your
  moves are shown under the algs with the wrong move marked.
- **When it is finished**: when the cube reaches the step's end state. For
  PLL that is a solved cube, so a case that needs a U turn after its alg
  isn't done until you turn it; OLL needs no final U turn.
- **Afterwards** you see your algs with the AUFs this case needed, e.g.
  `(U') R U R' U' ... (U2)`.
- Learned cases come up from a random side (when the set allows U turns
  around its cases) and with the pieces the step leaves free scrambled, so
  an OLL isn't known by its permutation. A new case keeps the picture it is
  learned from.

**Scheduling** is Anki-style. Cases in their learning steps (minutes
apart) come first, then the reviews due today in a random order, then new
cases from your learn queue. New cases stop at a daily limit (5) and wait
while 3 cases are in their learning steps. Intervals of three days or more
are spread by a few percent. Settings > Learn has the scheduler's options:
the learning and relearning steps, the graduating and easy intervals, how
much of its interval a forgotten case keeps (20 %), the ease a Good review
regains, credit for late reviews, whether Hard moves a learning case on a
step, the two limits above, the learn-ahead time (off), and when a case is
a leech or slow to recognise. Only Learn attempts change the schedule;
Drill attempts never do.

**The learn queue** is the strip under the list: the next cases, how many
were introduced today, **Add new cases** (adds the next N ticked cases; or
type N in the count field and press Enter) and **Learn next** (introduces
one now, whatever the limits). The **⋯** menu takes the next case out of
the queue, ticks or unticks every case shown, and sets the **order of new
cases**: list order, the order queued, shuffled, one from each group in
turn, shortest alg first, or fewest steps left first (for sets solved a
step at a time).

**With nothing due**, the page says **Done for today** (or **Done for
now** when more falls due later today) with the day's numbers a line each
and when the next review is, and offers buttons for what else there is:
the next learning case early, new cases, drilling the set (its weakest
cases once it has been drilled), another set's reviews, or every set's
reviews. It checks again by itself when a case falls due. When no ticked
case has an alg yet, **Paste algs in Library…** opens Library's Paste
algs for the set (Drill has the same button).

**The bar above the cube** has the alg set (each with its due count, and
**All sets: due reviews** to review every set together), **Practise**, the
view and **Hide list**. The header counts the queued, learning, due and
done-today cases.

**Practise** chooses how reviews go:

- **Solve**: as above.
- **Recognise**: a learned case is only shown. Press Space when you recall
  its alg, or Shift+Space if you can't; the algs are shown and you grade
  yourself 1-4. New and learning cases are still solved.
- **Which case?**: name the case among up to six of its group (keys 1-6).
  Timed, not graded.

**The case list.** Right-click a case to learn it now, mark it as learned
(it becomes a review, without counting against the daily limit), take it
out of the queue, suspend it (out of Learn, still ticked for Drill), bury
it until tomorrow, reset its progress (drill times stay) or see its
history. Select several cases, or right-click a group, to do the same to
all of them. "Learn this case now" on a learned case that isn't due is a
practice run that doesn't change the schedule.

The Recog column is each case's mean recognition time: from the case being
set up to your first turn of a layer other than U or D, in Learn and in
Drill on the virtual cube; one slower than the "slow to recognise" setting
is marked ▲. Lapses and Ease (hidden at first) come from its
card. A case forgotten 6 times is flagged as a leech. Right-click the
header to choose columns.

#### Set properties that change Learn

These are set in [Library](#library).

- **Guided learning** (on): the first time a case comes up, its main alg
  is a walkthrough. → and ← step through it on the cube, then Space sets
  the case up for you to do with each next move highlighted. The
  walkthrough isn't graded; the case comes back at its first learning step
  to be done from memory. While a case is in its learning steps, the first
  Tab shows only the next move (Hard) and a second Tab the algs (Again).
- **Learn by family** (off): a case is learned together with its mirror
  images (left-right, front-back and both), such as the four slots'
  versions of an F2L pair. The family has one card. Each time it comes up,
  one of its ticked cases is set up at random, and afterwards all of them
  are shown with their main algs. Families are worked out from the cases,
  not their names.
- **A step at a time** (off): for sets solved by reducing one case to an
  easier one, such as a last-slot set done with triggers. Each case holds
  only its next step (`U (R U' R')`), which stops at another case of the
  set.
  - A card asks for its case's next step. Any step that leaves fewer steps
    to go is Good, whether or not it is the set's; anything else is Again.
  - After the step you carry on from the case you landed in, to the end.
    Each case you pass is checked against its own card: a due one is
    graded, one that isn't due changes only if you miss its step, and one
    you haven't started is shown its step and not graded.
  - Afterwards every case you went through is listed with the set's step,
    what you did instead, and what became of its card. Esc during the
    solve goes back to where the current case came up.
- **Mirrored cases**: a set written for one F2L slot is also set up for
  the others. Each case comes up as written or in one of the chosen mirror
  images, seen from the front corner on that slot's side, with the algs
  mirrored to match (`R U R'` is `L' U' L` left-right, `R' U' R`
  front-back and `L U L'` both). A case that doesn't depend on the slot,
  such as an OCLL, is never mirrored. A case has one card however it comes
  up.

**Practise in context** (sets of F2L pair cases): a cube with EO Cross
done and 1 to 4 slots open, set up from cases you have started learning
where possible. Solve the pairs in any order. Afterwards each pair is
shown with the set's case and alg for it, and the set's cheapest order for
the whole state (Tab shows it as a hint). It isn't graded. Space gives
another, Esc the same one again, and Backspace goes back to learning.

### Drill

Drill times you on the cases, on the virtual cube (the default) or a
physical one, chosen per set under **Cube**. The two keep separate times
and statistics.

- **Virtual cube**: the case is set up on the cube, which only turns as a
  whole until the timer starts. Tap Space to arm it and the first face
  turn starts it (Settings > Timing and solves can have Space held and
  released instead). The timer stops by itself when the step is done;
  Space stops it earlier, which is a DNF. A DNF says whether you were an
  AUF off or how many pieces were wrong, and shows your moves.
- **Physical cube**: a random-state scramble sets the case up, so the
  scramble doesn't give the case away. Hold Space and release to start;
  any key but Shift, Ctrl or Alt stops.
- Either way, the pieces the step leaves free are scrambled, with a random
  U turn before and after if the set allows it.

**Cases** in the bar is one choice in two parts: where the cases come from
(the ticked ones, those the list shows, or a group) and which of them
(learned in Learn, the default; all; being learned; in the learn queue; or
the weakest 5 you have drilled).

**Order** is random (never the same case twice running), weighted (slow
and failed cases more often, by the median of each case's recent times),
list order, or a recap: every case once, then a summary. A failed case
comes back three cases later.

**After a case** you see your time (green at or under the set's target
time), the diagram and the algs with the AUFs this case needed. Keys are
ignored for a moment so a second press doesn't skip it.

| Key | Does |
| --- | --- |
| Space | next case |
| Backspace | toggle DNF (virtual) or mark a wrong alg (physical) |
| Delete, Ctrl+Z | delete the attempt |
| Esc | try the case again |
| Tab | practise the case |

- **Giving up**: Shift+Space before or during the solve records a DNF.
  Settings > Drill can make Backspace the give-up key instead.
- **Practice** (Tab after a case): the case set up exactly as it was, with
  no timer and nothing recorded. First with the algs shown, then with them
  hidden, then the next case once you solve it without help. Settings >
  Drill can have Space practise after a DNF, and practice wait for a key
  at each stage.
- A set with mirrored cases sets each case up in a random mirror image. A
  set solved a step at a time is drilled from the case to the end of the
  step.

**Statistics.** The list shows, per case, the recent average, the best
time (kept for good), the fail rate and the weakness (how often Weighted
picks it); a group's row sums its cases. An average over the set's target
time is marked ▲. Click a column header to sort.
Right-click a case for its drill history (a chart of its last 100
attempts, and every attempt by day) or to clear its times. The tally under
the list counts attempts, mean, DNFs and new bests since **Reset tally**,
and names the slowest cases. On the virtual cube each solved attempt also
records its recognition and execution times.

### Case lists

The lists in Learn, Drill and Library can be searched by case or group
name (in Learn and Drill, by alg too) and filtered by status: ticked,
new, queued, learning, learned or due; never drilled or slower than the
target; slow to recognise, leeches, suspended or buried; without algs or
with failing algs. "Tick shown" and "Untick shown" change only the cases
shown. Double-click a case in Learn or Drill to practise it now. The list
selects a case only once it is revealed, so it doesn't name the case you
are recognising.

### Library

Library creates and edits alg sets. It shows the set list, the selected
set's case tree, and the selected case with its diagram, group and algs.

- **Toolbar**: New set…, Set properties…, New case from alg…, Paste
  algs…, New group… and Check algs….
- **Menus**: **Set** (also a right-click in the set list) creates,
  duplicates and deletes sets, opens the properties, checks the algs and
  restores built-in sets and cases. **Cases** (also a right-click in the
  tree) makes cases and groups, pastes algs, and renames, moves and
  deletes the selection. Importing and exporting sets as
  [JSON](file-formats.md#alg-sets) is in **File**.
- **A case** has buttons to show it in **Learn** or **Drill** or open it
  in **Alg Lab**, and two tabs: **Algs** (with the selected alg's
  fingertricks) and **History** (its Learn card and drill statistics).
- **Undo**: every change is saved at once and can be undone (Ctrl+Z) and
  redone while v-cube is open, including deleted cases, groups and sets
  with their progress. Deleting a set asks first; deleting cases or groups
  doesn't.
- **Moving**: Ctrl- or Shift-click selects several cases. Drag them, or
  use **Move to group** or Alt+Up / Alt+Down.

**Algs.** A case can have several. Type one under the list to add it; drag
or Alt+Up / Alt+Down to reorder; double-click or F2 to edit; Delete to
remove. Every alg is checked against its case and shows the result:
`(U') … (U2), 14 moves` for a pass, or `leaves 3 corners unsolved (U
layer)` or `can't read at character 7` for a fail. Moves are counted in
HTM. Failing algs are kept with a warning, and **Check algs…**
(Ctrl+Shift+K) lists them all.

The **main alg** is the first passing one (right-click > **Make main
alg**). Each case is drawn, and set up in Learn and Drill, from the angle
its main alg starts from: a leading `y`, `y'`, `y2` or U turn is done to
the case.

**Set properties…** (double-click a set, or Alt+Enter) has, with each
option explained:

- the name, description, target time, and random U turns around cases;
- guided learning, learning by family, a step at a time and mirrored
  cases (see [above](#set-properties-that-change-learn));
- **what the algs may leave unsolved**, e.g. "last-layer permutation" for
  an OLL-like set, for the whole set or as a group's own override;
- **how diagrams look**: full colour, top colour only or grey; from above
  or in 3D from any top corner. "Follow the pieces" applies the last-layer
  settings to the last-layer pieces wherever they are, and to F2L pieces
  the step leaves free (F2L sets use it).

A diagram previews the settings, and a line says how many algs would stop
or start passing.

**Built-in sets**: **Set > Restore a built-in set** brings back a deleted
one. **Restore built-in cases…** brings back a set's shipped cases and
keeps your algs, added cases and settings.

#### Getting algs into a set

**Paste algs…** (Ctrl+Shift+V) takes any list of algs:

- Lines may be `Name: alg1 / alg2` (as in Alg-Trainer), tab-separated
  `name, alg, alg…` columns from a spreadsheet, or bare algs, and may end
  with a `// comment`.
- An alg goes to the case its line names, if it solves it; without a name,
  to the case it solves, under any AUF the set allows and any rotation.
- **Check** shows a table of where each alg would go before anything
  changes. An alg that solves a different case than named, several cases
  or none isn't filed, but you can correct it or give it a case there.
  **Apply** adds the rest; algs left out come back the next time you
  paste.
- With "Make new cases" ticked, algs that match no case make new ones in
  the group you pick.
- Text copied from web pages works: curly primes and no-break spaces are
  cleaned up, and repeats may be written `(R U R' U')2`, `x2`, `*2` or
  `×2`.

For your own set, **Set > New set…** (Ctrl+N) asks what the algs solve;
then **Cases > New case from alg…** (Ctrl+Shift+A) or Paste algs… builds
the cases.

### Alg Lab

Alg Lab finds, compares and times algs for a case. Each alg keeps its own
times.

**The case.** The drop-down at the top left picks an alg set or **Lab
cases**, Alg Lab's own cases for states that aren't in a set. **New…**
makes a lab case from a setup alg and what its algs must solve: the whole
cube, the last layer oriented, the F2L, the step of one of your sets, or
your own free pieces (`LL:p`, as in set properties), and whether a U turn
may come before or after. **Edit…** changes it and **Delete…** removes it
with its candidates and times.

#### Search tab

The case's candidate algs: the set's own (the main alg in bold), then the
ones you typed, pasted or generated. An alg that doesn't solve the case is
greyed out with the reason. The columns are:

- where it is **From** (set, generated, pasted, typed);
- move counts in **HTM** (a slice counts 2), **STM** (any layer turn is
  1), **QTM** (half turns count 2) and **ETM** (rotations count too);
- the ergonomic **Score** (lower is easier);
- its times on the chosen cube: count, mean, median, best, ao5, ao12,
  standard deviation and DNF rate, over all time, the current test session
  or the last 50 attempts.

The time columns appear once an alg listed has times. **Times shown**
picks the cube and the attempts the time columns cover, and the profile
of the Score. **Algs shown** filters to the algs of a move set, **No
rotations** hides algs with x, y or z, and the length box hides longer
algs. Type an alg under the list to add it, or **Paste algs…**. **Add to
session** puts the selected algs in the current test session, **Add to
set** and **Make main alg** edit the set (Ctrl+Z takes them back),
**Remove** forgets kept candidates and **Clear times…** forgets the
selected algs' times. The list takes the keyboard too: the arrow keys
select algs (Shift for several), Enter adds them to the session and
Delete removes them.

**The generator** (fold it away with the **Generator** button) finds every
alg of a move set that solves the case, shortest first.

- **Move set**: a preset from `<U, R>` to all faces, one of your own, or
  **Custom…**: a grid of every face, wide turn and slice, also typed as
  text (`R, U, F2`; a 2 means half turns only, a 1 quarter turns only).
  Name it to keep it as a preset.
- **Shortest +** stops that many moves after the shortest alg found;
  **Every length** goes on to the limit. **Up to** is the longest alg
  searched for and **at most** caps the number of algs. Lengths are in
  STM.
- AUF is allowed as the case allows it, and an alg never starts or ends
  with a U turn that would only be that AUF. Algs never turn a layer twice
  running.
- **Find algs** searches the case; **Whole group…** searches every case of
  its group or set in turn. **Stop** ends a search.
- The first search with a move set and step builds tables (about 3 seconds
  for all six faces, much less for smaller sets). Restricted move sets
  search deep quickly; all six faces past 13 or 14 moves can take minutes.

**Score profiles.** The score comes from the profile chosen beside it.

- **Two-handed grips** (the default) is a grip profile: the score is the
  cheapest way to turn the alg with two hands, summing a fingertrick for
  each move and the regrips between them.
- **Right-handed 2H** and **One-handed (right)** are move profiles: each
  move has a cost, with extra costs for changing axis and for awkward
  pairs of moves in a row.

**Profile…** edits the chosen profile, copies it, makes a new grip
profile, deletes it or restores the built-in ones.

**Fingertricks.** Under the candidates, the selected alg is shown as it is
best turned, by the chosen grip profile. A hand's grip is where its thumb
sits as the wrist turns: **home**, **up** a quarter turn (the right thumb
after `R`), **down**, or **far up** / **far down**, which are a strain.
The top line gives the starting grips, the cost and the number of regrips.
Below is a column per move with the trick each hand uses, and **↻ regrip**
where a hand changes grip first. A regrip while the other hand turns costs
less than one that pauses the alg.

Click a move to list every trick that can turn it, with what the whole alg
would cost that way. Click one to use it and the rest of the alg is
planned round it; **Reset picks** returns to the best plan. **Copy**
copies the plan as text. Library shows the same under a case's algs.

**Edit this profile…** on a grip profile lists every trick: the move, the
hand and finger, how it turns, the grips it starts from, how it moves the
thumb, and its cost. Turn off tricks you don't use, change costs or add
tricks; **Standard tricks** restores the defaults. The costs of a regrip,
a regrip under cover of the other hand, strain, starting away from home
and one finger turning two moves running are there too. Moves no trick
turns (`E`, `S`, wide `u`) cost a flat amount.

The standard tricks:

| Layer | Tricks |
| --- | --- |
| U | index flicks (`U` right, `U'` left), double flicks from home (`U2`), index pushes the other way |
| R, L, `r`, `l` | the wrists |
| D | ring-finger pulls and pushes from home, right ring double pull (`D2`) |
| F | right index from the top (thumb down), right ring from below (thumb up), the thumbs from the front, left index from the top (thumb down), a slower right index reach |
| B | right index from below (thumb up), right ring from the top (thumb down), left index from below (thumb up) |
| `M` | right ring finger |
| rotations | both hands let go and take the cube again at home |

#### Test tab

A test session times a list of algs, each with its case. Its options:

- **Cube**: physical (a random-state scramble sets the case up; hold Space
  and release to start, any key stops) or virtual (your first turn starts
  the timer and it stops when the case is solved; Space stops it early, a
  DNF).
- **Order**: interleaved (rounds of every alg once, in random order) or
  blocked (every rep of one alg, then the next), and the **reps** of each.
- **Warm-up**: reps of each alg left out of the statistics. **Trim**: the
  share of fastest and slowest times left out of the mean, median and SD.
- **Random AUF**: off, the case is set up from the side the alg starts
  from, so only the alg is timed; on, a random U turn comes first.

Space starts. On the virtual cube your moves are checked against the alg
as Learn checks them; a time where they left it is marked and not counted
(the Times tab can count it). After an attempt, Space goes on, Backspace
toggles DNF, Delete deletes the time and Esc does the same alg again.
Double-click an alg in the session's list to time it next.

**Compare** shows the session's algs side by side: the statistics and a
chart, either each alg's spread (dots, with a box for the middle half and
a line at the median) or the times in order, each alg numbered as in the
legend. **Times** lists the attempts;
right-click one to toggle DNF, count a time that left the alg, or delete
it. An alg's times are kept across sessions, separately for each cube, and
deleting a session keeps them.

## Solves

Timed, Untimed and Move Count each keep their own sessions. A session is
for the virtual cube (the default) or a physical one; the choice is fixed
once it has solves.

**On the virtual cube** the scramble is put on the cube, which only turns
as a whole until the solve starts. Each solve keeps its scramble, its
solution and its move count in HTM. Counts are simplified by default:
turns that cancel across a turn of the opposite layer or a rotation don't
count (`U D U'` is one move). Settings > Timing and solves can count every
turn as done instead.

**The solve list**: right-click a solve to copy its scramble or solution
or open **Details…**, which has its date and comment too. Delete removes
the last solve and Ctrl+Z puts it back. Click a statistic to see its
solves. Averages are rounded as the WCA does. The chart names each line
(ao5, ao12, or the splits) at its right end; its right-click menu picks how
many solves it shows (12 to 500); pointing at the chart shows that solve's
time and averages on the line under it.

### Timed

Hold Space to start; the hold time is in Settings > Timing and solves.

- **Physical cube**: a net beside the scramble shows the scrambled cube.
  Any key stops the timer.
- **Virtual cube**: the timer stops when the cube is solved (a setting can
  leave that to Space). Space stops it earlier, as a DNF, and the cube
  stays as you left it until Space brings the next scramble. Esc cancels a
  solve. Each move's time is kept, which gives the **Moves**, **TPS**,
  **Cross**, **F2L** and **LL** columns (EO Cross for a ZZ solve) and the
  chart's split lines.
- **Inspection** (optional, Settings > Timing and solves): WCA inspection
  with +2 and DNF applied automatically and optional beeps at 8 s and
  12 s. Tap Space to start inspecting, then hold Space to start. The
  virtual cube can stay hidden until inspection starts.
- **Sessions** show ao5, ao12, ao100, mo3, best and mean, with a trend
  chart. Alt+1, Alt+2 and Alt+3 set no penalty, +2 and DNF on the last
  solve; Alt+C comments on it.
- **cstimer**: the session menu imports and exports cstimer sessions.
  Imports come in as physical sessions, with any solutions and move times;
  other puzzles and empty sessions are skipped. To load an export in
  cstimer, use **Import session(s) from other timers**. cstimer's "Import
  from file" would replace all your data.

### Untimed

Solves with no timer.

- **Virtual cube**: hold Space until the hint turns green and release to
  start. The solve ends when the cube is solved, or on Space as a DNF.
  Afterwards you see the move count, with your EO Cross count and the
  optimal, and the cube turns freely; the alg field applies a typed alg.
- **Physical cube**: large scramble text with a net. Space or → keeps the
  scramble as done and shows the next; Backspace or ← goes back through
  the last 100.

### Move Count

Move efficiency for one step, with no timer. Each session has a step:

| Step | Done when |
| --- | --- |
| EO, EO+1, EO+2 | edges oriented, with any one or two cross edges |
| EOLine | EO with the two cross edges on the EO axis |
| EO Cross | EO and the cross |
| XEOCross | EO Cross and any one F2L pair |
| ZZ F2L | the first two layers with every edge oriented |
| Cross | the cross, on any face |
| CFOP F2L | the first two layers |

The step can be done on any colour and axis.

- **Virtual cube**: hold Space and release to start; Space ends the solve.
  The count is recorded if the step is done, DNF if not.
- **Physical cube**: solve the step, type your move count and press Enter.
  Alt+3 records a scramble that went wrong as DNF. Both can be clicked on
  their keys under the cube.
- **Opt** is each scramble's optimal count for the step (EO to EO Cross,
  and the cross) in any hold; a setting limits it to your colour
  neutrality's holds. The statistics add your mean moves over optimal and
  how often you were optimal.
- Choosing a step picks its scramble type: WCA, EO Cross solved for ZZ
  F2L, or the cross solved for CFOP F2L.
- **Skip scramble** (Ctrl+→) moves on without recording. Right-click a
  count to edit it, toggle DNF, comment or copy.
- **Retry the worst 5** (session menu; also Untimed, virtual cube) retries
  the DNFs and the solves furthest over optimal, unrecorded.

### Review

**Review** (double-click a solve, or Alt+R for the last) works for all
three solve modes. It replays the solve on its own cube (←/→, Home/End or
the slider, or Space or the play button to play it a move at a time),
marks where EO, EO+1, EO+2, EO Cross and ZZ F2L were first
done, and says how many moves each took and in which hold. For EO to EO
Cross and the cross it lists the optimal solutions, in your holds and over
every colour and axis.

For a ZZ F2L or whole solve it splits the F2L into pairs and compares each
with your set of F2L pair cases: which case the pair was, your moves
against your alg and against the optimal <R, U, L> pair, the cheapest pair
at that point, and the set's cheapest order overall. A whole solve also
gets its splits (cross, each pair, last layer), with time and TPS if it
was timed. **Copy reconstruction** copies the solve a step per line, and
**alg.cubing.net** opens it there.

**Retry this scramble** (virtual cube) puts the scramble back to solve
again, unrecorded, and shows the count against the original and the
optimal.

### Scramble types

Timed and Untimed offer:

- WCA random state;
- cstimer-style subsets: F2L (cross solved), XCross, EOLine, EOCross,
  LSLL, LL, ZBLL, CLL, ELL, PLL, EPLL, corners only, edges only;
- any of your alg sets, from its ticked cases. Tab shows or hides the algs
  for the case each scramble sets up. Nothing is graded.

## Practice

Three modes that take a ZZ solve a step at a time: **EO Cross**, **F2L**
and **TSLE**. They work the same way. Each has three tabs:

- **Learn** (F2L and TSLE): one case at a time, as cards, untimed on the
  virtual cube.
- **Inspection** (EO Cross): plan the step within an inspection time,
  then do it blind.
- **Untimed**: the whole step on the virtual cube at your own pace, for
  efficiency: a line on each pair or trigger as you go, then how the solve
  compares with the best.
- **Timed**: the whole step against the clock, on the virtual cube or a
  physical one, for speed.

**States are what a solve gives you.** EO Cross starts from a random-state
scramble, F2L from a random state with the cross (and EO) done, TSLE from
a random last-slot state. Nothing is picked by how your last tries went.

### Learn

Each case is a **card**, scheduled with spaced repetition exactly as an
alg set's cases are in the [Learn mode](#learn), with the same settings.
**Cards come in by themselves**, a few a day (*New cases per day* and
*New cases learning at once* in Settings > Learn), easiest first. Each
time:

1. A **new case** is shown before you try it (in F2L you first choose its
   solutions, see [F2L](#f2l)). `Enter` plays the solution on the cube,
   which is then put back; do it yourself and the case is in. It comes
   back in a minute to do from memory. `Space` skips the showing for a
   case you know.
2. A **due case** is set up without its name. Solve it. `Tab` gives a
   hint, shown large under the cube; `Backspace` gives up, `Esc` starts
   again.
3. The card is graded **Good** when you solved it unaided and well, and
   **Again** after a hint, giving up or a poor solution. `1`–`4` change
   the grade to Again, Hard, Good or Easy.

When nothing is due the page says when the next card is. `Space` takes a
card in its learning steps early, and the Untimed tab is always there to
practise.

**After a solve** there is one line, the verdict, and the best solution
under it. A result over the best is marked ▲, and is bold as well when it
is far over. `Tab` opens the details (they take room from the cube only
while open); click a solution there to play it. `Enter` plays the best
solution from the start, `Space` goes on, and `Esc` gives the same state
again, unrecorded and ungraded.

**The list** on the left shows the cards with when they are due (new, due,
or in how long), their reviews and lapses. Double-click a case to practise
it at once, ungraded. Above the list are the counts due, new left today
and learned. F2L and TSLE cards also appear in [Today](#the-window).

### Inspection

EO Cross only. The first state of a sitting waits for `Space` (nothing
timed starts by itself). Then the inspection clock counts down while you
look at the cube, turning it only as a whole, and plan. Your **first face
turn** stops the clock and hides the cube: do your plan from memory, then
`Space` shows the cube again and the plan is judged, with the time you
looked. A plan that didn't do the step counts as not done. **Inspection**
in the bar sets the limit (none, 8, 10, 15 as in the WCA, 20 or 30
seconds); going over it is marked, but the plan is judged all the same,
and the sitting's summary counts the tries that went over.
After the verdict, `Space` brings the next state at once. `Esc` while
planning gives the same state again, unrecorded.

The list shows, per step, your tries, how many did the step, the time you
looked and how far over the optimum the plans were lately.

### Untimed

The whole step on the virtual cube, at your own pace. As each unit goes
in, a line under the cube says how it went: in F2L whether it was the
best pair to do then and how its solution compares with the best for it;
in TSLE whether the trigger was the best from that AUF, good, or not. At
the end the verdict compares the whole solve with the best way, and `Tab`
shows it unit by unit. A solve under way is still there when you come
back to the mode.

The list shows your solves, their moves lately and how far over the best
they were.

### Timed

Timed works exactly as [Drill](#drill) does, with the same settings
(Settings > Drill):

- **Virtual cube**: the state is shown and can only be turned as a whole.
  A tap of `Space` arms the timer, the first face turn starts it, and it
  stops by itself when the step is done. `Space` stops it early: a DNF
  unless the step is done.
- **Physical cube** (**Cube** in the bar): a scramble is shown. Hold
  `Space` and let go to start; any key stops. Nothing can be checked, so
  mark a DNF yourself.

The result is about speed: the time, then your moves, turns per second
(TPS) and how the time compares with your recent mean. `Tab` shows **where
the time went** (each pair, trigger or part of the EO Cross with its time,
the pause before its first turn and its TPS) and how efficient the solve
was against the best.

`Shift+Space` gives up. Afterwards `Backspace` toggles the DNF, `Delete`
deletes the attempt, `Enter` plays the best solution, `Space` goes on and
`Esc` gives the same state again, unrecorded.

**The list** shows your solves, mean (of the last 12), best and TPS per
group, kept apart for the two cubes: per step in EO Cross, per method in
F2L, per starting case in TSLE.

### EO Cross

**Step** picks EO, EOLine, EO+1, EO+2 or EO Cross. A step may be done in
any hold your [colour neutrality](#colour-neutrality) allows, on either EO
axis. The cube is shown in your main hold; turn it as a whole to solve
from another. Your moves (HTM) are compared with the **optimum**: the
shortest solution over all those holds for the state on the cube.

EO Cross has no finite list of cases, so it has no cards: its tabs are
[Inspection](#inspection), [Untimed](#untimed-1) and [Timed](#timed-1).

**The details** list every hold with its optimum and a few optimal
solutions that aren't reorderings of each other. For an EO Cross they
also show **where the moves went**: the solve is cut where EO, EO+1, EO+2
and EO Cross were first done, and each part shows its moves and what it
**lost** (its moves less how much nearer EO Cross it brought the cube).

### F2L

**The bar** sets the F2L of your method:

- **F2L**: *ZZ F2L - 1 (ZZ-CT)*, the default, leaves one slot for the
  last-slot step; *ZZ F2L* solves all four; *CFOP F2L* and *CFOP F2L - 1*
  are the same without EO. In the ZZ methods no solution shown ever breaks
  EO.
- **Last slot** (F2L - 1): the slots that may be the one left; FR and FL
  by default.

Solutions are searched for with every move the method allows: R, U, L, D,
F2 and B2 in ZZ, and also F, B and wide turns without EO (a wide turn
costs 1.0 on top of your profile's cost); never slices or rotations.
Those within two moves of the shortest are ranked by **cost**: the
fingertrick cost under the score profile chosen in Alg Lab.

The cube is in the **flat view** by default, which hides what a real cube
hides: the back slots and the back of the top layer.

**Learn** has a card for each pair case for the **front** slots and
another for the **back** ones: 166 for a ZZ F2L (334 without EO), by where
the corner and edge are, seen for the front-right slot. A card is set up
in one of its side's two slots with the pair ringed and the other slots
solved (but for those its pieces sit in, and perhaps the slot ZZ-CT
leaves open).

- **Choosing solutions**: when a case comes in you choose the solutions
  you'll use for it, from its best 8 for the side's own slot (FR or BR;
  in FL and BL they are done mirrored, and shown so). The alg of your set
  of pair cases (read once from the set, or its newest deleted copy) is
  listed too, ticked to start with. `1`–`9` tick and untick, a click on a
  solution plays it on the cube, and the field under the list takes one of
  your own, as done for the slot shown. `Enter` confirms. After a try of
  the card, `0` changes them (`Esc` leaves them as they were). Solutions
  a method doesn't allow (an F quarter turn chosen with CFOP, under ZZ)
  don't count with it: a case with none left is chosen for again, and the
  others are kept for the method they were chosen with.
- **Good** is one of your solutions, from any AUF and however the cube was
  turned, or anything that costs no more than the cheapest of them; `+`
  then adds what you did to your solutions. `Tab` while solving shows a
  move more of your first solution each time.

**Untimed and Timed** do the whole F2L. The verdict gives your moves and
cost, and how far that was over the cheapest way found from the start
(the order of pairs and their solutions together). The details list each
pair with your moves, the best solution from where you were, and the pair
that would have been best to do then.

### TSLE

ZZ-CT's last-slot step. With EO done and three slots solved, TSLE inserts
the last slot's edge and orients the five remaining corners. It is done
with the slot's three triggers (`R U R'`, `R U' R'` and `R U2 R'` for
front-right, each after a U turn if needed) until the step is done or an
OCLL is left, which an OCLL alg finishes.

There are 104 cases, the seven OCLLs among them. The mode doesn't hold one
trigger per case; from every state it knows **every good trigger**:

- A trigger is **good** when it is on a way to the end with the fewest
  steps (triggers plus the closing OCLL). No case needs more than three.
- The **best** is the good trigger with the fewest moves to the end from
  the current AUF.
- Where a way with one more step has fewer moves in all, the details say
  so, but it doesn't count as good.

**Last slot** picks the slots practised (FR and FL by default). Each
state uses one at random, with that slot's triggers (`L' U L` for
front-left); a case is tracked once whatever the slot. OCLL algs are shown
as written, never mirrored, and **OCLL algs…** changes them.

- **Learn**: a case at a random AUF; do the next step. There are 97
  cards, the cases with fewest steps left first (the OCLLs are known algs
  and have none). Any good trigger is **Good**; the verdict says whether
  it was the best from that AUF. `Tab` shows the best step.
- **Untimed and Timed**: the whole step, through the closing OCLL. An
  OCLL alg joined on to the trigger before it (`R' R` cancelling) counts
  joined, as the best way does. The details compare the solve trigger by
  trigger.

A name such as `UF CW 0120` gives the edge (UF, or FR for in the slot),
the slot corner's place (N oriented, CW, CCW) and the twists at URF, UFL,
ULB and UBR.

## Colour neutrality

Edit > Colour neutrality… (`Ctrl+Shift+N`) applies to every mode.
Scrambles are applied holding white on top and green in front; you choose
the holds you solve in, and each scramble is set up for one of them at
random.

- Presets start from a main hold: fixed, y2, y (any front), x2 (top or its
  opposite), x2 y2, x2 y and full (all 24 holds). Or tick any holds in the
  grid.
- For example, an EOCross scramble with x2 y from white top gives a white
  or a yellow cross with any front colour.
- The virtual cube shows each case in its hold's colours, and physical
  Drill's scrambles set the case up for its hold. Diagrams are drawn in
  the main hold.

## The virtual cube

- Each mode has a view: flat (the U and F faces with the adjacent L and R
  stickers, as in Alg-Trainer; the default), animated 3D, a net, or
  hidden. Learn remembers the view per set, and its 3D view looks from the
  corner the set's diagrams are drawn from.
- It is turned with cstimer's face keys (Shift for wide moves), which you
  can remap in Settings > Cube and keys. See
  [keys.md](keys.md#turning-keys).

## Settings

Edit > Settings… (`Ctrl+,`). Each option is explained under it.

| Tab | What is there |
| --- | --- |
| **Appearance** | theme, the interface text size (all the text, up to about 16 px), sticker colours, the scramble, timer and hint text sizes, the size of case pictures, decimal places |
| **Colour neutrality** | see [above](#colour-neutrality) |
| **Timing and solves** | hold times for the solve modes and Drill (up to 2 s), how Drill's virtual timer starts and stops, WCA inspection and its beeps, auto-stop when solved, hiding the cube until inspection, simplified move counts, Move Count's Opt holds |
| **Cube and keys** | turn speed and the turning keys |
| **Learn** | the scheduler (see [Learn](#learn)) |
| **Drill** | the give-up key, what Space does after a DNF, whether practice goes on by itself, when a failed case comes back, how many cases "Weakest" drills |

Where settings and data are kept is in [file-formats.md](file-formats.md).
