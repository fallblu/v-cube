# Building v-cube

v-cube is C++23 with Qt 6 Widgets, built with CMake (3.25 or later) and
Ninja. It is developed on Ubuntu 26.04 under WSL2/WSLg with g++ 15 and
Qt 6.10, and also builds natively on Windows with MSVC. CI builds it with
GCC 13 and 14, Clang 18, MSVC 2022, and Qt 6.5 and 6.8.

## Requirements

- A C++23 compiler: GCC 13 or later, Clang 18 or later, or MSVC 2022 or
  later.
- CMake 3.25 or later and Ninja.
- Qt 6.5 or later: Core, Gui, Widgets, Concurrent and Network for the app
  (all in `qt6-base-dev` on Debian and Ubuntu); only Core for the libraries
  and tests.
- Optional: Qt Multimedia (`qt6-multimedia-dev`). With it the inspection
  calls play a sound; without it they use the system beep.
- Catch2 3 for the tests. An installed copy is used if CMake finds one;
  otherwise v3.7.1 is fetched with git at configure time.

## Linux

```bash
sudo apt install build-essential cmake ninja-build qt6-base-dev catch2
cmake --preset release
cmake --build --preset release
./build/release/src/app/v-cube
```

`cmake --install build/release --prefix ~/.local` installs the app with a
desktop entry and icons (and the README and licence under
`share/doc/v-cube`).

## Presets

Each configure preset builds into `build/<preset>` with Ninja and writes
`compile_commands.json`. Each has a build preset and a test preset of the
same name.

