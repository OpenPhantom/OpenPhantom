/* mp_bridge_lobby_late.c: a client whose lobby opens on a running session. See the header. */
#include "mp_bridge_lobby_late.h"

#include "mp_bridge_drain.h"
#include "mp_bridge_drain_host.h"
#include "mp_bridge_lobby.h"
#include "mp_lobby.h"
#include "mp_wallclock.h"

#include "common/logging.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct lobby_late_state {
    bool     heard;              /* this lobby has heard its host's setup */
    bool     running_at_first;   /* and the first one it heard was a running session's start */
    bool     took_running;       /* the start last taken from a lobby was such a session's */
    bool     said;               /* the join of a running session was said in this lobby */
    bool     holding;            /* a running session's start waits for this player's ready */
    uint32_t hold_since_ms;
    uint32_t holds;              /* starts held for want of ready, over the run */
    uint32_t longest_ms;
    uint32_t taken;              /* starts taken from a lobby */
    uint32_t taken_not_ready;    /* of those, with this player not ready: must be 0 */
} lobby_late_state_t;

/* Module state because a process holds one lobby. */
static lobby_late_state_t late;

bool mp_bridge_lobby_joins_running_session(void)
{
    mp_lobby_setup_t setup;

    memset(&setup, 0, sizeof setup);
    return mp_bridge_drain_is_client() && mp_bridge_lobby_peek_start(&setup) &&
           (setup.flags & MP_LOBBY_F_STARTED) != 0u && (setup.flags & MP_LOBBY_F_ENDED) == 0u;
}

static bool this_player_ready(void)
{
    bool ready = false;

    mp_bridge_get_lobby(NULL, &ready);
    return ready;
}

/* A wait that ended, whichever way: its length kept for the report, and the wait over. */
static uint32_t end_hold(void)
{
    uint32_t held;

    if (!late.holding) {
        return 0u;
    }
    held         = mp_wallclock_ms() - late.hold_since_ms;
    late.holding = false;
    if (held > late.longest_ms) {
        late.longest_ms = held;
    }
    return held;
}

void mp_bridge_lobby_late_tick(bool client_lobby_open)
{
    mp_lobby_setup_t setup;
    bool             offered;

    if (!client_lobby_open) {
        return;
    }
    offered = mp_bridge_lobby_joins_running_session();
    if (!offered) {
        (void)end_hold();   /* nothing to wait for any more: taken, or the session ended */
        return;
    }
    /* A wait that is on stays on until the start is taken, which says how long it was: a ready
     * pressed now is taken by the screen's next frame. */
    if (late.holding || mp_lobby_start_may_be_taken(this_player_ready(), offered)) {
        return;
    }
    late.holding       = true;
    late.hold_since_ms = mp_wallclock_ms();
    ++late.holds;
    if (!late.said && mp_bridge_lobby_setup(&setup)) {
        late.said = true;
        log_info("this side joins a running session: the host is in %s (%s), generation %u; the "
                 "level is entered when this player says ready", setup.title, setup.level,
                 (unsigned)setup.generation);
    }
}

void mp_bridge_lobby_late_heard_setup(bool lobby_open)
{
    if (!lobby_open || late.heard) {
        return;
    }
    late.heard            = true;
    late.running_at_first = mp_bridge_lobby_joins_running_session();
}

void mp_bridge_lobby_late_took_start(bool lobby_open)
{
    bool     was_holding = late.holding;
    uint32_t held;

    if (!lobby_open) {
        late.took_running = false;   /* the follow's start: a world change, not a join */
        return;
    }
    ++late.taken;
    if (!this_player_ready()) {
        ++late.taken_not_ready;
    }
    late.took_running = late.running_at_first;
    held              = end_hold();
    if (was_holding) {
        log_info("the start of a running session was held %u ms until this player was ready",
                 (unsigned)held);
    }
}

void mp_bridge_lobby_late_reset(void)
{
    (void)end_hold();
    late.heard            = false;
    late.running_at_first = false;
    late.took_running     = false;
    late.said             = false;
}

bool mp_bridge_lobby_late_start(void)
{
    return late.took_running;
}

void mp_bridge_lobby_late_counts(mp_bridge_lobby_late_counts_t *out)
{
    if (out == NULL) {
        return;
    }
    out->taken           = late.taken;
    out->held            = late.holds;
    out->longest_ms      = late.longest_ms;
    out->taken_not_ready = late.taken_not_ready;
}

void mp_bridge_lobby_late_report(bool is_host)
{
    mp_bridge_lobby_late_counts_t counts;

    if (is_host) {
        mp_bridge_drain_host_report();
        return;
    }
    mp_bridge_lobby_late_counts(&counts);
    log_info("  the lobby's start (client): %u taken, %u held for want of ready (longest %u ms), "
             "%u taken while not ready (must be 0)", (unsigned)counts.taken,
             (unsigned)counts.held, (unsigned)counts.longest_ms,
             (unsigned)counts.taken_not_ready);
    mp_bridge_drain_lobby_report();
}
