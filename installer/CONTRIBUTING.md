# Contributing

The rules this directory is held to. Most exist because breaking one cost somebody a day.

The repository's own `CONTRIBUTING.md` holds here too. `legacy/CONTRIBUTING.md` does not: it is a C
ruleset for patching a running binary, and nothing here places a detour or resolves a byte pattern.

## The game folder belongs to the player

The expensive rules, and the reason for most of the code in `[Code]`.

**Identify before replacing, and identify by content rather than by file name.** Four of the names
this installer writes are contested, because another project may already be using them in the same
folder: `dinput.dll`, `ddraw.dll`, `winmm.dll` and `dsound.dll`. Our own files carry a marker string,
so a file in one of those slots that does not carry it belongs to somebody else and is moved aside,
never overwritten. Our loader chains to whatever held `dinput.dll` before, so overwriting that one
would destroy the thing the chain needs.

**A configuration file is never silently replaced.** `obi.ini` is the player's, so it is merged key
by key and never written whole. `engine_fixes.ini`, `dxwrapper.ini`, `Xidi.ini` and `alsoft.ini` are
ours and are replaced, with the previous file kept beside them under a `.previous` name. Those four
are not preference files: each names one section per component, and components are renamed between
releases, so an old file looks configured and behaves like defaults.

`dist/` holds configuration this installer ships itself. Everything else it writes is downloaded, so
a file only belongs in `dist/` when no download owns it.

**Saved games are never deleted**, including by the uninstaller. Where the folder has to be laid down
fresh, the player's files are copied out first, copied back last, and counted both ways.

**Anything we did not install, we do not remove.** Inno records every file it wrote and the
uninstaller removes exactly those.

## Every external result is checked

* A helper process reports an exit code and the installer reads it.
* A download is verified by size and by hash before it is used.
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

The exception is rows Inno cannot check anyway. A source that is `external` and lives inside an
archive that does not exist until the installer runs buys no compile time check, so the libVLC plugin
rows are generated from one macro that derives each destination folder from its source folder. That
is the only generated row set here.

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
* A download that is not pinned by hash. Where the host is not the file's author, say so at the pin,
  because there is then no upstream checksum to compare against.
* A binary fetched from a mirror when its author publishes it themselves.
* Loosening file system permissions beyond what the game needs to write its saves.
* A shipped binary that is not either built from this repository or downloaded from its author.
* Game data of any kind, in any form, for any reason.
* Silent failure of any kind.

## Before you open a pull request

* The script compiles with no warnings and the extractor builds 32 bit at `/W4 /WX` with none.
* You installed with it and started the game afterwards.
* You said which paths you exercised and which you did not.
* Both languages carry every message you added. A missing translation is not a compile error and
  shows up as an empty label.
