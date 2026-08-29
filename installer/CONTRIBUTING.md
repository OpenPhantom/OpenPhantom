# Contributing

The rules this directory is held to. Most exist because breaking one cost somebody a day.

The repository's own `CONTRIBUTING.md` holds here too. `legacy/CONTRIBUTING.md` does not: it is a C
ruleset for patching a running binary, and nothing here places a detour or resolves a byte pattern.

## The game folder belongs to the player

The expensive rules, and the reason for most of the code in `[Code]`.

**Identify before replacing, and identify by content rather than by file name.** Three of the names
this installer writes are contested, because another project may already be using them in the same
folder: `dinput.dll`, `ddraw.dll` and `dsound.dll`. Our own files carry a marker string, so a file in
one of those slots that does not carry it belongs to somebody else and is moved aside, never
overwritten. Our loader chains to whatever held `dinput.dll` before, so overwriting that one would
destroy the thing the chain needs.

**Prefer a name nobody contests.** `winmm.dll` used to be a fourth, back when this installer
offered a controller wrapper installed under that name. It no longer installs a controller wrapper
of any kind, so that fourth name is gone rather than merely avoided: controller support reads a pad
directly and needs no DLL standing in front of another subsystem. Where a system name can be
avoided, avoid it; not needing the name at all is better still.

**A configuration file is never silently replaced.** `obi.ini` is the player's, so it is merged key
by key and never written whole. `engine_fixes.ini`, `dxwrapper.ini` and `alsoft.ini` are ours and
are replaced, with the previous file kept beside them under a `.previous` name. Those three
are not preference files: each names one section per component, and components are renamed between
releases, so an old file looks configured and behaves like defaults.

`dist/` holds everything the installer carries, which is everything it writes except the game. It
downloads nothing, during installation or afterwards. A component is added by putting its files in
`dist/<component>/` and pointing rows at them, never by fetching at run time: a download that works
today is a download that fails for somebody in five years, and this is a preservation project.

Keep each upstream component in a folder of its own, even where its files would otherwise sit beside
another's. `dist/patch/` is refreshed wholesale from a patch release, so anything else kept in it is
destroyed on the next bump; that is why DxWrapper has a folder to itself.

**Saved games are never deleted**, including by the uninstaller. Where the folder has to be laid down
fresh, the player's files are copied out first, copied back last, and counted both ways.

**Anything we did not install, we do not remove.** Inno records every file it wrote and the
uninstaller removes exactly those.

## Every external result is checked

* A helper process reports an exit code and the installer reads it.
* The installer downloads nothing. What it ships is in `dist/`, and Inno's own integrity check
  covers it. One tool it carries may download when a player runs it on its own, under the
  conditions set out in "What must not appear"; the installer never invokes it that way.
* A copy that had to succeed and did not stops the installation with a message naming the file.
* Work that can leave the machine half done happens before the first file is written, where stopping
  still costs nothing.

## The shape of the thing

`openphantom_installer.iss` is the entry point and owns the disc. Everything optional is an include
under `src/`, one file per subject, and the extractor is an ordinary C project under
`src/is3_extract`. `output/` holds the build and nothing in it is committed.

A line limit is the wrong instrument for a script that is mostly a table, so the measure here is
ownership: if you cannot say in one sentence what a script owns, split it. The C helper is ordinary C
and keeps to the usual limits.

## Rows and repetition

Component rows and file rows are written out, one per artefact, because Inno resolves component names
and local sources at compile time and a half finished rename then cannot ship. Never put
`skipifsourcedoesntexist` on a file that is supposed to be there; the flag is right only for a file
that genuinely varies between pressings, which is `VOICE.LAB` and the disc's `INSTALL` folder.

The one generated row set is the libVLC plugins. It was generated originally because the sources
lived inside an archive that did not exist until the installer ran, so there was no compile time
check to lose. That stopped being true when they moved into `dist/`, and the macro stays for the
reason that outlived it: libVLC finds a plugin by scanning, so each destination folder has to match
its source folder exactly, and deriving one from the other is what keeps thirty rows from drifting.
A plugin one folder over is not found, and the only symptom is `libvlc_new` refusing.

Otherwise: three occurrences of the same shape is the threshold, and consolidate when doing so makes
a property true rather than promised. Language idiom is not repetition; `ExpandConstant('{app}')`
stands everywhere because that is how Inno is written.

## Naming

* Pascal code follows Inno's own conventions: `PascalCase`, and a `T` prefix for types.
* C code is `snake_case`, with `UPPER_SNAKE_CASE` for macros and constants.
* Component names match the artefact they install, so `patch\decal_fix` installs `decal_fix.dll`.
  The exception is a fix that means nothing on its own: `effect_clock.dll` installs under
  `patch\framerate_fix`, because it only does anything once the frame rate is free.
* Filenames and directory names are `snake_case`.

## Comments

Say the fact and stop: where a number came from, what breaks in the other order, which branch is not
obvious from the code. One or two lines usually covers it, and if a comment needs three paragraphs,
say it once and delete the other two.

No document references. No section numbers, no paths to files outside this repository. A comment has
to stand on its own.

## What must not appear

* Absolute paths belonging to anyone's machine.
* A download by the installer, at install time or after it, for any reason.

  There is one bounded exception, and it is bounded rather than a hole. `dist/patch/` is also
  published as a patch archive on its own, and that archive carries no FFmpeg, so a player who
  installed the patch alone would have a cutscene converter with nothing to convert with. The
  reason this rule exists is preservation, and preservation is a promise the *installer* makes:
  it must still work in twenty years with no network. The patch archive never made it, and its
  user is already sourcing components themselves.

  A download in a tool that also ships in the patch archive alone is therefore allowed, if all
  three hold:

    * it is pinned to an exact URL and checked against a recorded sha256 and size before use,
      never a rolling "latest";
    * it is never the only route to the component, so a dead URL costs convenience and not
      capability;
    * it is refused outright when the installer is the caller.

  `tools\convert_movies.ps1` is the only such tool. The installer invokes it with `-NoDownload`
  and `-FFmpegPath`, naming the copy it carried, so the installer's own guarantee is unchanged.
  That second switch matters as much as the first: the script consults its `%LOCALAPPDATA%`
  cache before `PATH`, so without being told which binary to use it could prefer a copy left by
  an earlier run over the pinned one just installed, and run it elevated.
* A binary in `dist/` taken from a mirror when its author publishes one themselves. Where no such
  build exists, say so where the component is defined and name who built the one that ships.
* Loosening file system permissions beyond what the game needs to write its saves.
* A binary in `dist/` whose licence, version and upstream are not recorded in the third party
  notices, and whose corresponding source is not pinned there where its licence asks for it.
* Game data of any kind, in any form, for any reason.
* Silent failure of any kind.

## Before you open a pull request

* The script compiles with no warnings and the extractor builds 32 bit at `/W4 /WX` with none.
* You installed with it and started the game afterwards.
* You said which paths you exercised and which you did not.
* Both languages carry every message you added. A missing translation is not a compile error and
  shows up as an empty label.
