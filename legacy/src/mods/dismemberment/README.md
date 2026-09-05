# dismemberment

**Produces:** `dismemberment.dll` -> `mods\`

Lightsaber dismemberment: the node the blade actually hit, and on the killing blow.

## Supported executables

Retail `WMAIN.EXE` (EN/DE) and the Fix Pack build. On `obi.exe` the patterns do not resolve.

## Configuration: `[dismemberment]`

| Key | Default | Range | Meaning |
|---|---|---|---|
| `Mode` | `2` | 0-2 | 0 off, 1 correct the node only, 2 also sever on the killing blow |
| `SpinScale` | `0.35` | 0-2 | the tumble of the flying piece |
| `GravityScale` | `0.40` | 0.1-2 | its gravity |
| `YawScale` | `0.12` | 0-2 | the 90 degree per substep yaw kick in the flight arm |
| `SettleSeconds` | `1.20` | 0-5 | after this the piece lies still for good |
| `SettleDamping` | `0.80` | 0.1-1 | tumble damping per substep before that |
| `Diagnostics` | `0` | | record the full state of every flying piece |

## Engine locations

| Site | Retail VA | What |
|---|---|---|
| the node probe call | `0x43386E + 0x0B` | redirected; the original still runs |
| `enemy_detachPiece` | probe + `0x3D` | resolved, not patched |
| the death gate | `0x43707D` | detoured, naked, 6-byte prologue |
| the `hideMeshesBelow` call | `0x41441D + 0x19` | redirected through a translating thunk |
| `candy_stuntTick` | `0x42F64C` | detoured, 6-byte prologue |
| `candy_stuntOnContact` | `0x42FB2D` | detoured, 6-byte prologue |
| four tumble constants, gravity, the yaw kick | in `.data` | scaled directly; readers only in the flight code |

## The two defects

**The wrong node.** The probe uses the attacker's *feet* and the attacker's whole body cylinder, so
it picks the node nearest the attacker's body axis, not the one the blade touched. On a head strike
the leg flies off. The right value is already in the message mailbox, written from the blade sphere
nine bytes before the handler call.

**A type error.** `bapobj_detachNode` passes a NODE ordinal where `bapobj_hideMeshesBelow` compares
a MESH index. Measured over 265 actor models and 2986 severable nodes: the right piece **1.8 %**, a
foreign subtree **87.9 %**, nothing at all **10.3 %**. That is both user complaints in one finding.
The shortcut `meshIdx = nodeIndex - 1` holds in only 78.8 % of cases, so the mesh index is **read**,
not computed.

**The authored gate is not touched.** `stateFlags & 4` is carried by seven of 2250 ENMY records and
no opcode can set it, but opening it would also double every saber hit against every NPC, because
the damage doubling lives in the same branch. Mode 2 acts at the *death* gate instead, which is
reached only when health has already fallen to zero.

## The six safety gates

Mailbox discriminator (1,0), identity of attacker and victim, the node bound (**mandatory**,
`bapobj_detachNode` checks nothing, and an out-of-range index has roughly a 50 % chance of instant
death through `partIo`), not already severed, the authored body-part mask, with the engine's own
`part > 4` as a documented fallback for the 255 rigs that carry no mask, and a mesh must exist
somewhere in the node's subtree.

## Why the piece kept spinning: and where the fault really was

Measured, not asserted: `spin` is exactly zero from 0.5 s on and `rot` is literally constant over
seconds, with 112 of 120 samples in the damping arm. **The piece lies still in the simulation.**

The cause is in `bapobj_drawAll`: the drawn attitude is interpolated between `prevRot` and `rot`,
and `candy_stuntTick` maintains `prevPos` by hand but `prevRot` **never**. So `prevRot` stays at its
creation value while `rot` runs to -29 degrees, and the drawn yaw saws 32 times a second. **That is a
defect of the original engine**; this DLL maintains the field the way the engine already maintains
`prevPos`.

Three earlier diagnoses, the contact re-roll, the undamped arm, the distance-inverse impulse, were
each byte-correct descriptions and **none** was the cause. The rule that came out of it: a byte path
that *could* produce the symptom is not a cause; only a measurement that sees it entered is.

## Known limitations

* The throw **direction** is not fixed. For a high node the direction vector points down and the
  upward component flips negative. Fixing it means inserting code behind the local-to-world
  rotation, a separate change needing its own review.
* Without the contact hook the rest state is ineffective, because the tick damps while the contact
  re-rolls. A failure there is logged loudly rather than silently accepted.
* At most 12 pieces are tracked for `prevRot` maintenance and 3 for the diagnostics; beyond that the
  surplus is equalised (a hard frame, never the sawtooth) or stays silent.

## Testing status

Built and linked, `/W4 /WX` clean. Offline verification passes on both retail builds.
**Accepted in game**, in the 1.5.0 build, which was played through by hand.

To re-check the `prevRot` fix after any change here: decapitate an enemy and watch without
moving. The piece must fall, tumble briefly, and then **really** lie still. Then set
`Diagnostics=0`.
