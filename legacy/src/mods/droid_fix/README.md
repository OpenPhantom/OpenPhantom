# droid_fix

**Produces:** `droid_fix.dll` -> `mods\`

A field report of a severe, reproducible frame-rate stall (down to single-digit fps) at two
specific lift/elevator platforms, each carrying a small cluster of droids. Traced to five known
placements and two independent engine bugs. Named droid_fix rather than lift_droid_fix because only
one of the two is still lift-specific: bug 1 stays scoped to those five placements, a general,
game-wide version of its fix was tried and reverted, see "Bug 1" below. Bug 2 shipped scoped to the
same five placements too, on a plausible-but-unconfirmed assumption, and a later full playthrough
proved that assumption wrong: it now covers the whole level, not just those two lifts, see "Bug 2"
below.

## Supported executables

Any build whose code matches the retail sites this resolves by pattern:
`FUN_00437850` (the actor-teardown function, `activation_race_fix.c`) and the
`FUN_004524b9` prologue block up to its own read of the ballistic-physics list head
(`projectile_cleanup_fix.c`). If either does not resolve, that half of the DLL changes nothing and
says so in the log; the two fixes are otherwise fully independent of each other.

## Configuration: `[droid_fix]`

| Key | Default | Meaning |
|---|---|---|
| `Enabled` | `1` | Master switch. `0` installs neither fix. |
| `FixActivationRace` | `1` | See "Bug 1" below. |
| `FixProjectileCleanup` | `1` | See "Bug 2" below. Applies to persisting entries on the whole ballistic list, not just the five placements; ordinary projectiles are never touched. |

## The five placements

Found live, via a player-position-and-nearby-placement capture, not guessed at. Two earlier
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
below. Bug 1 stays scoped to just these five placements; bug 2 does not any more, see its own
section for why.

## Bug 1: an activation/deactivation race (`FixActivationRace`)

`FUN_00437161` (the activation scan) creates an actor from a placement's **static** position and an
ACTIVATION radius (`placement+0x28`). Separately, `FUN_00432bf2`'s own per-tick tail destroys an
actor (reason 0) from the actor's **live** position and a different DEACTIVATION radius
(`placement+0x2C`). On level geometry that never moves the two tests never disagree. The two lift
platforms this fix is for spawn low and carry their riders upward once the player gets close, and
close enough underfoot the two tests can disagree fast enough to tear an actor down that
`FUN_00437850`'s own reason-0 branch immediately re-arms for creation: the actor is destroyed and
recreated every tick, forever, for as long as the disagreement holds.

**Three earlier designs, tried and rejected:**

1. Re-check "is the player still within the activation radius" from inside the destroy hook, right
   before forwarding a reason-0 destroy. Field-tested at zero suppressions: this hook's own read of
   the player's position happens a handful of instructions after the engine's own read that just
   produced the deactivation verdict, with nothing moving in between, so re-running the identical
   test on the identical position can never disagree with an answer already computed.
2. A time-based grace period, applied to every placement: suppress a reason-0 destroy within a few
   ticks of a successful creation. Worked exactly as designed and was field-tested as making no
   difference to the frame rate, but measured against two placements a later, more careful capture
   proved were never actually part of either stalling encounter at all. That verdict was measured
   on the wrong target.
3. A general, mover-based version: the actor structure appeared, from decompiled evidence, to carry
   a persistent field (`actor+0x108`) identifying which mover an actor is currently riding, written
   by `FUN_00435c67`/`FUN_0040be00` ahead of the deactivation check every tick. Refusing the
   reason-0 destroy whenever that field was non-null, rather than matching against five known
   positions, would have covered any actor on any mover, anywhere in the game. It built and
   reviewed clean, but a played session that reached the known encounters logged 2,014 reason-0
   destroys for the five known placements and this design refused none of them. Reverted; whatever
   `actor+0x108` actually is, it did not behave as the decompile suggested for these actors in
   practice, and that question is left open for a future investigation rather than blocking a
   working build.

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
own generic ballistic-physics list update loop, also home to live blaster bolts, per
`diagnostics.dll`'s own `Projectiles` census) removes a list entry one of two ways: an external
"please clean me up" flag (bit `0x40000000` of the entry's own flags word), or immediately on its
first hit, but **only** for an entry that does not bounce/persist on impact (flag bit `0x2` clear).

**The actual mechanism**, found by a deeper investigation after this fix originally shipped: bit
`0x40000000` is set from exactly one place outside `FUN_004524b9`, `FUN_00454aa1` (`0x00454aa1`),
an event-driven "on contact" handler that only ever runs in response to a real collision. A
persisting entry that collides has a probabilistic "stick" outcome (a random roll at `0x00452c8c`
against a threshold at `0x004a8790`); on a stick, `FUN_004524b9` zeroes both the entry's velocity
and its acceleration and clears the persist bit. Since next-tick position is computed purely from
velocity and acceleration, zeroing both means the entry never moves again, so it never collides
again, so it never generates the contact event that is the only thing that can flag it for removal.
It goes permanently inert. **This is an event-starvation bug, not a position-staleness check.**
Nothing in the traced call graph compares a remembered position to a current one, and nothing there
treats a moving surface differently from a static one; an earlier version of this document guessed
at a position-unchanged-for-N-seconds check as the cause, which this later reading refutes. One
piece is still unresolved: a per-type callback `FUN_004524b9` also invokes every tick was not
traced for this specific debris type, and could play a further role.

Measured live: entries were observed to accumulate to 44-57 during a single fight at the two lift
platforms this field report is originally about, instead of draining within a second or two the way
the same combat does everywhere else in the level, directly correlating with the worst fps windows.

**Scope history.** Because the mechanism found is not moving-surface-specific in anything traced,
this was suspected from the start to be able to happen anywhere a bouncing effect settles, but it
first shipped scoped to just the five placements it was field-confirmed at, a deliberate choice to
not widen a fix on the strength of a plausible-but-unconfirmed generalisation. A later full
playthrough, with the fix's own logging on, refuted that caution directly: the list piled up to 89
entries at multiple points scattered across the whole level, correlating with 80 measured hitches
in one session, every one of them at a droid death nowhere near either lift, and therefore outside
what the position-scoped version could ever touch. The mechanism was never actually lift-specific;
the field report just hadn't been chased far enough yet to show it. The fix below now covers the
whole list.

**Fix:** rather than repair the starvation at its source, shared physics/collision code used by
every ballistic effect in the game, and not worth the risk without first resolving the callback
above, this sets the same removal flag `FUN_004524b9` already watches for, directly, on any list
entry that BOTH persists on impact (flag bit `0x2`, the same flag that is the whole reason an entry
can get stuck in the first place) AND is old enough (`STALE_AGE_SECONDS = 0.1f`) that no legitimate
settled debris would still be waiting for a collision that is never coming. No position check at
all any more, but the persist-bit check is load-bearing, not optional; see "the persist check is
not optional" just below. Checked every frame (`CLEANUP_EVERY_FRAMES = 1u`); an earlier build
checked every 5th frame and was field-reported as making debris "vanish faster or slower"
inconsistently: the coarse polling interval was jitter comparable in magnitude to the 0.1s
threshold itself, and checking every frame removed it.

**The persist check is not optional.** Widening the fix to the whole list was first tried with only
the age check, on the reasoning that "a blaster bolt has never been observed to need more than a
couple of seconds", so 0.1s seemed generous. That reasoning had never actually been tested against
a live bolt: the threshold was tuned entirely while the fix was ALSO gated by position, and a
flying bolt leaves a 12-unit radius in far less than 0.1s regardless of its own lifetime, so the
age check alone had never once been exercised against something still in flight. Without the
position gate, every projectile on the list, live blaster bolts included, is normally still there
and still flying well past 0.1s old, and the age-only version force-removed all of them a tenth of
a second after creation. Field-reported directly: enemies could no longer hit the player, because
their own shots vanished just past the muzzle. The persist-bit check restricts this fix to exactly
the entries that can actually get stuck, the category the field report was always about, and
leaves ordinary projectiles, already removed correctly on their own first hit, untouched regardless
of age.

The list head (`DAT_00872fb8`) is resolved by signature. `FUN_004524b9` reads it as a literal
absolute operand at `0x004524e7`, reached by a `jnz` that converges two control-flow paths onto
that exact point, which is what the pattern anchors on. The wildcarded operand is read back and
range-checked against the host image rather than embedded as a fixed address.

## Field tuning history (`FixProjectileCleanup`)

`STALE_AGE_SECONDS` was tested at four values, `CLEANUP_EVERY_FRAMES` at two, in the field, before
landing on the current pair:

| `STALE_AGE_SECONDS` | `CLEANUP_EVERY_FRAMES` | Result |
|---|---|---|
| 4.0 | 5 | Safe, but never touched the fps peak: entries never aged out before the worst of a fight had already happened |
| 1.5 | 5 | Cut the peak list size (57 -> 44) and the recovery time (never -> ~8s), barely moved the worst fps (~10 -> ~12) |
| 0.5 | 5 | Worse than 0.1 by field report |
| 0.1 | 5 | Best fps measured to that point, but field-reported visual defect: debris "vanishing faster or slower" |
| **0.1** | **1** | Current. Worst fps in a long combat-heavy session: 41.9 (was 3-12). Peak list size: 9 (was 44-57). List consistently drained to 0 between fights. No further visual complaint. |

## Engine locations

| Site | Retail VA | What |
|---|---|---|
| `FUN_00437161` (activation scan) | `0x00437161` | not touched; view_distance_fix.dll resolves this site for its own NPC range scale, unrelated |
| `FUN_00432bf2` (per-actor tick, deactivation check) | inside this function | not touched; the source of the reason-0 destroy this fix refuses to forward |
| `actor+0x108` (candidate "current mover" field) | offset, not an address | investigated, not used; see design 3 above, reverted after a played session showed it not behaving as the decompile suggested |
| `FUN_00437850` (actor teardown) | `0x00437850` | independent chained detour; refuses one reason for five placements |
| `FUN_004524b9` (projectile/effect list tick) | `0x004524b9` | read only; supplies the list head address, never touched itself |
| `FUN_00454aa1` (on-contact event handler) | `0x00454aa1` | read only; the sole external setter of the removal flag `FUN_004524b9` watches for, never touched itself |
| `DAT_00872fb8` (projectile/effect list head) | resolved by signature | read every `CLEANUP_EVERY_FRAMES`; one flags-word bit force-set on qualifying entries |
| entry `+0x84` bit `0x2` (persist/bounce flag) | offset, not an address | read only; restricts cleanup to entries that can actually get stuck, never touched itself |

## Testing status

Both fixes were built, field-tested and iteratively tuned live against the two reported stalls
before being extracted from `view_distance_fix.dll` into this DLL of their own, and confirmed again
after extraction: `engine_fixes.log` showed both sites resolving, both fixes arming, and both
mechanisms firing during play against the known placements (`activation race fix: refused a
reason-0 destroy of "enemy076"/"enemy077"` climbing across a session, `projectile cleanup fix:
forced removal of a` firing 55 times in one session). The user confirmed the stall stays fixed.

A general, mover-based replacement for bug 1's fix (design 3 in "Bug 1" above) was subsequently
built, reviewed clean, and shipped, but a played session showed it refusing zero of 2,014 reason-0
destroys it should have caught for the five known placements; it was reverted the same session, back
to the version described above. The mechanism explanation for bug 2 was also corrected afterward
(see "the actual mechanism" in "Bug 2" above).

A full playthrough later, with `lift_droid_fix` enabled the whole time, still showed the fps stall,
both at a known lift and separately near a droid death in open space nowhere near either lift. A
controlled isolation (toggling `lift_droid_fix`, `controller_input` and `xidi_bridge` independently
across several short sessions) confirmed the stall tracked `lift_droid_fix` alone, on and off,
regardless of which of those three, if any, was providing controller input; on the session where
`lift_droid_fix` was off, the projectile list was directly observed piling up to 89 entries at
points scattered across the whole level. Bug 2's fix was widened from the five-placement version to
a whole-list, age-only version as a direct result.

**That age-only version was itself immediately field-reported broken**, in the very next play
session: enemy blaster fire stopped reaching the player, vanishing just past the muzzle. Root
cause: the 0.1s age threshold had only ever been safe because the position gate it was tuned
alongside already excluded every live, flying projectile (a bolt leaves a 12-unit radius in far
less than 0.1s), so the age check alone had never actually been exercised against a live shot until
the position gate was removed. Fixed by restricting cleanup to entries with the persist/bounce flag
set, the same flag that is the actual reason an entry can get stuck, which ordinary blaster bolts
never carry; see "the persist check is not optional" in Bug 2 above.

**This persist-flag fix has since been played and confirmed working.** A full playthrough of the
first level, both fixes enabled the whole time, measured 42 small hitches over five minutes, 396
successful debris cleanups, and a ballistic-list size that stayed capped at 8-21 entries throughout
rather than piling up; no further report of enemy fire vanishing. The user's own words: "plays
beautifully."

The log lines to look for: `activation-race fix armed at`, `projectile cleanup fix armed at`, and
during play, `activation race fix: refused a reason-0 destroy` / `projectile cleanup fix: forced
removal of a`.

## Relationship to `view_distance_fix`

Both fixes used to live inside `view_distance_fix.dll` (`spawn_census.c` and
`projectile_cleanup_fix.c`) and were moved out into this DLL of their own, per this project's "one
DLL per independent fix" rule. Neither mechanism has anything to do with view distance, fog or NPC
activation range, which is what that DLL is actually for, and neither depends on the other or on
that DLL at run time. `view_distance_fix.dll`'s own `spawn_census.c` still independently detours
`FUN_00437850` for an unrelated observation log; the two chain rather than conflict.
