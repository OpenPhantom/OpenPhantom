# Legacy engine fixes

Fixes for the original 1999 engine of *Star Wars Episode I: The Phantom Menace*, so the retail game
runs properly on a current machine. This is not the OpenPhantom reimplementation and it does not
replace it: it is a loader and twelve small DLLs that patch the retail executable in memory.

Nothing on disk is modified. Drop the loader next to `WMAIN.EXE`, put the fixes you want in
`mods\`, and each one patches the image as the game starts. Delete a DLL and its fix is gone.
Delete the loader and the game is exactly as it shipped.

|  |  |
|---|---|
| Target | `WMAIN.EXE`, 829,952 bytes, MD5 `7c5af8428c19b17cca09ae3a49bd10ef` |
| Form | a 32 bit `dinput.dll` loader plus one DLL per fix. No launcher, no injector, no admin rights |
| Build | CMake and the MSVC x86 toolset |
| Tests | 14 programs, 700 checks, none of which needs the game |

The English and German retail releases are byte identical, so that one MD5 covers both.

**You need your own copy of the game.** No game data, no executable and no patched binary is
included here or distributed with this project.

## What the fixes do

| DLL | What it does |
|---|---|
| `variable_fov` | A field of view you can set, corrected for your aspect ratio, with a real slider in the game's own video options screen |
| `enhanced_resolution` | Modern resolutions in the options screen, and a mouse pointer that stays in the window |
| `framerate_fix` | A free render rate that does not change how the game plays |
| `hud_ratio_scaling` | One size knob for the HUD, and text that is not stretched on a wide screen |
| `view_distance_fix` | Draw distance and fog, coupled so that raising one is not wasted, with a watchdog on the limits they run into |
| `enhanced_input` | Mouse look, sideways walking and free look |
| `dismemberment` | Lightsaber dismemberment: the limb the blade actually hit, and only on the killing blow |
| `imuse_fix` | Pauses the music when the game loses focus, and stops the music thread locking itself up |
| `decal_fix` | Restores blast marks, scorch marks and blob shadows, which a Direct3D 9 translation layer drops |
| `crt_copy_fix` | Repairs an inlined copy loop that reads four bytes before its source, in 40 places |
| `crash_report` | On a crash: exception code, address, module, registers and the engine frames from the stack |
| `diagnostics` | Observation only, per subsystem, off by default |

Each directory under `src/mods` has a `README.md` with its settings, the engine locations it
touches, and what has and has not been tested.

## Building

You need **CMake 3.15 or newer** and **Visual Studio 2019 16.8 or newer** with the **x86** (32 bit)
C++ toolset. That compiler version is the floor because the compile time layout checks need C11.

The game is 32 bit, so the DLLs must be too. `-A Win32` is not optional, and CMake stops with an
explanatory error if you leave it out.

```sh
cmake -S . -B build -A Win32
cmake --build build --config Release
ctest --test-dir build -C Release
```

That produces `build/dist/dinput.dll` and `build/dist/mods/*.dll`, one per fix. Nothing is
installed for you, and no build output is tracked in this repository.

## Installing

1. Copy `build/dist/dinput.dll` next to `WMAIN.EXE`.
   **If a `dinput.dll` is already there, rename it to `dinput_orig.dll` first rather than
   overwriting it.** Graphics wrappers and ASI loaders want the same slot, and the loader forwards
   every DirectInput export to whatever it finds under that name, so the previous occupant keeps
   working.
2. Copy the DLLs you want from `build/dist/mods/` into `<game>\mods\`.
3. Copy `dist/engine_fixes.ini` next to `WMAIN.EXE`.
4. Start the game.

## Configuration

`engine_fixes.ini` sits beside `WMAIN.EXE`, one section per DLL, named after that DLL. The copy in
`dist/` is meant to be playable as it stands: every fix on, every measurement off, and nothing that
changes how the game plays beyond repairing what it was written to repair. Each key carries a short
comment, so the file is also the reference.

The file is optional. Every setting has the same default in the code, so a missing ini behaves
exactly like the shipped one.

**Free look** (`[enhanced_input] FreeLook`) is the one deliberate exception: it is off, because it
changes the control scheme rather than fixing a fault. There is a check box for it on the game's
own controls screen.

**Nothing writes to disk while you play.** Every diagnostic channel is off. Each DLL logs a few
lines to `engine_fixes.log` when it installs and then goes quiet, which is enough for a bug report
and costs nothing during play. `[crash_report]` is on for the same reason: it does nothing at all
until the process dies.

When you upgrade, keep your own ini and copy across any keys the new version added. Nothing
rewrites your file for you.

## If a fix does not take

Read `engine_fixes.log`. Every DLL logs the branch it took rather than just the result, so a fix
that did nothing says which site failed to resolve. A byte pattern that does not match exactly once
disables that one patch and leaves the engine untouched; the other fixes carry on. That is the
designed behaviour on an executable these patterns were not cut from.

## Project structure

The tree is laid out the way the artefacts are installed, so a path tells you where its output
goes:

```
CMakeLists.txt         project settings and compiler flags, nothing else
README.md
CONTRIBUTING.md        the coding rules, and why each one exists
dist/
  engine_fixes.ini     the configuration that ships with a release
src/
  common/              static library, linked into every DLL and every test
  loader/              builds dinput.dll, installed next to WMAIN.EXE
  mods/
    variable_fov/      builds mods\variable_fov.dll
    enhanced_input/    builds mods\enhanced_input.dll
    ...                one directory per fix, twelve of them
unittests/             pure logic, runs without the game
```

### Three layers, and why

**`src/common` is the shared layer.** It is a *static* library, so each DLL links its own copy.
That is deliberate: there is no runtime library that everything else needs, no version to keep in
step, and deleting one fix cannot break another. Ten small modules, one job each:

| Module | Job |
|---|---|
| `host_image` | Where the game is: base address, code section, its directory |
| `signature` | Find engine code by what it looks like, never by where it used to be |
| `memory` | Range checks and page protection |
| `patch` | Every write into engine memory goes through here |
| `detour` | Inline hooks that several DLLs may place on one function |
| `frame_hook` | Call me once per rendered frame |
| `logging` | One log file, one prefix per DLL |
| `ini` | One ini file, one section per DLL |
| `menu_patcher` | Append widgets to one of the game's own menu screens |
| `engine_types.h` | Binary structures more than one fix needs |

**`src/loader` builds `dinput.dll`**, which sits next to the executable and patches nothing itself.
Its only job is to load the fixes at the right moment and get out of the way.

**`src/mods` holds the fixes**, one directory per DLL. They never call each other. A fix can be
deleted from `mods\` and nothing else notices, which is also how you bisect a problem.

### Inside a fix

A small one is three files:

```
src/mods/decal_fix/
  dll_main.c       the DllMain and the engine_fix_install export, a few lines
  decal_fix.c      the feature
  decal_fix.h
  README.md        settings, engine locations touched, testing status
```

A larger one splits by responsibility rather than by size. `enhanced_input` has separate files for
the mouse, sideways walking, free look, the camera it steers, the byte patterns that find the
engine sites, and the menu widgets it adds. The rule of thumb is that a new file earns its place
when it has a job you can name in one sentence.

**One word names everything.** The directory, the DLL, the ini section and the log prefix are the
same word for every fix, so a line in the log tells you which file produced it with no lookup
table:

```
src/mods/variable_fov/  ->  mods\variable_fov.dll  ->  [variable_fov]  ->  [variable_fov] slider ...
```

### How a new fix is added

1. Create `src/mods/your_fix/` with `dll_main.c`, `your_fix.c` and `your_fix.h`.
2. Add one `add_engine_fix(your_fix dll_main.c your_fix.c)` call to `src/mods/CMakeLists.txt`.
3. Add a section to `dist/engine_fixes.ini` and a `README.md` to the directory.

There is no registry to update and no list to keep in step. The loader finds the DLL because it is
in `mods\`, and calls it because it exports `engine_fix_install`.

### Includes

`src` is the include root, so the shared layer is always reached the same way from anywhere in the
tree:

```c
#include "common/logging.h"     /* from a fix, from the loader, from a test */
```

No file counts directory levels to find a header, and moving a fix does not change a single
include.

### Tests

`unittests/` holds one file per module under test, built against the real module rather than a
stub. `unittest.c` is a small shared harness: `ut_check`, `ut_near`, `ut_section` and a summary.
Adding a suite is one line in `unittests/CMakeLists.txt`.

They cover the arithmetic, which is where the mistakes that are invisible at run time live: field
of view maths, the HUD layout, the mouse and movement dampers, the fog band, the instruction
encoder, the menu patcher, the pattern matcher and the detour chain. None of them needs the game
or a graphics device, so `ctest` runs the lot in about two seconds.

## How it works

**The loader.** `dinput.dll` is a static import of `WMAIN.EXE`, so its `DllMain` runs before the
game does anything. It loads nothing there, because calling `LoadLibrary` under the loader lock is
how deadlocks are made. Instead it writes five bytes: a jump over the host's entry point. The stub
puts the original bytes back, loads every DLL in `mods\` in sorted order, calls each one's
`engine_fix_install` export, and jumps to the entry point, which then runs from its first byte.

The obvious trigger, the game's own call to `DirectInputCreateA`, is too late: graphics startup
runs before input startup, so several patches would arrive after the thing they patch has already
been used. It is kept as a fallback and whichever fires first wins.

A DLL in `mods\` without an `engine_fix_install` export is loaded anyway and noted in the log, so
an ordinary third party mod works too.

**Signatures, not addresses.** Three builds of this engine exist at 829,952 bytes each, and one of
them is a recompile in which most of the code section differs and the frame limiter has moved. An
address table would have written silently into a different function. So every site is found by a
byte pattern that has to match an expected number of times, usually exactly once. Anything else
disables that one patch and says so in the log.

**Chained detours.** Six of the twelve DLLs want the same engine function, the one that ends a
rendered frame. A conventional trampoline hook placed on an already hooked target copies the first
hook's jump into its own trampoline and builds an infinite loop that reports success.
`src/common/detour.c` instead detects the existing branch, keeps its destination as the caller's
`original`, and points the branch at itself. The chain unwinds correctly whatever order the DLLs
loaded in, which is why load order encodes no dependencies. `unittests/detour.c` builds a chain of
three hooks and calls through it.

**Validate, then write.** Every patch reads back what it is about to overwrite and refuses when it
is not what it expected. That habit is also what makes the patches idempotent: a second run finds
the new bytes rather than the expected old ones, and declines.

## Status

* Builds clean with MSVC x86 at `/W4 /WX`, no warnings.
* 14 test programs, 700 checks, all passing, none of which needs the game.
* Every byte pattern resolves with its expected match count against the retail `WMAIN.EXE` and the
  shipped `IMUSE.DLL`, checked offline without running the game.
* Three fixes have been accepted in actual play: the camera damper, the decal repair and the aim
  stance. Everything else is verified offline only, and each feature README says so for itself.

## Contributing

`CONTRIBUTING.md` has the coding rules. They are short, and most of them exist because breaking one
cost somebody a day. Patches are welcome; if a rule gets in your way, say so in the pull request
rather than working around it quietly.

## Legal

An independent fan project, not affiliated with, endorsed by or sponsored by LucasArts, Lucasfilm
or Disney. All trademarks belong to their owners. No game content is included or redistributed.
