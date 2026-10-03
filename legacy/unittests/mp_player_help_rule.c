/* The two buttons of the developer menu, as rules: the reader, and the teleport to the host.
 *
 * Two things can go wrong with a button another DLL presses that no compiler sees. A press made
 * before a session began fires when the session arms its reader, and the player is moved or let
 * go by something he pressed in single player; the mark is what prevents it, and it is walked
 * here the way a session reads it, frame after frame. And a button that can refuse refuses for
 * a reason the player is shown, so every row of the teleport's table is held to its own reason
 * and to its place in the order.
 *
 * The search is held to the one thing that would put a player at the level's start: a place
 * the seat search answers with after it has moved on to its fallback is never used.
 *
 * The rules of the repair are in mp_player_help_repair.c.
 */
#include "unittest.h"

#include "mp_player_help_rule.h"

#include "common/player_help_note.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* ==============================================================================================
 * The reader.
 * ============================================================================================ */

/* One frame of a session's reader: the ask on file is acted on when it is new, and the mark
 * moves with it. Answers whether it was acted on. */
static bool frame(uint32_t *mark, uint8_t kind, uint32_t serial)
{
    if (!mp_player_help_is_new(kind, serial, *mark)) {
        return false;
    }
    *mark = serial;
    return true;
}

static void check_the_mark(void)
{
    uint32_t mark;
    unsigned fired = 0u;
    unsigned i;

    ut_section("the mark: a press of before the session never fires, a new one fires once");

    ut_check(mp_player_help_mark(false, 77u) == 0u, "no ask on file at the arming: a mark of 0");
    ut_check(mp_player_help_mark(true, 0u) == 0u, "the empty ask of the overlay: a mark of 0");
    ut_check(mp_player_help_mark(true, 7u) == 7u, "a press on file: its serial is the mark");

    mark = mp_player_help_mark(true, 7u);
    for (i = 0u; i < 1000u; ++i) {
        fired += frame(&mark, PLAYER_HELP_KIND_REPAIR, 7u) ? 1u : 0u;
        fired += frame(&mark, PLAYER_HELP_KIND_TELEPORT, 7u) ? 1u : 0u;
    }
    ut_check(fired == 0u && mark == 7u,
             "press 7 was made before the session: read for a thousand frames it never fires");
    ut_check(!frame(&mark, PLAYER_HELP_KIND_TELEPORT, 6u) &&
                 !frame(&mark, PLAYER_HELP_KIND_TELEPORT, 1u),
             "and neither does an older press that turns up again");

    ut_check(frame(&mark, PLAYER_HELP_KIND_REPAIR, 8u) && mark == 8u,
             "press 8, made in the session, fires");
    fired = 0u;
    for (i = 0u; i < 1000u; ++i) {
        fired += frame(&mark, PLAYER_HELP_KIND_REPAIR, 8u) ? 1u : 0u;
    }
    ut_check(fired == 0u, "and read again for a thousand frames it does not fire a second time");
    ut_check(frame(&mark, PLAYER_HELP_KIND_TELEPORT, 9u) &&
                 !frame(&mark, PLAYER_HELP_KIND_TELEPORT, 9u),
             "the next press fires once as well");

    mark = mp_player_help_mark(false, 0u);
    ut_check(!frame(&mark, PLAYER_HELP_KIND_NONE, 0u),
             "a session armed with no ask on file: the empty ask filed later is no press");
    ut_check(frame(&mark, PLAYER_HELP_KIND_REPAIR, 1u), "and the first press, number 1, fires");

    ut_section("the mark across the wrap of the serial, and what is no press at all");
    mark = 0xFFFFFFFEu;
    ut_check(frame(&mark, PLAYER_HELP_KIND_REPAIR, 0xFFFFFFFFu), "the last serial fires");
    ut_check(frame(&mark, PLAYER_HELP_KIND_REPAIR, 1u) && mark == 1u,
             "the press after it carries 1, never 0, and fires");
    ut_check(!frame(&mark, PLAYER_HELP_KIND_REPAIR, 0xFFFFFFFFu),
             "the serial before the wrap is an older press after it");
    ut_check(!mp_player_help_is_new(PLAYER_HELP_KIND_REPAIR, 0u, 0xFFFFFFFFu) &&
                 !mp_player_help_is_new(PLAYER_HELP_KIND_TELEPORT, 0u, 5u),
             "a serial of 0 is never a press, whatever the mark");
    ut_check(!mp_player_help_is_new(PLAYER_HELP_KIND_NONE, 9u, 5u) &&
                 !mp_player_help_is_new(3u, 9u, 5u) && !mp_player_help_is_new(0xFFu, 9u, 5u),
             "a kind that is neither button is never acted on, however new its serial");
    ut_check(mp_player_help_is_new(PLAYER_HELP_KIND_REPAIR, 0x80000004u, 5u) &&
                 !mp_player_help_is_new(PLAYER_HELP_KIND_REPAIR, 0x80000005u, 5u),
             "after the mark means less than half the counter ahead of it");
}

