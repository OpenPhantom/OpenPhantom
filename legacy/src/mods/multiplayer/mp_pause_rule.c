/* mp_pause_rule.c: the pause menu of a session, as rules. See the header. */
#include "mp_pause_rule.h"

#include "mp_armed.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

bool mp_pause_rule_holds_action(int32_t action)
{
    /* By id, for all four readers. The menus' own ids pass whoever asks, and every other id is
     * held whoever asks, which also holds the reads another mod makes of these readers from
     * outside the image. The menu's pointer is not among them: it is the window's own mouse
     * position, and the cursor cells the axis reader feeds have no reader at all. */
    return action >= 0 && action < MP_PAUSE_FIRST_MENU_ACTION;
}

mp_pause_way_t mp_pause_rule_way(bool open, bool transport, bool input_split)
{
    /* Asked first: the menu that is up decides for itself, whatever became of the transport under
     * it, and a second press must not reach the engine's pause from inside it. */
    if (open) {
        return MP_PAUSE_WAY_ALREADY_OPEN;
    }
    if (!transport) {
        return MP_PAUSE_WAY_ENGINE;
    }
    /* A menu over a running world with nothing holding the input would let the player walk and
     * shoot behind it, so without the split the engine's own pause is the honest answer. */
    return input_split ? MP_PAUSE_WAY_SESSION : MP_PAUSE_WAY_ENGINE_UNHELD;
}

const char *mp_pause_rule_reason_text(mp_pause_reason_t reason)
{
    switch (reason) {
    case MP_PAUSE_REASON_DEATH:
        return "a death";
    case MP_PAUSE_REASON_SCENE:
        return "a scene lock that rose";
    case MP_PAUSE_REASON_LEVEL:
        return "the level's outcome left 2";
    case MP_PAUSE_REASON_SESSION:
        return "the session ended";
    case MP_PAUSE_REASON_NONE:
    case MP_PAUSE_REASON_COUNT:
    default:
        break;
    }
    return "the player";
}

/* Clears everything that belongs to one opening and nothing that belongs to all of them. */
static void clear_the_opening(mp_pause_session_t *s)
{
    s->open               = false;
    s->opened_ms          = 0u;
    s->opened_substeps    = 0u;
    s->lock_floor         = 0;
    s->reason             = MP_PAUSE_REASON_NONE;
    s->reason_ms          = 0u;
    s->cancels            = 0u;
    s->given_up           = false;
    s->loaded             = false;
    s->frames             = 0u;
    s->gate_frames        = 0u;
    s->stalled_frames     = 0u;
    s->last_substeps      = 0u;
    s->last_substep_ms    = 0u;
    s->longest_stretch_ms = 0u;
}

bool mp_pause_rule_open(mp_pause_session_t *session, bool lock_read, int32_t lock,
                        uint32_t substeps, uint32_t now_ms, bool *note_said)
{
    bool said;

    if (note_said != NULL) {
        *note_said = false;
    }
    if (session == NULL || session->open) {
        return false;
    }
    clear_the_opening(session);
    session->open            = true;
    session->opened_ms       = now_ms;
    session->opened_substeps = substeps;
    session->lock_floor      = lock_read ? lock : 0;
    session->last_substeps   = substeps;
    session->last_substep_ms = now_ms;
    ++session->opened_total;

    said = mp_armed_hold_input(MP_ARMED_HOLDER_PAUSE, true);
    if (!said) {
        ++session->held_note_refusals;
    }
    if (note_said != NULL) {
        *note_said = said;
    }
    return true;
}

/* The first reason this look finds, in the order that names the cause best: a death ends the
 * level too, and naming it by the outcome would hide why. A lock is judged on its rise: one that
 * stood when the menu opened, a host's scene a client pressed ESC in, keeps it open, and a lock
 * that has fallen since lowers the floor so that the next one to rise closes it. */
static mp_pause_reason_t judge(mp_pause_session_t *s, const mp_pause_look_t *look)
{
    if ((look->health_read && look->health <= 0) || look->dead) {
        return MP_PAUSE_REASON_DEATH;
    }
    if (look->lock_read) {
        if (look->lock > s->lock_floor) {
            return MP_PAUSE_REASON_SCENE;
        }
        if (look->lock < s->lock_floor) {
            s->lock_floor = look->lock;
        }
    }
    if (look->outcome_read && look->outcome != MP_PAUSE_OUTCOME_RUNNING) {
        return MP_PAUSE_REASON_LEVEL;
    }
    return MP_PAUSE_REASON_NONE;
}

/* The frame's place between two substeps. A substep that ran since the last look starts the
 * stretch over; otherwise the stretch is the time since the last one, and a frame drawn more than
 * the stall time after it counts as drawn over a stopped world. */
static void count_the_frame(mp_pause_session_t *s, const mp_pause_look_t *look)
{
    uint32_t stretch;

    ++s->frames;
    if (look->substeps != s->last_substeps) {
        s->last_substeps   = look->substeps;
        s->last_substep_ms = look->now_ms;
    }
    stretch = look->now_ms - s->last_substep_ms;
    if (stretch > MP_PAUSE_STALL_MS) {
        ++s->stalled_frames;
    }
    if (stretch > s->longest_stretch_ms) {
        s->longest_stretch_ms = stretch;
    }
    if (look->gate_held) {
        ++s->gate_frames;
        s->loaded = true;
    }
}