| Preset | Build type | Notes |
| --- | --- | --- |
| `debug` | Debug | |
| `release` | Release | |
| `asan` | Debug | `VCUBE_SANITIZE=ON`: AddressSanitizer and UBSan (not on MSVC) |
| `windows-debug` | Debug | MSVC, only on a Windows host; see [Windows](#windows) |
| `windows-release` | Release | MSVC, only on a Windows host; installs into `install/windows-release` |

## Tests

A test preset runs the tests of its own build, so build that preset first:

```bash
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

The same goes for `release` and `asan`. The tests (in `tests/`, one Catch2
executable, `vcube_tests`) cover the cube engine, the training logic and
the storage, need Qt Core but no display, and check among other things that
every built-in case is distinct and that standard algs are matched to the
right cases. Under `asan`, LeakSanitizer is given `tests/lsan.supp` for
leaks inside Qt.

## Build options

Pass these to the configure step, e.g.
`cmake --preset debug -DVCUBE_USE_CCACHE=ON`.

| Option | Default | What it does |
| --- | --- | --- |
| `VCUBE_BUILD_APP` | `ON` | Build the Qt application. `OFF` builds only the libraries and tests, which need Qt Core but no GUI modules. |
| `VCUBE_BUILD_TESTS` | `ON` | Build the unit tests. |
| `VCUBE_WARNINGS_AS_ERRORS` | `OFF` | Treat warnings as errors (`-Werror`, `/WX`). CI and the release builds turn it on. |
| `VCUBE_SANITIZE` | `OFF` | Build with AddressSanitizer and UBSan (GCC and Clang; ignored on MSVC). The `asan` preset turns it on. |
| `VCUBE_USE_CCACHE` | `OFF` | Compile through ccache (which must be installed). |
| `VCUBE_USE_MOLD` | `OFF` | Link with mold. Needs CMake 3.29 or later. |

ccache and mold are off by default so that a build doesn't depend on what
happens to be on `PATH`.

Warnings: everything builds with `-Wall -Wextra -Wpedantic -Wshadow` (`/W4
/permissive-` on MSVC); the Qt-free libraries in `src/cube` and `src/train`
add `-Wconversion -Wsign-conversion`. Builds should have no warnings.

## Formatting

Formatting follows `.clang-format`. CI checks every `.cpp` and `.hpp` file
with clang-format 21.1.8 (other versions format some constructs
differently):

```bash
pipx install clang-format==21.1.8
git ls-files -z '*.cpp' '*.hpp' | xargs -0 clang-format --dry-run --Werror
```

## Windows

Prerequisites:

- Visual Studio 2022 or later, or its Build Tools, with the **Desktop
  development with C++** workload. Check that its **Windows 11 SDK** (or
  Windows 10 SDK) component is ticked: the build needs the SDK's `rc.exe`
  and `mt.exe`, and CMake fails with "is not able to compile a simple test
  program" without them. The workload also provides CMake and Ninja; a
  standalone CMake 3.25+ and Ninja work too.
- Qt 6.5 or later for MSVC (`msvc2022_64`) from the
  [Qt online installer](https://www.qt.io/download-qt-installer).
- Git, used to fetch Catch2 for the tests unless an installed Catch2 3
  (e.g. from vcpkg) is on `CMAKE_PREFIX_PATH`.

Open an **x64 Native Tools Command Prompt** for your Visual Studio version
(`where rc mt` should find both), point `QTDIR` at the Qt install (the
presets put it on `CMAKE_PREFIX_PATH`), then configure, build, test and
deploy:

```bat
set QTDIR=C:\Qt\6.10.0\msvc2022_64
cmake --preset windows-release
cmake --build --preset windows-release
ctest --preset windows-release
cmake --install build\windows-release
install\windows-release\bin\v-cube.exe
```

`cmake --install` runs Qt's deploy script (`windeployqt`) and copies the
MSVC runtime, so `install\windows-release\bin` is a self-contained folder
that you can copy to a PC without Qt. The tests get the Qt DLLs copied next
to them, so they run without Qt on `PATH`, but the `v-cube.exe` in
`build\` needs Qt's `bin` folder on `PATH` to run. `windows-debug` works
the same way.

## WSL

Under WSL (detected from `WSL_DISTRO_NAME` or a kernel release that names
Microsoft) the app uses Qt's X11 (`xcb`) platform plugin by default rather
than Wayland. WSLg forwards every Wayland surface to Windows as a separate
window, which leaves closed drop-downs and menus on screen and stops the
window position from being restored. If the `xcb` plugin or its libraries
are missing, it falls back to Wayland instead of failing to start. To pick
another plugin, set `QT_QPA_PLATFORM` yourself; it always takes precedence:

```bash
QT_QPA_PLATFORM=wayland ./build/release/src/app/v-cube
```

**Help > About v-cube** shows the platform plugin in use. Outside WSL, Qt
picks the plugin as usual.

## Packaging

`cpack`, run in a build folder, packs the install tree: on Windows a zip of
the self-contained folder (`v-cube-<version>-windows-x64.zip`), elsewhere a
tarball. The Linux AppImage is made from the install tree with linuxdeploy
and its Qt plugin (see `.github/workflows/release.yml`):

```bash
cmake --install build/release --prefix AppDir/usr
./linuxdeploy-x86_64.AppImage --appdir AppDir --plugin qt --output appimage \
  --desktop-file AppDir/usr/share/applications/v-cube.desktop \
  --icon-file AppDir/usr/share/icons/hicolor/256x256/apps/v-cube.png
```

The version shown in **Help > About**, the package names and the Windows
file version all come from `git describe --tags --always --dirty`, so a
tagged build is named after its tag (`v0.2.0` gives `0.2.0`) and other
builds carry the commit (e.g. `0.2.0-3-gabc1234`, or `0.1.0-gabc1234`
before the first tag). It is worked out again at every build, and nothing
is rebuilt unless it changed. A source tree without git uses the project
version from `CMakeLists.txt`.

### Releases

Pushing a tag such as `v0.2.0` runs `.github/workflows/release.yml`. It
builds and tests a Linux AppImage (release preset, Qt 6.8 with Qt
Multimedia, on Ubuntu 24.04) and a Windows zip (`windows-release` preset
and `cpack`), each with warnings as errors, and attaches both to a GitHub
release with generated notes. The workflow can also be run by hand, which
builds both without publishing.

## CI

`.github/workflows/ci.yml` runs on pushes to `main`, on pull requests and by
hand, with `QT_QPA_PLATFORM=offscreen`:

| Job | What it does |
| --- | --- |
| Format | clang-format 21.1.8 over every `.cpp` and `.hpp` |
| Linux GCC | g++ 14, Qt 6.8.3 with Qt Multimedia, Debug, warnings as errors, tests |
| Linux Clang | clang++ 18, otherwise as GCC |
| Linux ASan + UBSan | g++ 14, Qt 6.8.3 without Qt Multimedia, `VCUBE_SANITIZE=ON` |
| Linux Qt 6.5 | g++ 13 and Qt 6.5.3, the oldest Qt accepted, without Qt Multimedia |
| Windows MSVC | `windows-release` preset with warnings as errors, tests, `cpack`; the zip is kept as a build artifact |

## Source layout

| Directory | Contents |
| --- | --- |
| `src/cube` | Pure C++ engine. Notation parser (groups, commutators, conjugates; invert, mirror, simplify); a facelet model built from 3D geometry that handles slices, wide moves and rotations; a cubie model; Kociemba's two-phase solver (tables built in under a second); random-state and subset scramblers; holds. |
| `src/train` | Pure C++ training logic, without widgets. Alg sets, AUF-aware alg-to-case matching and checks, paste import, the SRS scheduler and Learn's session, attempt grading and walkthrough, Drill's flow and case picking, mirror families, mirror worlds and step-at-a-time (reduction) sets, the timer and the Timed, Untimed / Move Count, Inspection and Blind flows as state machines, WCA statistics, Move Count step checks, optimal EO to EO Cross, cross and <R, U, L> pair solving (IDA*), solve review, Old Pochmann tracing and setups, and the Today summary. |
| `src/io` | Qt Core storage. The data file format checks and background writes, JSON alg sets and progress, the alg-set library, the session files of every mode, cstimer conversion, daily backups and export. |
| `src/app` | Qt Widgets UI (`vcube_gui`, and `main.cpp`). The cube renderer, the mode pages and their shared session panel, the Library editor, the solve review, the help panel and command list, the shared scramble source, settings and the remembered layout. |
| `tests` | Catch2 tests for the engine, training logic and storage. |
| `resources` | Built-in alg sets, the app icon, the Windows version resource and the Linux desktop entry. |
| `.github/workflows` | CI and tag releases. |
