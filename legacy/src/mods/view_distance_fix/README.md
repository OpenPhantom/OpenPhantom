# view_distance_fix

**Produces:** `view_distance_fix.dll` -> `mods\`

Draw distance, fog, NPC activation and two-sided severed bodies, with a watchdog on the two walls
they run into.

## Supported executables

Retail `WMAIN.EXE` (EN/DE) and the Fix Pack build. `obiold.exe` is rejected by the append-block
match count (four instead of three) and `netobi.exe` by the table/bucket cross-check; that is
deliberate, and both gates are needed.

## Configuration: `[view_distance_fix]`

| Key | Default | Range | Meaning |
|---|---|---|---|
| `Enabled` | `1` | | |
| `ViewRangeScale` | `1.0` | 1.0-2.0 | the watchdog only ever lowers this |
| `FogFollowFov` | `1` | | scale each level's band with the cut edge and the field of view |
| `FogInsideCut` | `1` | | additionally cap the band to the no-pop-in limit |
| `FogSettleSeconds` | `1.5` | 0-10 | how long a fog change takes; `0` steps immediately |
| `FogScale` | `0.0` | 0 or 1.0-4.0 | 0 follows `ViewRangeScale` |
| `VertexFog` | `1` | | run the fog on the engine's own per-vertex ramp, see below |
| `NpcRangeScale` | `1.25` | 1.0-2.0 | the one setting here that touches GAME BEHAVIOUR |
| `TwoSidedSevered` | `1` | | draw dismembered bodies two-sided |
| `TwoSidedMax` | `8` | 1-64 | at most N per frame |
| `RelocateDrawTable` | `1` | | move the cell table, raise its limit to 16384 |
| `LowerCellLimit` | `1` | | lower it to 7168 instead; skipped when the relocation is active |

## Engine locations

| Site | Retail VA | What |
|---|---|---|
| `bapmat_viewDistance` | `0x40E42A` | detoured; only the return value is scaled |
| `baplight_applyLevelFog` | `0x41F14A` | detoured; `world+0x218` and `+0x21C` are written |
| `bapdraw_setFrameState` | `0x401DFE` | read only; `g_level` taken from two operands, cross-checked |
| table-fog capability query | `0x487B30` | 3 bytes -> `xor eax,eax ; ret` |
| `FOGTABLEMODE` (state machine) | `0x4884B8` | `push 3` -> `push 0` (`D3DFOG_NONE`) |
| `FOGTABLEMODE` (state commit) | `0x489B5B` | `push 3` -> `push 0` (`D3DFOG_NONE`) |
| `enemy_activationScan` | `0x4371E4 + 0x18` | one call site redirected |
| `rdMesh_draw` cull word | `0x40F3F7 - 4` | address read from the operand |
| `rdThing_Draw` | `0x40FE70` | detoured; the cull word is always restored |
| `rdCamera_BuildProjection` | `0x475FFA` | observed only, for the radius cap and the fog |
| `bapdraw_gatherCell` limit | `0x4064B8` | the whole dword, never one byte |
| `bapdraw_gatherCell` append | `0x4067CF` | table, bucket and counter read and cross-checked |
| `bapvrt_transformWorld` gate | `0x41A0DF` | the vertex-cache counter |
| the nine relocation words | 4 append blocks + the limit | all-or-nothing, with rollback |

## The two things you must know before turning a number here

**Draw distance and fog are two numbers no engine code connects**, and `graphics_clearFrame` clears
the picture to the **fog colour**. Geometry beyond `fogEnd` is exactly background-coloured, so
raising the range alone costs fill rate without a single extra pixel. The two are coupled here on
purpose, in `fog_regime.c`.

**Field of view and range multiply.** The cell count goes like `(hFOV/360) * pi * r^2`. From 63 degrees to 106 degrees
that is a factor of 1.68 from the field of view alone. Hence the radius cap `64 * sqrt(63 * budget/hFOV)`, which is
exactly 64 at the authored 63 degrees, retail-identical, and only shortens the range once *we* widen the
picture.

## Why the fog follows the field of view, and by exactly how much

**The cut edge is a circle of cell centres, in world units.** `bapdraw_drawWorld` builds a cell
rectangle from the four frustum corners and then rejects every cell in it with one test at
`0x004054B0`: `dx^2 + dz^2 < range^2`, where `dx`/`dz` are the horizontal distances from the camera to
the **cell centre**. The grid step is one world unit; every world-to-cell conversion in that
function is `floor(coordinate + 0.5)` with no scale factor. The emitter cull at `0x004221FA`
confirms it independently: it takes the *same* number and compares it against a real
`sqrt(dx^2 + dz^2)` in world units. So `range` and `fogEnd` are the same unit and comparable directly.

**The circle is radial; the fog is not.** The per-vertex ramp at `0x0040206C` walks
`z = 1 / rhw`, the view **depth**, not the radius. A cell centre sitting on the cut circle at
radius `R` has depth `R * cos(theta)`, where `theta` is its horizontal angle off the view axis. Dead centre
that is `R`; at the left or right edge of the screen it is `R * cos(hFOV/2)`. The nearest place a
newly collected cell can appear is therefore the **screen edge**, and it comes nearer as the
picture opens:

```
no pop-in anywhere on screen  if and only if  fogEnd <= (R - sqrt(2)/2) * cos(hFOV/2)
```

The `sqrt(2)/2` is half the diagonal of the one-unit cell the test measures to: a cell dropped because
its *centre* is outside the circle can hold geometry that much nearer.

The engine ships at **60 degrees** (`push 0x42700000` at `0x00417F79`, the first argument of the one and
only `rdCamera_new` call), where `cos(hFOV/2)` is 0.866. At 87 degrees it is 0.724.

**Capping the fog that way costs nothing visible.** Everything past the cut edge is already exactly
fog-coloured, because that is what the frame was cleared to. Pulling the fog inside the edge does
not hide anything that was on screen; it replaces a hard silhouette with a gradient into a colour
that is there either way.

### The two clamps, and what each is for

`FogFollowFov` is **relative**: it multiplies each level's authored band by

```
min(1, edge_limit(hFOV, R_live) / edge_limit(60 degrees, R_engine))
```

where `R_engine` is where the cut edge would have been and `R_live` is where we actually put it,both read out of the draw-distance detour, which is the only place the two exist at once. At 60 degrees
with an unmoved cut edge that factor is **exactly `1.0f`**, so every level keeps its shipped
numbers bit-for-bit. It is what makes the fog follow the FOV slider, the radius cap *and* the cell
watchdog lowering the range mid-level.

`FogInsideCut` is **absolute**: it caps the band at `edge_limit(hFOV, R_live)` regardless of what
the level authored. That is the clamp that removes the shipped cut-edge wall in BIGCITY and
FEDSHIP, whose fog ends 30 and 38 units *behind* their own geometry. With it on it dominates for
all eleven shipped levels; with it off the relative coupling alone runs and every level is
retail-exact at 60 degrees.

### The whole band moves, and that is a correctness rule

`bapdraw_setFrameState` forms `end - start` at `0x00401E8B` and, when the result is **negative**,
clears the "compute the fog yourself" flag at `0x00401EAA`. With that flag clear the emitter writes
a **constant zero** into every world vertex's specular (`0x00402459`), and zero means *fully
fogged*, while `FOGENABLE` was already set nine instructions earlier. An end pulled below the
level's own start therefore does not remove the fog; it paints the entire world in the fog colour.

Two shipped levels sit inside that trap: **BIGCITY** authors `start 20.0` with a draw distance of
20, and **FEDSHIP** authors `start 16.0` with a draw distance of 18. Any cap that moves the end
alone crosses their start. So the band is multiplied by **one ratio**: `end - start` keeps its sign
for every positive ratio, and the authored profile survives exactly, an object at depth `d * k` gets
precisely the fog the level's author gave depth `d`.

### The authored bands, and what they become

Read out of the level files at the offsets the loader uses (`hdr+0x90`, `hdr+0x94`, `hdr+0x854`).
`FogInsideCut=1`, `FogFollowFov=1`, `ViewRangeScale=1.0`:

| Level | draw | authored | at 60 degrees | at 87.2 degrees | at 60 degrees, `FogInsideCut=0` |
|---|---|---|---|---|---|
| GUNGA | 16 | 6.0-14.0 | 5.7-13.2 | 4.7-11.1 | 6.0-14.0 |
| GARDEN | 22 | 4.0-26.0 | 2.8-18.4 | 2.4-15.4 | 4.0-26.0 |
| SWAMP | 22 | 6.0-30.0 | 3.7-18.4 | 3.1-15.4 | 6.0-30.0 |
| ESPA | 22 | 8.0-32.0 | 4.6-18.4 | 3.9-15.4 | 8.0-32.0 |
| RACE | 22 | 10.0-32.0 | 5.8-18.4 | 4.8-15.4 | 10.0-32.0 |
| MAUL | 28 | 8.0-32.0 | 5.9-23.6 | 4.9-19.8 | 8.0-32.0 |
| FINAL | 24 | 12.0-30.0 | 8.1-20.2 | 6.7-16.9 | 12.0-30.0 |
| ASSAULT | 23 | 12.0-38.0 | 6.1-19.3 | 5.1-16.1 | 12.0-38.0 |
| QUEEN | 26 | 12.0-38.0 | 6.9-21.9 | 5.8-18.3 | 12.0-38.0 |
| BIGCITY | 20 | 20.0-50.0 | 6.7-16.7 | 5.6-14.0 | 20.0-50.0 |
| FEDSHIP | 18 | 16.0-56.0 | 4.3-15.0 | 3.6-12.5 | 16.0-56.0 |

**This is a real change from retail at 60 degrees for ten of the eleven levels**, and it is
`FogInsideCut`'s doing, not the FOV coupling's. Set `FogInsideCut=0` to get the authored numbers
back exactly and keep the FOV following.

### Nothing changes abruptly

Every change, the FOV slider moved, the watchdog lowering the range, the level's fog re-applied,is eased rather than stepped. The exponent comes from the engine's own `g_frameDelta`, read out of
`render_frameEnd`'s first operand, so a given amount of *real* time produces the same result at 30
and at 144 fps. A level **load** snaps instead: there is nothing on screen to ease away from.

## Why the distance fog does not render, and what `VertexFog` does about it

The engine carries **two fog regimes** and chooses between them once per frame, from a single
device capability bit.

`0x00487B30` reads `deviceRecord+0x1A4` and masks it with `0x100`. The record holds a
**0xFC-byte `D3DDEVICEDESC` copy at +0x138**, 0xFC is `sizeof(D3DDEVICEDESC)` in DirectX 6, and
every other field the device enumerator derives lands on the matching member of that struct
(`dcmColorModel` +0x08, `dpcTriCaps.dwZCmpCaps` +0x70, `dwShadeCaps` +0x80, `dwTextureCaps` +0x84,
`dwDeviceRenderBitDepth` +0x9C, `dwDeviceZBufferBitDepth` +0xA0, `dwMaxBufferSize` +0xA4,
`dwMaxVertexCount` +0xA8). So `+0x1A4` is `+0x138 + 0x6C` = **`dpcTriCaps.dwRasterCaps`**, and
`0x100` is **`D3DPRASTERCAPS_FOGTABLE`**.

Its only two callers, `0x00401E31` (world frame setup) and `0x00419894` (vertex-cache frame setup),
treat the answer like this:

* **zero, no table fog.** The engine arms its own per-vertex ramp. It walks the authored band in
  world units and writes the D3D fog factor into each vertex's **specular alpha** (`0xFF` clear,
  `0x00` fully fogged). That is the ordinary way a pre-transformed vertex carries fog.
* **non-zero, table fog available.** The engine leaves fog to the device: `FOGTABLEMODE` =
  `D3DFOG_LINEAR` plus `FOGSTART`/`FOGEND` handed over **unconverted, in world units**.

Every level ships with fog on (`world+0x210` bit 0, from B3D `hdr+0x88`) and a band of 4-20 near /
14-56 far. Every polygon in this game is submitted **pre-transformed**: the vertex format literal
in every draw call is `0x1C4` = `XYZRHW|DIFFUSE|SPECULAR|TEX1`, a 32-byte vertex. A modern
D3D-backed wrapper reports `D3DPRASTERCAPS_FOGTABLE`, so the second branch is taken, and the
device measures that world-unit band against a **device-space** depth confined to `[0,1]`. Every
pixel lands before `FOGSTART`, the fog factor clamps to "clear", and nothing reports a problem:
all five fog render states are issued exactly as the engine intends and are accepted.

`VertexFog=1` answers the capability question with "no table fog" and sets `FOGTABLEMODE` to
`D3DFOG_NONE` at **both** writers, so the specular alpha the ramp produces is what the device
blends with.

**The three writes are all-or-nothing, and that is not tidiness.** With the ramp disarmed, the
world pass writes a **constant zero** into the specular of every world vertex (`0x00402459`), and
zero means *fully fogged*. Clearing `FOGTABLEMODE` without arming the ramp would not lose the fog,
it would paint the entire world in the fog colour. The install validates all three sites before it
writes any of them and rolls the earlier writes back if a later one fails.

The 2-D layer is unaffected: sprites and lines are drawn with render-state words that do not carry
the fog bit `0x40`, so the state machine writes `FOGENABLE = 0` before them and their specular
alpha is never read.

### What was ruled out

A stale render-state shadow after a display-mode change was the first hypothesis, and a whole-image
byte sweep killed it. `[0x855964]` has exactly **three** writers in the image, `0x004884EA` and
`0x00488503` in the per-primitive state machine, and `0x00489D1C` at the tail of the whole-state
commit, which syncs it from `[0x855960]`. There is no fourth writer, and none is needed: the commit
re-issues all thirty-four render states, the five fog states among them, **unconditionally**, and
it runs from `std3D_open`, from `graphics_setMode` itself (`0x0046BD8B`: `getRenderFlags` ->
`setRenderFlags` -> commit) and from every level load through `baplight_applyLevelFog`
(`0x0041F200`). Fog cannot be lost to a stale shadow.

## The two walls

* **The cell table.** `bapdraw_gatherCell` checks its 8192-entry limit exactly once, on entry. The
  face loop afterwards is unchecked and `bapdraw_gatherCellMovers` has no limit at all. The end of
  the table **is** the start of the bucket list heads, and an overflow was proven in the field:
  entry 8193 landed on `g_bucket[3]`, and the values the renderer then read as a pointer are
  byte-exactly the corner indices of GARDEN cell 7406, slots 5 and 7; each of which occurs exactly
  **once** in all 334,396 faces of the game.
* **The vertex cache.** 16384 slots of 0x40 bytes. It aborts *cleanly* but leaves torn geometry
  **until the level reloads**, because gate 2 jumps behind the `touched` reset loop. It is therefore
  braked earlier and harder than the cells.

Both counters are watched per frame. The effective scale is only ever lowered, never raised again:
better a permanently shorter view than a crash ten minutes later.

## Known limitations

* `NpcRangeScale` changes gameplay: an actor created earlier thinks earlier. 1127 of 1315 scannable
  placements activate inside the visible picture already, so this is the knob that removes pop-in,  and the reason its cap is 2.0 against a 128-slot character pool.
* Two-sided drawing **softens** the see-through look; it does not close the hole. A severed limb is
  not a cut mesh, `bapobj_detachNode` only hides a node, there is no cap and no cut geometry.
* Without the per-frame hook there is no watchdog, and the range is then **held at 1.0** rather than
  trusted.
* The relocated buffer is never freed, not even at detach: the bucket heads can still point into it,
  and a stale pointer into valid memory is harmless where one into freed address space is not.
* The no-pop-in limit uses the **horizontal** field of view only. The cut test is two-dimensional
  (`dx^2 + dz^2`), so a camera with a lot of pitch sees geometry at a depth this does not model.
* The cut edge is taken from the level's current cell, which is what `bapmat_viewDistance` returns.
  A cell whose `bapCell+0x03` override differs from the one the camera stands in is not modelled,  the fog eases towards the new value when the camera walks into it.
* **`fx_rampFog` no longer draws anything in this regime, and that predates this change.** The
  cutscene tint and the fade to black (`0x0043906E`) walk the **device's** fog start `[0x866FA8]`,
  which only table fog reads. With `VertexFog=1` the fog is computed from `world+0x218/+0x21C`
  instead, so those effects are inert. Nothing here made that worse and nothing here fixes it; it
  is a consequence of the regime switch and belongs in its own change.

## Fallback behaviour

Each patch installs independently and says so. If `gather_append` does not match, the watchdog is
off and the scale is pinned to 1.0. If the camera cannot be observed, the radius cap assumes the
authored 63 degrees, the fog assumes 60 degrees, which is exactly the authored band, and both say so.

If `level_pointer` does not resolve, or there is no per-frame hook, there is **no fog tick**: the
band is still computed and written once per level, so a field-of-view change taken mid-session
reaches the fog at the next level load rather than at once. Logged as such.

The tick also yields rather than insists. It writes only while the record still holds exactly what
it last put there; anything that deliberately changes `world+0x218`/`+0x21C` keeps its value until
`baplight_applyLevelFog` runs again. A whole-image sweep for those two displacements finds four
writers and no more, `0x0041CC47`/`0x0041CC54` (the world defaults 10.0/22.0) and
`0x0041D0A3`/`0x0041D0B5` (the B3D header copy), so today that clause only ever fires on a level
load, but it is what makes the coupling safe next to a future one.

The fog regime is the one all-or-nothing part: a single transaction over three sites. If any of the
three does not resolve or does not carry the expected bytes, **none** of them is written and the log
says the regime is unchanged, because half of that change would either do nothing or paint the
world in the fog colour.

## Testing status

Built and linked, `/W4 /WX` clean. `fog_regime_test` covers the arithmetic: the eleven shipped
bands come back bit-exact at the authored field of view, no band inverts over 30-170 degrees and every cut
edge 2-64, the authored profile survives every ratio, the easing agrees between 30 and 120 fps, and
repeated evaluation is bit-identical. Offline verification passes on all four retail executables,
including the table/bucket cross-check and the three-hit count on the ecx append blocks.

**Not accepted in game; nothing here has been run.** The fog-regime change in particular has been
proven only on the bytes: the vertex format, the capability bit, the two `FOGTABLEMODE` writers and
the constant-zero specular fallback are all read out of the image, but whether the fog is then
visible on screen can only be established by running the game. The lines to look for are:

```
[view_distance_fix] distance fog runs on the engine's own per-vertex ramp: ...
[view_distance_fix] fog tick active - g_level ........, g_frameDelta ........, settle 1.50 s
[view_distance_fix] fog band coupled at 0041F14A: ...
[view_distance_fix] level fog 6.0..14.0 -> 5.7..13.2 (draw distance 16, 60.0 degrees)
```

The last one appears once per level load and is the one to read the numbers off. Its absence, or a
"the fog regime is NOT changed" / "no per-frame fog tick" warning next to it, means that part
declined.
