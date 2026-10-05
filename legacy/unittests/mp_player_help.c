/* The reader of the developer menu's two buttons, against the records the overlay files and
 * reads.
 *
 * The module is the real one, and so are its rules and the two records, in one process, which
 * is the situation they serve: the overlay's half is played by the test, which files an ask the
 * way a press does and reads the answer the way the panel does. What a press then does to the
 * game is stood in for: the repair and the teleport answer what the test tells them to and
 * keep what they were handed.
 *
 * What is held here is what no rule can show by itself: that a press on file before a session
 * is never carried out and one made in it is carried out once; that nobody is told to listen
 * before a frame of the pump has run, and somebody is told nobody listens when the session
 * ends; that the record always answers the newest press; and that an answer left open is ended
 * on the way out.
 *
 * It is one walk, in order, because the two records live as long as the process: the first
 * part needs a process in which no ask was ever filed.
 *
 * SIZE NOTE: a little over 600 lines. The follow of a warp is pressed through the same reader,
 * and what is held about it is held against the presses made before it in the walk: the record
 * that must not move and the counts that must not either. A file of its own would need the same
 * stand-ins and the same presses again to say the same thing.
 */
#include "unittest.h"

#include "mp_bridge_drain.h"
#include "mp_player_help.h"
#include "mp_player_help_rule.h"
#include "mp_repair_lock.h"
#include "mp_scene_free.h"
#include "mp_teleport_host.h"
#include "mp_wallclock.h"
#include "mp_warp_follow.h"

#include "common/logging.h"
#include "common/player_help_note.h"
#include "common/text.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define LINE_BYTES 1024u

/* The opening of the reader's report line, of the line that counts the presses, and of the line
 * that says somebody listens. */
#define READER_LINE  "  the player's help, the reader: "
#define PRESSES_LINE "  the player's help: "
#define LISTENS_LINE "the player's help listens"

static struct {
    /* What the modules around the reader answer. */
    bool                     is_client;
    bool                     repair_bound;
    bool                     teleport_bound;
    uint32_t                 now_ms;
    mp_player_help_verdict_t teleport_asked;   /* the door's answer to a press */
    bool                     teleport_under_way;
    bool                     teleport_ends;    /* the next frame ends it */
    mp_player_help_verdict_t teleport_ended;
    mp_player_help_verdict_t repair_answer;
    uint16_t                 repair_released;
    bool                     follow_due;       /* the follow of a warp asks for a press, once */

    /* What they were handed. */
    unsigned repairs;
    bool     repair_overlay_holds;
    bool     repair_left_a_teleport;
    unsigned teleports;
    bool     teleport_overlay_holds;
    unsigned teleport_leaves;
    unsigned sessions_ended;
    unsigned listens_lines;
    char     reader_line[LINE_BYTES];
    char     presses_line[LINE_BYTES];
    unsigned                 follow_presses;   /* answers of the door handed to the follow */
    mp_player_help_verdict_t follow_pressed;
    unsigned                 follow_ends;      /* ends of a teleport handed to it */
    mp_player_help_verdict_t follow_ended;
    unsigned                 follow_leaves;    /* times it was told it is ended from outside */
} s_world;

/* ===================================== What the module calls ============================== */

bool mp_bridge_drain_is_client(void)
{
    return s_world.is_client;
}

bool mp_scene_free_install(void)
{
    return s_world.repair_bound;
}

uint32_t mp_wallclock_ms(void)
{
    return s_world.now_ms;
}

bool mp_teleport_host_bound(void)
{
    return s_world.teleport_bound;
}

mp_player_help_verdict_t mp_teleport_host_ask(bool overlay_holds, uint32_t substeps)
{
    (void)substeps;
    ++s_world.teleports;
    s_world.teleport_overlay_holds = overlay_holds;
    if (s_world.teleport_asked.outcome == PLAYER_HELP_OUTCOME_OPEN) {
        s_world.teleport_under_way = true;
    }
    return s_world.teleport_asked;
}

bool mp_teleport_host_frame(uint32_t substeps, mp_player_help_verdict_t *ended)
{
    (void)substeps;
    if (!s_world.teleport_under_way || !s_world.teleport_ends) {
        return false;
    }
    s_world.teleport_under_way = false;
    s_world.teleport_ends      = false;
    *ended = s_world.teleport_ended;
    return true;
}

