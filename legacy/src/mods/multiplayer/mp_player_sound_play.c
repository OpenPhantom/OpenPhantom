/* mp_player_sound_play.c: a far player's own sounds and shield, carried out at his puppet. See the
 * header.
 *
 * Three things of the engine's are written here, each the way the engine itself writes it: the
 * body's shield slot at +0x100, which the engine's pickup fills with the shield it created and its
 * release empties to -1; and bit 0 of the body's flags, drawn, which the engine's burst clears and
 * which is set again only on the body this file burst, only once its player stands again, and only
 * while the bit is still clear.
 */
#include "mp_player_sound_play.h"

#include "mp_bank.h"
#include "mp_body.h"
#include "mp_player_sound.h"
#include "mp_player_sound_rule.h"
#include "mp_signatures_player_sound.h"

#include "common/logging.h"
#include "common/memory.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The player record's place, the point the engine's own cries are played at. */
#define RECORD_POS 0x118u

/* The body's flag word, whose bit 0 is drawn, and its shield slot, -1 for none. */
#define OBJECT_FLAGS     0x00u
#define OBJECT_DRAWN     0x01u
#define OBJECT_SHIELD_ID 0x100u
#define NO_SHIELD        (-1)

/* How many due moments one window may leave for after it. A player makes a handful in a substep at
 * the very most; the rest are counted. */
#define PENDING_MAX 8u

typedef int32_t(__cdecl *play_by_name_fn_t)(int32_t cue, const char *name, int32_t *handle,
                                            const float *at, uint32_t flags);
typedef void(__cdecl *burst_fn_t)(uint32_t body, float speed, int32_t unused);
typedef int32_t(__cdecl *shield_create_fn_t)(uint32_t owner);
typedef int32_t(__cdecl *shield_release_fn_t)(int32_t id);

/* One moment that fell due in the window, with what it needs after it. */
typedef struct pending {
    uint8_t                what;
    mp_player_sound_plan_t plan;
    float                  at[3];
    bool                   placed;   /* `at` read */
    uint32_t               object;
} pending_t;

/* One far body: what its window left, the shield this file put on it, and whether this file
 * burst it. */
typedef struct far_sound {
    pending_t pending[PENDING_MAX];
    size_t    pending_count;

    bool      shield_held;
    uint32_t  shield_object;
    int32_t   shield_id;
    uint8_t   shield_seconds;
    uint32_t  shield_substeps;
    bool      shield_seen_down;

    bool      burst_waiting;    /* the burst is due, and the pose still says he stands */
    uint32_t  burst_wait_object;
    uint32_t  burst_wait_substeps;

    bool      burst_held;
    uint32_t  burst_object;
    bool      burst_seen_down;
} far_sound_t;

/* Per kind: arrived, played or put on, too late, and no body here for its slot. */
typedef struct kind_counts {
    uint32_t arrived;
    uint32_t played;
    uint32_t late;
    uint32_t no_body;
} kind_counts_t;

typedef struct play_state {
    bool                bound_once;
    bool                bound;
    play_by_name_fn_t   play_by_name;
    burst_fn_t          burst;
    float               burst_speed;
    shield_create_fn_t  shield_create;
    shield_release_fn_t shield_release;
    uint32_t            names;
    uint32_t            death_voices;
    uint32_t            burn_voices;

    far_sound_t far[MP_BANK_FAR_MAX + 1u];   /* by far bank, 0 the spare */

    /* The one cell every sound is handed its place in. The engine copies the point during the
     * call, and a place that is static is read only then, so one cell serves every call. */
    float at[3];

    kind_counts_t kind[MP_PLAYER_SOUND_KIND_MAX + 1u];
    uint32_t      silent;
    uint32_t      gate_refused;
    uint32_t      torn;
    uint32_t      unbound;
    uint32_t      unreadable;
    uint32_t      dropped;
    uint32_t      bursts;
    uint32_t      burst_refused;
    uint32_t      burst_standing;
    uint32_t      shown_again;
    uint32_t      shield_refused;
    uint32_t      shield_taken_off;
    uint32_t      shield_gone;
    uint32_t      shield_at_reentry;
    uint32_t      shield_past_length;
} play_state_t;

