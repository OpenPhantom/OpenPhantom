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
| `NeutraliseZBias` | `1` | | write `je` -> `jmp` at `0x00488270`, so the decal arm issues `SetRenderState(D3DRENDERSTATE_ZBIAS, 0)` instead of `1`. **This is the fix.** A translation layer that synthesises a subtracting depth bias for a state Direct3D 9 does not have turns the engine's exact-equality decal test into a strict less-than and loses every decal |
| `DepthBias` | `0.0` | 0-0.01 | how far a decal is pulled towards the camera, in device depth. Ships at 0, meaning off: pulling the vertex was measured and changed nothing, and `NeutraliseZBias` above is what does the work |
| `StateClear` | `0` | | bits to clear from the decal's render state word `0x0010AE40` before it reaches the engine. An instrument, not a feature |
| `StateSet` | `0` | | bits to set in the same word. Both ship at 0, so the word reaches the engine exactly as it left `bapvrt_drawPolyDecals`. They exist to settle which removed state costs the decal in single runs instead of one rebuild per suspect: that word asks for at least two things Direct3D 9 removed, ZBIAS (bit `0x00100000`, state 47) and `TEXTUREMAPBLEND=DECALALPHA` (bit `0x00000400`, state 21), and a translation layer may honour, drop or mistranslate either |
| `ScorchReach` | `1` | | a burn reaches every polygon its mark touches. Three things in the engine's burn stopped it: the cell query around an impact is capped at 1.9 units while the polygon test uses the mark's full size; a quad is tested against the mark's sphere as two triangles that do not cover it, so a mark centred just across a quad's fourth edge skips that quad; and a neighbour that slopes away from the shot by a little more than the shot's own angle is refused as facing away. All three give a blast mark cut along a straight line with part missing (issue 28). The cap is stepped over, the second triangle becomes the one that covers the quad, and a neighbour is refused only past about 45 degrees. A repair: a mark that fits inside one polygon is unchanged |
| `SubmitTrace` | `0` | | a measurement: sample the x87 status word either side of every decal submit and name any submit across which the stack pointer moved; see the section below |
| `DryAtStart` | `1` | | a freshly spawned or restored body starts dry. The game lays wet footprints for eight seconds after a footstep in water, timed on a clock that starts with the process, and a new body's last-wet time is written as zero, so for the first eight seconds after launch every character that has never touched water leaves wet prints on dry ground. The two stores of that zero write a time long past instead. A repair: nothing differs once eight seconds have passed |

`DepthBias=0` is not the same as `Enabled=0`. It is an amount rather than a switch, and it ships at
0, so that row documents a lever rather than something the patch is doing for you. `Enabled=0` also
turns off `NeutraliseZBias`, which is the byte that brings the decals back.

## Engine locations

| Site | Retail VA | What |
|---|---|---|
| the decal fan submit | `0x00487F40` | detoured, 8-byte prologue; only vertex `z` and the render state word are changed, the return value is passed through |
| the ZBIAS branch | `0x00488270` | one byte, `je` (`74`) -> `jmp` (`EB`), **only** when `NeutraliseZBias=1`. Validated as the expected `je` before it is written, so a second install declines |
| the depth compare selector | `0x00487672` | **read, never patched**, and only for the mask cell operand at `+23`. Read per call rather than latched at install: at the host entry point the graphics are not up and the cell is still zero |
| `bapvrt_drawPolyDecals` | `0x0041C87D` | the one and only caller, read during the RE, not patched |
| the query box cap in `fx_scorch` | `0x00456ECA` | the `jne` that keeps a box under 1.9 units, made a `jmp`, **only** when `ScorchReach=1`; found by a pattern over the compare, the store and the call that follows, both constant cells masked |
| the quad's second sphere test in `fx_scorch` | `0x00456FDE` | the call of `inter_sphereVsTriangle` with vertices 1-2-3; its return address is what the detour below answers differently, **only** when `ScorchReach=1`; found by a pattern over the vertex count test, the three pushes and the call, the call's displacement masked and read back to prove it reaches the function below |
| `inter_sphereVsTriangle` | `0x0046DBCA` | detoured; every caller gets the original, except a call returning into the site above, which is handed vertices 0, 2 and 3 of the same quad |
| the facing test in `fx_scorchProjectPoly` | `0x00457126` | the operand of `fcomp` against a shared zero, repointed to a cell of this DLL's holding 0.7, **only** when `ScorchReach=1`; the cell it pointed at is read and must hold zero first |
| the wet stamp in `bapobj_init` | `0x0041235F` | the immediate of `mov [thing+0x108],0` at `0x00412359`, repointed from 0 to a time long past, **only** when `DryAtStart=1`; found by an address-free pattern over the five stores around it |
| the wet stamp in `bapobj_restoreObject` | `0x00410FFA` | the same store at `0x00410FF4`, the one field the restore clears after copying the saved record back; both immediates are written or neither |

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
and never writes it, so the bias is its only claim to the pixel.

