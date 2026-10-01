/* mp_enemy_shield_rule.h: a droideka's shield as sixteen bits of its record, and the director
 * commands that bring a replica to it.
 *
 * Layer 1, pure.
 *
 * The shield is a sphere the director hangs on a body: command 2 hangs one, in one of five colours
 * or the default green, command 3 takes it off, command 4 shows or hides it and command 5 sizes it.
 * What a watcher sees is where those commands left it, so it travels as STATE in the record, read
 * on the host out of the body and the shield's own pool slot, and a replica is brought to it
 * through the same four commands only where it differs. A replica runs no script, so without this
 * it never shows one.
 *
 * The low byte: bit 0 one hangs, bit 1 it is shown, bits 2 to 4 its colour, bit 5 the sender could
 * read the body at all. A record without bit 5 says nothing, and a replica keeps what it has: a
 * host that cannot read shields must not take one off a replica that shows one. The high byte is
 * the radius in eighths of a unit, 0 for none said. Every radius a shipped script sets is 1.0 to
 * 2.0 and a hang gives 0.5; 1.9 is 1.875 in eighths, so the two sides compare radii in eighths and
 * never in floats, or a client would size the shield again on every record.
 */
#ifndef MULTIPLAYER_MP_ENEMY_SHIELD_RULE_H
#define MULTIPLAYER_MP_ENEMY_SHIELD_RULE_H

#include <stdbool.h>
#include <stdint.h>

#define MP_ENEMY_SHIELD_HANGS        0x01u
#define MP_ENEMY_SHIELD_SHOWN        0x02u
#define MP_ENEMY_SHIELD_COLOUR_SHIFT 2u
#define MP_ENEMY_SHIELD_COLOUR_MASK  0x1Cu
#define MP_ENEMY_SHIELD_READ         0x20u
#define MP_ENEMY_SHIELD_RADIUS_SHIFT 8u
#define MP_ENEMY_SHIELD_RADIUS_MASK  0xFF00u
#define MP_ENEMY_SHIELD_RADIUS_STEPS 8.0f

/* The director's five colours, by the operand of command 2: red, blue, violet, yellow, cyan. Any
 * other operand leaves the pool's default green, and so does 0 here. */
#define MP_ENEMY_SHIELD_COLOURS 5u

typedef struct mp_enemy_shield {
    bool     read;     /* the rest was read */
    bool     hangs;
    bool     shown;
    uint32_t colour;   /* 1..5, or 0 for the default and any colour not of the five */
    uint32_t radius;   /* in eighths, 1..255, or 0 for none said */
} mp_enemy_shield_t;

uint32_t          mp_enemy_shield_pack(const mp_enemy_shield_t *shield);
mp_enemy_shield_t mp_enemy_shield_unpack(uint32_t field);

/* The operand of command 2 that paints these three bytes, or 0. */
uint32_t mp_enemy_shield_colour(uint8_t red, uint8_t green, uint8_t blue);

/* A radius in eighths, rounded, from 1 for anything above nought to 255 for anything past 31.9;
 * 0 for a radius that is not a positive number. And the radius eighths stand for. */
uint32_t mp_enemy_shield_eighths(float radius);
float    mp_enemy_shield_radius_of(uint32_t eighths);

/* A replica's shield as the director sees it. */
typedef struct mp_enemy_shield_here {
    int32_t  id;       /* the body's shield word, below 0 for none */
    bool     in_use;   /* the pool slot it names is in use, so a shield is drawn */
    bool     shown;
    uint32_t colour;
    uint32_t radius;   /* the slot's radius in eighths */
} mp_enemy_shield_here_t;

/* The commands, in the order they are given: take off, hang, show or hide, size. */
typedef struct mp_enemy_shield_plan {
    bool     release;    /* command 3 */
    bool     create;     /* command 2 with `colour` */
    uint32_t colour;
    bool     show;       /* command 4 with `shown` */
    bool     shown;
    bool     recolour;   /* the take off and the hang are for another colour */
    bool     size;       /* command 5 with `radius` in eighths */
    uint32_t radius;
} mp_enemy_shield_plan_t;

/* What brings `here` to `want`. Nothing for a record that says nothing. A body word that names a
 * slot no longer in use shows nothing, but the director will not hang a new shield over it, so it
 * is taken off first; command 3 does no more for such a word than set it back to none. A hung
 * shield is shown and sized 0.5, so hiding it is a command of its own, and so is its size after
 * every hang. */
mp_enemy_shield_plan_t mp_enemy_shield_plan(const mp_enemy_shield_t *want,
                                            const mp_enemy_shield_here_t *here);

/* Whether a replica already is what the host says. */
bool mp_enemy_shield_agrees(const mp_enemy_shield_t *want, const mp_enemy_shield_here_t *here);

#endif /* MULTIPLAYER_MP_ENEMY_SHIELD_RULE_H */