bool mp_teleport_host_leave(const char *why, uint8_t *reason)
{
    (void)why;
    if (!s_world.teleport_under_way) {
        return false;
    }
    s_world.teleport_under_way = false;
    ++s_world.teleport_leaves;
    if (reason != NULL) {
        *reason = PLAYER_HELP_REASON_WORLD_CHANGED;
    }
    return true;
}

void mp_teleport_host_report(void)
{
}

mp_player_help_verdict_t mp_repair_lock_press(bool overlay_holds, bool left_a_teleport,
                                              uint32_t substeps, uint16_t *released)
{
    (void)substeps;
    ++s_world.repairs;
    s_world.repair_overlay_holds   = overlay_holds;
    s_world.repair_left_a_teleport = left_a_teleport;
    *released = s_world.repair_released;
    return s_world.repair_answer;
}

void mp_repair_lock_frame(uint32_t substeps)
{
    (void)substeps;
}

void mp_repair_lock_session_ended(void)
{
    ++s_world.sessions_ended;
}

void mp_repair_lock_report(void)
{
}

bool mp_warp_follow_due(uint32_t substeps)
{
    const bool due = s_world.follow_due;

    (void)substeps;
    s_world.follow_due = false;
    return due;
}

void mp_warp_follow_pressed(mp_player_help_verdict_t answer, uint32_t substeps)
{
    (void)substeps;
    ++s_world.follow_presses;
    s_world.follow_pressed = answer;
}

void mp_warp_follow_ended(mp_player_help_verdict_t ended, uint32_t substeps)
{
    (void)substeps;
    ++s_world.follow_ends;
    s_world.follow_ended = ended;
}

void mp_warp_follow_left(const char *why)
{
    (void)why;
    ++s_world.follow_leaves;
}

void mp_warp_follow_report(void)
{
}

/* ===================================== The log, kept ====================================== */

/* Every entry of common/logging is defined here, so the linker never takes the library's file
 * and nothing is written to disk. The reader's report line is kept, and the lines that say
 * somebody listens are counted. */
static void keep(const char *format, va_list arguments)
{
    char line[LINE_BYTES];

    (void)text_vformat(line, sizeof line, format, arguments);
    if (strncmp(line, READER_LINE, strlen(READER_LINE)) == 0) {
        memcpy(s_world.reader_line, line, sizeof line);
    }
    if (strncmp(line, PRESSES_LINE, strlen(PRESSES_LINE)) == 0) {
        memcpy(s_world.presses_line, line, sizeof line);
    }
    if (strncmp(line, LISTENS_LINE, strlen(LISTENS_LINE)) == 0) {
        ++s_world.listens_lines;
    }
}

void log_init(const char *feature_name, bool truncate)
{
    (void)feature_name;
    (void)truncate;
}

void log_shutdown(void)
{
}

void log_info(const char *format, ...)
{
    va_list arguments;

    va_start(arguments, format);
    keep(format, arguments);
    va_end(arguments);
}

void log_warning(const char *format, ...)
{
    va_list arguments;

    va_start(arguments, format);
    keep(format, arguments);
    va_end(arguments);
}

void log_error(const char *format, ...)
{
    va_list arguments;

    va_start(arguments, format);
    keep(format, arguments);
    va_end(arguments);
}

const char *log_path(void)
{
    return "";
}

/* ===================================== The overlay's half ================================= */

/* A press as the overlay files it. */
static bool press(uint8_t kind, uint32_t serial, bool overlay_holds)
{
    player_help_ask_t ask;

    memset(&ask, 0, sizeof ask);
    ask.version = PLAYER_HELP_NOTE_VERSION;
    ask.kind    = kind;
    ask.flags   = overlay_holds ? PLAYER_HELP_ASK_F_OVERLAY_HOLDS : 0u;
    ask.serial  = serial;
    return player_help_ask_publish(&ask);
}

/* The answer as the panel reads it; a record of noughts when none is on file. */
static player_help_answer_t the_answer(bool *on_file)
{
    player_help_answer_t answer;
    bool                 read;

    memset(&answer, 0, sizeof answer);
    read = player_help_answer_read(&answer);
    if (on_file != NULL) {
        *on_file = read;
    }
    return answer;
}

/* Whether the answer on file answers press `serial` of `kind` with `outcome` and `reason`. */
static bool answered(uint32_t serial, uint8_t kind, uint8_t outcome, uint8_t reason)
{
    const player_help_answer_t answer = the_answer(NULL);

    return answer.serial == serial && answer.kind == kind && answer.outcome == outcome &&
           answer.reason == reason;
}

