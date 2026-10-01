/* mp_enemy_block_rule.h: the decisions behind an NPC's blade clang on the machine that watches.
 *
 * Layer 1, pure. Two of them, and each is a question the engine half would otherwise answer by
 * hand at its call site.
 *
 * Whether the engine played the clang at all. The clang is rationed by one cooldown cell for every
 * actor in the level: the engine compares it against the world clock, and only a call it lets
 * through plays the sound, throws the spark and writes the cell again, to the clock plus 0.2 s. A
 * call it holds writes nothing. So the cell read before and after the call is the verdict, and the
 * engine is asked nothing twice.
 *
 * How the kind travels. The engine has three, a ricochet, blade on blade and blade on armour, and
 * they ride the low byte of the world event offset by one, as they rode the record's field before,
 * so a low byte of 0 is still no clang.
 */
#ifndef MULTIPLAYER_MP_ENEMY_BLOCK_RULE_H
#define MULTIPLAYER_MP_ENEMY_BLOCK_RULE_H

#include <stdbool.h>
#include <stdint.h>

/* The largest kind the engine passes. */
#define MP_ENEMY_BLOCK_MAX_KIND 2u

typedef enum mp_enemy_block_gate {
    MP_ENEMY_BLOCK_UNREAD = 0,   /* the cell did not resolve or did not read: no verdict */
    MP_ENEMY_BLOCK_HELD,         /* the cooldown held the call, and nothing was played */
    MP_ENEMY_BLOCK_PASSED        /* the cooldown let it through: sound, spark and flash */
} mp_enemy_block_gate_t;

/* The verdict from the cell's bits before and after one call. Any change is a pass: the engine
 * writes the cell only on the path that plays, and the clock plus 0.2 s never rounds back to the
 * value that let the call through. */
mp_enemy_block_gate_t mp_enemy_block_gate(bool read_before, uint32_t before, bool read_after,
                                          uint32_t after);

/* The kind into the field's low byte and back. Packing answers 0 for a kind the engine never
 * passes; unpacking answers false for a low byte of 0. */
uint32_t mp_enemy_block_pack(int32_t kind);
bool     mp_enemy_block_unpack(uint32_t field, int32_t *kind);

#endif /* MULTIPLAYER_MP_ENEMY_BLOCK_RULE_H */
