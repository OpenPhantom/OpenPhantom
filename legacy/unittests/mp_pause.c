/* mp_pause.c: the pause menu of a session, against the rules it is built from.
 *
 * The engine's own pause is the reference and it is written out here as the reference model:
 * the latch, the SUSPEND broadcast, the gate, the menu, and the three cells its answer decides.
 * The session's menu must leave the same three cells as the engine's own pause for every answer
 * the menu can give, including a load from inside the menu that holds the gate from outside,
 * while it writes no latch and sends no broadcast at all.
 *
 * Every way out of the menu is then walked, the four answers and the four reasons the menu closes
 * itself for, and each has to leave the same state behind: the menu down, the hold released, the
 * session note saying so, and one count in the right place.
 *
 * The chat holds the same input for a reason of its own, so every way out is walked twice: once
 * with the chat opened and closed while the menu is up, and once with the chat open before the
 * menu opens and still open after it has gone. Neither may free the other's hold.
 */
#include "unittest.h"

#include "mp_armed.h"
#include "mp_pause_rule.h"

#include "common/session_note.h"
#include "common/text.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The cells the engine's pause touches, and what else it does, for the reference model. */
typedef struct cells {
    uint32_t latch;
    uint32_t gate;
    uint32_t outcome;
    uint32_t restore;
    uint32_t suspends;
    uint32_t resumes;
} cells_t;

/* sys_pause 0x0043FAB5, statement for statement: the latch, SUSPEND, the gate, the menu, the
 * answer, RESUME, the latch. A load inside the menu holds the gate through campaign_loadLevel,
 * which in this model has already happened by the time the answer arrives. */
static void engine_pause(cells_t *c, int32_t reply, bool load_inside)
{
    if (c->latch == 1u) {
        return;
    }
    c->latch = 1u;
    ++c->suspends;
    c->gate = 1u;
    if (load_inside) {
        c->gate = 1u;
    }
    if (reply == 6) {
        c->outcome = 3u;
        c->restore = 1u;
    } else {
        c->gate = 0u;
    }
    ++c->resumes;
    c->latch = 0u;
}

static void apply(cells_t *c, const mp_pause_write_t *writes, size_t count)
{
    size_t i;

    for (i = 0; i < count; ++i) {
        switch (writes[i].cell) {
        case MP_PAUSE_WRITE_OUTCOME:
            c->outcome = writes[i].value;
            break;
        case MP_PAUSE_WRITE_RESTORE:
            c->restore = writes[i].value;
            break;
        case MP_PAUSE_WRITE_GATE:
            c->gate = writes[i].value;
            break;
        }
    }
}

static mp_pause_look_t quiet_look(uint32_t now_ms, uint32_t substeps)
{
    mp_pause_look_t look;

    memset(&look, 0, sizeof look);
    look.now_ms       = now_ms;
    look.substeps     = substeps;
    look.health_read  = true;
    look.health       = 100;
    look.lock_read    = true;
    look.lock         = 0;
    look.outcome_read = true;
    look.outcome      = MP_PAUSE_OUTCOME_RUNNING;
    return look;
}

/* The session's menu as the hull drives it, in the model: open, some frames, the answer, leave. */
static void session_pause(mp_pause_session_t *s, cells_t *c, int32_t reply, bool load_inside)
{
    mp_pause_write_t writes[MP_PAUSE_EXIT_WRITES_MAX];
    mp_pause_look_t  look = quiet_look(1000u, 10u);
    size_t           count;
    bool             said = false;

    (void)mp_pause_rule_open(s, true, 0, 10u, 1000u, &said);
    mp_pause_rule_look(s, &look);
    if (load_inside) {
        c->gate = 1u;
        look = quiet_look(1100u, 10u);
        look.gate_held = true;
        mp_pause_rule_look(s, &look);
    }
    count = mp_pause_rule_leave(s, reply, 1200u, writes, MP_PAUSE_EXIT_WRITES_MAX);
    apply(c, writes, count);
}

