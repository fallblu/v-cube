# v-cube

A lightweight Qt desktop app for creating, learning and drilling 3x3 alg
sets, with random-state scrambles and a cstimer-style solve timer. Learn
algs on a virtual cube turned from the keyboard, then drill and time them
on the virtual cube or a normal (non-smart) cube with the space-bar timer. Solves can be timed,
untimed or move-counted, on the virtual cube or a physical one, and
reviewed against the optimal solutions and your own F2L algs. Practice
modes train ZZ inspection planning and Old Pochmann blindfolded solving.

## Features

The app has eight modes, listed in a sidebar in three groups (`Ctrl+1` to
`Ctrl+8` switch between them):

| Group | Mode | What it's for |
| --- | --- | --- |
| Algs | **Learn** | Untimed practice on the virtual cube, scheduled by spaced repetition |
| Algs | **Drill** | Timed practice on the virtual cube or a physical one with the space-bar timer |
| Algs | **Library** | Creating and editing alg sets |
| Solves | **Timed** | Timed solves, with sessions and statistics |
| Solves | **Untimed** | Solves with no timer, with their move counts |
| Solves | **Move Count** | Move-efficiency practice for one step (EO to ZZ F2L, the cross, CFOP F2L) |
| Practice | **Inspection** | Planning EO to EO Cross (or EOLine) in inspection and executing it blind, and counting bad edges |
| Practice | **Blind** | Old Pochmann blindfolded solving: letters, memo tracing, setup moves and blind solves |

Each Solves mode keeps its own sessions, and each session is for the
virtual cube (the default) or a physical one: pick it with the cube
drop-down next to the scramble type. Sessions saved before there was a
choice are for a physical cube.