static play_state_t play;

static far_sound_t *far_of(size_t bank)
{
    return &play.far[bank <= MP_BANK_FAR_MAX ? bank : 0u];
}

/* The engine's entries and tables, taken from the sites once. A reading whose pairs disagree binds
 * nothing, and every moment is then counted as having nothing to play it with. */
static bool bind(void)
{
    const mp_player_sound_engine_t *engine;

    if (play.bound_once) {
        return play.bound;
    }
    play.bound_once     = true;
    engine              = mp_signatures_player_sound_engine();
    play.play_by_name   = (play_by_name_fn_t)engine->play_by_name;
    play.burst          = (burst_fn_t)engine->burst;
    play.burst_speed    = engine->burst_speed;
    play.shield_create  = (shield_create_fn_t)engine->shield_create;
    play.shield_release = (shield_release_fn_t)engine->shield_release;
    play.names          = engine->names;
    play.death_voices   = engine->death_voices;
    play.burn_voices    = engine->burn_voices;
    play.bound = engine->agree && play.play_by_name != NULL && play.names != 0u &&
                 play.death_voices != 0u && play.burn_voices != 0u;
    if (!play.bound) {
        log_warning("a far player's own sounds cannot be played here: the voice entry, the names "
                    "or the voice tables did not resolve, or two sites disagree");
    }
    return play.bound;
}

void mp_player_sound_due(size_t bank, uint32_t record, uint32_t object, const mp_event_t *moment,
                         uint32_t render_tick, bool render_known)
{
    far_sound_t *far = far_of(bank);
    pending_t   *p;
    uint8_t      what;

    if (moment == NULL || moment->kind != MP_EVENT_PLAYER_SOUND ||
        moment->sound_what > MP_PLAYER_SOUND_KIND_MAX) {
        ++play.torn;
        return;
    }
    what = moment->sound_what;
    ++play.kind[what].arrived;
    if (far->pending_count >= PENDING_MAX) {
        ++play.dropped;
        return;
    }
    p = &far->pending[far->pending_count];
    memset(p, 0, sizeof *p);
    mp_player_sound_rule_plan(what, moment->sound_index, moment->sound_flags, &p->plan);
    if (!p->plan.valid) {
        ++play.torn;
        return;
    }
    /* A sound at a moment long gone is a sound at nothing. A shield is the state of the body, and
     * a late word about it is still the truth. */
    if (!p->plan.shield_on && !p->plan.shield_off &&
        mp_player_sound_rule_too_late(moment->tick, render_tick, render_known)) {
        ++play.kind[what].late;
        return;
    }
    p->what   = what;
    p->object = object;
    p->placed = record != 0u &&
                memory_try_read((uintptr_t)record + RECORD_POS, p->at, sizeof p->at);
    ++far->pending_count;
}

/* The name a plan's sound byte comes to, out of this machine's own tables. */
static const char *name_for(const mp_player_sound_plan_t *plan)
{
    uint32_t index = plan->row;
    uint32_t name = 0u;

    if (plan->table == MP_PLAYER_SOUND_TABLE_NONE) {
        return NULL;   /* a shield: nothing to play */
    }
    if (plan->table == MP_PLAYER_SOUND_TABLE_DEATH || plan->table == MP_PLAYER_SOUND_TABLE_BURN) {
        uint32_t table = plan->table == MP_PLAYER_SOUND_TABLE_DEATH ? play.death_voices
                                                                    : play.burn_voices;

        if (!memory_try_read((uintptr_t)table + plan->row * sizeof(uint32_t), &index,
                             sizeof index)) {
            return NULL;
        }
    }
    if (index >= MP_PLAYER_SOUND_NAMES ||
        !memory_try_read((uintptr_t)play.names + index * sizeof(uint32_t), &name, sizeof name) ||
        name == 0u) {
        return NULL;
    }
    return (const char *)(uintptr_t)name;
}