static void check_the_exit_against_the_engine(void)
{
    static const int32_t replies[] = { 0, 5, 6, -1 };
    size_t               i;
    int                  load;

    ut_section("the one exit leaves the cells the engine's own pause leaves, for every answer");

    mp_armed_set_transport(true, true);
    for (load = 0; load < 2; ++load) {
        for (i = 0; i < sizeof replies / sizeof replies[0]; ++i) {
            mp_pause_session_t s;
            cells_t            engine;
            cells_t            session;

            memset(&s, 0, sizeof s);
            memset(&engine, 0, sizeof engine);
            engine.outcome = MP_PAUSE_OUTCOME_RUNNING;
            session = engine;

            engine_pause(&engine, replies[i], load != 0);
            session_pause(&s, &session, replies[i], load != 0);

            ut_checkf(engine.gate == session.gate && engine.outcome == session.outcome &&
                          engine.restore == session.restore,
                      "reply %d%s: gate %u, outcome %u, restore %u, as the engine leaves them "
                      "(the session's menu left %u, %u, %u)",
                      (int)replies[i], load ? " after a load inside the menu" : "",
                      (unsigned)engine.gate, (unsigned)engine.outcome, (unsigned)engine.restore,
                      (unsigned)session.gate, (unsigned)session.outcome,
                      (unsigned)session.restore);
            ut_checkf(session.latch == 0u && session.suspends == 0u && session.resumes == 0u,
                      "reply %d%s: no latch written and no SUSPEND or RESUME sent",
                      (int)replies[i], load ? " after a load" : "");
        }
    }
    mp_armed_set_transport(false, false);
}

static void check_the_way_in(void)
{
    ut_section("a press goes to the engine's own pause unless a session's menu can hold the input");

    ut_check(mp_pause_rule_way(false, false, false) == MP_PAUSE_WAY_ENGINE,
             "no transport and no input split: the engine's own pause");
    ut_check(mp_pause_rule_way(false, false, true) == MP_PAUSE_WAY_ENGINE,
             "no transport: the engine's own pause, whatever else is installed");
    ut_check(mp_pause_rule_way(false, true, false) == MP_PAUSE_WAY_ENGINE_UNHELD,
             "a session without the input split: the engine's own, said as such");
    ut_check(mp_pause_rule_way(false, true, true) == MP_PAUSE_WAY_SESSION,
             "a session with the input split: the menu over a running world");
    ut_check(mp_pause_rule_way(true, true, true) == MP_PAUSE_WAY_ALREADY_OPEN,
             "a press while a session's menu is up opens nothing");
    ut_check(mp_pause_rule_way(true, false, true) == MP_PAUSE_WAY_ALREADY_OPEN,
             "and neither does one after the transport went down under it");
}

/* The census of the four readers' call sites, by id: every id the player's pipeline or the
 * dialogue asks is held, every id the menus ask passes. */
static void check_the_id_table(void)
{
    static const int32_t held[] = {
        0x00, 0x01,                                         /* both axis readers */
        0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x09,           /* the player's actions */
        0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x14, 0x15,
        0x16, 0x17, 0x18                                    /* the dialogue's, read every frame */
    };
    static const int32_t passed[] = {
        0x1A, 0x1B, 0x1C, 0x1D,                             /* the pad's moves through a menu */
        0x1E, 0x1F, 0x20                                    /* its menu buttons */
    };
    size_t i;

    ut_section("the readers hold every id below the menus' own, and pass the menus' own");

    for (i = 0; i < sizeof held / sizeof held[0]; ++i) {
        ut_checkf(mp_pause_rule_holds_action(held[i]), "id 0x%02X is held", (unsigned)held[i]);
    }
    for (i = 0; i < sizeof passed / sizeof passed[0]; ++i) {
        ut_checkf(!mp_pause_rule_holds_action(passed[i]), "id 0x%02X passes",
                  (unsigned)passed[i]);
    }
    ut_check(!mp_pause_rule_holds_action(-1), "an id no reader is ever asked for is not held");
}

/* One way out, and what every way out must leave behind. With `chat_open` the chat holds the input
 * before the menu opens and after it has gone, and has to be the one still holding it then; without
 * it the chat is opened and closed once while the menu is up, and the menu has to be the one still
 * holding it then. */
