# Contributing

These are the rules this directory is held to. Most of them exist because breaking them cost
somebody a day, and the ones that sound fussy are usually the ones that did.

Patches are welcome. If something here gets in your way, say so in the pull request rather than
working around it quietly.

## The shape of the thing

**One DLL per independent fix.** Each gets a directory, and the directory name is the DLL name.
A DLL contains only what belongs to that feature. Closely related parts stay together, so
`variable_fov` may own the horizontal FOV, the vertical FOV, the projection rebuild and its own
slider, but it has no business touching audio or save games.

**Feature DLLs never depend on each other at run time.** Shared code is a static library linked
into each of them. There is no runtime DLL that everything else needs, so deleting one feature
cannot break another. It also means each DLL carries its own copy of the shared state: if your
install function does not call `log_init` and `host_image_resolve` itself, another DLL having done
so earlier does nothing for you.

**Those two calls come first, before anything else:**

```c
void my_fix_install(void)
{
    log_init("my_fix", false);
    if (!host_image_resolve()) {
        log_error("no 32-bit host image");
        return;
    }
    ...
}
```

Both failures are silent and both lie about their cause. Without the first, every line the DLL
writes is dropped, including the warnings that would name the problem, and the log shows only the
loader's "calling engine_fix_install" line, which reads like a crash. Without the second the
signature scanner searches an empty range, so every pattern comes back with zero matches, which
reads exactly like an unsupported executable. That combination cost two full test rounds once.

**The tree mirrors the installation.** `src/common` is the static library, `src/loader` builds the
`dinput.dll` that sits beside the executable, and every directory under `src/mods` builds one DLL
into `mods\`. If you know where a file is, you know where its output goes.

Inside a feature directory the layout is flat and headers sit next to their source. A small fix is
`dll_main.c`, `feature.c`, `feature.h`, and it grows a fourth file when there is a real second
responsibility, not because a state struct exists. Do not add `include/`, `public/`, `private/` or
another `src/` below that level; nothing here needs them.

Each component owns its `CMakeLists.txt`. The root file holds the project settings and the compiler
flags and nothing else, so adding a fix is a new directory plus one `add_engine_fix()` call next to
the existing ones.

**Reach the shared layer through the include root:**

```c
#include "common/logging.h"          /* yes */
#include "../../common/logging.h"    /* no */
```

`src` is on the include path, so no file counts directory levels to find a header.

## Finding engine code

**Signatures, never addresses.** Three builds of this engine ship inside one installation, all
829,952 bytes, and one of them is a recompile in which 60% of the code section differs. An address
table would have written silently into a different function. Use `common/signature.c`.

**Uniqueness is a requirement.** A pattern that matches zero times or more than once disables that
one patch and logs it. It never guesses. Where a site is deliberately not unique, the key is the
triple of address free pattern, expected match count, and the address read out of the matched
operand; `signature_count_matches()` serves that case.

**Read addresses out of operands rather than embedding them.** That keeps a pattern working under
forced ASLR and after another patch has edited a nearby immediate.

**A pattern for a detour target must not contain the bytes the detour overwrites.** The first DLL
to install replaces the prologue with a jump. Every later DLL then searches for a pattern that
begins with that prologue, finds nothing, and switches itself off. It happened on the first real
run: four DLLs wanted `render_frameEnd` and only one got it. Use `SIGNATURE_ENTRY_DETOUR`, which
falls back to the pattern's tail and proves the head is either the authored prologue or a branch.

## Writing into a live process

Every write goes through `common/patch.c`. Do not reimplement `VirtualProtect`, protection
restoration, instruction cache flushing or readable range checks in feature code.

**Validate the whole range you are about to touch, not just its first address.** Then read it back
and refuse if it is not what you expected. That single habit is also what makes patches
idempotent: a second run finds the new value rather than the expected old one and declines.

**An unknown build must fail safely.** Never patch optimistically. A partially installed feature
must stay inactive, and a failure after earlier writes should roll those writes back where the
patch system supports it.

**Log the branch, not only the result.** A silent exit is a blind spot. If a plausibility limit
rejects something, it must say so rather than skip quietly.

**Compute from the remembered original, never from the current value.** `baplight_applyLevelFog`
has two callers, and without a remembered original the scale squares itself on the second run.

**`memory_read_*` and `memory_is_readable_range` belong in installation code and in code that runs
at human rates, never in a path the engine drives per object or per frame.** They call
`VirtualQuery`, which is a system call, and `memory_read` validates the range again underneath, so
a guarded pointer read costs two of them rather than one. Use `memory_try_read` or
`memory_try_readable` on any path the engine drives; a structured-exception frame is a few
instructions of setup on x86, and it is also stricter, since it catches a fault anywhere in the
range rather than trusting a walk done a moment earlier.

This rule is written down because breaking it cost real time twice. A guard on
`bapmap_tickMover`, which looks like draw-path frequency, was measured at 3,400 calls per frame
during combat near a lift and took the game from 60 fps to 8.5; the stall was attributed to the
engine for weeks and had an entire DLL built to compensate for it. `face_latch.c` had the same
defect more mildly. `render_guard.c` had already stated the rule in a struct comment and was the
only file that got it right, which is the argument for it living here instead.

**Ask the cheap question first.** If an expensive walk feeds a test that will reject on a counter
or a flag, do that test before the walk rather than after. `face_latch.c` walked four engine
structures on every poll to answer a question its own throttle then discarded fifteen times out of
sixteen.

## Hooks

Hooks stay small: check whether the feature is active, prepare state, call the original, finalise,
return the original's result. Calculations and persistence belong in ordinary functions.

Calling conventions come from reverse engineering evidence, never from a guess. Get one wrong and
the stack is corrupted at a point nowhere near the symptom.

`common/detour.c` chains, and that is the whole reason it exists rather than a vendored library.
When you place a detour on a function another DLL may also want, your `original` may be that DLL's
hook rather than the engine. Call it exactly as if it were the real function and the chain unwinds
correctly whatever order the DLLs loaded in. There is no uninstall, so a detour you place stands
for the life of the process, and an install sequence that can fail halfway has to abandon the whole
feature rather than leave live hooks behind.

## Comments

**English only, everywhere: code, comments, log messages, commit messages.**

Comments explain why, not what.

```c
/* The engine ignores text widgets whose state is negative. */
widget->state = 0;
```

Every hard coded address, offset, opcode, structure field and unusual constant needs an
explanation next to it. That is why several files here are long: the code is short and the byte
level evidence is not. Do not delete that evidence to get a file under a limit.

**The evidence lives in the comments, and there is no `notes/` directory.** The disassembly, the
census counts and the approaches that were tried and refuted belong in the file itself, next to
the code they explain. A source file whose evidence was left somewhere else arrives here looking
finished and is not, and nothing will tell you it happened.

The reverse also matters. A file here may already hold evidence you are about to overwrite, so
read it first. Keep what is still true and correct what is not: a claim that has since been
refuted and is carried forward out of caution is worse than one deleted on purpose.

**No document references in comments.** No section numbers, no paths to files outside this
repository. A comment has to stand on its own, because a reference is a promise that the reader
has the other file open, and it rots the moment something is renumbered.

**A comment that no longer matches the code is a bug.** Fix it or delete it in the same change.

## File and function size

* Prefer files under 400 lines. Between 400 and 600, look for a seam.
* Over 600 lines needs a stated reason in a `SIZE NOTE` in the file header.
* **900 lines is a hard limit**, comments and whitespace included.

Check it before you push:

```sh
find src unittests -name '*.c' -o -name '*.h' | while read f; do
    n=$(wc -l < "$f")
    [ "$n" -gt 600 ] && { printf '%4d %s ' "$n" "$f"; grep -q 'SIZE NOTE' "$f" && echo ok || echo MISSING; }
