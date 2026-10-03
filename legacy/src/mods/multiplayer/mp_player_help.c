/* mp_player_help.c: the two buttons of the developer menu's Multiplayer heading, heard and
 * answered. See the header.
 *
 * Two things about the code are worth having in front of a maintainer:
 *
 * The answer is a record with one writer, kept here as it should stand on file and filed again
 * until the channel takes it. It always answers the newest press: a teleport that ends after a
 * later press was answered is counted, its own lines say how it ended, and the record is left
 * alone, because the overlay reads an answer only against its last press.
 *
 * That somebody listens is said from the first frame of the pump after an arming and not at the
 * arming. The bits tell the overlay that a press would be read, and a frame of the pump is the
 * proof that the reader runs; what the reader can do rests on bindings that are installed on the
 * session's way in, after the transport went up and before the pump is hooked.
 */
#include "mp_player_help.h"

#include "mp_bridge_drain.h"
#include "mp_player_help_rule.h"
#include "mp_repair_lock.h"
#include "mp_scene_free.h"
#include "mp_teleport_host.h"
#include "mp_wallclock.h"

#include "common/logging.h"
#include "common/player_help_note.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct help_counts {
    uint32_t arms;
    uint32_t old_presses;         /* armings that found a press older than their session */
    uint32_t reads;               /* looks at the ask */
    uint32_t misses;              /* of those, the ones that found no ask on file */
    uint32_t filed;               /* answers the channel took */
    uint32_t file_refused;
    uint32_t withdrawals;
    uint32_t open_at_exit;        /* answers still open at a session's end, ended there */

    uint32_t repairs;
    uint32_t repairs_nothing;

    uint32_t teleports;
    uint32_t teleports_landed;
    uint32_t teleports_nothing;
    uint32_t teleports_refused[PLAYER_HELP_REASON_COUNT];
    uint32_t late_ends;           /* a teleport's end, with a newer press answered since */
} help_counts_t;

typedef struct help_state {
    bool                 armed;
    uint32_t             mark;           /* a press up to this serial is not acted on */
    bool                 ask_found;      /* a read has found the overlay's note in this process */
    bool                 ask_tried;
    uint32_t             ask_tried_ms;
    bool                 ready_said;     /* since this arming */
    player_help_answer_t answer;         /* as it should stand on file */
    bool                 dirty;          /* and it does not yet */
    bool                 file_waits;     /* the channel refused it: tried again a second later */
    uint32_t             file_refused_ms;
    bool                 said_refusal;
    uint32_t             teleport_serial;   /* the press the teleport under way answers */
    help_counts_t        n;
} help_state_t;

static help_state_t help;

/* ==============================================================================================
 * The answer.
 * ============================================================================================ */

static bool file_the_answer(void)
{
    help.answer.version = PLAYER_HELP_NOTE_VERSION;
    if (player_help_answer_publish(&help.answer)) {
        help.dirty      = false;
        help.file_waits = false;
        ++help.n.filed;
        return true;
    }
    ++help.n.file_refused;
    help.file_waits      = true;
    help.file_refused_ms = mp_wallclock_ms();
    if (!help.said_refusal) {
        help.said_refusal = true;
        log_warning("the player's help: the channel refused the answer, so the developer menu "
                    "goes on showing the two buttons as it last read them; it is filed again "
                    "once a second until it is taken");
    }
    return false;
}

static void answer(uint32_t serial, uint8_t kind, mp_player_help_verdict_t verdict,
                   uint16_t released)
{
    help.answer.serial   = serial;
    help.answer.kind     = kind;
    help.answer.outcome  = verdict.outcome;
    help.answer.reason   = verdict.reason;
    help.answer.released = released;
    help.dirty           = true;
    (void)file_the_answer();
}

/* What the reader can do, said on the first frame after an arming and whenever it changes. */
static void say_who_listens(void)
{
    const bool    repair   = mp_scene_free_install();
    const bool    client   = mp_bridge_drain_is_client();
    const bool    teleport = mp_teleport_host_bound();
    const uint8_t ready    = mp_player_help_ready(repair, client, teleport);

    if (help.ready_said && ready == help.answer.ready) {
        return;
    }
    help.ready_said   = true;
    help.answer.ready = ready;
    help.dirty        = true;
    log_info("the player's help listens for the two buttons of the developer menu: repair lock "
             "%s, teleport to host %s; a press up to number %u is older than this session and "
             "is left alone",
             repair ? "is carried out" : "is not offered, the lock's release is not bound",
             !client ? "is not offered, this machine is the host"
                     : teleport ? "is carried out"
                                : "is not offered, the seat's probes or the scene's fade and "
                                  "modes are not bound",
             (unsigned)help.mark);
}

/* ==============================================================================================
 * The two presses.
 * ============================================================================================ */