static void leave_by(mp_pause_session_t *s, int32_t reply, mp_pause_reason_t reason,
                     bool chat_open, const char *way)
{
    mp_pause_write_t writes[MP_PAUSE_EXIT_WRITES_MAX];
    mp_pause_look_t  look = quiet_look(2000u, 20u);
    session_note_t   note;
    bool             said = false;
    bool             gave_up = false;
    int32_t          code = 0;
    char             what[96];

    text_format(what, sizeof what, "%s, %s", way,
                chat_open ? "the chat open throughout" : "the chat opened and closed under it");

    if (chat_open) {
        (void)mp_armed_hold_input(MP_ARMED_HOLDER_CHAT, true);
    }
    ut_checkf(mp_pause_rule_open(s, true, 0, 20u, 2000u, &said), "%s: the menu opens", what);
    ut_checkf(mp_armed_input_held(), "%s: the hold stands while it is up", what);
    if (!chat_open) {
        (void)mp_armed_hold_input(MP_ARMED_HOLDER_CHAT, true);
        (void)mp_armed_hold_input(MP_ARMED_HOLDER_CHAT, false);
        ut_checkf(mp_armed_input_held(), "%s: the chat letting go leaves the menu's hold standing",
                  what);
    }

    switch (reason) {
    case MP_PAUSE_REASON_DEATH:
        look.health = 0;
        mp_pause_rule_look(s, &look);
        break;
    case MP_PAUSE_REASON_SCENE:
        look.lock = 5;
        mp_pause_rule_look(s, &look);
        break;
    case MP_PAUSE_REASON_LEVEL:
        look.outcome = 3u;
        mp_pause_rule_look(s, &look);
        break;
    case MP_PAUSE_REASON_SESSION:
        (void)mp_pause_rule_session_ended(s, 2050u);
        break;
    case MP_PAUSE_REASON_NONE:
    case MP_PAUSE_REASON_COUNT:
    default:
        mp_pause_rule_look(s, &look);
        break;
    }
    if (reason != MP_PAUSE_REASON_NONE) {
        code = mp_pause_rule_nav(s, 0, 2100u, &gave_up);
        ut_checkf(code == MP_PAUSE_NAV_CANCEL, "%s: an idle navigation read answers a cancel",
                  what);
        reply = 0;   /* what every screen answers a cancel with, on its way out */
    }
    (void)mp_pause_rule_leave(s, reply, 2200u, writes, MP_PAUSE_EXIT_WRITES_MAX);

    memset(&note, 0, sizeof note);
    note.input_held = true;
    ut_checkf(!s->open && s->reason == MP_PAUSE_REASON_NONE && !s->given_up && s->cancels == 0u,
              "%s: afterwards the menu is down and nothing of the opening is left", what);
    if (chat_open) {
        ut_checkf(mp_armed_input_held(), "%s: the chat's hold outlasts the menu", what);
        ut_checkf(session_note_read(&note) && note.input_held && note.running,
                  "%s: and the session note still says the input is held", what);
        (void)mp_armed_hold_input(MP_ARMED_HOLDER_CHAT, false);
    }
    ut_checkf(!mp_armed_input_held(), "%s: and the hold is released", what);
    ut_checkf(session_note_read(&note) && !note.input_held && note.running,
              "%s: and the session note says the input is free while the session runs", what);
}

static void check_every_way_out(void)
{
    mp_pause_session_t s;
    size_t             reason;
    int                chat;

    ut_section("every way out of the menu leaves the same state behind, beside a chat or not");

    for (chat = 0; chat < 2; ++chat) {
        const bool open = chat != 0;

        memset(&s, 0, sizeof s);
        mp_armed_set_transport(true, false);
        leave_by(&s, 0, MP_PAUSE_REASON_NONE, open, "closed by the player");
        leave_by(&s, 5, MP_PAUSE_REASON_NONE, open, "resumed");
        leave_by(&s, 6, MP_PAUSE_REASON_NONE, open, "left the level");
        leave_by(&s, -1, MP_PAUSE_REASON_NONE, open, "refused by the engine");
        leave_by(&s, 0, MP_PAUSE_REASON_DEATH, open, "closed for a death");
        leave_by(&s, 0, MP_PAUSE_REASON_SCENE, open, "closed for a scene lock");
        leave_by(&s, 0, MP_PAUSE_REASON_LEVEL, open, "closed for the level");
        leave_by(&s, 0, MP_PAUSE_REASON_SESSION, open, "closed for the end of the session");
        mp_armed_set_transport(false, false);

        ut_check(s.opened_total == 8u, "eight openings counted");
        ut_check(s.left_to_play == 6u && s.left_to_quit == 1u && s.refused_by_engine == 1u,
                 "six left to play, one to quit, one refused by the engine");
        ut_check(s.closed_by[MP_PAUSE_REASON_NONE] == 4u, "four by the player's own answer");
        for (reason = MP_PAUSE_REASON_DEATH; reason < MP_PAUSE_REASON_COUNT; ++reason) {
            ut_checkf(s.closed_by[reason] == 1u, "and one each for %s",
                      mp_pause_rule_reason_text((mp_pause_reason_t)reason));
        }
    }
}

