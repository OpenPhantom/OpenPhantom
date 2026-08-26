# ground_clip_fix

**Produces:** `ground_clip_fix.dll` -> `mods\`

Stops a character being pushed down through the floor it is standing on.

## The symptom

A character sitting on a box can be walked down through it and under the level by bumping into
them. Standing on their head and jumping does it fastest. They never come back up. It happens in
the 1999 game as shipped and has nothing to do with this project's other fixes.

## What is actually wrong

**The character is not failing a collision test. She is not taking one.**

`FUN_004362C8`, the character movement function, reads the movement mode at `character+0x98` before
it does anything else, and jumps clean past the whole collision block when bit 0 is set:

```
004363DF  mov  eax,[edx+0x98]
004363E5  and  eax,1
004363EA  jnz  0043650B          past the swept test, straight to the commit
```

The allow flag it commits against was already reset to permitted at the top of the step by
`FUN_00435c67`, so the move is committed unconditionally.

That exemption is sound while its assumption holds. These are the static, seated characters that
fill out a street and never go anywhere, so testing them would be work for nothing. Read out of one
level with `[diagnostics] Characters`:

| mode | who | tested |
|---|---|---|
| 0 | every ordinary NPC, including the one stood next to her who never sinks | yes |
| 1 | a fixed prop | no |
| 3 | the seated background characters, including the one that sinks | no |
| 5 | one character, bit 0 set alongside another bit | no |

**Contact is what breaks the assumption.** The handler in `enemy.c` at `FUN_00436a68` adds an
impulse to a character's velocity on contact without asking whether that character can be moved
safely. Standing on her points it down. The movement function integrates it and commits the result
at `0x0043655E` with nothing consulted, and the landing path then clears the velocity while leaving
the position where it ended up, so the next push starts from there.

## What this changes

Before the movement function runs, a character that is exempt from collision has its velocity
cleared. That restores the engine's own invariant, that an untested character does not move, rather
than arguing with the exemption.

A collision tested character is never touched, which is every ordinary NPC and the player. A
falling character keeps its velocity and lands normally, because it is tested.

## Configuration: `[ground_clip_fix]`

| Key | Default | Meaning |
|---|---|---|
| `Enabled` | `1` | off leaves the engine's own behaviour, and says so in the log |

## Engine locations

| Site | Retail VA | What |
|---|---|---|
| character movement | `0x4362C8` | detoured; an untested character's velocity is cleared before the move |

Fields read, both confirmed by the diagnostics character census before this fix was written:
`character+0x98` the movement mode, `character+0xDC` the velocity.

## Three earlier attempts, and why they failed

Recorded because each looked right from the disassembly, each cost a play session, and the next
person reading this will be tempted by at least one of them again.

| attempt | how it died |
|---|---|
| gravity settling her onto a wrongly chosen floor | the steps were exactly one sixteenth every time and never accelerated, and paused for seconds while the player stood beside her. Gravity does none of that |
| a refused move never clearing her downward velocity, so it accumulated | her velocity reads zero while she stands still and spikes only on the steps she moves, so it is an impulse, not something retained |
| the swept collision test raising its ray origin by a step-over allowance, hiding a small descent | **built, shipped to a test install, and changed nothing.** The instrument added to find out why is what found the real cause |

That third one is the useful one. The census put in to explain the failure reported: in four
thousand sweeps, six were descending, all six were the **player** landing, and not one carried the
allowance the character move test passes. Her move never reaches that function at all.

The lesson worth keeping: counting what a hook actually sees is worth more than reasoning about
what it should see.

## Testing status

**Accepted in the game.** Bumped and jumped on in the level where the fault was reported: the
character held her position for the whole run, and no character in the level changed height at all,
against a measured `0.875` unit descent in fourteen steps before the fix.

The contact impulse is still visible in the census at the moment it is applied, so the push is
still happening and simply goes nowhere. That is the intended behaviour rather than the impulse
being suppressed.

18 checks cover the decision, using the modes read out of the live level rather than invented ones,
on both sides of the line: a collision tested character keeps its velocity even when falling, an
untested one loses it, and a NaN velocity on an untested character is cleared rather than trusted.

**What has not been tested** is a whole game. These characters are meant to be inert, so the risk
this design carries is the opposite of the original bug: something that should move no longer
moving. If a background character that used to shuffle or get knocked about now stands frozen, or a
prop that used to be shovable will not budge, that is this fix overreaching and worth reporting.
