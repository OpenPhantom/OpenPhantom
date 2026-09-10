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
| `MoverTravelLimitPerStep` | `64.0` | world units a mover may cross in one simulation step before the blend refuses it and snaps instead. Guards against a teleport being smeared into a slide |
| `InterpolateRiders` | `1` | keep each drawn object's previous position here rather than reading the engine's, which a platform's carry flattens. `2` and `3` are measurements rather than settings; see **A rider had nothing to be drawn between** |
| `RiderTravelLimitPerStep` | `2.0` | the furthest a CHARACTER may travel in one simulation step before the blend refuses it and draws it where it landed. Not the mover's number: 64 here is what made the first attempt unusable |
| `StatsFrameInterval` | `0` | >0: log a frame-time/substep summary every N frames |
| `StatsPlayerFrames` | `0` | >0: dump the player's draw interpolation for N frames |

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
| `bapobj_drawAll` euler | `0x4112D9` | 0x20 bytes replaced by a call |
| `rdThing_Draw` pose gate | `0x410019` | `74 19` -> `90 90` |
| facing completion test | `0x42E3AD` | detoured; one caller, the 0x202 handler |
| substep counter increment | `0x4757DB` | operand read, address-free pattern |

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

* **The anchor is the one site where instructions are rewritten, and it fails closed.** A mean
  cannot be rate-corrected by any single factor, substituting `k` gives weights summing to `2k`,
  and at a high frame rate the anchor roughly doubles every frame until the eye leaves the world.
  That shipped once. So the arithmetic is replaced instead: `(anchor - target)*k + target`, whose
  weights are `k` and `1-k` by construction. All 63 bytes are written in one call, only on an exact
  match of all 63, and a partial match declines, a short write would land mid-x87-sequence and
  corrupt the camera *silently*.
  Two consequences: this DLL must never be unloaded (the `fmul` operand names a cell
  inside it), and the 63-byte write is safe because the mods load from the host entry point with
  one thread in the process.
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
the same six stack locals from it. A step boundary needs no clock: the ask happens once per object
per frame and an object's position changes only when the simulation moves it, so a position that
differs from the one last seen is the boundary.

Nothing the simulation reads is written. `obj->pos` and `obj->prevPos` are read and never stored
to, and the only writes are into the caller's own frame, which is the convention
`draw_interpolation.c` already follows for the drawn pitch and roll. An object the table has no
answer for falls back to the engine's own previous position, which reproduces the original
arithmetic exactly, so the worst case is the behaviour that shipped.

`RiderTravelLimitPerStep` is the same guard `MoverTravelLimitPerStep` gives a platform: past it the
object is drawn where it landed rather than being smeared across the gap, because a jump that large
is a teleport and not motion.

### Testing status

Built, and unit tested at 23 checks covering the sequence properties a game run cannot show: the
same answer for every frame inside a step, moving on exactly once when the step does, two objects
not reading each other's history, a full table refusing rather than guessing, a stale slot being
reclaimed, and the travel limit on the whole vector rather than one axis.

Played and confirmed: the judder on a platform is gone for the player and for NPCs alike, and
standing on ordinary ground is steady.

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
as long as it stood there. The boundary is counted from the alpha now, and the previous position is
brought forward whether or not the object moved.

Both faults have a check that fails without the fix.

**Still open, and separate from this.** The platform itself is a touch jittery, which is the
mover's own interpolation rather than the rider's. The instrument line reports 35 to 84 refused
poses per 600 frames against 10,000 to 15,000 blended, so about one pose in 250 is drawn unblended.
Whether that accounts for what the eye sees is not established, and the refusal count does not yet
say which of the guards refused.

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
