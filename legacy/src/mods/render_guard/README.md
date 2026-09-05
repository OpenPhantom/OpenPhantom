# render_guard

**Produces:** `render_guard.dll` -> `mods\`

Two arrays in the engine's deferred face path are filled without checking either bound, and one of
them ends on the submitting function's saved return address. The same DLL replaces a depth
comparison the engine can compute as a value Direct3D does not define.

Nothing here changes what a scene inside the authored limits draws.

## Supported executables

Three builds of this engine ship inside one installation and all three were checked: the retail
`WMAIN.EXE`, `wmain.exe`, and `obi.exe`, which is a recompile. The German retail executable is
byte identical to the English one, so it is the same build rather than a fourth. All three sites
resolve on every one of them. Every site is found by pattern, and a pattern that does not match
disables that one part and says so in the log.

## Configuration: `[render_guard]`

| Key | Default | Range | Meaning |
|---|---|---|---|
| `Enabled` | `1` | | |
| `MaxDeferredVertices` | `30` | 1-31 | the most vertices one deferred face may carry |
| `PoolCapacityVertices` | `8196` | 0 or more | the ceiling on the shared vertex pool; `0` switches that second bound off |
| `GuardDepthCompare` | `1` | | substitute LESS when the comparison mapper answers 0 |

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

## Hooks installed

Two chaining detours and no writes anywhere else in the image. The face hook checks two numbers and
either returns null or calls the original with the arguments it was given. The mapper hook calls
the original first and only looks at its answer.

The 8 byte prologue is not a typo. The submit function has no frame pointer at all: it loads its
queue count from an absolute address, reserves its locals, and pushes the callee saved registers
only afterwards, so the first instruction boundary at or past five bytes is eight.

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

That is the whole of the pool's bookkeeping. The queue's *entry* count is tested against a ceiling
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
already returns when its queue is full, and that is an ordinary condition in a busy scene, so every
caller of the function already handles it. No new path is introduced.

## The pool ceiling is derived, not measured, and it is labelled as one

The engine never names the pool's capacity. The only number in the image it can be derived from is
the queue's entry ceiling, `0x2004`, which is the count the same function tests on its first
instruction, and that is a derivation rather than a measurement.

A census was attempted and did not settle it. Scanning `.text` for dword literals landing between
the pool's base at `0x00734C10` and a generous end at `0x00790000` returns 74 hits at byte
alignment, 15 of them dword aligned and only 6 real instruction operands, so most are coincidences
inside instructions rather than addresses. From the other side, the nearest address above the pool's
base that anything else is known to use is `0x008439AC`, which is 1.06 MB higher, so the pool is not
immediately followed by anything identified. The derived capacity, `0x2004` vertices of 32 bytes
each, which is `0x40080` bytes ending at `0x00774C90`, is consistent with that gap but not proven by
it.

So a refusal on this bound during ordinary play is evidence that the pool is bigger than the
derivation, not evidence that the scene is too complex. The log says exactly that the first time it
happens, and `PoolCapacityVertices` is how to answer it.

## Known limitations

* **The depth comparison repair is not expected to fire on modern hardware.** Any Direct3D 9 device
  advertises GREATER, so the mapper reaches the `0x10` arm and the hook passes the answer through
  untouched. Whether any real device takes the probe failure path has not been established, which
  is why this substitutes rather than assuming either way.
* **Only the first refusal of each kind is logged.** The counters keep running after that but no
  later line prints them, so a session's total is not visible in `engine_fixes.log`.
* **The immediate path is not guarded.** Its limit is 64 and it is a different array in a different
  function; nothing here touches it.
* The pool cursor is read fresh on every call rather than tracked, because the engine zeroes it
  when it drains the queue and this hook has no reliable way to see that moment.
* There is no uninstall, which is a property of the shared detour layer rather than of this
  feature.

## Fallback behaviour

If the deferred face submit does not resolve, neither bound is guarded and the DLL says so and
stops. If the pool cursor does not resolve, or its operand does not point inside the host image at
readable memory, the pool bound is switched off and the outcode bound is installed alone. If the
entry ceiling cannot be read out of the matched bytes, the pool bound falls back to whatever the
ini carries and stays off when the ini carries nothing.

The depth comparison repair can fail to resolve or fail to detour without affecting the bounds. The
reverse does not hold: it is attempted only once the face hook is in place, so a build on which the
submit pattern does not resolve loses this repair too. That is worth knowing before anyone reads a
silent log as "the mapper was fine here".

Nothing is written to the image on any of these paths, so a partial install leaves the game exactly
as it found it.

## Considered and not built

Four neighbouring repairs were proposed with this one and each was dropped after reading the bytes
or measuring the assets, not after building.

* **Forcing square textures.** The engine guards that path with a capability that is only cleared
  when `D3DPTEXTURECAPS_SQUAREONLY` is set, which is Voodoo era hardware. Through a Direct3D 9
  translation layer the bit is not set and the path does not run. The engine's logic there is
  conditional and correctly conditional.
* **The 256 pixel texture clamp.** The cache clamps both axes to 1..256 and crops rather than
  scales. Measuring 4000 of the 6482 exported textures finds the most common sizes to be 32x32,
  16x32, 64x32 and 64x64, and not one of them exceeds 256 in either axis, so the clamp never fires
  on the shipped assets.
* **A 32 bit texture path.** The recorded cost is always `width * height * 2` and there is no 32
  bit path. The source data is 8 bit palettised or 16 bit RGB at those sizes, so a 32 bit
  destination would cost more memory for no more information.
* **Reviving the dead eviction sweep at `0x00489170`.** It is real dead code with zero callers, but
  the same texture census says there is nothing to evict, a level's worth of 32x32 pages being a
  few megabytes. It would also need to be known what re-uploads a record whose surfaces it has just
  released, and that is not established. Not worth building blind.

## Testing status

**Offline pattern verification passes on all three builds**, with the addresses in the table above.
That check is also what caught the first version of the comparison mapper pattern: the obvious
30 byte anchor, from the prologue through the first capability test, matches a sibling function in
the same module as well, and the two only diverge at the second arm, `and edx,3` with `or al,4`
against `and edx,4` with `or al,3`. Extending the pattern through that arm makes it unique
everywhere.

**Both bounds have a unit test.** They are in `face_bounds.c` so that a test can reach them at all,
because this DLL exposes nothing but its install function and the comparisons were static inside it.
`unittests/face_bounds.c` covers both sides of each boundary and the exact boundary itself, a face
carrying no vertices, a cursor that is already past the ceiling, the highest limit the ini will
accept, a ceiling of zero, and the counts that would wrap if the pool question were asked as
`used + count > capacity` rather than as a subtraction. The vertex limit and the pool ceiling are
passed in, so the test drives the same code the game runs without an engine cell anywhere near it.

**Not compiled and not run in the game.** The test was written but has not been built here.

An untriggered session is the expected result rather than evidence that the guard works: on a
correct scene and a working device none of the three paths is taken. What can be confirmed in game
is the install line in `engine_fixes.log`, which names the resolved submit address, the vertex
limit in force, the pool cursor address and the ceiling derived from the engine's own instruction.
If any of those is missing, the corresponding part declined and the log line above it says why.
