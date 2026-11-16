# v-cube

A lightweight Qt desktop app for creating, learning and drilling 3x3 alg
sets, with random-state scrambles and a cstimer-style solve timer. Learn
algs on a virtual cube turned from the keyboard, then drill and time them
on the virtual cube or on a normal (non-smart) cube with the space-bar
timer. Solves can be timed, untimed or move-counted, on the virtual cube or
a physical one, and reviewed against the optimal solutions and your own F2L
algs. Practice modes train ZZ inspection planning and Old Pochmann
blindfolded solving.

## Modes

The sidebar lists nine modes in three groups (`Ctrl+1` to `Ctrl+9`, in the
sidebar's order), under
a **Today** view (`Ctrl+0`) that shows each alg set's reviews due and new
cases left, your practice streak, and what you did in each mode today and
over the last 7 days.

| Group | Mode | What it's for |
| --- | --- | --- |
| Algs | **Learn** | Untimed practice on the virtual cube, scheduled by spaced repetition |
| Algs | **Drill** | Timed practice on the virtual cube or a physical one with the space-bar timer |
| Algs | **Library** | Creating and editing alg sets, and filing pasted algs under their cases |
| Algs | **Alg Lab** | Finding algs with a generator, comparing them by move count and an ergonomic score, showing the fingertricks and regrips to turn each one with, and timing each alg on a physical or virtual cube |
| Solves | **Timed** | Timed solves, with sessions, statistics and cstimer import and export |
| Solves | **Untimed** | Solves with no timer, with their move counts |
| Solves | **Move Count** | Move-efficiency practice for one step (EO to ZZ F2L, the cross, CFOP F2L) |
| Practice | **F2L** | The F2L a part at a time (ZZ F2L - 1 for ZZ-CT, ZZ F2L, CFOP): solving a pair well, choosing the pair to do next, whole F2Ls, and a browser of the pair cases, each compared with solutions searched for the state on the cube |
| Practice | **TSLE** | ZZ-CT's last-slot step a trigger at a time: every good trigger of each of the 104 cases, the best one for the AUF at hand, whole TSLEs, and a guided introduction of the cases |
| Practice | **Inspection** | Planning EO to EO Cross in inspection and doing it blind, and counting bad edges |
| Practice | **Blind** | Old Pochmann: letters, memo tracing, setup moves and blind solves |

Some of what runs through every mode:

- The virtual cube is turned with cstimer's keyboard layout (which you can
  remap). It is shown in 3D, as a flat Alg-Trainer-style view or as a net.
- Colour neutrality: pick the holds you solve in, and every scramble and
  case is set up for one of them.
- Alg sets ship as case definitions only (PLL, OLL, 2-look OLL, 2-look PLL),
  so you add your own algs, or make your own sets. Every alg is checked
  against its case.
- `F1` shows the keyboard controls for the current mode beside the page,
  and `Ctrl+K` opens a command list that finds any menu command or choice.
- Your data is saved as you go, backed up daily, and can be exported in one
  go.

## Install and build

A tagged release builds a Linux AppImage and a Windows zip (see
[Releases](docs/building.md#releases)); until there is one, build from
source. On Linux:

```bash
sudo apt install build-essential cmake ninja-build qt6-base-dev catch2
cmake --preset release
cmake --build --preset release
./build/release/src/app/v-cube
```

To run the tests, build the debug preset first, since a test preset runs
the tests of its own build:

```bash
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

[docs/building.md](docs/building.md) covers the other presets and options,
Windows with MSVC, WSL, packaging and CI.

## Documentation

- [User guide](docs/user-guide.md): what each mode does and how to use it.
- [Keys](docs/keys.md): every keyboard control, mode by mode. The same
  controls are in the app under `F1`.
- [Data and file formats](docs/file-formats.md): where your data is kept,
  backups, and the JSON formats of alg sets, progress and sessions.
- [Building](docs/building.md): presets, build options, Windows, packaging,
  CI and the source layout.

## Credits

Inspired by [cstimer](https://github.com/cs0x7f/cstimer) (timer, subset
scrambles and keyboard layout) and
[Alg-Trainer](https://github.com/tao-yu/Alg-Trainer) (case drilling with
real scrambles, and the layout of the flat cube view). No code from either
is included. The scrambles use Herbert Kociemba's two-phase algorithm.

## License

MIT; see [LICENSE](LICENSE).