/* The set itself: each holder takes and drops only its own bit, and nothing holds without a
 * transport, whoever asks. */
static void check_the_holders(void)
{
    static const mp_armed_holder_t holders[] = { MP_ARMED_HOLDER_PAUSE, MP_ARMED_HOLDER_CHAT };
    session_note_t                 note;
    size_t                         i;

    ut_section("the input is held while any holder holds it, and only with a transport");

    mp_armed_set_transport(false, false);
    for (i = 0; i < sizeof holders / sizeof holders[0]; ++i) {
        (void)mp_armed_hold_input(holders[i], true);
        memset(&note, 0, sizeof note);
        ut_checkf(!mp_armed_input_held() && session_note_read(&note) && !note.input_held,
                  "holder %u takes no hold while no transport stands", (unsigned)holders[i]);
    }
    (void)mp_armed_hold_input(MP_ARMED_HOLDER_PAUSE, true);
    (void)mp_armed_hold_input(MP_ARMED_HOLDER_CHAT, true);
    ut_check(!mp_armed_input_held(), "and neither do both of them together");

    mp_armed_set_transport(true, true);
    ut_check(!mp_armed_input_held(), "a transport that goes up brings no hold asked for before it");
    (void)mp_armed_hold_input(MP_ARMED_HOLDER_PAUSE, true);
    (void)mp_armed_hold_input(MP_ARMED_HOLDER_CHAT, true);
    (void)mp_armed_hold_input(MP_ARMED_HOLDER_PAUSE, false);
    ut_check(mp_armed_input_held(), "the pause menu letting go leaves the chat's hold standing");
    (void)mp_armed_hold_input(MP_ARMED_HOLDER_CHAT, false);
    ut_check(!mp_armed_input_held(), "and the chat letting go after it frees the input");

    (void)mp_armed_hold_input(MP_ARMED_HOLDER_CHAT, true);
    (void)mp_armed_hold_input(MP_ARMED_HOLDER_PAUSE, true);
    (void)mp_armed_hold_input(MP_ARMED_HOLDER_CHAT, false);
    ut_check(mp_armed_input_held(), "the chat letting go leaves the pause menu's hold standing");
    (void)mp_armed_hold_input(MP_ARMED_HOLDER_CHAT, false);
    ut_check(mp_armed_input_held(), "and letting go twice frees nothing more");
    (void)mp_armed_hold_input(MP_ARMED_HOLDER_PAUSE, false);
    ut_check(!mp_armed_input_held(), "and the pause menu letting go after it frees the input");

    (void)mp_armed_hold_input(MP_ARMED_HOLDER_PAUSE, true);
    (void)mp_armed_hold_input(MP_ARMED_HOLDER_CHAT, true);
    mp_armed_set_transport(false, false);
    ut_check(!mp_armed_input_held(), "a transport that comes down drops every holder at once");
    mp_armed_set_transport(true, false);
    memset(&note, 0, sizeof note);
    ut_check(!mp_armed_input_held() && session_note_read(&note) && !note.input_held,
             "and the next transport starts with none of them");
    mp_armed_set_transport(false, false);
}

