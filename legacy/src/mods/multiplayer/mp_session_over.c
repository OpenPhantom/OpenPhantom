/* mp_session_over.c: the end of a session, carried out. See the header for the decision and for
 * why the two cells are written in the order they are written.
 */
#include "mp_session_over.h"
#include "mp_text.h"

#include "mp_actions.h"
#include "mp_armed.h"
#include "mp_arrival.h"
#include "mp_bank.h"
#include "mp_body.h"
#include "mp_bridge.h"
#include "mp_bridge_drain.h"
#include "mp_bridge_lobby.h"
#include "mp_bridge_savefile.h"
#include "mp_bridge_world.h"
#include "mp_cells.h"
#include "mp_chat.h"
#include "mp_chat_input.h"
#include "mp_cutscene.h"
#include "mp_damage.h"
#include "mp_enemy_sync.h"
#include "mp_follow.h"
#include "mp_npc_copies_bridge.h"
#include "mp_puppet.h"
#include "mp_respawn.h"
#include "mp_reentry.h"
#include "mp_round.h"
#include "mp_start.h"
#include "mp_wallclock.h"
#include "mp_world.h"

#include "common/logging.h"
#include "common/patch.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* What the campaign round reads out of the game mode cell. Three is "this level is complete";
 * paired with the restore flag it means "and go back to the title screen". The round spins in
 * `while (outcome == 2)` and then branches: 3 fades half a second and moves to the next level,
 * 1 runs the closing credits, 4 to 10 are the death screen's continue choices, and only after
 * that does it look at the restore flag. So 3 is the one value that leaves a level cleanly. */
#define GAME_MODE_LEVEL_DONE 3u

typedef struct session_over {
    mp_lobby_over_t reason;      /* what ended it, and the latch that makes this idempotent */
    bool            cells_written;
    uint32_t        ends;        /* sessions ended, all reasons */
    uint32_t        cell_refusals;
    uint32_t        announces;

    /* The one exit. */
    bool            closing;          /* the first half is said and the transport still stands */
    mp_exit_why_t   closing_why;
    uint32_t        closing_since_ms;
    bool            level_seen;       /* a level of this session has begun on this side */
    mp_lobby_over_t notice;           /* what the title tells the player, NO while nothing */
    uint32_t        exits[MP_EXIT_WHY_COUNT];
    uint32_t        finished;
    uint32_t        lingered;         /* finishes that stopped waiting for an acknowledgement */
    void          (*report)(const char *why);
    void          (*unarmed)(void);
} session_over_t;

static session_over_t so;

/* ==============================================================================================
 * Clearing up.
 * ============================================================================================ */

/* Everything that outlives a session and must not. The order is the whole content of this
 * function, and it has exactly one hard constraint: the second body goes first. It is an engine
 * object in a world the engine is about to free, and a teardown attempted after that walks a
 * pointer into released memory. Everything below it is this feature's own memory and does not
 * care when it is cleared. */
static void clear_what_outlives_a_session(bool keep_hosting)
{
    /* A host that goes on hosting keeps its copies: whoever owned one hands it to the host, which
     * the copies' own census sees. Anybody else starts the copies' next epoch here. */
    if (!keep_hosting) {
        mp_npc_copies_bridge_new_world();
    }

    /* The far player's body, while there is still a world holding it. This is also the only
     * caller this function has ever had: until now the mod believed the puppet was still standing
     * after every world change, because nothing ever put the flag back. */
    mp_session_over_take_bodies_down();

    /* The switch that suppresses a co-op death. This is the line that unsticks a dead client:
     * with it left on and no far side to be respawned beside, a dead player gets no respawn, no
     * death screen and no pause menu, and the only way out of the game is to kill the process. */
    mp_reentry_note_no_session();
    mp_damage_set_survives_death(false);

    mp_respawn_cancel();
    mp_start_cancel_pose();
    mp_round_end_session();

    /* The far side's indices, histories and queued events. A queued mover event is the sharpest
     * of them: nothing in the queue names the level it was made in, so one left standing over a
     * world change opens the door of the same number in the NEXT level. */
    mp_world_clear();
    mp_actions_clear();
    mp_puppet_reset_all();
    mp_bridge_world_reset();
    mp_enemy_sync_release_all();

    /* What the host keeps. A host that is still standing in its own level is still hosting it:
     * its repeated setup note is the only thing that can tell somebody who connects now what is
     * being played and that it has already started, and clearing it would leave a late joiner
     * waiting on a host that never chooses again. A client has no such thing to keep. */
    if (!keep_hosting) {
        mp_bridge_lobby_reset();
        mp_chat_input_close(MP_CHAT_CLOSE_SESSION);   /* the line being typed goes with it */
        mp_chat_forget();   /* the lines of this session, never kept past it */
    }
}