static void check_the_reader(void)
{
    uint8_t  reason;
    unsigned distinct = 0u;

    ut_section("the look at the ask: once a second until it was found, every frame after");
    ut_check(mp_player_help_read_due(false, false, 0u), "never tried: the first look is taken");
    ut_check(!mp_player_help_read_due(false, true, 0u) &&
                 !mp_player_help_read_due(false, true, 512u) &&
                 !mp_player_help_read_due(false, true, PLAYER_HELP_NOTE_RETRY_MS - 1u),
             "tried and not found: no look inside the second after it");
    ut_check(mp_player_help_read_due(false, true, PLAYER_HELP_NOTE_RETRY_MS) &&
                 mp_player_help_read_due(false, true, 0xFFFFFFFFu),
             "a look again at the second, and after any longer wait");
    ut_check(mp_player_help_read_due(true, true, 0u) && mp_player_help_read_due(true, false, 0u),
             "found once: every frame looks, a read is a copy from then on");

    ut_section("the ready bits: what the overlay offers each button by");
    ut_check(mp_player_help_ready(true, true, true) ==
                 (PLAYER_HELP_READY_LISTENING | PLAYER_HELP_READY_CAN_REPAIR |
                  PLAYER_HELP_READY_CAN_TELEPORT),
             "a client with everything bound listens and can do both");
    ut_check(mp_player_help_ready(true, false, true) ==
                 (PLAYER_HELP_READY_LISTENING | PLAYER_HELP_READY_CAN_REPAIR),
             "a host can repair and never teleports, whatever is bound");
    ut_check(mp_player_help_ready(false, true, true) ==
                 (PLAYER_HELP_READY_LISTENING | PLAYER_HELP_READY_CAN_TELEPORT),
             "without the lock's release bound a repair is not offered");
    ut_check(mp_player_help_ready(true, true, false) ==
                 (PLAYER_HELP_READY_LISTENING | PLAYER_HELP_READY_CAN_REPAIR),
             "without the seat's probes or the scene's fade a teleport is not offered");
    ut_check(mp_player_help_ready(false, false, false) == PLAYER_HELP_READY_LISTENING,
             "a reader that can do neither still says it listens, and offers nothing");

    ut_section("every reason has words of its own for the log");
    for (reason = 0u; reason < PLAYER_HELP_REASON_COUNT; ++reason) {
        const char *text = mp_player_help_reason_text(reason);
        uint8_t     other;
        bool        own = text != NULL && text[0] != '\0';

        for (other = 0u; own && other < reason; ++other) {
            own = strcmp(text, mp_player_help_reason_text(other)) != 0;
        }
        distinct += own ? 1u : 0u;
    }
    ut_checkf(distinct == PLAYER_HELP_REASON_COUNT, "%u of the %u reasons read differently",
              distinct, (unsigned)PLAYER_HELP_REASON_COUNT);
    ut_check(strcmp(mp_player_help_reason_text(PLAYER_HELP_REASON_COUNT),
                    mp_player_help_reason_text(PLAYER_HELP_REASON_NONE)) == 0 &&
                 strcmp(mp_player_help_reason_text(0xFFu),
                        mp_player_help_reason_text(PLAYER_HELP_REASON_NONE)) == 0,
             "a value past the list reads as no reason, never as a null");
}

