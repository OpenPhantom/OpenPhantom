# diagnostics

**Produces:** `diagnostics.dll` -> `mods\`

Per-subsystem runtime observation, switchable in the ini. **Off by default.**

This is a tool for finding faults, not a feature. Every hook calls the original, returns its result
unchanged, and touches neither registers nor flags nor any game field. A diagnostic hook that
changes the game is a defect.

**With `Enabled=0` this DLL touches not one byte of the image and does not even read `.text`.**

## Supported executables

Retail `WMAIN.EXE` (EN/DE) and the Fix Pack build. Each observer resolves independently; on
`obi.exe` most do not, and each one that fails switches only itself off, with a log line.

## Configuration: `[diagnostics]`

| Key | Default | Meaning |
|---|---|---|
| `Enabled` | `0` | the master switch |
| `Audio` | `0` | 1 play/stop/volume/zones, 2 plus the channel allocation |
| `Music` | `0` | 1 state/sequence/volume, with the muscript symbol names |
| `Trigger` | `0` | 1 mover commands, 2 plus every integrator phase change, 3 plus the mover call-site census, 4 plus the render-path census (entries to bapmap_polyToWorld and bapvrt_transformWorld), 5 plus a call-site census for bapmap_polyToWorld itself, 6 plus call-site censuses for the two traces bapmap_polyToWorld runs behind (FUN_0040be00 general trace, FUN_0040c2be floor trace) |
| `Fsm` | `0` | 1 AI mode changes, 2 plus **every executed opcode** |
| `Level` | `0` | 1 level loading and the cutscene lock |
| `Player` | `0` | 1 mode changes of the 14-mode state machine |
| `Dialogue` | `0` | 1 spoken lines and voice files |
| `Fx` | `0` | 1 emitters on/off/destroyed, 2 plus every decal **stamped**, 3 plus every decal **drawn** |
| `Frame` | `0` | 1 one frame-time summary a second, 2 plus the frames around every hitch. A hitch is both a percentage past the median of the last 64 frames and at least two milliseconds past it; without that floor the instrument reports scheduler noise as hitches and, because each dump writes from inside the frame callback, stretches the frames it measures |
| `FrameHitchPercent` | `0` | how far past the median counts as a hitch. 0 uses the built-in default |
| `FrameGpuCounter` | the graphics counter path | the performance counter the graphics load is read from. Windows localises these names, which is the only reason this is a setting |
| `Present` | `0` | 1 names which output path is live, 2 also times the flip and says whether it blocks in the driver or spins |
| `Projectiles` | `0` | 1 counts the engine's own ballistic-physics list every 30 frames, and past 10 live entries also names the first few by position, so a pileup reads as stacked or spread at a glance |
| `Characters` | `0` | 1 names the characters within `CharactersRadius` of the player every 60 frames, with position, state, AI mode and the height each gained or lost since the previous report; 2 reports every live character in the level and ignores the radius |
| `CharactersRadius` | `12` | world units around the player that `Characters=1` reports. Ignored at level 2, and a value below 1 falls back to the default |
| `CharacterWatch` | empty | a character name from `Characters` above. Places a hardware write breakpoint on that character's height and logs the address of every instruction that writes it. Needs `Characters` on |
| `CharacterWatchVelocity` | `0` | which field the watch is armed on: 0 the character's position height, 1 its velocity height. Position names what moved it, velocity names what decided it should move |
| `AudioCensusMilliseconds` | `0` | >0 lists the occupied channels every N ms (minimum 100) |
| `MaxLinesPerSecond` | `60` | 0 = unlimited |
| `AlsoToMainLog` | `0` | mirror into `engine_fixes.log` as well |

## The log

`engine_fixes_diag.log`, next to the game, deliberately **not** the install log. A diagnostic
stream of hundreds of lines would make the install record unreadable, and one wants to throw it
away between two attempts. What was *installed* still goes to the shared log.

## `Characters` answers "which one is that"

A field report names what a character looked like, not what the engine calls it. This walks the
engine's own character pool and reports the ones near the player, so a report can name the record
it means rather than describing the costume.

**The name on its own does not identify a character.** It is copied out of the placement, and a
placement name is a reused archetype label; two earlier attempts in this project to pin down a
specific placement by name matching were both wrong for exactly that reason. It is the name
together with the position that picks one out.

The vertical column is the second reason it exists. Each line carries `since`, the height the
character gained or lost since the previous report. A character sinking through the surface it is
standing on reads as a run of negative values, and their shape says which kind of fault it is:
irregular values with pauses in between mean something is displacing it, a smooth and growing run
means it is falling.

`step` sits beside it and is a different measurement: the engine's own current position against its
own previous one. **Do not read `step` as whether a character is sinking.** It was the first thing
this observer reported and it is always zero, because both values are read at frame end once the
simulation has already copied one into the other. A whole measured session had `step` at zero on
every line while a character descended nearly a unit through the floor during it. It is kept only
because it separates a character the simulation is moving from one being written to from outside,
and `since` is the column that answers the question.

The pool's slot array is walked directly rather than through the engine's own iterator. That
iterator keeps its cursor inside the list header and advances it on every call, so an observer
using it would move a cursor the engine is in the middle of using. A census that perturbs what it
measures is not a census.

Each line also carries a silent correctness check. The body a character points at holds a pointer
back to the character, and the two are compared; a line that ends `owner mismatch, offsets suspect`
means the layout this observer assumes no longer holds, and every number on it should be distrusted
rather than reported.

## `CharacterWatch` names the instruction, rather than inferring it

Reading the disassembly outward from a field says which functions *could* write it. It does not say
which one does, and this project has twice spent a long time on a mechanism that turned out not to
be the one running.

This sets a hardware data breakpoint on four bytes and reports the address of every instruction
that writes them. The processor stops on the write itself, so the address in the exception context
is the instruction, with no inference in between.

Some detail that matters if you are reading a report:

The debug registers are per thread, and they are written from a short lived helper thread that
suspends the simulation thread first. A thread cannot reliably set its own, and the failure is
silent rather than loud, which would read as a field nothing writes.

The handler does no file work. It records into a fixed buffer and returns, and the frame callback
writes those records out afterwards. Logging from inside the handler would put file work between
the faulting instruction and the next one, changing the timing of the thing being measured.

A watch is on an address, not on an object. It is disarmed when the level changes, because the
memory behind that address then belongs to something else entirely.

**A debugger attached to the game owns these registers.** The arm will appear to succeed and then
be silently overwritten, so a report gathered under a debugger cannot be trusted.

## `Fx=2` and `Fx=3` answer two different questions

**A decal that is stamped is not a decal that is drawn**, and the gap between the two is where this
game hides its missing shadows and scorch marks. Level 2 hooks `bapvrt_addDecal` and reports the
pool: is a record created, and if not, why. Level 3 hooks `bapvrt_drawPolyDecals` and reports what
happens to that record afterwards.

That distinction is not academic. A measured session reported **1319 of 1319 records `stamped`**
while nothing whatsoever was visible on screen, level 2 alone would have said the decal system was
healthy.

The drawer has exactly two silent exits, and neither retires the record or logs anything:

| line | meaning |
|---|---|
| no `decal DRAW` line at all | the drawer never runs for these polygons |
| `recs=0` | it runs, but finds no record for that polygon |
| `NO PAGE=n` | the material exists and its texture page (`mat+0xB0`) is NULL, never paged in |
| `decal REFUSED by rdMaterial_selectCel` | the page exists and the material layer rejects it |
| `page ok=n` with nothing on screen | both gates passed; the loss is in the submit or the depth test |

`tLastDraw` looks like it should distinguish drawn from skipped and **does not**: `bapvrt_addDecal`
stamps it at creation with the same frame clock the drawer uses, and the blob shadow is re-stamped
every frame, so an undrawn record can carry a current timestamp. Only the page pointer is read.

Level 3 is loud, the drawer runs once per decorated polygon per frame. Keep `MaxLinesPerSecond`
on, stand still, and read the collapsed counts rather than the individual lines.

## Flood protection

The simulation runs at 32 substeps/s and the renderer at up to 240 frames/s. Events that fire per
frame are debounced at the call site, only the **change** is logged. Above that sit two generic
brakes:

* **Identical-line collapse.** The same line twice in a row is counted rather than printed, and
  handed in at the next change as `(previous line xN)`. That loses no information about *what*
  happened, only about how often, and the count comes with it.
* **A token bucket.** Anything above `MaxLinesPerSecond` is counted and reported as
  `N lines suppressed`, so one never falls for a silent hole.

## Names instead of numbers

86 music states and 82 music sequences (from IMUSE.DLL's muscript tables), 60 FSM opcodes (the
editor's own names), the 14 player modes, the 16 enemy reaction states, the mover types and phases,
and the `SNDF_*` bits. The **number is always printed alongside**, so a missing name can never be
mistaken for a different value.

## The one hook that is not on a function entry

`Fsm=2` places a detour **into the middle of `ai_run`**. The opcode dispatcher was inlined by MSVC
and has no symbol and no frame of its own, so there is no other way to observe which opcode is
running. That is tied to three conditions:

1. **The pattern is the proof.** The two stolen instructions encode the `ebp` offsets the hook
   reads. If it matches, the offsets are proven; if it does not, the hook is off. Nothing is
   assumed.
2. 10 bytes, an exact instruction boundary, and a linear disassembly of the whole `.text` (three
   phase offsets) finds no branch targeting into them.
3. The hook is `naked` and saves the GP registers, the flags **and the complete x87 state**
   (`fnsave`/`frstor`, 108 bytes), the engine is mid-frame there and the logger uses the CRT.

At about 36 actors and 10 opcodes per tick it costs roughly 11,000 calls per second. The formatting
is the expensive part, not the detour.

## Known limitations

* `Fsm=2` and `Fx=2` are expensive by construction; both are level-2 for that reason.
* `deactivate` on a sound zone stops no running voice; it only clears a flag. The log says so at
  the point where it matters, because that asymmetry is what a "the sound will not stop" report
  hides behind.
* Both music setters latch **before** the DLL call, so a cue the DLL rejects leaves the latch out of
  step with what is audible. The hook therefore logs the latch before *and* after.

## Testing status

Built and linked, `/W4 /WX` clean. Offline verification passes for every observer pattern on all
both retail builds. **Not accepted in game.**
