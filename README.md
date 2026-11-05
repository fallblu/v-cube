# v-cube

A lightweight Qt desktop app for creating, learning and drilling 3x3 alg
sets, with random-state scrambles and a cstimer-style solve timer. Learn
algs on a virtual cube turned from the keyboard, then drill and time them
on a normal (non-smart) cube with the space-bar timer.

## Features

The app has six modes, listed in a sidebar in two groups (`Ctrl+1` to
`Ctrl+6` switch between them):

| Group | Mode | What it's for |
| --- | --- | --- |
| Algs | **Learn** | Untimed practice on the virtual cube, scheduled by spaced repetition |
| Algs | **Drill** | Timed practice on a physical cube with the space-bar timer |
| Algs | **Library** | Creating and editing alg sets |
| Solves | **Timer** | Timed solves on a physical cube, with sessions and statistics |
| Solves | **Scramble** | Scrambles for casual solving, with no timer |
| Solves | **Virtual** | A virtual-cube sandbox |

- **Learn**: learn the cases of an alg set on the virtual cube.
  - Each case is set up on the cube and you solve it from memory, with no
    timer. The algs stay hidden until you press Tab.
  - Solving the case without a hint grades it Good; a hint or giving up
    grades it Again. Keys 1-4 change the grade afterwards.
  - Cases are scheduled Anki-style: due reviews first, then cases in their
    learning steps (minutes apart), then new cases from your learn queue,
    up to a daily limit. "Start learning" adds the next N ticked cases to
    the queue.
  - When the case is solved you see your algs with the AUFs it needed, e.g.
    `(U') R U R' U' ... (U2)`.
- **Drill**: drill cases on a physical cube.
  - The scrambles are real random-state scrambles. The pieces the step
    doesn't care about (e.g. the permutation when drilling OLL) are
    scrambled too, and there's a random U turn before and after, so you
    can't tell the case from the scramble.
  - Drill your learned cases (the default), all ticked cases, or the cases
    you are still learning, in random order, weighted order (slow and
    failed cases come up more) or list order.
  - After each case you see your time, coloured green at or under the
    set's target time, and the algs with the AUFs this scramble needs.
    Backspace marks a wrong alg.
  - Per-case recent average, best time and fail rate. Drill attempts never
    change the spaced-repetition schedule.
- Learn and Drill share each set's ticked cases and learn queue. Only cases
  with at least one alg that solves them are offered, and both modes show
  how many are skipped. Hold the cube any way you like: choose the top and
  front colours, or let each case pick a random front, or a random top and
  front.
- **Library**: create and edit alg sets.
  - The main view shows the set list, the selected set's case tree and the
    selected case with its diagram and algs. The **Set** menu (also on
    right-click) creates, duplicates, deletes, imports, exports and checks
    sets; **Edit** (also on right-click) adds and renames groups and cases.
  - **Set properties…** (or double-click a set) edits the name,
    description, random U turns around drilled cases, target time, and the
    step settings for the whole set or as a group's own override: what the
    algs may leave unsolved (e.g. "last-layer permutation" for OLL-like
    sets) and how diagrams look (full colour, top colour only or grey, from
    above or in 3D).
  - Each case can have several algs. Drag them to reorder, double-click to
    edit, and right-click to make one the main alg or remove it.
  - Every alg is checked against its case under the step settings in force,
    with a visible result: e.g. `(U') … (U2), 14 moves` for a pass, or
    `leaves 3 corners unsolved (U layer)` or `can't read at character 7`
    for a fail. OLL algs pass whatever last-layer permutation they leave,
    and algs may need a U turn before or after when the set allows it.
    Failing algs are kept with a warning; the case tree marks cases with
    failing or no passing algs, the set list counts failing algs, and
    **Check algs…** lists them all.
  - The main alg is the first passing alg. Each case is drawn, and set up
    in Learn and Drill, from the angle its main alg starts from: a leading
    `y`, `y'`, `y2` or U turn is done to the case, so the rest of the alg
    solves it directly. The stored case and algs don't change.
  - Paste any list of algs: each one is matched to the case it solves (any
    AUF, any cube rotations), so the order doesn't matter. Lines may look
    like `Name: alg1 / alg2`, as in Alg-Trainer.
  - Create a case from an alg, and import and export sets as JSON.
- **Built-in sets**: PLL (21), OLL (57), 2-look OLL (10) and 2-look PLL (6).
  These ship as case definitions only, with no algs, so you add your own.
- **Timer**: hold Space to start (the hold time is configurable) and press
  any key to stop.
  - A small net next to the scramble shows the scrambled cube, white on top
    and green in front.
  - Optional WCA inspection, with +2 and DNF applied automatically.
  - Sessions, with ao5/ao12/ao100, mo3, best, mean, +2/DNF/comments per
    solve, and a trend chart. The session menu switches, adds, renames and
    deletes sessions, and imports and exports cstimer sessions.