/* ==============================================================================================
 * Telling the engine.
 * ============================================================================================ */

/* The two cells are the restore flag at 0x00881340 and the game mode cell at 0x00881368. The
 * restore flag is read out of the clearing store at the top of the campaign round,
 *
 *     0043EBD6  C7 05 40 13 88 00 00 00 00 00   mov dword [0x881340], 0
 *
 * whose position at the top of the round makes the site, since other instructions name the
 * cell with a full operand as well, the leave-level choice's store at 0043FAFD among them; the
 * pattern around it matches exactly once in each of the five retail builds. */
static bool write_cell(mp_cell_t cell, uint32_t value)
{
    uintptr_t address = mp_cells_address(cell);

    if (address == 0u || patch_write_u32(address, value) != PATCH_RESULT_OK) {
        ++so.cell_refusals;
        return false;
    }
    return true;
}

/* The restore flag first, then the outcome, because the outcome is the trigger and it reads
 * better to set up what is being triggered before triggering it.
 *
 * It is NOT a race, and an earlier version of this comment claimed it was. The engine's own
 * "leave level" choice writes the same pair the other way round and works, for two reasons that
 * both hold here: nothing re-enters the engine between these two writes, so the campaign loop
 * cannot observe the half-written state at all, and it reads the restore flag a good half second
 * later anyway, after the fade. Either order is correct. */
static void tell_the_engine_to_leave(void)
{
    if (!write_cell(MP_CELL_RESTORE_PENDING, 1u)) {
        log_warning("the restore flag could not be written, so the session ends without leaving "
                    "the level; the players are on their own");
        return;
    }
    if (!write_cell(MP_CELL_GAME_MODE, GAME_MODE_LEVEL_DONE)) {
        log_warning("the level outcome could not be written; the restore flag is set, so the "
                    "level will end at its own end rather than now");
        return;
    }
    so.cells_written = true;
}

/* ==============================================================================================
 * The entry points.
 * ============================================================================================ */

const char *mp_session_over_text(mp_lobby_over_t reason)
{
    switch (reason) {
    case MP_LOBBY_OVER_HOST_ENDED: return mp_text(MP_TEXT_OVER_HOST_ENDED);
    case MP_LOBBY_OVER_HOST_LOST:  return mp_text(MP_TEXT_OVER_HOST_LOST);
    case MP_LOBBY_OVER_ALL_LEFT:   return mp_text(MP_TEXT_OVER_ALL_LEFT);
    case MP_LOBBY_OVER_CONTENT:    return mp_text(MP_TEXT_CONTENT_MISMATCH);
    case MP_LOBBY_OVER_BEHIND:     return mp_text(MP_TEXT_OVER_BEHIND);
    case MP_LOBBY_OVER_NO:
    default:                       return NULL;
    }
}

mp_lobby_over_t mp_session_over_reason(void)
{
    return so.reason;
}

void mp_session_over_forget(void)
{
    so.reason        = MP_LOBBY_OVER_NO;
    so.cells_written = false;
}

/* Idempotent: the second call in the same session does nothing, which is what lets the tick keep
 * calling while the engine takes the several frames it needs to fade the level out. It is static
 * because the tick below is the only thing that ever decides a session is over; offering it in
 * the header would have been an entry point nobody used. */
static void session_is_over(mp_lobby_over_t reason)
{
    const char *text;

    if (reason == MP_LOBBY_OVER_NO || so.reason != MP_LOBBY_OVER_NO) {
        return;   /* nothing to do, or already done: the tick keeps calling while the level fades */
    }
    so.reason = reason;
    ++so.ends;
    text = mp_session_over_text(reason);

    /* A host does not lose its level because a player left, and it used to. The two
     * endings a CLIENT can see mean the world it was standing in has no authority any more, and
     * there is nothing to do with it but leave. The one a HOST sees means only that it is alone:
     * it owns the level, it is still in it, and a player who quit the lobby next door is no
     * reason to throw it back to the title screen in the middle of its own game. So the far
     * player's state goes, the body goes, and the level stays. A peer that connects again lifts
     * the ending, which is what the tick below already does. */
    if (reason == MP_LOBBY_OVER_ALL_LEFT) {
        log_info("the last other player left (%s); this side keeps its own level and goes on "
                 "hosting it", text != NULL ? text : "no reason given");
        clear_what_outlives_a_session(true);
        return;
    }

    log_info("the session is over (%s); returning to the title screen",
             text != NULL ? text : "no reason given");

    clear_what_outlives_a_session(false);
    tell_the_engine_to_leave();
    /* And the session itself, by the one exit. Its transport comes down once the level has let
     * go, and the reason is carried to the title screen. */
    mp_session_over_exit(MP_EXIT_WHY_VERDICT);
}

