/* mp_starter_rule.h: which clip an engine starter plays on the overlay channel, and whether an
 * actor carries it, pure.
 *
 * Three engine functions the puppet calls choose a clip themselves and play it through the overlay
 * player: the weapon setter plays one of four draw clips, the force push starter plays 0x71, and
 * the swing starter plays 0x55 for the one row of its table that names that clip. The overlay
 * player tests the ordinal against the actor's clip count only through an assert, which ends the
 * process, and the test is one too far: an ordinal equal to the count passes and the table is read
 * one entry past its end. So the puppet works out which clip a starter is about to play and asks
 * whether the body's actor carries it before it calls the starter. These are the numbers; the
 * reads of the actor are in mp_puppet_starter.c.
 */
#ifndef MULTIPLAYER_MP_STARTER_RULE_H
#define MULTIPLAYER_MP_STARTER_RULE_H

#include <stdbool.h>
#include <stdint.h>

/* The clips the three starters play on the overlay channel. */
#define MP_STARTER_CLIP_DRAW_SABRE 0x20u   /* the sabre is asked for */
#define MP_STARTER_CLIP_SWAP       0x21u   /* one weapon for another */
#define MP_STARTER_CLIP_FROM_NONE  0x22u   /* a weapon out of empty hands */
#define MP_STARTER_CLIP_HOLSTER    0x23u   /* back to empty hands */
#define MP_STARTER_CLIP_MIDAIR     0x55u   /* the swing row that names this clip plays it here */
#define MP_STARTER_CLIP_PUSH       0x71u

#define MP_STARTER_SLOT_EMPTY 0u
#define MP_STARTER_SLOT_SABRE 1u

/* The weapon table has twelve rows: empty hands, the sabre and ten guns. The equip commit indexes
 * it with the slot and no bound, and the row after the last is the first row of the swing table,
 * whose node name the node lookup refuses with an assert. */
#define MP_STARTER_WEAPON_ROWS 12u

/* The row of the shipped swing table that names MP_STARTER_CLIP_MIDAIR. */
#define MP_STARTER_MIDAIR_ROW 24u

typedef enum mp_starter_verdict {
    MP_STARTER_GO,        /* the starter may be called */
    MP_STARTER_NOT_YET,   /* the actor did not read: wait as for a busy track, never call */
    MP_STARTER_NEVER,     /* the actor lacks the clip or the slot has no row: do not call */
    MP_STARTER_SET_ONLY   /* a worn body without the draw clip: write the slot, play no clip */
} mp_starter_verdict_t;

/* The clip the weapon setter plays in its raw mode for a body holding `equipped` that is asked for
 * `wanted`. Asking for the slot already held puts the weapon away. False when it plays nothing,
 * which is empty hands asked for empty hands. */
bool mp_starter_weapon_clip(uint32_t equipped, uint32_t wanted, uint32_t *clip);

/* Whether `wanted` names a row of the weapon table. */
bool mp_starter_weapon_slot_ok(uint32_t wanted);

/* The clip a swing row plays on the overlay channel: the row's own clip when that is
 * MP_STARTER_CLIP_MIDAIR, and nothing otherwise, because every other row plays on the base
 * channel, whose player refuses a missing clip by itself. `clip` may be NULL. */
bool mp_starter_swing_overlay_clip(uint32_t row_clip, uint32_t *clip);

/* Whether an actor carrying `count` clips has clip `clip`: strictly below the count, and a count
 * the engine's signed compare reads as negative has none. */
bool mp_starter_clip_fits(uint32_t clip, uint32_t count);

#endif /* MULTIPLAYER_MP_STARTER_RULE_H */
