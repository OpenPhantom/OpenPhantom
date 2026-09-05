# enhanced_input

**Produces:** `enhanced_input.dll` -> `mods\`

Mouse look and strafing: the tank controls become shooter controls. Off by default.

Two mutually exclusive control modes live here. **Mouse look** turns the body and the camera
follows it, as the engine always did. **Free look** (`FreeLook=1`, also off by default) turns the
camera instead and turns the body toward wherever the player asks to travel, horizontally only;
the pitch is not touched by a single byte.

Which of the two is live is a **setting, not a launch-time choice**: both are switchable from the
game's own controls screen while it runs, alongside sideways walking.

## Supported executables

Retail `WMAIN.EXE` (EN/DE) and the Fix Pack build. The phase-table pattern is an unusually rare
encoding, `FF 14 8D`, `call dword ptr [ecx*4+disp32]`, with exactly one hit in all eight shipped
images, including `obiold` and `netobi`, whose VAs differ by more than `0x1E000`.

## Configuration: `[enhanced_input]`

| Key | Default | Range | Meaning |
|---|---|---|---|
| `MouseLook` | `0` | | the master switch |
| `MouseDegreesPerCount` | `0.030` | 0.001-1.0 | degrees of view turn per **mouse count**; also settable from the controls screen, where the slider reads **1-100** in steps of one thousandth |
| `MouseAccumulate` | `1` | | bank the axis once per rendered frame instead of reading it once per substep. `0` restores the old behaviour for an A/B comparison |
| `MouseRawInput` | `1` | | read the device directly instead of through the engine's DirectInput axis. The engine's reader answers a sum, so nothing downstream can tell three device reports in a step from four; this one counts them. Falls back to the engine's reader, and says so in the log, if the registration fails |
| `MenuCursorRawInput` | `1` | | move the menu pointer from the device as well. The engine moves it in whole screen pixels against a hard-coded centre, which loses everything under one pixel and loses travel at a screen edge |
| `NewMouseInput` | `1` | | advance the **drawn** view angle once per rendered frame and hand the simulation the total afterwards. Needs `MouseAccumulate=1` and `MouseLookRigidCamera=1`; it refuses with a named reason in the log if either is off |
| `NewMouseInputDump` | `0` | 0-400 | measurement: that many rendered frames of the drawn turn, one line each, once per session |
| `MouseLookRigidCamera` | `1` | | force the camera's plain yaw arm. The engine's other arm eases toward its target and reads back the yaw it drew last frame, which would compound a per-frame correction instead of recomputing it |
| `CameraJumpWatchDeg` | `0` | 0-180 | measurement: log a line whenever the drawn camera yaw moves more than this in one frame. `0` is off |
| `KeyTurnRate` | `120` | 15-720 | degrees per second for the turn keys. The engine's own value is 120 and it is clamped there internally, so raising this also lifts that clamp |
| `MouseSpikeLimitDegPerSec` | `3000` | 360-20000 | degrees per second. At **delivery** a step past it is held back and paid out on the next frame rather than deleted; at the **door** a single frame's sample past it is cut to it before it is banked at all. A guard against a broken device, not against your hand |
| `MouseLog` | `0` | | one line per sample the door bolt had to cut. A healthy session cuts nothing and logs nothing |
| `Strafe` | `0` | | requires `MouseLook=1`; also settable from the developer menu, and from the controls screen when `MenuWidgets=1` |
| `MenuWidgets` | `0` | | put this project's three widgets on the game's own controls screen: the two check boxes and the mouse sensitivity slider. Ships off so that screen looks as it did in 1999. Nothing is lost by it: all three are keys here and the developer menu has a row for each, sensitivity included. Read once at startup, so it takes effect on the next launch |
| `StrafeInvert` | `0` | | |
| `StrafeTurnsBody` | `1` | | turn the model to face the way it travels |
| `StrafeSettleMs` | `250` | 0-1000 | how long the travel angle takes to close 90 % of a change. `0` = no damping, the old instant step |
| `StrafeTurnRate` | `240` | 30-2000 | degrees per second, the damper's hard rate cap |
| `SteerLean` | `1` | | the upper body leans into a turn again: chest and head are re-twisted after the original from the **engine's own** turn value. Mouse look only for now, under free look that value is the mouse, and there the mouse is the camera |
| `SteerLeanFromHand` | `1` | | that lean follows **your hand** rather than the engine's turn cell. Under a mouse that cell is not a rate: its only way down is a store of zero, taken on any step whose frame carried no report from the device, so the twist collapses to centre and climbs back many times a second. A held turn key is untouched and keeps the engine's own climb. Needs `SteerLean=1` and `MouseLook=1` |
| `RestoreTurnRate` | `1` | | stop zeroing the engine's turn cell, so the speed penalty on turning is the engine's again. Nothing is written into it, the double integration is subtracted in phase 7. It does not reach the follow camera, which overwrites that cell with its own number before it looks at it |
| `FreeLookAimKeepsMovement` | `1` | | while the fire button is held, your keys keep steering the walk. The shot is aimed by the engine's own offset cell instead of by turning the body, so it still goes where you look, and the upper body turns into the shot, because the engine drives the chest from that same cell |
| `SteerLog` | `0` | 0-4000 | measurement: that many substep lines in which the player is steering, then it stops |
| `SteerLeanTestDegrees` | `0` | +/-90 | measurement: force a fixed twist on chest and head, ignoring the turn. Answers whether a node rotation reaches the screen at all |
| `FreeLook` | `0` | | **the second control mode.** The mouse turns the camera and no longer turns the body. Requires `MouseLook=1`; mutually exclusive with the mouse-to-body path; also settable from the developer menu, and from the controls screen when `MenuWidgets=1` |
| `FreeLookBodyTurnMs` | `150` | 0-1000 | how long the body takes to close 90 % of a turn toward its travel. `0` = snap |
| `FreeLookBodyTurnMaxDegPerSec` | `540` | 60-2000 | the body turn's hard rate cap, deliberately above the engine's own 120 degrees per second clamp |
| `FreeLookAimSnap` | `1` | | while an attack is live, drive the body to the camera yaw |
| `FreeLookRegionRecoverDeg` | `25` | 0-180 | how much of the engine's own recentre is undone when an **authored camera region** hands the camera back. `0` switches the recovery off. A **scripted** camera never recovers |
| `FreeLookLog` | `0` | | one line in the log per **change** of free look's arming gate, never one per frame. Off unless you are chasing a camera that turns on its own |

### `MouseMaxTurnRate` was renamed, because it was the defect

The old key was a **speed limit** wearing a spike guard's name, and it produced the two complaints
"the aim sometimes snaps too hard" and "the camera shivers" as one defect:

* its allowance was `rate times the duration of the single frame it was applied in`, and **whatever it
  cut was deleted**. A mouse reports on its own clock, so the counts arriving in a frame do not
  scale with that frame's length, and frame times swing by a factor of **4.6** on real hardware
  (measured: 5.5 / 11.3 / 25.1 ms over one 60-frame window). Identical hand movement was therefore
  clipped on a short frame and passed on a long one. That is frame-time jitter cut straight into
  the aim, and it is worst under `FreeLook=1`, which drains once per rendered frame;
* and `720` was justified against the engine's own `120 degrees per second` **keyboard** turn ceiling. A 180 degrees flick
  of the wrist is 1200-1800 degrees per second, so the threshold sat in the middle of ordinary aiming: at the
  shipped sensitivity roughly half of every fast flick was thrown away.

`MouseSpikeLimitDegPerSec` holds motion back instead of deleting it, what it does not deliver this
frame is delivered on the next, so the total turn is always `degrees per count times the counts your
hand produced`. An old `MouseMaxTurnRate` left in the file is ignored and reported once, because a
tuned `720` would have carried the defect forward invisibly.

### ...and the same limit has to sit at the door, which the first version of that repair missed

Holding motion back instead of deleting it is right for **input** and wrong for a **fault**, and the
old limiter had been deleting both. The only bound on a single sample was
`MAX_PLAUSIBLE_AXIS_SAMPLE`, a million axis units, which at the shipped sensitivity is a **hundred
thousand degrees in one frame**. Once clipped motion started being paid out instead of dropped, one bad sample from the
device or its driver became a **guaranteed full turn, delivered in about a tenth of a second**. That
is what "press a direction key, move the mouse, and the view suddenly whips right round" was, and it
affected **both** control modes, free look merely shows it first, because it drains every rendered
frame while the mouse-to-body path drains once per substep.

A single frame's sample past the limit is now **cut to it before it is banked**, so the reservoir
only ever holds motion a person really made. It is cut rather than dropped because at the top of the
sensitivity band a genuine flick can reach that rate, and losing one whole is worse than shortening
it. Every cut is named in the log, once as a warning and, with `MouseLog=1`; one line each.

### `MouseSensitivity` was renamed, and the old value must not be reused

`MouseSensitivity` is **no longer read**. It is replaced by `MouseDegreesPerCount`, and the key was
renamed rather than reinterpreted because its meaning changed twice over:

* its unit was the engine's axis unit, which is `0.3` device counts, so `1.00` meant **0.3 degrees
  per count**, a full turn in 1.5 inches of desk at 800 DPI;
* and it was tuned against a path that discarded most of the motion. With `MouseAccumulate=1`
  nothing is discarded, which makes the same number roughly 4.5 times hotter at 144 fps.

An old key left in the ini is detected and reported once in the log, with the equivalent value in
the new unit (`old times 0.3`) and the value actually in force. The new default `0.030` is 12000 counts
per full turn, 15 inches at 800 DPI; the band a shooter is usually tuned in, 10-20 inches, is
`0.045` down to `0.0225`.

`StrafeSpeed` is gone as well. Sideways movement is the engine's own walk or run, so its speed is
the gait the player is in and there is nothing left for a key to set. A stale key in an existing ini
is harmless and is reported once in the log.

## Engine locations

The player half is a **pure data patch**: two pointers in `.data`, no byte in the camera path, the
integrator or the collision code. The menu half adds two detours and one repointed `push` operand.

| Site | Retail VA | What |
|---|---|---|
| `Plr_RunPhases`' phase table | `0x4482E0 + 0x06` | the table address is read from the operand |
| phase 2 (`Plr_Steer`) | table[2] | replaced by our thunk |
| phase 7 (`Plr_Integrate`) | table[7] | replaced by our thunk |
| the relative axis reader | `0x449F94 + 0x09` | resolved, not patched |
| the absolute axis reader | `0x44A089 + 0x08` | resolved, not patched |
| `pPlayer` | `0x449F94 - 4` | read from the operand, opcode checked first |
| the mode descriptor table | `0x4479D2 + 48` | read from `player_save`'s own operand |
| the fire action handler | `0x44BA3A` | detoured. It spawns the bolt as `heading + [pPlr+0x178]` from a heading read LIVE at that instant, substeps after `Plr_AutoAim` ran, which is why a heading swapped across `Plr_AutoAim` aims the search cone and nothing else |
| `bapobj_setNodeYaw` | `0x41481B` | resolved and **called**, not patched. An ABSOLUTE store, which is what lets the steering lean re-issue the two writes the original already made |
| `render_frameEnd` | `0x46C139` | **detoured** (chained), for the mouse bank and the live check box |
| `g_frameDelta` | `0x46C139 + 0x0A` | read from the operand |
| `options_controls` | `0x442A98` | **detoured**; its `push imm32` at `+23` is repointed at our widget copy |
| `swmenu_getString` | `0x45EB7B` | **detoured**, to answer one invented string id |

Free look adds seven sites of its own, six of them inside the camera update and all resolved by
address-free masked patterns. Five cells appear in two patterns each and are cross-checked against
each other before they are believed; the yaw offset appears in **three** independent ones. **They
are resolved and detoured whenever `MouseLook=1`, whether `FreeLook` reads `0` or `1`**; that is
what makes the control mode a live setting, and while it is off the hooks write nothing at all.

| Site | Retail VA | What |
|---|---|---|
| `updateCam` | `0x418544` | **detoured** (chained), 9-byte prologue, the write site |
| `gOver`, `reset`, `frames`, `substepAlpha` | `0x418544 + 0x11 / 0x18 / 0x1E / 0x40` | read from the operands of the function's own debug statement |
| the two-deep heading history | `0x4185E5 + 0x02 / 0x08` | read from the operands, the second cell twice |
| the yaw offset, the recentre rate, the region yaw | `0x418C81 + 0x09 / 0x0F / 0x02` | read from the three arguments of the recentre lerp; the offset again at `+0x1E` |
| the camera object | `0x418EDD + 0x01` | read from the operand; its `+0x38` yaw is the field the offset moves |
| the current camera region | `0x418FC5 + 0x02` | read from the operand, twice, **optional** |
| the yaw arm-select cell | `0x4186B6 + 0x0B` and `+0x2D` | read from two operands that must agree, **optional**, and the install log names which branch was taken |
| `Plr_AutoAim` | `0x44B804` | **detoured** (chained), 6-byte prologue, **optional** |

The camera update's pattern is anchored on the engine's own debug statement, which prints the
function's name and four of its globals in one line, a cheaper instrument than any call graph, in a
module the code map admits it has barely read. The `frames` cell appears twice inside that one
pattern (pushed, then zeroed) and the two operands must agree, which is what turns "seventy-one
bytes lined up" into "this is the function we want" without embedding a single address.

`Plr_AutoAim`'s pattern carries its own two constants, `FLT_MAX` and the **16.0-degree** cone half
angle, and both of its `pPlayer` operands must equal the cell the steering already resolved out of
`Plr_Steer`. It takes exactly one `int32_t` argument, which it compares against 2 inside its body
and whose four bytes its single caller cleans; a thunk declared to take none would let the original
read its own caller's frame.

Free look and `framerate_fix` cannot collide on the recentre rate. `framerate_fix` rewrites the
`mov dword ptr [rate], imm32` **immediates** at the tail of the same function; free look writes the
**cell**. The 34-byte window free look anchors on is disjoint from all four sites `framerate_fix`
patches, and the current-region pattern ends exactly where `framerate_fix`'s lag immediates begin,
so neither DLL's pattern can be broken by the other having loaded first.

`bapobj_setNodeYaw`'s pattern runs the full 63 bytes of the function, and that length is not
thoroughness. `bapobj_setNodePitch` at `0x414789` is byte-identical except for its `jae`
displacement and its final store, `89 14 08`, component 0, against `89 54 08 04`, component 1. A
pattern that stopped before the store would match both and could resolve to the wrong one.

The tables' **shape** is cross-checked rather than their addresses: 13 phase pointers that all land
in the image followed by the terminator `1`; 14 mode descriptors followed by `NULL`; and on the
controls screen the authored ids 2, 3 and a CANCEL 4.

## Why it costs no mode tests of our own

Five of the fourteen player modes skip these phases already, because their descriptors say so:
hanging, shimmy, auto-vault, death and turret. Swimming has its own phase-2 pointer in its
descriptor. Only Sidle and FixedJump are excluded by hand, because they build their displacement
from a launch velocity rather than from the run speed.

The camera does not appear here at all: in the follow state the camera direction **is** the player
direction, so turning `heading` has already turned the camera, with the authored damping and all
176 authored camera regions intact. Auto-aim and the melee sweep hang off the same number.

## The mouse: read from the device, banked per frame, drawn per frame

`game_frame` runs the substeps **first** and polls the devices afterwards, and the substep driver is
a fixed-step accumulator. So a substep only ever reads the previous frame's sample, and every frame
that runs no substep is a sample nobody reads, at 144 fps about four fifths of the hand's movement
was being polled and overwritten unread, and the surviving fifth was modulated by where the frame
boundaries fell.

The axis is therefore banked in a `render_frameEnd` callback and consumed **whole and zeroed** in
phase 2. With `MouseRawInput` the sample comes from the device rather than from the engine's axis,
which is what makes the count of reports in a frame knowable at all; and with `NewMouseInput` the
drawn angle is advanced once per rendered frame and the simulation is handed the total one step
later, so the camera turns by what the hand did on that frame instead of by a fifth of what it did
over the last step. The body still owns the heading and receives every degree. The total turn over
any interval becomes "degrees per count times the counts the hand actually produced", independent of
frame rate and of how many substeps fell where. It costs no latency: the substeps already consumed
the previous frame's input, and they still do; nothing is delayed that was not already delayed.

**The one arithmetic hazard, stated correctly.** A heading step of exactly 180 degrees is ambiguous and
anything past it turns the short way round. There is exactly **one** route to it and it is the
repeat: a frame long enough to owe two substeps runs them back to back with no poll in between, both
read the same sample, and two capped steps add up. Consume-and-zero means at most one substep per
frame receives a non-zero step, so the 90 degrees cap alone bounds it. What the clamp does at its own limit
is *saturation*, not reversal, a 179 degrees step used to be a 179 degrees turn the correct way, which reads as a
defect but does not change sign.

**Pause, cutscene and loading screens.** While the game is paused the engine skips the poll
entirely, so the axis reader keeps answering the same non-zero number for as long as the pause
lasts; a naive accumulator would integrate it once per frame and dump the total on resume. The bank
therefore goes **dormant** after 0.125 s with nobody consuming; it empties itself and stops
collecting, and the next consume re-arms it. That costs exactly one substep of input at the resume
and covers the cutscene case (polled, never consumed) with the same rule.

## Walking sideways

The DLL does not move the player sideways. It tells the engine the player is **walking**, the
forward bit of `moveInput` and the `moveDrive` the engine itself computes for a fully deflected
axis, and then turns the direction that walk comes out in. The walk and run clips, the footsteps, the
speed caps and their ramp, the acceleration, the turn penalty and the collision all follow, because
all of it is the engine walking rather than this DLL shoving.

That replaced a `carryDelta` sidestep, and the reason is one line in the clip selector: holding only
a sideways key left `moveInput` and `curSpeed` at zero, so `Plr_StandClipSelect` picked an **idle**
clip. The player slid sideways while standing still.

The direction is turned by offsetting `heading` across the phase-7 call and nothing else. The
integrator takes `sincos_deg(heading)` inside its own body, so the displacement comes out rotated
while every other reader sees the value it always had; afterwards `heading` is put back with the
engine's own formula and the facing vector rebuilt. Knockback and the conveyor surcharge are added
after the heading term in world axes, so they are untouched.

The **body** is turned by `bapobj_setNodeYaw` on node 0, the model root, applied on top of the
blended animation. The euler post-multiplies the joint matrix, so it acts in the node's own local
frame and the node's translation is carried through unchanged; node 0 is a mesh-less locator within
about a millimetre of the vertical centre line in all four hero rigs, so **the character spins in
place**.

### The angle is damped, and that is what the feature feels like

The raw angle only ever takes five values: 0, +/-45 and +/-90. Stepping straight between them moves the
model 90 degrees in one substep, 2880 degrees per second, and no amount of animation cross-fading can hide it, because
the root yaw is applied *after* the blend: the compositor builds each joint matrix from the blended
euler and only then multiplies our value in. Whatever is written is exactly what is drawn.

So the angle eases toward its target instead, closing 90 % of any gap in `StrafeSettleMs` and never
travelling faster than `StrafeTurnRate`. Holding it through the release is also the coast that used
to be missing: the key goes up, the forced walk bit stops being set, the engine's own decay brings
the speed down, and the travel direction swings back to the front over the same quarter second
rather than snapping.

Both numbers are derived from the **live** substep (`player + 0x74`), never from a compile-time
1/32. The simulation runs at 1/32 s, or at 1/64 s when the engine's own sixty-frames flag is set and
`framerate_fix`'s `PinSimulationRate` is off; a damper built on a constant would settle twice as
fast and cap twice as high in that configuration without saying so.

### The latch is cleared when Stand is left

The root node is a latch, whatever sits in it at pose-build time is used again on every rebuild.
The walk is driven in Stand only, so the last angle written there used to stay on the model for the
whole of a jump, a sabre swing or a swim. It is now walked back down to exactly zero, from **both**
thunks, so that modes which run phase 7 without phase 2 are covered too. The unwind only ever
touches the node while a non-zero angle of ours is still on it.

**Driven in Stand mode only.** That gate is load-bearing far beyond the animation: phase 2 also runs
while shoving a crate, and the push-block mode is left only when neither move bit is set, so a
forced forward bit would push the crate on a sideways key and lock the player in the mode. It also
keeps the forced bit out of the air.

**What it does not do; this README previously claimed the opposite.** It does not keep the forced
bit out of the sabre chain. `Plr_SelectSabreAction` is called from the phase-6 tick at two sites,
both gated `cmp [ecx+0x60], kMode_StandDesc`, the selector is **Stand-only**, so gating strafe on
Stand puts the forced bit *in front of* it. While a sideways key is held the selector reads a set
forward bit and picks the moving swing over the standing one. The parry arm reads a different bit
and is untouched.

## Horizontal free look

`FreeLook=1` switches the DLL into its **second control mode**. It is mutually exclusive with the
mouse-to-body path: the two never run together, the choice is made once at install, and the log
names which of the two is live in its first line. Getting that exclusion wrong is the one way this
feature could install, report success and do nothing at all.

**What it is.** The follow camera's yaw is one addition, `interpolatedPlayerHeading + a persistent
global`, and that global is a plain `f32` the engine wraps, saves and restores. Free look writes it
and freezes the one damper that pulls it home. There is no code patch in the camera arithmetic and
**no byte written anywhere in the pitch path**: the pitch is a different field, built by a different
lerp whose rate is pushed as an immediate rather than read from the frozen cell, and the eye height
is composed by adding an **unrotated** Z. A horizontal free look provably cannot tilt the view or
raise the eye. The vertical stays exactly where the level authors put it.

**Where the write happens, and why it is not the per-frame hook.** The camera consumes the offset
*after* a frame-end callback could write it, by which time the substeps have moved the interpolated
heading it was computed against. A value written there lands at "what we asked for, plus that
frame's heading change"; nothing while the body stands still, and up to nine degrees of swing per
frame while it turns fast. That is exactly the wobble the offset exists to remove. So the write goes
in a chained detour on the camera update's own prologue, immediately before the original runs, out
of the same three globals it is about to read. There is no residual and no prediction.

**The body.** Movement becomes camera-relative: `wanted travel = cameraYaw + atan2(-strafe,
forward)`, and the heading is damped toward it with the sideways walk's own damper. Because the body
genuinely faces the way it travels, the model-root rotation the sideways walk latches is walked home
and left there, and the vault probe, which under strafing tests a restored facing against a wall
the rotated displacement ran into, now tests the direction that actually hit the wall.

**The back-pedal clip is retired while this is on.** The drive is always forward, so holding back is
a half turn and a forward walk toward the camera. That is a real loss of authored content, taken
deliberately: the alternative, driving backward whenever the wanted travel is more than a right
angle off the current facing, flips the body through 180 degrees at the boundary and is worse.

**What is exempt, and what "release" means.** The camera yaw is ours only while all nine of these
allow it: the feature is on, the player record is readable, the player module's state word is 1 (the
death arm is not), the mode can be told apart, the camera object is readable and in its FOLLOW
state, no script has forced a camera region, no snap countdown is running, and the region under the
player carries none of the fixed-look-at / fixed-heading / cut / world-fixed flags. Releasing means
**stop writing**: the camera update rewrites the recentre rate from its own immediate before it
returns, so the engine's damper is back on the next frame with no restore path of ours to get wrong.

**A scripted release and an authored-region release are not the same release.** They used to be
treated as one, and that is the defect behind "in certain situations while walking the camera just
rotates".

When a **script** takes the camera, a cutscene, the director, a warp, a load, the death arm, the
wanted yaw is **dropped** and the next armed frame seeds it from the live cell. That is what makes
coming back from a cutscene put the camera where the engine has just recentred it, behind the
player, instead of where the mouse left it a minute ago.

When an **authored camera region** on the floor takes it, the player walks in and out again within a
second or two, and all the while the engine's recentre is eating his aim at four per cent of the
remaining gap per rendered frame. Dropping the yaw there hands back a camera pointing wherever that
eating happened to stop, a function of how many frames he spent on those floor polygons and of
nothing else. Every shipped level carries **between one and eight** non-follow regions (`GUNGA`,
`MAUL`, `RACE` have eight; `FEDSHIP` seven), selected per floor polygon from the high nibble of a
byte on the surface record, so this is the ordinary case rather than the exotic one. A region
therefore **remembers** the wanted yaw across its hold and gives it back when it lets go, but only
while the engine has turned the camera less than `FreeLookRegionRecoverDeg`. Past that the swing is
a genuine re-aim rather than a few stolen degrees, and undoing it would be a jump of its own.

The ordering inside the gate is what keeps the two apart, and it is load-bearing: a forced region
drives the camera object into exactly the state an authored one does, so the scripted tests run
first. Both orderings are asserted in the unit tests rather than left to reading.

**The save game, decided rather than emergent.** The yaw offset is part of the save block, so a save
taken under free look carries a rotated one. A load sets the camera's snap countdown; the exemption
releases while it runs; the engine's own snap overwrites the restored offset with the region's
authored yaw before free look writes again. **A save taken under free look restores with the camera
behind the player.** Free look adds nothing to the save block and reads nothing from it.

### The pitch and the eye height cannot move, and the log shows it

Both were re-verified against the retail image for this change rather than inherited:

* the eye height is `bv+0x2C = bv+0x1C + bv+0x0C`. The camera offset's X and Y are rotated by the
  camera's own euler, its **Z is forced to zero before that rotation** (`0x41821B`), and the rotated
  Z is then **discarded**, the unrotated `bv+0x0C` is added instead (`0x41825A`/`0x41825D`/
  `0x418263`). The yaw is not on the height path at all. The world-fixed arm at `0x418268` adds all
  three components unrotated and has no yaw on it either;
* the pitch `bv+0x34` is written from a lerp whose rate is a **pushed immediate** (`0x41868A`), not
  the recentre-rate cell this feature freezes;
* the two cells free look writes are `[0x5BB534]` (the yaw offset) and `[0x8A0100]` (the yaw
  recentre rate). Whole-image four-byte sweeps for them return **27** and **5** hits, every one of
  them inside the camera update or the module's save and restore, and none on the height path;
* the camera **position** damper is a different cell, `gPosLag [0x8A0104]`, six hits, read three
  times, once per component, including the Z at `0x418E82`. Freezing the yaw rate writes four bytes
  at `0x8A0100` and cannot reach it. What blends the region's authored eye offset in is that cell
  and the region's own record, neither of which this DLL writes;
* the authored height itself is `region+0x18`, copied to `[0x8A0118]` at `0x418836` (or `region+0x1C`
  when the detail switch `[0x4AC538] < 2`). It is asset data.

So the only way free look can change what is under the camera is by changing **where the player
walks**, which is what a control scheme is for. Every transition line in the log therefore carries
the live pitch and eye height: when those two move, the region named on the same line is what moved
them.

**Aiming.** The auto-aim searches a 16 degrees cone about the player's heading, which under free look is
where the feet point. The cone is carried across `Plr_AutoAim` by a chained detour that swaps the
heading for the camera yaw and restores it afterwards, the heading is read twice inside and both
reads are covered. That same detour is the only byte-proven signal this DLL has that an attack has
begun, so it also starts the **aim snap**: for 0.35 s the body is driven to the camera yaw, which is
what makes the shot yaw and the force-push direction follow the camera as well.

## The three settings on the controls screen

`Strafe`, `FreeLook` and the mouse sensitivity can all be set from the game's own **controls**
screen. There is no gameplay screen, a census of all 22 `swmenu_build` call sites finds exactly
four options screens, and all three are control-scheme settings.

| widget | id | string id | rect | drives |
|---|---|---|---|---|
| backdrop plate | `0x74` | | `0, 0, 640, 480` | `volume.bmp`, appended **first** |
| mouse speed slider | `0x72` | | `365, 20, 255, 50` | `[enhanced_input] MouseDegreesPerCount` |
| its caption | `0x73` | | `368, 74, 255, 50` | shows the setting multiplied by 1000, so `0.030` is `30` |
| sideways walking | `0x70` | `0x7655` | `368, 124, 255, 50` | `[enhanced_input] Strafe` |
| free look | `0x71` | `0x7656` | `368, 174, 255, 50` | `[enhanced_input] FreeLook` |

The authored widget ids on that screen are 50, 0, 1, 2, 3, 4, 5 and 6, so all five are free, and
the patcher is asked again at run time rather than trusting that list.

**The plate is not decoration.** The right-hand column is empty because the background art leaves it
empty, and what is behind it is the live 3-D scene, white caption text over which is barely
readable. Every authored block of text on these screens sits on a plate for that reason.

The first plate was `popup.bmp`, this screen's own 300x200 message plate. It fixed the readability
and still looked wrong, and the reason is worth keeping: **a message plate is drawn to be an
interruption**, so borrowing it for settings puts them in the box the game uses to ask "are you
sure". The **audio** screen has already solved this exact problem; two sliders with their labels
stacked in a panel drawn for them, and every options screen shares one bitmap table, so its
background `volume.bmp` (index 9) is already loaded here. The group therefore draws that background
and lands on that screen's grid, row for row:

| audio screen | this group |
|---|---|
| `SLIDER 365, 20,255,50` music | `SLIDER 365, 20,255,50` mouse speed |
| `CHKBOX 368, 74,255,50` | `TEXT   368, 74,255,50` its caption |
| `SLIDER 365,120,255,50` sound | `CHKBOX 368,124,255,50` sideways walking |
| `CHKBOX 368,174,255,50` | `CHKBOX 368,174,255,50` free look |

The numbers are copied rather than approximated, because the panel behind them has its recesses
drawn at exactly those places, and the plate's rectangle is the full canvas because its panel is
part of the bitmap rather than a rectangle. It is a **whole screen background**, so it brings the
audio screen's other artwork with it; that is the trade, taken deliberately. Three
`_Static_assert`s fail the build if a row leaves the plate or reaches BACK's row, and the plate is
appended **first**, because the engine draws a screen in table order.

**The slider is only possible because the two screens share a bitmap table.** A slider names its
track and knob as indices into the screen's own bitmap-name table, and the controls screen ships
none of its own, but it is built with `pBmpNames = 0x004AEE10`, the same table the video screen
uses, whose entries 2 and 3 are `slgauge.bmp` and `slslide.bmp`. That was checked before the widget
was written, because the failure mode is a control that installs, logs success and draws nothing.

**The group is a right-hand column, and the first attempt was not.** The boxes were at `7,350` and
`7,400`, which collided with no authored rectangle and still looked wrong: the screen's background
is a single full-screen bitmap whose panel plate reaches *below* the last button's rectangle, so the
first box sat half on the plate. No rectangle in the table says so. The group now uses the video
screen's own grid, the slider and caption have that screen's gamma-slider rectangles shifted down
as a pair, so the two screens read as one design. It clears the title (ends y = 86) and BACK
(starts y = 400 at x >= 484), and it overlaps only the screen's hidden message plate at
`170,140,300,200`, which ships `visible = 0`.

They are **check boxes** and not selectable labels because `swchkbox_activate` ends in `or eax,-1`:
every screen loop runs `while (result < 0)`, so a check box is the one widget class that cannot
close a screen. Each state is written by the engine straight into our own array, so reading it back
needs no engine call, and each box records its own index so the two can never read each other's
setting.

`swmenu_getString` indexes the localised table with **no bounds check** and the highest authored id
is 414 across every widget on every screen, so an invented label id would be an unchecked read
rather than a blank label. The getter is therefore detoured **first**, and no widget is appended
until that detour stands; it answers either reserved id whether or not the matching box was
appended, so an id this DLL has claimed cannot reach the table by any route. The captions follow the
Windows UI language, ASCII only because the menu bitmap fonts' coverage above `0x7F` has not been
read out of the assets:

| | en | de | fr | it | es |
|---|---|---|---|---|---|
| strafe | `STRAFE` | `SEITWAERTS LAUFEN` | `PAS CHASSE` | `PASSO LATERALE` | `PASO LATERAL` |
| free look | `FREE LOOK` | `FREIE KAMERA` | `CAMERA LIBRE` | `TELECAMERA LIBERA` | `CAMARA LIBRE` |

Neither German caption is the English word. *Strafe* is German for punishment; and it is `FREIE
KAMERA` rather than `FREIER BLICK` because the feature turns the camera, not the gaze, the pitch
field this DLL never writes.

**Both boxes take effect at once, and neither patches a byte when it is ticked.** Each one only
flips a `bool` that its machinery already reads: the phase thunks for sideways walking, the two
camera detours for free look. That is why free look's hooks are installed whether the ini says `0`
or `1`, while it is off they write nothing at all, because the arming gate refuses on the switch
itself before it reads anything. The alternative, resolving the camera and patching its prologue at
the moment the box is ticked, would write to code from inside a menu screen's own loop.

Switching free look off is exactly the release the feature already performs whenever a cutscene
takes the camera: it stops writing, and the engine's own recentre, whose rate the camera update
rewrites from its own immediate before every return, swings the camera back behind the player.
Switching it on re-seeds the wanted yaw from the live cell, so the camera is picked up where the
engine has it and nothing jumps.

**Neither box exists when `MouseLook=0`.** That is the whole handling of "free look requires mouse
look": both settings need this DLL's two phase thunks, those are only swapped in under `MouseLook=1`,
and install returns before the menu is patched, so the screen is left exactly as it shipped rather
than given switches that could not do anything. The log says so. Mouse look itself gets no box for
the reverse reason: turning it off would have to unswap two phase pointers, and the detour machinery
here deliberately cannot be uninstalled.

If the follow camera in a build is not recognised, free look cannot run at all in that session and
**its box is not added**, the strafe box still is, and one warning names what is missing. A box that
could never turn anything would mislead.

Exactly one DLL may extend any one screen: once a DLL has repointed the `push` operand at its own
buffer, that address is no longer inside the host image and a second DLL is refused. That is why the
second box is a second entry in the *same* patch, appended to the one copy taken at loader time.
`enhanced_input` owns **controls**; `variable_fov` owns **video**.

## Known limitations

**Under `FreeLook=1` the walk-backward clip never plays.** Holding back is a half turn and a forward
walk toward the camera, so the forward move bit is set and the backward one cleared; that is what
makes the body face its travel. It is deliberate, and it is the one animation the free-look scheme
substitutes. With `FreeLook=0` the backward clip plays exactly as it shipped.

* **`Strafe` requires `MouseLook`.** `turnWheel` is the only turn channel in the engine. Mouse and
  keyboard exclude each other in the original, but both land there, so turning the keyboard axis
  into strafing means clearing it, which would leave a character that cannot turn at all. The DLL
  enforces this and logs it.
* **`FreeLook` requires `MouseLook` too**, and for a plainer reason: free look replaces the
  mouse-to-body half of the mouse-look scheme, and that scheme is the two phase thunks. With
  `MouseLook=0` neither thunk is installed, so there is nothing to replace. Nothing is offered in
  that state, no check box on the controls screen either, rather than a switch that could not do
  anything.
* **With mouse look on, the forward lean of chest and head stays off.** `Plr_Steer` computes it at
  the end of its own body, from the value it has just set itself, so overwriting `turnWheel`
  afterwards is too late.
* With mouse look on and strafe off, keyboard axis 0 does nothing at all. Logged as a warning.
* **Sideways walking is ground-only.** No air strafing, and none during a sabre attack.
* **Diagonals are no longer faster.** One capped velocity is rotated instead of two being added.
* **A shot fired mid-strafe leaves from a rotated hip.** The muzzle is a node on the body and the
  body is turned; the shot's direction is built from `heading` and is not. Cosmetic.
* **The body latch is not unwound in Hang, Death or Tripod.** Those three modes run neither phase 2
  nor phase 7, so nothing of ours is called there. Entering one of them straight out of a strafe
  leaves the angle on the model until a mode that does run a phase is entered again.

### Free look only

* **Walking into an authored fixed-camera region is noticed one frame late.** The engine chooses the
  region *inside* the camera update, from the floor polygon under the player, so no external feature
  can see the change before it happens. The cost is one frame of a wrongly rotated camera followed
  by the engine's own half-second swing into place, or nothing at all if that region carries the cut
  bit. Every **scripted** camera, dialogue, the cutscene director, the front end, a set piece, the
  turret, is caught one frame *early* instead, because each forces its region during the substeps.
  This is the first thing to look at in game: walk into a fixed-camera region with the camera held
  90 degrees off and judge whether the transition is acceptable.
* **Leaving a region the recovery declines is still a stranded camera.** When the engine has re-aimed
  the shot by more than `FreeLookRegionRecoverDeg`, free look arms on the first follow frame and
  freezes the camera wherever the recentre had reached, which in the original game would have gone
  on swinging until it sat behind the player. The camera is then pointing at a world direction the
  player did not choose until he moves the mouse. Letting the swing finish before arming was
  considered and rejected: at the engine's own rate that is up to two seconds during which the mouse
  turns the *body* instead, which is a worse surprise than the one it fixes. If the field log shows
  this is the common case rather than the rare one, this is the thing to revisit.
* **The cut bit is in the release mask even though it leaves the camera in its follow state.** Bits
  0, 1 and 3 pick a camera family; bit 2 only asks for a snap, and the arm it selects assigns the
  yaw offset the region's authored yaw *later in the same call* than anything free look can write in
  front of it. Staying armed through a cut region would be a per-frame flicker between the two
  values, not a compromise, so the release hands the cut to the engine whole. No shipped region
  carries bit 2 on its own, the three that carry it are `flags 12`, world-fixed as well, so this
  is a rule about what the mask means rather than an observable difference.
* **The body does not turn outside Stand, unless an attack is live.** In the air, in a launched
  sidestep and while swimming the mouse moves the camera and the body holds its heading. That is not
  conservatism about animation: outside Stand the forced forward drive is not in force, so a
  backward key is still a *negative speed along an unchanged facing* rather than a half turn, and
  building the camera-relative angle there would double-count the reversal and send the player the
  wrong way. The body comes round of its own accord on the next Stand substep in which a movement
  key is held.
* **The turn penalty is off and stays off.** `Plr_Integrate` scales the displacement from
  `|turnWheel|`, and mouse look clears `turnWheel`. Under free look the body turns fast and often and
  the penalty, which exists to stop exactly that, never bites. It cannot be restored by writing
  `turnWheel`, because `Plr_Steer` clamps it and computes the chest and head lean from it before our
  thunk regains control. If it reads as skating, the remedy is a penalty on `moveDrive`, not there.
* **The shot yaw and the force-push direction follow the body, not the camera, on the first frame of
  an attack.** Neither `Plr_FireWeaponAux` nor `Plr_ForcePushAux` has a single `call rel32` site in
  the image; both are reached through a dispatch table, so neither can be diverted the way
  `Plr_AutoAim` can. The aim snap closes the gap within about a sixth of a second and an *aimed* shot
  is correct immediately, because the auto-aim cone it was picked from is the camera's.
* **A shot fired from the air.** The un-reconstructed air-attack block was swept for both the heading
  and the actor-yaw field and reads neither, so it is not a further consumer, but the body does not
  turn in the air either, so an air attack goes where the body was left.
* **Freezing the recentre is not a strict identity.** The engine's angular lerp returns its target
  outright when the two are within a thousandth of a degree, *before* it looks at the rate. Inside
  that band the offset is snapped to the region's authored yaw whatever we write. A thousandth of a
  degree, named rather than hidden.
* **`FreeLookRecentreMs` is not implemented.** A soft recentre toward the heading would fight the
  seventeen shipped follow regions that author a real over-the-shoulder angle, three of them a full
  90 degrees. No key is offered rather than a key that does nothing.
* **Switching it on or off changes what the movement keys mean, mid-session.** That is the feature
  working, not a defect, but it is worth naming: under free look a held back key is a half turn and
  a forward walk toward the camera, and under mouse look the same key back-pedals. The switch is
  live in both directions and nothing has to be reloaded.
* **The two camera detours are placed even when free look is switched off.** They write nothing in
  that state, the arming gate refuses on the switch before it reads a cell, but they are in the
  chain on `updateCam` and `Plr_AutoAim` for every session with `MouseLook=1`. That is the price of
  a live switch, and it is paid deliberately: the alternative is patching code from inside a menu
  loop.

### Both modes

* **Not implemented here, and deliberately so:** the momentum rewrite (driving `curSpeed` or a full
  2-D velocity), the animation phase carry between the walk and run clips, and writing the clips'
  own blend duration. Each carries a hazard that has to be handled on its own terms, an
  out-of-bounds write when the puppet's four track slots are exhausted; a fade of <= 0 s becoming a
  *one-second* ramp rather than an instant one; `want, curSpeed` behaving discontinuously at
  `curSpeed == 0` because the zero arm applies no cap at all; thirteen functions touching
  `curSpeed`, three of them inside a documented reconstruction hole; and `bafClip` records being
  shared per `.baf` **name** across every actor built from it. The damper alone is what the research
  ranks first for feel, and it is what this change ships.

## Fallback behaviour

Every resolution step refuses rather than guesses: an implausible mode index, a table whose shape
does not match, an axis reader outside the image. If the second phase pointer cannot be written the
first is rolled back, the phase table is never left half swapped.

**The mouse bank.** Without the `render_frameEnd` hook, or without `g_frameDelta` (the bank needs a
clock both to measure its own rate limit and to notice a pause freezing the sample), the axis is
read once per substep exactly as the engine does it. That path is real and implemented, not
described: motion between substeps is dropped and the effective sensitivity follows the frame rate,
and the per-substep step is capped at **25 degrees** rather than 90 degrees. That number is arithmetic, not taste.
The substep driver clamps a frame to 0.1 s before dividing it into substeps of 1/32 s, or 1/64 s
with the sixty-frames flag, so at most seven substeps can run on one poll, and 7 x 25 = 175 stays
under 180 in the worst case the engine can build. At 32 Hz it is still 800 degrees per second of allowance. The log
line states the degraded behaviour, not the intended one.

**Free look refuses rather than half-installs.** If any of the four cells it cannot run without, the
yaw offset, the recentre rate, the interpolated heading's two inputs, the camera object, fails to
resolve or fails one of its cross-checks, or if the camera update cannot be detoured, free look
**cannot be offered at all in that session**: not from the ini, not from the controls screen. It
says which check failed, its box is left off the screen, and today's mouse look runs untouched.
There is no honest half of it: writing the offset without freezing the recentre gives a camera that
slides home under the player's hand, and freezing the recentre without the exemption signals leaves
a scripted camera rotated for good. Nothing is written before the last check passes, so a refusal
leaves the process exactly as it was.

Two of its sites are **optional**, and each has a real fallback rather than a described one:

* *the current camera region.* Without it the exemption falls back to the camera object's own state,
  which the engine sets from those same region flags, so every fixed-camera family on the shipped
  levels is still caught. What is lost is the direct test of the cut bit on a region that is
  otherwise a follow region, which is the one case the state cannot see.
* *`Plr_AutoAim`.* Without it the cone stays centred on the body and only the aim snap brings it
  round.

**What a release falls back to is the other mode, in full.** Whenever free look is not armed, a
scripted camera, an authored fixed region, a snap countdown, the death arm, no level, the mouse
step goes back to turning the body and sideways walking behaves exactly as it does with
`FreeLook=0`. The player is never left with a mouse that does nothing. The model-root angle that
path latches is walked home on the first substep after free look arms again.

**The transition log** (`FreeLookLog=1`). A release writes no byte, changes no setting and used to
produce no message, so a camera that turned on its own could not be traced to the condition that
caused it. One line is now written per **change** of the gate, never one per frame, and at most 400
per session, after which one warning says so and the count itself is the finding. Each line carries
the condition **by name**, the region record's address and flags and its authored yaw, the yaw on
screen against the yaw free look wants and the difference between them, the live camera **pitch** and
**eye height**, and `gOver` / snap countdown / camera state / module state / mode index. A release
whose reason changes while it lasts, a region handing over to a cutscene, is a second line, not a
silence.

**Free look and the mouse bank.** Free look drains the bank a second time, once per rendered frame,
so the camera advances on every frame rather than only on the roughly one frame in five that runs a
substep at 144 fps. That is only safe while the bank is live, because the degraded path answers a
**live, unzeroed** sample that two readers would both receive and the turn would be doubled. So the
second drain is switched off whenever `mouse_look_is_accumulating()` is false, and the log says the
camera then advances once per substep instead. That is also why free look is installed *after* mouse
look: the answer is not decided until then, and calling them the other way round degrades to the
per-substep path rather than double-counting.

**Non-finite values.** Every configuration float is clamped through a comparison that turns NaN into
the minimum and either infinity into a bound. On top of that the interpolated heading, the seed read
back out of the live cell, the wanted camera yaw, the offset about to be stored and the body's damper
step are each tested for finiteness at the moment they are produced; a value that is not finite
releases the camera for that frame and is reported once. A NaN reaching the camera euler is an
unrecoverable picture, so it is gated at the store rather than hoped away upstream.

**The check boxes.** If `swmenu_getString` cannot be hooked, **no widget is added at all**, a label
that reads past a table is not an acceptable fallback for a convenience. If the controls screen
cannot be detoured, no widget is added either, because a box whose state is never read back is worse
than no box; the widget array is committed only after that detour stands, so the failure leaves the
engine owning its own array. Without the per-frame hook a box takes effect when the screen closes
instead of on the click, and it is still read, applied and saved.

The two boxes fail independently. One that cannot be appended is simply absent, and the other is
unaffected; each is read back through its own recorded index, and only a box that was both appended
*and* committed is ever read at all. A setter that refuses puts its box back in step with what
actually happened, so a refusal shows as the tick springing back rather than as a switch that
silently does nothing.

## Testing status

Built and linked with the configured 32-bit MSVC toolchain, `/W4 /WX` clean. Three builds of this
engine ship inside one installation, and every byte pattern resolves with the expected match count
on the two retail ones. The failures on `obi.exe` are expected and are the point of the design, that
build being a recompile; the six free-look patterns resolve there as well, because every one of them
is address-free and masked.

The mouse path has been played. The raw device reader, the per-frame bank and the per-frame drawn
angle were accepted in the game on a 240 Hz display, and the drawn turn was measured over more than
two thousand rendered frames while that was done.

Four test files cover this feature. `unittests/mouse_rate.c` drives the rate estimator and the
bank; `unittests/view_lead.c` proves a property of a sequence rather than of one call, that the
drawn angle advances by one frame of hand movement on every frame while the body turns once per
step, and it carries the first design as a regression because that one turned the camera backwards
once per step; `unittests/delivery_rates.c` asks whether the delivery survives a slow mouse, which
a field test on one desk cannot answer. The fourth, `unittests/strafe_walk.c` with 128 checks,
covers:

* the travel angle including the backward family;
* the damper, framerate independence across 1/32 and 1/64, the 90 % settle definition, the rate
  cap, monotonicity, landing exactly on the target, decaying exactly to zero, and the three
  degenerate inputs;
* the mouse bank, consume-and-zero, the dormancy rule against a frozen sample, and the rate clamp;
* free look's wrapping, including that a value which is not a number **survives** rather than being
  normalised into a plausible zero, because the finiteness gate is what releases the camera and it
  can only see what reaches it;
* free look's input angle, whose rule is the mirror image of the travel angle's; there the drive's
  sign is divided out, here it must stay in, and a lone backward key is a half turn rather than a
  no-op. Swapping the two rules sends the player forward while the backward key is held;
* the offset round trip, including across the 0/360 seam: interpolated heading plus offset must be
  the camera yaw that was asked for;
* that the camera yaw is INDEPENDENT of the heading, the same wanted yaw reconstructed against
  four very different headings, plus the exact half turn a backward key produces, which is the
  case the retired leash used to drag the camera through;
* the arming gate, with each of its conditions closed one at a time, plus the two cases that
  must **not** close it (the fast-lag bit alone, and a region that could not be resolved);
* which condition the gate **names**, that no two of them share a name, and the load-bearing pair:
  that a script forcing a region outranks the region it forced and that a snap outranks the camera
  state it produced. Those two orderings are what keep a cutscene out of the family that gets its
  yaw handed back;
* the bounded recovery: taken back on either side of the limit, exactly at it, refused one degree
  past it, measured across the 0/360 seam rather than the long way round, and switched off by a
  limit of zero, a negative one or one that is not a number.

`unittests/menu_patcher.c` additionally appends **two** check boxes to a copy of the shipped
controls table, the eight authored widgets with their real ids and rects, and checks that both
ids are free on it, that each box gets its own index in append order, that every field of the
byte-proven check-box record lands where it belongs, that a duplicate or shadowing id is refused,
and that the second row clears BACK and fits on a 480-high screen. The widget ids and the string
ids were also confirmed against the retail image directly: the controls table uses ids 50, 0...6, and
a census of every widget in every screen puts the highest authored string id at 414, with neither
`0x7655` nor `0x7656` in use.

**Accepted in game**, in the 1.5.0 build, which was played through by hand. Both check boxes have
been seen and clicked, and both switches take effect live: the arming gate reads them on every
camera update and the phase thunks read the same gate on every substep, which is what the code
path predicted and what play confirms.

The check boxes are behind `MenuExtras` and ship off, so the vanilla menu is the one the game
shipped with. The same two switches are always reachable from the dev menu's Utilities page,
which is where they were exercised.

**One inference that play does not settle.** That an authored camera region is what released the
gate during ordinary walking is read out of the bytes, and no amount of ordinary play tells the
two apart from the outside.
It is the only one of the gate's conditions that *can* fire while merely walking, the other five are
scripted (`gOver` has seven call sites, all cutscene, scene-op or set-piece code), or level
transitions (`reset` is set to 3 by `bapview_newView` and to 1 by the save restore, and is
decremented once per camera update), or structural. `FreeLookLog=1` for one session answers it
outright: if the release lines name *an authored camera region under the player*, the inference
holds; if they name something else, the fix above is aimed at the wrong condition and the lines say
which one to aim at instead.

## The camera has two yaw arms, and free look forces one of them

`bapview_updateCam` computes the camera yaw in two different ways and picks between them on a
global that holds the **signed per-frame change of the body's target heading**:

* **arm B** `0x418F86`, `yaw = wrap360(interpolatedHeading + yawOffset)`. Runs iff that global is
  exactly `0.0`.
* **arm A** `0x418F6D`, an eased blend of where the camera is and where it should be.

Everything free look computes assumes arm B. Arm A's 0/360 seam handling is derived from the sign of
the **body's** turn, which is a fine proxy while the camera is bolted to the body, and free look is
the one thing that decouples them. Measured in the field: a camera 2.7 degrees from its target was moved
**78 degrees in a single frame**, and with the camera between 90 degrees and 270 degrees and a gap over half a turn no
seam branch can fire at all.

So on armed frames free look writes the interpolated heading the camera is about to use into the
cell the engine compares against. The engine then measures a change of zero and takes arm B. That
cell has exactly two references in the whole image, both inside that one comparison, and the engine
overwrites it with its own value a few instructions later in the same call, so the write leaves
nothing behind and, as with the recentre freeze, the release is simply to stop writing.

**What it costs:** on armed frames the engine's one-frame easing of the camera **yaw** is gone and
the yaw becomes exactly what free look asked for. The eye position keeps its own lag, the pitch is
untouched (it is built further upstream from a different lerp), and on released frames the engine
chooses its arm as it always did.

If that cell does not resolve, free look still installs and runs, with the defect, and the log
says so in as many words rather than exiting quietly.
