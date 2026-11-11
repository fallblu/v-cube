# Data and file formats

v-cube saves everything as you go: there is no Save command. This page says
where the files are, how they are kept safe, and what is in them, for
anyone who wants to back them up, move them, or edit an alg set by hand.

## Where the data is

| What | Linux | Windows |
| --- | --- | --- |
| Alg sets (one JSON file each) | `~/.local/share/v-cube/algsets/` | `%LOCALAPPDATA%\v-cube\algsets\` |
| Learn and Drill progress, per set | `~/.local/share/v-cube/algsets/progress/` | `%LOCALAPPDATA%\v-cube\algsets\progress\` |
| Deleted alg sets and their progress | `~/.local/share/v-cube/algsets/deleted/` | `%LOCALAPPDATA%\v-cube\algsets\deleted\` |
| Timed sessions | `~/.local/share/v-cube/sessions.json` | `%LOCALAPPDATA%\v-cube\sessions.json` |
| Untimed sessions | `~/.local/share/v-cube/untimed.json` | `%LOCALAPPDATA%\v-cube\untimed.json` |
| Move Count sessions | `~/.local/share/v-cube/movecounts.json` | `%LOCALAPPDATA%\v-cube\movecounts.json` |
| Inspection sessions | `~/.local/share/v-cube/inspection.json` | `%LOCALAPPDATA%\v-cube\inspection.json` |
| Blind sessions and drill statistics | `~/.local/share/v-cube/blind.json` | `%LOCALAPPDATA%\v-cube\blind.json` |
| Alg Lab (lab cases, candidates, test sessions, times, profiles) | `~/.local/share/v-cube/alglab.json` | `%LOCALAPPDATA%\v-cube\alglab.json` |
| Daily backups | `~/.local/share/v-cube/backups/` | `%LOCALAPPDATA%\v-cube\backups\` |
| Settings (theme, colours, keys, per-set and per-mode choices) | `~/.config/v-cube/v-cube.ini` | `%APPDATA%\v-cube\v-cube.ini` |

The data folder is Qt's generic data location plus `v-cube`, so on Linux it
follows `XDG_DATA_HOME`, and the settings folder follows `XDG_CONFIG_HOME`.

**File > Open data folder** opens the data folder in your file manager
(Windows Explorer under WSL, through `wslpath` and `explorer.exe`). If no
file manager can be started, it shows the data folder and settings file
paths with buttons to copy them.

Starting v-cube while it is running brings the running window to the front
instead, so two copies never save over each other (`v-cube.lock` in the
data folder tells them apart; one left by a copy that crashed is taken
over).

## Keeping the data safe

- **Atomic saves**: every file is written to a temporary file and renamed
  over the old one, so a crash mid-save leaves the old file whole.
- **A file that can't be read** (cut short, not JSON, not the kind of file
  it should be, a version that isn't a number) is never saved over: it is
  renamed to `<name>.unreadable-<yyyyMMdd>-<HHmmss>` next to where it was,
  the app says so when it starts, and that data starts afresh. If it can't
  even be renamed, it is left in place and nothing is saved over it.
- **A file written by a newer version** of v-cube (a `version` higher than
  this version reads) is left as it is and never saved over; the app says
  so, and that data starts afresh until you update.
- **An alg set with a problem in its cases** (stickers no real cube has,
  e.g. from a typo in a hand-edited file, a missing or repeated case id, or
  an alg that isn't text) is left out, and the app names the problem when
  it starts. If two alg-set files hold the same set id (such as a copy made
  in a file manager), the one named after the id is loaded, the other is
  left alone, and the app says so.
- **A save that fails** (a full disk, a folder that can't be written)
  shows a red banner naming the file, which stays until a later save of
  that file works or you dismiss it.
- **Daily backups**: the first time the app starts each day, it copies the
  data folder into `backups/<yyyy-MM-dd>/` (without the backups and deleted
  folders) and keeps the newest 14 days. A copy is written as
  `<date>.partial` first, so a copy cut short is never taken for a day's
  backup. The settings file isn't in the backups.
- **Deleted alg sets**: deleting a set (Library, **Set > Delete set…**)
  moves its file to `algsets/deleted/<id>.<yyyyMMdd-hhmmss>.json` and its
  progress to `algsets/deleted/progress/` instead of deleting them. To bring
  one back, move the files back (into `algsets/` and `algsets/progress/`,
  named `<id>.json`) while v-cube is closed. Deleting a set in the app can
  also be undone (Ctrl+Z) while v-cube is open.
- **Export all data** (File > Export all data…) copies the data folder
  (without backups and deleted sets) into `v-cube-export-<date>/data/` and
  the settings file into `v-cube-export-<date>/settings/`, in a folder you
  choose. To move to another computer, copy the `data` folder's contents
  into that computer's data folder, and the settings file into its settings
  folder, while v-cube is closed there.

## Format and version

Every data file is a JSON object that starts with a `format` and a
`version`:

```json
{"format": "v-cube.sessions", "version": 1, ...}
```

| File | `format` | May the format be missing? |
| --- | --- | --- |
| Alg set | `v-cube.algset` | no |
| Progress | `v-cube.progress` | yes (older files have none) |
| Timed sessions | `v-cube.sessions` | yes (older files have none) |
| Untimed sessions | `v-cube.untimed` | no |
| Move Count sessions | `v-cube.movecount` | no |
| Inspection sessions | `v-cube.inspection` | no |
| Blind | `v-cube.blind` | no |
| Alg Lab | `v-cube.alglab` | no |

Every format is at version 1. A file whose `format` is another v-cube
format, or not a v-cube format, can't be read. `version` may be left out
(read as 1); otherwise it must be a whole number of at least 1, and a
higher number than the app reads makes the file "newer" (see above).
Unknown keys are ignored, and keys that are left out take the defaults
below, so files from earlier versions load.

Times are milliseconds and timestamps are Unix seconds, unless said
otherwise. Scrambles, algs and solutions are move text in v-cube's notation
(`R U R' U'`, `r`, `M2`, `x`, ...).

## Alg sets

`algsets/<id>.json`, one file per set. Built-in sets are copied in on first
run and are then yours to edit (`algsets/installed-builtins.json` lists the
ones that were copied, so that a built-in set you deleted isn't copied in
again). **Set > Restore built-in cases…** brings back a built-in set's
shipped cases (their names and states, and any you deleted) and keeps your
algs, the cases and groups you added and the set's settings; **Set >
Restore a built-in set** brings back a deleted one.

```json
{
  "format": "v-cube.algset", "version": 1,
  "id": "oll", "name": "OLL", "description": "Orientation of the last layer: 57 cases.",
  "builtin": true,
  "free": "LL:p",
  "mask": {"llCorners": "orientation", "llEdges": "orientation", "rest": "full"},
  "view": "top",
  "aufBefore": true, "aufAfter": true, "targetSeconds": 3,
  "groups": [
    {"name": "OCLL (cross)", "cases": [
      {"id": "oll27", "name": "OLL 27 (Sune)",
       "state": "UUU…", "algs": ["R U R' U R U2 R'"]}
    ]}
  ]
}
```

### Set keys

| Key | Type | Default | Meaning |
| --- | --- | --- | --- |
| `format`, `version` | | | `"v-cube.algset"`, `1` |
| `id` | string | required | The set's id; must not be empty. Progress and per-set settings are kept under it, and the file is named after it. |
| `name` | string | `""` | Shown in the set lists. |
| `description` | string | `""` | Shown in Library and in the set's properties. |
| `builtin` | bool | `false` | A set that came with the app (Restore built-in cases works on it). |
| `aufBefore` | bool | `true` | An alg may need a U turn before it. In Learn and Drill, a random U turn is done before the case. |
| `aufAfter` | bool | `true` | An alg may need a U turn after it. In Learn and Drill, a random U turn is done after the case. |
| `targetSeconds` | number | `3` | Drill's target time; must be a positive number if given. |
| `guided` | bool | `true` | Guided learning: a new case's first time in Learn is a walkthrough. |
| `families` | bool | `false` | Learn by family: a case and its mirror images share one review card. |
| `reduction` | bool | `false` | Solved a step at a time: an alg may stop at another case of the set. |
| `mirrors` | array of strings | `[]` | Mirror images the cases are also set up in: any of `"left-right"`, `"front-back"` and `"both"`. Others are ignored. |
| `free` | string | `""` | What the algs may leave unsolved (below). |
| `mask` | object | all `"full"` | How diagrams look (below). |
| `view` | string | `"top"` | The diagrams' view (below). |
| `groups` | array | `[]` | The groups, in order. |

The app writes `guided`, `families`, `reduction` and `mirrors` only when
they differ from the default, and always writes `free`, `mask` and `view`.

### Group keys

| Key | Type | Meaning |
| --- | --- | --- |
| `name` | string | The group's name. |
| `free`, `mask`, `view` | | The group's own step settings, as for the set. A group with any of them has its own settings: the ones it gives, and the set's for the rest. A group with none follows the set. |
| `cases` | array | The cases, in order. |

### Case keys

| Key | Type | Meaning |
| --- | --- | --- |
| `id` | string | Required, and unique within the set. Progress is kept under it. |
| `name` | string | Shown in lists; may repeat. |
| `state` | string | Required: the case as it is to be solved, 54 facelet letters (below). It must be a cube that can be reached by turning. |
| `algs` | array of strings | The case's algs, the main one first (the main alg is the first that passes its check). Every entry must be a string. An alg that doesn't parse or doesn't solve the case is kept, and marked in Library. |

### `state`: facelets

54 letters from `URFDLB`, each naming the face whose centre colour the
sticker has. The stickers are in Kociemba's order: the nine of U, then R,
F, D, L and B; within a face row by row, as seen in the usual net (U from
above with B at the top, D from below with F at the top, the side faces
with U at the top). The solved cube is
`UUUUUUUUURRRRRRRRRFFFFFFFFFDDDDDDDDDLLLLLLLLLBBBBBBBBB`.

### `free`: what may be left unsolved

Space-separated tokens `<pieces>:<flags>`. `<pieces>` is `ALL`, `LL` (the
last layer), `LLC` (its corners), `LLE` (its edges), `F2L`, or one piece by
its position: corners `URF UFL ULB UBR DFR DLF DBL DRB`, edges
`UR UF UL UB DR DF DL DB FR FL BL BR`. `<flags>` is any of `p` (the
permutation may be left unsolved) and `o` (the orientation may). An empty
string leaves nothing free (a solved cube). E.g. `LL:p` is OLL (the last
layer oriented, permuted any way) and `LL:po` is F2L (the last layer
anything).

Pieces whose places are free trade places, so they share what may be left
unsolved: if one of them may also be left twisted or flipped, all of them
may. `LLE:p UF:o` is read as `LLE:po`, as the drill scrambles have it.
A token that can't be read makes the file unreadable.

### `mask` and `view`: how diagrams look

`mask` has three keys for the stickers of the last-layer corners
(`llCorners`), the last-layer edges (`llEdges`) and the rest (`rest`), each
`"full"` (full colour), `"orientation"` (the top colour only; other
stickers grey) or `"hidden"` (grey). An unknown or missing value is
`"full"`. `"by": "piece"` makes the mask follow the pieces ("Follow the
pieces" in the set's properties): the last-layer settings go to the
last-layer pieces wherever they are, and to F2L pieces the step leaves
free.

`view` is `"top"` (from above), `"3d"` (3D from the front right),
`"3d-front-left"`, `"3d-back-left"` or `"3d-back-right"`. Anything else is
`"top"`. Learn's and Drill's 3D cube looks from the same corner.

## Progress

`algsets/progress/<id>.json`, one per set, written as the set is learned
and drilled. A set's progress goes with it when it is deleted.

```json
{
  "format": "v-cube.progress", "version": 1,
  "cases": {
    "oll27": {
      "card": {"state": 2, "step": 0, "ease": 2.5, "interval": 3.2,
               "due": 1762900000, "reps": 4, "lapses": 0,
               "introduced": 1762500000},
      "attempts": [[1762800000, 0, 0, "learn", 820, 0],
                   [1762800100, 1430, 0, "vdrill", 610, 820]],
      "drillArchive": {"vdrill": {"best": 1210,
                                  "days": [[20400, 12, 1, 18400, 1210]]}}
    }
  }
}
```

`cases` maps a case id to its progress. In a set learned by family, a
family's card is kept under a family id (`family:` followed by where its
pieces are), not under one of its cases.

**`card`**: the spaced-repetition card.

| Key | Meaning |
| --- | --- |
| `state` | `0` new, `1` learning, `2` review, `3` relearning |
| `step` | The learning or relearning step it is at |
| `ease` | Ease factor (default 2.5) |
| `interval` | Review interval in days |
| `due` | When it is due (Unix seconds) |
| `reps`, `lapses` | Reviews done, and times forgotten |
| `introduced` | When it was first learned or marked learned; `0` never. A card in state 1 with `reps` and `introduced` both 0 is in the learn queue, and its `due` is when it was queued. |
| `marked` | Optional, `true`: marked as learned rather than learned in Learn |
| `suspended` | Optional, `true`: left out of Learn |
| `buriedUntil` | Optional: left out of Learn until then (Unix seconds) |

Files from before `introduced` existed get it worked out from the
attempts.

**`attempts`**: the case's attempts, oldest first, at most 100 per mode.
Each is an array `[timestamp, timeMs, failed, mode, recognitionMs,
executionMs, recognitionOnly]`:

| Index | Meaning |
| --- | --- |
| 0 | When (Unix seconds) |
| 1 | The time in ms (0 for Learn) |
| 2 | `1` failed (a DNF or wrong alg in Drill, Again in Learn), `0` not |
| 3 | `"learn"`, `"vdrill"` (Drill on the virtual cube) or `"drill"` (Drill on a physical cube); a missing mode is a physical drill |
| 4, 5 | Optional: recognition time (from the case being set up to the first turn off the U-D axis, or to Space in a recognition review) and execution time, in ms; 0 when not measured |
| 6 | Optional: `1` for a Learn recognition review (recognised, not solved) |

**`drillArchive`**: optional. Per drill mode (`"vdrill"`, `"drill"`), what
is kept of the attempts that dropped out of the last 100: `best` (the best
time ever, ms) and `days`, one `[day, attempts, fails, sumMs, bestMs]` per
day (`day` is the local date as days since 1970-01-01; the sum and best are
over the attempts that didn't fail).

Which cases are ticked, and the choices made in Learn and Drill for the
set, are in the settings file, not here.

## Timed sessions

`sessions.json`:

```json
{
  "format": "v-cube.sessions", "version": 1, "current": 0,
  "sessions": [
    {"name": "Session 1", "scrambleType": "333", "cube": "virtual",
     "solves": [[10234, 0, "R U F' ...", "", 1762800000,
                 "R U' F ...", [180, 420, 610]]]}
  ]
}
```

`current` is the index of the session shown. Each session has a `name`, a
`scrambleType` (below; default `"333"`), a `cube` (`"virtual"` or
`"physical"`; sessions from before there was a choice are physical), and
`solves`, each an array:

| Index | Meaning |
| --- | --- |
| 0 | The time in ms, without the +2 |
| 1 | Penalty: `0` none, `1` +2, `2` DNF |
| 2 | Scramble |
| 3 | Comment (`""` for none) |
| 4 | When it finished (Unix seconds) |
| 5 | Optional: the solution on the virtual cube (or imported from cstimer) |
| 6 | Optional: ms from the start of the solve to each move of the solution |

Scramble types: `333` (WCA random state), `f2l`, `xcross`, `eoline`,
`eocross`, `lsll`, `ll`, `zbll`, `cll`, `ell`, `pll`, `epll`, `corners`,
`edges`, or `set:<alg set id>` for an alg set's ticked cases.

**cstimer**: the Timed session menu imports cstimer's export files (every
3x3 session with solves; other puzzles' sessions and empty ones are left
out, and come in as physical sessions, with any virtual or smart cube
solutions and their move times) and exports sessions in cstimer's format,
keeping its scramble types where cstimer has the same one (`333`
otherwise) and the solutions. In cstimer, import the file with **Import
session(s) from other timers**, which adds the sessions to yours;
cstimer's "Import from file" would replace all your data.

## Untimed and Move Count sessions

`untimed.json` (`"v-cube.untimed"`) and `movecounts.json`
(`"v-cube.movecount"`) have the same layout:

```json
{
  "format": "v-cube.movecount", "version": 1, "current": 0,
  "sessions": [
    {"name": "EO Cross", "scrambleType": "333", "cube": "virtual",
     "step": "eocross",
     "attempts": [{"scramble": "...", "timestamp": 1762800000, "moves": 7,
                   "solution": "y R' F ...", "comment": "nice"}]}
  ]
}
```

| Session key | Meaning |
| --- | --- |
| `name`, `scrambleType`, `cube` | As for Timed sessions. Move Count accepts only `333`, `eoline`, `eocross` and `f2l`; any other is read as `333`. |
| `step` | Move Count only: `eo`, `eo+1`, `eo+2`, `eoline`, `eocross`, `xeocross`, `zzf2l`, `cross` or `cfopf2l`. Older files without it get the step from the session's name, or else EO Cross (ZZ F2L for an `eocross` scramble type). |
| `attempts` | The solves, oldest first. |

| Attempt key | Meaning |
| --- | --- |
| `scramble`, `timestamp` | The scramble, and when it was recorded |
| `moves` | The move count (HTM), 0 to 999. Left out for a solve with no count (an Untimed physical-cube scramble kept as done). |
| `dnf` | Optional, `true`: the step wasn't done, or the cube wasn't solved |
| `solution` | Optional: the moves on the virtual cube, as done |
| `comment` | Optional |

A new Untimed file starts with one session; a new Move Count file with one
virtual-cube session per step.

## Inspection sessions

`inspection.json` (`"v-cube.inspection"`), with `current` and `sessions`
as above. A session is a Plan session or a Bad edges session:

| Session key | Meaning |
| --- | --- |
| `name` | |
| `drill` | `"plan"` (default) or `"edges"` |
| `step` | Plan: `eo`, `eoline`, `eo+1`, `eo+2` or `eocross` (default and fallback `eocross`) |
| `pair` | Plan, EO Cross only, optional: the pair tracked after the cross, `fr`, `fl`, `bl` or `br` |
| `limit` | Plan, optional: the inspection limit in seconds (up to 3600); none without it |
| `wca` | Plan, optional, `true` with a `limit` of 15: WCA inspection, with the calls at 8 and 12 s |
| `axes` | Bad edges: `"fb"`, `"rl"` or `"both"` (default) |
| `attempts` | Oldest first |

Every attempt has `scramble`, `timestamp`, `ms` (Plan: the inspection time
used; Bad edges: the time to the last answer) and an optional `comment`.

| Plan attempt key | Meaning |
| --- | --- |
| `moves` | Moves done with the cube hidden (HTM); with a pair tracked, those to EO Cross |
| `dnf` | Optional, `true`: the step wasn't done |
| `solution` | Optional: the moves as done |
| `optimal` | Optional: the optimal length in your colour neutrality's holds (either EO axis) |
| `hold` | Optional: the hold the step was done in |
| `pair`, `pairMoves` | Optional, with a pair tracked: whether it was solved, and the moves after EO Cross |

| Bad edges attempt key | Meaning |
| --- | --- |
| `hold` | The hold the cube was shown in |
| `fb`, `rl` | The counts given for the F/B and R/L axes, for the axes asked |
| `fbCorrect`, `rlCorrect` | The right counts |

A hold is two digits, the top and front faces in `URFDLB` order counted
from 0, in the WCA scrambling orientation (U white, R red, F green, D
yellow, L orange, B blue): `02` is white top, green front, and `35` yellow
top, blue front.

A new file starts with EO, EO+2 and EO Cross Plan sessions (WCA inspection)
and a Bad edges session for both axes.

## Blind

`blind.json` (`"v-cube.blind"`):

```json
{
  "format": "v-cube.blind", "version": 1, "current": 0,
  "sessions": [{"name": "Session", "cube": "virtual",
                "attempts": [{"time": 95000, "memo": 41000, "scramble": "...",
                              "timestamp": 1762800000, "dnf": true,
                              "solution": "x2 ...", "comment": ""}]}],
  "quiz": {"edges": {"A": {"attempts": 12, "correct": 11, "ms": 9800}},
           "corners": {}},
  "setups": {"edges": {}, "corners": {}},
  "tracing": [{"time": 52000, "timestamp": 1762800000,
               "edges": true, "corners": false}]
}
```

- `sessions`: blind solve sessions, each with a `name`, a `cube`
  (default `"virtual"`) and `attempts`: `time` (memo and execution, ms),
  `memo` (ms), `scramble`, `timestamp`, and optionally `dnf`, `solution`
  (virtual cube: starting with the rotation from the WCA hold to the blind
  hold) and `comment`.
- `quiz` and `setups`: the letter quiz's and the setup drill's statistics,
  for `edges` and `corners`, by sticker, keyed by the sticker's Speffz
  letter (`A` to `X`) whatever your lettering: `attempts`, `correct` and
  `ms` (the total time of the right answers, each counted up to 30 s).
- `tracing`: the memo traces, oldest first, at most the last 1000: the
  `time` taken, when, and whether the `edges` and `corners` were right.

The lettering, your own setups and the blind hold are in the settings file
under `[blind]`.

## Alg Lab

`alglab.json` (`"v-cube.alglab"`):

```json
{
  "format": "v-cube.alglab", "version": 1, "current": 0,
  "sessions": [{"id": "3fa2c41b09de", "name": "Session 1",
                "cube": "physical", "order": "interleaved", "reps": 12,
                "warmup": 1, "trim": 0, "randomAuf": false,
                "entries": [{"case": "set:pll/T",
                             "alg": "R U R' U' R' F R2 U' R' U' R U R' F'"}]}],
  "cases": [{"id": "1", "name": "Sune", "setup": "R U2 R' U' R U' R'",
             "free": "LL:p", "aufBefore": true, "aufAfter": true}],
  "candidates": [{"case": "lab:1",
                  "algs": [{"alg": "R U R' U R U2 R'",
                            "source": "generated", "added": 1762800000}]}],
  "attempts": [{"case": "set:pll/T", "alg": "...", "cube": "virtual",
                "time": 1830, "session": "3fa2c41b09de",
                "timestamp": 1762800000, "dnf": true, "deviated": true,
                "include": true, "scramble": "...", "moves": "..."}],
  "profiles": [{"name": "Right-handed 2H", "moves": {"R": 1.0, "U'": 1.1},
                "other": 2.5, "axis": 0.15,
                "rules": [{"first": "R2", "second": "F*", "cost": 0.6}]}],
  "moveSets": [{"name": "Mine", "moves": "U, R, F2"}]
}
```

- A case is named by a key: `set:<set id>/<case id>` for a case of an alg
  set, `lab:<id>` for a lab case. Algs are written out plainly (one space
  between moves, `R'` for `R3`), which is how an alg's times find it.
