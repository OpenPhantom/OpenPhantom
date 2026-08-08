# decal_fix

**Produces:** `decal_fix.dll` -> `mods\`

Ground shadows, scorch marks, footprints and ripples, on a graphics wrapper that translates
DirectDraw7 to Direct3D 9.

## Supported executables

Retail `WMAIN.EXE` (EN/DE) and the Fix Pack build. The site resolves by pattern; if it does not
match, the DLL changes nothing and says so.

## Configuration: `[decal_fix]`

| Key | Default | Range | Meaning |
|---|---|---|---|
| `Enabled` | `1` | | |
| `DepthBias` | `0.00015` | 0-0.01 | how far a decal is pulled towards the camera, in device depth |

`DepthBias=0` is the same as `Enabled=0`: it is the amount, not a switch.

## Engine locations

| Site | Retail VA | What |
|---|---|---|
| the decal fan submit | `0x00487F40` | detoured; only vertex `z` is changed, the return value is passed through |
| `bapvrt_drawPolyDecals` | `0x0041C87D` | the one and only caller, read during the RE, not patched |

## Why a decal needs help at all

**A decal is not a quad above the ground. It is the ground polygon itself, submitted a second
time.** `bapvrt_drawPolyDecals` copies eight dwords per vertex verbatim out of the array the world
pass already transformed, and replaces exactly the UV pair and one byte of the diffuse alpha:

```
0041C81A  mov ecx, 8
0041C824  rep movsd                 <- eight dwords, unaltered
0041C829  mov [eax+0x18], ecx       <- u
0041C82F  mov [eax+0x1C], ecx       <- v
0041C83B  add eax, 0x20             <- stride 32 bytes
```

So the second draw lands at **exactly** the depth of the first. What normally lets it win is one
render state. The drawer sets the state word `0x0010AE40` at `0x0041C667`, and the state applier
turns its bit `0x00100000` into a device call at `0x0048825B`:

```
0048825B  test dword [esp+0x14], 0x100000
00488274  push 1
00488276  push 0x2F                 <- D3DRENDERSTATE_ZBIAS
00488279  call [ecx+0x58]           <- IDirect3DDevice3::SetRenderState
```

That state word carries `ZTEST` (`0x0800`) and **not** `ZWRITE` (`0x1000`): the decal tests depth
and never writes it, so the bias is the whole of its claim to the pixel.

**`D3DRENDERSTATE_ZBIAS` does not exist in Direct3D 9.** It was replaced by `D3DRS_DEPTHBIAS`, a
float in normalised depth instead of an integer 0..16, and every DirectDraw7-to-Direct3D9 layer has
to invent the conversion itself. The engine cannot influence the result and is never told it failed.

## What this does instead

It stops asking the device for a favour and moves the geometry. Every polygon in this game is
submitted **pre-transformed**, the vertex format literal is `0x1C4` = `XYZRHW|DIFFUSE|SPECULAR|TEX1`
, so `z` is already the device-space depth in `[0,1]`, which is exactly the quantity `ZBIAS` was
meant to shift. Subtracting a small constant from it is what `ZBIAS` did, done one layer earlier and
on our side of the wrapper. Negative results are clamped to `0`.

**The site is exclusive, and that is the whole reason this is safe.** An `E8 rel32` sweep of the
entire `.text` finds `0x00487F40` has exactly **one** caller, `0x0041C87D`, inside
`bapvrt_drawPolyDecals`. Nothing else in the game reaches it, so the hook needs no render-state test
and can never touch world geometry, sprites, the HUD or the front end.

## Which way is "forward" is not a constant

**This engine does not always use a less-than depth test**, and getting the direction wrong is not a
weaker effect; it is the exact opposite one, at every magnitude. The compare function is chosen
from a device capability at run time, at `0x00487672`:

```
00487672  mov eax,[0x85596C]        ; the device record
00487677  mov ecx,[eax+0x24]
0048767A  and cl, 0x10              ; can this device do GREATER?
0048767D  neg cl / sbb ecx,ecx
00487681  and ecx, 0x0E
00487684  add ecx, 2                ; -> 0x10 when it can, otherwise 2
00487687  mov [0x866FC8], ecx
```

`[0x866FC8]` goes through the caps-bit-to-`D3DCMP` mapper at `0x0048AAF9`, whose whole table is
`0x01->1 NEVER`, `0x04->3 EQUAL`, `0x08->4 LESSEQUAL`, **`0x10->5 GREATER`**, `0x20->6`, `0x40->7`,
`0x80->8`. So the mask `0x10` is a **reversed** depth test, in which nearer means a **larger** `z`.

Any Direct3D 9 device advertises `GREATER`, so on a translation layer that is the branch that runs.
This DLL's first release subtracted unconditionally and therefore pushed every decal *away* from
the camera, which is why it changed nothing at any value of `DepthBias`, including values a
thousand times larger than the depth buffer's quantisation.

The direction is read **per call**, not latched at install: at the host entry point the graphics are
not up yet and the cell still reads zero. The log names the answer once, on the first decal:

```
[decal_fix] the device gave the engine a REVERSED (D3DCMP_GREATER) depth test, so a decal is
            pulled forward by +0.00015 in device depth
```

The engine reads the direction late for the same reason, which is why the third sort key of its
deferred draw list is direction-dependent rather than fixed.

## Choosing the number

`0.00015` is roughly ten depth units on a 16-bit buffer and about 2500 on a 24-bit one, enough to
clear the quantisation either way, small enough that the world offset it corresponds to is
invisible. Device depth is non-linear, so a constant here is a very small distance near the camera
and a large one far away, which is the right shape: that is where depth resolution is worst.

Raise it if decals still flicker or vanish at a distance. Lower it if a decal shows through a thin
piece of geometry standing on its polygon. The ceiling is `0.01`, and a value above it is clamped
with a warning rather than honoured.

## What this does NOT fix

* **A decal that is never stamped.** This DLL sits after the pool; if `bapvrt_addDecal` never
  creates the record, there is nothing here to bias. Use `[diagnostics] Fx=2` for that question and
  `Fx=3` for this one.
* **`fx_rampFog`.** The cutscene tint walks the *device's* fog start and is inert under
  `view_distance_fix`'s vertex-fog regime. Unrelated to decals and unchanged here.

## Testing status: ACCEPTED IN GAME (2026-08-07)

Ground shadows, scorch marks and footprints are back under `dxwrapper` with `Dd7to9=1`, with
`NeutraliseZBias=1` and `DepthBias=0.0`. The single byte at `0x00488270` is the whole fix.

**What did NOT work, so nobody repeats it:** biasing the vertex `z` (any magnitude, either sign) and
swapping `TEXTUREMAPBLEND` for `MODULATEALPHA`. Both were measured, both changed nothing. The
mistake behind all three attempts was the same, compensating for a request the engine makes
instead of asking what the engine *relies on*. It relies on an exact depth equality, and the bias it
asks for on top is redundancy that a translation layer turns into poison.

## Historical status

Built and linked, `/W4 /WX` clean. Offline verification passes on all five retail executables.

**The mechanism is proven, the number is not.** That decals depend on `ZBIAS` was established by
measurement, not inference: with `[diagnostics] Fx=3`, a session reported 1026 draw calls in which
every record kept its material, kept its texture page and was accepted by the cel selector, and
nothing appeared on screen. The same build on a wrapper that passes D3D7 through to the system
implementation, where `ZBIAS` still exists, shows every decal. What has *not* been established in
game is whether `0.00015` is the right amount on this hardware.
