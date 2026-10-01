/* mp_exit.c: the one way out of a session, as rules. See the header. */
#include "mp_exit.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

mp_exit_step_t mp_exit_step(bool closing, bool level_running, size_t reliable_pending,
                            uint32_t waited_ms)
{
    if (!closing) {
        return MP_EXIT_STEP_NONE;
    }
    /* A level that still runs is a world the first half is clearing and the engine is fading
     * out. Its end is what the transport waits for, with no deadline: a transport taken down under
     * a standing level leaves a client's parked replicas with nobody to release them. */
    if (level_running) {
        return MP_EXIT_STEP_WAIT;
    }
    if (reliable_pending == 0u || waited_ms >= MP_EXIT_LINGER_MS) {
        return MP_EXIT_STEP_FINISH;
    }
    return MP_EXIT_STEP_WAIT;
}

bool mp_exit_title_door(const mp_exit_title_facts_t *facts)
{
    bool still_following;

    if (facts == NULL || !facts->title_shown || !facts->menu_armed || facts->closing) {
        return false;
    }
    if (facts->start_pending) {
        return false;   /* the lobby's start and a follower's start both pass through here */
    }
    /* A follower waits at the title for its host's file or for its own start, and it is still in
     * the session while it does. A host that ended the session or went away is not followed. */
    still_following = facts->following && facts->host_connected && !facts->ended;
    if (still_following) {
        return false;
    }
    /* A session that has not had a level yet is a lobby that is on its way to one, or an ini run
     * that never had a menu. Neither is this door's: the lobby has its own, and the start's give
     * up has its own. */
    return facts->level_seen;
}

mp_lobby_over_t mp_exit_notice(bool is_client, mp_lobby_over_t verdict, bool heard_ended,
                               bool host_said_goodbye, bool host_went_quiet, bool sent_away)
{
    if (!is_client) {
        return MP_LOBBY_OVER_NO;
    }
    if (verdict == MP_LOBBY_OVER_CONTENT) {
        return MP_LOBBY_OVER_CONTENT;   /* this side cut itself off, whatever the host did next */
    }
    if (verdict == MP_LOBBY_OVER_BEHIND || sent_away) {
        return MP_LOBBY_OVER_BEHIND;    /* the host's last notice to this side said why */
    }
    if (verdict == MP_LOBBY_OVER_HOST_ENDED || heard_ended || host_said_goodbye) {
        return MP_LOBBY_OVER_HOST_ENDED;
    }
    if (verdict == MP_LOBBY_OVER_HOST_LOST || host_went_quiet) {
        return MP_LOBBY_OVER_HOST_LOST;
    }
    return MP_LOBBY_OVER_NO;
}

const char *mp_exit_why_name(mp_exit_why_t why)
{
    switch (why) {
    case MP_EXIT_WHY_LOBBY_BACK:     return "the lobby was left";
    case MP_EXIT_WHY_HOST_GONE:      return "the host ended the lobby";
    case MP_EXIT_WHY_LEFT_LEVEL:     return "the session level was left for the title";
    case MP_EXIT_WHY_AT_TITLE:       return "the title screen came back with the session up";
    case MP_EXIT_WHY_START_GIVEN_UP: return "the start never happened";
    case MP_EXIT_WHY_VERDICT:        return "the level ended with the session";
    case MP_EXIT_WHY_NEW_TRANSPORT:  return "the menu asked for another transport";
    case MP_EXIT_WHY_COUNT:
    default:                         return "an unnamed reason";
    }
}