void mp_session_over_announce(void)
{
    /* The bridge refuses this unless it is a host with a session, so the role is not asked here.
     * Counting it here rather than there keeps the number beside the rest of this module's. */
    if (!mp_bridge_lobby_ended()) {
        mp_bridge_lobby_end_session();
        if (mp_bridge_lobby_ended()) {
            ++so.announces;
        }
    }
}

void mp_session_over_tick(void)
{
    mp_lobby_over_t verdict;

    if (so.reason != MP_LOBBY_OVER_NO) {
        /* An ending is not a life sentence. A host that leaves its level and hosts again sends a
         * fresh setup with the ended word gone, and a client that is still in the same process
         * has to be able to play it. Until this line the first ending in a run was the last
         * session that run could have: the reason latched, the tick returned here for good, and
         * everything the ending cleared, the round, the survival switch, the re-entry rules,
         * stayed cleared while the players went on playing.
         *
         * Three things have to be true together, and the last one is what keeps this from firing
         * during the fade the ending itself starts: the word is gone, somebody is on the wire,
         * and a level is running again. */
        if (!mp_bridge_lobby_ended() && mp_bridge_joined() && mp_start_level_running()) {
            log_info("the session is on again, so the ending is forgotten");
            mp_session_over_forget();
        }
        return;
    }
    verdict = mp_bridge_lobby_session_over(mp_start_level_running());
    if (verdict != MP_LOBBY_OVER_NO) {
        session_is_over(verdict);
    }
}

/* ==============================================================================================
 * The one exit.
 * ============================================================================================ */

void mp_session_over_take_bodies_down(void)
{
    size_t bank;

    for (bank = 1u; bank <= MP_BANK_FAR_MAX; ++bank) {
        (void)mp_body_teardown_at(bank);
    }
}

void mp_session_over_set_listeners(void (*report)(const char *why), void (*unarmed)(void))
{
    so.report  = report;
    so.unarmed = unarmed;
}

/* The second half, in the order its pieces stand on each other: the report while the bridge can
 * still describe itself; the goodbye, the lobby and the game back to co-op; the start, the follow,
 * the host's savegame offer and the scene hold; the transport and its timer; and last this
 * module's record and what the arming wrote, which the listener gives back. */
static void finish(uint32_t pending)
{
    mp_exit_why_t why = so.closing_why;

    so.closing = false;
    ++so.finished;
    if (pending != 0u) {
        ++so.lingered;
    }
    if (so.report != NULL) {
        so.report("the end of a session");
    }
    mp_bridge_lobby_withdraw();
    mp_start_cancel();
    mp_follow_forget();
    mp_bridge_savefile_withdraw();
    mp_cutscene_set_client_holds_back(false);
    (void)mp_bridge_uninstall_udp();
    so.level_seen    = false;
    so.reason        = MP_LOBBY_OVER_NO;
    so.cells_written = false;
    if (so.unarmed != NULL) {
        so.unarmed();
    }
    log_info("the session is over (%s)%s: the transport is down, nothing of the session runs any "
             "more, and this side plays single player until a lobby is opened again",
             mp_exit_why_name(why), pending != 0u ? ", after a last word nobody acknowledged" : "");
}

void mp_session_over_exit(mp_exit_why_t why)
{
    bool is_client;

    /* The loopback is the ini's test harness: both of its ends are this process, and there is no
     * transport a menu could take down. */
    if (!mp_armed_transport() || !mp_bridge_drain_is_udp() || so.closing ||
        (unsigned)why >= (unsigned)MP_EXIT_WHY_COUNT) {
        return;
    }
    is_client           = mp_bridge_drain_is_client();
    so.closing          = true;
    so.closing_why      = why;
    so.closing_since_ms = mp_wallclock_ms();
    ++so.exits[why];

    /* What the title tells the player, decided while the facts it is made of still stand: the
     * lobby reset below forgets the host's word. An ending the player chose is told nothing, and a
     * lobby that heard its host go tells its player itself. */
    if (why == MP_EXIT_WHY_LEFT_LEVEL || why == MP_EXIT_WHY_AT_TITLE ||
        why == MP_EXIT_WHY_VERDICT || why == MP_EXIT_WHY_START_GIVEN_UP) {
        bool            goodbye = mp_bridge_lobby_host_left();
        mp_lobby_over_t notice  = mp_exit_notice(is_client, so.reason, mp_bridge_lobby_ended(),
                                                 goodbye, mp_bridge_lobby_gave_up() && !goodbye,
                                                 mp_bridge_lobby_sent_away());

        if (notice != MP_LOBBY_OVER_NO) {
            so.notice = notice;
        }
    }
    /* The host's last word, in a lobby nobody started as much as in a level: a client next door is
     * told the session is over rather than left to find it out from a silence. */
    if (!is_client) {
        mp_session_over_announce();
    }
    clear_what_outlives_a_session(false);
    log_info("the session ends: %s. %s", mp_exit_why_name(why),
             is_client ? "The host is told goodbye once this side's level has let go"
                       : "The clients are told it is over; the transport comes down once no "
                         "level stands and they have heard it, or after a second");
    mp_session_over_exit_tick();
}

