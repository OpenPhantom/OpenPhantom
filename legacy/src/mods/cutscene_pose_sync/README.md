# cutscene_pose_sync

**Produces:** `cutscene_pose_sync.dll` -> `mods\`

Keeps the player's own render-interpolation state from going stale right as a cutscene begins. This
is a real, if minor, fix found while chasing a much more visible report - see "What this does NOT
fix" below for where that report actually got fixed.

## Configuration: `[cutscene_pose_sync]`

| Key | Default | Meaning |
|---|---|---|
| `Enabled` | `1` | |

## What is actually broken

Field report: right after the intro movies play going into level 6 (`fedship.b3d`, the Trade
Federation Battleship), Obi-Wan drops into the map as if he had been suspended somewhere higher, and
looks wrong for a few seconds before settling. Isolated live to `fmv_player.dll` (the retail Bink
path, which always forces a resolution switch around every movie, does not show it).

Every drawn object's position is blended each frame, `previous + (current - previous) * alpha`
(`bapobj_drawAll`, `0x004112D9`), between two fields on its own body: `current` at `body+0x18` and
`previous` at `body+0x54`. A live probe caught the player's own body at the exact moment the opening
cutscene locks player control, twice, back to back:

```
current=(122.30,129.00,42.00)  previous=(0.00,0.00,0.00)      <- first read
current=(122.30,129.00,42.23)  previous=(122.30,129.00,42.00)  <- second read, a moment later
```

`previous` is not merely stale, it is the world origin - a value nothing ever wrote, on a body that
plainly did not exist yet the instant before (a probe taken right after the level finished loading,
earlier in the same run, found no body at all: `body=00000000`). This is what a freshly created
object's own render-interpolation pair looks like before anything has had a chance to seed
`previous` to match `current` - and it self-repairs almost immediately, which is exactly why it is
invisible in ordinary play: something reaches this body again very soon after and sets it right.
What retail's resolution switch was actually buying, incidentally, was time - an extra frame or two
for that natural repair to land before the very first real draw ever samples `previous`.
`fmv_player`'s own faster transition does not leave that gap, so the first draw can land before the
repair does.

## What this does

`Dialog_EnterInputLock` (`0x00430ED9`) is the cutscene lock's own entry point, confirmed across
every capture taken chasing this report to fire reliably at exactly this transition, before the
frame's own draw call: the simulation step (where a level's own script, including this lock, runs)
is ahead of the camera and object draw in the same frame. Hooked here, this reads the player's own
body (through `pPlayer`, the same pointer chain `dev_overlay`'s giant/tiny player cheat already
uses) and writes its `current` position over its own `previous`, unconditionally, every time this
lock is entered.

That is deliberately not conditional on detecting the stale case. In the ordinary case `previous`
already sits close to `current` - normal per-substep tracking keeps them within one substep's travel
of each other - so forcing equality changes nothing the eye could ever register: a cutscene lock is
also the instant free player movement stops anyway, so even a genuine one-frame interpolation reset
lands at the one moment in the whole game built to tolerate it without being noticed. Only in the
stale case, which is what this exists for, does the write actually matter.

Only ever touches the player's own body, and only the three floats at its own `+0x54`. No level data
changes, and nothing here can act on any other actor, any camera state, or any resolution.

## What this does NOT fix

**The visible "Obi drops from the ceiling" symptom is a different, larger bug, fixed in
`fmv_player.dll`, not here.** Deployed and tested on its own, this fix did not resolve it: Obi still
visibly dropped, with a genuine falling/landing sound. A second, much more thorough live probe (this
time watching `pPlayer+0xA0`, `pPlayer+0x118` and `pPlayer+0x124` frame by frame, not just the render
blend) found the real mechanism - a position-override flag on the player that force-copies its
*authoritative* position, not just the drawn one, through a real overshoot-and-settle transient
lasting several hundred ms after the cutscene lock. That is a genuine engine transient, not a
rendering artefact, which is exactly why a real sound plays during it. See `fmv_player/README.md`,
"The post-movie curtain", for the actual fix and the full data behind it.

This mod is kept anyway: `previous=(0,0,0)` on a freshly spawned body is real, confirmed staleness,
and correcting it is free and harmless even though it turned out not to be the whole story - or even
most of it - for this particular report.

## Testing status: PARTIALLY CONFIRMED IN GAME (2026-08-22)

The mechanism (`previous` reading as the world origin on a freshly created player body) is confirmed
live, twice, and the fix compiles clean and installs without error. What is confirmed NOT to follow
from it: fixing this alone does not stop the visible drop reported for level 6, which needed the
separate fix in `fmv_player.dll` described above.
