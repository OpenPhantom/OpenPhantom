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
| `TargetFps` | `0` | 0 = uncapped (clears the limiter); otherwise 1-1000 |
| `CompensateCamera` | `1` | rescale the per-frame dampers `k^(dt*30)` |
| `CompensateCameraAnchor` | `1` | replace the anchor's per-frame mean with a rate-correct blend. The only patch here that rewrites *instructions* rather than an operand, so it has its own switch |
| `CompensateAnimation` | `1` | the animation clock and the emitter dormancy counter |
| `AnimationClockMode` | `1` | 1 = the authored 30 Hz rate |
| `SpinSleep` | `0` | `Sleep(0)` -> `Sleep(1)` in the frame wait |
| `PinSimulationRate` | `1` | nail the substep to 1/32 s |
| `InterpolatePitchRoll` | `1` | interpolate the drawn pitch and roll like the drawn yaw |
| `PosePerFrame` | `1` | rebuild the joint matrices every frame |
| `StatsFrameInterval` | `0` | >0: log a frame-time/substep summary every N frames |
| `StatsPlayerFrames` | `0` | >0: dump the player's draw interpolation for N frames |

## Engine locations

| Site | Retail VA | What |
|---|---|---|
| `sys_waitForFrame` | `0x475B75` | both cap immediates, the limiter flag, the Sleep push |
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

## Why each correction exists

* **The simulation is already free.** `sys_runSubsteps` is a fixed-step accumulator at 1/32 s. AI,
  physics, projectiles and collision do not care about the render rate.
* **The camera does not know that.** `bapview_updateCam` answers a per-frame message and ignores
  the dt it is handed; five exponential dampers run 4.8 times as often at 144 fps.
* **The animation clock is a per-frame counter.** Water waves and scrolling UVs read it as if it
  were a clock.
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

## Fallback behaviour

If the per-frame hook cannot be installed, the camera compensation and the animation clock do not
run and the log says so explicitly. The render cap, the pinned simulation rate, the emitter
dormancy and both draw patches are already in place by then and stay in place.

## Testing status

Built and linked, `/W4 /WX` clean. Offline verification of every pattern passes on all five retail
executables (EN, DE, the Fix Pack, and both install copies). One unit test, `camera_anchor`, the
anchor encoder is the only isolated pure logic in this DLL, and the only place where a wrong byte
produces no crash and no log line. **Not accepted in game.**
