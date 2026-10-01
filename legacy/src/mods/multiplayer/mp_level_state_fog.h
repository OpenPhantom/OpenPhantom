/* mp_level_state_fog.h: the director's fog the whole level shares, as a state with one writer.
 *
 * Layer 2. The host plays the level's fog as its scripts ask and follows what each command did in
 * mp_level_state_fog_rule. A command that changed the fog goes into the level's journal with the
 * script's own values, and the state goes in every note. A client refuses its own scripts' fog in
 * a started session, plays the journal's fog entries in the host's order through the director past
 * this feature's hull, and brings its engine to the state when it meets a level's first note or
 * finds that entries were lost.
 *
 * A room's fog is not this: a script the viewers' module plays per viewer is refused on the host
 * before it reaches the engine, and nothing of it is journaled. The fog the viewers play goes
 * through the same director and the same line of what was drawn, by mp_level_state_fog_play.
 *
 * Nothing here runs without a session: alone, the director's fog goes to the engine and is only
 * counted.
 */
#ifndef MULTIPLAYER_MP_LEVEL_STATE_FOG_H
#define MULTIPLAYER_MP_LEVEL_STATE_FOG_H

#include "mp_level_state_internal.h"
#include "mp_level_state_rule.h"

#include <stdbool.h>
#include <stdint.h>

/* One fog command the director was asked for on this side. `can_match` says a client can play the
 * host's fog instead. True when it is refused and must not reach the engine. */
bool mp_level_state_fog_hear(void *actor, int32_t command, int32_t a1, int32_t a2,
                             mp_level_side_t side, bool can_match);

/* The host, once a substep: a ramp counts down by this machine's substep length. */
void mp_level_state_fog_host_tick(void);

/* The host: the fog part of a note, when there is any fog to say. */
void mp_level_state_fog_describe(mp_level_state_note_t *note);

/* A client: one fog entry of the journal, played. False when the director did not take it. */
bool mp_level_state_fog_replay(const mp_level_journal_entry_t *entry);

/* A client: its engine brought to the note's fog. */
void mp_level_state_fog_heal(const mp_level_state_note_t *note);

/* One command through the director past this feature's hull, with the stand-in, noted for the line
 * of what was drawn. `whose` is one of MP_LEVEL_DRAWN_. False without a director. */
bool mp_level_state_fog_play(const char *whose, int32_t command, int32_t a1, uint32_t a2,
                             uint16_t sequence);

/* Forgets what a level or a session left here. */
void mp_level_state_fog_reset(void);

void mp_level_state_fog_report(bool host);

#endif /* MULTIPLAYER_MP_LEVEL_STATE_FOG_H */
