/* mp_pumps.c: the bridge's two idle pumps, and the order in which a drawn frame hands the
 * session's news to the modules that act on it.
 *
 * The frame hook runs once per drawn frame, the blocking menu loops included; the thread timer
 * keeps the sessions serviced through a level load, when no frame is drawn. The order inside the
 * frame pump is part of its meaning, and each step says why it stands where it does.
 */
#include "mp_pumps.h"
#include "multiplayer.h"

#include "mp_armed.h"
#include "mp_arrival.h"
#include "mp_bank.h"
#include "mp_bridge.h"
#include "mp_bridge_drain.h"
#include "mp_bridge_far.h"
#include "mp_census_probe.h"
#include "mp_chat_draw.h"
#include "mp_chat_input.h"
#include "mp_cutscene.h"
#include "mp_bridge_lobby.h"
#include "mp_follow.h"
#include "mp_host_settings.h"
#include "mp_hud.h"
#include "mp_lobby.h"
#include "mp_movie_gate.h"
#include "mp_pause.h"
#include "mp_reentry.h"
#include "mp_respawn.h"
#include "mp_round.h"
#include "mp_scene_host.h"
#include "mp_scene_watch.h"
#include "mp_seat.h"
#include "mp_session_now.h"
#include "mp_session_over.h"
#include "mp_start.h"

#include "common/frame_hook.h"
#include "common/logging.h"

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The bridge's two idle pumps. The frame hook runs once per drawn frame, the blocking menu loops
 * included, but not while a level loads; the thread timer is dispatched by the engine's own message
 * pump, which the load loop keeps running. Neither sends state and both turn back inside a tick. */
#define BRIDGE_PUMP_TIMER_MS 30u

/* One re-entry peer and one seat body for each far bank, in bank order. */
_Static_assert(MP_REENTRY_MAX_PEERS == MP_BANK_FAR_MAX, "a re-entry peer for every far bank");
_Static_assert(MP_SEAT_FAR_BODIES == MP_BANK_FAR_MAX, "a seat body for every far bank");

/* The movie gate and the host's settings, from whichever pump is running. Only the timer pump runs
 * while a movie is on screen and a level loads, and the frame pump is the one that runs every
 * frame, so both file them. The host's settings ask mp_session_now's second question themselves,
 * whether this machine plays in a running session, which is the one the world holds ask. */
static void file_the_movie_gate(void)
{
    bool    runs       = false;
    uint8_t generation = 0u;
    bool    client     = mp_session_now_client_of_a_started_session(&runs, &generation);

    mp_movie_gate_pump(runs, client, generation);
    mp_host_settings_pump();
}

/* What the re-entry rules have to know and cannot read for themselves: which world slot this
 * machine holds, where each far player is and whether it is standing, and what game is being
 * played under which rules. All three are the bridge's answers, and handing them over keeps the
 * rule that consumes them out of the wire layer and drivable in a test with no session. */
static void feed_reentry(void)
{
    mp_lobby_setup_t     setup;
    /* Not named 'far': windows.h still defines that as an empty macro, and a local called far
     * disappears at the preprocessor with nothing to read but a syntax error. */
    mp_bridge_far_pose_t peer;
    size_t               bank;

    if (mp_bridge_lobby_setup(&setup) && (setup.flags & MP_LOBBY_F_STARTED) != 0u) {
        mp_reentry_note_session(setup.mode, mp_bridge_drain_is_client(), &setup.rules);
    } else {
        mp_reentry_note_no_session();
    }
    /* And the same session, read for the other question it answers: a scene belongs to the host,
     * so a client's own scripts may not take its player for one. */
    mp_cutscene_set_client_holds_back(mp_session_now_client_of_a_started_session(NULL, NULL));
    mp_reentry_note_my_slot(mp_bridge_drain_my_slot());
    /* Every far player, because in co-op the last one standing is the last of all of them. The
     * same reading goes to the seat search, which tries the standing ones as anchors and keeps
     * every one of them, standing or not, off the seats it hands out. */
    for (bank = 1u; bank <= MP_BANK_FAR_MAX; ++bank) {
        if (mp_bridge_far_pose(bank, MP_FAR_READER_REENTRY, &peer)) {
            /* A body that has been resolved but has said neither alive nor dead is not
             * somebody to stand beside. */
            bool stands = mp_bridge_far_pose_stands(&peer);

            mp_reentry_note_peer(bank - 1u, peer.position, peer.heading, stands);
            mp_seat_note_body(bank - 1u, peer.position, peer.heading, stands);
        } else {
            mp_reentry_note_no_peer(bank - 1u);
            mp_seat_note_no_body(bank - 1u);
        }
    }
    mp_reentry_tick(mp_bridge_drain_substeps());
}

