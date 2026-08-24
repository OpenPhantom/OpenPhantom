/* projectile_cleanup_fix.h: a safety net for the engine's own generic ballistic-physics list, so a
 * settled entry that never gets cleaned up cannot accumulate forever.
 *
 * FUN_004524b9 (0x004524b9), decompiled during the field-report investigation this fix closes out:
 * every entry on the list is removed one of two ways - an external "please clean me up" flag (bit
 * 0x40000000 of the entry's own flags word), or immediately on its first hit, but ONLY for an entry
 * that does not bounce/persist on impact (flag bit 0x2 clear). An entry that DOES persist on impact
 * - sparks, debris, anything that scatters rather than vanishing on the spot - takes neither path:
 * on impact it settles (velocity zeroed) and then simply continues to exist, still running its own
 * full per-frame world-collision trace, for as long as whatever external code is supposed to set
 * that removal flag keeps failing to do so.
 *
 * Measured live: on a static floor this is invisible, presumably because that external check is
 * something like "has this object's position been unchanged for N seconds" and a settled object on
 * unmoving ground satisfies it quickly. On the two lift platforms this field report is about, the
 * platform itself keeps moving, so an object resting ON it never stops changing world position tick
 * to tick - the external check most likely never fires, and entries were measured to accumulate for
 * the length of an entire fight (three kills, dozens of entries, one capture) rather than being
 * cleaned up within a second or two the way the same combat produces everywhere else in the level.
 *
 * THE FIX. Rather than find and repair whatever the external check actually is, this sets the SAME
 * removal flag the engine's own update loop already watches for, directly, on any list entry that
 * is both old enough that no legitimate short-lived effect would still be alive (a blaster bolt or
 * a spark has never been observed to need more than a couple of seconds) and within range of one of
 * the five confirmed placements this field report is about. The engine's own removal code - proven
 * correct, already running, never touched - does the rest on its very next pass over the list.
 * Nothing here reads the "does it bounce" bit or the "has it settled" state, because age plus
 * position already tell the whole story without needing either: nothing legitimate near these five
 * placements should still be alive past the age threshold, however it got there. */
#ifndef PROJECTILE_CLEANUP_FIX_H
#define PROJECTILE_CLEANUP_FIX_H

#include <stdbool.h>

void projectile_cleanup_fix_install(bool enabled);

#endif /* PROJECTILE_CLEANUP_FIX_H */