- **Scramble**: large scramble text with a net preview and a Copy button.
  Space or → shows the next scramble, Backspace or ← goes back through
  the last 100. Nothing is timed or saved.
- **Scramble types** (Timer, Scramble and Virtual):
  - WCA random state.
  - cstimer-style subsets: F2L/cross solved, XCross, EOLine, EOCross,
    LSLL, LL, ZBLL, CLL, ELL, PLL, EPLL, corners only, edges only.
  - Any of your alg sets, using its ticked cases.
- **Virtual**: turn the virtual cube freely. Space scrambles with the
  chosen scramble type, Esc resets, and the alg field applies a typed alg
  with animation.
- **Virtual cube** (Learn and Virtual):
  - Choose the view in each mode: animated 3D, a flat view of the U and F
    faces with the adjacent L and R stickers (as in Alg-Trainer), an
    unfolded net, or hidden.
  - Turned from the keyboard with cstimer's face keys (Shift for wide
    moves), which you can remap in Settings.

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
| Anywhere | Ctrl+1 … Ctrl+6 | switch to Learn, Drill, Library, Timer, Scramble, Virtual (not during a solve) |
| Learn | turning keys | turn the virtual cube; solving the case finishes it |
| Learn | Tab | show the algs (grades the case Again) |
| Learn | Space | give up (grades the case Again) |
| Learn | Esc | reset the case |
| Learn, after a case | Space / Enter | next case |
| Learn, after a case | 1-4 | grade Again / Hard / Good / Easy |
| Learn, after a case | Esc | reset the cube to practise the case |
| Drill, Timer | hold Space, release | start (after the hold time, when the time turns green) |
| Drill, Timer | any key | stop |
| Drill, Timer | Esc | cancel |
| Drill, after a case | Space | next case |
| Drill, after a case | Backspace | toggle "wrong alg" |
| Drill, after a case | Esc | try the case again |
| Scramble | Space / → | next scramble |
| Scramble | Backspace / ← | previous scramble |
| Virtual | turning keys | turn the cube |
| Virtual | Space | scramble |
| Virtual | Esc | reset to solved |
| Virtual, alg field | Enter / Esc | apply the alg / return the keys to the cube |

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
2. Paste your list. Each alg is matched to its case automatically, and its
   check result is shown next to it.
3. In a case's alg list, drag an alg to the top or right-click it and
   choose **Make main** to pick which alg Learn and Drill show first.

For your own sets, **Set > New set…** asks what the algs solve, then
**Edit > New case from alg…** or **Paste algs…** (with "Create new cases"
ticked) builds the cases. If you change a set's step settings later,
**Set > Check algs…** lists the algs that no longer pass.

### Data

| What | Linux | Windows |
| --- | --- | --- |
| Alg sets (one JSON file each) | `~/.local/share/v-cube/algsets/` | `%LOCALAPPDATA%\v-cube\algsets\` |
| Learn and drill progress per set | `~/.local/share/v-cube/algsets/progress/` | `%LOCALAPPDATA%\v-cube\algsets\progress\` |
| Timer sessions | `~/.local/share/v-cube/sessions.json` | `%LOCALAPPDATA%\v-cube\sessions.json` |
| Settings (theme, colours, keys, mode choices) | `~/.config/v-cube/v-cube.ini` | `%APPDATA%\v-cube\v-cube.ini` |

Settings are an INI file on every platform; Windows doesn't use the
registry. Earlier versions kept them in `~/.config/v-cube/v-cube.conf`.
Those settings are copied to `v-cube.ini` the first time a newer version
runs, and the old file is left in place.

**File > Open data folder** opens the data folder in your file manager
(Windows Explorer under WSL, through `wslpath` and `explorer.exe`). If no
file manager can be started, it shows the data folder and config file paths
with buttons to copy them.

Built-in sets are copied into the alg-set folder on first run, and are
then yours to edit. **Set > Restore built-in cases** reloads the shipped
cases and keeps your algs.

Alg-set files look like this (states are 54-character facelet strings in
URFDLB order; `free` lists what the step may leave unsolved, e.g. `LL:p` =
last-layer permutation):

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
| `src/train` | Pure C++ training logic. Alg sets, AUF-aware alg-to-case matching and verification, display orientation, drill states, paste import, timer state machine, WCA statistics, SRS scheduler, Learn and Drill case picking. |
| `src/io` | Qt Core persistence. JSON alg sets, the user library, progress, sessions and cstimer conversion. |
| `src/app` | Qt Widgets UI. Cube renderer, the six mode pages, the shared scramble source and settings. |
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
