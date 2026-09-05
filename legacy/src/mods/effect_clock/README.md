# effect_clock

**Produces:** `effect_clock.dll` -> `mods\`

Lightning arcs, flickering objects and halo brightness stop speeding up with the frame rate. Three
places in the drawing path draw a fresh random number every time they are drawn, and the drawing
path runs at whatever rate the display does. This DLL makes all three answer from the simulation's
substep counter instead, which advances 32 times a second whatever the display is doing.

At 30 frames a second, the rate the game was authored for, this changes nothing you can see. It
only takes effect above that, and what it restores is the authored appearance rather than a new
one.

## Supported executables

Three builds of this engine ship inside one installation, and all three were checked: the retail
`WMAIN.EXE`, `wmain.exe`, and `obi.exe`, which is a recompile. The German retail executable is byte
identical to the English one, so it is the same build rather than a fourth. On `obi.exe` the five
code anchors sit at the same addresses, while the two data cells this DLL reads have moved:
the random seed to `0x004BA5CC` and the substep counter to `0x004B8810`. Both are read out of a
matched operand rather than from a table, so that build is handled by the same code.

Every site resolves by pattern. A pattern that matches anything other than exactly once disables
that one patch and says so in the log; the others still install.

## Configuration: `[effect_clock]`

| Key | Default | Range | Meaning |
|---|---|---|---|
| `Enabled` | `1` | | |
| `PaceLightning` | `1` | | pace the lightning arcs |
| `PaceFlicker` | `1` | | pace the per object flicker flag |
| `PaceHalo` | `1` | | pace the halo brightness jitter |
| `SubstepsPerRoll` | `1` | 1 to 32 | how many simulation steps an effect keeps its value |

`SubstepsPerRoll=1` gives 32 re-rolls a second, which is the authored rate. Raise it for slower,
calmer arcs; at `32` an effect changes once a second. A value outside the range is refused with a
warning and `1` is used.

## Engine locations

| Site | Retail VA | What |
|---|---|---|
| the random generator | `0x0049A580` | located by pattern; its seed cell `0x004BA61C` is read out of the operand and never patched |
| the substep counter | its increment inside the simulation driver | the cell address is read out of the operand |
| the arc pool call | `0x00438D94` | call displacement rewritten to a bracket |
| the arc pool | `0x0043D095` | the callee itself is untouched |
| the flicker draw | `0x00411438` | call displacement rewritten to a replacement generator |
| the halo draw | `0x00439FBD` | call displacement rewritten to a replacement generator |
| `render_frameEnd` | via `common/frame_hook.c` | detoured, to reset the two draw order counters each frame |

Three call displacements, four bytes each. No engine function is detoured by this DLL directly, and
no callee is modified, so anything else that calls the generator or the arc pool is unaffected.

## How the three sites were chosen

By census, because an effect that re-rolls itself has to draw from the generator and therefore has
to appear in its caller list. A sweep of `E8 rel32` over the whole code section finds **136** call
sites of `0x0049A580`. Intersecting those with the functions a drawn frame reaches leaves **18**,
across eleven functions:

```
0x00411028  bapobj_drawAll      1 site    the FLICKER skip
0x004146BE                      1
0x00416232                      2
0x00417711                      1
0x0041FB9E  bgl_randomUnitXYZ   2         the random supplier itself
0x00439AB6  fxfade              1         the halo pool
0x0043A229  fxprint             1
0x0043ABF0  fxshield            2
0x0043B701  fxshield            1
0x0043D095  fxzappo             3         the arc pool
0x0043D784  fxzappo             3         the recursive midpoint displacement
```

A first pass attributed all eighteen to only two functions. It grouped each site under the nearest
preceding entry point, and that heuristic walks straight across function boundaries. A backwards
scan for `55 8B EC` preceded by a `ret`, an `int3` or a padding `nop` gives the eleven above.

**Being on the drawing path is not the same as being clocked per frame.** Each of the eleven was
traced up its callers to the module proc it belongs to, and the message was read out of that proc's
jump table in the image, the dispatch shape being `msg -= N; if (msg > M) default;
jmp [msg*4 + TABLE]`:

```
fxzappo    0x0043D095   root 0x00438CD0   message 0x0D   PER FRAME
fxshield   0x0043B701   root 0x00438CD0   message 0x0E   per substep, already correct
bapobj     0x00411028   root 0x00410870   message 0x0D   PER FRAME
fxfade     0x00439AB6   reached from bapobj_drawAll      PER FRAME
0x004146BE  under shot_updateAll 0x004524B9, a task      per substep, already correct
0x00416232  messages 0x06 and 0x07                       setup
0x00417711  messages 0x05, 0x08 and 0x09                 setup
0x0043ABF0  reached from the enemy code through aiext    simulation
0x0043A229  reached from the SW_TEXT widget              user interface
```

## What was deliberately left alone

* **The body sphere**, `fxshield`. It lives in the same module as the arcs, carries three of the
  eighteen sites across its two functions, and from the outside looks exactly like the arcs: a
  random shape rebuilt around a body. It is on message `0x0E`, the substep broadcast, so it is
  already clocked correctly and pacing it would have made it worse. Reading the jump table rather
  than trusting the resemblance is what caught that.
* **`bgl_randomUnitXYZ` at `0x0041FB9E`**, with its wrapper `bgl_randomUnit` at `0x0041FC2C`. Its
  two draws are the textbook uniform point on a sphere, `z = 2r/32767 - 1` and
  `theta = 360*r/32767`, then `1 - z*z`; the angle constant is 360, so the module works in degrees.
  It is not an effect that re-rolls itself, it is the supplier effects draw from, and how often it
  is asked is its caller's business. A per frame rate there would be the caller's defect.
* The shot effects under `shot_updateAll`, which run inside a task on the substep clock; the two
  setup sites, which run on the create and destroy messages; the enemy path reached through
  `aiext`, which is simulation; and the `SW_TEXT` widget path, which is the user interface.

## Known limitations

* **The flicker and the halo use the object's place in the frame's draw order**, not its identity,
  because the engine's draw does not tell the generator who is asking. That ordinal is stable in
  practice: the object list is rebuilt and sorted by asset name on each draw, and no object spawns
  or dies between two frames of one simulation step. If it ever stopped holding, the worst case is
  the behaviour being replaced, an object changing its mind inside a step.
* **One degenerate case in the flicker mixer.** When the ordinal plus one equals the tick the two
  terms cancel and the mixer maps zero to zero, so that object is skipped for that one simulation
  step. It happens at most once per ordinal in a session, against an original that skipped the same
  object several times a second.
* **The drawing path no longer advances the engine's global random generator** when the flicker or
  the halo is paced, because their replacements do not call it. The simulation's own sequence
  therefore stops depending on how many frames were drawn. The original was already frame rate
  dependent at exactly that point, so no single authentic behaviour is given up, but it is a
  change. The arc bracket does not have this property: it restores the seed on the way out, so the
  simulation sees the sequence it would have seen.
* Changing a key in the ini while the game runs does nothing. Everything here is decided once, at
  install.

## Fallback behaviour

Each of the three is independent. If the arc call site does not resolve, the flicker and the halo
are still paced, and the other way round.

If either data cell cannot be resolved and validated, **no** patch is applied at all, because
nothing here can be put on a clock without both.

If the per frame hook cannot be installed, the flicker and the halo are left alone. A draw order
counter that never restarts would answer differently in successive frames of one substep, which is
the defect rather than the repair. The arcs do not need the hook and are still paced.

Both cells are validated once at install and then read as plain loads. The hooks run on the drawing
path, where a range check would cost a system call per frame.

## Testing status

**Accepted in game**, in the 1.5.0 build from this tree, which was played through by hand. It
also builds `/W4 /WX` clean here with the unit test suite green, though neither of those says
anything about the patch on its own: they say the code compiles and that the tests which exist
still pass.

**The mixers are unit tested.** The two replacement generators are pure arithmetic and are exactly the
kind of thing this project expects to be testable without the game: same tick and same ordinal must
give the same answer, a different tick must give an unrelated one, and every answer must land in 0
to 32767 so the engine's own scaling still holds. `unittests/substep_noise.c` drives them: the range
the engine's own scaling depends on, that one substep answers alike across its frames while the next
re-rolls, that two objects in one frame do not agree, and the cancellation the comments name.

Every byte level claim above is read out of the shipped executables, and the pattern resolution was
re-run against all three builds while preparing this tree rather than taken from the source it
came from. All five patterns are unique in all three: the arc anchor matches at `0x00438D8B` in each of them,
and the flicker and halo anchors are unique in each. In `obi.exe` both redirected calls name
`0x0049A520` where the retail builds name `0x0049A580`, which is the case the operand reading
exists for.

To check in game: run at an uncapped frame rate somewhere with lightning, and confirm the bolts
crackle at a visible rate rather than blurring into noise. The log names each site it took and the
address it redirected, so a patch that installed and then did nothing is visible there:

```
[effect_clock]   random seed        004BA61C from the operand at 0049A580
[effect_clock]   flicker test     call at 00411438 redirected, was 0049A580
[effect_clock]   halo jitter      call at 00439FBD redirected, was 0049A580
```
