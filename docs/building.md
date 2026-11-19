# Building v-cube

C++23 and Qt 6 Widgets, built with CMake and Ninja. Developed on Ubuntu
26.04 under WSL2 (g++ 15, Qt 6.10); also builds natively on Windows with
MSVC.

## Requirements

- GCC 13+, Clang 18+ or MSVC 2022+.
- CMake 3.25+ and Ninja.
- Qt 6.5+: Core, Gui, Widgets, Concurrent and Network (all in
  `qt6-base-dev` on Debian and Ubuntu), plus Test for the GUI test. The
  libraries and unit tests need only Core.
- Optional: Qt Multimedia (`qt6-multimedia-dev`) for the inspection sounds.
  Without it they are the system beep.
- Catch2 3 for the tests. If CMake doesn't find an installed copy, it
  fetches v3.7.1 with git at configure time.

## Linux

```bash
sudo apt install build-essential cmake ninja-build qt6-base-dev catch2
cmake --preset release
cmake --build --preset release
./build/release/src/app/v-cube
```

`cmake --install build/release --prefix ~/.local` installs the app with its
desktop entry, icons and docs.

## Presets

Each preset builds into `build/<preset>` and has a build preset and a test
preset of the same name.

| Preset | Build type | Notes |
| --- | --- | --- |
| `debug` | Debug | |
| `release` | Release | |
| `asan` | Debug | AddressSanitizer and UBSan (which stops at the first error) |
| `windows-debug` | Debug | MSVC, Windows hosts only; installs into `install/windows-debug` |
| `windows-release` | Release | MSVC, Windows hosts only; installs into `install/windows-release` |

## Tests

A test preset runs the tests of its own build, so build it first:

```bash
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

There are two test programs:

- `vcube_tests` (Catch2): the cube engine, the training logic and the
  storage. Needs Qt Core but no display.
- `vcube_gui_tests` (Qt Test, `tests/test_gui.cpp`): drives the main window
  on the offscreen platform with temporary settings and data folders. It
  also checks that `docs/keys.md` matches the key table in
  `src/app/help_text.cpp`; run it once with `VCUBE_WRITE_DOCS=1` to rewrite
  the file after changing the table.

Under `asan`, LeakSanitizer uses `tests/lsan.supp` for a leak inside Qt.

## Build options

Pass these when configuring, e.g.
`cmake --preset debug -DVCUBE_USE_CCACHE=ON`.

| Option | Default | What it does |
| --- | --- | --- |
| `VCUBE_BUILD_APP` | `ON` | Build the app. `OFF` builds only the libraries and unit tests. |
| `VCUBE_BUILD_TESTS` | `ON` | Build the tests. |
| `VCUBE_WARNINGS_AS_ERRORS` | `OFF` | `-Werror` or `/WX`. CI and release builds turn it on. |
| `VCUBE_SANITIZE` | `OFF` | AddressSanitizer and UBSan (GCC and Clang only). |
| `VCUBE_USE_CCACHE` | `OFF` | Compile through ccache. |
| `VCUBE_USE_MOLD` | `OFF` | Link with mold (CMake 3.29+). |

Everything builds with `-Wall -Wextra -Wpedantic -Wshadow` (`/W4
/permissive-` on MSVC), and `src/cube` and `src/train` add `-Wconversion
-Wsign-conversion`. A build should have no warnings.

## Formatting

CI checks every `.cpp` and `.hpp` against `.clang-format` with clang-format
21.1.8. Other versions format some constructs differently.

```bash
pipx install clang-format==21.1.8
git ls-files -z '*.cpp' '*.hpp' | xargs -0 clang-format --dry-run --Werror
```

## Windows

You need:

- Visual Studio 2022 or its Build Tools with the **Desktop development with
  C++** workload, including a Windows SDK (for `rc.exe` and `mt.exe`;
  without them CMake fails with "is not able to compile a simple test
  program"). The workload includes CMake and Ninja.
- Qt 6.5+ for MSVC (`msvc2022_64`) from the
  [Qt online installer](https://www.qt.io/download-qt-installer).
- Git, to fetch Catch2 unless one is on `CMAKE_PREFIX_PATH`.

In an **x64 Native Tools Command Prompt**, set `QTDIR` to the Qt install
and build:

```bat
set QTDIR=C:\Qt\6.10.0\msvc2022_64
cmake --preset windows-release
cmake --build --preset windows-release
ctest --preset windows-release
cmake --install build\windows-release
install\windows-release\bin\v-cube.exe
```

`cmake --install` runs `windeployqt` and copies the MSVC runtime, so
`install\windows-release\bin` runs on a PC without Qt. The tests get the Qt
DLLs copied next to them; the `v-cube.exe` in `build\` needs Qt's `bin`
folder on `PATH`.

### Checking the UI on Windows

The GUI tests and the layout checks run offscreen on Linux, which can't
show Windows' own font (Segoe UI) or its display scaling. Before a release,
run `install\windows-release\bin\v-cube.exe` on Windows and check, at
100%, 125% and 150% scaling (Settings > System > Display > Scale; sign out
and in again if Windows asks):

- The window opens on the screen and fits it, at its default size and
  maximised; the sidebar's names line up on the left with the due counts
  on the right.
- Today, Learn, Drill, Library, Alg Lab (both tabs), Timed and the three
  practice modes: no text is cut off or overlaps, option rows wrap rather
  than push the window wider, and the time and cube keep their room.
- The timer's hold: hold Space in Timed until it turns green and let go;
  the time shows rings on each side while held and filled dots once
  ready, and the colours read as red and green.
- The dialogs: Settings (every tab), the command list (Ctrl+K), the solve
  review (Alt+R), a case's Learn history and Drill history (Library >
  History), Alg Lab's grip profile (Profile… > Edit this profile…) and
  Paste algs.
- Dark and light theme (Settings > General), and the text size settings
  at their largest.

## WSL

Under WSL the app uses Qt's X11 (`xcb`) platform plugin instead of Wayland,
because WSLg shows every Wayland surface as a separate Windows window:
closed menus stay on screen and the window position isn't restored. If
`xcb` can't load, it falls back to Wayland. `QT_QPA_PLATFORM` overrides
the choice:

```bash
QT_QPA_PLATFORM=wayland ./build/release/src/app/v-cube
```

**Help > About v-cube** shows the plugin in use.

## Versions and packaging

The version in **Help > About**, the package names and the Windows file
version come from `git describe --tags --always --dirty`: `0.2.0` on the
tag `v0.2.0`, `0.2.0-3-gabc1234` three commits later, and `0.1.0-gabc1234`
before the first tag. A source tree without git uses the version in
`CMakeLists.txt`.

`cpack`, run in a build folder, packs the install tree: a zip of the
self-contained folder on Windows, a tarball elsewhere. The Linux AppImage
is made from the install tree with linuxdeploy:

```bash
cmake --install build/release --prefix AppDir/usr
./linuxdeploy-x86_64.AppImage --appdir AppDir --plugin qt --output appimage \
  --desktop-file AppDir/usr/share/applications/v-cube.desktop \
  --icon-file AppDir/usr/share/icons/hicolor/256x256/apps/v-cube.png
