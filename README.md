# v-cube

A lightweight Qt desktop app for creating, learning and drilling 3x3 alg
sets, with random-state scrambles and a cstimer-style solve timer. Every
feature works with a normal (non-smart) cube via the space-bar timer, or
with the built-in virtual cube turned from the keyboard.

## Features

- **Trainer**: drill the cases of an alg set.
  - The scrambles are real random-state scrambles. The pieces the step
    doesn't care about (e.g. the permutation when drilling OLL) are
    scrambled too, and there's a random U turn before and after, so you
    can't tell the case from the scramble.
  - Choose the order: random, weighted (slow and failed cases come up more),
    recap (each case once), in order, or spaced repetition (Anki-style:
    learning steps of minutes, then reviews days apart, with a limit on new
    cases per day).
  - Each attempt is graded from your time against the set's target time;
    Backspace marks a wrong alg and 1-4 override the grade.
  - After each case you see your algs with the AUFs it needed, e.g.
    `(U') R U R' U' ... (U2)`.
  - Per-case status (new, learning, learned), recent average, best time
    and due date. "Start learning" adds the next N cases to your learn
    queue, and the Drill filter can restrict drilling to those.
  - Hold the cube any way you like: choose the top and front colours, or
    let each case pick a random front, or a random top and front.
- **Library**: create and edit alg sets.
  - Organise each set into groups of cases. Each case can have several
    algs, with the first one as the main alg; you can swap, reorder and
    edit them.
  - Paste any list of algs: each one is matched to the case it solves (any
    AUF, any cube rotations), so the order doesn't matter. Lines may look
    like `Name: alg1 / alg2`, as in Alg-Trainer.
  - Create a case from an alg, and set what a step may leave unsolved (e.g.
    "last-layer permutation" for OLL-like sets). Diagrams can show full
    colour, the top colour only, or grey, from above or in 3D.
  - Import and export sets as JSON.
- **Built-in sets**: PLL (21), OLL (57), 2-look OLL (10) and 2-look PLL (6).
  These ship as case definitions only, with no algs, so you add your own.
- **Timer**: hold Space to start (the hold time is configurable) and press
  any key to stop.
  - Optional WCA inspection, with +2 and DNF applied automatically.
  - Sessions, with ao5/ao12/ao100, mo3, best, mean, +2/DNF/comments per
    solve, and a trend chart.
  - Import and export cstimer sessions.
- **Scrambles**:
  - WCA random state.
  - cstimer-style subsets: F2L/cross solved, XCross, EOLine, EOCross,
    LSLL, LL, ZBLL, CLL, ELL, PLL, EPLL, corners only, edges only.
  - Any of your alg sets, using the trainer's ticked cases.
- **Virtual cube**:
  - Animated 3D view; click it to switch to a flat net or to hide it.
  - Turned with cstimer's keyboard layout, which you can remap in Settings.
  - Timer mode: Space scrambles, the first turn starts the timer and solving
    stops it.
  - Trainer mode: the cube starts on the case, and the timer stops as soon
    as the step is done.

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

- Visual Studio 2022 or its Build Tools, with the **Desktop development
  with C++** workload (MSVC, the Windows SDK, CMake and Ninja).
  A standalone CMake 3.25+ and Ninja also work.
- Qt 6.5 or later for MSVC (`msvc2022_64`) from the
  [Qt online installer](https://www.qt.io/download-qt-installer).
- Git, used to fetch Catch2 for the tests.

Open an **x64 Native Tools Command Prompt for VS 2022**, point `QTDIR` at
the Qt install, then configure, build, test and deploy:

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
| Timer, real cube | hold Space, release | start (after the hold time, when the time turns green) |
| Timer, real cube | any key | stop |
| Timer, real cube | Esc | cancel |
| Timer, virtual cube | Space | scramble the virtual cube |
| Timer, virtual cube | first turn / solved | start / stop |
| Timer, virtual cube | Esc | abort (DNF if timing) |
| Trainer, real cube | Space (hold, release) | start; any key stops |
| Trainer, virtual cube | first turn | starts; stops when the step is done |
| Trainer, virtual cube | Space | give up (shows the algs, resets the case) |
| Trainer | Tab | show the algs before solving (counts as a fail) |
| Trainer, after a case | Space / Enter | next case |
| Trainer, after a case | Backspace | toggle "wrong alg" |
| Trainer, after a case | 1-4 | grade Again / Hard / Good / Easy |
| Trainer, after a case | Esc | reset the case to try it again |

Default turning keys (cstimer / Ryan Heise layout):

| Move | Key | Move | Key | Move | Key |
| --- | --- | --- | --- | --- | --- |
| R R' | I K | U U' | J F | F F' | H G |
| L L' | D E | D D' | S L | B B' | W O |
| r r' | U M | l l' | V R | M' | . X |
| M | 5 6 | E E' | 2 9 | S S' | 0 1 |
| x | T Y | x' | B N | y y' | ; A |
| z z' | P Q | u u' | , C | d d' | Z / |

### Getting algs into a set

1. In the **Library** tab, pick a set (e.g. PLL) and click **Paste algs…**.
2. Paste your list. Each alg is matched to its case automatically. An alg
   that doesn't solve the case is shown in red with a warning.
3. In a case's alg list, **Make main** picks which alg the trainer shows
   first.

For your own sets, **New…** asks what the algs solve, then **New case from
alg…** or **Paste algs…** (with "Create new cases" ticked) builds the cases.

### Data

| What | Linux | Windows |
| --- | --- | --- |
| Alg sets (one JSON file each) | `~/.local/share/v-cube/algsets/` | `%LOCALAPPDATA%\v-cube\algsets\` |
| Drill progress per set | `~/.local/share/v-cube/algsets/progress/` | `%LOCALAPPDATA%\v-cube\algsets\progress\` |
| Timer sessions | `~/.local/share/v-cube/sessions.json` | `%LOCALAPPDATA%\v-cube\sessions.json` |
| Settings (theme, colours, keys, trainer choices) | `~/.config/v-cube/v-cube.ini` | `%APPDATA%\v-cube\v-cube.ini` |

Settings are an INI file on every platform; Windows doesn't use the
registry. Earlier versions kept them in `~/.config/v-cube/v-cube.conf`.
Those settings are copied to `v-cube.ini` the first time a newer version
runs, and the old file is left in place.

**File > Open data folder** opens the data folder in your file manager
(Windows Explorer under WSL, through `wslpath` and `explorer.exe`). If no
file manager can be started, it shows the data folder and config file paths
with buttons to copy them.

Built-in sets are copied into the alg-set folder on first run, and are
then yours to edit. **Restore built-in cases** reloads the shipped cases
and keeps your algs.

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
| `src/train` | Pure C++ training logic. Alg sets, AUF-aware alg-to-case matching, drill states, paste import, timer state machine, WCA statistics, SRS scheduler and case picker. |
| `src/io` | Qt Core persistence. JSON alg sets, the user library, progress, sessions and cstimer conversion. |
| `src/app` | Qt Widgets UI. Cube renderer, timer, trainer, library and settings. |
| `tests` | Catch2 tests for the engine, training logic and storage. They also check that every built-in case is distinct and that standard algs are matched to the right cases. |

## Credits

Inspired by [cstimer](https://github.com/cs0x7f/cstimer) (timer, subset
scrambles and keyboard layout) and
[Alg-Trainer](https://github.com/tao-yu/Alg-Trainer) (case drilling with
real scrambles). No code from either is included. The scrambles use Herbert
Kociemba's two-phase algorithm.

## License

MIT; see [LICENSE](LICENSE).
