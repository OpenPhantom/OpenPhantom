# large_textures

**Produces:** `large_textures.dll` -> `mods\`

Raises the ceiling on how big a texture page may be, so that replacement artwork larger than 1999
hardware allowed is loaded and drawn instead of being silently cropped.

**This is a capability, not a fix.** It repairs no defect, and on the artwork the game ships it is
the identity. If you are running the original data, installing this DLL changes nothing you can
see, and that is the intended outcome.

## Supported executables

Three builds of this engine ship inside one installation and the clamp site was found in all three:
the retail `WMAIN.EXE`, `wmain.exe`, and `obi.exe`, the Edit Tool's recompile, where it sits at a
different address and the same pattern still finds it. The German retail executable is byte
identical to the English one, so it is the same build rather than a fourth.

The loader site was only checked against the retail image, so `obi.exe` in particular is untested
for the second patch. That costs nothing: both sites resolve by pattern, a site that does not match
disables that one patch and says so in the log, and the other one still applies.

## Configuration: `[large_textures]`

| Key | Default | Range | Meaning |
|---|---|---|---|
| `Enabled` | `1` | | |
| `MaxTextureSize` | `1024` | 256-8192, powers of two | pixels per axis for every texture in the game |
| `MaxWorldPageSize` | `256` | 256-4096, powers of two | pixels per axis for a level geometry page |

`256` is the engine's own value for both keys and means "change nothing"; the DLL logs that and
leaves the site alone. A value outside the range, or one that is not a power of two, is refused
with a warning rather than applied, and the engine keeps its own value.

`MaxWorldPageSize` defaults to the engine's own value on purpose. Raising it enlarges a buffer so
that larger level artwork *can* be loaded; it does not make any existing level larger, and it is
useless until somebody authors a level that needs it.

**Order matters for `MaxWorldPageSize` and it is not negotiable.** Raise it, start the game once so
the patch is in, and only then write larger world pages into a level file. A level whose pages are
larger than that block corrupts the heap while it loads, and it does so before anything can report
it.

The block costs axis times axis bytes for the length of one level load: 1024 gives 1 MB, 2048 gives
4 MB, 4096 gives 16 MB.

## Engine locations

| Site | Retail VA | What |
|---|---|---|
| `std3D_AddToTextureCache`, the clamp | `0x00488757` | the `mov edi, 0x100` operand is repointed; on `obi.exe` the same site is at `0x004886F7` |
| `bapworld_loadTex`, the scratch allocation | `0x0041DAB6` | the `push 0x10000` operand is repointed |

Neither site is detoured. Both are a single 32 bit operand write through `patch_repoint_operand`,
which refuses unless the operand still holds the engine's own value, so a second run declines
instead of doubling anything.

## Two ceilings, and why one DLL owns both

The engine enforces a size limit twice, in different modules, for different reasons. Raise one and
not the other and you get either cropped artwork or a corrupted heap, and neither failure names its
own cause.

**The device ceiling governs every texture in the game.** The one function that creates a texture
clamps both axes to 1..256 and then **crops** rather than scales: a wider page is created at 256 and
the conversion loop copies only the leftmost 256 columns of each row. There is no resampling
anywhere on that path. One immediate governs both axes, because the ceiling is loaded into a
register once and both comparisons use that register:

```
00488757  BF 00 01 00 00  mov  edi, 0x100      the ceiling, loaded once
0048875E  3B C7           cmp  eax, edi        the width
00488776  3B C7           cmp  eax, edi        the height, same register
```

Changing that immediate is safe because nothing else in the function depends on it. `edi` is loaded
with the ceiling exactly once and read only by the four clamp instructions; at `0x0048879A` it is
reloaded with `lea edi, [ebp-0xA4]` for the descriptor clear and never carries the ceiling again.

**The world page scratch governs only the level geometry textures.** The level loader reads every
world page into one fixed 64 KB block, and then reads width times height bytes into it without
checking either number:

```
0041DAB6  68 00 00 01 00  push 0x10000         the block, allocated once per level load
...
0041DB19  8B 45 EC        mov  eax, [ebp-0x14] width, out of the 16 byte sub header
0041DB1C  0F AF 45 F0     imul eax, [ebp-0x10] times height
0041DB20  50              push eax             no bound test anywhere
```

So a page over 65536 pixels writes past the end of a heap block during level load. The block is
used for exactly two things and then freed on every exit path: it is the destination of that read,
and it is the source pointer handed to the page builder.

## Why the second patch exists

Raising `MaxTextureSize` on its own does nothing at all for the level geometry. Census over the 11
shipped levels: 116 world pages, of which **98 are 256 by 256**, 14 are 64 by 64 and 4 are 128 by
128. Since 256 times 256 is 65536, which is exactly the fixed block, the stock loader can grow only
the 18 small pages and only as far as 256:

```
upscale factor                   x2          x4          x8         x16
pages in the level files      18/116      14/116       0/116       0/116
pages in the other files    2725/2725   2725/2725   2725/2725   2725/2725
```

Only the first row goes through this loader. That is why a session run with the ceiling raised as
far as it goes showed no change in the levels at all, which read like a patch that had not
installed when in fact the second limit was doing its job silently.

## Why the default is inert

4000 of the 6482 exported textures were read out of their own headers. The most common sizes are
32 by 32 (1063), 16 by 32 (471), 64 by 32 (329), 64 by 64 (320) and 16 by 16 (317), and **none of
the 4000 exceeds 256 on either axis**. So on the original artwork the clamp never fires and any
ceiling at or above 256 is the identity. The remaining 2482 were not measured, which is why this
says 4000 rather than all of them.

## Known limitations

* **The engine never asks the device for its own maximum texture size.** `std3D_open` decodes the
  device description into the record at `+0x138` and names `+0x1DC dwMaxBufferSize` and
  `+0x1E0 dwMaxVertexCount`, but nothing on the texture path consults a maximum dimension. The
  clamp is the only limit there is. A ceiling the device cannot honour is therefore not caught
  here, and no warning is possible.
* **Powers of two only.** The clamped size is handed straight to surface creation and the page
  builder at `0x0040EC33` stores `widthMinus1` and `heightMinus1` and uses them as addressing
  masks. `D3DPTEXTURECAPS_POW2` is a capability the engine also never checks, so the requirement is
  enforced by this DLL instead, as a refusal with a warning.
* **A world page larger than the device ceiling still gets cropped on upload.** Raising
  `MaxWorldPageSize` past `MaxTextureSize` makes such a page load and then lose everything past
  column 256. The DLL detects that combination and warns; it does not silently raise the other key
  for you.
* **The world's page array is still 64 entries**, inline at `world+0xC4` and ending before
  `numLights` at `world+0x1C4`, and the palette count is still checked hard by `bapworld_loadPals`.
  Bigger pages, not more of them.
* This does not upscale anything. It removes a limit; producing the artwork is somebody else's job.

## Fallback behaviour

Each site is independent: different functions, different modules, no shared state and no ordering
constraint between them. If one does not resolve, that patch is skipped, the other still applies,
and the log names which of the two happened. If neither applies, the DLL logs `nothing was applied`
and the game runs exactly as it would without it.

A size that is out of range, not a power of two, or already the engine's own is refused before any
search happens, and each of those three refusals has its own log line. There is no path here that
writes optimistically: `patch_repoint_operand` reads the operand back first and declines if it does
not hold the value this DLL expects.

## Testing status

**A unit test for the size validation, and accepted in game**, in the 1.5.0 build, which was
played through by hand. This copy builds and links here, `/W4 /WX` clean, and ships in the
patch. The pattern work below is what establishes where it acts.

* `unittests/texture_size.c` covers what a requested size is allowed to do: the identity case, both
  clamps and the values on either side of them, a size that is not a power of two, a negative one,
  and the two numbers the engine's own instructions hold. It runs with the rest of the suite, so it is a
  statement about the arithmetic and not yet evidence that the arithmetic passes.
* The clamp site was located in all three builds and its operand read out of the image in each:
  `0x00488757` in the retail `WMAIN.EXE` and in `wmain.exe`, `0x004886F7` in `obi.exe`, all three
  holding `0x100`.
* The loader site's uniqueness rests on a census over the retail image's `.text` only. With the
  size immediate wildcarded, the bare allocate-and-bail form matches 3 sites, `0x00401C5D`
  (push 0xB8), `0x0041DAB6` (push 0x10000) and `0x0048C377` (push 0x90); with `83 EC 1C` in front
  it matches exactly one. **That census has not been repeated on the other four executables.**
* Both patterns wildcard the bytes this DLL writes, so they keep matching after the patch has been
  applied once. `patch_repoint_operand` supplies the safety the pattern gives up.
* The game has been run with the ceiling raised, and the observed result was that the levels looked
  unchanged. That is consistent with the second limit and is what led to the second patch, but it
  is not acceptance of either patch: nothing has yet been observed loading or drawing a texture
  larger than 256.

To check in game: raise `MaxTextureSize`, start the game, and read `engine_fixes.log` for the
`texture page ceiling 256 -> N` line. With the stock artwork that is the whole of what can be
confirmed, because no shipped texture reaches the old ceiling. Confirming that the patch does
something visible needs a replacement texture larger than 256 on an axis.

One hazard for whoever touches this next: the loader pattern begins three bytes into the function,
so it overlaps the five bytes a trampoline detour writes over a prologue. Nothing in this project
detours `bapworld_loadTex` today. The moment something does, this pattern has to move past the
prologue and disambiguate by its immediate instead, or it will quietly resolve to nothing.