- **Learn**: learn the cases of an alg set on the virtual cube.
  - Each case is set up on the cube and you solve it from memory, with no
    timer. The algs stay hidden until you press Tab. When the set has
    random U turns before and after its cases (as Drill uses), the case
    comes from a random side, so you learn to recognise it from any angle.
  - Solving the case with one of its algs and without a hint grades it
    Good; a hint, giving up (Shift+Space, so that a double tap of Space
    after a case can't give up the next one) or leaving the algs grades it
    Again. Keys 1-4 change the grade afterwards.
  - U turns and rotations are free until your first turn of a layer other
    than U or D, so you can look at the case from any side and line it up
    (an alg's D turns may come before or after that AUF). From then on
    your moves must follow one of the case's algs. The same turns done
    another way still count: rotations, a half turn as two quarter turns
    either way, a move written with a 3 as those three quarter turns or
    one turn the other way (R3 as R R R or R'), a wide or slice turn as
    the turns it is made of (r as
    R M'), turns of opposite faces in either order, and a U turn at the
    end that the alg leaves out or adds. Once only U turns are left of the
    alg, any U turns count (its last U turn and the AUF done separately,
    or a U turn too far put right). A wrong turn, even one you undo, or starting over
    with Esc once the alg is begun leaves the algs. The status line says
    so straight away, and your moves are shown under the algs after the
    case, with the move where they left the algs marked (a turn the wrong
    way is marked even though the next turn is what gave it away).
  - The case is finished when the cube reaches the step's end state. For
    a step that leaves nothing free, such as PLL, that is a solved cube, so
    a case that needs a U turn after its alg isn't done until you turn it.
    A step that leaves the last-layer permutation free (OLL) needs no U
    turn at the end.
  - Once the case is solved the cube turns back upright, e.g. after an
    alg that solves the case before its last rotation.
  - Cases are scheduled Anki-style: cases in their learning steps (minutes
    apart) first, then reviews due today (a review is due all day on the
    day it falls due) in a random order, so that cases learned together
    don't always come up in the same sequence, then new cases from your
    learn queue, up to a daily limit and only while fewer than 3 cases are
    in their learning steps (both in Settings), so that new cases don't
    arrive back to back. Review intervals of three days or more are spread
    out by a few percent.
  - Settings > Learning holds the scheduler's choices: the learning and
    relearning steps, the graduating and easy intervals, how much of its
    interval a forgotten case keeps (20 %; earlier versions kept 50 %), the
    ease a Good review regains (so a case once missed can space out again),
    credit for reviews done late, whether Hard moves a learning case on a
    step, how many new cases learn at once, the learn-ahead time (off;
    earlier versions took learning cases up to 20 minutes early), when a
    case is a leech and when it is slow to recognise.
  - "Start learning" adds the next N ticked cases to the queue (type N and
    press Enter to do the same). **New cases** (for each set) chooses the
    order cases are queued and introduced in: list order, the order you
    queued them, shuffled, one from each group in turn, shortest alg first,
    or fewest steps left first (for sets solved a step at a time). Under
    the list the queue shows the next cases and how many were introduced
    today; "Learn next" introduces the next one now, whatever the limits,
    and "Remove" takes it out of the queue.
  - With nothing due the page says why, when the next review is, and how
    the day went (reviewed, Again rate, due tomorrow). It looks again by
    itself when a case falls due; Space looks again, and takes the next
    learning case early when that is all there is.
  - The header above the cube (also in focus mode) counts the queued,
    learning, due and done-today cases, and the set drop-down shows each
    set's due reviews. **All sets: due reviews** at the top of the
    drop-down reviews every set's due cases in one session, interleaved;
    the list shows the set of the case at hand.
  - Right-click a case to start learning it, mark it as learned (it then
    comes up as a review, without the walkthrough, and doesn't count
    against the daily limit), remove it from the learn queue, suspend it
    (left out of Learn but still ticked for Drill), bury it until tomorrow,
    reset its learning progress (its card and Learn attempts; drill times
    stay) or see its history (its card, and every attempt with its
    recognition and execution times). Select several cases, or right-click
    a group's row, to do the same to all of them at once.
  - The list's Recog column is each case's mean recognition time (from the
    case being set up to your first turn of a layer other than U or D, in
    Learn and on the virtual cube in Drill, for cases solved with their
    algs and recognition reviews); Lapses and Ease (hidden at first) come
    from its card. A case forgotten 6 times is flagged as a leech. The list
    can be filtered to cases slow to recognise (over 1.5 s, in Settings),
    leeches, or suspended and buried ones, and right-clicking the header
    hides or shows columns. F9 hides the list, and the splitters between the list, the
    cube and the algs keep their place.
  - The view drop-down's neighbour picks how reviews go: **Solve** (as
    above), **Recognise** (a learned case is only shown: press Space as
    soon as you recall its alg, or Shift+Space if you can't; the algs are
    shown and you grade yourself 1-4; new and learning cases are still
    solved) or **Which case?** (name the case among up to six cases of its
    group, timed but not graded).
  - After a case the outcome is shown as a coloured badge, and each grade
    button shows the interval it would give. Ctrl+Z takes back the last
    grades, up to 10, and brings the case up again.
  - Learned cases come up with the pieces the step leaves free scrambled
    (as in Drill), so an OLL isn't known by its permutation; a new case
    keeps the picture it is learned from.
  - "Learn this case now" (right-click a case) on a learned case that isn't
    due yet is a practice run: you see how you did, but the schedule
    doesn't change, also for the cases a step-at-a-time set passes on the
    way.
  - When the case is solved you see your algs with the AUFs it needed, e.g.
    `(U') R U R' U' ... (U2)`. A U turn written at the start or end of an
    alg joins the AUF on that side, so `(U2) R U R'` that needs a U first
    shows as `(U') R U R'`.
  - **Guided learning** (a set property, on by default): the first time a
    case comes up, its main alg is a walkthrough. → and ← step through it
    on the cube (Home goes back), then Space sets the case up again for
    you to do it with each next move highlighted. The walkthrough isn't
    graded: the case comes back at its first learning step to be done from
    memory (1-4 grade it straight away instead). While the case is in its
    learning steps, Tab first shows only the next move (graded Hard, which
    moves it on a step but never finishes its steps), and a second Tab
    shows the algs (Again). Reviews work as before.
  - **Learn by family** (a set property, off by default): each case is
    learned together with its mirror images, left-right, front-back and
    both, such as the four F2L slots' versions of a pair. The family has
    one review card; each time it comes up one of its ticked cases is set
    up at random, and afterwards all of them are shown with their main
    algs. The case list shows every case of a family with the family's
    status, and Drill counts them as learned together. Families are worked
    out from the cases themselves, not from their names.
  - **A step at a time** (a set property, off by default) is for sets
    solved by reducing one case to an easier one, such as a last-slot set
    done with triggers: each case holds only its next step (`U (R U' R')`),
    which stops at another case of the set, and that case's step carries on
    until an alg finishes the step.
    - A case's card asks for its next step only. Any step that leaves fewer
      steps to go than before is Good, whether or not it is the set's; a
      step that doesn't, a hint or giving up is Again. A case with an alg
      that finishes the step (an OCLL at the end, say) needs an alg that
      does.
    - After the step the cube shows the case it stopped at and you carry
      on, case after case, to the end. Each case you pass is checked
      against its own card: a due one is graded, one that isn't due
      changes only if you miss its step, and one you haven't started
      learning has its step shown and isn't graded. A case is graded once
      per attempt.
    - Cases come up with the pieces the step leaves free anywhere, as they
      are after a step, and the status line says how many steps are left
      after each one. Esc goes back to where the current case came up.
    - Afterwards every case you went through is listed with the set's step,
      what you did instead where that differed, and what became of its
      card. Keys 1-4 change the grade of the case that came up.
  - **Mirrored cases** (a set property): a set written for one F2L slot is
    also set up for the others. Each case then comes up as written or in
    one of the chosen mirror images (left-right, front-back or both), with
    the cube seen from that slot's corner and the algs mirrored to match:
    `R U R'` is `L' U' L` left-right, `R' U' R` front-back and `L U L'`
    both ways. A case that doesn't depend on the slot, such as an OCLL
    with the F2L whole, is never mirrored: it comes up as written and keeps
    its algs. When a mirrored step stops at such a case, the cube is seen
    from the front right again, the way its alg is done. A case has one
    card whichever way it comes up.
  - **Practise in context** (for sets of F2L pair cases, such as a ZZ F2L
    set): a cube with EO Cross done and 1 to 4 slots open (the number next
    to the button), set up from cases you have started learning where it
    can be. Solve the pairs in any order; afterwards each pair you did is
    shown with the set's case and alg for it, and the set's cheapest order
    for the whole state (Tab shows it as a hint). Not graded; Space gives
    another, Esc tries the same one again, "Back to learning" returns to
    your cases.
- **Drill**: drill cases on the virtual cube (the default) or a physical
  cube, chosen for each alg set under "Cube". The two keep separate drill
  times and statistics, and the case list shows the chosen cube's.
  - The pieces the step doesn't care about (e.g. the permutation when
    drilling OLL) are scrambled too, and there's a random U turn before
    and after.
  - On the virtual cube the case is set up on the cube, which only turns
    as a whole until the timer starts: tap Space to arm it and the first
    face turn starts it (Settings > Drill can have Space held and let go
    instead; a turn made while it is held and green starts it too). The
    timer stops by itself as soon as the cube shows the step done (solved
    for PLL, the last layer oriented for OLL, and so on); Space stops it
    before that, which is a DNF unless the step is done. A DNF says
    whether you were an AUF off or how many pieces were wrong, and shows
    your moves. Stopped before any face turn, the attempt is cancelled.
  - On a physical cube a real random-state scramble sets the case up, so
    you can't tell the case from the scramble; hold Space and let go to
    start, and any key but Shift, Ctrl or Alt stops the timer. Drill has
    its own hold time (Settings > Drill).
  - Choose what to drill apart from the ticks Learn shares: the ticked
    cases, the cases the list shows (searched or filtered) or one group;
    and of those, the ones learned in Learn (the default), all of them,
    those being learned, those in Learn's queue, or the weakest few you
    have drilled. The order is random (never the same case twice in a
    row), weighted (slow and failed cases come up more, by the median of
    each case's recent times so that one slow time can't take over),
    list order (the list can be sorted) or a recap: every case once, then
    a summary. A failed case comes back three cases later (Settings).
  - After each case you see your time, coloured green at or under the
    set's target time, and the case diagram and algs with the AUFs this
    case needs. For a moment after the stop keys are ignored, so a second
    press doesn't skip it. Space goes on to the next case; Backspace
    toggles DNF (virtual) or marks a wrong alg (physical); Delete deletes
    the attempt (one stopped by mistake, say).
  - Shift+Space before or during the solve gives the case up, for a case
    you have forgotten: it is recorded as a DNF. Given up before the timer
    started, Delete takes it back and sets the case up again. (Settings
    can have Backspace give up instead, as earlier versions did.)
  - Tab practises the case, set up exactly as it was, with no timer and
    nothing recorded: first with the algs shown, then (Tab, or by itself
    once solved on the virtual cube) with them hidden, then the next case
    once you solve it without help. Space goes on to the next case at any
    point, and Backspace still toggles the DNF. The virtual cube checks
    each practice solve and Esc resets it; on a physical cube you
    scramble again. Settings can have Space practise after a DNF (Enter
    then goes on), and practice wait for a key at each stage.
  - A set with mirrored cases has each case set up in a random one of its
    mirror images, and a set solved a step at a time is drilled from the
    case to the end of the step, with every step shown afterwards.
  - Per-case recent average, best time (kept for good, not only for the
    attempts kept), fail rate and weakness (how often Weighted picks the
    case), with each group's row showing its cases taken together. Click a
    column's header to sort by it (again for the other way, a third time
    for list order). Right-click a case for its drill history (a chart of
    its last 100 attempts, and every attempt summed up by day as a table
    and a chart) or to clear its drill times. A session panel counts the
    attempts, mean, DNFs and new bests since the session started and
    names the slowest cases. On the virtual cube
    each solved attempt also records its recognition time (from the case
    being set up to the first turn of a layer other than U or D) and its
    execution time, which Learn's Recog column and the "slow to recognise" filter
    use. Drill attempts never change the spaced-repetition schedule.
- Learn and Drill share each set's ticked cases and learn queue (cases
  added to a set later come ticked). Only cases
  with at least one alg that solves them are offered, and both modes show
  how many are skipped. Cases are set up for the holds chosen under
  colour neutrality (below).
