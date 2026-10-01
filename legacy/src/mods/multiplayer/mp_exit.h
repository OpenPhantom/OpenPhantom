/* mp_exit.h: the one way out of a session, as rules with nothing under them.
 *
 * Layer 1, pure. A session used to end by four doors, and each door cleared a different part of
 * it: backing out of the lobby forgot the lobby and kept the socket, the end of a client's level
 * cleared the world and kept the transport, the quit said goodbye and nothing else, and a change of
 * side took the transport down and kept the world. A host that had gone back to its title screen
 * still admitted players, and a single player level after a session carried the session's timer,
 * roles and relays.
 *
 * Now there is one exit and it has two halves. The first half is said at once, wherever the exit
 * was asked for: the host declares the session over, and the world the session left standing is
 * cleared while it still stands. The second half takes the transport down, and it waits for two
 * things: no level may be running, because a level is the world the first half was clearing, and
 * the word the host has just said must have been delivered, or a second must have passed. These
 * rules decide when each of those is true.
 */
#ifndef MULTIPLAYER_MP_EXIT_H
#define MULTIPLAYER_MP_EXIT_H

#include "mp_lobby.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* How long the second half waits for the host's last word to be acknowledged. A client that has
 * the note acknowledges it within a round trip; one that does not has gone, and the goodbye after
 * the wait tells whoever is still listening. */
#define MP_EXIT_LINGER_MS 1000u

/* Why a session is being left. The number indexes the report's counters. */
typedef enum mp_exit_why {
    MP_EXIT_WHY_LOBBY_BACK = 0,   /* the player backed out of the lobby screen */
    MP_EXIT_WHY_HOST_GONE,        /* a client's lobby heard its host end the session or leave */
    MP_EXIT_WHY_LEFT_LEVEL,       /* the campaign is on its way to the title from a session level */
    MP_EXIT_WHY_AT_TITLE,         /* the title screen is up and the session has no level left */
    MP_EXIT_WHY_START_GIVEN_UP,   /* the start the lobby promised never happened */
    MP_EXIT_WHY_VERDICT,          /* a client's level ended with its session */
    MP_EXIT_WHY_NEW_TRANSPORT,    /* the menu asks for the other side or another port */
    MP_EXIT_WHY_COUNT
} mp_exit_why_t;

/* What the pumps do about an exit this frame. */
typedef enum mp_exit_step {
    MP_EXIT_STEP_NONE = 0,   /* no exit is under way */
    MP_EXIT_STEP_WAIT,       /* a level still stands, or the last word is still on its way */
    MP_EXIT_STEP_FINISH      /* take the transport down now */
} mp_exit_step_t;

/* The facts the title door is decided from. */
typedef struct mp_exit_title_facts {
    bool title_shown;      /* the engine's title screen is the screen on show */
    bool menu_armed;       /* the menu put the transport up, not the ini or the loopback */
    bool closing;          /* an exit is already under way */
    bool start_pending;    /* a start is being driven through the title menu */
    bool following;        /* a client is on its way to its host's next world */
    bool host_connected;   /* a client still has its host */
    bool ended;            /* the setup says the session is over */
    bool level_seen;       /* a level of this session has begun on this side */
} mp_exit_title_facts_t;

/* The second half's step. `waited_ms` is the time since the exit was asked for. */
mp_exit_step_t mp_exit_step(bool closing, bool level_running, size_t reliable_pending,
                            uint32_t waited_ms);

/* Whether the title screen being on show means the session has nothing left to be, which is the
 * door for every way back to the title the campaign takes without saying so. A start that is being
 * driven passes through the title on purpose, and so does a client on its way to its host's next
 * world, but only while it has a host that has not ended the session. */
bool mp_exit_title_door(const mp_exit_title_facts_t *facts);

/* What a player is told at the title about an ending that was not their choice. A host is told
 * nothing, because a host's session ends when it leaves. A client is told the host's own word when
 * it heard one, or saw the host say goodbye; that the connection was lost when it only went quiet;
 * that the game data differed when that was the verdict; and that the host sent it away for
 * falling behind when the host's last notice said so. `verdict` is the level's verdict, or
 * MP_LOBBY_OVER_NO when there was none. */
mp_lobby_over_t mp_exit_notice(bool is_client, mp_lobby_over_t verdict, bool heard_ended,
                               bool host_said_goodbye, bool host_went_quiet, bool sent_away);

/* A word for the log. */
const char *mp_exit_why_name(mp_exit_why_t why);

#endif /* MULTIPLAYER_MP_EXIT_H */
