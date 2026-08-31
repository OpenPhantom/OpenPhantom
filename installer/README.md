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
the localisation archive and the data tree. `big.lab` is unpacked from `GAMEDATA\GOBS\BIG.Z` while
the wizard runs, which is what the C helper is for.

`BIG.Z` and `MENACE.DAT` are left behind, about 747 MB between them. `BIG.Z` is 711 MB on the
pressing although its archive is only the first 81 MB, and `MENACE.DAT` starts out byte identical to
it and stops short of the archive's end, so nothing can read it.

The registry entry the game reads for its CD path is pointed at the installation folder, which
removes the need for the disc in the drive.

Everything else ships inside the installer, in `dist/`. Nothing is downloaded at any point, which
is what makes an installation reproducible years from now rather than dependent on somebody else's
hosting.

## Components

Three tiers. Part of the patch and not unpickable: the graphics wrapper, the two crash repairs, the
crash reporter and the two audio repairs. Recommended, so a full installation takes them: resolution,
frame rate, field of view, HUD scaling, decals, view distance and the ground clip repair. Offered but not ticked: large
textures, input, sound, the movie player, dismemberment, the developer overlay, the diagnostics, a
set of finished saved games, and a starting controller layout and display mode.

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

Two things do not survive a refresh on their own and have to be re-applied: the six settings in
`dxwrapper.ini` that differ from the file upstream ships, listed in `THIRD-PARTY-NOTICES.md`, and
any component whose destination folders are derived rather than written out, currently the libVLC
plugins. Diff the shipped ini against the one inside the release archive rather than working from
a list; upstream adds settings between releases, and a new one arrives with its own default only
if the file is rebuilt from theirs.

## Testing status

Checked without installing anything: the script compiles with no warnings under Inno Setup 6.6.0,
every component named by a file row is declared, every file row has a file and every file in
`dist/patch/mods` has a row, and both languages carry every message. The extractor is verified
against a retail pressing, where `BIG.Z` produces a `big.lab` of 120,859,357 bytes, byte identical
to a known good copy.

**Installed from, in this offline form.** A full installation has been run and the folder it left
behind was read back rather than taken on trust: all 21 patch files hash equal to the build they
were carried from, `mods\` holds exactly the twenty DLLs that build produces, and `engine_fixes.ini`
carries no section for a component this project no longer ships. FFmpeg is beside libVLC in
`mods\fmv`, which is the part that used to be fetched the first time the cutscene converter ran, so
the one download that outlived installation is gone as well. This installer no longer offers a
controller component at all, so there is nothing left to say about controller support here.

**Verified with the network disconnected**, which is the claim this version exists to support and
the only way to make it properly. A second full installation was run with no network at all, on a
different target folder, and completed without pausing for anything. Its folder was read back the
same way: twenty DLLs, all twenty-one patch files hash equal to the build, no section for a removed
component, and libVLC, FFmpeg, DSOAL, dxwrapper and the tools all in place. The game was started
from it afterwards and its own log shows all twenty DLLs loading and arming. One site does not
resolve, `view_distance_fix`'s `thing_draw`, and that is a pre-existing fault of the patch on every
install rather than anything the installer did.

**The starting settings component has been installed with, on a fresh installation.** The folder was
read back afterwards: all fifty-nine bindings are byte for byte the layout they were taken from,
`JOYENABLE` is on, and the game started at 1920x1080 rather than the engine's own 640x480.

The backup beside it is the part worth recording, because it shows the merge rather than merely the
result. `obi.ini.previous` came out at 79 bytes holding two lines, `[options]` and the sound driver
key, which is exactly the state the file was in at that moment: the sound provider had created it a
step earlier, and this component then backed that up and merged sixty more keys into it. On a fresh
installation there is no player file to preserve yet, and this is what preserving it looks like
anyway.

The resolution is a starting value and not a limit, which the same run demonstrated by accident: the
player changed it in the game's own video options afterwards and the new mode stuck.

**The DLL counts recorded above are of the build tested at the time, and are left as they were
observed rather than raised.** The patch now carries twenty one fixes: `ground_clip_fix` was added
after both of those installations were run. `dist/patch/` has since been refreshed from a build
that includes it, the script compiles with no warnings, all twenty one file rows have a file and
all twenty one staged files have a row. **No installation has been run from this bundle.** The two
runs recorded above installed the twenty DLL build and nothing has been installed since, so those
paragraphs describe a bundle that is no longer the one in the tree.

**The bundled saved games never overwrite a slot the player already has.** They used to: the copy
went through the same helper the carry-over restore uses, which overwrites because that is what
restoring somebody's own files back needs. Combined with the component sitting in the default
install type and a rerun returning to that default, a returning player updating a patch lost slots
1 to 11, while the error text on that very copy promised their saved games were not touched. The
component is still offered by default, because with collisions skipped a fresh installation still
gets all eleven and a rerun keeps what the player has used.

**Installed from twice, and the saved games were verified rather than assumed.** A second run over
a folder that already held all eleven slots reported `complete_saves: 11 carried, 0 written, 11
slots left alone because the player already had a save there`. Zero written is the whole claim: the
same run before the skip was added would have replaced every one of them. Confirmed in the game
afterwards as well, which is the check the log cannot make: the saves loaded were the player's own
progress from before the installation, not the bundled chapter starts. The carry-over is visible
in the same log putting the player's saves, obi.ini and converted cutscenes back around the
installation.

That log exists because of this. `SetupLogging` was off, so the first installation to exercise the
skip produced nothing to read the decision in, and a save handling change is not something to take
on trust.

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
