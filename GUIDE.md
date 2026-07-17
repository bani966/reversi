# Building Reversi from source

A from-scratch build guide, distinct from README.md's quick-start — this walks through
prerequisites, first-time setup, every regular command, and the gotchas that actually show up in
practice. Windows is the primary target (the GUI is developed and tested there); Linux and macOS
build the engine/CLI/tests today, and macOS also builds the full GUI (CI verifies this — see
`.github/workflows/ci.yml`).

## Windows

### Prerequisites

- **Visual Studio 2022** — the "Desktop development with C++" workload (Build Tools alone are
  enough; the full IDE isn't required). This provides the MSVC compiler and the CMake Visual
  Studio generator uses to locate it.
- **CMake ≥ 3.24** — either standalone or the copy bundled with Visual Studio's C++ workload.
- **Qt 6.8** (`msvc2022_64` kit, with the `qtmultimedia` module) — install via the
  [Qt online installer](https://www.qt.io/download-qt-installer) or `aqtinstall`. Note the
  install path (e.g. `C:/Qt/6.8.3/msvc2022_64`) — it's needed in the next step.

### First-time setup

```
copy CMakeUserPresets.json.example CMakeUserPresets.json
```

Open `CMakeUserPresets.json` and set `CMAKE_PREFIX_PATH` to your Qt install path from above. This
file is gitignored on purpose (the path is machine-specific) — it's not something to commit.

Qt's `bin/` directory must also be on `PATH` in whatever shell you build/test/run from (e.g.
`C:\Qt\6.8.3\msvc2022_64\bin`). Without it, `ctest` fails every GUI-related test with
`STATUS_DLL_NOT_FOUND`, and launching `reversi-app.exe` directly shows a native "Qt6Widgets.dll
was not found" dialog instead of the app. This is unrelated to whether CMake configured
correctly — it's purely a runtime DLL search path issue for this one shell session.

### Build & test

```
cmake --preset dev
cmake --build --preset dev          (Release; use dev-debug for a Debug build)
ctest --preset dev
```

The `dev` preset uses the Visual Studio generator, which is multi-config — every build/test
invocation needs a configuration attached, which is exactly what the presets already do. Don't
call `cmake`/`cmake --build`/`ctest` without a preset; a bare invocation won't pick the right
generator or configuration.

Do **not** switch this preset to Ninja. A plain shell (not a Visual Studio Developer/IDE terminal)
has no MSVC environment (`vcvars`) loaded, and Ninja needs `cl.exe` already on `PATH` to work at
all. The Visual Studio generator doesn't have this problem — MSBuild locates the toolchain itself
regardless of what's on `PATH` — which is exactly why it's the one used here.

### Running it

```
build\msvc\app\Release\reversi-app.exe      (GUI — remember Qt's bin/ on PATH, see above)
build\msvc\cli\Release\reversi-cli.exe      version
build\msvc\cli\Release\reversi-cli.exe      perft 6
build\msvc\cli\Release\reversi-cli.exe      bench 12
build\msvc\cli\Release\reversi-cli.exe      selfplay search:4 random 20
```

`reversi-cli` with no arguments prints the full command list (`perft`, `bench`, `selfplay`,
`solve`) with usage for each.

## Linux / macOS

Engine + CLI + tests build and run on both (no Qt needed for this subset):

```
cmake --preset ci-linux            (or ci-macos, given a Qt install — builds the GUI too)
cmake --build --preset ci-linux --parallel
ctest --preset ci-linux
```

`ci-macos` additionally builds the full Qt GUI if Qt 6.9+ is available (`CMAKE_PREFIX_PATH` set
the same way as Windows' `CMakeUserPresets.json`, just pointed at the macOS Qt kit instead) — this
is exactly what the `macos` CI job does, and it's the only way this combination gets verified
today, since there's no macOS machine in the regular dev loop for this project.

There's no packaged Linux GUI target (no Qt-on-Linux install instructions here) — Linux's role in
this project is engine/CLI correctness and the two sanitizer jobs (ASan+UBSan, TSan), not a GUI
deployment target.

## Optional: the WTHOR/MPC tooling (`tools/`)

Off by default (`REVERSI_BUILD_TOOLS`, default `OFF`) — this is the offline pipeline that
generates trained pattern-evaluation weights, opening books, and Multi-ProbCut models, not
something the routine app/CLI build needs:

```
cmake --preset dev -DREVERSI_BUILD_TOOLS=ON
```

The C++ side (`wthor_extractor`, `mpc_fitter`) builds and tests the same way as everything else
above. The Python side (`tools/train_pattern_eval.py`) needs its own virtualenv:

```
python -m venv tools/.venv
tools\.venv\Scripts\pip install -r tools/requirements.txt
tools\.venv\Scripts\python tools/train_pattern_eval.py <dataset...> <out.bin>
```

See `tools/README.md` and `CLAUDE.md`'s "WTHOR pipeline"/"Multi-ProbCut" sections for the full
subcommand reference and file formats — none of this is needed to build or run the app itself,
since the app works fully with disc-differential evaluation and no book/MPC model loaded.

## Packaging an installer/DMG locally

Not part of the routine build. See README.md's "Installing" section for prebuilt downloads, or
`app/CMakeLists.txt` (the `qt_generate_deploy_app_script()` call) and
`packaging/windows/reversi.iss.in` if you want to reproduce the Windows installer or macOS DMG
locally — the exact commands CI uses are in `.github/workflows/ci.yml`'s `windows`/`macos` jobs.

## Troubleshooting

- **`STATUS_DLL_NOT_FOUND` running tests, or a "Qt6Widgets.dll was not found" dialog launching the
  app** — Qt's `bin/` isn't on `PATH` in this shell. See "First-time setup" above.
- **CMake can't find Qt6 at configure time** — `CMAKE_PREFIX_PATH` in `CMakeUserPresets.json`
  doesn't match your actual Qt install path, or the `qtmultimedia` module wasn't installed
  alongside the base Qt kit.
- **`cmake --install` fails with a Qt deploy-script error about "Unparsed arguments"** — a real Qt
  6.8.3 CMake bug: the generated deploy script doesn't quote its executable path, so it breaks if
  the resolved path contains spaces (e.g. a project checked out somewhere like
  `C:\Users\you\My Documents\...`). Not something this repo can fix from its side — check out the
  project at a space-free path, or use `subst` to create a space-free drive-letter alias to build
  from instead. CI is unaffected (its checkout paths never contain spaces).
