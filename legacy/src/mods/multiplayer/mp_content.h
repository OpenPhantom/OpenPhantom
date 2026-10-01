/* mp_content.h: the character roster as one number, the data file two machines must share.
 *
 * This file used to hash the mod set as well: for eight mods whether the DLL was loaded and their
 * whole ini section. That refused a friend for a frame rate, a draw distance or a detail level,
 * none of which a client runs for the host any more, since a client parks its own activation scan
 * and replays the host's world. What two machines must share is now judged at the join by
 * mp_mod_manifest_rule: the damage table, this roster, and the multiplayer's own build. The roster
 * stays here because it is read out of the roster module and is a data file, not a setting.
 */
#ifndef MULTIPLAYER_MP_CONTENT_H
#define MULTIPLAYER_MP_CONTENT_H

#include <stdint.h>

/* The character roster as one number: every profile's asset name and the clip ordinals it names,
 * in the order the roster holds them.
 *
 * It is a DATA FILE, and what it decides is heavier than any tuning value: `characters.ini` gives
 * an actor its clip ordinals by role, and a roster that differs between two machines makes the same
 * ordinal on the wire mean two different animations. The measured shape of that: of the 265
 * shipped actors exactly one agrees with the player's own asset ordinal for ordinal, only two
 * carry 114 clips or more, 237 carry fewer than 18, and 261 of them agree on none of the five
 * locomotion ordinals.
 *
 * The ORDER is part of the hash and that is deliberate. The roster's index is used as an identity
 * by the panels that switch a character, so two files with the same lines in a different order are
 * NOT the same roster.
 *
 * Zero when the roster is empty, which is a game with no `characters.ini` beside it: nothing to
 * compare, and the judge does not compare a zero. */
uint32_t mp_content_roster_fingerprint(void);

/* The roster's line in the report. */
void mp_content_report(void);

#endif /* MULTIPLAYER_MP_CONTENT_H */
