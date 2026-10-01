/* mp_session_now.c: the one reading of the session this machine is in. See the header. */
#include "mp_session_now.h"

#include "mp_armed.h"
#include "mp_bridge.h"
#include "mp_bridge_drain.h"
#include "mp_bridge_lobby.h"
#include "mp_lobby.h"
#include "mp_scene_rule.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The session as the HOST said it, not as this side's own bridge remembers it. At a level begin
 * and on a screen inside a level no substep need have run, and the drain's own joined flag is then
 * whatever the last level left; the setup note is the thing the host actually sent. */
bool mp_session_now_client_of_a_started_session(bool *runs, uint8_t *generation)
{
    mp_lobby_setup_t setup;
    bool             started = mp_bridge_lobby_setup(&setup) &&
                               (setup.flags & MP_LOBBY_F_STARTED) != 0u;
    bool             ended   = started && (setup.flags & MP_LOBBY_F_ENDED) != 0u;

    if (runs != NULL) {
        *runs = mp_scene_session_runs(started, ended);
    }
    if (generation != NULL) {
        *generation = started ? setup.generation : 0u;
    }
    return mp_scene_client_of_a_started_session(started, ended, mp_bridge_drain_is_client());
}

bool mp_session_now_plays_in_a_running_session(bool *client)
{
    mp_lobby_setup_t setup;
    bool             is_client = mp_bridge_drain_is_client();

    if (client != NULL) {
        *client = is_client;
    }
    if (!mp_armed_transport()) {
        return false;
    }
    if (!mp_bridge_lobby_setup(&setup)) {
        if (mp_bridge_armed_by_menu()) {
            return false;   /* a lobby before the first note, or one reset at a session's end */
        }
        return is_client ? mp_bridge_drain_host_connection() != 0u : mp_bridge_lobby_connected();
    }
    if ((setup.flags & MP_LOBBY_F_STARTED) == 0u || (setup.flags & MP_LOBBY_F_ENDED) != 0u) {
        return false;
    }
    return !is_client || mp_bridge_drain_host_connection() != 0u;
}