- The case lists in Learn, Drill and Library can be searched by case or
  group name (so "BL" finds every case in a group named for the BL slot)
  and filtered by status: ticked or not, new, in the learn queue,
  learning, learned or due in Learn; learned, never drilled or slower than
  the target in Drill; without algs, without a passing alg or with
  failing algs in Library; slow to recognise in Learn and Drill. In Learn
  and Drill the search also matches the cases' algs. While the list is
  filtered, "Tick shown" and "Untick shown" change only the cases shown.
  Double-click a case in Learn or Drill to practise it now (the keys go
  back to the cube). A case is selected in the list only once it is
  revealed, so the list doesn't name the case you are recognising.
- **Library**: create and edit alg sets.
  - The main view shows the set list, the selected set's case tree and the
    selected case with its diagram, group, algs and Learn and Drill
    progress. In Library mode the menu bar has a **Set** menu (also on
    right-click in the set list) that creates, duplicates, deletes,
    imports, exports and checks sets, and a **Cases** menu (also on
    right-click in the case tree) that adds, renames, moves and deletes
    groups and cases. Ctrl- or Shift-click selects several cases; drag
    them (or a group) to another place, or use **Move to group** or
    Alt+Up / Alt+Down.
  - Every change is saved at once and can be undone (**Edit > Undo**,
    Ctrl+Z) and redone while v-cube is open, including deleted cases,
    groups and sets with their progress and per-set settings. Deleting asks
    first and says how much progress goes with it.
  - **Set properties…** (or double-click a set, or Alt+Enter) edits, on
    three tabs with each option explained, the name, description,
    random U turns around cases in Learn and Drill, target time, guided
    learning, learning by family, solving a step at a time and the mirror
    images cases are also set up in (see Learn), and the
    step settings for the whole set or as a group's own override: what the
    algs may leave unsolved (e.g. "last-layer permutation" for OLL-like
    sets) and how diagrams look (full colour, top colour only or grey; from
    above, or in 3D from any top corner). "Follow the pieces" applies the
    last-layer settings to the last-layer pieces wherever they are, and to
    F2L pieces the step leaves free, so that F2L pieces in the top layer
    keep their colours (F2L sets use it). With "top colour only", a
    D-layer corner the step leaves free shows its D sticker instead, as it
    does when it sits in the last layer. A diagram shows a case of the set
    as the settings draw it, and a line says how many algs would stop (or
    start) passing. Pieces that trade places share what may be left
    unsolved, so a spec like `LLE:p UF:o` is checked as `LLE:po`, as drill
    scrambles have it.
  - Each case can have several algs (no two the same). Drag them to
    reorder (or Alt+Up / Alt+Down), double-click or F2 to edit, Delete to
    remove, and right-click to make one the main alg.
  - Every alg is checked against its case under the step settings in force,
    with a visible result: e.g. `(U') … (U2), 14 moves` for a pass, or
    `leaves 3 corners unsolved (U layer)` or `can't read at character 7`
    for a fail (moves in HTM, a slice turn counting two, as solves count
    them). OLL algs pass whatever last-layer permutation they leave,
    and algs may need a U turn before or after when the set allows it.
    In a set solved a step at a time an alg also passes when it stops at
    another case of the set, shown as `→ Sune, 4 moves`, and the case shows
    every step from there to the end.
    Failing algs are kept with a warning; the case tree marks cases with
    failing or no passing algs, the set list counts failing algs, and
    **Check algs…** lists them all.
  - The main alg is the first passing alg. Each case is drawn, and set up
    in Learn and Drill, from the angle its main alg starts from: a leading
    `y`, `y'`, `y2` or U turn is done to the case, so the rest of the alg
    solves it directly. The stored case and algs don't change.
  - **Paste algs…** (Ctrl+Shift+V) takes any list of algs. Lines may look
    like `Name: alg1 / alg2`, as in Alg-Trainer, or be tab-separated
    `name, alg, alg…` columns copied from a spreadsheet, and may end with a
    `// comment`. Each alg goes to the case its line names (by name or id)
    when it solves it; without a name, to the case it solves (any AUF the
    set allows, any cube rotations), so the order doesn't matter. Check
    shows a table of what each alg would do before anything changes: an
    alg that solves another case than the one named, or several cases, or
    none, isn't filed but can be corrected in place or given a case. New
    cases (when allowed) go to the group you pick; a set solved a step at a
    time gets none. Apply adds the rest, and algs left out come back the
    next time you paste. Algs copied from web pages are fine: curly or
    accented primes and no-break spaces are stored as plain ' and spaces,
    and repeats may be written `(R U R' U')2`, `x2`, `*2` or `×2`.
  - Create a case from an alg, and import and export sets as JSON. Each
    set is saved back to the file it came from; if two files hold the same
    set id (such as a copy made in a file manager), only one is loaded and
    the other is reported. **Set > Restore a built-in set** brings back a
    deleted built-in set, and **Restore built-in cases…** its deleted
    cases.