static void count_a_teleport(mp_player_help_verdict_t verdict)
{
    switch (verdict.outcome) {
    case PLAYER_HELP_OUTCOME_DONE:
        ++help.n.teleports_landed;
        break;
    case PLAYER_HELP_OUTCOME_NOTHING:
        ++help.n.teleports_nothing;
        break;
    case PLAYER_HELP_OUTCOME_REFUSED:
        ++help.n.teleports_refused[verdict.reason < PLAYER_HELP_REASON_COUNT
                                       ? verdict.reason : PLAYER_HELP_REASON_NONE];
        break;
    default:
        break;   /* open: counted as it ends */
    }
}

/* A teleport under way ended from outside, by a repair or by the session's end. True when one
 * was under way; it is counted as refused, for the reason `reason` receives. */
static bool leave_the_teleport(const char *why, uint8_t *reason)
{
    mp_player_help_verdict_t ended;

    ended.outcome = PLAYER_HELP_OUTCOME_REFUSED;
    ended.reason  = PLAYER_HELP_REASON_GAVE_UP;
    if (!mp_teleport_host_leave(why, &ended.reason)) {
        return false;
    }
    count_a_teleport(ended);
    if (reason != NULL) {
        *reason = ended.reason;
    }
    return true;
}

static void take_a_teleport(uint32_t serial, bool overlay_holds, uint32_t substeps)
{
    const mp_player_help_verdict_t verdict = mp_teleport_host_ask(overlay_holds, substeps);

    ++help.n.teleports;
    if (verdict.outcome == PLAYER_HELP_OUTCOME_OPEN) {
        help.teleport_serial = serial;
    }
    count_a_teleport(verdict);
    answer(serial, PLAYER_HELP_KIND_TELEPORT, verdict, 0u);
}

/* The end of the teleport under way, answered to the press that began it, unless a newer press
 * has been answered since: a second press of the same button, refused as busy. */
static void end_the_teleport(mp_player_help_verdict_t ended)
{
    count_a_teleport(ended);
    if (help.answer.kind == (uint8_t)PLAYER_HELP_KIND_TELEPORT &&
        help.answer.serial == help.teleport_serial) {
        answer(help.teleport_serial, PLAYER_HELP_KIND_TELEPORT, ended, 0u);
        return;
    }
    ++help.n.late_ends;
}

/* A repair ends a teleport under way before anything else: the player asked to be let go. The
 * repair's answer then stands in the record, so the teleport's open answer ends with it. */
static void take_a_repair(uint32_t serial, bool overlay_holds, uint32_t substeps)
{
    const bool               left = leave_the_teleport("a repair lock was pressed", NULL);
    uint16_t                 released = 0u;
    mp_player_help_verdict_t verdict;

    verdict = mp_repair_lock_press(overlay_holds, left, substeps, &released);
    ++help.n.repairs;
    help.n.repairs_nothing += verdict.outcome == PLAYER_HELP_OUTCOME_NOTHING ? 1u : 0u;
    answer(serial, PLAYER_HELP_KIND_REPAIR, verdict, released);
}

/* ==============================================================================================
 * The reader.
 * ============================================================================================ */

/* A press to act on, when the ask on file is one made after the mark. The mark moves with it,
 * so the same press read again on the next frame is not a new one. */
static bool read_a_press(player_help_ask_t *ask)
{
    const uint32_t now = mp_wallclock_ms();

    if (!mp_player_help_read_due(help.ask_found, help.ask_tried, now - help.ask_tried_ms)) {
        return false;
    }
    help.ask_tried    = true;
    help.ask_tried_ms = now;
    ++help.n.reads;
    if (!player_help_ask_read(ask)) {
        ++help.n.misses;
        return false;
    }
    help.ask_found = true;
    if (!mp_player_help_is_new(ask->kind, ask->serial, help.mark)) {
        return false;
    }
    help.mark = ask->serial;
    return true;
}

void mp_player_help_arm(void)
{
    player_help_ask_t ask;
    bool              found;

    memset(&ask, 0, sizeof ask);
    found = player_help_ask_read(&ask);
    help.armed        = true;
    help.mark         = mp_player_help_mark(found, ask.serial);
    help.ready_said   = false;
    help.ask_found    = help.ask_found || found;
    help.ask_tried    = true;
    help.ask_tried_ms = mp_wallclock_ms();
    ++help.n.arms;
    help.n.old_presses += help.mark != 0u ? 1u : 0u;
}

