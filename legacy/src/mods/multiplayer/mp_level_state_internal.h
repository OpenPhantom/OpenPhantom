/* mp_level_state_internal.h: what the level's state and its two fog halves say to each other.
 *
 * The level's state is one module in three files: mp_level_state.c builds, sends, takes and applies
 * the note and keeps the host's journal; mp_level_state_fog.c is the director's fog the level
 * shares; mp_fog_viewers.c is the fog of a room each machine plays for its own player. They share
 * the side this machine plays in a session and the one journal, and nothing outside them reads
 * either.
 */
#ifndef MULTIPLAYER_MP_LEVEL_STATE_INTERNAL_H
#define MULTIPLAYER_MP_LEVEL_STATE_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

/* Which side of a session this machine is, as the session says it now. */
typedef enum mp_level_side {
    MP_LEVEL_SIDE_ALONE = 0,   /* no session runs: single player, or one that has ended */
    MP_LEVEL_SIDE_HOST,
    MP_LEVEL_SIDE_CLIENT       /* a client of a started session */
} mp_level_side_t;

mp_level_side_t mp_level_state_side(void);

/* Whether a far player's bank is open, so the hero block holds a puppet's body. False when the
 * session did not say how to ask. */
bool mp_level_state_banked(void);

/* The host notes one change that took effect for the journal. Returns its number, 0 when the host
 * keeps no journal in this substep (no session runs). */
uint16_t mp_level_state_journal_note(uint8_t kind, uint8_t a, uint16_t b, uint32_t c);

/* The level this substep runs in, as the enemy table's identity says it. False with none known. */
bool mp_level_state_level(uint16_t *identity);

/* The substeps this side has run with its transport armed, joined or not: the level is told once
 * at the start of each. */
uint32_t mp_level_state_substep(void);

#endif /* MULTIPLAYER_MP_LEVEL_STATE_INTERNAL_H */