static mp_player_help_verdict_t verdict(uint8_t outcome, uint8_t reason)
{
    mp_player_help_verdict_t v;

    v.outcome = outcome;
    v.reason  = reason;
    return v;
}

/* `count` frames of the pump, a few milliseconds apart. */
static void frames(unsigned count)
{
    unsigned i;

    for (i = 0u; i < count; ++i) {
        s_world.now_ms += 4u;
        mp_player_help_frame(i);
    }
}

/* Whether the reader's report line, printed now, contains `text`. */
static bool report_says(const char *text)
{
    s_world.reader_line[0] = '\0';
    mp_player_help_report();
    return strstr(s_world.reader_line, text) != NULL;
}

/* The same for the line that counts the presses. */
static bool presses_say(const char *text)
{
    s_world.presses_line[0] = '\0';
    mp_player_help_report();
    return strstr(s_world.presses_line, text) != NULL;
}

/* ===================================== The walk =========================================== */

static void check_before_any_ask(void)
{
    bool on_file = true;

    ut_section("no session: nothing is read and nothing is filed");
    frames(10u);
    (void)the_answer(&on_file);
    ut_check(!on_file && report_says("armed 0 time(s)") && report_says(", 0 look(s) at the ask"),
             "frames with no arming look at nothing and file no answer");

    ut_section("a session with no ask on file: one look a second, and the answer says who "
               "listens");
    mp_player_help_arm();
    (void)the_answer(&on_file);
    ut_check(!on_file && s_world.listens_lines == 0u,
             "the arming itself files nothing: no frame of the pump has run yet");
    frames(1u);
    ut_check(the_answer(&on_file).ready ==
                     (PLAYER_HELP_READY_LISTENING | PLAYER_HELP_READY_CAN_REPAIR |
                      PLAYER_HELP_READY_CAN_TELEPORT) &&
                 on_file && s_world.listens_lines == 1u,
             "the first frame says it: a bound client listens and can do both");
    ut_check(answered(0u, PLAYER_HELP_KIND_NONE, PLAYER_HELP_OUTCOME_NONE,
                      PLAYER_HELP_REASON_NONE),
             "and it answers no press yet");
    frames(100u);
    ut_check(s_world.listens_lines == 1u, "said once, not on every frame");
    ut_check(report_says(", 0 look(s) at the ask"),
             "inside the second after the arming's look found no ask, no frame looks again");
    s_world.now_ms += PLAYER_HELP_NOTE_RETRY_MS;
    frames(1u);
    ut_check(report_says(", 1 look(s) at the ask, 1 of them found none"),
             "a second later one frame looks, and finds none");
    frames(100u);
    ut_check(report_says(", 1 look(s) at the ask, 1 of them found none"),
             "and the frames after it wait again");
    mp_player_help_withdraw();
    ut_check(the_answer(&on_file).ready == 0u && on_file && s_world.sessions_ended == 1u,
             "the session's end says that nobody listens");
}

static void check_a_press_of_before_the_session(void)
{
    ut_section("a press on file before the session is never carried out");
    ut_check(press(PLAYER_HELP_KIND_REPAIR, 5u, false), "press 5, a repair, is filed with no "
             "session");
    frames(10u);
    ut_check(s_world.repairs == 0u, "with no session no frame carries it out");
    mp_player_help_arm();
    s_world.now_ms += PLAYER_HELP_NOTE_RETRY_MS;
    frames(200u);
    ut_check(s_world.repairs == 0u && s_world.teleports == 0u,
             "nor do two hundred frames of the session that armed after it");
    ut_check(the_answer(NULL).ready != 0u &&
                 answered(0u, PLAYER_HELP_KIND_NONE, PLAYER_HELP_OUTCOME_NONE,
                          PLAYER_HELP_REASON_NONE),
             "the answer listens again, and still answers no press");
    ut_check(report_says("armed 2 time(s), 1 of them with a press older than the session"),
             "the report counts the arming that found it");
}

