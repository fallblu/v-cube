# v-cube

A Qt desktop app for learning and drilling 3x3 algs, with random-state
scrambles and a cstimer-style timer. Algs are learned on a virtual cube
turned from the keyboard, then drilled and timed on the virtual cube or a
physical one. The practice modes are built around ZZ.

## Modes

| Group | Mode | What it's for |
| --- | --- | --- |
| | **Today** | Reviews due, new cases left, your streak, and what you did in each mode |
| Algs | **Learn** | Untimed practice on the virtual cube, scheduled by spaced repetition |
| Algs | **Drill** | Timed practice on the virtual cube or a physical one |
| Algs | **Library** | Creating and editing alg sets, and filing pasted algs under their cases |
| Algs | **Alg Lab** | Generating algs for a case, comparing them by move count and fingertrick cost, and timing them against each other |
| Solves | **Timed** | Timed solves with sessions, statistics, and cstimer import and export |
| Solves | **Untimed** | Solves with no timer, with their move counts |
| Solves | **Move Count** | Move efficiency for one step (EO to ZZ F2L, the cross, CFOP F2L) against the optimal |
| Practice | **EO Cross** | The start of a ZZ solve, EO to EO Cross, from real scrambles: untimed against the optimal solutions (in view or planned and done blind), and timed |
| Practice | **F2L** | Pair cases learned with spaced repetition against solutions searched for the state on the cube, and whole F2Ls untimed and timed |
| Practice | **TSLE** | ZZ-CT's last-slot step: every good trigger for each case, learned with spaced repetition, and whole TSLEs untimed and timed |

`Ctrl+0` to `Ctrl+9` and `Ctrl+-` switch modes in that order.

In every mode:

- The virtual cube uses cstimer's keyboard layout, which you can remap. It
  is drawn in 3D, as a flat Alg-Trainer-style view, or as a net.
- Colour neutrality: pick the holds you solve in, and every scramble and
  case is set up for one of them.
- The built-in alg sets (PLL, OLL, 2-look OLL, 2-look PLL) are case
  definitions only. You add your own algs or make your own sets, and every
  alg is checked against its case.
- `F1` shows the current mode's keys and `Ctrl+K` finds any command.
- Data is saved as you go and backed up daily.

## Install and build

There is no release yet, so build from source. On Linux:

```bash
sudo apt install build-essential cmake ninja-build qt6-base-dev catch2
cmake --preset release
cmake --build --preset release
./build/release/src/app/v-cube
```

To run the tests:

```bash
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

[docs/building.md](docs/building.md) covers Windows, WSL, build options,
packaging and CI.

## Documentation

- [User guide](docs/user-guide.md): what each mode does.
- [Keys](docs/keys.md): every keyboard control.
- [Data and file formats](docs/file-formats.md): where data is kept,
  backups, and the JSON formats.
- [Building](docs/building.md)

## Credits

Inspired by [cstimer](https://github.com/cs0x7f/cstimer) (timer, subset
scrambles, keyboard layout) and
[Alg-Trainer](https://github.com/tao-yu/Alg-Trainer) (case drilling with
real scrambles, the flat cube view). No code from either is included.
Scrambles use Herbert Kociemba's two-phase algorithm.

## License

MIT; see [LICENSE](LICENSE).
