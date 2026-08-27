# Installer

A Windows installer for the retail game, written for Inno Setup. It installs the 1999 game from the
player's own disc and then whichever parts of the OpenPhantom patch were ticked.

|  |  |
|---|---|
| Form | Inno Setup script plus one small C helper, `is3_extract` |
| Input | the original PC disc, or a mounted image of it |
| Output | `output/OpenPhantom_Installer.exe` |
| Carries | everything it installs: the extractor, the patch, libVLC, Xidi, DSOAL and the saved games. No game data |
| Fetches | nothing, during installation or afterwards |

**You need your own copy of the game.** No game data, no executable and no patched binary is included
here or distributed with this project.

## What it installs

From the disc: the launcher, the program and the runtimes beside it, the music and cutscene audio,
the localisation archive and the data tree. `big.lab` is unpacked from `GAMEDATA\GOBS\BIG.Z` while
the wizard runs, which is what the C helper is for.

`BIG.Z` and `MENACE.DAT` are left behind, about 747 MB between them. `BIG.Z` is 711 MB on the
pressing although its archive is only the first 81 MB, and `MENACE.DAT` starts out byte identical to
it and stops short of the archive's end, so nothing can read it.

The registry entry the game reads for its CD path is pointed at the installation folder, which
removes the need for the disc in the drive.

Everything else ships inside the installer, in `dist/`. Nothing is downloaded at any point, which is
what makes an installation reproducible years from now rather than dependent on somebody else's
hosting still being there.

## Components

Three tiers. Part of the patch and not unpickable: the graphics wrapper, the two crash repairs, the
crash reporter and the two audio repairs. Recommended, so a full installation takes them: resolution,
frame rate, field of view, HUD scaling, decals and view distance. Offered but not ticked: large
textures, input, controller support, sound, the movie player, dismemberment, the developer overlay,
the diagnostics, and a set of finished saved games.

What each fix does is in [`legacy/README.md`](../legacy/README.md), and each has a `README.md` beside
its own source. This directory does not repeat those.

## Building

```sh
cmake -S src/is3_extract -B src/is3_extract/build -A Win32
cmake --build src/is3_extract/build --config Release
ISCC openphantom_installer.iss
```

`-A Win32` is not optional; the game is 32 bit and so is everything this project ships. The result is
`output/OpenPhantom_Installer.exe`, and no build output is tracked here.

Built with Inno Setup 6.6. Nothing here needs a recent feature any more now that the download and
archive-extraction flags are gone, but that is the version it is compiled and tested with.

## Structure

```
openphantom_installer.iss  the entry point: the disc, the registry, the shortcuts, the wizard
dist/                      everything the installer carries; nothing is downloaded at install time
  patch/                   the OpenPhantom patch, unpacked from its release archive
  dxwrapper/               DirectDraw-to-Direct3D translation (ini edited, see the notices)
  vlc/                     33 files of libVLC, for the cutscene player
  ffmpeg/                  FFmpeg, for converting the cutscenes; installed beside libVLC
  dsoal/                   DSOAL and OpenAL Soft, for 3D sound
  xidi/                    the controller wrapper
  saves/                   a save at the start of each chapter
  vc_redist.x86.exe        Microsoft's runtime, run only when it is missing
  Xidi.ini                 configuration this project wrote
source/                    corresponding source for the GPL and LGPL parts of dist/, pinned to
                           the revisions they were built from; ships with every release
THIRD-PARTY-NOTICES.md     what is in dist/, under what licence, and what a release must ship
src/
  openphantom_patch.iss    the patch and the libVLC runtime
  xidi.iss                 the controller wrapper
  dsoal.iss                the audio wrapper
  complete_saves.iss       the optional saved games
  is3_extract/             the extractor, a small C project of its own
output/                    the built installer, ignored
```

One subject per file, so a new patch release touches one of them and a new carried component touches
one of them.

## Updating a carried component

Replace its folder under `dist/`, then update its row in `THIRD-PARTY-NOTICES.md` and in
`dist/THIRD-PARTY-NOTICES-Installer.txt` with the new version, and refresh the corresponding source
archive under `source/` where the licence asks for it.

`dist/patch/` is refreshed wholesale from a build of `legacy/`, so anything else kept in that folder
is destroyed on the next refresh; that is why dxwrapper has a folder of its own.

## Testing status

Checked without installing anything: the script compiles with no warnings, every component named by a
file row is declared, both languages carry every message, and the pinned patch URL answers and hashes
to the pinned value. The extractor is verified against a retail pressing, where `BIG.Z` produces a
`big.lab` of 120,859,357 bytes, byte identical to a known good copy.

**Installed and played once.** A full installation has been run on a machine and the game started
from it. What that run covered, read off the folder it left behind: the game came off the disc, the
configuration files were replaced with the previous ones kept beside them, the saved games went in,
the sound provider was written into `obi.ini`, and controller support worked from the installation
rather than from files placed by hand.

**Still not watched.** A run that succeeds does not exercise a branch it never reached, and these
were not reached: detecting a graphics wrapper that belongs to somebody else, the confirmation
before a folder is deleted and the carrying out and back that goes with it, and retiring a
controller wrapper an older version of this installer left under the name `winmm.dll`. The Visual
C++ runtime was already on that machine, so that path was skipped rather than tested, which is the
same as untested. Each of them stops rather than continuing optimistically, but a branch nobody has
taken is a branch nobody has watched.

If you exercise one, say which, and say what the folder looked like beforehand.

## Legal

An independent fan project, not affiliated with, endorsed by or sponsored by LucasArts, Lucasfilm or
Disney. All trademarks belong to their owners. No game content is included or redistributed.