static void voice(const pending_t *p)
{
    const char *name;
    int32_t     channel;

    if (p->plan.silent) {
        ++play.silent;
        return;
    }
    name = name_for(&p->plan);
    if (name == NULL || !p->placed) {
        ++play.unreadable;
        return;
    }
    memcpy(play.at, p->at, sizeof play.at);
    channel = play.play_by_name(p->plan.cue, name, NULL, play.at, p->plan.engine_flags);
    if (channel < 0) {
        ++play.gate_refused;   /* out of range, the same wav still playing, or no channel */
        return;
    }
    ++play.kind[p->what].played;
}

/* The engine's burst on the puppet: drawn before and hidden after is a burst, whatever its own
 * gates decided. Only a body whose player lies is burst, so a body that is never seen lying again
 * can never be left hidden. */
static void burst_now(far_sound_t *far, uint32_t object)
{
    uint32_t before = 0u;
    uint32_t after = 0u;

    if (play.burst == NULL ||
        !memory_try_read((uintptr_t)object + OBJECT_FLAGS, &before, sizeof before)) {
        ++play.burst_refused;
        return;
    }
    play.burst(object, play.burst_speed, 0);
    if ((before & OBJECT_DRAWN) == 0u ||
        !memory_try_read((uintptr_t)object + OBJECT_FLAGS, &after, sizeof after) ||
        (after & OBJECT_DRAWN) != 0u) {
        ++play.burst_refused;   /* hidden already, too wide, too few free objects */
        return;
    }
    ++play.bursts;
    far->burst_held      = true;
    far->burst_object    = object;
    far->burst_seen_down = true;
}

/* The moment and the state that says he lies are stamped with one tick, but the state can arrive
 * a sample later when the one before was lost. So a burst that finds him standing waits for him to
 * lie for as long as a moment may be late, and is then given up. */
static void burst_body(size_t bank, far_sound_t *far, const pending_t *p)
{
    if (p->object == 0u) {
        ++play.kind[p->what].no_body;
        return;
    }
    if (mp_body_far_player_stands(bank)) {
        far->burst_waiting       = true;
        far->burst_wait_object   = p->object;
        far->burst_wait_substeps = 0u;
        return;
    }
    burst_now(far, p->object);
}

static void take_shield_off(far_sound_t *far)
{
    int32_t slot = NO_SHIELD;

    if (play.shield_release != NULL &&
        memory_try_read((uintptr_t)far->shield_object + OBJECT_SHIELD_ID, &slot, sizeof slot) &&
        slot == far->shield_id) {
        (void)play.shield_release(far->shield_id);   /* and the slot goes back to -1 */
        ++play.shield_taken_off;
    } else {
        ++play.shield_gone;   /* the engine let it go with the body it hung on */
    }
    far->shield_held = false;
}

static void shield_on(far_sound_t *far, const pending_t *p)
{
    int32_t slot = NO_SHIELD;
    int32_t id;

    if (p->object == 0u) {
        ++play.kind[p->what].no_body;
        return;
    }
    if (far->shield_held && far->shield_object == p->object) {
        far->shield_seconds  = p->plan.seconds;   /* a second pickup starts the time again */
        far->shield_substeps = 0u;
        ++play.kind[p->what].played;
        return;
    }
    if (play.shield_create == NULL ||
        !memory_try_read((uintptr_t)p->object + OBJECT_SHIELD_ID, &slot, sizeof slot) ||
        slot >= 0) {
        ++play.shield_refused;
        return;
    }
    id = play.shield_create(p->object);
    if (id < 0 ||
        !memory_try_write((uintptr_t)p->object + OBJECT_SHIELD_ID, &id, sizeof id)) {
        ++play.shield_refused;   /* the engine's pool of 31 shields is full */
        return;
    }
    far->shield_held      = true;
    far->shield_object    = p->object;
    far->shield_id        = id;
    far->shield_seconds   = p->plan.seconds;
    far->shield_substeps  = 0u;
    far->shield_seen_down = false;
    ++play.kind[p->what].played;
}

