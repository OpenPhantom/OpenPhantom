# framerate_fix

**Produces:** `framerate_fix.dll` -> `mods\`

A free render rate that does not change how the game plays. **At 30 fps every correction here is
the identity**, so the original behaviour is a fixed point.

## Supported executables

Retail `WMAIN.EXE` (EN/DE) and the Fix Pack build. On `obi.exe` most patterns do not resolve and
each affected patch disables itself with a log line. The anchor blend is the exception: its site
survives that recompile, so it does not share a gate with the rest of the camera work.

## Configuration: `[framerate_fix]`

| Key | Default | Meaning |
|---|---|---|
| `Enabled` | `1` | |
| `MatchDisplayRefresh` | `1` | the cap follows the display's reported refresh rate and `TargetFps` is ignored. A cap below the refresh rate makes the screen repeat frames on an irregular pattern, which judders however correct the interpolation is; `frame_cap.h` carries the measurements. On by default, including when the key is absent, so an installation carrying an older `engine_fixes.ini` still gets it. When the display will not report a rate the configured number stands and the log says so |
| `TargetFps` | `0` | 0 = uncapped (clears the limiter); otherwise 1-1000. This removes the ENGINE's limiter and no other: if the frame rate still sits exactly on the display's refresh, that cap is in the graphics wrapper |
| `ProcessPriority` | `0` | 0 leaves it alone, 1 above normal, 2 high. The game is single threaded and saturates one core, so a busy background process competes with it directly while the task manager shows a low total. Not shown to repair anything; a precaution |
| `CompensateCamera` | `1` | rescale the per-frame dampers `k^(dt*30)` |
| `CompensateCameraAnchor` | `1` | replace the anchor's per-frame mean with a rate-correct blend. The only patch here that rewrites *instructions* rather than an operand, so it has its own switch |
| `CompensateCameraInCutscenes` | `0` | at `0` a scripted camera gets an anchor weight of zero, so a placed shot holds its gather origin instead of easing toward it. The five lag cells stay compensated either way, because they damp the rig and the euler rather than the origin. `1` compensates the anchor during a scripted camera as well |
| `CompensateAnimation` | `1` | the animation clock and the emitter dormancy counter |
| `AnimationClockMode` | `1` | 1 = the authored 30 Hz rate |
| `SpinSleep` | `0` | `Sleep(0)` -> `Sleep(1)` in the frame wait |
| `PinSimulationRate` | `1` | nail the substep to 1/32 s |
| `InterpolatePitchRoll` | `1` | interpolate the drawn pitch and roll like the drawn yaw |
| `PosePerFrame` | `1` | rebuild the joint matrices every frame |
| `FaceLatchYield` | `16` | hand the scripted facing command a "still turning" answer on one simulation step in N, for a clip that has already clamped at its last frame. 0 switches it off, range 2-64 |
| `PreciseFrameTime` | `1` | compute the frame delta in double instead of through the engine's float accumulator |
| `RebaseSimClock` | `1` | take the same amount off both simulation clocks so their difference, which is the interpolation weight, keeps its precision on a long level |
| `InterpolateParticles` | `1` | draw particles between simulation steps rather than on them |
| `InterpolateMovers` | `1` | the same for movers: doors, lifts and platforms |
| `MoverSubstepClock` | `1` | removes the substep loop's clamp of the world clock, so a mover integrates exactly one simulation step per substep and its sample pair lands on the same lattice as the alpha that blends it. On by default on the strength of the measurement below: without it a mover's drawn step disagrees with its neighbours on about one frame in three, and with it on about one in eighty, so `InterpolateMovers` barely works without this. It changes how movers move rather than only how they are drawn, which is a phase shift of under one substep for everything else on that clock. Engine location `bapmap_setWorldClock`, shared with `RebaseSimClock` |
| `MoverTravelLimitPerStep` | `64.0` | world units a mover may cross in one simulation step before the blend refuses it and snaps instead. Guards against a teleport being smeared into a slide |
| `InterpolateRiders` | `1` | keep each drawn object's previous position here rather than reading the engine's, which a platform's carry flattens. `2` and `3` are measurements rather than settings; see **A rider had nothing to be drawn between** |
| `RiderTravelLimitPerStep` | `2.0` | the furthest a CHARACTER may travel in one simulation step before the blend refuses it and draws it where it landed. Not the mover's number: 64 here is what made the first attempt unusable |
| `StatsFrameInterval` | `0` | >0: log a frame-time/substep summary every N frames |
| `StatsPlayerFrames` | `0` | >0: dump the player's draw interpolation for N frames |

## Produced frames against shown frames

Worth its own heading, because it caused a long hunt in this DLL for a fault that was never here.

`TargetFps` decides how fast frames are produced. Nothing in the shipped stack decides how fast
they are shown. The game presents through `IDirectDrawSurface4_Flip`, and a flip on a flipping
chain in exclusive fullscreen is scheduled for the next vertical retrace unless `DDFLIP_NOVSYNC`
is passed, which the game does not pass. The wrapper that now translates that flip builds its
device with `PresentationInterval = 0x80000000`, IMMEDIATE, so the pacing the flip used to
guarantee is gone. Neither of the wrapper's two vsync settings changes it on the DirectDraw
translation path, and its own DirectDraw section has no vsync key at all.

The engine's own `stdDisplay_waitVBlank` at `0x0048F1F3` would have paced it, and it is dead code:
a whole-image search finds no reference to it.

So a produced rate below the refresh rate means the display repeats frames on an irregular
pattern. Measured on a 144 Hz screen, a cap of 100 leaves 44 refreshes a second repeating a frame
and a cap of 144 leaves none; on a 90 Hz Steam Deck OLED a cap of 60 leaves 30. Uncapped is smooth
as well, measured at 256 to 310 frames a second, but by brute force rather than by pacing, and it
loads one core fully. `MatchDisplayRefresh` picks the one option that is both smooth and cheap.

The instrument caveat that goes with this: the drawn evenness figures below are only meaningful at
a steady frame rate. A drawn object correctly moves further on a longer frame, so uncapped, where
the frame time swings by a factor of three, the same measurement reads 35 to 40 per cent uneven
with nothing wrong at all.

## Engine locations

| Site | Retail VA | What |
|---|---|---|
| `sys_waitForFrame` | `0x475B75` | both cap immediates, the limiter flag, the Sleep push. The 1/30 immediate sits inside the matched pattern; the 1/60 one does not, so the instruction holding it is validated before either is written and the cap declines as a whole if it disagrees |
| `sys_runSubsteps` selector | `0x475737` | the 1/64 arm is overwritten with 1/32 |
| `render_frameEnd` | `0x46C139` | detoured for the per-frame tick |
| `g_clockTicks++` | `0x46C1B5` | operand read; the counter is driven from elapsed time |
| `emitter_renderAll` | `0x42238D` | the 30-frame imm8 |
| `bapview_updateCam` tail | `0x418FDD` | four lag immediates, rewritten at run time |
| `bapview_lerpPitch` | `0x41868A` | the pushed 0.96 |
| `bapview_followYaw` | `0x418F6D` | two operands repointed at `k` and `1-k` |
| the anchor mean | `0x418623` | 63 bytes replaced by three 18-byte blends against a live weight |
| the yaw deadband | `0x418715` | operand repointed at a scaled cell |
| `bapobj_drawAll` position blend | `0x41125B` | 126 bytes replaced by a call, the largest patch here. The pattern is the whole region rather than a prefix, so every byte overwritten is checked before anything is written, and it carries no absolute address at all: every operand is relative to the frame pointer or to the object |
| `bapobj_drawAll` euler | `0x4112D9` | 0x20 bytes replaced by a call. Immediately after the position blend above, in the same function, and the two regions do not overlap, so neither install order matters |
| `rdThing_Draw` pose gate | `0x410019` | `74 19` -> `90 90` |
| facing completion test | `0x42E3AD` | detoured; one caller, the 0x202 handler |
| `bapmap_setWorldClock` | `0x41F0C9` | detoured, and shared. `RebaseSimClock` adds the offset back so the world keeps its absolute time, and `MoverSubstepClock` removes the substep loop's clamp of that clock to the frame target. The second changes a value the simulation reads |
| `bapmap_tickMover` | `0x409170` | detoured, to capture the pose before the engine integrates it. Nine callers, and the draw itself is one of them: `bapvrt_transformWorld` ticks a mover at `0x419B2C` immediately before transforming it, so every drawn mover is already at the current world clock |
| the four mover pose consumers | `0x419B6A` and after | call displacements repointed, three into `bapmap_matMul3` and one into `mat34_invertRigid`. The `E8` is kept and only its displacement rewritten |
| substep counter increment | `0x4757DB` | operand read, address-free pattern. Nothing is written here; `substep_counter.c` owns the one resolution of it and both the facing latch and the rider blend read the cell it names. The cell has a second incrementer at `0x45DC47`, `swmenu_render`, which advances it once per frame while a Swift menu is on screen, so it counts substeps exactly during play and not otherwise |

## Why each correction exists

* **The simulation is already free.** `sys_runSubsteps` is a fixed-step accumulator at 1/32 s. AI,
  physics, projectiles and collision do not care about the render rate.
* **The camera does not know that.** `bapview_updateCam` answers a per-frame message and ignores
  the dt it is handed; five exponential dampers run 4.8 times as often at 144 fps.
* **The animation clock is a per-frame counter.** Water waves and scrolling UVs read it as if it
  were a clock.
* **The facing command's completion flag is a level, not an edge.** `bComplete` at
  `track+0x140` is raised inside the draw once per rendered frame and read once per
  simulation step. Below 32 fps some frames run two steps back to back and the second
  still sees it clear, which is the only reason the shipped game ever took the
  in-progress branch; the rate of that is 32 minus fps per second. At or above 32 fps
  it never happens, and a scripted state whose only work sits on that branch stops for
  good. In the swamp the opening scene runs its script on the player's own object, so
  the player is suspended and never released.
* **The pose throttle** compares against the SUBSTEP counter. At 30 fps there are 1.07 substeps per
  frame and the branch never fires, the shipped game never executes it. At 60 fps about 47 % of
  frames redraw the previous frame's joint matrices, freezing pose *and* placement together.

## Known limitations

* **Two sites rewrite instructions rather than an operand, and both fail closed.** The other is
  the position blend in `bapobj_drawAll`, 126 bytes at `0x41125B`, larger than this one. It
  declines as a whole when the pattern does not match, and `patch_write_bytes` reads the region
  back and rolls it back if the write did not take. Both also mean this DLL must never be
  unloaded: the anchor for the reason below, and the position blend because the call it writes
  names a function inside this module.
* **The anchor is the second, and it fails closed.** A mean
  cannot be rate-corrected by any single factor, substituting `k` gives weights summing to `2k`,
  and at a high frame rate the anchor roughly doubles every frame until the eye leaves the world.
  That shipped once. So the arithmetic is replaced instead: `(anchor - target)*k + target`, whose
  weights are `k` and `1-k` by construction. All 63 bytes are written in one call, only on an exact
  match of all 63, and a partial match declines, a short write would land mid-x87-sequence and
  corrupt the camera *silently*.
  Two consequences: this DLL must never be unloaded (the `fmul` operand names a cell
  inside it), and the 63-byte write is safe because the mods load from the host entry point with
  one thread in the process.
* **The step counter is only a step counter while the game is being played.** The simulation
  advances it once per substep, and `swmenu_render` advances it again once per frame for as long as
  a menu is drawn: the statistics window has measured 320 advances over 600 frames with the
  simulation not moving at all. Nothing has to defend against that. A stamp that moves while a
  position does not brings the remembered previous position up to the current one, which draws the
  object where it is, as the game did before any of this existed. The consequence is
  bounded to a menu drawn over a live world, where a rider is drawn unsmoothed rather than wrongly.
* **An object carrying `BAPOBJ_POSE_MATRIX`, flag bit 2, never reaches the replaced region.**
  `bapobj_drawAll` tests that flag at `0x41121A` and, when it is set, takes the pose-override
  branch and jumps from `0x411256` straight to the shared merge point at `0x411394`, past the
  position blend, the yaw blend and the matrix build. Such an object is drawn from its pose matrix
  with no interpolation of any kind, by the engine's own design, and the rider blend neither
  changes nor can change that. No writer of that matrix during ordinary play has been identified,
  so whether anything in a level actually uses it is not established.
* **Below 32 frames a second nothing here can be genuinely smooth.** That is a property of the
  game rather than of this DLL. The simulation steps faster than the display can show the result,
  so several steps land between two frames and no amount of blending invents a frame that was
  never drawn. What the rider blend owes at those rates is to do no harm, which is checked: the
  drawn moment still trails the simulation by exactly one step and still advances evenly.
* The emitter dormancy compare is a sign-extended `imm8`, so the largest writable value is 127:
  exact up to 127 fps, saturating above it.
* `AnimationClockMode` cannot express a fractional clock, the consumer truncates to `uint32`
  before converting to float.
* **A guard on a hot path has to be the structured-exception form, not the asking form.** The
  mover tick hook validates the record before reading it, and that check was
  `memory_is_readable_range`, which walks the region list through `VirtualQuery`, a system call.
  `bapmap_tickMover` looked like draw-path frequency and is not: a census measured it at 3,400
  calls per frame while settled debris was being created near a lift, every one of them arriving
  through this detour. The guard alone was driving thousands of kernel transitions a frame. See
  the testing status below for the measurement and `mover_interpolation.c` for the site.

## Fallback behaviour

If the per-frame hook cannot be installed, the camera compensation and the animation clock do not
run and the log says so explicitly. The render cap, the pinned simulation rate, the emitter
dormancy and both draw patches are already in place by then and stay in place.

## Testing status

Built and linked, `/W4 /WX` clean. Offline verification of every pattern passes on the executables
checked: EN, DE, the Fix Pack, and both install copies. One unit test, `camera_anchor`, on the
anchor encoder: it is the only isolated pure logic in this DLL, and the only place where a wrong
byte produces no crash and no log line.

`FaceLatchYield` was tested in the game. At an uncapped rate of about 90 fps the swamp opening
released the player after 8.9 s, against 9.13 s in a working 30 fps run, so the scene plays at
its authored pace rather than merely failing to hang. The confirming step, setting the key to 0
and checking that the freeze returns, has not been run yet.

## The plausibility bound

`frame_delta.c` refuses a measurement above `MAX_PLAUSIBLE_SECONDS`, which is ten, and leaves the
engine its own value when it does. The comment there used to say that this catches a level load.
It does not: a load takes a fraction of ten seconds, so the measured value is written. Whether the
engine is better off with that value or with its own has not been measured, so the bound stays at
what shipped.

`InterpolateMovers`, `InterpolateParticles`, `PreciseFrameTime` and `RebaseSimClock` were played
and accepted by the maintainer, so they now default to on. That is a judgement about how
they feel; the numbers in their own files are still a byte census and arithmetic rather than a
measurement of a session, and `sim_clock` and `mover_blend` have unit tests covering the
arithmetic alone.

**`InterpolateMovers` was field-reported as a severe frame-rate stall, and the cause was the guard
rather than the interpolation.** A stall at two lift platforms had been attributed to the engine for
some time and was being compensated for in a separate DLL. Bisecting the installed mods against a
pure retail install narrowed it here, and switching this one key off removed it: at the same
encounter, the same 57 debris entries and the same 60 fps cap, the burst that creates the debris ran
at 8.5 fps with movers on and 60.0 fps with them off.

The mechanism was then measured rather than guessed, with the `Trigger=6` call-site censuses in
`diagnostics`. Polygon transforms were not the cost: `bapmap_polyToWorld` ran at 3,679 calls per
frame with movers on against 3,090 with them off, which is nowhere near a tenfold difference in
frame time. The mover census carried the answer instead, in its unattributed column: with movers on,
all 679,974 `bapmap_tickMover` calls in a 200-frame window came from a return address it could not
recognise, because that address was this DLL's own trampoline. With movers off, none did. The hook
was in front of a function running 3,400 times a frame, and its `VirtualQuery` guard was the whole
cost.

The repair is three calls changed from `memory_is_readable_range` to `memory_try_readable`, the
structured-exception form `common/memory.c` documents for exactly this case. The spans validated are
unchanged. Confirmed in game afterwards at both lifts: a flat 60.0 fps through the burst with
`InterpolateMovers=1`, and the census showing 2,223 `tickMover` calls per frame still arriving
through the detour with 6,960 poses blended and none refused, so the interpolation is doing its full
job rather than having quietly stopped. That last check is the point: the call count did not fall,
only the cost per call.

The rest of this DLL is accepted in game too, in the v0.4.1 build, which was played through by
hand.

## A rider had nothing to be drawn between

A character riding a platform judders against the floor it is standing on. Smoothing the platform
did not cause it and switching that smoothing off did not cure it, which is where the search
started rather than where it ended.

**The engine was never the problem.** `bapobj_drawAll` blends every drawn object between its
previous and its current position on the substep alpha, and has since 1999. The pair it reads is
what breaks.

**The measurement.** The same field, the same object, the same instrument, twice:

| | `prev` differs from `cur` | `prev` equals `cur` |
|---|---|---|
| walking on ordinary ground | 2111 frames | 0 |
| carried by a platform | 0 | 600 frames |

While carried, the previous position is identical to the current one on every frame, so the blend
has nothing to work with and the rider steps 32 times a second against a platform drawn every
frame. Walking, the same pair is maintained correctly and the same blend works.

**Why the pair goes flat.** A hardware write watch named both writers, and they turn out to be the
same six lines twice over. `Plr_CommitPose` captures `prev = cur` and then moves `cur` to where the
simulation put it, which is correct. The carry runs a second commit in the same step, and because
the first has already run, that one captures a previous position which is already the current one.
The second write puts back the value that is already there. Finding it took a watch that names
same-value writers.

**What is done about it.** The previous position is remembered in `object_track.c`, keyed by the
object, and the region of `bapobj_drawAll` that computes the blend is replaced by a call that fills
the same six stack locals from it. Which simulation step a frame belongs to is read from the
engine's own count of them, which `substep_counter.c` resolves and both this and the facing latch
ask; two earlier ways of deciding it are in the history below, and both were wrong.

**The blend weight is not the alpha, once a frame can span more than one step.** The engine's blend
draws an object exactly one step behind the simulation, and that lag keeps it between two
positions it really held, and every frame inside a step raises the alpha by its own share of that
step, so the drawn position advances by one frame of travel whatever the rate. That holds while a
step is shared by two frames or more. Below 32 frames a second the two samples either side of a
frame are several steps apart, and using the alpha alone would cover all of that travel in one
step's worth of it: the object runs ahead and drops back, once every frame. Asking instead for the
same drawn moment, one step behind a simulation standing at `stamp + alpha`, gives
`(alpha + gap - 1) / gap`, which is the alpha exactly at a gap of one. The travel limit is scaled
by the same gap, because a legitimate two-step move is twice as far and would otherwise be read as
a teleport.

Nothing the simulation reads is written. `obj->pos` and `obj->prevPos` are read and never stored
to, and the only writes are into the caller's own frame, which is the convention
`draw_interpolation.c` already follows for the drawn pitch and roll. An object the table has no
answer for falls back to the engine's own previous position, which reproduces the original
arithmetic exactly, so the worst case is the behaviour that shipped.

`RiderTravelLimitPerStep` is the same guard `MoverTravelLimitPerStep` gives a platform: past it the
object is drawn where it landed rather than being smeared across the gap, because a jump that large
is a teleport and not motion.

### Testing status

Built, and unit tested at 39 checks covering the sequence properties a game run cannot show: the
same answer for every frame inside a step, moving on exactly once when the step does, how many
steps separate the two samples and the weight that follows from it, two objects not reading each
other's history, a full table refusing rather than guessing, a stale slot being reclaimed, and the
travel limit on the whole vector rather than one axis.

A second test, `rate_independence`, drives the same arithmetic over a run of frames at 20, 30, 32,
51.7, 60, 64, 100, 144 and 240 frames a second, and once more with the frame times jittered the way
a real vsync delivers them. What it pins down is the pair of claims that together are what smooth
means: the drawn moment trails the simulation by exactly one step, and consecutive frames advance
it by equal amounts. The worst error at any of those rates is under a millionth of a unit, which is
float rounding. Driven from the alpha alone instead of the weight, the same test fails at 20 and 30
with an error of 0.045 units, exactly one step of a carried character's travel, and passes
unchanged at every rate at or above 32; so the weight changes nothing at the rates people play at
and repairs the ones below.

Played and confirmed: the judder on a platform is gone for the player and for NPCs alike, and
standing on ordinary ground is steady.

Played again after the step count moved to the engine's counter and the weight took the gap into
account. Characters are smooth, and smooth while the platform under them is moving, at a measured
60.0 frames a second: the log's windows put the frame time between 16.66 and 16.79 ms throughout.
That is the rate the change was expected to leave alone, and it did.

### The blade drawn out of the hand

With this feature on, a lightsaber blade was drawn standing out of the player's hand, pale, and at
times many times its own length. It was found on Coruscant and reproduced on demand, and it took
twenty runs to corner, so the shape of the search is worth recording alongside the answer.

The bisect by setting landed on `InterpolateRiders` within four runs, cleanly and reproducibly:
off, no beam; on, beam. What followed was slower, because every obvious mechanism was cleared by
direct test. The values the patch writes were innocent: mode 2 draws the engine's own arithmetic
through the same replacement and had the beam. The registers were innocent: the disassembly of the
rest of `bapobj_drawAll` shows `edx` reloaded and `eax` and `ecx` written before use. The stack was
innocent: 512 bytes scribbled below the stack pointer changed nothing. The x87 stack was empty at
every one of forty thousand entries, counted from the tag word. The control word and MXCSR never
moved across the call. A byte-for-byte transcription of the replaced region behind the same call
drew no beam; the C behind the same call did.

The difference that mattered was the CRT's float classifier. The hook and `object_track.c` tested
finiteness with `isfinite`, which on this compiler is a copy of the value through the x87 unit and
a call into the CRT, and with those replaced by an integer test on the bit pattern the beam is
gone, with everything else unchanged. That is now the rule for this path: no CRT floating-point
routine and no x87 instruction beyond the one that receives `object_track_weight`'s return, and
the disassembly of the built object is what confirms it.

The instruction-level cause is **not established**. Reproducing the same copies and the same
classifier calls in assembly, in front of the transcription, did not show the beam, and the
honest reading of that is a false negative: this artefact needs a trigger, and a single clean run
is weaker evidence than it looks, which the search paid for more than once. Three mechanisms were
proposed with confidence along the way and each was refuted by a measurement built for it. What
is claimed here is what was measured; what is written in the code is the rule that follows.

Played at 25 as well, which is below the simulation rate and is what the weight exists for. Nothing
misbehaves there: no smearing, no snapping and nothing thrown across the level. It also does not
look good, and cannot, because the simulation steps faster than the display can show the result and
no amount of blending invents a frame that was never drawn. Passing at that rate means the feature
does no harm where it can do no good, which is all it owes. 32 exactly has not been played; it is
the rate at which the earlier alpha-derived step count could stall altogether, and it is now read
from a counter instead of inferred.

One thing was added afterwards on a rules audit rather than from play. The travel limit is a
distance comparison, and every comparison against a value that is not a number is false, so a
non-finite previous position would have passed the guard meant to refuse a bad one and been
drawn. The finiteness test now runs before the limit, and five checks cover it.

**It took two failed play sessions to get there, and both are worth recording.**

The first sent characters flying around the level. Two explanations stood: the patch was in the
wrong place, or the previous position it fed the blend was wrong. `InterpolateRiders=2` separated
them in one run by placing the patch and then doing the engine's own arithmetic. The game was
normal, so the region, the calling convention, the six locals and the alpha were all right and only
the remembered position was wrong.

`InterpolateRiders=3` then drew with the engine's values while reporting the worst disagreement
every 200 frames. An earlier version reported the first forty instead, which spent the whole budget
on two objects in the opening seconds and said only that the calm case is calm. The worst case
named it at once:

    mine(44.223 71.638 92.928)  engine(77.201 85.501 92.500)  cur(77.201 85.501 92.500)  apart 35.78
    mine(50.501 71.851 92.000)  engine(83.801 101.501 92.000) cur(83.801 101.501 92.000) apart 44.59

`engine == cur` exactly is the engine saying **do not interpolate this object**, which is how it
marks a spawn, a teleport or a pooled slot being reused. The travel limit should have refused those
and did not, because it was 64, copied from `MoverTravelLimitPerStep`, which describes a lift. A
character carried by a platform moves 0.045 units in a step and a walking one about 0.017, so 35
and 44 sailed underneath and were drawn smeared across the level. The limit is 2.0 now. The
flatness of the pair cannot tell a rider from a teleport; the distance can.

The second attempt cured the platform and planted the same judder on solid ground. The step
boundary was being inferred from the position changing, which needs no clock and works for anything
moving; a character standing still never changes position, so its previous was never brought
forward and the blend swung it between where it last walked and where it stood, once per step, for
as long as it stood there. The previous position is brought forward whether or not the object
moved now.

The boundary was then taken from the substep alpha dropping, which is wrong in both directions and
neither shows up at 60. A frame spanning two steps advances the count once instead of twice, and at
exactly 32 frames a second the alpha barely moves at all, so the drop that marks the boundary may
never arrive and the record freezes. It is read from the engine's own counter now.

All three faults have a check that fails without the fix.

**The travel limit was suspected of causing the camera jitter, and it is not.** The camera is fed
the player record's own position by `bapview_setCamTarget`, once per substep, and the engine
interpolates that pair on the same alpha, so it aims at the player's simulation position while the
body is drawn from the remembered pair. A refused blend draws the body at its current position, a
whole step ahead of where the camera points, and that would move against the frame only while the
camera follows. Since `RiderTravelLimitPerStep` is 2.0 units, chosen against a carried player
moving 0.045, and a platform is allowed 64 in the same step, a rider on a fast platform could have
been refused on every step.

Counted, over six windows of 200 frames: three refused nothing, and the others refused 2, 10 and 2
blends out of roughly 4000, with worst steps of 26.89, 137.30 and 37.12 units. Those are spawns and
pool reuses, the case the guard exists for. It never refuses a ride, and raising it would let a real
teleport smear. The count stays in the log as a warning, because a refusal is invisible on screen
except as the body drifting against a following camera.

**Still open, and separate from this.** The platform itself is a touch jittery, which is the
mover's own interpolation rather than the rider's. The instrument line reports 35 to 84 refused
poses per 600 frames against 10,000 to 15,000 blended, so about one pose in 250 is drawn unblended.
Whether that accounts for what the eye sees is not established, and the refusal count does not yet
say which of the guards refused.

### A mover is not on the clock the rest of the draw is on

The platform was still a touch jittery after it stopped stepping, and the reason is not the blend
but the moment being asked for.

`bapmap_tickMovers`, the plural, is reached from one place in the retail image, the frame
broadcast, so every door, lift and platform is swept once per rendered frame. The singular
`bapmap_tickMover` is a different matter and has nine callers; the correction below has the two
that affect this. It integrates against
the world clock, and that clock is only written inside the substep loop, which sets it to the end
of each substep clamped to the frame's own target time. So the last substep of a frame leaves the
world clock standing exactly at the moment that frame is meant to represent, and two things follow:

* a frame that ran at least one substep leaves the mover exactly where the frame wants it, so there
  is nothing to interpolate and the alpha should not be applied at all;
* a frame that ran no substep, which above 32 frames a second is most of them, leaves the clock
  untouched, `bapmap_tickMover` short-circuits on its own time base, and the mover is drawn where
  it was when the last substep ran.

A mover's newest pose is therefore never in the future and usually in the past. The substep alpha
measures the simulation clock and describes neither end of the mover's own move, so applying it
drew the mover at a moment unrelated to both: against a 60 fps lattice it lands between 24 and
27 ms behind, and it is the variation rather than the size that the eye reads as jitter.

**What is known exactly, and without reading an absolute clock.** The simulation time only ever
moves in whole substeps and the alpha is the phase of the frame's target between the last two of
them, so the render time since any earlier frame is the substep period times the change in the
alpha. The mover's own time base gives how much world time its last move covered. Neither
quantity decays as a level runs, which matters because the level clock is a float32 whose
resolution does.

The phase is then that elapsed time over that interval, and the two useful answers differ by
exactly one: the phase alone holds the mover one whole move behind, and one plus the phase asks
for the frame's own time.

### Both of those looked worse in the game, and neither had run

`mover_blend_world` guarded its weight with the open interval `(0, 1)`. That is right for a weight
which is the substep alpha and wrong for any other, and it is why the two modes above were both
reported as worse than the arithmetic they replaced. Mode 1 produces a weight in `[0, 1]` and mode
2 one in `[1, 2]`, so the guard refused every pose mode 2 ever computed and every pose mode 1
computed on an integrating frame. The caller draws the newest pose on a refusal. So mode 2 was
exactly the stepping it existed to remove, and mode 1 alternated a refusal with a blend, which is
a forward snap and a drop back on every tick. Twice as jittery, and worse again, were accurate
descriptions of the picture and said nothing at all about the arithmetic, which had not executed
once.

Two things are worth keeping from that. A guard which silently substitutes a fallback cannot be
diagnosed from the screen, because a refused feature and a wrong feature look identical. And the
instrument built to settle the question could not have settled it: it counted every refusal in one
bucket without recording which guard fired, so it would have reported a healthy-looking
measurement beside a mover that was never blended.

The range is now explicit at `MOVER_BLEND_WEIGHT_MAX`. Zero is a real weight, meaning draw the
earlier sample, and up to one whole interval past the newer sample is allowed, as an
extrapolating mode needs. The one case that stays a refusal is a weight of exactly 1.0: the caller
then draws the newer pose byte for byte, and the alpha reaches exactly one on every frame at 32
frames a second, so that is what keeps the feature the identity at the rate the game was authored
for. Running it through the lerp would be right to within rounding and would quietly stop being
byte-identical, because the arithmetic rounds twice and the rotation rows are renormalised.

### What the lattice mismatch actually costs

Worth writing down, because it is the size of the defect mode 0 still has. A mover integrates from
one frame time to the next, so its sample pair spans an interval between substep frames, which at
60 fps is 33.3 ms seven times in eight and 16.7 ms once, from 8 substeps every 15 frames. The
blend then applies the substep alpha, a phase within a 31.25 ms step. The drawn advance per frame
is therefore 1.07 times true speed on the two-frame intervals and 0.53 times on the one-frame
ones: a half-speed step lasting one substep, roughly every 250 ms at 60 fps. Periodic, which is
what the eye reads as a touch of jitter. At the authored 30 fps the same mismatch is 33.3 against
31.25, seven per cent, so it never showed.

### The rider and the camera are the same defect seen twice

`bapmap_carryRider` places a carried character from the mover's pose at that substep's world
clock, so the rider advances on the same frame lattice the platform does. Both are then blended by
the same alpha, so they agree with each other and a character looks steady on the platform. The
error is not cancelled where there is nothing to agree with, which is the world scrolling past the
camera at alternating speed. `bapview_updateCam` builds its target as a proper shift register
pair, once per substep, and every per-frame factor in it is compensated, so camera jitter seen
while riding is this defect rather than a camera one.

### A correction to the census above

The claim that `bapmap_tickMovers` is reached from one place is about the plural, and the
conclusion drawn from it, that a mover integrates once per frame, does not follow. The singular
`bapmap_tickMover` has nine callers in the named tree, and two of them matter here:
`bapmap_carryRider` ticks the mover a character stands on, and the carried-mover arm ticks a
carrier before reading it. Both run on the substep path, which is where the thousands of calls per
frame the stall census measured were coming from. So a ridden mover is snapshotted mid-frame with
that substep's alpha, which is a real defect in the measurement modes 1 to 3 rest on and is
separate from the guard.

### Testing status

Built, and `mover_blend` covers the whole weight range including the extrapolating half the old
guard silently refused. The blend applies the substep alpha, as shipped, and the measurements above show that
to be correct once the render cap matches the display.

Played at 144 frames a second on a 144 Hz screen with the cap matched, where the platform is smooth
and a character stays put on it. The residual that was open through several sessions is closed and
was never in this arithmetic.

### Removing the cause instead: `MoverSubstepClock`

Three attempts to compute a better weight in the draw failed, and the fourth option is not another
weight. The pair and the alpha are on different lattices, and nothing applied afterwards repairs
that; so the clamp that puts them on different lattices is removed.

`bapmap_setWorldClock` is already detoured here, by the clock rebase. Handed the clamped value and
the one before it, the hook now answers with a whole substep past the previous one, which is the
value the loop would have set had it not clamped. Every mover then integrates exactly one step per
substep, its sample pair spans exactly one step, and the substep alpha the draw already applies is
the correct weight for the platform and for the character riding it at the same time, at any frame
rate. There is nothing to tune and no extrapolation.

The sequence is unit tested by walking the accumulator the way the substep loop walks it, at 60
frames a second against a 32 Hz simulation, and checking that every call advances the clock by
exactly one substep. That walk makes 320 advances over 600 frames, which is ten seconds at 32 Hz,
and an earlier version of the check asked for more than 550 on the assumption that a substep runs
every frame. It does not, and that same assumption is what made the three draw-side attempts fail,
so the count is pinned down deliberately.

**It changes a value the simulation reads, so defaulting it on needed a measurement rather than a
preference.** The world clock is read by the simulation,
so this changes how movers move rather than how they are drawn. Un-clamped it runs up to one
substep ahead of the frame's time, which is where the object simulation already sits. The average
rate is untouched, since the loop runs the same number of substeps and each now advances the clock
by exactly one step, so nothing speeds up or slows down; what shifts is phase, by under 31 ms, for
every other reader of that clock. A constant offset of up to one step also remains within a level,
because the first value after a level opens has no predecessor to be un-clamped against, and
correcting it would need the loop's own simulation time and would move nothing relative to
anything else.

**Measured against itself, at a frame rate steady enough to trust.** Two runs at a matched 144 Hz,
frame time inside 6.93 to 6.96 ms both times, five windows each, counting drawn frames whose step
disagrees with the mean of its neighbours by more than five per cent:

| | uneven frames | share |
|---|---|---|
| `MoverSubstepClock=0` | 226/618, 1034/3000, 1351/3658, 1443/3993, 431/1275 | about 35 per cent |
| `MoverSubstepClock=1` | 36/1547, 29/1487, 49/4170, 49/3955, 52/4236 | about 1.5 per cent |

A factor of about twenty five, and the second run was reported as fully smooth by eye. The stronger
conclusion is that without it mover interpolation is largely defeated: one frame in three
disagreeing with its neighbours is why movers still looked stepped with the feature switched on.
`InterpolateMovers` and this are one fix in two halves.

**Played, and it is much better.** A character stays put on a moving platform and the platform
itself went from clearly stepping to barely jittery. Nothing paced off the world clock was
reported as having moved.

The instrument agrees and is more precise than the eye. With this on, the sample interval is
`31.25 to 31.25 ms` in every clean window, exactly one simulation step, where before it swung
between one and two frames. The substep alpha then advances 0.5333 to 0.5352 per rendered frame
against an exact 32/60 of 0.5333, over all 600 frames of a window: 280 ordinary frames and the 320
where a step boundary falls and the sample pair rolls on. That is uniform to 0.4 per cent, and
zero frames in a window pass one per cent.

**A residual remains and it is not in this arithmetic.** A touch of jitter is still visible on a
platform when it is contrasted against the background. It is specific to movers: the world
scrolling past on ordinary ground is smooth, which rules out the frame cadence and the display.

What the measurement above establishes is that the ALPHA advances uniformly. It assumes rather
than proves that a given mover's drawn position does, because that also needs each mover's pair to
roll on the frame the alpha wraps.

An earlier version of this section claimed the pose age of `0 to 1 frames` was evidence that the
roll does not land on the same frame for every mover. It is not evidence of anything. The frame
stamp is advanced at frame end, after the draw, so a mover ticked this frame reads 0 and the same
mover on the following untick frame reads 1; at 60 fps about half the frames are each kind, so
`0 to 1` is exactly what every mover rolling correctly produces. The reading was consistent with
the hypothesis and equally consistent with its opposite.

The lead with evidence behind it is the one this file has flagged twice and never instrumented:
35 to 84 refused poses per 600 frames. A refusal draws the newest pose, which is a one-frame
forward jump for that mover, and the total looking small says nothing about the distribution. If
they cluster on one subnode of one platform, that platform hitches every few frames while the
count stays under one per cent. Counting refusals per guard and per subnode answers it directly
and is cheaper than tracking a translation frame by frame.

Two earlier attempts to measure this were narrower than they looked and are worth recording as
such. The first counted only the 280 frames where the alpha rises, skipping every frame where it
wraps on the grounds that those are bookkeeping; those are the frames where the pair rolls, which
is the most likely place for a jump, so it reported uniform motion having excluded the half that
could have been uneven. The second reported ranges rather than frequencies, and a range cannot
distinguish smooth motion with one hiccup from motion that is uneven throughout.

## The offset has to be dropped where the level opens

The rebase banks time: it takes a power of two off both simulation clocks, so their difference
survives bit for bit, and adds the same amount back to the world clock so absolute time is
preserved. That addition happens in the `bapmap_setWorldClock` hook, which the engine calls twice
per substep.

The engine zeroes both clocks itself when a level opens. The banked offset has to go with them, or
the new level's world clock starts at the previous level's duration. That was already handled, but
in the wrong place: the check ran once a frame at frame end, and by then the opening frame's
substeps had already been handed `time + offset`.

**What that cost.** The world clock began the new level two seconds in, while every mover had just
been primed with a `timeBase` near zero. A mover's step is `now` minus the time it last ticked, so
each one swallowed the whole banked offset on its first tick. Movers at rest absorbed it invisibly.
A platform in mid journey crossed its entire gap in a single frame and left the characters standing
where the save had put them.

Measured on a Coruscant quicksave that reproduced it reliably: 59 movers each about to step
2.031 s in one tick, and none at all once the offset was dropped in time.

The detection now runs inside the world clock hook, before the addition, which is where a level
actually opens. The frame end check is kept as well, for a level that opens without the substep
loop running at all. Nothing about the rebase itself changed, so during a level not one float
differs; only the first instant of a new one does, and there it starts from zero as this feature
always intended.

The boundary line in the log is the evidence either way. It used to report `0 s` of accumulated
offset, not because there was none but because the substeps had already spent it. It now reports
what was really banked, 4, 6, 18 and 28 seconds across a session.

### Testing status

Played, with the fix on and the feature on. The quicksave that failed every few loads now holds,
the diagnostic mover observer reports no oversized steps, and the stutter this feature exists to
prevent stayed away over several minutes of ordinary play, which is the part only an eye can judge.
