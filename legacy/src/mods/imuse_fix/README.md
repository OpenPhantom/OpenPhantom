# imuse_fix

**Produces:** `imuse_fix.dll` -> `mods\`

The music pause, which this engine built and never wired to anything.

## What it does

**Pauses the music while the game is not in front.** The engine never does. Its window procedure
routes `WM_ACTIVATEAPP` to exactly two functions and both are about the mouse pointer; one warps
and captures it, the other releases it. Nothing in the audio path is reachable from a focus change
at all, so a game left running behind a browser keeps playing its soundtrack.

**Resumes a pause that nobody owns.** The music pause is a single latch, set by one function and
cleared by one function. If the latch is found set while the game is in front, the pause menu is
not up, and this DLL did not set it, then something paused the music and never came back. That
state is not silence: the music streams through a **looping** DirectSound buffer that the pause
never stops, so with nothing refilling it the buffer circles its last second of sound forever.

**Names every change**, on request, one line per transition.

## What it is NOT: the return-value patch

There is a well-travelled explanation of this game's music defect: the music module's command
handler leaves its result at `2` ("not handled") for `case 8` (pause) and `case 9` (resume) where
every other implemented case sets it to `0`, and a dispatcher pair in this engine gates its
suspend/resume bookkeeping on exactly that result, pause sets a "suspended" bit only when the
handler returned 0, and resume skips any module whose bit is clear. The chain is real and the
reading of those two dispatchers is correct.

**Those two dispatchers are never called.** No `call`, no `jmp`, and no stored pointer at either of
them anywhere in any shipped image, checked on both retail builds and on the Edit Tool's
recompiled `obi.exe`, where both functions have moved and are still unreferenced.

The live pause path is the pause menu. It uses the *other* dispatcher, which walks the module list,
calls every handler with the command and drops the result into a local nobody reads, so `ImPause`
and `ImResume` are balanced across it, and the handler's return value is not consulted at any point.
Patching those seven bytes changes nothing that executes.

The orphan guard is the honest version of the same worry. Instead of predicting how the latch might
get stuck it watches whether it *is* stuck, repairs it and says so, which means a log **without**
that line is evidence rather than silence.

## This is not known to fix the "one second on a loop" defect

The reported symptom, music suddenly hangs and repeats a short fragment endlessly while the game
carries on running normally, has **not** been traced to a cause. It is reported to occur without
any mods installed, so it is not something this project introduced. Three things are ruled out by
bytes rather than by argument:

* it is not the return value above (that path does not gate on it);
* it is not the per-frame music service failing to be sent, the pause menu is the only thing that
  suspends modules, and the frame that sends the service tick is the same frame that draws;
* it is not the module re-entrancy counter, which has 36 references image-wide and not one of them
  is a `cmp` or a `test`.

Set `MusicLog=1` for one session when it happens. If the guard's repair line appears, a stuck pause
was the cause after all and the mechanism that set it is worth finding. If it never appears while
the music is looping, the pause latch is innocent and the defect is inside `IMUSE.DLL` or below it
in the DirectSound path; this installation has DSOAL in the `dsound.dll` slot, which the engine
cannot see and this DLL cannot repair.

## Supported executables

Retail `WMAIN.EXE` (EN/DE), the Fix Pack build, and the Edit Tool's recompile. Every one of the five
patterns resolves uniquely in all three builds. In `obi.exe` all five engine cells have **moved**,
the music latch pair from `005BAB90/94` to `005BAB40/44` and the pause-menu latch from `006CCFE0` to
`006CCF90`, which is exactly why every address here is read out of a matched operand and none is
written down.

## Configuration: `[imuse_fix]`

| Key | Default | Range | Meaning |
|---|---|---|---|
| `Enabled` | `1` | | the master switch |
| `PauseMusicOnFocusLoss` | `1` | | pause while another application is in front, resume on the way back |
| `ResumeOrphanedPause` | `1` | | resume a pause with no owner. Switches itself **off** when the pause-menu latch cannot be resolved, because without it an owned pause and an orphan look the same |
| `OrphanGraceFrames` | `120` | 12-6000 | consecutive frames the ownerless state must persist first |
| `MusicLog` | `0` | | one line per **change** of the music state, capped at 400 a session |

## Engine locations

Nothing is patched and nothing is detoured. Five sites are located by signature; two are **called**,
five cells are **read**, and no byte of the image is written.

| Site | What it gives |
|---|---|
| `bapMusicPause` | called. The only caller of `ImPause` in the image |
| `bapMusicResume` | called. The only caller of `ImResume` |
| `bapMusicPeriodic` | read: the "music system is up" flag |
| the two cue getters | read: the state and sequence latches, for the log only |
| `sys_pause` | read: the pause menu's own latch, which is what makes an owned pause distinguishable from an orphan |

Cross-checks before any of it is believed: the pause latch is named by two independent patterns, the
attached flag by three, and the re-entrancy counter by three. A disagreement refuses the whole
install rather than picking a winner. Every resolved cell must also lie inside the host image and be
readable.

Neither `bapMusicPause` nor `bapMusicResume` checks whether the music system is up; both call into
`IMUSE.DLL` unconditionally. Nothing here calls either of them unless the attached flag reads
exactly `1`, which is the same test the engine's own per-frame service uses.

## Fallback behaviour

| What fails | What happens |
|---|---|
| the pause/resume pair does not resolve | nothing installs; the game keeps the behaviour it shipped with |
| `bapMusicPeriodic` does not resolve | nothing installs, without the attached flag there is no safe moment to call into the music DLL |
| the cue getters do not resolve | everything still works; the log can name every state change but not which cue was in force |
| `sys_pause` does not resolve | the orphan guard switches **off** and says so; the focus half still works |
| two patterns disagree about a cell | nothing installs, and the log names the two values |
| the frame hook cannot be installed | nothing installs. There is no degraded mode: both halves are decisions taken from state that only means anything when sampled every frame |

## Patching code another thread may be running

iMUSE runs its heartbeat from a WINMM timer thread, and that thread calls `ImLock`. The atomic
increment is written over code that thread may be executing. Installing before audio starts was
the assumption that made this safe, and until now it was only an assumption: nothing checked it
and nothing said it.

It matters for `ImLock` specifically because the patch **moves an instruction boundary**. The
`lock` prefix costs a byte, so `ret` shifts from `+6` to `+7`; a thread parked at `+6`, about to
return, would resume inside the address operand and run whatever it decoded as. Every other
thread is therefore suspended and asked where its instruction pointer is, and the write happens
only when none of them is inside the function. If one is, they are resumed and it is retried;
after eight attempts the patch declines, and a declined lock is rolled back exactly as any other
failure is.

Nothing is allocated while threads are suspended, which is why the `ImUnlock` detour is installed
outside that window: `detour_install` builds a trampoline with `VirtualAlloc`, and taking the
address space lock while holding threads still is a worse trade than what it would buy. `ImUnlock`
does not need the window anyway, because its patch puts a five byte `jmp` where a five byte `mov`
was and no boundary moves.

## Known limitations

* **The pause is the engine's own, so it is as coarse as the engine's own.** There is no fade and no
  volume ramp; the music stops and starts. Adding a fade would mean owning the volume path, which
  belongs to the audio options screen.
* **Sound effects are not touched.** Only music is paused on focus loss. The effects go through a
  different layer, and pausing them would need a second mechanism with its own risks.
* **The orphan guard can only ever resume.** It will not notice a pause that *should* have happened
  and did not.

## Testing status

Built with `/W4 /WX`, zero warnings. All five patterns verified offline against all three builds:
every pattern resolves with the expected match count on both retail builds.

**Seen in the game once, and it did nothing.** The first build reached the game and reported
*"NOT RESOLVED (0 matches)"* for all five sites, because `imuse_fix_install` did not call
`host_image_resolve()`, `common/` is a static library, so each DLL owns that state and the loader
resolving it does not carry over. The scanner searched an empty range. The refusal itself was
correct and the game was left untouched, which is precisely why it was not obvious. Fixed; **the
corrected build has still not been observed doing anything**, and no claim is made that it repairs
the looping-music defect, see the warning above.