- `sessions`: the test sessions, each with an `id` its attempts keep, its
  options (`cube`, `order` `"interleaved"` or `"blocked"`, `reps`,
  `warmup` reps, `trim` per cent, `randomAuf`) and its `entries`.
- `cases`: the lab cases: a `setup` alg that makes the case from a solved
  cube, the `free` pieces its algs may leave (as in alg sets) and whether
  a U turn may come before (`aufBefore`) and after (`aufAfter`) its algs.
- `candidates`: the algs kept for each case besides its set's own, with
  where they came from (`generated`, `pasted`, `typed`) and when.
- `attempts`: every timed attempt, oldest first, with its case, alg, cube,
  `time`, `session` and `timestamp`; `dnf`, `deviated` (virtual: the moves
  left the alg) and `include` (counted although it left the alg), the
  physical `scramble` and the virtual `moves` are left out when false or
  empty.
- `profiles`: the ergonomic profiles: the cost of each move (`moves`), of
  a move not listed (`other`) and of a turn on another axis (`axis`), and
  the `rules` for pairs of moves. A file without `profiles` gets the
  built-in ones.
- `moveSets`: your own move-set presets, as text (a layer for quarter and
  half turns, `2` for half turns only, `1` for quarter turns only).

Entries, candidates and attempts whose case key can't be read are left
out. The Alg Lab page's own choices (the case shown, the generator's
options, the filters, the profile) are in the settings file under
`[alglab]`.

## Settings

The settings file is an INI file on every platform; Windows doesn't use
the registry. Earlier versions kept them in the platform's native place
(`~/.config/v-cube/v-cube.conf` on Linux, the registry on Windows); those
settings are copied to `v-cube.ini` the first time a newer version runs,
and the old ones are left in place.

Besides what Edit > Settings shows, it remembers the window's size and
place, the lists' widths and columns, and each mode's choices: for each
alg set, under `[trainer]` as `sets\<id>\...`, the cases that are unticked
(`unticked`), Learn's order for new cases and view, and Drill's cube,
cases and order. Edit it by hand only while v-cube is closed.
