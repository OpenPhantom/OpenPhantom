/* mp_armed.h: whether a session's transport stands in this process, and what this process says
 * about its session to the other mods.
 *
 * Layer 1, one cell and the note beside it. The bridge puts a transport up and takes it down, and
 * it is the only writer; everything else asks. It is a module of its own, with nothing under it, so
 * that a hook in a relay module can ask without linking the bridge into every test that builds
 * that relay.
 *
 * The question it answers is the one the single player rule turns on: with no transport standing,
 * no multiplayer code does anything. A hook installed for a session stays installed for the life of
 * the process, because a detour cannot be taken out again, so the hooks that do work of their own
 * ask this first and hand the engine its own call when the answer is no.
 *
 * The session note (common/session_note) is written here and nowhere else, at the same moment as
 * the cell and from the same values. The developer overlay reads it to lock the rows a session
 * cannot survive, and the mouse and pad paths of enhanced_input read whether anything of the
 * session holds this player's input. Two writers for one state is how the overlay and the pause
 * menu came to be asked two different questions, so there is one.
 *
 * The hold is a set of holders rather than one switch. The pause menu, the chat and a scene the
 * host is brought to each hold the input for a reason of their own and let go on a way out of
 * their own, and with one switch the first to let go freed the other's hold: a pause menu
 * closed over an open chat would have let the player walk off while typing. Each holder takes and
 * drops only its own bit, and the input is held while any bit is set.
 */
#ifndef MULTIPLAYER_MP_ARMED_H
#define MULTIPLAYER_MP_ARMED_H

#include <stdbool.h>
#include <stdint.h>

/* The bridge's, at the end of an install and at the start of a take down. Says the note too; a
 * transport that comes down takes the input hold with it. */
void mp_armed_set_transport(bool standing, bool is_host);

/* Whether a transport stands: a lobby is open, a session level runs, or an exit is still saying
 * its last word. False before the first lobby and after every exit. */
bool mp_armed_transport(void);

/* Whether this machine is the one the others follow. False while no transport stands. */
bool mp_armed_is_host(void);

/* Who holds this player's input, one bit each. */
typedef enum mp_armed_holder {
    MP_ARMED_HOLDER_PAUSE = 1u << 0,   /* a session's pause menu */
    MP_ARMED_HOLDER_CHAT  = 1u << 1,   /* the chat's input line, and the key that closed it */
    MP_ARMED_HOLDER_SCENE = 1u << 2    /* the host kept at the place of a scene until it runs */
} mp_armed_holder_t;

/* `holder` takes this player's input, or lets its own hold go; another holder's hold is left as it
 * is. A hold is taken only while a transport stands, and a transport that comes down drops every
 * one. Returns false when the note refused it; the hold stands in this process either way, so the
 * engine's readers are held, and only another mod's own input path is not. */
bool mp_armed_hold_input(mp_armed_holder_t holder, bool held);

/* Whether anything of the session holds this player's input right now. */
bool mp_armed_input_held(void);

/* Who holds it, as mp_armed_holder_t bits; none while no transport stands. For the player's
 * repair, which drops a hold whose owner is gone: a holder takes and drops its own bit on its
 * own ways in and out, and a way out that was missed leaves the bit standing for the session. */
uint32_t mp_armed_holders(void);

/* How often the note was said, and how often the channel refused it. */
void mp_armed_note_counts(uint32_t *said, uint32_t *refused);

#endif /* MULTIPLAYER_MP_ARMED_H */
