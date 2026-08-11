# sfx_volume_save_fix

**Produces:** `sfx_volume_save_fix.dll` -> `mods\`

The SFX volume slider resets to (about) full on every reload, no matter what it was set to when
the game last closed. Two independent bugs, both fixed here.

## Supported executables

Any build whose audio code matches the retail sites at `0x00417459` / `0x0041738D` /
`0x00417379`. The sites resolve by pattern; if any does not match, that part of the DLL changes
nothing and says so in the log.

## Configuration: `[sfx_volume_save_fix]`

| Key | Default | Meaning |
|---|---|---|
| `Enabled` | `1` | |

## Bug 1: the saved value was wrong

`menu_options_audio_run` (`0x00441FA4`, the audio options screen) has exactly one function it
calls to find "what is the SFX volume right now": `sfx_master_volume_get_from_ail`,
`0x00417459`.

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
just pushed with `_AIL_set_digital_master_volume`. This function has exactly two callers, both
inside `menu_options_audio_run`: seeding the slider widget when the screen opens, and building
the value written to `obi.ini`'s `SVOL` key when the screen closes.

The engine already keeps a reliable copy of the true value. `sfx_master_volume_set`
(`0x00417379`), the function that **drives the slider live**, writes it two instructions in:

```
0041738D  DB 45 08               fild dword ptr [ebp+0x08]          ; the int 0..127 argument
00417390  D8 35 50 81 4A 00      fdiv float ptr [g_sfxVolumeScale]  ; -> 0x004A8150 (127.0)
00417396  D9 1D 70 A9 4A 00      fstp float ptr [g_sfxMasterVolume] ; -> 0x004AA970
```

`[0x004AA970]` is not a write-only shadow: `FUN_004169BD`, the per-channel volume calculation
that runs on every sample start, reads it back - `base * scale * g_sfxMasterVolume` - so this
cell has to stay correct for in-game volume to be right at all.

**Fix:** `sfx_master_volume_get_from_ail` is entirely replaced (not wrapped - calling through to
the AIL query first would just reintroduce the bug) with a detour that computes the same 0..127
integer the engine itself derived the mirror from, clamped to `[0, scale]`. Both data addresses
are read out of `sfx_master_volume_set`'s own instruction stream rather than hardcoded.

**This bug is real and was confirmed in game** with a temporary diagnostic build: dragging the
slider to 33 and closing the menu correctly produced `SVOL=33` in `obi.ini`. But fixing it alone
did **not** fix "resets on reload" - that symptom survived unchanged, which is what led to bug 2.

## Bug 2: the loaded value was never applied (the actual cause of "resets to full on reload")

`sfx_sound_init` (`0x004159F0`, runs once at startup) does this, in exactly this order:

```c
ini_read_int_alt("SVOL", 127, &loaded);   // reads obi.ini correctly
sfx_master_volume_set(loaded);            // <-- tries to apply it
...
g_soundReady = 1;                         // set AFTER the call above, not before
```

`sfx_master_volume_set`'s entire body is gated on that same flag:

```
0041737F  83 3D B8 B4 5B 00 00   cmp dword ptr [g_soundReady], 0
00417386  75 05                  jnz +5      ; only THEN does fild/fdiv/fstp run
0041738B  E9 C8 00 00 00         jmp <exit, does nothing>
```

At the exact moment `sfx_sound_init` calls the setter with the value it just loaded, `g_soundReady`
is **still 0** - it is not set to `1` until several instructions later, in the same function. The
load-time apply is therefore a **guaranteed silent no-op on every single launch**, regardless of
what `SVOL` says in the file. The mirror simply keeps its compiled-in startup value (measured as
`1.0`, i.e. full) until the player manually touches the slider. One statement in the wrong place
in the original 1999 code - this is the actual cause of the reported symptom.

**Confirmed** with a temporary diagnostic build across two separate sessions: the very first call
to `sfx_master_volume_set` in each run showed the mirror ending up at `1.0` regardless of the
argument passed in (`120` in one run, `0` in the other) - exactly what "the guard blocked the
write and the mirror kept its old value" looks like from outside the function.

**Fix:** `sfx_master_volume_set` is tapped (not replaced - the live path must keep working
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
| `sfx_master_volume_get_from_ail` | `0x00417459` | detoured; entire body replaced (bug 1) |
| `sfx_master_volume_set` | `0x00417379` | tapped; live behaviour unchanged, startup no-op rescued (bug 2) |
| `sfx_master_volume_set` (`fild`/`fdiv`/`fstp`) | `0x0041738D` | read only; supplies scale/mirror addresses |
| `g_sfxMasterVolume` | `0x004AA970` | read only; the engine's own live volume, `0..1` |
| `g_sfxVolumeScale` | `0x004A8150` | read only; the `0..127` conversion factor |
| `g_soundReady` | `0x005BB4B8` | read only; read out of `sfx_master_volume_set`'s own guard, not hardcoded |
| `sfx_sound_init` | `0x004159F0` | not touched - the fix works around it rather than editing its instruction order |
| `menu_options_audio_run` | `0x00441FA4` | not touched |

## Testing status

Both signatures for `sfx_master_volume_get_from_ail` and `sfx_master_volume_set` measured
against the real retail `WMAIN.EXE` and confirmed resolving uniquely via `engine_fixes.log`.
Bug 1 (wrong saved value) confirmed fixed in game. Bug 2 (dropped load-time apply) diagnosed from
two real runs' logs and fixed per the analysis above; **not yet re-confirmed in game after the
bug-2 fix** - the log line to look for is `startup SFX volume (N) applied`, and the actual test
is whether the slider now shows the last-saved value on the very next launch.

## Relationship to `ini_path_fix`

Independent bugs, independent fixes. `ini_path_fix` makes sure `obi.ini` is the same file on
every read and write; this makes sure the value written there is correct (bug 1) and that a
correct value in the file is actually the value the game starts at (bug 2). Music volume was
unaffected by either bug - it round-trips through a simple engine-side float with no driver query
and no ordering dependency on a "ready" flag - which is what pointed at these two SFX-specific
sites rather than a general save/load problem.