- **Built-in sets**: PLL (21), OLL (57), 2-look OLL (10) and 2-look PLL (6).
  These ship as case definitions only, with no algs, so you add your own.
- **Solving on the virtual cube** (Timed, Untimed and Move Count): each
  scramble is put on the cube, which can only be turned as a whole (to
  look at it from any side) until the solve starts, so a slip can't turn a
  face early. Space ends the solve. Each solve keeps its scramble and its
  solution, the moves as you did them with turns of the same layer joined
  (R R is R2, R R' nothing), and its move count in HTM (a slice turn counts
  two, rotations nothing). Counts are simplified by default: turns that
  cancel across a turn of the opposite layer or a rotation about the same
  axis don't count (U D U' is one move, R x R' none); Settings > Solves
  turns this off to count every turn as done. Hover over a solve to see
  its date, scramble and solution, right-click it to copy either or open
  **Details…**, and a solve with a comment is marked `*`. A session's cube
  (and Move Count step) is fixed once it has solves: start a new session
  for another. **Delete last** (Delete) sits under the list, and Ctrl+Z
  puts the last deleted solve back. Click a statistic to see its solves
  (brackets mark the ones an average drops); a new best single, mo3 or
  average is announced after the solve. Averages are rounded to the
  shown decimals as the WCA does (singles truncated first). The chart's
  right-click menu charts the last 12 to 500 solves; hover over it for a
  solve's values.