static void check_the_reasons(void)
{
    mp_pause_session_t s;
    mp_pause_look_t    look;
    bool               said;
    bool               gave_up = false;

    ut_section("the menu closes itself for a rising lock, a death, the level, the session");

    mp_armed_set_transport(true, true);

    memset(&s, 0, sizeof s);
    (void)mp_pause_rule_open(&s, true, 5, 0u, 0u, &said);
    look = quiet_look(10u, 0u);
    look.lock = 5;
    mp_pause_rule_look(&s, &look);
    ut_check(s.reason == MP_PAUSE_REASON_NONE,
             "a lock already standing when the menu opened closes nothing");
    look.lock = 0;
    mp_pause_rule_look(&s, &look);
    look.lock = 1;
    mp_pause_rule_look(&s, &look);
    ut_check(s.reason == MP_PAUSE_REASON_SCENE,
             "a lock that falls and then rises again is a rise, and it closes the menu");
    (void)mp_pause_rule_leave(&s, 0, 20u, NULL, 0u);

    memset(&s, 0, sizeof s);
    (void)mp_pause_rule_open(&s, true, 0, 0u, 0u, &said);
    look = quiet_look(10u, 0u);
    look.dead = true;
    mp_pause_rule_look(&s, &look);
    ut_check(s.reason == MP_PAUSE_REASON_DEATH, "a corpse closes the menu");
    look = quiet_look(20u, 1u);
    mp_pause_rule_look(&s, &look);
    ut_check(s.reason == MP_PAUSE_REASON_DEATH,
             "and the reason stays latched after a quick re-entry stood the player up again");
    (void)mp_pause_rule_leave(&s, 0, 30u, NULL, 0u);
    ut_check(s.reason == MP_PAUSE_REASON_NONE, "until the menu is left");

    memset(&s, 0, sizeof s);
    (void)mp_pause_rule_open(&s, true, 0, 0u, 0u, &said);
    look = quiet_look(10u, 0u);
    look.gate_held = true;
    look.outcome = 3u;
    look.health = 0;
    mp_pause_rule_look(&s, &look);
    ut_check(s.reason == MP_PAUSE_REASON_NONE,
             "nothing is judged while a load from inside the menu holds the gate");
    look.gate_held = false;
    look.health = 100;
    mp_pause_rule_look(&s, &look);
    ut_check(s.reason == MP_PAUSE_REASON_LEVEL, "an outcome that has left running closes it");

    ut_check(mp_pause_rule_nav(&s, 5, 100u, &gave_up) == 5 && !gave_up,
             "a code of the player's own goes where it was going");
    ut_check(mp_pause_rule_nav(&s, 0, 100u, &gave_up) == MP_PAUSE_NAV_CANCEL,
             "an idle read answers a cancel");
    ut_check(mp_pause_rule_nav(&s, 0, 10u + MP_PAUSE_FORCE_GIVE_UP_MS, &gave_up) == 0 && gave_up,
             "and the close is given up once the time has passed, said once");
    ut_check(mp_pause_rule_nav(&s, 0, 20u + MP_PAUSE_FORCE_GIVE_UP_MS, &gave_up) == 0 &&
                 !gave_up,
             "after which the screen is the player's again");
    ut_check(s.cancels == 1u && s.given_up_total == 1u, "one cancel handed, one give up counted");
    /* A message box stood over the menu when the player died, and he has answered it now: the menu
     * is still up over a corpse, and his key is what takes the close up again. */
    ut_check(mp_pause_rule_nav(&s, 7, 4000u, &gave_up) == 7 && !gave_up,
             "a key of the player's after the give up goes where it was going");
    ut_check(mp_pause_rule_nav(&s, 0, 4001u, &gave_up) == MP_PAUSE_NAV_CANCEL,
             "and takes the close up again: the next idle read answers a cancel");
    ut_check(mp_pause_rule_nav(&s, 0, 4000u + MP_PAUSE_FORCE_GIVE_UP_MS, &gave_up) == 0 &&
                 gave_up,
             "with the whole time again from that key before it is given up a second time");
    ut_check(s.cancels == 2u && s.given_up_total == 2u && s.rearmed_total == 1u,
             "two cancels handed, two give ups counted, one start over");
    (void)mp_pause_rule_leave(&s, 0, 9000u, NULL, 0u);

    memset(&s, 0, sizeof s);
    (void)mp_pause_rule_open(&s, true, 0, 0u, 0u, &said);
    ut_check(mp_pause_rule_session_ended(&s, 5u) && s.reason == MP_PAUSE_REASON_SESSION,
             "the end of the transport closes it");
    ut_check(!mp_pause_rule_session_ended(&s, 6u), "and is latched once");
    (void)mp_pause_rule_leave(&s, 0, 10u, NULL, 0u);

    mp_armed_set_transport(false, false);
}