/* ==============================================================================================
 * Teleport to host.
 * ============================================================================================ */

/* A client the door lets through: a level of a started session, everything bound, alive and
 * free, the host standing in this world twenty units away. */
static mp_teleport_look_t open_door(void)
{
    mp_teleport_look_t look;

    memset(&look, 0, sizeof look);
    look.is_client        = true;
    look.level_of_session = true;
    look.bound            = true;
    look.host_pose        = true;
    look.host_stands      = true;
    look.distance_known   = true;
    look.distance         = 20.0f;
    return look;
}

/* The rows of the table in their order: what is set on an open door, and what it answers. */
typedef enum row {
    ROW_IS_HOST = 0,
    ROW_NO_LEVEL,
    ROW_NOT_BOUND,
    ROW_DEAD,
    ROW_BUSY,
    ROW_TELEPORTING,
    ROW_AT_A_GUN,
    ROW_OVERLAY,
    ROW_HOST_ELSEWHERE,
    ROW_NO_POSE,
    ROW_HOST_DEAD,
    ROW_HOST_NOT_STANDING,
    ROW_NEAR,
    ROW_COUNT
} row_t;

static const struct {
    const char *name;
    uint8_t     outcome;
    uint8_t     reason;
} ROWS[ROW_COUNT] = {
    { "this machine is the host", PLAYER_HELP_OUTCOME_NOTHING, PLAYER_HELP_REASON_IS_HOST },
    { "no level of a started session", PLAYER_HELP_OUTCOME_REFUSED,
      PLAYER_HELP_REASON_NO_LEVEL },
    { "the binding is missing", PLAYER_HELP_OUTCOME_REFUSED, PLAYER_HELP_REASON_NOT_BOUND },
    { "this player is dead", PLAYER_HELP_OUTCOME_REFUSED, PLAYER_HELP_REASON_DEAD },
    { "a re-entry or a move is under way", PLAYER_HELP_OUTCOME_REFUSED,
      PLAYER_HELP_REASON_BUSY },
    { "a teleport is under way already", PLAYER_HELP_OUTCOME_REFUSED, PLAYER_HELP_REASON_BUSY },
    { "at the tripod gun", PLAYER_HELP_OUTCOME_REFUSED, PLAYER_HELP_REASON_AT_A_GUN },
    { "the developer menu holds the player", PLAYER_HELP_OUTCOME_REFUSED,
      PLAYER_HELP_REASON_OVERLAY_HOLDS },
    { "the host in another world", PLAYER_HELP_OUTCOME_REFUSED,
      PLAYER_HELP_REASON_HOST_ELSEWHERE },
    { "no pose of the host", PLAYER_HELP_OUTCOME_REFUSED, PLAYER_HELP_REASON_HOST_HAS_NO_BODY },
    { "the host dead", PLAYER_HELP_OUTCOME_REFUSED, PLAYER_HELP_REASON_HOST_DEAD },
    { "the host neither dead nor standing yet", PLAYER_HELP_OUTCOME_REFUSED,
      PLAYER_HELP_REASON_HOST_HAS_NO_BODY },
    { "nearer than four units to the host", PLAYER_HELP_OUTCOME_NOTHING,
      PLAYER_HELP_REASON_NEAR_ALREADY },
};