done
```

If a `SIZE NOTE` quotes an exact line count, that number has to be right. Prefer not quoting one:
"a little over 600 lines" cannot go stale, and an exact figure kept by hand always does. A file
claiming "895 lines, five to spare" while it actually sat at 1139 did not merely fail to warn, it
granted permission, and the same happened to the summary table that was supposed to catch it.

Do not get under the limit by deleting comments, packing statements onto one line, hiding code in
macros or headers, or moving unrelated things into `common`. Move a whole responsibility out
instead. When you do, say in the note which seam you took and, if you measured one and rejected it,
say that too.

Functions: prefer under 50 lines, review at 100, split above 150. Split when responsibilities are
mixed, not because a readable function is slightly long. Use early returns and avoid deep nesting.

## Style

`snake_case` for functions, variables, fields, typedefs, files and directories.
`UPPER_SNAKE_CASE` for macros, constants, widget IDs, offsets and expected opcodes.
Four spaces, no tabs, braces on every control flow block, lines preferably under 100 characters.

Explicit width types for anything binary facing, and `_Static_assert` for every layout assumption:

```c
_Static_assert(sizeof(sw_widget_t) == 0x38, "Unexpected sw_widget_t size");
_Static_assert(offsetof(sw_widget_t, rect) == 0x20, "Unexpected rect offset");
```

If the engine stores a boolean as a 32 bit integer, use `int32_t`. Binary compatibility beats
modernisation; never reorder a binary structure for readability.

No `sprintf`, `strcpy`, `strcat` or `gets`. Use the bounded forms and guarantee termination
yourself, because the truncating ones do not.

Warnings are errors here (`/W4 /WX`). Integer and pointer truncation, signed and unsigned
conversions and incompatible function pointers are exactly the mistakes that stay invisible until
the game crashes. A necessary suppression is local, documented and restored immediately.

## Tests

Anything that is pure arithmetic should be testable without the game, and most of the interesting
arithmetic here is. Tests are plain console programs under `unittests/`, one per module, driven by
ctest.

```c
#include "unittest.h"

int main(void)
{
    ut_section("the damper");
    ut_check(step > 0.0f, "a positive gap turns the body toward the target");
    ut_near(vertical_fov(4.0f / 3.0f), 60.0f, 0.001f, "4:3 gives the authored 60 degrees");

    return ut_summary("strafe walk");
}
```

Adding one is a single line in `unittests/CMakeLists.txt`:

```cmake
add_unit_test(<module> <directory under src/mods, or "" for common> [extra sources...])
```

Build the test against the **real** module, not a stub of it. A stub only proves the stub.

Write the check text as a claim about the code rather than as a label: "an empty list is refused",
not "test empty list". Where the check is the only place a byte level assumption is written down in
English, that sentence is the documentation.

Cover the boundaries: minimum, maximum, one past each, rounding edges, invalid configuration, and
NaN and infinity wherever a float can carry them.

Be precise about what has been shown. "It compiles" is not "it is tested", and neither is "the
unit tests pass" the same as "it works in the game". Say which one you mean.

## Before you open a pull request

* `cmake --build build --config Release` with no warnings
* `ctest --test-dir build -C Release` green
* the size check above, clean
* the game actually started with your DLL in `mods\`, and `engine_fixes.log` read afterwards. Every
  fix logs which sites resolved and which branch it took, so this catches a patch that installed
  and then did nothing, which is the failure mode that looks most like success
* the feature's `README.md` updated: configuration keys, engine locations touched, limitations,
  and what you actually tested
* `dist/engine_fixes.ini` updated if you added a setting, with the comment that explains it

Say plainly what you could not verify. "Reviewed statically, not run in the game" is a useful
sentence; "everything works" is not.
