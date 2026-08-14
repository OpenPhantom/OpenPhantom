# Contributing to OpenPhantom

Thanks for looking. OpenPhantom is a reverse engineering and preservation project around the PC
version of *Star Wars Episode I: The Phantom Menace*, and it is the kind of project that gets
better mainly through people digging into details nobody has looked at yet.

Contributions of every size are welcome: code, documentation, tests, tooling, research notes, or
just a good bug report. You do not need to know the engine to help.

## The one hard rule

**No game content, ever.** No executables, no assets, no extracted data, no patched binaries, not
in a commit, not in a release, not in an issue attachment. Everything here operates on files the
user already owns. A pull request that adds game data will be closed regardless of how good the
rest of it is, and if something slips through, say so immediately so it can be removed from the
history rather than quietly left there.

This is not paperwork. It is the difference between a project that survives and one that does not.

## Reporting something

A useful bug report has three parts: what you did, what happened, and what you expected instead.
For anything to do with the engine fixes, attach `engine_fixes.log` from next to `WMAIN.EXE`. It
records which patches installed, which declined and why, so it usually answers the first three
questions somebody would otherwise have to ask you.

Also worth mentioning if you have them: your game version, your graphics wrapper if any
(dgVoodoo2, DDrawCompat and so on), and your resolution. Most reports that turn out to be
environmental turn out to be one of those three.

If you are not sure whether something is a bug or the game being the game, open the issue anyway.
This engine has plenty of authentic behaviour that looks like a defect, and working out which is
which is useful in itself.

## Repository layout

```
legacy/          Fixes that patch the original 1999 executable in memory.
                 Self contained: its own CMake project, its own tests, its own rules.
engine/          The reimplementation.
editor/          Tools for maps, assets and game content.
architecture/    How the original engine is put together, written down.
installer/       Packaging and setup.
```

`legacy/` and `installer/` have something in them. The rest are placeholders, and that is worth
knowing before you plan a large contribution: if you want to start one of them, open an issue first
so the shape can be agreed before anybody writes a thousand lines.

Both components that exist are laid out the same way: an entry point and the documents at the top,
sources under `src/`, and an output directory that git keeps but never fills.

Each component owns its build, its tests and its coding rules. Where a component has a
`CONTRIBUTING.md` of its own, that one wins for anything inside it, and the document you are
reading covers what holds everywhere.

Two exist today, and they are deliberately not versions of each other:

* `legacy/CONTRIBUTING.md` is the C ruleset for the engine fixes: file sizes, how engine code is
  located, what a hook may and may not do, and how comments carry the byte level evidence.
* `installer/CONTRIBUTING.md` is about writing into a folder that belongs to somebody else: what may
  be replaced and on what evidence, what is never deleted, and why every external result is checked.

Read the one that covers what you are touching. A component ruleset answers the questions that
component actually raises, so do not carry a rule across from the other because it sounds strict.
Most of the C rules have no subject in an installer script, and the installer's rules about a
player's existing files have none in a DLL.

## How we work with the engine

This part is the character of the project and it applies to any component that touches the
original binaries.

**Evidence beats inference.** A claim about what the engine does should be traceable to bytes, to
a data census, or to something observed in the running game. Say which. "The engine does X" and
"the engine appears to do X, from the disassembly at this address" are different statements and
the second one is fine. The one to avoid is the first when you only had grounds for the second.

**Say what you actually verified.** Compiled, unit tested, and played are three different claims.
Write down which one you mean. "Reviewed statically, not run in the game" is a useful sentence in
a pull request; "works fine" is not, because the reader cannot tell what it rests on.

**A negative is only as good as the search behind it.** "Nothing else reads that value" is a claim
about a whole binary, so it needs a whole binary sweep, not a look at the two call sites you
happened to open. If you did not do the sweep, say the weaker thing you can support.

**Prefer the smallest change that fixes the cause.** This engine is full of places where a symptom
can be compensated for somewhere convenient. That kind of fix usually holds until the next
configuration and then fails in a way nobody can trace back. Find the thing that is actually
wrong, even when the patch ends up being one byte.

**Authentic is not a bug.** The goal is a game that runs properly on modern hardware, not a
modernised game. Where the original look or feel is deliberate, it stays, and anything that
changes how the game plays gets a switch and a default that leaves it alone.

## Pull requests

Keep a pull request to one subject. A rename, a refactor and a fix in one branch is three times
harder to review and three times harder to revert.

Before you open it:

* the component builds with no new warnings
* its tests pass
* you have described what you tested and what you did not
* documentation next to the change is updated in the same commit, not in a follow up

Commit messages: a short summary line saying what changed and why, in the present tense, then as
much detail as the change deserves. Long is fine. Nothing is worse to inherit than a subtle
one line change explained as "fix stuff".

Write in English, everywhere: code, comments, log messages, commit messages and documentation.

## Reviews

Reviews here are technical and direct, and that is not unfriendliness. Expect to be asked where a
number came from, what happens on the path you did not mention, and how you know. Answering "I do
not know, I assumed" is a completely acceptable answer and is more useful than a confident guess.

If you disagree with a review comment, say so and why. Being talked out of a change is a normal
outcome and so is talking somebody else out of one.

## If you want something to do

* Reproduce an open issue and add what you find. A confirmed report with a log attached is worth
  more than it sounds.
* Take one of the fixes in `legacy/` that is marked as not yet tested in the game and test it.
  Each feature README says what to look for. This is the single most useful thing right now,
  because almost everything there is verified offline and only three fixes have been confirmed in
  actual play.
* Improve a test. Most of the arithmetic is covered; most of the failure paths are not.
* Documentation. If something took you an hour to work out from the source, that hour is worth
  writing down for the next person.

## Licence

The project is MIT licensed and contributions are accepted under the same terms. The licence
covers this source code only. It grants nothing whatsoever regarding the original game, its
assets, or anything else its rights holders own.
