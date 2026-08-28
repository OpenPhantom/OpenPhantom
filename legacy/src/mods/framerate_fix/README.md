# framerate_fix

**Produces:** `framerate_fix.dll` -> `mods\`

A free render rate that does not change how the game plays. **At 30 fps every correction here is
the identity**, so the original behaviour is a fixed point.

## Supported executables

Retail `WMAIN.EXE` (EN/DE) and the Fix Pack build. On `obi.exe` most patterns do not resolve and
each affected patch disables itself with a log line. The anchor blend is the exception: its site
survives that recompile, which is why it does not share a gate with the rest of the camera work.

## Configuration: `[framerate_fix]`

| Key | Default | Meaning |
|---|---|---|
| `Enabled` | `1` | |
| `TargetFps` | `0` | 0 = uncapped (clears the limiter); otherwise 1-1000. This removes the ENGINE's limiter and nothing else: if the frame rate still sits exactly on the display's refresh, that cap is in the graphics wrapper |
| `ProcessPriority` | `0` | 0 leaves it alone, 1 above normal, 2 high. The game is single threaded and saturates one core, so a busy background process competes with it directly while the task manager shows a low total. Not shown to repair anything; a precaution |
| `CompensateCamera` | `1` | rescale the per-frame dampers `k^(dt*30)` |
| `CompensateCameraAnchor` | `1` | replace the anchor's per-frame mean with a rate-correct blend. The only patch here that rewrites *instructions* rather than an operand, so it has its own switch |
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
  Two consequences worth knowing: this DLL must never be unloaded (the `fmul` operand names a cell
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

Built and linked, `/W4 /WX` clean. Offline verification of every pattern passes on both retail builds
executables (EN, DE, the Fix Pack, and both install copies). One unit test, `camera_anchor`, the
anchor encoder is the only isolated pure logic in this DLL, and the only place where a wrong byte
produces no crash and no log line.

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
and accepted by the maintainer, which is why they now default to on. That is a judgement about how
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

Everything else in this DLL is still only reviewed and built, not accepted in game.