static void check_the_frames(void)
{
    mp_pause_session_t s;
    mp_pause_look_t    look;
    bool               said;

    ut_section("the frames under the menu: with the gate held, and drawn long after a substep");

    mp_armed_set_transport(true, true);
    memset(&s, 0, sizeof s);
    (void)mp_pause_rule_open(&s, true, 0, 7u, 0u, &said);

    look = quiet_look(10u, 8u);      /* a substep ran */
    mp_pause_rule_look(&s, &look);
    look = quiet_look(60u, 8u);      /* the ordinary gap */
    mp_pause_rule_look(&s, &look);
    look = quiet_look(200u, 8u);     /* a message box: no substep for 190 ms */
    mp_pause_rule_look(&s, &look);
    look = quiet_look(210u, 8u);
    look.gate_held = true;           /* and a load */
    mp_pause_rule_look(&s, &look);
    look = quiet_look(220u, 9u);     /* the world again */
    mp_pause_rule_look(&s, &look);
    (void)mp_pause_rule_leave(&s, 5, 230u, NULL, 0u);

    ut_check(s.last.frames == 5u, "five menu frames");
    ut_check(s.last.gate_frames == 1u, "one of them with the gate held");
    ut_check(s.last.stalled_frames == 2u, "two drawn over 100 ms after the last substep");
    ut_check(s.last.longest_stretch_ms == 200u, "the longest stretch without one is 200 ms");
    ut_check(s.last.substeps == 2u, "two substeps ran while it was up");
    ut_check(s.last.loaded && s.left_after_load == 1u, "and the leaving counts as after a load");
    mp_armed_set_transport(false, false);
}

static void check_the_pump(void)
{
    ut_section("the player list over a session's menu pumps the world as the engine's screens do");

    ut_check(mp_pause_rule_pump(true, true, 0u, true) == MP_PAUSE_PUMP_WORLD,
             "a nought backdrop cell over a session's menu pumps the world");
    ut_check(mp_pause_rule_pump(true, true, 1u, true) == MP_PAUSE_PUMP_MENU,
             "a set one keeps the menu's own pump, as the engine would");
    ut_check(mp_pause_rule_pump(false, true, 0u, true) == MP_PAUSE_PUMP_MENU,
             "outside a session's menu the list keeps its own pump");
    ut_check(mp_pause_rule_pump(true, false, 0u, true) == MP_PAUSE_PUMP_MENU,
             "an unread cell keeps it too");
    ut_check(mp_pause_rule_pump(true, true, 0u, false) == MP_PAUSE_PUMP_MENU,
             "and so does a world pump that did not resolve");
}

/* One predicate: the note is said where the cell is set, from the same values. */
static void check_one_predicate(void)
{
    session_note_t note;
    uint32_t       said_before = 0u;
    uint32_t       said_after = 0u;

    ut_section("the transport cell and the session note are one state with one writer");

    mp_armed_note_counts(&said_before, NULL);
    mp_armed_set_transport(true, true);
    memset(&note, 0, sizeof note);
    ut_check(session_note_read(&note) && note.running && note.is_host && !note.input_held,
             "a transport that goes up is said at once: running, this machine the host");
    ut_check(mp_armed_transport() && mp_armed_is_host(), "and the cell answers the same");

    ut_check(mp_armed_hold_input(MP_ARMED_HOLDER_PAUSE, true), "a hold is said");
    ut_check(session_note_read(&note) && note.running && note.is_host && note.input_held,
             "with the session's own two facts kept as they were");
    ut_check(mp_armed_hold_input(MP_ARMED_HOLDER_CHAT, true) && session_note_read(&note) &&
                 note.input_held,
             "a second holder is said the same way");

    mp_armed_set_transport(false, false);
    ut_check(session_note_read(&note) && !note.running && !note.is_host && !note.input_held,
             "a transport that comes down takes both holds with it");
    ut_check(!mp_armed_input_held(), "in the cell as well");
    (void)mp_armed_hold_input(MP_ARMED_HOLDER_PAUSE, true);
    (void)mp_armed_hold_input(MP_ARMED_HOLDER_CHAT, true);
    ut_check(!mp_armed_input_held() && session_note_read(&note) && !note.input_held,
             "and no hold stands while no transport does, whoever asks for one");

    mp_armed_note_counts(&said_after, NULL);
    ut_check(said_after >= said_before + 5u, "every one of those was said");
}

int main(void)
{
    check_the_exit_against_the_engine();
    check_the_way_in();
    check_the_id_table();
    check_every_way_out();
    check_the_holders();
    check_the_reasons();
    check_the_frames();
    check_the_pump();
    check_one_predicate();
    return ut_summary("mp_pause");
}
