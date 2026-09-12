# sfx_volume_save_fix

**Produces:** `sfx_volume_save_fix.dll` -> `mods\`

The SFX volume slider resets to (about) full on every reload, no matter what it was set to when
the game last closed. Two independent bugs, both fixed here.

A third bug lives in the same screen and is fixed here too, because it is the same slider: both
volume sliders lose 7 of 127 every time the audio screen is opened. That one takes the music
slider with it, since the two share the code that drifts. See **The sliders walk downward** below.

## Supported executables

Any build whose audio code matches the retail sites at `0x00417459` / `0x0041738D` /
`0x00417379`. The sites resolve by pattern; if any does not match, that part of the DLL changes
nothing and says so in the log.

Two of the three patterns carry absolute data addresses, because those addresses are the sites'
own operands and the patterns are only unique with them in. The consequence: a build that relinked
its data section fails those two and the DLL declines rather than guessing. Measured on every
retail image to hand, including the German one, all three resolve exactly once; on the Edit Tool's
own recompile of the engine only the address-free middle pattern resolves, and the DLL declines
with a log line, the intended answer.

## Configuration: `[sfx_volume_save_fix]`

| Key | Default | Meaning |
|---|---|---|
| `Enabled` | `1` | `0` installs nothing and the log says so |

## Bug 1: the saved value was wrong

`options_audio` (`0x00441FA4`, the audio options screen) has exactly one function it calls to find
"what is the SFX volume right now": `bapsound_getMasterVolume`, `0x00417459`.

```
0041745C  83 3D B8 B4 5B 00 00   cmp  dword ptr [g_soundReady], 0
00417463  75 04                  jnz  ...
00417465  33 C0                  xor  eax,eax
00417469  A1 98 AE 5B 00         mov  eax,[g_digitalDeviceHandle]
0041746E  50                     push eax
0041746F  FF 15 58 17 8C 00      call dword ptr [IAT: _AIL_digital_master_volume]
```

It asks **Miles** for the digital device's current master volume instead of reading back
whatever the engine itself last set, and that round-trip does not reliably reflect the value
just pushed with `_AIL_set_digital_master_volume`.

This function has exactly two callers, and both are inside `options_audio`: an `E8` sweep of the
whole `.text` finds two call sites, `0x004420C1` and `0x004428C2`, and the next function entry
after `0x00441FA4` is `0x00442A98`, so both lie inside that one screen. They are
the two things the screen does with the number: seeding the slider widget when it opens, and
building the value written to `obi.ini`'s `SVOL` key when it closes.

The engine already keeps a reliable copy of the true value. `bapsound_setMasterVolume`
(`0x00417379`), the function that **drives the slider live**, writes it two instructions in:

```
0041738D  DB 45 08               fild dword ptr [ebp+0x08]          ; the int 0..127 argument
00417390  D8 35 50 81 4A 00      fdiv float ptr [g_sfxVolumeScale]  ; -> 0x004A8150 (127.0)
00417396  D9 1D 70 A9 4A 00      fstp float ptr [g_sfxMasterVolume] ; -> 0x004AA970
```

`[0x004AA970]` is not a write-only shadow: the per-channel attenuation at `0x004169BD`, which runs
on every sample start, reads it back, `base * scale * g_sfxMasterVolume`, so this cell has to
stay correct for in-game volume to be right at all.

**Fix:** `bapsound_getMasterVolume` is entirely replaced (not wrapped: calling through to the AIL
query first would just reintroduce the bug) with a detour that computes the same 0..127 integer
the engine itself derived the mirror from, clamped to `[0, scale]`. Both data addresses are read
out of `bapsound_setMasterVolume`'s own instruction stream rather than hardcoded, and range
checked against the host image before they are followed.

**The replacement keeps the original's own guard branch.** That is not a detail. The function
answers `0`, not a volume, while `g_soundReady` is still `0`. A replacement that skipped that
branch would answer with the mirror instead, and the mirror reads `1.0` at that point for exactly
the reason bug 2 describes, so on a machine whose sound never initialises, the options screen
would seed its slider at full and write `SVOL=127` over the player's saved value. That is the very
symptom this DLL exists to remove, reintroduced for the no-sound case.

**This bug is real and was confirmed in game** with a temporary diagnostic build: dragging the
slider to 33 and closing the menu correctly produced `SVOL=33` in `obi.ini`. But fixing it alone
did **not** fix "resets on reload"; that symptom survived unchanged and led to bug 2.

## Bug 2: the loaded value was never applied (the actual cause of "resets to full on reload")

`bapsound_moduleInit` (`0x004159F0`, runs once at startup) does this, in exactly this order:

```c
ini_read_int_alt("SVOL", 127, &loaded);   // reads obi.ini correctly
bapsound_setMasterVolume(loaded);         // <-- tries to apply it, from 0x00415A78
...
g_soundReady = 1;                         // set AFTER the call above, not before
```

`bapsound_setMasterVolume`'s entire body is gated on that same flag:

```
0041737F  83 3D B8 B4 5B 00 00   cmp dword ptr [g_soundReady], 0
00417386  75 05                  jnz +5      ; only THEN does fild/fdiv/fstp run
0041738B  E9 C8 00 00 00         jmp <exit, does nothing>
```

At the exact moment `bapsound_moduleInit` calls the setter with the value it just loaded,
`g_soundReady` is **still 0**; it is not set to `1` until several instructions later, in the same
function. The load-time apply is therefore a **guaranteed silent no-op on every single launch**,
regardless of what `SVOL` says in the file. The mirror simply keeps its compiled-in startup value
(measured as `1.0`, i.e. full) until the player manually touches the slider. One statement in the
wrong place in the original 1999 code, and it is the actual cause of the reported symptom.

The setter's own caller census says the same thing: two `E8` call sites in the whole image,
`0x00415A78` inside `bapsound_moduleInit` and `0x0044249C` inside `options_audio`. One start-up
apply that cannot work, and one live slider that can.

**Confirmed** with a temporary diagnostic build across two separate sessions: the very first call
to `bapsound_setMasterVolume` in each run showed the mirror ending up at `1.0` regardless of the
argument passed in (`120` in one run, `0` in the other), exactly what "the guard blocked the
write and the mirror kept its old value" looks like from outside the function.

**Fix:** `bapsound_setMasterVolume` is tapped (not replaced: the live path must keep working
unchanged). If it is called while `g_soundReady` is still `0`, the intended value is remembered
instead of lost. A per-frame check (`common/frame_hook.h`, the same "call me once per rendered
frame" site every other feature in this tree uses for a live slider preview) re-applies that
value the instant `g_soundReady` actually becomes `1`, which happens a handful of instructions
later in the very same function, so in practice this resolves within the same frame `sys_frame`
is next pumped. Nothing about live control changes; this only rescues the one call the original
code was never going to honour.

## Engine locations

| Site | Retail VA | What |
|---|---|---|
| `bapsound_getMasterVolume` | `0x00417459` | detoured; entire body replaced, both branches (bug 1) |
| `bapsound_setMasterVolume` | `0x00417379` | tapped; live behaviour unchanged, startup no-op rescued (bug 2) |
| `bapsound_setMasterVolume` (`fild`/`fdiv`/`fstp`) | `0x0041738D` | read only; supplies scale/mirror addresses |
| `g_sfxMasterVolume` | `0x004AA970` | read only; the engine's own live volume, `0..1` |
| `g_sfxVolumeScale` | `0x004A8150` | read only; the `0..127` conversion factor |
| `g_soundReady` | `0x005BB4B8` | read only; taken from both sites' own guard operands and cross-checked, not hardcoded |
| `bapsound_moduleInit` | `0x004159F0` | not touched; the fix works around it rather than editing its instruction order |
| `options_audio` | `0x00441FA4` | not touched |

## A slider notch is not the test

The menu's own arithmetic truncates in both directions: the slider seeds from
`trunc(volume / 127 * 19)` and a drag writes back `trunc(notch / 19 * 127)`. Notch 5 gives
`SVOL=33`, and in the shipped engine 33 seeds back to `trunc(4.937)`, which is notch 4; only 0 and
19 survived the round trip exactly. This DLL now rounds the seed (**The sliders walk downward**
below), so 33 seeds to notch 5 again, but nineteen notches are still a coarse reading of a
`0..127` value, so **check `SVOL=` in `obi.ini` and the log, never the slider's position.**

## Testing status

All three patterns measured against every retail `WMAIN.EXE` available, including the German
build: one match each, at the addresses this file names. The `master_get` prologue alone matches
twice (`0x00417459` and `0x0041778C`), so the pattern is the whole 30-byte body.

Bug 1 (wrong saved value) was confirmed fixed in game before the guard branch was restored. Bug 2
(dropped load-time apply) was diagnosed from two real runs' logs and fixed per the analysis
above. **Both the restored guard branch and the bug-2 re-apply are accepted in game**, in the
v0.4.1 build, which was played through by hand.

The log line to look for is `startup SFX volume (N) applied`. The actual test is whether the value
in `obi.ini`'s `SVOL` is the value the game starts at on the very next launch.

## Why this is SFX and not a general save or load problem

Music volume is unaffected by either bug. It round-trips through a simple engine-side float with
no driver query and no ordering dependency on a "ready" flag. That pointed at these two
SFX-specific sites rather than at the settings file or the code that reads it.

## The sliders walk downward

Opening the audio screen costs 7 of 127 on the SFX slider, and the equivalent on the music one,
without anything being touched. Four visits take 60 down to about 32. The reporter of this
described it as a slider not being where they left it, which is how it looks from the outside.
It is also why the fault seemed to follow the 3-D provider list around: the provider row is simply
the thing people click on that screen.

### Nineteen steps, truncated twice

`[0x004A8634]` is `19.0f`, the number of steps the slider widget has. The screen seeds the widget
from the live volume when it opens, and reads the widget back when it moves:

    seed      widget = ftol(volume / 127.0f * 19.0f)     0x004420B1 music, 0x004420E1 sfx
    readback  volume = ftol(widget / 19.0f * 127.0f)

`__ftol` truncates toward zero. Neither step rounds, so the value can only ever fall:

| SVOL in | widget | SVOL out | lost |
|---|---|---|---|
| 127 | 19 | 127 | 0 |
| 106 | 15 | 100 | 7 |
| 100 | 14 | 93 | 7 |
| 60 | 8 | 53 | 7 |
| 33 | 4 | 26 | 7 |

Those are not worked examples. `106`, `100`, `93`, `60`, `53` and `33` are the values a field log
recorded, in that order, over two sessions of a player opening the screen and touching only the
provider list. Only full volume is stable.

Music takes the same path through a 100 scale, so `0.47` seeds `8.93`, truncates to 8, returns as
`0.421` and writes `MVOL=42`. A 47 becomes a 42 with nothing touched.

### Rounding the seed is the whole fix

`8.93` becomes 9, which reads back as `60.16`, truncates to 60, and the value is stable. Checked
across the range: every value holds except 1, which has nowhere to sit among nineteen steps and
becomes 0. Rounding the readback as well would be a second write for nothing, because by then the
seed has already made the value representable.

### Why two call redirects and not a detour

`__ftol` is the compiler's own helper and the image calls it from everywhere, so rounding inside it
would change every float-to-int conversion in the game. Only the two seed calls are redirected, to
a thunk that adds a half and falls into the real helper. The helper is read out of the displacement
being replaced rather than resolved separately, so a wrapper somebody else had already installed
still runs. Both displacements are read and compared before either is written, and if they name
different helpers nothing is touched.

### Engine locations

| What | Where |
|---|---|
| the music seed's `call __ftol` | `0x004420B1`, displacement rewritten |
| the sfx seed's `call __ftol` | `0x004420E1`, displacement rewritten |
| `__ftol` | `0x0049A44C`, read from those displacements, never modified |
| the slider step count | `[0x004A8634]`, `19.0f`, read only |

### Testing status

Played. Before, the log read `get 60` then `set 53` one line later, the widget answering the seed
with a different number; after, the `get 60` stands alone and `obi.ini` keeps `SVOL=60` across
repeated visits and provider changes. Confirmed with the music slider in the same session.