static void check_a_repair(void)
{
    ut_section("a press made in the session is carried out once, and answered");
    s_world.repair_answer   = verdict(PLAYER_HELP_OUTCOME_DONE, PLAYER_HELP_REASON_MENU_OPEN);
    s_world.repair_released = 0x0103u;
    ut_check(press(PLAYER_HELP_KIND_REPAIR, 6u, true), "press 6, a repair, the overlay holding");
    frames(1u);
    ut_check(s_world.repairs == 1u && s_world.repair_overlay_holds &&
                 !s_world.repair_left_a_teleport,
             "the next frame carries it out, with the ask's flag and no teleport to leave");
    ut_check(answered(6u, PLAYER_HELP_KIND_REPAIR, PLAYER_HELP_OUTCOME_DONE,
                      PLAYER_HELP_REASON_MENU_OPEN) &&
                 the_answer(NULL).released == 0x0103u && the_answer(NULL).ready != 0u,
             "the answer names the press and its kind, with what the repair said and still "
             "listening");
    frames(200u);
    ut_check(s_world.repairs == 1u, "read on two hundred more frames it is not carried out "
             "again");
    s_world.repair_answer = verdict(PLAYER_HELP_OUTCOME_NOTHING, PLAYER_HELP_REASON_NONE);
    ut_check(press(PLAYER_HELP_KIND_REPAIR, 7u, false), "press 7, a repair");
    frames(3u);
    ut_check(s_world.repairs == 2u && !s_world.repair_overlay_holds &&
                 answered(7u, PLAYER_HELP_KIND_REPAIR, PLAYER_HELP_OUTCOME_NOTHING,
                          PLAYER_HELP_REASON_NONE),
             "the next press is carried out once as well, and answered with nothing to do");
}

static void check_a_teleport(void)
{
    ut_section("a teleport answers open, and its end answers the press that began it");
    s_world.teleport_asked = verdict(PLAYER_HELP_OUTCOME_OPEN, PLAYER_HELP_REASON_NONE);
    ut_check(press(PLAYER_HELP_KIND_TELEPORT, 8u, false), "press 8, a teleport");
    frames(5u);
    ut_check(s_world.teleports == 1u && !s_world.teleport_overlay_holds &&
                 answered(8u, PLAYER_HELP_KIND_TELEPORT, PLAYER_HELP_OUTCOME_OPEN,
                          PLAYER_HELP_REASON_NONE),
             "taken once, and the answer stands open while it is under way");
    s_world.teleport_ended = verdict(PLAYER_HELP_OUTCOME_DONE, PLAYER_HELP_REASON_NONE);
    s_world.teleport_ends  = true;
    frames(1u);
    ut_check(answered(8u, PLAYER_HELP_KIND_TELEPORT, PLAYER_HELP_OUTCOME_DONE,
                      PLAYER_HELP_REASON_NONE),
             "it lands: the same press is answered done");

    ut_section("the record answers the newest press");
    ut_check(press(PLAYER_HELP_KIND_TELEPORT, 9u, false), "press 9, a teleport, taken");
    frames(1u);
    s_world.teleport_asked = verdict(PLAYER_HELP_OUTCOME_REFUSED, PLAYER_HELP_REASON_BUSY);
    ut_check(press(PLAYER_HELP_KIND_TELEPORT, 10u, false), "press 10, a teleport while 9 runs");
    frames(1u);
    ut_check(answered(10u, PLAYER_HELP_KIND_TELEPORT, PLAYER_HELP_OUTCOME_REFUSED,
                      PLAYER_HELP_REASON_BUSY),
             "press 10 is refused as busy");
    s_world.teleport_ends = true;
    frames(1u);
    ut_check(answered(10u, PLAYER_HELP_KIND_TELEPORT, PLAYER_HELP_OUTCOME_REFUSED,
                      PLAYER_HELP_REASON_BUSY) &&
                 report_says("1 ended after a newer press was answered"),
             "the teleport of press 9 lands after it, and the record keeps the answer to 10");

    ut_section("a repair ends a teleport under way");
    s_world.teleport_asked = verdict(PLAYER_HELP_OUTCOME_OPEN, PLAYER_HELP_REASON_NONE);
    s_world.repair_answer  = verdict(PLAYER_HELP_OUTCOME_DONE, PLAYER_HELP_REASON_NONE);
    ut_check(press(PLAYER_HELP_KIND_TELEPORT, 11u, false), "press 11, a teleport, taken");
    frames(1u);
    ut_check(press(PLAYER_HELP_KIND_REPAIR, 12u, false), "press 12, a repair");
    frames(1u);
    ut_check(s_world.teleport_leaves == 1u && s_world.repair_left_a_teleport &&
                 !s_world.teleport_under_way,
             "the teleport is left first, and the repair is told so");
    ut_check(answered(12u, PLAYER_HELP_KIND_REPAIR, PLAYER_HELP_OUTCOME_DONE,
                      PLAYER_HELP_REASON_NONE),
             "the repair's answer stands in the record, so no answer is left open");
}