static void carry_out(size_t bank, far_sound_t *far, const pending_t *p)
{
    if (p->plan.shield_on) {
        shield_on(far, p);
        return;
    }
    if (p->plan.shield_off) {
        if (far->shield_held) {
            take_shield_off(far);
            ++play.kind[p->what].played;
        } else {
            ++play.shield_gone;
        }
        return;
    }
    if (p->plan.burst) {
        burst_body(bank, far, p);   /* before the burning cry, as the engine's fire arm does */
    }
    voice(p);
}

/* The shield and the burst, looked after once a substep: a player who has come back from a death
 * stands in a body his machine built new, which wears no shield and was never burst. */
static void look_after(size_t bank, far_sound_t *far)
{
    bool stands;
    uint32_t flags = 0u;

    if (!far->shield_held && !far->burst_held && !far->burst_waiting) {
        return;
    }
    stands = mp_body_far_player_stands(bank);
    if (far->burst_waiting) {
        if (!stands) {
            far->burst_waiting = false;
            burst_now(far, far->burst_wait_object);
        } else if (++far->burst_wait_substeps > MP_PLAYER_SOUND_LATE_SUBSTEPS) {
            far->burst_waiting = false;
            ++play.burst_standing;   /* he never lay: the burst belongs to a body gone */
        }
    }
    if (far->shield_held) {
        ++far->shield_substeps;
        far->shield_seen_down = far->shield_seen_down || !stands;
        if (mp_player_sound_rule_came_back(far->shield_seen_down, stands)) {
            take_shield_off(far);
            ++play.shield_at_reentry;
        } else if (mp_player_sound_rule_shield_expired(far->shield_substeps,
                                                       far->shield_seconds)) {
            take_shield_off(far);
            ++play.shield_past_length;
        }
    }
    if (far->burst_held) {
        far->burst_seen_down = far->burst_seen_down || !stands;
        if (mp_player_sound_rule_came_back(far->burst_seen_down, stands)) {
            far->burst_held = false;
            if (memory_try_read((uintptr_t)far->burst_object + OBJECT_FLAGS, &flags,
                                sizeof flags) &&
                (flags & OBJECT_DRAWN) == 0u) {
                flags |= OBJECT_DRAWN;
                if (memory_try_write((uintptr_t)far->burst_object + OBJECT_FLAGS, &flags,
                                     sizeof flags)) {
                    ++play.shown_again;
                }
            }
        }
    }
}

void mp_player_sound_after_window(size_t bank)
{
    far_sound_t *far = far_of(bank);
    size_t       i;

    if (far->pending_count != 0u) {
        if (!bind()) {
            play.unbound += (uint32_t)far->pending_count;
        } else {
            for (i = 0; i < far->pending_count; ++i) {
                carry_out(bank, far, &far->pending[i]);
            }
        }
        far->pending_count = 0u;
    }
    look_after(bank, far);
}

void mp_player_sound_unplaced(const mp_event_t *moment)
{
    if (moment == NULL || moment->kind != MP_EVENT_PLAYER_SOUND ||
        moment->sound_what > MP_PLAYER_SOUND_KIND_MAX) {
        return;
    }
    ++play.kind[moment->sound_what].arrived;
    ++play.kind[moment->sound_what].no_body;
}

void mp_player_sound_forget(size_t bank)
{
    far_sound_t *far = far_of(bank);

    /* Nothing of the engine's is touched: a body that is gone took its shield with it, and one
     * that starts over is shown by whoever builds it. */
    memset(far, 0, sizeof *far);
}

