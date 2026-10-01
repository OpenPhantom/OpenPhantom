/* mp_movie_gate.c: the movie gate, filed from both pumps, and the movie player's record read back.
 * See the header.
 */
#include "mp_movie_gate.h"

#include "mp_armed.h"
#include "mp_bridge_drain.h"
#include "mp_follow.h"
#include "mp_text.h"

#include "common/logging.h"
#include "common/movie_note.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

typedef struct movie_gate_state {
    uint32_t      published;       /* every gate filed, the withdrawals included */
    bool          client_said;     /* the line for a client's side stands and was not taken back */
    bool          player_heard;    /* the movie player's record was read at least once */
    bool          movies_known;    /* movies_seen holds a count that was read */
    uint16_t      movies_seen;     /* the movie player's begun count at the last read */
    uint32_t      movies_played;   /* how far that count moved while this module read it */
    movie_state_t last;            /* the newest record read */
} movie_gate_state_t;

static movie_gate_state_t gate_state;

/* Said on the edge and not on every pump, because the pumps run hundreds of times a second. */
static void say_the_side(bool client)
{
    if (client == gate_state.client_said) {
        return;
    }
    gate_state.client_said = client;
    if (client) {
        log_info("a movie on this machine belongs to the host: this side's movies end with the "
                 "host's world (the movie gate says client of a started session)");
    } else {
        log_info("a movie on this machine is this side's own again: the movie gate no longer says "
                 "client of a started session");
    }
}

/* The movie player's record, read back. A begun count that moved is a movie that began, and a
 * host's is handed to the follow, which decides whether it opens the next world. The count moves
 * by one per movie, and this runs every thirty milliseconds, so one read never misses one. */
static void hear_the_player(bool runs, bool client)
{
    movie_state_t state;

    if (!movie_note_read_state(&state)) {
        return;
    }
    gate_state.player_heard = true;
    if (gate_state.movies_known && state.movies_begun != gate_state.movies_seen) {
        gate_state.movies_played += (uint16_t)(state.movies_begun - gate_state.movies_seen);
        if (runs && !client) {
            mp_follow_note_host_movie(state.stem);
        }
    }
    gate_state.movies_known = true;
    gate_state.movies_seen  = state.movies_begun;
    gate_state.last         = state;
}

void mp_movie_gate_pump(bool runs, bool client, uint8_t generation)
{
    movie_gate_t gate;

    if (!mp_armed_transport()) {
        return;
    }
    memset(&gate, 0, sizeof gate);
    gate.running        = runs;
    gate.client         = client;
    gate.connection     = client ? mp_bridge_drain_host_connection() : 0u;
    gate.host_connected = gate.connection != 0u;
    gate.host_moving    = client && mp_bridge_drain_host_moving();
    gate.host_payloads  = mp_bridge_drain_host_payloads();
    gate.generation     = generation;
    gate.language       = (uint8_t)mp_text_language();
    gate.published      = ++gate_state.published;
    (void)movie_note_publish_gate(&gate);   /* a refusal leaves the gate before it standing */
    say_the_side(client);
    hear_the_player(runs, client);
}

void mp_movie_gate_withdraw(void)
{
    movie_gate_t gate;

    memset(&gate, 0, sizeof gate);
    gate.language  = (uint8_t)mp_text_language();
    gate.published = ++gate_state.published;
    (void)movie_note_publish_gate(&gate);
    say_the_side(false);
}

static const char *how_it_ended(const movie_state_t *state)
{
    switch (state->state) {
    case MOVIE_STATE_PLAYING:
        return "still playing";
    case MOVIE_STATE_WAITING:
        return "still waiting for the host";
    case MOVIE_STATE_ENDED:
        break;
    default:
        return "none yet";
    }
    switch (state->end_reason) {
    case MOVIE_END_HOST:
        return "ended by the host";
    case MOVIE_END_SKIPPED:
        return "skipped";
    case MOVIE_END_ALONE:
        return "ended alone, because the session went away while it waited";
    default:
        return "ended on its own";
    }
}

void mp_movie_gate_report(void)
{
    log_info("  the movie gate: published %u time(s), host payloads seen %u; this machine's "
             "movies: %u played, last \"%s\" %s",
             (unsigned)gate_state.published, (unsigned)mp_bridge_drain_host_payloads(),
             (unsigned)gate_state.movies_played, gate_state.last.stem,
             how_it_ended(&gate_state.last));
    if (!gate_state.player_heard && mp_bridge_drain_is_client()) {
        log_info("  the movie note: nothing on this machine publishes one (fmv_player absent, "
                 "disabled or older), so this client's movies are not held for the host");
    }
}