/* A warp of the host's that this player follows is the same teleport, pressed by the reader on
 * the frame the follow says it is due. Nobody pressed a button, so the record the overlay reads
 * must not move, and the count of presses must not either. */
static void check_a_follow(void)
{
    const unsigned asked  = s_world.teleports;
    const unsigned leaves = s_world.teleport_leaves;
    const unsigned told   = s_world.follow_leaves;   /* every way out of a session tells it once */

    ut_section("a warp of the host's is followed through the same teleport and answers no press");
    ut_check(presses_say(" 4 teleport(s) asked, 2 landed, "),
             "before it: the four presses of a teleport so far, two of them landed");
    s_world.teleport_asked = verdict(PLAYER_HELP_OUTCOME_OPEN, PLAYER_HELP_REASON_NONE);
    s_world.follow_due     = true;
    frames(1u);
    ut_check(s_world.teleports == asked + 1u && s_world.teleport_under_way &&
                 !s_world.teleport_overlay_holds && s_world.follow_presses == 1u &&
                 s_world.follow_pressed.outcome == PLAYER_HELP_OUTCOME_OPEN,
             "the frame it is due on presses the teleport's door once, and the follow is told "
             "that it opened");
    ut_check(answered(12u, PLAYER_HELP_KIND_REPAIR, PLAYER_HELP_OUTCOME_DONE,
                      PLAYER_HELP_REASON_NONE),
             "the record still answers the last press of a button");
    frames(20u);
    ut_check(s_world.teleports == asked + 1u && s_world.follow_presses == 1u,
             "and it is pressed once, not on every frame after");
    s_world.teleport_ended = verdict(PLAYER_HELP_OUTCOME_DONE, PLAYER_HELP_REASON_NONE);
    s_world.teleport_ends  = true;
    frames(1u);
    ut_check(s_world.follow_ends == 1u &&
                 s_world.follow_ended.outcome == PLAYER_HELP_OUTCOME_DONE &&
                 answered(12u, PLAYER_HELP_KIND_REPAIR, PLAYER_HELP_OUTCOME_DONE,
                          PLAYER_HELP_REASON_NONE),
             "it lands: the end goes to the follow, and the record is left alone");
    ut_check(presses_say(" 4 teleport(s) asked, 2 landed, ") &&
                 report_says("1 ended after a newer press was answered"),
             "no press is counted for it and no landing, and it is no late end of a press");

    ut_section("a door that does not open for the follow is the follow's to wait out");
    s_world.teleport_asked = verdict(PLAYER_HELP_OUTCOME_REFUSED, PLAYER_HELP_REASON_DEAD);
    s_world.follow_due     = true;
    frames(1u);
    ut_check(s_world.follow_presses == 2u && !s_world.teleport_under_way &&
                 s_world.follow_pressed.outcome == PLAYER_HELP_OUTCOME_REFUSED &&
                 s_world.follow_pressed.reason == PLAYER_HELP_REASON_DEAD &&
                 answered(12u, PLAYER_HELP_KIND_REPAIR, PLAYER_HELP_OUTCOME_DONE,
                          PLAYER_HELP_REASON_NONE),
             "the follow is told the refusal, nothing is under way and nothing is answered");

    ut_section("a press of the button after a follow is a press again");
    s_world.teleport_asked = verdict(PLAYER_HELP_OUTCOME_OPEN, PLAYER_HELP_REASON_NONE);
    s_world.follow_due     = true;
    frames(1u);
    s_world.teleport_ended = verdict(PLAYER_HELP_OUTCOME_REFUSED, PLAYER_HELP_REASON_NO_SEAT);
    s_world.teleport_ends  = true;
    frames(1u);
    ut_check(s_world.follow_ends == 2u &&
                 s_world.follow_ended.reason == PLAYER_HELP_REASON_NO_SEAT,
             "a follow's teleport that found no place ends for the follow as well");
    ut_check(press(PLAYER_HELP_KIND_TELEPORT, 13u, false), "press 13, a teleport");
    frames(1u);
    s_world.teleport_ended = verdict(PLAYER_HELP_OUTCOME_DONE, PLAYER_HELP_REASON_NONE);
    s_world.teleport_ends  = true;
    frames(1u);
    ut_check(s_world.follow_ends == 2u &&
                 answered(13u, PLAYER_HELP_KIND_TELEPORT, PLAYER_HELP_OUTCOME_DONE,
                          PLAYER_HELP_REASON_NONE) &&
                 presses_say(" 5 teleport(s) asked, 3 landed, "),
             "its end answers the press and is counted, and the follow hears nothing of it");

    ut_section("a repair ends a follow's teleport for good, and counts no refused press");
    s_world.follow_due = true;
    frames(1u);
    ut_check(s_world.teleport_under_way, "a follow's teleport is under way");
    ut_check(press(PLAYER_HELP_KIND_REPAIR, 14u, false), "press 14, a repair");
    frames(1u);
    ut_check(s_world.teleport_leaves == leaves + 1u && s_world.repair_left_a_teleport &&
                 s_world.follow_leaves == told + 1u && s_world.follow_ends == 2u,
             "the teleport is left, the repair is told so, and the follow is told it is over "
             "and not that its teleport ended, which it would wait for again");
    ut_check(presses_say(" 5 teleport(s) asked, 3 landed, 2 refused ("),
             "the two refusals counted are press 10's and the teleport of press 11: the "
             "follow's teleport was no press");
}

