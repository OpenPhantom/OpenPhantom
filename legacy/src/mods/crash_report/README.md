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
  build, which does not exist. The raw stack is swept for values landing in `WMAIN`'s `.text`, which
  yields *candidates* for the call chain, stale ones included. That is why the stack offset is
  printed with each: the lowest offsets are the youngest frames and the most believable.
* At most four reports per process; beyond that nothing new is said and a flood would bury the one
  entry that matters.
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
function either talks to the OS or formats a report. **Not accepted in game.**
