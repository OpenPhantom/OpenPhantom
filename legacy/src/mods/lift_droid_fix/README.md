# lift_droid_fix

**Produces:** `lift_droid_fix.dll` -> `mods\`

A field report of a severe, reproducible frame-rate stall (down to single-digit fps) at two
specific lift/elevator platforms, each carrying a small cluster of droids. Traced to five known
placements and two independent engine bugs, both fixed here.

## Supported executables

Any build whose code matches the retail sites this resolves by pattern:
`FUN_00437850` (the actor-teardown function, `activation_race_fix.c`) and the
`FUN_004524b9` prologue block up to its own read of the ballistic-physics list head
(`projectile_cleanup_fix.c`). If either does not resolve, that half of the DLL changes nothing and
says so in the log; the two fixes are otherwise fully independent of each other.

## Configuration: `[lift_droid_fix]`

| Key | Default | Meaning |
|---|---|---|
| `Enabled` | `1` | Master switch. `0` installs neither fix. |
| `FixActivationRace` | `1` | See "Bug 1" below. |
| `FixProjectileCleanup` | `1` | See "Bug 2" below. |

## The five placements

Found live, via a player-position-and-nearby-placement capture, not guessed at - two earlier
guesses by loose name/position matching were both wrong, because a placement's name
(`enemy045`, `enemy092`, ...) is a reused archetype label rather than a per-placement ID; the same
name fired around twenty times over one level. Position is what actually distinguishes these five:

| Placement | Position | Lift |
|---|---|---|
| `enemy045` | (127.1, 80.5, 23.1) | first |
| `enemy046` | (126.5, 79.7, 23.1) | first |
| `enemy048` | (126.3, 80.9, 23.1) | first |
| `enemy076` | (122.4, 64.0, 23.0) | second |
| `enemy077` | (122.5, 64.5, 23.0) | second |

Full suppression (never letting any of the five spawn at all) was tried first and confirmed the
mechanism fully accounts for the field report: zero of the five present, zero frame drop, at either
lift. That is a bigger change than the bug calls for, so it was replaced with the two narrow fixes
below - both scoped to just these five placements, nothing else in the game.

## Bug 1: an activation/deactivation race (`FixActivationRace`)

`FUN_00437161` (the activation scan) creates an actor from a placement's **static** position and an
ACTIVATION radius (`placement+0x28`). Separately, `FUN_00432bf2`'s own per-tick tail destroys an
actor (reason 0) from the actor's **live** position and a different DEACTIVATION radius
(`placement+0x2C`). On level geometry that never moves the two tests never disagree. The two lift
platforms this fix is for spawn low and carry their riders upward once the player gets close, and
close enough underfoot the two tests can disagree fast enough to tear an actor down that
`FUN_00437850`'s own reason-0 branch immediately re-arms for creation - the actor is destroyed and
recreated every tick, forever, for as long as the disagreement holds.

**Two earlier suppression designs, tried and rejected:**

1. Re-check "is the player still within the activation radius" from inside the destroy hook, right
   before forwarding a reason-0 destroy. Field-tested at zero suppressions: this hook's own read of
   the player's position happens a handful of instructions after the engine's own read that just
   produced the deactivation verdict, with nothing moving in between, so re-running the identical
   test on the identical position can never disagree with an answer already computed.
2. A time-based grace period, applied to every placement: suppress a reason-0 destroy within a few
   ticks of a successful creation. Worked exactly as designed and was field-tested as making no
   difference to the frame rate - but measured against two placements a later, more careful capture
   proved were never actually part of either stalling encounter at all. That verdict was measured
   on the wrong target.

**Fix:** an independent, chained detour on `FUN_00437850` (`0x00437850`) refuses to forward
**only** a reason-0 destroy, and only for the five known placements. All five still spawn and fight
completely normally; every other reason, for every placement, known or not, reaches the engine
exactly as it always did, and a known placement genuinely killed still leaves the same way
(reason 1) it always has.

`view_distance_fix.dll`'s own `spawn_census.c` also detours this same function, for an unrelated
"destroying X, reason N" observation log. The two DLLs do not depend on each other; whichever loads
second chains onto the other's hook, per `common/detour.h`'s own contract.

## Bug 2: settled debris never gets cleaned up (`FixProjectileCleanup`)

Even with bug 1 fixed, fps still fell during a fight at either lift. `FUN_004524b9` (the engine's
own generic ballistic-physics list update loop - also home to live blaster bolts, per
`diagnostics.dll`'s own `Projectiles` census) removes a list entry one of two ways: an external
"please clean me up" flag (bit `0x40000000` of the entry's own flags word), or immediately on its
first hit, but **only** for an entry that does not bounce/persist on impact (flag bit `0x2` clear).
An entry that **does** persist on impact - sparks, debris, anything that scatters rather than
vanishing on the spot - takes neither path: on impact it settles and then simply continues to
exist, still running its own full per-frame world-collision trace, for as long as whatever external
code is supposed to set that removal flag keeps failing to do so.

Measured live: on a static floor this is invisible, presumably because that external check depends
on the entry's own position having been unchanged for some interval. On the two lift platforms this
fix is for, the platform itself keeps moving, so an object resting on it never stops changing world
position tick to tick - the external check most likely never fires, and entries were measured to
accumulate to 44-57 during a single fight instead of draining the way the same combat does
everywhere else in the level, directly correlating with the worst fps windows.

**Fix:** rather than find and repair whatever the external check actually is, this sets the same
removal flag the engine's own update loop already watches for, directly, on any list entry that is
both old enough (`STALE_AGE_SECONDS = 0.1f`) that no legitimate short-lived effect would still be
alive, and within 12 units of one of the five known placements. The engine's own removal code -
proven correct, already running, never touched - does the rest on its very next pass over the list.
Checked every frame (`CLEANUP_EVERY_FRAMES = 1u`); an earlier build checked every 5th frame and was
field-reported as making debris "vanish faster or slower" inconsistently - the coarse polling
interval was jitter comparable in magnitude to the 0.1s threshold itself, and checking every frame
removed it.

The list head (`DAT_00872fb8`) is resolved by signature - `FUN_004524b9` reads it as a literal
absolute operand at `0x004524e7`, reached by a `jnz` that converges two control-flow paths onto
that exact point, which is what the pattern anchors on. The wildcarded operand is read back and
range-checked against the host image rather than embedded as a fixed address.

## Field tuning history (`FixProjectileCleanup`)

`STALE_AGE_SECONDS` was tested at four values, `CLEANUP_EVERY_FRAMES` at two, in the field, before
landing on the current pair:

| `STALE_AGE_SECONDS` | `CLEANUP_EVERY_FRAMES` | Result |
|---|---|---|
| 4.0 | 5 | Safe, but never touched the fps peak - entries never aged out before the worst of a fight had already happened |
| 1.5 | 5 | Cut the peak list size (57 -> 44) and the recovery time (never -> ~8s), barely moved the worst fps (~10 -> ~12) |
| 0.5 | 5 | Worse than 0.1 by field report |
| 0.1 | 5 | Best fps measured to that point, but field-reported visual defect: debris "vanishing faster or slower" |
| **0.1** | **1** | Current. Worst fps in a long combat-heavy session: 41.9 (was 3-12). Peak list size: 9 (was 44-57). List consistently drained to 0 between fights. No further visual complaint. |

## Engine locations

| Site | Retail VA | What |
|---|---|---|
| `FUN_00437161` (activation scan) | `0x00437161` | not touched - view_distance_fix.dll resolves this site for its own NPC range scale, unrelated |
| `FUN_00432bf2` (per-actor tick, deactivation check) | inside this function | not touched - the source of the reason-0 destroy this fix refuses to forward |
| `FUN_00437850` (actor teardown) | `0x00437850` | independent chained detour; refuses one reason for five placements |
| `FUN_004524b9` (projectile/effect list tick) | `0x004524b9` | read only; supplies the list head address, never touched itself |
| `DAT_00872fb8` (projectile/effect list head) | resolved by signature | read every `CLEANUP_EVERY_FRAMES`; one flags-word bit force-set on qualifying entries |

## Testing status

Both fixes were built, field-tested and iteratively tuned live against the two reported stalls
before being extracted from `view_distance_fix.dll` into this DLL of their own. After extraction:
`cmake --build` and `ctest` both pass clean under `/W4 /WX`, and the signature this DLL now uses to
resolve the projectile list head (replacing a hardcoded address the pre-extraction version used) has
been checked by hand against the disassembly but **not yet re-verified against a live session** -
the mechanical behaviour should be identical to the pre-extraction, field-confirmed version, but a
person should confirm both `engine_fixes.log` shows both fixes arming cleanly and the two lifts no
longer stall before this is considered re-confirmed post-extraction.

The log lines to look for: `activation-race fix armed at`, `projectile cleanup fix armed at`, and
during play, `activation race fix: refused a reason-0 destroy` / `projectile cleanup fix: forced
removal of a`.

## Relationship to `view_distance_fix`

Both fixes used to live inside `view_distance_fix.dll` (`spawn_census.c` and
`projectile_cleanup_fix.c`) and were moved out into this DLL of their own, per this project's "one
DLL per independent fix" rule - neither mechanism has anything to do with view distance, fog or NPC
activation range, which is what that DLL is actually for, and neither depends on the other or on
that DLL at run time. `view_distance_fix.dll`'s own `spawn_census.c` still independently detours
`FUN_00437850` for an unrelated observation log; the two chain rather than conflict.