static void report_the_sounds(void)
{
    const kind_counts_t *death  = &play.kind[MP_PLAYER_SOUND_DEATH];
    const kind_counts_t *burn   = &play.kind[MP_PLAYER_SOUND_BURN];
    const kind_counts_t *pickup = &play.kind[MP_PLAYER_SOUND_PICKUP];
    const kind_counts_t *key    = &play.kind[MP_PLAYER_SOUND_KEY];
    const kind_counts_t *water  = &play.kind[MP_PLAYER_SOUND_WATER];
    const kind_counts_t *ground = &play.kind[MP_PLAYER_SOUND_GROUND];

    log_info("a far player's sounds (arrived/played/too late/no body here for that slot): death "
             "%u/%u/%u/%u (%u scripted and silent), burning %u/%u/%u/%u, pickup %u/%u/%u/%u, key "
             "%u/%u/%u/%u, water %u/%u/%u/%u, ground %u/%u/%u/%u; %u refused by the engine's "
             "start gate (distance, a duplicate or no channel), %u torn, %u with nothing here to "
             "play them, %u whose name or place did not read, %u dropped from a full window",
             (unsigned)death->arrived, (unsigned)death->played, (unsigned)death->late,
             (unsigned)death->no_body, (unsigned)play.silent,
             (unsigned)burn->arrived, (unsigned)burn->played, (unsigned)burn->late,
             (unsigned)burn->no_body, (unsigned)pickup->arrived, (unsigned)pickup->played,
             (unsigned)pickup->late, (unsigned)pickup->no_body, (unsigned)key->arrived,
             (unsigned)key->played, (unsigned)key->late, (unsigned)key->no_body,
             (unsigned)water->arrived, (unsigned)water->played, (unsigned)water->late,
             (unsigned)water->no_body, (unsigned)ground->arrived, (unsigned)ground->played,
             (unsigned)ground->late, (unsigned)ground->no_body, (unsigned)play.gate_refused,
             (unsigned)play.torn, (unsigned)play.unbound, (unsigned)play.unreadable,
             (unsigned)play.dropped);
}

static void report_the_burst_and_the_shield(void)
{
    const kind_counts_t *up   = &play.kind[MP_PLAYER_SOUND_SHIELD_ON];
    const kind_counts_t *down = &play.kind[MP_PLAYER_SOUND_SHIELD_OFF];

    log_info("  a far player's burst and shield: %u burst into pieces, %u refused by the engine's "
             "gates, %u given up because he stood all through the window, %u shown again when he "
             "stood | shield %u/%u/%u up (arrived/put on/no body here for that slot), %u refused "
             "by the engine, %u/%u down (arrived/taken off), %u already gone, %u taken off at his "
             "return, %u past its length",
             (unsigned)play.bursts, (unsigned)play.burst_refused, (unsigned)play.burst_standing,
             (unsigned)play.shown_again, (unsigned)up->arrived, (unsigned)up->played,
             (unsigned)up->no_body, (unsigned)play.shield_refused, (unsigned)down->arrived,
             (unsigned)down->played, (unsigned)play.shield_gone,
             (unsigned)play.shield_at_reentry, (unsigned)play.shield_past_length);
}

static void report_what_went_out(void)
{
    mp_player_sound_sent_t sent;

    mp_player_sound_sent_counts(&sent);
    log_info("  this player's sounds for the far side: death %u (burning %u), pickup %u, key %u, "
             "water %u, ground %u, shield up %u and down %u; %u left to the engine (no session, "
             "or not this player's body), %u whose record or timer did not read, %u with no site "
             "for the name, %u with nowhere to queue; %u of 4 calls caught",
             (unsigned)sent.sent[MP_PLAYER_SOUND_DEATH], (unsigned)sent.sent[MP_PLAYER_SOUND_BURN],
             (unsigned)sent.sent[MP_PLAYER_SOUND_PICKUP], (unsigned)sent.sent[MP_PLAYER_SOUND_KEY],
             (unsigned)sent.sent[MP_PLAYER_SOUND_WATER],
             (unsigned)sent.sent[MP_PLAYER_SOUND_GROUND],
             (unsigned)sent.sent[MP_PLAYER_SOUND_SHIELD_ON],
             (unsigned)sent.sent[MP_PLAYER_SOUND_SHIELD_OFF], (unsigned)sent.left,
             (unsigned)sent.unread, (unsigned)sent.unsited, (unsigned)sent.unqueued,
             (unsigned)sent.redirected);
}

void mp_player_sound_report(void)
{
    report_the_sounds();
    report_the_burst_and_the_shield();
    report_what_went_out();
}
