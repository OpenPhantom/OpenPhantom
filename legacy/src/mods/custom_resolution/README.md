# custom_resolution

**Produces:** `custom_resolution.dll` -> `mods\`

A resolution nobody enumerated: type any width and height into `engine_fixes.ini` and the game
applies it at startup and, where the aspect ratio allows, offers it in the options screen's own
list alongside the modes DirectDraw actually reported. Built for ultrawide monitors and handheld
panels, neither of which a stock 1999 mode enumeration is likely to include.

> Independent of `enhanced_resolution.dll`. Neither depends on the other being installed, and both
> can be installed together: `enhanced_resolution`'s own `WidescreenModes` lock-lift and this DLL's
> own copy of the same patch simply agree with each other, whichever gets there first.

## Configuration: `[custom_resolution]`

| Key | Default | Meaning |
|---|---|---|
| `Width` | `0` | 0 = disabled, nothing here is patched at all |
| `Height` | `0` | 0 = disabled |

Both are read once, at startup, clamped to the engine's own hard 640x480 floor (a smaller value
can never be selected regardless of what this DLL does, see below) and to a generous 7680x4320
sanity ceiling meant only to catch a typo, not to limit a real display.

## Why this works: one more record, not a new code path

The engine's own resolution handling, from the options screen down to actually creating the
device, is entirely INDEX based and reads from ONE table: the raw DirectDraw mode list at
`0x862740` (count at `0x862014`, 64 records of 0x54 bytes each - the same table
`enhanced_resolution`'s own `mode_filter.c` already documents). A resolution DirectDraw never
enumerated has no record in that table, and nothing downstream of it ever builds one on demand -
`graphics_setResolution` either finds an EXACT match or falls back to the CLOSEST EXISTING one, it
never invents a mode.

So this DLL adds one. Immediately before `graphics_buildModeList` runs (see Engine locations
below for why that is always the right moment), it clones a genuine, already-validated record byte
for byte and overwrites only its width and height, then appends it to the table and bumps the
count. Confirmed by decompiling every function in the chain directly: nothing between
`graphics_buildModeList` and the actual device-creation call (`FUN_0048EC54`) ever reads a cached
handle, a GUID, or anything else tied to how a record was originally populated - every field is
read straight out of the record at the moment it is needed, which is what makes a cloned-and-edited
record behave identically to a real one at every step: the options screen labels it correctly
(`"%4d x %4d"`, formatted straight from its own width/height), selecting it calls
`graphics_setMode` with its own index, and that calls into device creation with a pointer straight
into the record itself.

## Engine locations

| Site | Retail VA | What |
|---|---|---|
| `graphics_buildModeList` | `0x46C592` | detoured, 6-byte prologue. The custom record is added immediately before calling the original |
| `graphics_setResolution` | `0x46BE3D` | detoured, 6-byte prologue, own copy of `enhanced_resolution`'s own signature. Only the one startup call that reads "as `obi.ini` says" is touched |
| the 4:3 gate in `graphics_buildModeList` | `0x46C6D9` | two bytes, `EB 32` -> the accept label, own copy of `enhanced_resolution`'s own patch. Best effort: a non-4:3 custom resolution needs this to appear in the options screen, but not to be applied at startup |
| the DirectDraw enumeration callback | `0x4928FC` | **read only, never detoured here.** Its own operands give the raw table's base and live count. Resolved with the detour-aware two-stage search specifically so this still works when `enhanced_resolution.dll`'s `mode_filter.c` has already detoured the same site |

## What this cannot do

* **Below 640x480 is unreachable, and that is the engine's own floor, not this DLL's.**
  `graphics_setResolution` clamps any requested size up to 640x480 before it ever looks anything
  up, so a record smaller than that could never be found by an exact match. `Width`/`Height` below
  the floor are raised to it, logged, not silently dropped.
* **What the record's OTHER ~40 bytes mean is still not known**, only that they are safe to carry
  over unexamined from a real record - see the DLL's own header comment for why cloning rather
  than hand-building sidesteps needing to know.
* **The underlying DirectDraw-to-modern wrapper (dgVoodoo2, DDrawCompat) still has the final say,
  and field testing confirms exactly where that line sits.** This DLL gets the engine to ASK for
  the custom size through the same call every real mode change already goes through; whether the
  wrapper underneath honours it depends on whether the DRIVER recognises the size as a real mode,
  not on anything this DLL does. Two arbitrary, invented sizes (`2001x877`, then `2000x876` to rule
  out the odd width as the cause) both failed identically with the engine's own "could not
  initialise graphics hardware" - crash_report shows nothing, and the log shows the startup hook
  firing correctly and then silence, meaning device creation itself refused, one layer past
  anything this patch touches. Registering `2000x876` as a custom resolution in NVIDIA Control
  Panel - which tells the DRIVER a mode exists, nothing to do with this DLL - made the identical
  `Width`/`Height` pair work immediately afterward, engine option-screen listing included.
  **A real monitor's own native resolution does not have this problem**: it is already registered
  with the driver by the monitor's own EDID, the same way a real desktop span already was in the
  first successful test, so no driver-side step is needed for the case this DLL actually exists
  for. Do not advise a player to create a custom resolution in their GPU control panel to use their
  own monitor's real resolution; that is solving a problem a real display does not have.
* If the raw table is already full (64 records) by the time `graphics_buildModeList` runs, there
  is no room and the custom resolution is refused with a log line rather than overwriting a real
  entry.

## Testing status

**Field-confirmed, in the actual game, across several sessions.** Every log line this file's own
comments promise has been observed for real: `mode table resolved`, both detours and the aspect
gate hooked, `custom resolution WxH added as mode table record N` with a real record number past
the driver's own entries, and `startup resolution forced to the custom WxH`. Confirmed working end
to end at a real, driver-recognised size (`3840x800`, a real multi-monitor span) with a screenshot
of the game actually rendering at it. Confirmed AGAIN with a fully synthetic size (`2000x876`,
never enumerated by anything) after registering it with the GPU driver, including the record
appearing correctly labelled and selectable in the options screen's own list alongside the real
entries - the non-4:3 aspect gate lift, `graphics_enumModes`'s label formatting, and the listbox
integration all confirmed by that alone, not just the startup apply. See "What this cannot do"
above for the one real failure mode found in the field (device creation refusing an unregistered
synthetic size) and how it was isolated from everything this DLL itself controls.

Every engine address cited above was confirmed by disassembling or decompiling the actual function
directly during development, not inferred from a decompiler's own text or carried over from
`enhanced_resolution`'s documentation without independently re-reading the referenced code. The
signature bytes for `graphics_setResolution` and the 4:3 gate are deliberately copied verbatim from
`enhanced_resolution.c` rather than re-transcribed, for the same reason `cheats_openphantom.c`
copies `SIG_CAMERA_VIEW` verbatim from `enhanced_input`: matching bytes, not just a matching
address.