static void check_the_way_out(void)
{
    const unsigned listened = s_world.listens_lines;
    const unsigned repairs  = s_world.repairs;
    const unsigned leaves   = s_world.teleport_leaves;
    const unsigned told     = s_world.follow_leaves;

    ut_section("the session's end: the teleport left, its open answer ended, nobody listens");
    ut_check(press(PLAYER_HELP_KIND_TELEPORT, 23u, false), "press 23, a teleport, taken");
    frames(1u);
    ut_check(answered(23u, PLAYER_HELP_KIND_TELEPORT, PLAYER_HELP_OUTCOME_OPEN,
                      PLAYER_HELP_REASON_NONE),
             "its answer stands open");
    mp_player_help_withdraw();
    ut_check(s_world.teleport_leaves == leaves + 1u && !s_world.teleport_under_way,
             "the way out leaves the teleport, which the pump would never see end");
    ut_check(answered(23u, PLAYER_HELP_KIND_TELEPORT, PLAYER_HELP_OUTCOME_REFUSED,
                      PLAYER_HELP_REASON_WORLD_CHANGED) &&
                 the_answer(NULL).ready == 0u,
             "the open answer is ended as refused, for the reason the teleport gave, and nobody "
             "listens");
    ut_check(s_world.follow_leaves == told + 1u,
             "and a warp still waited for is told the session is over, once: the teleport that "
             "was left was a press's");
    ut_check(press(PLAYER_HELP_KIND_REPAIR, 24u, false), "press 24, a repair, with no session");
    frames(50u);
    ut_check(s_world.repairs == repairs, "after the way out no frame carries a press out");
    s_world.follow_due = true;
    frames(50u);
    ut_check(s_world.follow_due && s_world.follow_presses == 4u,
             "nor is a warp followed: with no session the follow is not even asked");
    s_world.follow_due = false;
    mp_player_help_withdraw();
    ut_check(report_says("withdrawn 2 time(s), 1 answer(s) still open ended there"),
             "a second way out with no session is nothing, and the report counts the first");

    ut_section("the next session: a host, whose reader marks the press made in between");
    s_world.is_client = false;
    mp_player_help_arm();
    frames(200u);
    ut_check(s_world.repairs == repairs, "press 24 was made before it and is left alone");
    ut_check(the_answer(NULL).ready ==
                     (PLAYER_HELP_READY_LISTENING | PLAYER_HELP_READY_CAN_REPAIR) &&
                 s_world.listens_lines == listened + 1u,
             "the host's reader listens and can repair, and never offers the teleport");
    s_world.repair_bound = false;
    frames(1u);
    ut_check(the_answer(NULL).ready == PLAYER_HELP_READY_LISTENING &&
                 s_world.listens_lines == listened + 2u,
             "what it can do is said again when it changes");
    ut_check(press(PLAYER_HELP_KIND_REPAIR, 25u, false), "press 25, a repair");
    frames(1u);
    ut_check(s_world.repairs == repairs + 1u &&
                 answered(25u, PLAYER_HELP_KIND_REPAIR, PLAYER_HELP_OUTCOME_DONE,
                          PLAYER_HELP_REASON_NONE),
             "and a press made in this session is carried out");
}

int main(void)
{
    s_world.is_client      = true;
    s_world.repair_bound   = true;
    s_world.teleport_bound = true;
    s_world.now_ms         = 100000u;

    check_before_any_ask();
    check_a_press_of_before_the_session();
    check_a_repair();
    check_a_teleport();
    check_a_follow();
    check_the_way_out();

    return ut_summary("mp_player_help");
}
