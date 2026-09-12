# Installer

A Windows installer for the retail game, written for Inno Setup. It installs the 1999 game from the
player's own disc and then whichever parts of the OpenPhantom patch were ticked.

|  |  |
|---|---|
| Form | Inno Setup script plus one small C helper, `is3_extract` |
| Input | the original PC disc, or a mounted image of it |
| Output | `output/OpenPhantom_Installer.exe` |
| Carries | everything it installs: the extractor, the patch, libVLC, FFmpeg, DSOAL, dxwrapper and the saved games. No game data |
| Fetches | nothing, during installation or afterwards |

**You need your own copy of the game.** No game data, no executable and no patched binary is included
here or distributed with this project.

## What it installs

From the disc: the launcher, the program and the runtimes beside it, the music and cutscene audio,
the localisation archive and the data tree. The C helper unpacks `big.lab` from
`GAMEDATA\GOBS\BIG.Z` while the wizard runs.

`BIG.Z` and `MENACE.DAT` are left behind, about 747 MB between them. `BIG.Z` is 711 MB on the
pressing although its archive is only the first 81 MB, and `MENACE.DAT` starts out byte identical to
it and stops short of the archive's end, so nothing can read it.

The registry entry the game reads for its CD path is pointed at the installation folder, which
removes the need for the disc in the drive.

Everything else ships inside the installer, in `dist/`. Nothing is downloaded at any point, so an
installation is reproducible years from now rather than dependent on somebody else's hosting.

## Components

Three tiers. Part of the patch and not unpickable: the graphics wrapper, the two crash repairs, the
crash reporter and the three audio repairs. Recommended, so a full installation takes them:
resolution, frame rate, field of view, HUD scaling, decals, view distance, the ground clip repair,
the conversation animation repair and the camera handback repair. Offered but not ticked: large
textures, input, sound, the movie player, dismemberment, the developer overlay, the diagnostics, a
set of finished saved games, and a starting controller layout and display mode. The tiers are the
`Types:` column of each row in `src/openphantom_patch.iss`, which is the list to trust over this
paragraph.

What each fix does is in [`legacy/README.md`](../legacy/README.md), and each has a `README.md` beside
its own source. This directory does not repeat those.

The wizard asks three questions of its own and writes the answers into the settings files: how the
cutscenes are scaled (`[fmv_player] Scaling`), the frame rate cap (`[framerate_fix] TargetFps`,
`MatchDisplayRefresh` and `RefreshDivisor`, where the default answer is the screen's own rate with
the fraction left automatic), and the starting resolution, one key in the game's own `obi.ini`,
whenever the resolution patch is installed.

## Version numbers

This installer is numbered `1.4.x` and the patch it carries is numbered `0.4.x`. They are two
different numbers on purpose, and both are set by hand:

| where | what it holds |
|---|---|
| `AppVer` in `openphantom_installer.iss` | the installer's own number |
| `src/is3_extract/version.rc` | the same number, stamped into the extractor's version resource |
| `PatchVersion` in `src/openphantom_patch.iss` | which patch release `dist/patch` was taken from |
| `OPENPHANTOM_VERSION` in `legacy/CMakeLists.txt` | the patch's number: every DLL's version resource, and the log header |

The values are in those files rather than repeated here, where a copy went stale twice.

**The last digit of the installer counts installer builds.** Build a new one, add one. It is not a
judgement about how much changed. An earlier rule here tried to be that.

**The binaries carry the patch's number.** The DLLs are the patch, so their version resources and
the first line of `engine_fixes.log` read the patch's number, while the installer that delivered
them reads its own. Two numbers on one machine is the cost of two lines, so both are written down
here.

**One release was published with the two merged**, as `v1.5.0` and `i1.5.0`. The lines are separate
again, so that release is renamed on GitHub to `v0.4.1` and `i1.4.1`, which puts it where it belongs
in both sequences and leaves no gap. The rename is presentational: the installer in that release was
not rebuilt and still reports `1.5.0` in its own properties, because re-cutting it would change
every hash and the file is already referenced from ModDB and PCGW.

Nothing in the script compares versions. An existing installation is found by `AppId` and the player
is asked what to do with it, so no number decides whether an install is allowed, and the renumbering
cannot block anybody.

## Building

```sh
cmake -S src/is3_extract -B src/is3_extract/build -A Win32
cmake --build src/is3_extract/build --config Release
ISCC openphantom_installer.iss
```

`-A Win32` is not optional; the game is 32 bit and so is everything this project ships. The result is
`output/OpenPhantom_Installer.exe`, and no build output is tracked here.

Built with Inno Setup 6.6. Nothing here needs a recent feature any more now that the download
and archive-extraction flags are gone, but that is the version it is compiled and tested with.