void mp_player_help_frame(uint32_t substeps)
{
    player_help_ask_t        ask;
    mp_player_help_verdict_t ended;

    if (!help.armed) {
        return;
    }
    say_who_listens();
    memset(&ask, 0, sizeof ask);
    if (read_a_press(&ask)) {
        const bool overlay_holds = (ask.flags & PLAYER_HELP_ASK_F_OVERLAY_HOLDS) != 0u;

        if (ask.kind == (uint8_t)PLAYER_HELP_KIND_REPAIR) {
            take_a_repair(ask.serial, overlay_holds, substeps);
        } else {
            take_a_teleport(ask.serial, overlay_holds, substeps);
        }
    }
    /* After the press, so a teleport taken on this frame has its first look on this frame. */
    if (mp_teleport_host_frame(substeps, &ended)) {
        end_the_teleport(ended);
    }
    mp_repair_lock_frame(substeps);
    if (help.dirty && (!help.file_waits ||
                       mp_wallclock_ms() - help.file_refused_ms >= PLAYER_HELP_NOTE_RETRY_MS)) {
        (void)file_the_answer();
    }
}

void mp_player_help_withdraw(void)
{
    uint8_t reason = PLAYER_HELP_REASON_GAVE_UP;
    bool    left;

    if (!help.armed) {
        return;
    }
    help.armed = false;
    ++help.n.withdrawals;
    left = leave_the_teleport("the session ended", &reason);
    mp_repair_lock_session_ended();
    /* An answer left open would grey its button into the next session, whether or not the
     * teleport it answered was still found under way. */
    if (help.answer.outcome == (uint8_t)PLAYER_HELP_OUTCOME_OPEN) {
        help.answer.outcome = PLAYER_HELP_OUTCOME_REFUSED;
        help.answer.reason  = reason;
        ++help.n.open_at_exit;
    }
    help.answer.ready = 0u;
    help.dirty        = true;
    if (!file_the_answer()) {
        log_warning("the player's help could not be withdrawn: the channel refused the answer, "
                    "so the developer menu goes on offering the two buttons with nobody "
                    "listening for them");
        return;
    }
    log_info("the player's help is withdrawn with the session: nobody listens for the two "
             "buttons of the developer menu any more%s",
             left ? ", and the teleport that was under way is ended" : "");
}

/* ==============================================================================================
 * The report.
 * ============================================================================================ */

void mp_player_help_report(void)
{
    const help_counts_t *n        = &help.n;
    const uint32_t      *refused  = n->teleports_refused;
    uint32_t             refusals = 0u;
    size_t               reason;

    for (reason = 0u; reason < (size_t)PLAYER_HELP_REASON_COUNT; ++reason) {
        refusals += refused[reason];
    }
    log_info("  the player's help: %u repair(s) asked, %u found nothing to do, %u teleport(s) "
             "asked, %u landed, %u refused (%u no level, %u dead, %u at a gun, %u busy, %u the "
             "host in another level, %u the host with no body, %u the host dead, %u no free "
             "place, %u held by the developer menu, %u not bound, %u gave up, %u the level "
             "changed)",
             (unsigned)n->repairs, (unsigned)n->repairs_nothing, (unsigned)n->teleports,
             (unsigned)n->teleports_landed, (unsigned)refusals,
             (unsigned)refused[PLAYER_HELP_REASON_NO_LEVEL],
             (unsigned)refused[PLAYER_HELP_REASON_DEAD],
             (unsigned)refused[PLAYER_HELP_REASON_AT_A_GUN],
             (unsigned)refused[PLAYER_HELP_REASON_BUSY],
             (unsigned)refused[PLAYER_HELP_REASON_HOST_ELSEWHERE],
             (unsigned)refused[PLAYER_HELP_REASON_HOST_HAS_NO_BODY],
             (unsigned)refused[PLAYER_HELP_REASON_HOST_DEAD],
             (unsigned)refused[PLAYER_HELP_REASON_NO_SEAT],
             (unsigned)refused[PLAYER_HELP_REASON_OVERLAY_HOLDS],
             (unsigned)refused[PLAYER_HELP_REASON_NOT_BOUND],
             (unsigned)refused[PLAYER_HELP_REASON_GAVE_UP],
             (unsigned)refused[PLAYER_HELP_REASON_WORLD_CHANGED]);
    log_info("  the player's help, the reader: armed %u time(s), %u of them with a press older "
             "than the session on file, %u look(s) at the ask, %u of them found none; %u "
             "teleport(s) had nothing to do, %u ended after a newer press was answered; %u "
             "answer(s) filed, %u refused by the channel; withdrawn %u time(s), %u answer(s) "
             "still open ended there%s",
             (unsigned)n->arms, (unsigned)n->old_presses, (unsigned)n->reads,
             (unsigned)n->misses, (unsigned)n->teleports_nothing, (unsigned)n->late_ends,
             (unsigned)n->filed, (unsigned)n->file_refused, (unsigned)n->withdrawals,
             (unsigned)n->open_at_exit, help.armed ? "; it listens now" : "");
    mp_repair_lock_report();
    mp_teleport_host_report();
}
