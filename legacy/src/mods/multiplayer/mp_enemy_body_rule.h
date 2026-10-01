/* mp_enemy_body_rule.h: how an enemy's body ends, as one word, and when a client writes it.
 *
 * Layer 1, pure. The body field of the enemy record carries what the host's body is at its end
 * and what the replica cannot work out for itself, because it runs none of the arms that change
 * it: whether it is drawn, whether it collides, whether it casts a shadow, its alpha and how far
 * it has dissolved. The fourth byte counts how often the base clip has started, so that a clip the
 * host begins again reaches the replica although its ordinal stays the same.
 *
 *   byte 0   bit 0 the host read a body, bit 1 drawn, bit 2 solid, bit 3 a shadow
 *   byte 1   alpha, 0 to 255 over 0 to 1, 255 opaque
 *   byte 2   dissolve, 0 to 255 over 0 to 1
 *   byte 3   the starts of the base clip, counted in its low four bits
 *
 * The write rule. A client writes a part of the body only when the host's value of that part has
 * changed against the one it last acted on, and on the first write of a life on a body. The one
 * local writer that can disagree with the host is the replica's own death clip, whose parameter
 * event takes the class away the moment the replica's playhead crosses it; the replica runs a
 * little behind the host, so the two agree within that delay, and a rule that wrote on every
 * record would stand the body up again in between. A part whose host value changed and which the
 * body already has is counted, not written.
 */
#ifndef MULTIPLAYER_MP_ENEMY_BODY_RULE_H
#define MULTIPLAYER_MP_ENEMY_BODY_RULE_H

#include "mp_enemy_wire.h"

#include <stdbool.h>
#include <stdint.h>

/* Byte 0. DRAWN and SOLID are the two bits the interest rule watches (mp_enemy_wire.h). */
#define MP_ENEMY_BODY_HAS          0x01u
#define MP_ENEMY_BODY_HAS_SHADOW   0x08u

#define MP_ENEMY_BODY_ALPHA_SHIFT     8u
#define MP_ENEMY_BODY_DISSOLVE_SHIFT  16u
#define MP_ENEMY_BODY_STARTS_SHIFT    24u
#define MP_ENEMY_BODY_STARTS_MASK     0x0Fu

/* The engine's own starting values: a thing is created opaque and not dissolving. A value that is
 * not a number is read as these. */
#define MP_ENEMY_BODY_OPAQUE       255u
#define MP_ENEMY_BODY_WHOLE        0u

typedef struct mp_enemy_body_state {
    bool    has;
    bool    drawn;
    bool    solid;
    bool    shadow;
    uint8_t alpha;
    uint8_t dissolve;
    uint8_t starts;    /* 0 to 15 */
} mp_enemy_body_state_t;

uint32_t mp_enemy_body_pack(const mp_enemy_body_state_t *state);
void     mp_enemy_body_unpack(uint32_t field, mp_enemy_body_state_t *out);

/* A level of 0 to 1 as a byte, rounded, and a byte as its level. A value outside 0 to 1 is
 * clamped; one that is not a number reads as `otherwise`. */
uint8_t mp_enemy_body_quantise(float level, uint8_t otherwise);
float   mp_enemy_body_level(uint8_t quantised);

/* The parts of the body a client writes, one bit each. */
#define MP_ENEMY_BODY_PART_DRAWN    0x01u
#define MP_ENEMY_BODY_PART_SOLID    0x02u
#define MP_ENEMY_BODY_PART_SHADOW   0x04u
#define MP_ENEMY_BODY_PART_ALPHA    0x08u
#define MP_ENEMY_BODY_PART_DISSOLVE 0x10u
#define MP_ENEMY_BODY_PARTS         5u

/* The parts where two states differ. */
uint32_t mp_enemy_body_differ(const mp_enemy_body_state_t *a, const mp_enemy_body_state_t *b);

/* The decision for one record, as three sets of parts:
 *
 *   write    the host's value changed (or this is a first write) and the body has another one
 *   already  the host's value changed (or first write) and the body already has it
 *   left     the host's value stood still and the body has another one, which is the replica's own
 *            death clip ahead of the host, or a value nobody has told it about; left alone
 *
 * `acted` is the host's value the client last acted on, NULL for a first write. */
typedef struct mp_enemy_body_verdict {
    uint32_t write;
    uint32_t already;
    uint32_t left;
} mp_enemy_body_verdict_t;

mp_enemy_body_verdict_t mp_enemy_body_decide(const mp_enemy_body_state_t *host,
                                             const mp_enemy_body_state_t *acted,
                                             const mp_enemy_body_state_t *local);

/* The class a solid body gets: its own shooter class, which is the one value the engine ever puts
 * back, at the spawn and at a savegame's load; and 0 for a body that does not collide. */
int32_t mp_enemy_body_class_for(bool solid, int32_t shooter_class);

/* A flag word with the drawn bit and the shadow bit set as `state` says, for the parts in
 * `parts`; every other bit as it was, because they belong to other writers. */
uint32_t mp_enemy_body_flags_with(uint32_t flags, const mp_enemy_body_state_t *state,
                                  uint32_t parts);

/* Whether the base track the host reads is a start of a clip since the last read: another slot,
 * another keyframe, or, on a clip that does not wrap, a head more than a frame behind the last
 * one. A clip that loops comes round on its own, and a restart of it in the same slot is not told
 * from a wrap. */
bool mp_enemy_body_started(int32_t slot_before, uint32_t keyframe_before, float head_before,
                           int32_t slot, uint32_t keyframe, float head, bool loops);

#endif /* MULTIPLAYER_MP_ENEMY_BODY_RULE_H */