```

### Releases

Pushing a tag such as `v0.2.0` runs `.github/workflows/release.yml`, which
builds and tests the AppImage and the Windows zip (Qt 6.8, warnings as
errors) and attaches them to a GitHub release. Run by hand, it builds both
without publishing. The AppImage has the xcb and Wayland platform plugins,
and linuxdeploy is pinned to a release (`LINUXDEPLOY_TAG` and
`LINUXDEPLOY_QT_TAG` in the workflow).

## CI

`.github/workflows/ci.yml` runs Format and Linux GCC on pushes to `main`
and on pull requests, and every job on a manual run and weekly when `main`
has changed. A change to docs only (Markdown, `docs/`, `LICENSE`) skips it,
except one to `docs/keys.md`, which the GUI test checks.

| Job | What it does |
| --- | --- |
| Format | clang-format 21.1.8 |
| Linux GCC | g++ 14, Qt 6.8.3 with Qt Multimedia, Debug, warnings as errors, tests |
| Linux Clang | clang++ 18, otherwise the same |
| Linux ASan + UBSan | g++ 14, Qt 6.8.3 without Qt Multimedia, sanitizers on; undefined behaviour fails the test |
| Linux Qt 6.5 | g++ 13 and Qt 6.5.3 (the oldest accepted), without Qt Multimedia |
| Windows MSVC | `windows-release` with warnings as errors, tests and `cpack`; the zip is kept as an artifact |

## Source layout

| Directory | Contents |
| --- | --- |
| `src/cube` | The cube engine, with no Qt: notation parser, facelet and cubie models, Kociemba's two-phase solver, random-state and subset scramblers, holds. |
| `src/train` | Training logic, with no Qt: alg sets and alg-to-case matching, the SRS scheduler, the flows of Learn, Drill and the solve modes as state machines, the EO Cross, F2L and TSLE engines, step solvers (IDA*), the alg generator, ergonomic scoring, statistics and solve review. |
| `src/io` | Storage on Qt Core: the data file format, alg sets and progress, each mode's files, cstimer conversion, backups and export. |
| `src/app` | The Qt Widgets UI: `vcube_gui` (everything but `main`) and `main.cpp`. |
| `tests` | The Catch2 unit tests and the GUI test. |
| `resources` | Built-in alg sets, icons, the Windows version resource and the Linux desktop entry. |

### Adding a mode

1. Add the enumerator to `AppMode::Id` in `src/app/mode_table.hpp`, before
   `Today` or after it with `Count` updated.
2. In `src/app/mode_table.cpp`, add it to `kModes` (settings id and label,
   in enum order), `kAll` (enum order) and `kSidebarOrder`; `static_assert`s
   there fail if one is short. Add it to the switch in `sidebarHeader` and,
   if it needs one, to `menuSeparatorAfter`.
3. Create the page in `MainWindow`'s constructor and add it to the table
   that fills `pages_` (`src/app/main_window.cpp`), and to the page list of
   `GuiTest::modesSwitch` in `tests/test_gui.cpp`.
4. Give it a section in the key table in `src/app/help_text.cpp`, then
   regenerate `docs/keys.md` (see [Tests](#tests)).
5. If it records activity, add a row to Today's mode table
   (`src/app/today_page.cpp`).
