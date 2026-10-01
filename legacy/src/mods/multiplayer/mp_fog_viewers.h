/* mp_fog_viewers.h: the fog of a room on the running engine, played by each machine for its own
 * player.
 *
 * Layer 2. The decisions are mp_fog_viewers_rule; this finds the scripts of the level that is
 * open, reads each one's fog out of this machine's own copy of it, and drives one viewer per run
 * of such a script: the host's own player on the host, this client's own player on a client. The
 * host finds the actors that run such a script, refuses their fog before the engine sees it, and
 * says in its note that the script runs, that it has ended, and where its actor stands. A client
 * takes that and plays the room's fog for its own player alone.
 *
 * Only a script the table classes per viewer and whose entries all held what the table says is
 * played here; any other stays shared, on both sides, which is how every fog was played before.
 * Nothing here runs without a session.
 */
#ifndef MULTIPLAYER_MP_FOG_VIEWERS_H
#define MULTIPLAYER_MP_FOG_VIEWERS_H

#include "mp_level_state_rule.h"

#include <stdbool.h>
#include <stdint.h>

/* What the host does with a fog command of an actor's script. */
typedef enum mp_fog_viewers_verdict {
    MP_FOG_VIEWERS_SHARED = 0,   /* not a room this side plays per viewer: the level's fog */
    MP_FOG_VIEWERS_WITHHELD,     /* a room's, and this side's own viewer runs: refused */
    MP_FOG_VIEWERS_OWN_ENGINE    /* a room's, and the viewer did not run: this side's engine plays
                                  * it for its own player, and it is never the level's fog */
} mp_fog_viewers_verdict_t;

/* The host: the verdict on a fog command of `actor`. A room's command is never the level's fog,
 * whether the viewer runs or not; SHARED wherever this side could not bind the script. Counted. */
mp_fog_viewers_verdict_t mp_fog_viewers_hear(uintptr_t actor);

/* The host, once a substep: the runs looked up and this side's own viewer driven. */
void mp_fog_viewers_host_tick(void);

/* The host: its runs into a note. */
void mp_fog_viewers_describe(mp_level_state_note_t *note);

/* A client: the host's runs out of a note it applied. `first` is the first note of its level and
 * generation, which starts every viewer over. */
void mp_fog_viewers_client_take(const mp_level_state_note_t *note, bool first);

/* A client, once a substep: this side's own viewer driven by the host's runs. */
void mp_fog_viewers_client_tick(void);

/* Forgets the runs, the viewers and the level's scripts. The counters stay for the report. */
void mp_fog_viewers_reset(void);

void mp_fog_viewers_report(bool host);

#endif /* MULTIPLAYER_MP_FOG_VIEWERS_H */