void mp_pause_rule_look(mp_pause_session_t *session, const mp_pause_look_t *look)
{
    mp_pause_reason_t found;

    if (session == NULL || look == NULL || !session->open) {
        return;
    }
    count_the_frame(session, look);
    if (look->gate_held || session->reason != MP_PAUSE_REASON_NONE) {
        return;
    }
    found = judge(session, look);
    if (found != MP_PAUSE_REASON_NONE) {
        session->reason    = found;
        session->reason_ms = look->now_ms;
    }
}

bool mp_pause_rule_session_ended(mp_pause_session_t *session, uint32_t now_ms)
{
    if (session == NULL || !session->open || session->reason != MP_PAUSE_REASON_NONE) {
        return false;
    }
    session->reason    = MP_PAUSE_REASON_SESSION;
    session->reason_ms = now_ms;
    return true;
}

int32_t mp_pause_rule_nav(mp_pause_session_t *session, int32_t code, uint32_t now_ms,
                          bool *gave_up)
{
    if (gave_up != NULL) {
        *gave_up = false;
    }
    if (session == NULL || !session->open || session->reason == MP_PAUSE_REASON_NONE) {
        return code;
    }
    /* Given up, the close waits for the player: a key of his, the one that answered a message box
     * standing over the menu say, starts it over with the whole time again. Without that the menu
     * stayed over a corpse, or over a player already standing again, until he shut it himself. */
    if (session->given_up) {
        if (code != 0) {
            session->given_up  = false;
            session->reason_ms = now_ms;
            ++session->rearmed_total;
        }
        return code;
    }
    /* Only an idle read is answered. A message box reads the codes too and has no answer for a
     * cancel, so replacing the player's own codes there would leave no way to dismiss it. */
    if (code != 0) {
        return code;
    }
    if (now_ms - session->reason_ms >= MP_PAUSE_FORCE_GIVE_UP_MS) {
        session->given_up = true;
        ++session->given_up_total;
        if (gave_up != NULL) {
            *gave_up = true;
        }
        return code;
    }
    ++session->cancels;
    return MP_PAUSE_NAV_CANCEL;
}

/* The cells the engine's own pause leaves for this answer, in its order. It leaves the gate held
 * on "leave the level", because it held it on the way in; the session's menu never held it, so it
 * holds it here, and the level ends on the same simulation state. Every other answer frees the
 * gate, which is also the only thing that frees one a load from inside the menu has held. */
static size_t exit_writes(int32_t reply, mp_pause_write_t *writes, size_t capacity)
{
    mp_pause_write_t plan[MP_PAUSE_EXIT_WRITES_MAX];
    size_t           count = 0;
    size_t           i;

    if (reply == MP_PAUSE_REPLY_QUIT) {
        plan[count].cell  = MP_PAUSE_WRITE_OUTCOME;
        plan[count].value = MP_PAUSE_OUTCOME_QUIT;
        ++count;
        plan[count].cell  = MP_PAUSE_WRITE_RESTORE;
        plan[count].value = MP_PAUSE_RESTORE_QUIT;
        ++count;
        plan[count].cell  = MP_PAUSE_WRITE_GATE;
        plan[count].value = MP_PAUSE_GATE_HELD;
        ++count;
    } else {
        plan[count].cell  = MP_PAUSE_WRITE_GATE;
        plan[count].value = MP_PAUSE_GATE_FREE;
        ++count;
    }
    if (writes == NULL) {
        return 0u;
    }
    for (i = 0; i < count && i < capacity; ++i) {
        writes[i] = plan[i];
    }
    return i;
}

static void count_the_leaving(mp_pause_session_t *s, int32_t reply, uint32_t now_ms)
{
    s->last.reply              = reply;
    s->last.reason             = s->reason;
    s->last.duration_ms        = now_ms - s->opened_ms;
    s->last.frames             = s->frames;
    s->last.gate_frames        = s->gate_frames;
    s->last.stalled_frames     = s->stalled_frames;
    s->last.longest_stretch_ms = s->longest_stretch_ms;
    s->last.substeps           = s->last_substeps - s->opened_substeps;
    s->last.cancels            = s->cancels;
    s->last.loaded             = s->loaded;

    if (reply == MP_PAUSE_REPLY_QUIT) {
        ++s->left_to_quit;
    } else if (reply < 0) {
        ++s->refused_by_engine;
    } else {
        ++s->left_to_play;
        if (s->loaded) {
            ++s->left_after_load;
        }
    }
    ++s->closed_by[s->reason < MP_PAUSE_REASON_COUNT ? s->reason : MP_PAUSE_REASON_NONE];
    s->frames_total         += s->frames;
    s->gate_frames_total    += s->gate_frames;
    s->stalled_frames_total += s->stalled_frames;
    s->substeps_total       += s->last.substeps;
    if (s->longest_stretch_ms > s->longest_stretch_total_ms) {
        s->longest_stretch_total_ms = s->longest_stretch_ms;
    }
}

size_t mp_pause_rule_leave(mp_pause_session_t *session, int32_t reply, uint32_t now_ms,
                           mp_pause_write_t *writes, size_t capacity)
{
    size_t count;

    if (session == NULL || !session->open) {
        return 0u;
    }
    count = exit_writes(reply, writes, capacity);
    count_the_leaving(session, reply, now_ms);
    clear_the_opening(session);
    (void)mp_armed_hold_input(MP_ARMED_HOLDER_PAUSE, false);
    return count;
}

mp_pause_pump_t mp_pause_rule_pump(bool session_menu_open, bool backdrop_read,
                                   uint32_t backdrop, bool world_pump_known)
{
    if (!session_menu_open || !backdrop_read || !world_pump_known) {
        return MP_PAUSE_PUMP_MENU;
    }
    return backdrop == 0u ? MP_PAUSE_PUMP_WORLD : MP_PAUSE_PUMP_MENU;
}