void mp_session_over_exit_tick(void)
{
    uint32_t pending;

    if (!so.closing) {
        return;
    }
    /* A level the verdict told to leave fades until its end arrives, and the world it fades is the
     * one the first half cleared, so the transport waits for that end as well. */
    pending = mp_bridge_reliable_pending();
    if (mp_exit_step(true, mp_start_level_running() || so.cells_written, pending,
                     mp_wallclock_ms() - so.closing_since_ms) == MP_EXIT_STEP_FINISH) {
        finish(pending);
    }
}

void mp_session_over_finish_now(void)
{
    if (so.closing) {
        finish(mp_bridge_reliable_pending());
    }
}

bool mp_session_over_exit_waits(void)
{
    return so.closing;
}

void mp_session_over_note_level_begin(void)
{
    mp_lobby_setup_t setup;

    /* A level that begins while an exit still waits is one this side began on its own: the
     * session the exit waited on is not in it. */
    if (so.closing) {
        mp_session_over_finish_now();
        return;
    }
    if (mp_bridge_lobby_setup(&setup) && (setup.flags & MP_LOBBY_F_STARTED) != 0u) {
        so.level_seen = true;
    }
}

const char *mp_session_over_at_title(bool menu_armed)
{
    mp_exit_title_facts_t facts;
    mp_lobby_over_t       notice;

    if (mp_armed_transport()) {
        memset(&facts, 0, sizeof facts);
        facts.title_shown    = true;
        facts.menu_armed     = menu_armed;
        facts.closing        = so.closing;
        facts.start_pending  = mp_start_pending();
        facts.following      = mp_bridge_drain_is_client() && mp_follow_leaving();
        facts.host_connected = mp_bridge_lobby_connected();
        facts.ended          = mp_bridge_lobby_ended();
        facts.level_seen     = so.level_seen;
        if (mp_exit_title_door(&facts)) {
            mp_session_over_exit(MP_EXIT_WHY_AT_TITLE);
        }
    }
    notice    = so.notice;
    so.notice = MP_LOBBY_OVER_NO;
    return mp_session_over_text(notice);
}

void mp_session_over_report(void)
{
    const char *text = mp_session_over_text(so.reason);

    log_info("  sessions ended %u, announced %u, cell refusals %u%s%s", (unsigned)so.ends,
             (unsigned)so.announces, (unsigned)so.cell_refusals,
             text != NULL ? ", this one: " : "", text != NULL ? text : "");
    log_info("  the exits: %u lobby left, %u lobby ended by its host, %u level left, %u at the "
             "title, %u start given up, %u with the level's verdict, %u for a new transport; %u "
             "finished, %u of them after a last word nobody acknowledged%s",
             (unsigned)so.exits[MP_EXIT_WHY_LOBBY_BACK], (unsigned)so.exits[MP_EXIT_WHY_HOST_GONE],
             (unsigned)so.exits[MP_EXIT_WHY_LEFT_LEVEL], (unsigned)so.exits[MP_EXIT_WHY_AT_TITLE],
             (unsigned)so.exits[MP_EXIT_WHY_START_GIVEN_UP],
             (unsigned)so.exits[MP_EXIT_WHY_VERDICT],
             (unsigned)so.exits[MP_EXIT_WHY_NEW_TRANSPORT], (unsigned)so.finished,
             (unsigned)so.lingered, so.closing ? "; ONE IS STILL WAITING" : "");
    if (so.reason != MP_LOBBY_OVER_NO && !so.cells_written &&
        so.reason != MP_LOBBY_OVER_ALL_LEFT) {
        log_warning("  the session ended but the engine was never told to leave the level: the "
                    "players are standing in a world with no session");
    }
}
