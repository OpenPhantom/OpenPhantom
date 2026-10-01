/* movie_rule.c: who decides when a movie ends. See the header. */
#include "movie_rule.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

movie_role_t movie_rule_role(bool gate_read, const movie_gate_t *gate)
{
    if (!gate_read || gate == NULL || !gate->running) {
        return MOVIE_ROLE_FREE;
    }
    if (!gate->client) {
        return MOVIE_ROLE_HOST;
    }
    if (!gate->host_connected || gate->connection == 0u) {
        return MOVIE_ROLE_ALONE;
    }
    if (gate->host_moving) {
        return MOVIE_ROLE_SKIP;
    }
    return MOVIE_ROLE_LOCKED;
}

movie_exits_t movie_rule_exits(movie_role_t role)
{
    movie_exits_t exits;

    /* The movie player's own behaviour, the one every installation has shipped with. */
    exits.escape_ends  = true;
    exits.focus_ends   = true;
    exits.close_ends   = true;
    exits.pump_session = false;

    switch (role) {
    case MOVIE_ROLE_ALONE:
        exits.pump_session = true;
        break;
    case MOVIE_ROLE_HOST:
        /* The retail player ends a movie on neither, and in a session a click into a fellow
         * player's window would otherwise end the movie of everybody. */
        exits.focus_ends   = false;
        exits.close_ends   = false;
        exits.pump_session = true;
        break;
    case MOVIE_ROLE_LOCKED:
        exits.escape_ends  = false;
        exits.focus_ends   = false;
        exits.close_ends   = false;
        exits.pump_session = true;
        break;
    case MOVIE_ROLE_FREE:
    case MOVIE_ROLE_SKIP:
    default:
        break;
    }
    return exits;
}

/* Grown, and by less than half the counter: a count that went backwards is not the host moving,
 * and one that wrapped still is. */
static bool count_grew(uint32_t before, uint32_t now)
{
    uint32_t step = now - before;

    return step != 0u && step < 0x80000000u;
}

movie_verdict_t movie_rule_verdict(const movie_baseline_t *begin, bool gate_read,
                                   const movie_gate_t *now, uint32_t quiet_ms)
{
    if (begin == NULL || !gate_read || now == NULL) {
        return quiet_ms >= MOVIE_RULE_QUIET_MS ? MOVIE_VERDICT_QUIET : MOVIE_VERDICT_GO_ON;
    }
    if (!now->running || !now->client) {
        return MOVIE_VERDICT_NOT_RUNNING;
    }
    /* The multiplayer files a connection that has gone as nought, so a gate from after the drop
     * answers here, whatever the count did before it; both answers let the player go. */
    if (!now->host_connected || now->connection == 0u) {
        return MOVIE_VERDICT_NOT_CONNECTED;
    }
    if (now->connection != begin->connection) {
        return MOVIE_VERDICT_OTHER_CONNECTION;
    }
    if (count_grew(begin->host_payloads, now->host_payloads)) {
        return MOVIE_VERDICT_HOST_DONE;
    }
    if (quiet_ms >= MOVIE_RULE_QUIET_MS) {
        return MOVIE_VERDICT_QUIET;
    }
    return MOVIE_VERDICT_GO_ON;
}

uint32_t movie_rule_quiet_after(uint32_t quiet_ms, bool gate_moved, uint32_t turn_ms)
{
    uint32_t counted = turn_ms < MOVIE_RULE_TURN_CAP_MS ? turn_ms : MOVIE_RULE_TURN_CAP_MS;

    if (gate_moved) {
        return 0u;
    }
    return quiet_ms > UINT32_MAX - counted ? UINT32_MAX : quiet_ms + counted;
}

void movie_rule_let_go(movie_loop_t *loop)
{
    if (loop == NULL) {
        return;
    }
    loop->poll  = NULL;
    loop->exits = movie_rule_exits(MOVIE_ROLE_ALONE);
}