static void apply_row(mp_teleport_look_t *look, row_t row)
{
    switch (row) {
    case ROW_IS_HOST:           look->is_client = false; break;
    case ROW_NO_LEVEL:          look->level_of_session = false; break;
    case ROW_NOT_BOUND:         look->bound = false; break;
    case ROW_DEAD:              look->dead = true; break;
    case ROW_BUSY:              look->busy = true; break;
    case ROW_TELEPORTING:       look->teleporting = true; break;
    case ROW_AT_A_GUN:          look->at_gun = true; break;
    case ROW_OVERLAY:           look->overlay_holds = true; break;
    case ROW_HOST_ELSEWHERE:    look->host_elsewhere = true; break;
    case ROW_NO_POSE:           look->host_pose = false; break;
    case ROW_HOST_DEAD:         look->host_dead = true; break;
    case ROW_HOST_NOT_STANDING: look->host_stands = false; break;
    case ROW_NEAR:              look->distance = 3.0f; break;
    case ROW_COUNT:
    default:                    break;
    }
}

static bool answers(const mp_teleport_look_t *look, uint8_t outcome, uint8_t reason)
{
    mp_player_help_verdict_t verdict = mp_teleport_door(look);

    return verdict.outcome == outcome && verdict.reason == reason;
}

static void check_the_door(void)
{
    mp_teleport_look_t look = open_door();
    int                row;
    int                later;
    unsigned           in_order = 0u;
    unsigned           pairs    = 0u;

    ut_section("the door of a teleport: every row of the table, and their order");

    ut_check(answers(&look, PLAYER_HELP_OUTCOME_OPEN, PLAYER_HELP_REASON_NONE),
             "a living client far from a standing host: the door is open");
    ut_check(answers(NULL, PLAYER_HELP_OUTCOME_REFUSED, PLAYER_HELP_REASON_NOT_BOUND),
             "no look at all is refused, never opened");
    for (row = 0; row < ROW_COUNT; ++row) {
        look = open_door();
        apply_row(&look, (row_t)row);
        ut_checkf(answers(&look, ROWS[row].outcome, ROWS[row].reason), "%s: %s, %s",
                  ROWS[row].name,
                  ROWS[row].outcome == PLAYER_HELP_OUTCOME_NOTHING ? "nothing to do" : "refused",
                  mp_player_help_reason_text(ROWS[row].reason));
    }
    /* With two rows true at once the earlier one answers. */
    for (row = 0; row < ROW_COUNT; ++row) {
        for (later = row + 1; later < ROW_COUNT; ++later) {
            look = open_door();
            apply_row(&look, (row_t)later);
            apply_row(&look, (row_t)row);
            ++pairs;
            in_order += answers(&look, ROWS[row].outcome, ROWS[row].reason) ? 1u : 0u;
        }
    }
    ut_checkf(in_order == pairs, "of %u pairs of rows true together, %u answer with the earlier",
              pairs, in_order);

    ut_section("beside the host already: the seat's outer ring, and a distance that is none");
    look = open_door();
    look.distance = MP_TELEPORT_NEAR;
    ut_check(answers(&look, PLAYER_HELP_OUTCOME_OPEN, PLAYER_HELP_REASON_NONE),
             "at four units exactly the player is not beside the host yet");
    look.distance = nextafterf(MP_TELEPORT_NEAR, 0.0f);
    ut_check(answers(&look, PLAYER_HELP_OUTCOME_NOTHING, PLAYER_HELP_REASON_NEAR_ALREADY),
             "a hair nearer he is, and there is nothing to do");
    look.distance = 0.0f;
    ut_check(answers(&look, PLAYER_HELP_OUTCOME_NOTHING, PLAYER_HELP_REASON_NEAR_ALREADY),
             "standing in the host is beside him");
    look.distance = (float)NAN;
    ut_check(answers(&look, PLAYER_HELP_OUTCOME_OPEN, PLAYER_HELP_REASON_NONE),
             "a distance that is not a number is not near");
    look.distance = -(float)INFINITY;
    ut_check(answers(&look, PLAYER_HELP_OUTCOME_OPEN, PLAYER_HELP_REASON_NONE),
             "nor is one that is not finite");
    look.distance       = 1.0f;
    look.distance_known = false;
    ut_check(answers(&look, PLAYER_HELP_OUTCOME_OPEN, PLAYER_HELP_REASON_NONE),
             "nor one that was not read, whatever the field holds");
}

