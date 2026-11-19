# Data and file formats

v-cube saves as you go; there is no Save command. This page says where the
files are, how they are protected, and what is in them.

## Where the data is

Data is in `~/.local/share/v-cube/` on Linux (following `XDG_DATA_HOME`)
and `%LOCALAPPDATA%\v-cube\` on Windows.

| File or folder | Contents |
| --- | --- |
| `algsets/` | Alg sets, one JSON file each |
| `algsets/progress/` | Learn and Drill progress, one file per set |
| `algsets/deleted/` | Deleted alg sets and their progress |
| `deleted/` | Practice files from before the modes had their tabs |
| `sessions.json` | Timed sessions |
| `untimed.json` | Untimed sessions |
| `movecounts.json` | Move Count sessions |
| `eocross.json` | EO Cross: Learn solves and timed attempts |
| `f2l.json` | F2L: cards, Learn solves and timed attempts |
| `tsle.json` | TSLE: cards, Learn solves, timed attempts and OCLL algs |
| `alglab.json` | Alg Lab: lab cases, candidates, test sessions, times, profiles |
| `backups/` | Daily backups |
| `v-cube.lock` | Marks the running copy, so a second one hands over to it |

Settings (theme, colours, keys, and per-set and per-mode choices) are in
`~/.config/v-cube/v-cube.ini` on Linux (following `XDG_CONFIG_HOME`) and
`%APPDATA%\v-cube\v-cube.ini` on Windows.

**File > Open data folder** opens the data folder in your file manager
(Windows Explorer under WSL). If none can be started, it shows both paths
with buttons to copy them.

## Keeping the data safe

- **Atomic saves**: every file is written to a temporary file and renamed
  over the old one, so a crash mid-save leaves the old file whole.
- **A file that can't be read** (cut short, not JSON, the wrong kind of
  file) is never saved over. It is renamed to
  `<name>.unreadable-<yyyyMMdd>-<HHmmss>`, the app says so at startup, and
  that data starts afresh. If it can't be renamed, it is left in place and
  nothing is saved over it.
- **A file from a newer version** of v-cube (a higher `version` than this
  one reads) is left alone and never saved over. That data starts afresh
  until you update.
- **An alg set with a problem** is left out, and the app names the problem
  at startup. Problems are: no `id`, a `free` or `targetSeconds` that can't
  be read, a case state no real cube has, a missing or repeated case id, or
  an alg that isn't text. If two files hold the same set id, the one named
  after the id is loaded and the other is left alone.
- **A save that fails** (a full disk, say) shows a red banner naming the
  file until a later save works or you dismiss it.
- **Daily backups**: the first start of each day copies the data folder
  into `backups/<yyyy-MM-dd>/`, without the backups, the deleted sets and
  the lock file, and keeps the newest 14 days. The settings file isn't
  backed up.
- **Deleted alg sets**: deleting a set moves its file to
  `algsets/deleted/<id>.<yyyyMMdd-hhmmss>.json` and its progress to
  `algsets/deleted/progress/`. To bring one back, move the files back
  (named `<id>.json`) while v-cube is closed. In the app, Ctrl+Z undoes the
  delete while v-cube is open.
- **Export all data** (File menu) copies the data folder (as a backup
  does) into `v-cube-export-<date>/data/` and the settings file into
  `v-cube-export-<date>/settings/`, in a folder you choose. To move to
  another computer, copy both into place there while v-cube is closed.

## Format and version

Every data file is a JSON object with a `format` and a `version`:

```json
{"format": "v-cube.sessions", "version": 1, ...}
```

| File | `format` | Version |
| --- | --- | --- |
| Alg set | `v-cube.algset` | 1 |
| Progress | `v-cube.progress` (may be missing) | 1 |
| Timed sessions | `v-cube.sessions` (may be missing) | 1 |
| Untimed sessions | `v-cube.untimed` | 1 |
| Move Count sessions | `v-cube.movecount` | 1 |
| EO Cross, F2L, TSLE | `v-cube.practice` | 1 |
| Alg Lab | `v-cube.alglab` | 2 (added grip profiles; a version 1 file gets the built-in one) |

A file with another `format` can't be read. A missing `version` is read as
1. Unknown keys are ignored and missing keys take the defaults below.

Times are in milliseconds and timestamps in Unix seconds unless said
otherwise. Scrambles, algs and solutions are move text (`R U R' U'`, `r`,
`M2`, `x`).

## Alg sets

`algsets/<id>.json`, one file per set. Built-in sets are copied in on first
run and are then yours to edit. `algsets/installed-builtins.json` lists the
ones copied, so a built-in set you deleted isn't copied in again.

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
A token that can't be read leaves the set out.

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

`algsets/progress/<id>.json`, one per set.

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

A card without `introduced` gets it worked out from its attempts.

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
`"physical"`; physical if missing), and `solves`, each an array:

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

**cstimer**: the Timed session menu imports cstimer's export files. Every
3x3 session with solves comes in as a physical session, with any solutions
and their move times. The export keeps the solutions and the scramble
types cstimer shares (`lsll` becomes its `lsll2`); `xcross`, `epll` and
alg-set types become `333`.

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
| `step` | Move Count only: `eo`, `eo+1`, `eo+2`, `eoline`, `eocross`, `xeocross`, `zzf2l`, `cross` or `cfopf2l`. If missing, the step is taken from the session's name, or else is EO Cross (ZZ F2L for an `eocross` scramble type). |
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

## EO Cross, F2L and TSLE

`eocross.json`, `f2l.json` and `tsle.json` (all `"v-cube.practice"`) hold
what each of the three practice modes keeps:

```json
{
  "format": "v-cube.practice", "version": 1,
  "cards": {"cases": {"front/URF1-UB0": {"card": {"state": 2, "due": 1794086400},
                                         "attempts": [[1794000000, 0, 0, "learn"]]}}},
  "choices": {"front/URF1-UB0": ["U R U' R'", "U2 R U R' U R U' R'"]},
  "learn": {"whole": [{"timestamp": 1794000200, "moves": 31, "best": 24,
                       "extra": 4.5, "hint": true}]},
  "inspection": {"eocross": [{"timestamp": 1794000300, "inspect": 16200,
                              "over": true, "moves": 8, "best": 7, "extra": 1}]},
  "timed": {"virtual/zz-1": [{"timestamp": 1794000400, "ms": 14200,
                              "moves": 31, "best": 24, "extra": 4.5}],
            "physical/zz-1": [{"timestamp": 1794000500, "ms": 15900,
                               "dnf": true}]},
  "notes": {"ocll/antisune": "L' U' L U' L' U2 L",
            "sheet": "deleted/zz-f2l.20261116-092557.json",
            "sheet/front/URF1-UB0": "U R U' R'"}
}
```

- `cards`: the Learn tab's cards, written as an alg set's
  [progress](#progress) is. The key is the case: in F2L a pair case for
  the front slots or the back ones (`front/URF1-UB0`: the side, then where
  the corner and edge are, seen for the front-right slot, with the twist
  and flip), in TSLE a TSLE case (`UF-CW-0120`). EO Cross has none. A case
  never started is put in the learn queue when the mode first asks for a
  card. F2L cards of before, keyed by the case alone, are dropped.
- `choices`: F2L's solutions chosen for each card, written for the side's
  own slot (FR for `front/`, BR for `back/`) and done mirrored in the
  other. Kept when the cards are cleared.
- `learn`: the Untimed tab's solves, by group, the last 200 of each:
  `whole` in F2L and TSLE, the step (`eo`, `eoline`, `eo+1`, `eo+2`,
  `eocross`) in EO Cross.
- `inspection`: EO Cross's Inspection tries by step, the last 200 of each.
- `timed`: timed attempts, the last 200 of each group, by cube and group:
  `virtual/` or `physical/`, then the step (EO Cross), the method (F2L:
  `zz-1`, `zz`, `cfop`, `cfop-1`) or the case the solve started from
  (TSLE).
- `notes`: TSLE's OCLL algs changed from the default ones, as
  `ocll/<id>` (an alg that doesn't solve its OCLL is ignored). In F2L,
  `sheet` says where the algs of your set of pair cases were read from
  (once, from the library or the newest deleted copy of the set), and
  `sheet/<card>` holds each card's alg, offered ticked when its solutions
  are chosen.

A try has `timestamp` and, left out when they don't apply: `ms` (timed
attempts), `moves` (HTM; none on a physical cube), `best` (the best
solution's moves), `extra` (what the solve was over the best: moves, or in
F2L fingertrick cost; left out when it isn't known), `dnf` (given up, or
stopped with the step not done), `hint`, `inspect` (Inspection: the time
looked before the first face turn) and `over` (that went over the limit).

Before the modes had their tabs they kept other files under the same
names (`"v-cube.eocross"`, `"v-cube.f2l"`, `"v-cube.tsle"`). Such a file
is moved to `deleted/` when the mode starts; only TSLE's OCLL algs are
carried over.

The modes' choices are in the settings file under `[eocross]`, `[f2l]` and
`[tsle]`: `tab` (`learn`, `inspection`, `untimed` or `timed`) and
`physical` in each, and the mode's own options (`step` and
`inspectionLimit` in seconds for EO Cross; `method` and `leave` for F2L;
`slots` for TSLE).

## Alg Lab

`alglab.json` (`"v-cube.alglab"`):

```json
{
  "format": "v-cube.alglab", "version": 2, "current": 0,
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
  "profiles": [{"name": "Two-handed grips", "moves": {}, "other": 2.5,
                "axis": 0, "rules": [],
                "grips": {"other": 2.5, "regrip": 1.2, "hiddenRegrip": 0.4,
                          "regripStep": 0.3, "strain": 0.5, "start": 0.25,
                          "sameFinger": 0.4,
                          "tricks": [{"move": "U", "hand": "right",
                                      "finger": "index", "how": "flick",
                                      "from": "home, up", "turn": 0,
                                      "cost": 1.0},
                                     {"move": "y", "hand": "both",
                                      "finger": "hands", "how": "regrip",
                                      "from": "any", "turn": 0, "cost": 1.5,
                                      "reset": true, "off": true}]}},
               {"name": "Right-handed 2H", "moves": {"R": 1.0, "U'": 1.1},
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
- `profiles`: the score profiles. A file without any gets the built-in
  ones. A move profile has the cost of each move (`moves`), of a move not
  listed (`other`) and of a turn on another axis (`axis`), and `rules` for
  pairs of moves. A grip profile also has `grips`, which it scores with
  instead (below).
- `moveSets`: your own move-set presets, as text (a layer for quarter and
  half turns, `2` for half turns only, `1` for quarter turns only).

`grips` has these costs:

| Key | Cost of |
| --- | --- |
| `regrip` | a regrip |
| `hiddenRegrip` | a regrip while the other hand turns |
| `regripStep` | each regrip step past the first |
| `strain` | each move ending with a hand far up or down |
| `start` | each step from home of the starting grips |
| `sameFinger` | a finger turning two moves in a row |
| `other` | a move no trick turns |

and its `tricks`, each with:

| Key | Meaning |
| --- | --- |
| `move` | the move it turns, as written (`R2`) |
| `hand` | `right`, `left`, `both` or `either` |
| `finger` | `thumb`, `index`, `middle`, `ring`, `wrist` or `hands` |
| `how` | shown with them (`flick`, `push`) |
| `from` | the grips it starts from: `far down`, `down`, `home`, `up`, `far up`, comma-separated, or `any` |
| `turn` | the steps its hand's thumb moves (R is 1) |
| `cost` | its cost |
| `reset` | optional, `true`: both hands end at home (rotations) |
| `off` | optional, `true`: never chosen |

A trick that can't be read is left out.

Entries, candidates and attempts whose case key can't be read are left
out. The Alg Lab page's own choices (the case shown, the generator's
options, the filters, the profile, as `scoreProfile`) are in the settings
file under `[alglab]`.

## Settings

`v-cube.ini` is an INI file on every platform; Windows doesn't use the
registry. Settings kept in the platform's native place by an early version
(`~/.config/v-cube/v-cube.conf`, or the registry) are copied to
`v-cube.ini` on first run and left in place.

Besides what Edit > Settings… shows, the file remembers the window's size
and place, list widths and columns, and each mode's choices. Per alg set,
under `[trainer]` as `sets\<id>\...`, it has the unticked cases
(`unticked`), Learn's order for new cases and view, and Drill's cube, cases
and order. Edit it only while v-cube is closed.
