/* mp_death.h: a death on the wire, and nothing else.
 *
 * Why it is its own file. The three functions below used to live in mp_hit_relay.c, which is an
 * engine binding: it hooks the damage path, reads player records out of resolved cells and knows
 * what a bank slot is. That is the right place for the half that NOTICES a death, and the wrong
 * place for the half that puts one in four bytes, because a machine with no engine has every
 * reason to read a death and no way to link that file.
 *
 * The dedicated server is that machine. It keeps the score of a deathmatch it does not simulate,
 * out of the deaths its players report to each other, and until this file existed it could not
 * read one.
 *
 * mp_hit_relay_death.h includes this and mp_hit_relay.h includes that, so nothing that already
 * used these names had to change.
 *
 * SIZE NOTE: under 80 lines, no seam.
 */
#ifndef MULTIPLAYER_MP_DEATH_H
#define MULTIPLAYER_MP_DEATH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Four bytes is shared with other messages on the same channel, and that is safe because every
 * recogniser on it tests its own tag as well as the length. This one does the same. */
#define MP_EVENT_DEATH       0x94u
#define MP_EVENT_DEATH_BYTES 4u

/* Nobody killed this player: a fall, the level, or a death nothing can be attributed to. */
#define MP_DEATH_NO_KILLER 0xFFu

/* Why the player died. The reason is what a rule set charges points for, so it is on the wire
 * rather than derived: only the victim's machine can tell a fall from a hit. */
#define MP_DEATH_BY_HIT         0u   /* somebody else's contact */
#define MP_DEATH_BY_SUICIDE     1u   /* the victim's own weapon, blade or force */
#define MP_DEATH_BY_FALL        2u
#define MP_DEATH_BY_ENVIRONMENT 3u   /* the level itself: fire, a crusher, a script */
#define MP_DEATH_REASON_MAX     MP_DEATH_BY_ENVIRONMENT

typedef struct mp_death_note {
    uint8_t victim_slot;
    uint8_t killer_slot;   /* MP_DEATH_NO_KILLER when nothing can be named */
    uint8_t reason;
} mp_death_note_t;

/* The three pure halves of the message. The recogniser tests the length and the tag together, as
 * every other recogniser on this channel does; the decoder refuses a reason it does not know and
 * a killer that is the victim, because a slot cannot betray itself and a rule set that charged
 * for it would be charging for a decoding fault. */
bool mp_death_is(const uint8_t *note, size_t bytes);
bool mp_death_encode(const mp_death_note_t *note, uint8_t *bytes, size_t capacity);
bool mp_death_decode(const uint8_t *note, size_t bytes, mp_death_note_t *out);

#endif /* MULTIPLAYER_MP_DEATH_H */