## Structure

```
openphantom_installer.iss  the entry point: the disc, the registry, the shortcuts, the wizard
dist/                      everything the installer carries; nothing is downloaded at install time
  patch/                   the OpenPhantom patch, unpacked from its release archive
  dxwrapper/               DirectDraw-to-Direct3D translation (ini edited, see the notices)
  vlc/                     33 files of libVLC, for the cutscene player
  ffmpeg/                  FFmpeg, for converting the cutscenes; installed beside libVLC
  dsoal/                   DSOAL and OpenAL Soft, for 3D sound
  saves/                   a save at the start of each chapter
source/                    corresponding source for the GPL and LGPL parts of dist/, pinned to
                           the revisions they were built from; ships with every release
THIRD-PARTY-NOTICES.md     what is in dist/, under what licence, and what a release must ship
src/
  openphantom_patch.iss    the patch and the libVLC runtime
  dsoal.iss                the audio wrapper
  complete_saves.iss       the optional saved games
  game_defaults.iss        the optional starting settings written into obi.ini
  is3_extract/             the extractor, a small C project of its own
output/                    the built installer, ignored
```

One subject per file, so a new patch release touches one of them and a new carried component
touches one of them.

## Updating a carried component

Replace its folder under `dist/`, then update its row in `THIRD-PARTY-NOTICES.md` and in
`dist/THIRD-PARTY-NOTICES-Installer.txt` with the new version, and refresh the corresponding source
archive under `source/` where the licence asks for it.

`dist/patch/` is refreshed wholesale from a build of `legacy/`, so anything else kept in that folder
is destroyed on the next refresh; that is why dxwrapper has a folder of its own.

Two things do not survive a refresh on their own and have to be re-applied: the settings in
`dxwrapper.ini` that differ from the file upstream ships, listed in `THIRD-PARTY-NOTICES.md`, and
any component whose destination folders are derived rather than written out, currently the libVLC
plugins. Diff the shipped ini against the one inside the release archive rather than working from
a list; upstream adds settings between releases, and a new one arrives with its own default only
if the file is rebuilt from theirs.

## Testing status

Checked without installing anything: the script compiles with no warnings under Inno Setup 6.6,
every component named by a file row is declared, every file row has a file and every file in
`dist/patch/mods` has a row, and both languages carry every message. The extractor is verified
against a retail pressing, where `BIG.Z` produces a `big.lab` of 120,859,357 bytes, byte identical
to a known good copy, and it imports nothing but `KERNEL32.dll`, so it runs on a machine that has
no Visual C++ runtime installed.

**Installed from and played.** The build that carries patch 0.4.4 was installed in full into a
fresh folder and the game played from it: the log shows every DLL loading and arming, the
subtitles resolving all twelve of their sites, the music heartbeat never stalling, and the panel
opening. Earlier builds of this same script were also installed with the network disconnected, on
a different target folder, and completed without pausing for anything; that is the claim the
offline form exists to support and nothing in the script has since touched a network.

**The starting settings component**, installed onto a fresh installation: every binding byte for
byte the layout it was taken from, `JOYENABLE` on, and the game starting at the chosen size rather
than the engine's own 640x480. The backup beside it, `obi.ini.previous`, held the two lines the
sound provider had just written; that is preserving a player's file when there is not yet a
player's file to preserve. The resolution is a starting value and not a limit; the
player changed it in the game's own video options afterwards and the new mode stuck.

**The bundled saved games never overwrite a slot the player already has.** They used to: the copy
went through the same helper the carry-over restore uses, which overwrites because that is what
restoring somebody's own files back needs, and a returning player updating a patch lost slots 1 to
11 while the error text on that very copy promised their saved games were not touched. A second
run over a folder that already held all eleven slots now reports `complete_saves: 11 carried, 0
written, 11 slots left alone because the player already had a save there`, and the saves loaded
in the game afterwards were the player's own progress, not the bundled chapter starts.
`SetupLogging` is on because of this: the first installation to exercise the skip produced nothing
to read the decision in.

**Still not watched.** A run that succeeds does not exercise a branch it never reached, and these
were not reached: detecting a graphics wrapper that belongs to somebody else, the confirmation
before a folder is deleted and the carrying out and back that goes with it, and retiring a
controller wrapper an older version of this installer left under the name `winmm.dll`. Each of them
stops rather than continuing optimistically, but a branch nobody has taken is a branch nobody has
watched.

If you exercise one, say which, and say what the folder looked like beforehand.

## Legal

An independent fan project, not affiliated with, endorsed by or sponsored by LucasArts, Lucasfilm or
Disney. All trademarks belong to their owners. No game content is included or redistributed.
