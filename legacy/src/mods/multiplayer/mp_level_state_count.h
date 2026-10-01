/* mp_level_state_count.h: every entry into a level switch arm, counted by where it happened.
 *
 * Layer 2. It changes nothing; the hulls in mp_level_switch.c decide and this counts what they
 * decided, apart by the question the design turns on: did the call come from a client of a started
 * session, and from an actor that machine had parked, or from one its own scan woke and nobody
 * parked. Kept apart as well by placement and kind, because the one line per kind could never say
 * which actor made a thousand calls, and the first eight calls of each side are written out with
 * what the host's table, the enemy block and the hand back said about that actor at the time.
 */
#ifndef MULTIPLAYER_MP_LEVEL_STATE_COUNT_H
#define MULTIPLAYER_MP_LEVEL_STATE_COUNT_H

#include <stdbool.h>
#include <stdint.h>

typedef enum mp_level_kind {
    MP_LEVEL_KIND_EMITTER = 0,   /* emitter_set, opcode 0x212 */
    MP_LEVEL_KIND_LIGHT,         /* light_set, opcodes 0x207, 0x208 and 0x210 */
    MP_LEVEL_KIND_SOUND,         /* sound_set, opcode 0x213 */
    MP_LEVEL_KIND_COUNT
} mp_level_kind_t;

/* How one entry was decided. */
typedef enum mp_level_verdict {
    MP_LEVEL_VERDICT_WITHOUT = 0,   /* no started session with this side a client: let through */
    MP_LEVEL_VERDICT_WITHHELD,      /* a client of a started session that can match: refused */
    MP_LEVEL_VERDICT_FAIL_OPEN      /* such a client that cannot match: let through */
} mp_level_verdict_t;

/* `host` says this side is the host of a started session, whose line names no client question. */
void mp_level_state_count_switch(mp_level_kind_t kind, uintptr_t actor, int32_t mode,
                                 mp_level_verdict_t verdict, bool host);

/* The line per kind, and the placements with an entry. `host` only names the side. */
void mp_level_state_count_report(bool host);

#endif /* MULTIPLAYER_MP_LEVEL_STATE_COUNT_H */
