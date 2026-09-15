# render_guard

**Produces:** `render_guard.dll` -> `mods\`, from `render_guard.c` (the bounds and the depth
comparison), `face_bounds.c` (the two comparisons, testable), `flat_quad.c` (the engine's flat
screen quad with vertices every driver draws), `node_verts.c` (the x87 stack the halo draw
leaves one short) and `halo_verts.c` (a halo drawn from twelve floats nothing wrote).

Two arrays in the engine's deferred face path are filled without checking either bound, and one of
them ends on the submitting function's saved return address. The same DLL replaces a depth
comparison the engine can compute as a value Direct3D does not define, it replaces the engine's
flat screen quad, whose vertices Intel's Direct3D 9 driver does not draw, it balances the x87
stack across the node vertex copy, which every halo drawn left one short, and it culls a halo on
a node with no mesh, which the engine built out of its own stale stack and drew as a blade out of
a Jedi's hand.

Nothing here changes what a scene inside the authored limits draws.

## Supported executables

Three builds of this engine ship inside one installation and all three were checked: the retail
`WMAIN.EXE`, `wmain.exe`, and `obi.exe`, which is a recompile. The German retail executable is
byte identical to the English one, so it is the same build and not a fourth. All three sites
resolve on every one of them. Every site is found by pattern, and a pattern that does not match
disables that one part and says so in the log.

## Configuration: `[render_guard]`

| Key | Default | Range | Meaning |
|---|---|---|---|
| `Enabled` | `1` | | `0` installs nothing and the log says so. |
| `MaxDeferredVertices` | `30` | 1-31 | the most vertices one deferred face may carry |
| `PoolCapacityVertices` | `8196` | 0 or more | the ceiling on the shared vertex pool; `0` switches that second bound off |
| `GuardDepthCompare` | `1` | | substitute LESS when the comparison mapper answers 0 |
| `GuardFlatQuads` | `1` | | draw the engine's flat screen quads with `rhw = 1` and `z = 0` instead of its own `rhw = 0` and, on a 16-bit depth buffer, `z = 1.0`, which Intel does not draw |
| `BalanceNodeVerts` | `1` | | make the two node vertex routines return nothing on every path and their three callers pop nothing, so a halo drawn no longer leaves the x87 stack pointer one higher; `0` leaves the stack as the game shipped it |
| `GuardHaloVerts` | `1` | | cull a halo whose node has no mesh: `halo_draw`'s copy of the node's vertices zeroes its buffer when the copy has nothing to read, so the quad projects to a point and the engine's own two pixel cull drops it, instead of drawing it from whatever the stack held; `0` leaves the stack read as the game shipped it |
| `DeferTrace` | `0` | | a measurement: sample the x87 status word either side of every deferred face and name any face across which the stack pointer moved; see the section below |

`MaxDeferredVertices` above 31 or below 1 is refused with a log line and the authored 30 is used
instead: 32 is the array and its 33rd entry is the return address, so there is nothing sensible to
configure up there.

`PoolCapacityVertices` defaults to the queue entry ceiling read out of the engine's own instruction
at install, which is 8196. A value in the ini overrides that, because raising the bound is the
documented answer if a refusal ever shows up in ordinary play. A negative value is refused with a
log line.

## Engine locations

| Site | Retail VA | On `obi.exe` | What |
|---|---|---|---|
| the deferred face submit | `0x00487D20` | `0x00487CC0` | detoured, 8 byte prologue |
| its entry ceiling immediate | site + `0x12` | same | read, never written |
| the pool cursor advance | `0x00487EEB` | `0x00487E8B` | its operand is read to locate the cursor cell at `0x00867380`; not patched |
| the capability to comparison mapper | `0x0048AAF9` | `0x0048AA99` | detoured, 11 byte prologue |
| the flat screen quad | `0x00419660` | untested | replaced whole, 6 byte prologue; its four callees are read out of its body at `+0x153`, `+0x15C`, `+0x16C` and `+0x18B` |
| the node range return of `bapobj_getNodeMeshVerts` and its twin | `0x004137D0`, `0x004138A7` region | untested | one pattern matching twice, the `fld` of 0.0 at each hit made a no-op |
| the missing table return of the same two | `0x004137FB`, `0x004138D1` region | untested | the same, the second failure arm |
| `halo_draw`'s pop after the copy | `0x00439B46` | untested | `fstp st(0)` made a no-op |
| `Plr_CaptureBladeMesh`'s pop | `0x00449884` | untested | the same |
| `Plr_SetBladeSize`'s pop after the write | `0x00449E41` | untested | the same, the twin's one caller |
| `halo_draw`'s call of the copy | `0x00439B41` | untested | the `call rel32` redirected to this DLL's copy, the callee untouched for its other two callers; the buffer is `[ebp-0x70]`, 48 bytes under the node pose |

## Hooks installed

Two chaining detours and no writes anywhere else in the image. The face hook checks two numbers and
either returns null or calls the original with the arguments it was given. The mapper hook calls
the original first and only looks at its answer.

The 8 byte prologue is not a typo. The submit function has no frame pointer at all: it loads its
queue count from an absolute address, reserves its locals, and pushes the callee saved registers
only afterwards, so the first instruction boundary at or past five bytes is eight.

## The flat screen quad

The engine draws every untextured rectangle on the screen through one routine: the fade veil, the
letterbox bars, the menu backdrops, the black the loading bar is repainted over between steps. It
writes each vertex with `rhw = 0` and, on a 16-bit depth buffer (the one dxwrapper creates for
this game, `D3DFMT_D16` in its log), `z = 1.0`, the far plane. NVIDIA and AMD draw that. Intel
does not: a report from an Intel UHD laptop had the developer panel's text and pointer with none
of its fills, the movie player's post-movie curtain missing (seen as the character dropping in
after a movie), and, once those two were repaired at their callers through `common/screen_fill.c`,
the loading screen's bar frames and percentages piling up on each other, because the black behind
them never landed. A quad at the far plane loses a LESS depth test against a cleared buffer, and a
zero `rhw` is a division the driver may drop the primitive over.

`flat_quad.c` replaces the routine whole. It is the routine as the bytes at `0x00419660` have it
with two fields changed, `rhw = 1` and `z = 0`, the values Direct3D defines for a transformed
vertex; `z = 0` is also what the routine itself writes on any depth buffer that is not 16-bit.
Both arms are kept, the immediate one making the routine's own three host calls and the queued
one handing the fan to the engine's sorted queue as before; the four targets are read out of the
routine's body with the bytes around each call checked first, and if anything does not read the
routine is left as it is and the log says so. On a driver that drew the original the pixels are
the same.

**The snap is the part that had to come from the bytes.** The first version of this file copied
the recreation's C, which floors the far edge after adding `0.9999`. The bytes add the word at
`0x004a81b0` (the float `0xbf7ff972`, subtracted, so `0.9999` added) and then call the CRT's
`ceil` at `0x0049a960` before its `floor`; only the near edges floor alone. For an integer far
edge the difference is a whole pixel: the cutscene letterbox's `W - 1` becomes `W` in the game
and stayed `W - 1` in the copy, so every quad the copy drew was a pixel short on the right and
at the bottom, on every driver, and the bars showed a sliver at their edges on NVIDIA and on
the Deck alike (2026-09-14; `GuardFlatQuads=0` restored them, and changing `rhw` or `z` alone did
not). The recreation's gate marks this routine MISMATCH, which was the warning: a MISMATCH body
is a reading, not the routine, and a replacement is written from the bytes.

The two callers that resolve this routine by pattern, `dev_overlay` and `fmv_player`, look for it
the way a detoured site is looked for, since the load order that puts them ahead of this DLL is
alphabetical and not promised.

## What is wrong

**The outcode array.** The function keeps a 32 byte array of per vertex clip outcodes on its own
stack at `[esp+0x18]` and fills one byte per vertex, with nothing comparing the count against the
array's length. The frame is `sub esp,0x28` plus four pushes, so `[esp+0x38]` is the saved return
address, that byte is `outcode[32]`, and the 33rd vertex of a face writes it. The symptom is a
corrupted return whose target depends on where the vertex landed on screen, so it would never
reproduce twice the same way.

Thirty is the authored contract, not an inference. The editor build still carries the assert the
shipping build compiles out, and its text names the number: "Can't have more than 30 sided polys as
special faces". Thirty three is where it actually breaks, so refusing at 31 refuses nothing a
correct scene submits. The immediate path has a different limit, 64, which is the size of its own
scratch vertex buffer; only the deferred path has 30.

**The vertex pool.** Every accepted face copies its vertices into one global pool and advances a
cursor by the vertex count:

```
00487ED7  mov edi,[0x00867380]
00487EDD  shl edi,5                 stride 32 bytes, proven here
00487EE2  add edi,0x00734C10        and this is the pool's base
00487EEB  mov ebx,[0x00867380]
00487EF1  add ebx,esi               esi is the vertex count
00487EF3  mov [0x00867380],ebx      stored back, with no test at all
```

That is the pool's entire bookkeeping. The queue's *entry* count is tested against a ceiling
on the function's first instruction, so a scene cannot submit unlimited faces; the *vertex* cursor
is tested against nothing, so a scene that stays under the entry ceiling can still run it past the
end of the pool.

**The depth comparison.** The mapper that turns a device capability into a `D3DCMPFUNC` has one arm
per capability and no arm for LESS:

```
0x01 NEVER        -> 1      0x02 LESS         -> no arm at all
0x04 EQUAL        -> 3      0x08 LESSEQUAL    -> 4
0x10 GREATER      -> 5      0x20 NOTEQUAL     -> 6
0x40 GREATEREQUAL -> 7      0x80 ALWAYS       -> 8
```

An input of exactly `0x02` falls through every branch and the accumulator comes back as it was
initialised, zero, which is not a member of `D3DCMPFUNC`. The engine hands that to `SetRenderState`
as the depth comparison. The input is reachable: the device open path stores exactly 2 into the
capability cell at `0x00866FC8` when the device fails its comparison probe.

## What a refusal costs

One polygon, in a frame that was about to corrupt memory. The refusal is the same null the function
already returns when its queue is full. That is an ordinary condition in a busy scene, so every
caller of the function already handles it. No new path is introduced.

## The pool ceiling is derived, not measured

The engine never names the pool's capacity. The only number in the image it can be derived from is
the queue's entry ceiling, `0x2004`, the count the same function tests on its first instruction.
That is a derivation, not a measurement.

A census was attempted and did not settle it. Scanning `.text` for dword literals landing between
the pool's base at `0x00734C10` and a generous end at `0x00790000` returns 74 hits at byte
alignment, 15 of them dword aligned and only 6 real instruction operands, so most are coincidences
inside instructions, not addresses. From the other side, the nearest address above the pool's
base that anything else is known to use is `0x008439AC`, which is 1.06 MB higher, so the pool is not
immediately followed by anything identified. The derived capacity, `0x2004` vertices of 32 bytes
each, which is `0x40080` bytes ending at `0x00774C90`, is consistent with that gap but not proven by
it.

So a refusal on this bound during ordinary play is evidence that the pool is bigger than the
derivation, not evidence that the scene is too complex. The log says that the first time it
happens, and `PoolCapacityVertices` is how to answer it.

## Known limitations

* **The depth comparison repair is not expected to fire on modern hardware.** Any Direct3D 9 device
  advertises GREATER, so the mapper reaches the `0x10` arm and the hook passes the answer through
  untouched. Whether any real device takes the probe failure path has not been established, so
  this substitutes and assumes nothing either way.
* **Only the first refusal of each kind is logged.** The counters keep running after that but no
  later line prints them, so a session's total is not visible in `engine_fixes.log`.
* **The immediate path is not guarded.** Its limit is 64 and it is a different array in a different
  function; nothing here touches it.
* The pool cursor is read fresh on every call, never tracked, because the engine zeroes it
  when it drains the queue and this hook has no reliable way to see that moment.
* There is no uninstall, a property of the shared detour layer, not of this feature.
* **The flat quad repair has not been seen working on Intel.** It was built after the reporter had
  gone. What it does on Intel follows from the same change having brought back the panel fills
  and the curtain there, through `common/screen_fill.c`, and from the loading bar's black going
  through the same vertices. The snap correction of 2026-09-14 changes nothing on that side:
  the vertices are a pixel further out, the fields are the same.

## Fallback behaviour

If the deferred face submit does not resolve, neither bound is guarded and the DLL says so and
stops. If the pool cursor does not resolve, or its operand does not point inside the host image at
readable memory, the pool bound is switched off and the outcode bound is installed alone. If the
entry ceiling cannot be read out of the matched bytes, the pool bound falls back to whatever the
ini carries and stays off when the ini carries nothing.

The depth comparison repair can fail to resolve or fail to detour without affecting the bounds. The
reverse does not hold: it is attempted only once the face hook is in place, so a build on which the
submit pattern does not resolve loses this repair too. Do not read a silent log as "the mapper
was fine here".

Nothing is written to the image on any of these paths, so a partial install leaves the game exactly
as it found it.

## The x87 stack left one short by every halo drawn

Found 2026-09-15, at the end of a hunt that began on Coruscant with a lightsaber blade drawn
out of the player's hand and came back in Mos Espa out of Obi-Wan's, and that framerate_fix's
README carries in full. Its last cut was the diagnostics DLL's `X87` observer, which samples the
x87 status word either side of seven calls of the object draw: the stack pointer moved across
`halo_drawForThing` on every frame the player's halos drew, once a frame, a net pop, and across
nothing else, not the model draw, the track advance, the clip events, the shadow projector or
the queue flush.

The bytes say why. `bapobj_getNodeMeshVerts` (`0x0041378A`) copies a node's mesh vertices out
and returns nothing on the path that does the copy; its two failure arms, a node index past the
model's count and a model with no vertex table, leave through `fld` of a 0.0, a float return.
Its twin `bapobj_setNodeMeshVerts` (`0x00413866`) has the same shape. Every caller of either
follows the call with `fstp st(0)`, discarding a float the success path never pushed: `halo_draw`
for each halo it draws, `Plr_CaptureBladeMesh` at the player's spawn and `Plr_SetBladeSize` as
the blade grows and shrinks. That pop from an empty stack is a stack underflow: TOP in the
status word goes up by one, every register still tagged empty, and the pointer stays wrong for
the rest of the frame and into the next, cycling through all eight values over eight frames
(measured in the trace, with the mods folder cut to one DLL and then with every switch in that
DLL off, so the drift is the game's own). The recreation of the engine had already noted, at
that function, "either the return value is never used, or this is a bug that has shipped"; it
is the second, and the value is never used either, since every caller discards it.

Halos are attached only to the models in the engine's halo colour table, the Jedi, so only a
Jedi on screen runs the fault, and the garbage the wrong pointer produces lands in the halo and
blade geometry of that same model: a beam out of a Jedi's hand, Obi-Wan's where he stands at
the ship in Mos Espa, the player's own on Coruscant. Which float took it shifted with the code
around the fault, so the beam moved with every rebuild of this project, and the first hunt,
which took the CRT float classifier out of framerate_fix's blend hook, saw it go and could not
say why. In 1999, under the DirectX 6 runtime, the same underflow left no visible
trace; through dxwrapper's Direct3D 9 path it does. That last sentence is inferred from the two
environments; everything above it is measured.

`node_verts.c` makes the two functions return nothing on every path and the three callers pop
nothing: the four `fld` of the failure arms (six bytes each) and the three `fstp st(0)` (two
bytes each) become no-ops, through the patch journal, each site checked for the exact opcode
before it is written and every earlier write put back if one refuses. The two failure-arm
patterns are expected to match exactly twice, once in each function; the three caller patterns
once each. Played after the repair with the observer still on: zero moves across every call
for the whole run, the frame-end pointer at 0 throughout, and no beam. Then played with the
observer off, on Windows and under Wine, and the beam was back with the stack still balanced.
The underflow was one of two faults in the same halo, and the observer's own frames had hidden
the other; the next section has it.

## A halo drawn from twelve floats nothing wrote

`halo_draw` keeps a 12 float buffer on its stack, `[ebp-0x70]`, for the four vertices of the
node the halo hangs on, and fills it with one call of `bapobj_getNodeMeshVerts`. That routine
writes nothing when the node index is past the model's node count or the node carries no mesh:
it returns, and `halo_draw` reads the two vertices it wants, indices 0 and 2, out of whatever the
stack held before the call. The halo is then a screen quad between two stale points, anchored on
the node's pose, which for a blade halo is the hand. Usually the stale numbers are small and the
routine's own "shorter than two pixels" test culls the quad; with the x87 stack one short they
were the indefinite NaN, which that test cannot cull; and with the stack balanced they are
whatever the calls before the draw left there, which any change of code layout changes. So the
beam came and went with every rebuild of this project, went on Windows and under Wine while the
diagnostics observer's seven detour frames sat in front of the draw, and came back on both the
moment they were taken out.

`halo_verts.c` diverts `halo_draw`'s call alone, through `patch_redirect_call`, so the routine's
other two callers keep it. The replacement makes the same two tests the routine makes, from the
same offsets (`obj+0x9C` the thing, `+4` the model, `model+0x54` the node count, `+0x58` the
nodes at `0xB4` each, `node+0x4C` the mesh index, `model+0x28` the meshes at `0x70` each,
`mesh+0x48` the vertex count, `+0x30` the vertices), copies at most the four vertices the buffer
holds when they pass, and zeroes all twelve floats when they do not. Two zero vertices project to
one point and the engine's own cull leaves nothing drawn. Whether the caller still pops a float
after the call is read from the bytes at install, so the replacement returns one or nothing to
match and the guard is correct with `BalanceNodeVerts` on or off. The first failure on each model
and node is logged with the reason.

Played 2026-09-15 in Mos Espa on the rig with the observer off: one line, `halo on obinpc.3do
node 12: the node carries no mesh`, and the bar out of Obi-Wan's hand gone. Node 12 is the last
of the four blade halos `halo_attachToThing` gives every model in the halo colour table, and on
the NPC Obi-Wan model it is present, switched on and empty, so the fourth halo was built from the
stack on every frame he was drawn. Played the same evening under Wine on NVIDIA and on Intel
and on the Steam Deck, across several levels: the same single line each run and no other model
named, so on the evidence so far his is the only model in the game with an empty halo node.

## A measurement: `DeferTrace`

`[render_guard] DeferTrace=1` samples the x87 status word either side of every deferred face
and names the call when the stack pointer moved across it. It exists for the blade drawn out of
a hand, framerate_fix's README under that heading, and it answered that the pointer never moves
across a deferred face. Off as shipped.

## Considered and not built

Four neighbouring repairs were proposed with this one and each was dropped after reading the bytes
or measuring the assets, not after building.

* **Forcing square textures.** The engine guards that path with a capability that is only cleared
  when `D3DPTEXTURECAPS_SQUAREONLY` is set, which is Voodoo era hardware. Through a Direct3D 9
  translation layer the bit is not set and the path does not run. The engine's logic there is
  conditional and correctly conditional.
* **The 256 pixel texture clamp.** The cache clamps both axes to 1..256 and crops; it does not
  scale. Measuring 4000 of the 6482 exported textures finds the most common sizes to be 32x32,
  16x32, 64x32 and 64x64, and not one of them exceeds 256 in either axis, so the clamp never fires
  on the shipped assets.
* **A 32 bit texture path.** The recorded cost is always `width * height * 2` and there is no 32
  bit path. The source data is 8 bit palettised or 16 bit RGB at those sizes, so a 32 bit
  destination would cost more memory for no more information.
* **Reviving the dead eviction sweep at `0x00489170`.** It is real dead code with zero callers, but
  the same texture census says there is nothing to evict, a level's worth of 32x32 pages being a
  few megabytes. It would also need to be known what re-uploads a record whose surfaces it has just
  released. That is not established. Not worth building blind.

## Testing status

**Offline pattern verification passes on all three builds**, with the addresses in the table above.
That check also caught the first version of the comparison mapper pattern: the obvious
30 byte anchor, from the prologue through the first capability test, matches a sibling function in
the same module as well, and the two only diverge at the second arm, `and edx,3` with `or al,4`
against `and edx,4` with `or al,3`. Extending the pattern through that arm makes it unique
everywhere.

**Both bounds have a unit test.** They are in `face_bounds.c` so that a test can reach them at all,
because this DLL exposes nothing but its install function and the comparisons were static inside it.
`unittests/face_bounds.c` covers both sides of each boundary and the exact boundary itself, a face
carrying no vertices, a cursor that is already past the ceiling, the highest limit the ini will
accept, a ceiling of zero, and the counts that would wrap if the pool question were asked as
`used + count > capacity` instead of as a subtraction. The vertex limit and the pool ceiling are
passed in, so the test drives the same code the game runs without an engine cell anywhere near it.

**The unit test builds and passes.** `face_bounds` is a registered ctest target, so CI runs it.

**The node vertex balance was played on the rig (NVIDIA)** in Mos Espa beside Obi-Wan, with the
diagnostics DLL's x87 observer on: the stack pointer no longer moved across any of the seven
calls it watches and sat at 0 at every frame's end, where before the repair it had moved once a
frame across the halo draw and cycled through every value. The same run under Wine on NVIDIA and
on Intel gave the same zeros.

**The halo guard was played on the rig** in Mos Espa with the observer off, where the balance
alone still drew the bar: the log named `obinpc.3do` node 12 once and the bar was gone. Under
Wine on NVIDIA and on Intel and on the Steam Deck, across several levels, the same one line and
no bar.
**Installed in every played build since it shipped**, as one of the components the installer does
not let a player untick, and no guarded path has been observed firing.

**The flat quad replacement was played on the rig (NVIDIA)** through a level load, a movie, a
cutscene with letterbox bars and the developer panel, all four callers of the routine, with the
log naming the four targets it read (`00488210`, `00488510`, `00487260`, `00487C50`), which match
the reference bytes of the retail image. "Picture unchanged" was the first version's claim and
it was wrong by a pixel at the far edges: the letterbox sliver was reported the next day from a
tester's NVIDIA machine and seen on the Deck, and the snap was rewritten from the bytes. The
corrected routine was then confirmed the same day on both: the tester's RTX 3050 and Chip's
Deck, the bars back where `GuardFlatQuads=0` puts them.

An untriggered session is the expected result, not evidence that the guard works: on a
correct scene and a working device none of the three paths is taken. What can be confirmed in game
is the install line in `engine_fixes.log`, which names the resolved submit address, the vertex
limit in force, the pool cursor address and the ceiling derived from the engine's own instruction.
If any of those is missing, the corresponding part declined and the log line above it says why.