**`D3DRENDERSTATE_ZBIAS` does not exist in Direct3D 9.** It was replaced by `D3DRS_DEPTHBIAS`, a
float in normalised depth instead of an integer 0..16, and every DirectDraw7-to-Direct3D9 layer has
to invent the conversion itself. The engine cannot influence the result and is never told it failed.

## What this does instead

It stops asking the device for a favour and moves the geometry. Every polygon in this game is
submitted **pre-transformed**, the vertex format literal is `0x1C4` =
`XYZRHW|DIFFUSE|SPECULAR|TEX1`, so `z` is already the device-space depth in `[0,1]`, the
exact quantity `ZBIAS` was meant to shift. Subtracting a small constant from it is what
`ZBIAS` did, done one layer earlier and on our side of the wrapper. Negative results are clamped to
`0`.

**This is safe because the site is exclusive.** An `E8 rel32` sweep of the entire `.text` finds
`0x00487F40` has exactly **one** caller, `0x0041C87D`, inside `bapvrt_drawPolyDecals`. Nothing else
in the game reaches it, so the hook needs no render-state test and can never touch world geometry,
sprites, the HUD or the front end.

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
the camera. It changed nothing at any value of `DepthBias`, including values a thousand times
larger than the depth buffer's quantisation.

The direction is read **per call**, not latched at install: at the host entry point the graphics are
not up yet and the cell still reads zero. The log names the answer once, on the first decal:

```
[decal_fix] the device gave the engine a REVERSED (D3DCMP_GREATER) depth test, so a decal is
            pulled forward by +0.00015 in device depth
```

The engine reads the direction late for the same reason. The third sort key of its deferred draw
list is direction-dependent rather than fixed.

## Choosing the number

`0.00015` is roughly ten depth units on a 16-bit buffer and about 2500 on a 24-bit one, enough to
clear the quantisation either way, small enough that the world offset it corresponds to is
invisible. Device depth is non-linear, so a constant here is a very small distance near the camera
and a large one far away, which is the right shape: that is where depth resolution is worst.

Raise it if decals still flicker or vanish at a distance. Lower it if a decal shows through a thin
piece of geometry standing on its polygon. The ceiling is `0.01`, and a value above it is clamped
with a warning rather than honoured.

## The wet prints that appear with no water

`footstep_tick` at `0x00437AC0` stamps `thing+0x108` with the wall clock whenever the polygon under
the foot carries floor material 12, 13 or 14 (shallow water, swamp, the water surface plane), and
the print pass at `0x004385B0` lays a wet print on any other material while `clock - stamp < 8`.
The clock is the Time module's, zeroed once at startup, and a new body's stamp is written as zero
in `bapobj_init` and again in `bapobj_restoreObject`. So for the first eight seconds of the
process a body that has never been wet passes the test on every step.

Nobody saw it in 1999 because no machine reached a level inside eight seconds. A modern machine
loading a save from the menu does, and the report was "wet footprints on dry ground, at random":
random because it depended on how fast the save was loaded after launch. It was caught in the act
with `[diagnostics] Footsteps=1`: `wet prints begin ... the stamp is 7.28 s old (clock 7.28, stamp
0.00)`, on metal, seven seconds after launch.

`DryAtStart` writes a time a thousand million seconds in the past in both stores, so a fresh body
fails the test for the life of the process, and the first real footstep in water overwrites the
stamp with the clock exactly as before. The restore's clear is kept in the same spirit: a stamp
from a previous process was on a different clock and must not come back, and now it comes back
as "long ago" rather than as "now".

## The mark cut in half

`fx_scorch` (`0x00456E8B`) gathers the cells around the impact with a box of the mark's size
times two, then tests every gathered polygon against a sphere of the full size and stamps each
one the sphere touches, one decal per polygon, all under one owner. The box is capped at 1.9
units; the sphere is not. The ground scorch is placed at size 1.25, so the box is already past the
cap and asks a 1.9 unit box for cells while testing a 2.5 unit sphere. A polygon whose cell lies
more than 0.95 units from the impact is never gathered, so the part of the mark that falls on it
is never stamped, and the mark ends in a straight line at the polygon's edge with the far half
missing. That is the picture in issue 28: half a blast mark on the sand, cut clean.

`ScorchReach` steps over the store of the cap, so the box is always the mark's size times two,
which is the reach the sphere test already has. Nothing else in the function reads the cap. A
sabre scorch or a small shot mark fits inside 1.9 units and gathers exactly what it did.

