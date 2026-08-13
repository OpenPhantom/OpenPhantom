# sfx_volume_save_fix

**Produces:** `sfx_volume_save_fix.dll` -> `mods\`

The SFX volume slider resets to (about) full on every reload, no matter what it was set to when
the game last closed. Two independent bugs, both fixed here.

## Supported executables

Any build whose audio code matches the retail sites at `0x00417459` / `0x0041738D` /
`0x00417379`. The sites resolve by pattern; if any does not match, that part of the DLL changes
nothing and says so in the log.

Two of the three patterns carry absolute data addresses, because those addresses are the sites'
own operands and are what makes the patterns unique. The consequence is worth stating: a build
that relinked its data section fails those two and the DLL declines rather than guessing. Measured
on every retail image to hand, including the German one, all three resolve exactly once; on the
Edit Tool's own recompile of the engine only the address-free middle pattern resolves, and the DLL
declines with a log line, which is the intended answer.

## Configuration: `[sfx_volume_save_fix]`

| Key | Default | Meaning |
|---|---|---|
| `Enabled` | `1` | |

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
whole `.text` finds call sites at `0x004420C1` and `0x004428C2` and nothing else, and the next
function entry after `0x00441FA4` is `0x00442A98`, so both lie inside that one screen. They are
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
on every sample start, reads it back - `base * scale * g_sfxMasterVolume` - so this cell has to
stay correct for in-game volume to be right at all.

**Fix:** `bapsound_getMasterVolume` is entirely replaced (not wrapped - calling through to the AIL
query first would just reintroduce the bug) with a detour that computes the same 0..127 integer
the engine itself derived the mirror from, clamped to `[0, scale]`. Both data addresses are read
out of `bapsound_setMasterVolume`'s own instruction stream rather than hardcoded, and range
checked against the host image before they are followed.

**The replacement keeps the original's own guard branch,** and that is not a detail. The function
answers `0`, not a volume, while `g_soundReady` is still `0`. A replacement that skipped that
branch would answer with the mirror instead, and the mirror reads `1.0` at that point for exactly
the reason bug 2 describes - so on a machine whose sound never initialises, the options screen
would seed its slider at full and write `SVOL=127` over the player's saved value. That is the very
symptom this DLL exists to remove, reintroduced for the no-sound case.

**This bug is real and was confirmed in game** with a temporary diagnostic build: dragging the
slider to 33 and closing the menu correctly produced `SVOL=33` in `obi.ini`. But fixing it alone
did **not** fix "resets on reload" - that symptom survived unchanged, which is what led to bug 2.

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
`g_soundReady` is **still 0** - it is not set to `1` until several instructions later, in the same
function. The load-time apply is therefore a **guaranteed silent no-op on every single launch**,
regardless of what `SVOL` says in the file. The mirror simply keeps its compiled-in startup value
(measured as `1.0`, i.e. full) until the player manually touches the slider. One statement in the
wrong place in the original 1999 code - this is the actual cause of the reported symptom.

The setter's own caller census says the same thing: two `E8` call sites in the whole image,
`0x00415A78` inside `bapsound_moduleInit` and `0x0044249C` inside `options_audio`. One start-up
apply that cannot work, and one live slider that can.

**Confirmed** with a temporary diagnostic build across two separate sessions: the very first call
to `bapsound_setMasterVolume` in each run showed the mirror ending up at `1.0` regardless of the
argument passed in (`120` in one run, `0` in the other) - exactly what "the guard blocked the
write and the mirror kept its old value" looks like from outside the function.

**Fix:** `bapsound_setMasterVolume` is tapped (not replaced - the live path must keep working
unchanged). If it is called while `g_soundReady` is still `0`, the intended value is remembered
instead of lost. A per-frame check (`common/frame_hook.h`, the same "call me once per rendered
frame" site every other feature in this tree uses for a live slider preview) re-applies that
value the instant `g_soundReady` actually becomes `1` - which happens a handful of instructions
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
| `bapsound_moduleInit` | `0x004159F0` | not touched - the fix works around it rather than editing its instruction order |
| `options_audio` | `0x00441FA4` | not touched |

## A slider notch is not the test

The menu's own arithmetic truncates in both directions: the slider seeds from
`trunc(volume / 127 * 19)` and a drag writes back `trunc(notch / 19 * 127)`. Notch 5 gives
`SVOL=33`, and 33 seeds back to `trunc(4.937)`, which is notch 4. Only 0 and 19 survive the round
trip exactly. That is 1999 engine behaviour and not something this DLL changes, so **check
`SVOL=` in `obi.ini` and the log, never the slider's position.**

## Testing status

All three patterns measured against every retail `WMAIN.EXE` available, including the German
build: one match each, at the addresses this file names. The `master_get` prologue alone matches
twice (`0x00417459` and `0x0041778C`), which is why the whole 30-byte body is the pattern.

Bug 1 (wrong saved value) was confirmed fixed in game before the guard branch was restored. Bug 2
(dropped load-time apply) is diagnosed from two real runs' logs and fixed per the analysis above;
**neither the restored guard branch nor the bug-2 re-apply has been run in the game since.**
Reviewed statically and compiled, not played.

The log line to look for is `startup SFX volume (N) applied`. The actual test is whether the value
in `obi.ini`'s `SVOL` is the value the game starts at on the very next launch.

## Relationship to `ini_path_fix`

Independent bugs, independent fixes. `ini_path_fix` makes sure `obi.ini` is the same file on
every read and write; this makes sure the value written there is correct (bug 1) and that a
correct value in the file is actually the value the game starts at (bug 2). Music volume was
unaffected by either bug - it round-trips through a simple engine-side float with no driver query
and no ordering dependency on a "ready" flag - which is what pointed at these two SFX-specific
sites rather than a general save/load problem.
