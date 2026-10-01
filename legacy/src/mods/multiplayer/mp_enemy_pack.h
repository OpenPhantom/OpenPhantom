/* mp_enemy_pack.h: putting something that may not be there into a delta coded field.
 *
 * Layer 1, pure.
 *
 * An enemy record is compared against a baseline and only the fields that DIFFER travel. The
 * baseline of a record nobody has described yet is zeros, so a field whose value is zero is
 * absent from the change mask and never reaches the other machine. Any field that carries an
 * optional number therefore cannot use zero for one of its real values.
 *
 * So the value is offset by one and zero means nothing is there. Both fields that need it use
 * this, rather than each spelling the offset out, because an offset written twice is two chances
 * to get it wrong in one direction only.
 */
#ifndef MULTIPLAYER_MP_ENEMY_PACK_H
#define MULTIPLAYER_MP_ENEMY_PACK_H

#include <stdbool.h>
#include <stdint.h>

/* One number. Answers 0, which reads back as nothing, for a value the field cannot hold. */
uint32_t mp_enemy_pack_one(uint32_t value, uint32_t max);
bool     mp_enemy_unpack_one(uint32_t packed, uint32_t *value);

/* Two numbers in one field: the offset one in the low byte and a plain one in the high byte.
 * Only the low one decides whether anything is there, because only it is offset. */
uint32_t mp_enemy_pack_pair(uint32_t low, uint32_t high, uint32_t low_max, uint32_t high_max);
bool     mp_enemy_unpack_pair(uint32_t packed, uint32_t *low, uint32_t *high);

#endif /* MULTIPLAYER_MP_ENEMY_PACK_H */