- **Timed**: hold Space to start (the hold time is configurable).
  - Physical cube: a small net next to the scramble shows the scrambled
    cube, white on top and green in front, and any key stops the timer.
  - Virtual cube: the scrambled cube is shown, rotations only, until the
    timer starts. The timer stops when the cube is solved (Settings >
    Solves can leave that to Space). Space stops it too; if the cube isn't
    solved then, the solve is a DNF, and the cube stays as you left it to
    look at until Space (or Esc) brings the next scramble. Esc during a
    solve cancels it and puts the scramble back. Each move's time is kept,
    which gives the **Moves**, **TPS**, **Cross**, **F2L** and **LL**
    columns (EO Cross for a ZZ solve; right-click the header to choose
    columns) and, from the chart's menu, cross/F2L/LL lines. After a solve
    its EO Cross count is shown with the optimal. **Review last** (Alt+R,
    or double-click a solve) opens the review.
  - Optional WCA inspection, with +2 and DNF applied automatically. On
    the virtual cube the scrambled cube is hidden until inspection starts
    (a setting).
  - With colour neutrality, the hold a scramble was made for is shown
    under it (in Untimed and Move Count too).
  - Sessions, with ao5/ao12/ao100, mo3, best, mean, +2/DNF/comments per
    solve, and a trend chart. The session menu switches, adds, renames and
    deletes sessions, and imports and exports cstimer sessions. Imported
    sessions are for a physical cube; empty sessions and sessions for
    other puzzles are left out, and virtual and smart cube solutions come
    with their move times. The export keeps cstimer's scramble types and
    solutions; import it in cstimer with **Import session(s) from other
    timers**, which adds the sessions to yours (cstimer's "Import from
    file" would replace all your data).
- **Untimed**: solves with no timer.
  - Virtual cube: hold Space until the hint turns green and release it to
    start the solve, as in Timed; it ends when the cube is solved (or on
    Space, as a DNF if it isn't). No move count is shown during the solve;
    afterwards "Last: N moves" shows it, with your EO Cross count and the
    optimal. After the solve the cube turns freely, and the alg field
    applies a typed alg with animation; Esc resets the cube to solved, and
    Space or Enter gives the next scramble. During a solve Esc puts the
    scramble back.
  - Physical cube: large scramble text with a net preview. Space or →
    keeps the scramble as done and shows the next, Backspace or ← goes back
    through the last 100 (and → forward again). Done scrambles are listed
    in the session with their dates, and the statistics count them (in
    all and today); right-click one to copy it.
  - Sessions show mo3, ao5/ao12/ao100, best and mean over the move counts
    of virtual solves, and a trend chart.
- **Move Count**: practise move efficiency for one step at a time, with no
  timer. Each session is for one step: EO, EO+1 or EO+2 (EO with any one
  or two cross edges), EOLine (EO with the two cross edges on the EO
  axis), EO Cross, XEOCross (EO Cross and any one F2L pair), ZZ F2L (the
  first two layers with every edge oriented), Cross, or CFOP F2L (the
  first two layers, edges in any orientation). New installs start with
  one virtual-cube session per step.
  - Virtual cube: hold Space until the count turns green and release it
    to start the solve, as in Timed; Space ends it. The move count
    is recorded if the step is done, and DNF if not, and the next scramble
    follows. The step may be done on any colour and any axis: EO is
    checked as ZZ does (edges that need no F or B quarter turns) with the
    cube held any way, and the cross may be on any face. Esc puts the
    scramble back.
  - Physical cube: solve the step, then type how many moves your solution
    took and press Enter to record it and get the next scramble. Type the
    count with the digit keys (or keypad); Backspace corrects it and Esc
    clears it. **DNF** records a scramble whose step went wrong.
  - Sessions work as in Timed, with mo3, ao5/ao12/ao100, best, mean, DNF
    and comments per scramble, and a trend chart, all over move counts.
    **Skip scramble** moves on without recording, and right-clicking a
    count edits it, toggles DNF, adds a comment or copies its scramble or
    solution.
  - The **Opt** column has each scramble's optimal move count for the
    session's step (EO to EO Cross, and the cross) in any hold, as the step
    is checked (Settings > Solves can limit it to the holds of your colour
    neutrality, either EO axis allowed), and the statistics add the mean
    moves over optimal and how often you were optimal.
  - **Review** (double-click a solve, right-click > Review…, or **Review
    last**): replays the solve on a cube of its own (←/→, Home/End or the
    slider) with the moves where EO, EO+1, EO+2, EO Cross and ZZ F2L were
    first done marked, and how many moves each took and in which hold. For
    EO to EO Cross and the cross it lists the optimal solutions in your
    holds and over every colour and axis ("show" plays one). For a ZZ F2L
    (or whole) solve it splits the F2L into pairs and compares each with an
    alg set of F2L pair cases (your ZZ F2L set; any set whose cases each
    solve one slot's pair, with its mirrors covering the other slots): the
    case the pair was, your moves against your alg and against the optimal
    <R, U, L> pair, the cheapest pair at that point, and the set's cheapest
    order overall. Untimed and Timed solves can be reviewed too; a whole
    solve also gets its splits (cross, each pair, last layer: moves, and
    time and TPS for a timed solve). **Copy reconstruction** copies the
    solve a step per line, and **alg.cubing.net** opens it there.
  - **Retry this scramble** (in the review, virtual cube) puts the scramble
    back on the cube to solve again. The retry isn't recorded; you see its
    count against the original and the optimal, and Space (or Skip
    scramble) goes back to new scrambles. **Retry the worst 5** (session
    menu, Untimed and Move Count on the virtual cube) retries the DNFs and
    the solves furthest over the optimal (or longest) one after another.
  - Each session keeps its own scramble type. Choosing a step picks a
    WCA scramble, or EO Cross solved for ZZ F2L. Only the scramble types
    that suit these steps are offered: WCA random state (for EO, EOLine,
    EO Cross or cross), EOLine solved, EOCross solved (ZZ F2L) and F2L
    (cross solved, for CFOP F2L). Colour neutrality applies, as
    everywhere.
- **Inspection**: practise the inspection of a ZZ solve on the virtual
  cube, in sessions of its own.
  - Plan sessions are for one step: EO, EOLine, EO+1, EO+2 or EO Cross.
    Space shows the scrambled cube, which only turns as a whole while you
    plan. Space again hides it, and you do your plan with the turning keys
    without seeing the cube; Space then reveals it. The step is checked
    (DNF if it isn't done, with how far it got: bad edges left, cross edges
    solved), the moves are counted in HTM, the hold you did it in is kept,
    and your count is compared with the optimal in your colour neutrality's
    holds (either EO axis) and in any hold, with up to three optimal
    solutions that aren't reorderings of one another, such as
    `yellow cross, green front: z2 · R' B D2 L' F`. After an attempt,
    rotations turn the cube to look at it. **Review last**, double-click
    or right-click > Review… replays an attempt; **Retry** (or Retry in the
    review) plans its scramble again without recording it; **Delete last**
    deletes the last attempt. A scramble you have seen is never served
    again (Esc during an attempt puts it back, unrecorded).
  - An EO Cross session can track a pair: pick it under **Then** (FL, BL,
    FR or BR, as you hold the cube when the cross is done) and solve that
    pair blind after the cross. The moves to EO Cross are compared with the
    optimum, and whether the pair was solved (and in how many more moves)
    is kept.
  - Each Plan session has an inspection limit: WCA (15 s, with calls at 8
    and 12 seconds), 10, 20 or 30 s, a custom one, or none. With a limit
    the seconds left are counted down, as in Timed mode. The cube is
    hidden when it runs out (a Space just then is ignored, and Space with
    no moves done doesn't finish), and the time used is kept with each
    attempt.
  - Bad edges sessions show the scrambled cube in a hold from your colour
    neutrality. Type how many edges are bad for the F/B axis (the ZZ axis,
    as held), then Enter, and for the R/L axis too if the session asks for
    both. The counts are checked and the bad edges ringed; Tab rings the
    other axis's.
  - Plan statistics: mo3/ao5/ao12 over move counts, moves over optimal,
    how often you were optimal, and mean inspection time, broken down by
    the scramble's optimal length and by the inspection time used. The
    chart shows moves over optimal, the optimal rate over the last 12
    attempts, or move counts. Bad edges
    statistics: accuracy, mean time when right, and ao5/ao12 of times with
    a wrong count as DNF. Right-click an attempt to comment, copy its
    scramble or solution, review it, or delete it. New installs start with
    EO, EO+2 and EO Cross (WCA inspection) and a Bad edges session.
- **Blind**: blindfolded solving with Old Pochmann. Edges use buffer UR,
  swapped with UL by the T-perm. Corners use buffer UBL, swapped with RDF
  (its R sticker) by the modified Y-perm. After an odd number of edge
  targets, the parity alg goes before the corners. Each drill has a tab
  at the top of the page, shown in focus mode too:
  - **Letter quiz**: a sticker of the solved cube is ringed on the net and
    you type its letter, or a letter is shown and you click its sticker.
    Mixed does both in turn. Choose edges, corners or both. Each letter
    keeps its tries, right answers and mean time (an answer counts for at
    most 30 s, and the clock stops while you are away from the page or
    the window), and slow or missed letters come up more often. Letters
    are matched ignoring the case of A-Z, as the scheme checks them.
  - **Memo tracing**: a scrambled cube is shown (turn it as a whole to look
    around) and the timer runs. Press Enter, type the edge letters, Enter,
    the corner letters, and Enter again. The memo is checked by doing it,
    so any valid cycle break counts (letters that can't be read are
    pointed out to fix, not graded). The first wrong letter is marked with
    what should have come there and ringed on the cube, next to the memo
    traced by the app and whether there is parity; rotations still turn
    the cube. A scramble traced is never one of the blind solves. Right-
    click a trace to delete it.
  - **Setup moves**: a letter is given and its sticker ringed. Turn the
    cube to bring that sticker to the swap spot (◎) without moving the
    buffer or the other pieces the swap alg turns. The drill finishes the
    moment it's there, and shows your setup next to the one in your scheme.
    Or choose "Type it" to type the setup instead: keys pressed outside
    the field go back to it, Esc there gives the letter up, and an empty
    Enter isn't an answer. Statistics per letter, as in the quiz.
  - **Blind solves**: sessions on the virtual cube or a physical one.
    - Virtual: Space starts the memo and the timer, with the cube turning
      only as a whole. Space hides the cube for the execution, and Space
      stops the timer. A cube that isn't solved is a DNF, with how many
      edges and corners are off and the traced memo; rotations turn the
      cube to look at it.
    - Physical: Space starts the timer, Space again ends the memo, any key
      stops. Backspace toggles DNF on the solve just done.
    - A solve cancelled after the memo started gets a new scramble.
    - Statistics: mo3, ao5, ao12, success rate, mean of successes and mean
      memo, plus a trend chart.
  - **Lettering & setups…**: the blind hold (the colours on top and in
    front that the letters belong to; white top, green front by default),
    a net with every sticker's letter, and for each sticker its letter and
    setup. Setups are worked out by the app (edges with L, D, M, E, l and
    d; corners with R, D and F) and can be replaced with your own, which
    are checked; one that doesn't work isn't used (the computed one is,
    until it is fixed). Speffz letters by default; every letter can be
    changed.
    The lettering, your own setups and the blind hold are kept in the
    settings file under `[blind]`.
- **Scramble types** (Timed and Untimed):
  - WCA random state.
  - cstimer-style subsets: F2L/cross solved, XCross, EOLine, EOCross,
    LSLL, LL, ZBLL, CLL, ELL, PLL, EPLL, corners only, edges only.
  - Any of your alg sets, using its ticked cases, in the set's mirror
    images too. Tab (or the Algs button) shows or hides the algs for the
    case each scramble sets up, with the AUFs it needs (every step to the
    end for a set solved a step at a time); nothing is graded or recorded.
- **Colour neutrality** (Edit > Colour neutrality…, `Ctrl+Shift+N`, or the
  Settings tab of the same name) applies to every mode. Scrambles are
  always applied holding white on top and green in front; you choose the
  holds you solve in, and each scramble is set up for one of them, picked
  at random.
  - Presets start from a main hold (e.g. yellow top, green front): fixed,
    y2 (front colour or its opposite), y (any front), x2 (top colour or
    its opposite), x2 y2, x2 y (top or its opposite, any front) and full
    (any of the 24 holds). Or tick any set of holds in the top/front grid.
  - For example, an EOCross scramble held x2 y neutral from white top,
    green front gives a yellow or a white cross (on the bottom once you
    turn the cube to the hold), with any front colour.
  - Learn and virtual Drill show each case on the virtual cube in the
    colours of its hold, and physical Drill's scrambles set the case up for
    its hold.
  - Case diagrams and thumbnails are drawn in the main hold.
- **Virtual cube** (Learn, Drill on the virtual cube, and sessions on the
  virtual cube):
  - Choose the view in each mode: animated 3D, a flat view of the U and F
    faces with the adjacent L and R stickers (as in Alg-Trainer), an
    unfolded net, or hidden. Learn remembers the view for each alg set, and
    its 3D view (and Drill's) looks from the corner the case's diagrams are
    drawn from (e.g. front left for left-slot F2L cases).
  - Turned from the keyboard with cstimer's face keys (Shift for wide
    moves), which you can remap in Settings: select a row and **Change**
    its key or move, or **Add** one. A turning key may be held with Shift
    but not Ctrl or Alt.
- **Full screen**: F11 (or View > Full screen) toggles full screen. The
  window reopens at the size and place you left it, but never in full
  screen or focus mode.
- **Focus mode**: F10 (or View > Focus mode) hides the sidebar, menus,
  status bar and each mode's lists and options, leaving the scramble,
  timer, cube and algs; messages show at the top of the window instead.
  Right-click anywhere outside them to switch full screen or focus mode
  with the mouse.

## Building

The app is developed on Ubuntu 26.04 under WSL2/WSLg with g++ 15 and Qt
6.10, and also builds natively on Windows with MSVC.

### Linux

```bash
sudo apt install build-essential cmake ninja-build qt6-base-dev catch2
cmake --preset release
cmake --build --preset release
./build/release/src/app/v-cube
```

Catch2 is fetched automatically if it isn't installed. Other presets:
`debug`, and `asan` (AddressSanitizer + UBSan). Run the tests with
`ctest --preset debug`. Pass `-DVCUBE_BUILD_APP=OFF` to build only the
libraries and tests, which need Qt Core but no GUI.

### Windows

Prerequisites:

- Visual Studio 2022 or later, or its Build Tools, with the **Desktop
  development with C++** workload. Check that its **Windows 11 SDK** (or
  Windows 10 SDK) component is ticked: the build needs the SDK's `rc.exe`
  and `mt.exe`, and CMake fails with "is not able to compile a simple test
  program" without them. The workload also provides CMake and Ninja; a
  standalone CMake 3.25+ and Ninja work too.
- Qt 6.5 or later for MSVC (`msvc2022_64`) from the
  [Qt online installer](https://www.qt.io/download-qt-installer).
- Git, used to fetch Catch2 for the tests.

Open an **x64 Native Tools Command Prompt** for your Visual Studio version
(`where rc mt` should find both), point `QTDIR` at the Qt install, then
configure, build, test and deploy:

```bat
set QTDIR=C:\Qt\6.10.0\msvc2022_64
cmake --preset windows-release
cmake --build --preset windows-release
ctest --preset windows-release
cmake --install build\windows-release
install\windows-release\bin\v-cube.exe
```

`cmake --install` runs `windeployqt` and copies the MSVC runtime, so
`install\windows-release\bin` is a self-contained folder that you can copy
to a PC without Qt. A `windows-debug` preset is also available. The
executable in `build\` needs Qt's `bin` folder on `PATH` to run.

### WSL

Under WSL (detected from `WSL_DISTRO_NAME` or a kernel release that names
Microsoft) the app uses Qt's X11 (`xcb`) platform plugin by default rather
than Wayland. WSLg forwards every Wayland surface to Windows as a separate
window, which leaves closed drop-downs and menus on screen and stops the
window position from being restored. To pick another plugin, set
`QT_QPA_PLATFORM` yourself; it always takes precedence:

```bash
QT_QPA_PLATFORM=wayland ./build/release/src/app/v-cube
```

**Help > About v-cube** shows the platform plugin in use. Outside WSL, Qt
picks the plugin as usual.

## Using it

### Keys

| Where | Key | Action |
| --- | --- | --- |
| Anywhere | Ctrl+1 … Ctrl+8 | switch to Learn, Drill, Library, Timed, Untimed, Move Count, Inspection, Blind (not during a solve) |
| Anywhere | F11 | toggle full screen |
| Anywhere | F10 | toggle focus mode |
| Anywhere | Ctrl+Shift+N | colour neutrality settings |
| Learn | turning keys | turn the virtual cube; solving the case finishes it |
| Learn, walkthrough | → / ← / Home | step through the main alg / back / to the start |
| Learn, walkthrough | Space / Enter | do the case with each next move shown |
| Learn, in context | Tab / Esc / Shift+Space | show the set's cheapest order / start again / give up |
| Learn, in context, afterwards | Space / Enter / Esc | another one / the same one again |
| Learn, in context | Backspace | back to learning |
| Learn | Tab | show the algs (grades the case Again) |
| Learn | Shift+Space | give up (grades the case Again) |
| Learn | Esc | reset the case (grades it Again once the alg is begun; in a set solved a step at a time, back to where the current case came up) |
| Learn, after a case | Space / Enter | next case |
| Learn, after a case | 1-4 | grade Again / Hard / Good / Easy |
| Learn, after a case | Esc | reset the cube to practise the case |
| Learn | Ctrl+Z | take back the last grade (up to 10), and do the case again |
| Learn | F9 | hide or show the case list |
| Learn, nothing due | Space / Enter | look again; take the next learning case early |
| Learn, Recognise | Space / Shift+Space | recalled the alg / can't recall it (the algs are shown) |
| Learn, Recognise, afterwards | 1-4 / Space | grade and go on / go on with the grade shown |
| Learn, Which case? | 1-6 / Space | answer / next case |
| Learn, Drill, Library | search box: Enter / Esc | return the keys to the cube / clear the search |
| Library | Ctrl+Z / Ctrl+Shift+Z (or Ctrl+Y) | undo / redo |
| Library | Ctrl+Shift+V / Ctrl+Shift+A / Ctrl+Shift+G | paste algs / new case from alg / new group |
| Library | Ctrl+N / Alt+Enter / Ctrl+K | new set / set properties / check algs |
| Library | Delete / F2 / Alt+Up, Alt+Down | delete, rename, move the selected cases or group (or the selected alg, in the alg list) |
| Drill (virtual) | tap Space, then turn | arm the timer; the first face turn starts it (or hold Space and let go, in Settings) |
| Drill (physical), Timed | hold Space, release | start (after the hold time, when the time turns green) |
| Drill, Timed (physical) | any key but a modifier | stop |
| Drill, Timed | Esc | cancel |
| Drill (virtual) | turning keys | rotations only before the timer starts, then any turn; the timer stops when the step is done |
| Drill (virtual) | Space | stop early (DNF if the case's step isn't done) |
| Drill | Shift+Space | give the case up (a DNF) |
| Drill, after a case | Space / Enter | next case |
| Drill, after a case | Tab | practise the case, with the algs, then without (Tab switches) |
| Drill, after a case | Backspace | toggle DNF (virtual) or "wrong alg" (physical) |
| Drill, after a case | Delete or Ctrl+Z | delete the attempt (take back a give-up before the timer started) |
| Drill, after a case | Esc | try the case again |
| Timed (virtual) | turning keys | rotations only before the timer starts, then any turn |
| Timed (virtual) | Space | stop (DNF if the cube isn't solved); after a DNF, the next scramble |
| Timed, after a solve | Alt+1 / Alt+2 / Alt+3 | no penalty / toggle +2 / toggle DNF on the last solve |
| Timed, Untimed, Move Count | Delete / Ctrl+Z | delete the last solve / put the last deleted solve back |
| Timed, Untimed, Move Count | Ctrl+→ | skip the scramble |
| Timed, Untimed, Move Count | Alt+C / Alt+R | comment on / review the last solve |
| Untimed, Move Count (virtual) | turning keys | rotations only before the solve starts, then any turn |
| Untimed, Move Count (virtual) | hold Space, release | start the solve (after the hold time, when it turns green) |
| Untimed, Move Count (virtual) | Space | end the solve (DNF if not solved, or the step not done) |
| Untimed, Move Count (virtual) | Esc | put the scramble back |
| Untimed (virtual), after a solve | Space or Enter / Esc | next scramble / reset the cube to solved |
| Untimed (virtual), alg field | Enter / Esc | apply the alg / return the keys to the cube |
| Untimed (physical) | Space / → | keep the scramble as done and show the next |
| Untimed (physical) | Backspace / ← | previous scramble |
| Move Count (physical) | 0-9 | type the move count |
| Move Count (physical) | Backspace / Esc | correct / clear the move count |
| Move Count (physical) | Enter | record the move count and show the next scramble |
| Move Count (physical) | Alt+3 | record the scramble as DNF |
| Timed, Untimed | Tab | show or hide the algs for an alg set scramble's case |
| Review | ← / → / Home / End | step the replay back / forward / to the scramble / to the end |
| Inspection | Space | start inspection / hide the cube / reveal it / next scramble |
| Inspection | turning keys | rotations only while you look at the cube; any turn once it is hidden; rotations after an attempt |
| Inspection | Esc | put the scramble back (nothing is recorded); end a retry |
| Inspection (bad edges) | 0-9, Backspace, Enter | type a count, correct it, give it |
| Inspection (bad edges), after an attempt | Tab | ring the other axis's bad edges |
| Blind, letter quiz | letter keys / Enter / Backspace | answer / answer a longer label / correct |
| Blind, letter quiz | click a sticker on the net | answer a letter → sticker question |
| Blind, quiz and setups | Space | next letter (before an answer: give up or skip; not for half a second after the quiz moved on) |
| Blind, setup moves | turning keys / Esc | do the setup / start it again (after it: try the letter again) |
| Blind, typed setups | Esc (outside the field) | give the letter up |
| Blind, memo tracing | Space / Enter / Esc | new scramble / type the letters (Enter after edges, Enter to check) / give up the scramble |
| Blind, after a trace or a virtual solve | turning keys | rotations, to look at the cube |
| Blind solves (virtual) | Space | start memo, start execution (hides the cube), stop |
| Blind solves (physical) | Space / any key | start, end memo / stop |
| Blind solves | Esc / Backspace | cancel / toggle DNF (physical, after the solve) |

Default turning keys (face keys from cstimer's layout; Shift + a face key
turns the wide layer, e.g. Shift+I is r and Shift+G is f'):

| Move | Key | Move | Key | Move | Key |
| --- | --- | --- | --- | --- | --- |
| R R' | I K | U U' | J F | F F' | H G |
| L L' | D E | D D' | S L | B B' | W O |
| M' M | U M | x x' | R V | y y' | ; A |
| z z' | P Q | | | | |

### Getting algs into a set

1. In **Library** mode, pick a set (e.g. PLL) and click **Paste algs…**.
2. Paste your list and click **Check**. Each alg is matched to its case
   (the one its line names, or the one it solves), and the table shows
   where each goes; fix or assign the ones that aren't filed, then
   **Apply**. Each alg's check result is shown next to it in its case.
3. In a case's alg list, drag an alg to the top or right-click it and
   choose **Make main** to pick which alg Learn and Drill show first.

For your own sets, **Set > New set…** asks what the algs solve, then
**Cases > New case from alg…** or **Paste algs…** (with "Make new cases"
ticked) builds the cases. If you change a set's step settings later,
**Set > Check algs…** lists the algs that no longer pass.

### Data

| What | Linux | Windows |
| --- | --- | --- |
| Alg sets (one JSON file each) | `~/.local/share/v-cube/algsets/` | `%LOCALAPPDATA%\v-cube\algsets\` |
| Learn and drill progress per set | `~/.local/share/v-cube/algsets/progress/` | `%LOCALAPPDATA%\v-cube\algsets\progress\` |
| Timed sessions | `~/.local/share/v-cube/sessions.json` | `%LOCALAPPDATA%\v-cube\sessions.json` |
| Untimed sessions | `~/.local/share/v-cube/untimed.json` | `%LOCALAPPDATA%\v-cube\untimed.json` |
| Move Count sessions | `~/.local/share/v-cube/movecounts.json` | `%LOCALAPPDATA%\v-cube\movecounts.json` |
| Inspection sessions | `~/.local/share/v-cube/inspection.json` | `%LOCALAPPDATA%\v-cube\inspection.json` |
| Blind sessions and drill statistics | `~/.local/share/v-cube/blind.json` | `%LOCALAPPDATA%\v-cube\blind.json` |
| Settings (theme, colours, keys, mode choices) | `~/.config/v-cube/v-cube.ini` | `%APPDATA%\v-cube\v-cube.ini` |

A file that can't be read (e.g. cut short by a crash) is never saved
over: it is renamed to `<name>.unreadable-<date>-<time>` next to where it
was, the app says so when it starts, and that data starts afresh. A file
written by a newer version of v-cube is left as it is and never saved
over; the app says so, and that data starts afresh until you update. An
alg set file with a problem in its cases (stickers no real cube has, e.g.
from a typo in a hand-edited file, a missing or repeated case id, or an
alg that isn't text) is left out, and the app names the problem when it
starts.

Settings are an INI file on every platform; Windows doesn't use the
registry. Earlier versions kept them in `~/.config/v-cube/v-cube.conf`.
Those settings are copied to `v-cube.ini` the first time a newer version
runs, and the old file is left in place.

**File > Open data folder** opens the data folder in your file manager
(Windows Explorer under WSL, through `wslpath` and `explorer.exe`). If no
file manager can be started, it shows the data folder and config file paths
with buttons to copy them.

Built-in sets are copied into the alg-set folder on first run, and are
then yours to edit. **Set > Restore built-in cases** brings back the
shipped cases (their names and states, and any you deleted) and keeps your
algs, the cases and groups you added and the set's settings.

Alg-set files look like this (states are 54-character facelet strings in
URFDLB order; `free` lists what the step may leave unsolved, e.g. `LL:p` =
last-layer permutation; `"by": "piece"` in `mask` makes it follow the
pieces; `view` is `top`, `3d` (from the front right), `3d-front-left`,
`3d-back-left` or `3d-back-right`; `"reduction": true` makes the set one
solved a step at a time, and `"mirrors"` lists the mirror images its cases
are also set up in, of `"left-right"`, `"front-back"` and `"both"`):

```json
{
  "format": "v-cube.algset", "version": 1,
  "id": "oll", "name": "OLL", "free": "LL:p",
  "mask": {"llCorners": "orientation", "llEdges": "orientation", "rest": "full"},
  "view": "top", "aufBefore": true, "aufAfter": true, "targetSeconds": 3,
  "groups": [{"name": "OCLL (cross)", "cases": [
    {"id": "oll27", "name": "OLL 27 (Sune)", "state": "UUU…", "algs": ["R U R' U R U2 R'"]}
  ]}]
}
```

## Layout

| Directory | Contents |
| --- | --- |
| `src/cube` | Pure C++ engine. Notation parser (groups, commutators, conjugates; invert, mirror, simplify); a facelet model built from 3D geometry that handles slices, wide moves and rotations; a cubie model; Kociemba's two-phase solver (tables built in under a second); random-state and subset scramblers. |
| `src/train` | Pure C++ training logic. Alg sets, AUF-aware alg-to-case matching and verification, display orientation, drill states, paste import, timer state machine, WCA statistics (also over move counts), Move Count step checks and virtual-cube attempts, optimal EO to EO Cross and cross solving and <R, U, L> pairs (IDA*), solve review (step marks, step splits, F2L pair breakdown against pair-case sets, reconstructions), mirror families, mirror worlds and step-at-a-time (reduction) sets, Old Pochmann tracing and setups, SRS scheduler, Learn and Drill case picking. |
| `src/io` | Qt Core persistence. JSON alg sets, the user library, progress, timer, move count, inspection and blind sessions, and cstimer conversion. |
| `src/app` | Qt Widgets UI. Cube renderer, the eight mode pages, the solve review, the shared scramble source and settings. |
| `tests` | Catch2 tests for the engine, training logic and storage. They also check that every built-in case is distinct and that standard algs are matched to the right cases. |

## Credits

Inspired by [cstimer](https://github.com/cs0x7f/cstimer) (timer, subset
scrambles and keyboard layout) and
[Alg-Trainer](https://github.com/tao-yu/Alg-Trainer) (case drilling with
real scrambles, and the layout of the flat cube view). No code from either
is included. The scrambles use Herbert
Kociemba's two-phase algorithm.

## License

MIT; see [LICENSE](LICENSE).