static void check_the_search(void)
{
    mp_player_help_verdict_t verdict;
    int                      stage;

    memset(&verdict, 0, sizeof verdict);
    ut_section("the search: a place beside the host or none, for five seconds");

    ut_check(mp_teleport_search(false, true, 0u) == MP_TELEPORT_SEARCH_GOES_ON &&
                 mp_teleport_search(false, true, MP_TELEPORT_SEARCH_SUBSTEPS - 1u) ==
                     MP_TELEPORT_SEARCH_GOES_ON,
             "nothing found beside the host yet: the search goes on up to its last substep");
    ut_check(mp_teleport_search(false, true, MP_TELEPORT_SEARCH_SUBSTEPS) ==
                     MP_TELEPORT_SEARCH_NO_SEAT &&
                 mp_teleport_search(false, true, 0xFFFFFFFFu) == MP_TELEPORT_SEARCH_NO_SEAT,
             "at 160 substeps with nothing found it is given up");
    ut_check(mp_teleport_search(true, true, 0u) == MP_TELEPORT_SEARCH_SEATED &&
                 mp_teleport_search(true, true, 40u) == MP_TELEPORT_SEARCH_SEATED,
             "a place found beside the host is taken");
    ut_check(mp_teleport_search(true, true, MP_TELEPORT_SEARCH_SUBSTEPS) ==
                 MP_TELEPORT_SEARCH_SEATED,
             "and taken on the very substep the time runs out");
    ut_check(mp_teleport_search(false, false, 0u) == MP_TELEPORT_SEARCH_NO_SEAT,
             "the seat search moved on to its fallback inside a look that found nothing: "
             "given up at once");
    ut_check(mp_teleport_search(true, false, 0u) == MP_TELEPORT_SEARCH_NO_SEAT &&
                 mp_teleport_search(true, false, 96u) == MP_TELEPORT_SEARCH_NO_SEAT,
             "a place the fallback answered with is never used, however early");
    ut_check(MP_TELEPORT_SEARCH_SUBSTEPS == 160u && MP_TELEPORT_NEAR == MP_SEAT_RING_FAR,
             "the bound is five seconds of substeps, and near is the seat's outer ring");

    ut_section("the move: how it ended, by the stage of its seat");
    for (stage = MP_SCENE_SEAT_IDLE; stage <= MP_SCENE_SEAT_RESPAWNING; ++stage) {
        const bool over = mp_teleport_moved((mp_scene_seat_stage_t)stage, &verdict);
        const bool done = stage == MP_SCENE_SEAT_DONE;
        const bool gone = stage == MP_SCENE_SEAT_GIVEN_UP || stage == MP_SCENE_SEAT_IDLE;

        ut_checkf(over == (done || gone) &&
                      (!done || (verdict.outcome == PLAYER_HELP_OUTCOME_DONE &&
                                 verdict.reason == PLAYER_HELP_REASON_NONE)) &&
                      (!gone || (verdict.outcome == PLAYER_HELP_OUTCOME_REFUSED &&
                                 verdict.reason == PLAYER_HELP_REASON_GAVE_UP)),
                  "stage %d: %s", stage,
                  done ? "landed, done" : gone ? "refused, it gave up" : "still under way");
    }
    ut_check(mp_teleport_moved(MP_SCENE_SEAT_DONE, NULL) &&
                 !mp_teleport_moved(MP_SCENE_SEAT_PLACED, NULL),
             "the question alone is answered with nowhere to put the verdict");
    ut_check(mp_teleport_left_reason(true) == PLAYER_HELP_REASON_WORLD_CHANGED &&
                 mp_teleport_left_reason(false) == PLAYER_HELP_REASON_GAVE_UP,
             "ended from outside: the level changed under it, or it was given up in its world");
}

int main(void)
{
    check_the_mark();
    check_the_reader();
    check_the_door();
    check_the_search();

    return ut_summary("mp_player_help_rule");
}