That was the first repair, and the picture after it still showed cut marks, along polygon edges
and not cell edges. The second cause is a few instructions further on. Each gathered polygon
is tested against the sphere as triangles, and a quad as two: vertices 0-1-2, then 1-2-3
(`lea ecx,[ebp-0x40]`, which is `verts + 1`). Those two triangles do not tile a quad; 0-1-2 and
0-2-3 do. The wedge along the edge from vertex 3 back to vertex 0 is never tested, so a mark
centred on the polygon next door, just across that edge, overlaps the quad only in the wedge, the
quad is skipped, and the mark ends in a straight line along that edge. One edge in four of every
floor quad does it, so some marks across a seam are whole and some are cut.

The right second triangle is not three vectors in a row, so the call's argument cannot be fixed
with a different displacement. `inter_sphereVsTriangle` is detoured instead, and for a call whose
return address is that one site it is handed a copy of vertices 0, 2 and 3; every other caller in
the engine, the sabre and the projectile tests included, gets the original untouched. The call's
displacement is read back and must reach the detoured function, so the site and the callee are
proven to be each other's before anything is installed.

The third cause is in the projector, and it showed once the first two were gone: marks still cut
at every fold in the sand, flat seams included. `fx_scorchProjectPoly` refuses a polygon whose
normal faces along the shot, the dot of the two above zero. For the polygon the shot hit that dot
is near minus one. The same test runs on every polygon of the splash, and ground falling away
from a low shot by a few degrees more than the shot's own angle gives a small positive number and
is refused, so the mark ends at the fold. The compare's operand, a shared cell holding zero, is
repointed to a cell of this DLL's holding 0.7, the sine of about 45 degrees: a neighbour that has
merely folded a little is stamped and one on the far side of a ridge is still refused. The mirror
and the mark type read the same dot afterwards and are untouched.

What is left is a mark across a steep corner. The projector puts a mark on each polygon
by that polygon's own facing, a plan view and the big burn on a floor, a side view and the shot
texture on anything steeper than 45 degrees, so the two halves across such a fold are projected on
different planes with different textures and cannot meet. That is how the game was drawn in 1999,
before the decals went missing on modern wrappers, and it is left as it is.

## A measurement: `SubmitTrace`

`[decal_fix] SubmitTrace=1` samples the x87 status word either side of every decal submit and
names the call when the stack pointer moved across it. It exists for the blade drawn out of a
hand, framerate_fix's README under that heading, and it answered that the pointer never moves
across a submit, so the pop that hunt is after is not in the decal path. Off as shipped.

## What this does NOT fix

* **A decal that is never stamped.** This DLL sits after the pool; if `bapvrt_addDecal` never
  creates the record, there is nothing here to bias. Use `[diagnostics] Fx=2` for that question and
  `Fx=3` for this one.
* **`fx_rampFog`.** The cutscene tint walks the *device's* fog start and is inert under
  `view_distance_fix`'s vertex-fog regime. Unrelated to decals and unchanged here.

## Testing status: accepted in game (2026-08-07, the dry start 2026-09-12, the reach 2026-09-13)

Ground shadows, scorch marks and footprints are back under `dxwrapper` with `Dd7to9=1`, with
`NeutraliseZBias=1` and `DepthBias=0.0`. The single byte at `0x00488270` is the whole fix.

**The reach is accepted in game.** Three rounds in the Mos Espa canyon, each after one of the
three repairs: the cap alone left marks cut along polygon edges; the quad's second triangle fixed
the flat seams and left the folds; the facing threshold fixed the folds. The last picture showed a
dozen marks whole across seams and folds, the only cuts at steep corners, where the engine's own
projection changes. Sabre marks on walls unchanged.

**The dry start is accepted in game.** The defect was reproduced first, a save loaded seven
seconds after launch leaving wet prints on a metal floor with the footstep observer recording the
zero stamp, then the repair was installed and the same load left none. Loading into water, loading
out of water and standing in water were then played with the observer on, and every print spell in
that log begins 0.03 s after a stamp on a real water or swamp polygon, which is the rule doing what
it was written to do.

**What did NOT work, so nobody repeats it:** biasing the vertex `z` (any magnitude, either sign) and
swapping `TEXTUREMAPBLEND` for `MODULATEALPHA`. Both were measured, both changed nothing. The
mistake behind all three attempts was the same, compensating for a request the engine makes
instead of asking what the engine *relies on*. It relies on an exact depth equality, and the bias it
asks for on top is redundancy that a translation layer turns into poison.

## Historical status

Built and linked, `/W4 /WX` clean. Offline verification passes on both retail builds.

**The mechanism is proven, the number is not.** That decals depend on `ZBIAS` was established by
measurement, not inference: with `[diagnostics] Fx=3`, a session reported 1026 draw calls in which
every record kept its material, kept its texture page and was accepted by the cel selector, and
nothing appeared on screen. The same build on a wrapper that passes D3D7 through to the system
implementation, where `ZBIAS` still exists, shows every decal. What has *not* been established in
game is whether `0.00015` is the right amount on this hardware.