static void bridge_frame_pump(void)
{
    /* The frame hook cannot be taken out again, so a pump armed for one session asks whether there
     * still is one. Single player after a session draws its frames with none of this. */
    if (!mp_armed_transport()) {
        return;
    }
    mp_bridge_pump_idle(MP_BRIDGE_PUMP_FRAME);
    mp_census_probe_run();
    /* Before anything acts on the session, because what this may decide is that there is no
     * longer one. Running it after the round pump would score a frame of a session that is over,
     * and after the re-entry feed would arm a respawn beside a peer that has gone. */
    mp_session_over_tick();
    /* After it, so a session that has just ended is not followed anywhere. */
    mp_follow_tick();
    multiplayer_follow_the_hosts_game();
    /* After the pump, so a setup that arrived in this frame starts its round in this frame. */
    mp_round_pump();
    /* And the frame boundary the scoreboard measures its once-per-frame guard against. This runs
     * AFTER the frame the board drew into, because the frame hook sits behind the engine's frame
     * end and the board is drawn from the first message inside it. */
    mp_hud_pump();
    /* The chat box counts the same boundary, and the chat's input looks at the same world: a menu,
     * a movie or a level that ended closes it here, and the key is read again once a second. */
    mp_chat_draw_frame();
    mp_chat_input_frame();
    /* A session's pause menu looks at the world it stands over: after the follow, which may just
     * have ended the level, and before the re-entry feed and the respawn, so a death is seen
     * before a quick re-entry stands the player up again. */
    mp_pause_frame();
    feed_reentry();
    /* After the scene gate, so a frame sets both from the same reading of the setup note. */
    file_the_movie_gate();
    /* And the wish itself, which is retried, carried out or dropped here rather than at the moment
     * it was made: both engine gates it waits for are shut for the whole of a level load. */
    mp_respawn_tick(mp_bridge_drain_substeps());
    /* Before mp_start_tick, so a seat handed over in this frame is taken in this frame
     * rather than in the next one. */
    mp_arrival_tick(mp_bridge_drain_substeps());
    /* The load-by-name flag is the engine's own and transient: the campaign round never clears it,
     * so a flag left standing would reload the same path at the start of every later round. This
     * clears it the moment the game mode says a level is running. */
    mp_start_tick();
    /* Last, because it may take the transport down, and everything above has then read the
     * session once more on the frame it ends. */
    mp_session_over_exit_tick();
}

static void CALLBACK bridge_timer_pump(HWND hwnd, UINT message, UINT_PTR id, DWORD time)
{
    (void)hwnd;
    (void)message;
    (void)id;
    (void)time;
    mp_bridge_pump_idle(MP_BRIDGE_PUMP_TIMER);
    /* The one pump that runs while a movie is on screen, so the gate is filed here as well, and a
     * chat left open is closed for it. */
    file_the_movie_gate();
    mp_chat_input_look();
    mp_session_over_exit_tick();   /* a level load draws no frame, and an exit may still wait */
}

/* A thread timer belongs to the thread that creates it and fires only when that thread
 * dispatches messages. This runs at the host's entry point on the thread that will run the
 * engine's window and message loop, which is what makes the timer land in the engine's pump. */
void mp_pumps_arm(void)
{
    static bool frame_hooked = false;
    UINT_PTR    timer;

    /* The scene watch, on the session's way in and never at the DLL's load, which is the only
     * reason it is armed here: a single player game never comes this way. It reads the target
     * resolver's answers, and the arming has installed the resolver before it gets here. The
     * gathering and the mirror after it, with their hull on the lock's release, for the same
     * reason and in this order: the watch's doors are what begin a gathering. */
    (void)mp_scene_watch_install();
    (void)mp_scene_install();
    /* The chat's hook and cells once for the process, and its key read on every way in. */
    (void)mp_chat_input_arm();

    /* The frame hook once for the process, because it has no removal and a second arming used to
     * hang a second pump on every frame. The timer is the exit's to kill, so it is set again. */
    if (!frame_hooked) {
        frame_hooked = frame_hook_add(&bridge_frame_pump);
        if (!frame_hooked) {
            log_warning("the frame hook is unavailable, so the session is pumped only from the "
                        "substeps and the timer");
        }
    }
    timer = SetTimer(NULL, 0, BRIDGE_PUMP_TIMER_MS, &bridge_timer_pump);
    if (timer == 0u) {
        log_warning("the pump timer could not be created (error %lu); the session is pumped "
                    "only from the substeps and the frame hook, so a level load longer than the "
                    "connected timeout drops the peer", (unsigned long)GetLastError());
        return;
    }
    mp_bridge_note_pump_timer((uintptr_t)timer);
    log_info("the session is pumped between substeps from the frame hook and from a %u ms "
             "thread timer", (unsigned)BRIDGE_PUMP_TIMER_MS);
}
