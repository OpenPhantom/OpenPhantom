# crash_report

**Produces:** `crash_report.dll` -> `mods\`

Writes a usable report when the process dies hard: exception code, address, module, registers, the
bytes around the faulting instruction and the engine frames found on the stack.

## Supported executables

Any 32-bit PE. It reads no engine address; it only needs the `.text` range to recognise engine
frames, which comes from the PE headers.

## Configuration: `[crash_report]`

| Key | Default | Meaning |
|---|---|---|
| `Enabled` | `1` | |

## What it installs

Two hands, because one is not enough:

1. **A vectored handler** sees the exception first, before any SEH frame, including a graphics
   wrapper's. It therefore fires even when something further out swallows the exception.
2. **`SetUnhandledExceptionFilter`** catches the case where the vectored handler did not run. The
   last installer wins here, so a wrapper loading after us replaces our filter, which is why the
   vectored handler is the important one of the two, not the other way round.

**It changes nothing.** Both paths return `CONTINUE_SEARCH` or hand on to the previous filter, so
the crash unfolds exactly as it would without us. A reporter that bends the control flow reports on
a different program than the one that crashed.

## How it survives its own report

A crash reporter runs in the least forgiving conditions in the process, so three things are
arranged deliberately rather than left to chance.

* **It cannot re-enter itself.** If the reporter faults, the vectored handler sees that exception
  exactly as it saw the first, and the reporter would recurse on an ever shorter stack until
  something else killed the process, leaving the log ending mid line. One interlocked guard is
  held across the whole body. Two threads faulting at once resolve the same way: the second is
  dropped, because the first crash is the one worth reading.
* **The registers are written before the module is named.** `GetModuleHandleEx` and
  `GetModuleFileName` both take the loader lock, and one of the three crashes this was written for
  hung inside a graphics wrapper cleanup, which is inside the loader holding that lock. Everything
  obtainable from the exception record and the context alone is therefore already in the file
  before anything reaches for it, so a deadlock there costs one line instead of the whole report.
* **The buffers are static, not automatic.** The guard above makes the body single threaded, so
  there is nothing to race, and roughly 550 bytes stay off a stack that may be nearly gone.

## Known limitations

* **The stack sweep is not a real stack walk.** That would need the unwind data of a 1999 MSVC
  build, which does not exist. The raw stack is swept for values that land in executable memory,
  which yields *candidates* for the call chain, stale ones included. That is why the stack offset
  is printed with each: the lowest offsets are the youngest frames and the most believable.
* **It sweeps for every module, not only the engine.** A frame inside `WMAIN` is marked `engine`;
  anything else is named by the base of the module holding it and its offset into it, and a short
  legend at the end resolves those bases to file names. The legend is last on purpose: naming a
  module needs `GetModuleFileName` and the loader lock, so if that deadlocks the report is already
  complete and only the names are missing.

  This was added after a crash whose entire call chain was in `d3d9.dll`, `USER32.dll` and the
  graphics driver, with no engine frame beneath it. The old sweep recognised `WMAIN` and nothing
  else, so it printed the frame loop, stopped, and read as a dead end; finding the DLL responsible
  took five rounds of disabling features by hand. The addresses were on the stack the whole time.
* At most four full reports per process; beyond that nothing new is said and a flood would bury
  the one entry that matters.
* **A first-chance access violation is not a report.** This project reads engine memory through
  the guarded readers in `common/memory.c`, which are SEH: they provoke access violations on
  purpose across 47 call sites and answer `false`. A vectored handler installed first sees every
  one of them, and the exception code is the same code a real crash carries, so filtering by code
  cannot tell them apart. Four recovered probes used to spend the whole budget above, leaving the
  reporter silent for the crash it exists to catch.

  Access violations therefore get one compact line per distinct faulting site, then a count, from
  a budget of their own; the full report for one comes from the unhandled filter, which runs only
  when nothing else took it. Every other fatal code still gets its report at first chance, because
  a `memcpy` cannot raise an illegal instruction or a divide by zero. The cost is that an access
  violation swallowed further out, while the process then hangs rather than dying, is one line
  instead of a report; that line still names the faulting address, what it touched and what it was
  doing, and the registers and stack sweep are what is given up.
* Only genuinely fatal codes are reported. Breakpoints, C++ throws (`0xE06D7363`) and the
  thread-naming exception are control flow, not crashes.
* On `EXCEPTION_STACK_OVERFLOW` the report itself needs stack, and it has only the single page
  Windows leaves after clearing the guard. That report is therefore deliberately smaller: the byte
  dump around the faulting instruction is skipped, since on an overflow that instruction is
  whichever one happened to touch the guard page rather than the bug, and the stack sweep is
  shortened to 512 bytes, since in a runaway recursion the repeating pattern of return addresses
  is already the answer. It still is not guaranteed to fit, but it is a great deal more likely to.

## Testing status

Built and linked, `/W4 /WX` clean. No unit tests: there is no isolated pure logic here; every
function either talks to the OS or formats a report.

**Accepted in game, on real crashes rather than induced ones.** It caught a repeatable fault on
three machines and produced the report that was used to diagnose it: the exception, the registers,
the stack extent, and the module each frame belonged to. The module naming is what turned a report
that read as a dead end into the one that named the fault, so that part is confirmed by having
done its job rather than by having been looked at.
