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

**Exempt from collision does not mean static, and that distinction cost a shipped regression.**

The obvious repair is to clear the velocity of any character the engine will not collision test.
That was built, shipped, and was wrong. Two populations carry the exemption:

| | | |
|---|---|---|
| seated background characters | never move | clearing a velocity they never carry costs nothing |
| ships, birds, droids on flying platforms | exempt **because** they fly | they move by velocity, and clearing it froze every one of them |

No field separates the two. What separates the cases is **where the velocity came from**: a scripted
mover sets its own, while a contact adds one through the handler in `enemy.c`.

So this hooks the contact handler, remembers the velocity of the character being contacted, lets the
handler run, and puts the velocity back if that character is one nothing will collision test. A
ship's own velocity is identical either side of that call, so there is nothing to undo and it is
never touched. Only a velocity the handler itself changed counts as a push.

It restores the previous value rather than zero, so a flying character contacted mid flight keeps
the course it arrived with instead of stopping dead. Everything else the handler does, damage
included, is left exactly as the engine wrote it.

## Configuration: `[ground_clip_fix]`

| Key | Default | Meaning |
|---|---|---|
| `Enabled` | `1` | off leaves the engine's own behaviour, and says so in the log |

## Engine locations

| Site | Retail VA | What |
|---|---|---|
| contact handler, `enemy.c` | `0x436A68` | detoured; a contact push is taken back off a character nothing will collision test |

The contacted body comes from a global read out of the matched operand at `+0x07` rather than
written down, and is refused if it does not land inside the image. Fields read, all confirmed by the
diagnostics character census first: `body+0xA0` the owner, `character+0x98` the movement mode,
`character+0xDC` the velocity.

## Four earlier attempts, and why they failed

Recorded because each looked right from the disassembly, each cost a play session, and the next
person reading this will be tempted by at least one of them again.

| attempt | how it died |
|---|---|
| gravity settling her onto a wrongly chosen floor | the steps were exactly one sixteenth every time and never accelerated, and paused for seconds while the player stood beside her. Gravity does none of that |
| a refused move never clearing her downward velocity, so it accumulated | her velocity reads zero while she stands still and spikes only on the steps she moves, so it is an impulse, not something retained |
| the swept collision test raising its ray origin by a step-over allowance, hiding a small descent | **built, shipped to a test install, and changed nothing.** The instrument added to find out why is what found the real cause |
| clearing the velocity of every collision exempt character | **built, shipped, and it froze the ships, the birds and the droids on flying platforms.** Exempt means the engine will not test it, not that it never moves |

The third is the one that taught the method. The census put in to explain the failure reported: in four
thousand sweeps, six were descending, all six were the **player** landing, and not one carried the
allowance the character move test passes. Her move never reaches that function at all.

The fourth is the one that taught the caution: a repair can pass every test, be accepted in play,
and still be wrong about a population nobody thought to look at.

The lesson worth keeping: counting what a hook actually sees is worth more than reasoning about
what it should see.

## Testing status

**Accepted in the game, on both counts.** Bumped and jumped on in the level where the fault was
reported: the character held her position for the whole run and no character in the level changed
height at all, against a measured `0.875` unit descent in fourteen steps before the fix. In the same
session the ships, birds and droids on flying platforms all moved normally, which is the check the
previous attempt failed.

The contact impulse is still visible in the census at the moment it is applied, so the push still
happens and simply goes nowhere. That is the intended behaviour rather than the impulse being
suppressed.

20 checks cover the decision, using the modes read out of the live level rather than invented ones.
**The test for the regression comes before the test for the bug**, because that is the failure that
actually reached a player: a character whose velocity is unchanged either side of the handler is
left alone whatever its mode.

**What has not been tested** is a whole game. The risk this design still carries is a contact that
is supposed to move something the engine does not test. If something that used to be knocked about
by walking into it now refuses to budge, that is this fix overreaching and worth reporting.
