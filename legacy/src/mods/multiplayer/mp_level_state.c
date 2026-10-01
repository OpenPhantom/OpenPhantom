/* mp_level_state.c: the level's state, sent by the host and matched by a client. See the header.
 *
 * The host's build and send, a client's take and the order it takes notes in, the journal and the
 * director's hands all read the one state below, and the fail-open answer is asked by all of them.
 * What a client does with a note it took is mp_level_state_apply.c; the report, which only reads,
 * is mp_level_state_report.c; the director's fog the level shares is mp_level_state_fog.c, the fog
 * of a room is mp_fog_viewers.c, and both are driven from the two entry points of a substep here.
 */
#include "mp_level_state.h"

#include "mp_actor_loop.h"
#include "mp_channel.h"
#include "mp_fog_viewers.h"
#include "mp_level_state_apply.h"
#include "mp_level_state_bind.h"
#include "mp_level_state_drawn.h"
#include "mp_level_state_fog.h"
#include "mp_level_state_internal.h"
#include "mp_level_state_journal_rule.h"
#include "mp_level_state_report.h"

#include "common/logging.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define LIGHT_BYTES ((MP_LEVEL_STATE_MAX_LIGHTS + 7u) / 8u)

typedef struct host_side {
    bool               sent_before;
    uint32_t           last_sent_tick;
    bool               change_before;
    uint32_t           last_change_tick;
    uint8_t            last_sent[MP_LEVEL_STATE_MAX_BYTES];
    size_t             last_sent_bytes;
    uint8_t            last_read[MP_LEVEL_STATE_MAX_EMITTERS];   /* for changes of its own */
    bool               read_known;
    uint16_t           read_emitters;
    uint8_t            last_lights[LIGHT_BYTES];
    bool               lights_known;
    uint16_t           read_lights;
    bool               escort_known;
    uint8_t            escort_said;   /* the health a journal entry would carry, 0 when down */
    mp_level_journal_t journal;
} host_side_t;

typedef struct client_side {
    bool                  holding;
    bool                  no_level_counted;
    mp_level_state_note_t held;
    mp_level_taken_t      taken;
    bool                  journal_known;   /* a note of this level and generation was applied */
    uint16_t              journal_last;
} client_side_t;

typedef struct level_state {
    bool                     arms_hulled;
    bool                     level_known;
    uint16_t                 level;
    uint32_t                 substep;   /* counted where the level is told, once a substep */
    mp_level_state_role_fn_t role;
    mp_level_state_bank_fn_t bank;
    host_side_t              host;
    client_side_t            client;
    mp_level_state_last_t    last;
    mp_level_state_counts_t  n;
    uint32_t                 host_changes[MP_LEVEL_STATE_MAX_EMITTERS];
    bool                     oversize_said;
    bool                     too_large_said;
} level_state_t;

static level_state_t state;

_Static_assert(MP_LEVEL_STATE_MAX_BYTES <= MP_CHANNEL_MESSAGE_BYTES,
               "the largest level state note fits one message of the reliable channel");

bool mp_level_state_install(bool arms_hulled)
{
    state.arms_hulled = arms_hulled;
    return mp_level_state_bind_install();
}

bool mp_level_state_can_match(void)
{
    return state.arms_hulled && mp_level_state_bind_ready() && mp_level_state_bind_has_director();
}

void mp_level_state_set_level(bool known, uint16_t identity)
{
    state.level_known = known;
    state.level       = identity;
    ++state.substep;
}

uint32_t mp_level_state_substep(void)
{
    return state.substep;
}

bool mp_level_state_level(uint16_t *identity)
{
    if (identity != NULL) {
        *identity = state.level;
    }
    return state.level_known;
}

mp_level_side_t mp_level_state_side(void)
{
    bool runs = false;

    if (state.role == NULL) {
        return MP_LEVEL_SIDE_ALONE;
    }
    if (state.role(&runs, NULL)) {
        return MP_LEVEL_SIDE_CLIENT;
    }
    return runs ? MP_LEVEL_SIDE_HOST : MP_LEVEL_SIDE_ALONE;
}

bool mp_level_state_banked(void)
{
    return state.bank != NULL && state.bank() != 0u;
}

bool mp_level_state_switch(mp_level_kind_t kind, uintptr_t actor, int32_t mode,
                           bool client_of_started)
{
    mp_level_verdict_t verdict = MP_LEVEL_VERDICT_WITHOUT;

    if (client_of_started) {
        verdict = mp_level_state_can_match() ? MP_LEVEL_VERDICT_WITHHELD
                                             : MP_LEVEL_VERDICT_FAIL_OPEN;
    }
    mp_level_state_count_switch(kind, actor, mode, verdict,
                                !client_of_started &&
                                    mp_level_state_side() == MP_LEVEL_SIDE_HOST);
    return verdict == MP_LEVEL_VERDICT_WITHHELD;
}

/* ==============================================================================================
 * The director's three classes this module hears, and the journal they and the host's reading
 * write to.
 * ============================================================================================ */

uint16_t mp_level_state_journal_note(uint8_t kind, uint8_t a, uint16_t b, uint32_t c)
{
    uint16_t sequence = mp_level_journal_push(&state.host.journal, kind, a, b, c);

    if (sequence != 0u && kind < (uint8_t)MP_LEVEL_JOURNAL_KINDS) {
        ++state.n.journal_entries[kind];
    }
    return sequence;
}

uint16_t mp_level_state_journal_newest(void)
{
    return state.host.journal.newest;
}

static bool fog_hand(void *actor, int32_t command, int32_t a1, int32_t a2)
{
    return mp_level_state_fog_hear(actor, command, a1, a2, mp_level_state_side(),
                                   mp_level_state_can_match());
}

/* The escort's bar is state the host reads out of its engine; a client that can set it refuses
 * its own scripts' command, whose arm divides by the hit points of an actor it may not have. */
static bool escort_hand(void *actor, int32_t command, int32_t a1, int32_t a2)
{
    (void)actor;
    (void)command;
    (void)a1;
    (void)a2;
    ++state.n.escort_seen;
    if (mp_level_state_side() == MP_LEVEL_SIDE_CLIENT && mp_level_state_bind_escort()) {
        ++state.n.escort_withheld;
        return true;
    }
    ++state.n.escort_through;
    return false;
}

/* A line of text is a moment: the host shows it and journals its text, a client that can replay
 * the host's refuses its own scripts' lines. */
static bool crawl_hand(void *actor, int32_t command, int32_t a1, int32_t a2)
{
    mp_level_side_t side = mp_level_state_side();

    (void)actor;
    (void)command;
    (void)a2;
    ++state.n.crawl_seen;
    if (side == MP_LEVEL_SIDE_CLIENT && mp_level_state_bind_has_director()) {
        ++state.n.crawl_withheld;
        return true;
    }
    if (side == MP_LEVEL_SIDE_HOST && a1 >= 0 && a1 <= (int32_t)UINT16_MAX &&
        mp_level_state_journal_note((uint8_t)MP_LEVEL_JOURNAL_CRAWL, 0u, (uint16_t)a1, 0u) !=
            0u) {
        ++state.n.crawl_journaled;
    }
    ++state.n.crawl_through;
    return false;
}

void mp_level_state_arm(mp_level_state_register_fn_t register_hand, mp_level_state_role_fn_t role,
                        mp_level_state_bank_fn_t bank)
{
    state.role = role;
    state.bank = bank;
    if (register_hand == NULL) {
        return;
    }
    (void)register_hand(MP_DIRECTOR_FOG, &fog_hand, "the level's state");
    (void)register_hand(MP_DIRECTOR_ESCORT, &escort_hand, "the level's state");
    (void)register_hand(MP_DIRECTOR_CRAWL, &crawl_hand, "the level's state");
}

/* ==============================================================================================
 * The host: read, build, and send when the rule says so.
 * ============================================================================================ */

static void read_emitters(uint32_t world, uint32_t count, mp_level_state_note_t *note)
{
    host_side_t *h = &state.host;
    uint32_t     i;

    note->parts |= (uint8_t)MP_LEVEL_STATE_PART_EMITTERS;
    note->emitters = (uint16_t)count;
    for (i = 0; i < count; ++i) {
        bool was_on = h->last_read[i] == (uint8_t)MP_LEVEL_EMITTER_ON;
        bool is_on;

        note->emitter[i] = (uint8_t)mp_level_state_bind_emitter(world, i);
        is_on = note->emitter[i] == (uint8_t)MP_LEVEL_EMITTER_ON;
        state.last.emitters_on += is_on ? 1u : 0u;
        state.last.emitters_off += note->emitter[i] == (uint8_t)MP_LEVEL_EMITTER_OFF ? 1u : 0u;
        state.last.emitters_gone += note->emitter[i] == (uint8_t)MP_LEVEL_EMITTER_GONE ? 1u : 0u;
        state.last.emitters_never += note->emitter[i] == (uint8_t)MP_LEVEL_EMITTER_NEVER ? 1u : 0u;
        if (h->read_known && h->read_emitters == count && h->last_read[i] != note->emitter[i]) {
            ++state.host_changes[i];
            /* What is seen changed: a placement going on, or on and then anything else. */
            if (was_on != is_on) {
                (void)mp_level_state_journal_note((uint8_t)MP_LEVEL_JOURNAL_EMITTER,
                                                  is_on ? 1u : 0u, (uint16_t)i, 0u);
            }
        }
        h->last_read[i] = note->emitter[i];
    }
    h->read_known    = true;
    h->read_emitters = (uint16_t)count;
    state.last.emitters = count;
}

/* All the lights or none: a light that did not read is not "off", and a client would switch it
 * off to match. The note then goes without its light part, and a client keeps its own. */
static void read_lights(uint32_t world, uint32_t count, mp_level_state_note_t *note)
{
    host_side_t *h = &state.host;
    uint32_t     i;
    uint32_t     lit = 0;

    note->lights = (uint16_t)count;
    for (i = 0; i < count; ++i) {
        bool on = false;

        if (!mp_level_state_bind_light(world, i, &on)) {
            note->lights = 0u;
            ++state.n.lights_left_out;
            return;
        }
        mp_level_state_set_light(note, i, on);
        lit += on ? 1u : 0u;
    }
    note->parts |= (uint8_t)MP_LEVEL_STATE_PART_LIGHTS;
    for (i = 0; i < count && h->lights_known && h->read_lights == count; ++i) {
        bool was = (h->last_lights[i >> 3] & (uint8_t)(1u << (i & 7u))) != 0u;
        bool is  = mp_level_state_light_on(note, i);

        if (was != is) {
            (void)mp_level_state_journal_note((uint8_t)MP_LEVEL_JOURNAL_LIGHT, is ? 1u : 0u,
                                              (uint16_t)i, 0u);
        }
    }
    memcpy(h->last_lights, note->light, sizeof h->last_lights);
    h->lights_known = true;
    h->read_lights  = (uint16_t)count;
    state.last.lights_on = lit;
    state.last.lights    = count;
}

/* The escort's bar as the host's engine shows it. A bar that is down says 0 in the journal, which
 * is the health the engine's own setter takes to let it fall. */
static void read_escort(mp_level_state_note_t *note)
{
    host_side_t *h = &state.host;
    uint8_t      health = 0;
    bool         shown = false;
    uint8_t      said;

    state.last.escort_read = mp_level_state_bind_escort() &&
                             mp_level_state_bind_escort_read(&health, &shown);
    if (!state.last.escort_read) {
        return;
    }
    note->parts |= (uint8_t)MP_LEVEL_STATE_PART_ESCORT;
    note->escort_health = health;
    note->escort_flags  = shown ? (uint8_t)MP_LEVEL_STATE_ESCORT_SHOWN : 0u;
    said = shown ? health : 0u;
    if (h->escort_known && said != h->escort_said) {
        (void)mp_level_state_journal_note((uint8_t)MP_LEVEL_JOURNAL_ESCORT, said, 0u, 0u);
    }
    h->escort_known = true;
    h->escort_said  = said;
    state.last.escort_health = health;
    state.last.escort_shown  = shown;
}

static size_t build(uint32_t tick, uint32_t generation, uint8_t *buffer, size_t capacity)
{
    mp_level_state_note_t note;
    uint32_t              world = mp_level_state_bind_world();
    uint32_t              emitters = 0;
    uint32_t              lights = 0;
    size_t                bytes;

    if (world == 0u || !state.level_known ||
        !mp_level_state_bind_counts(world, &emitters, &lights)) {
        return 0u;
    }
    mp_level_state_note_init(&note, tick, state.level, generation);
    memset(&state.last, 0, sizeof state.last);
    if ((emitters > MP_LEVEL_STATE_MAX_EMITTERS || lights > MP_LEVEL_STATE_MAX_LIGHTS) &&
        !state.oversize_said) {
        state.oversize_said = true;
        log_warning("this level has %u emitter placement(s) and %u light(s), and the level's state "
                    "carries at most %u and %u, so the part past that is described to nobody",
                    (unsigned)emitters, (unsigned)lights, (unsigned)MP_LEVEL_STATE_MAX_EMITTERS,
                    (unsigned)MP_LEVEL_STATE_MAX_LIGHTS);
    }
    /* A host whose emitter half did not bind says nothing of the placements, rather than calling
     * every one of them gone. */
    if (emitters <= MP_LEVEL_STATE_MAX_EMITTERS && mp_level_state_bind_ready()) {
        read_emitters(world, emitters, &note);
    }
    if (lights <= MP_LEVEL_STATE_MAX_LIGHTS) {
        read_lights(world, lights, &note);
    }
    read_escort(&note);
    mp_level_state_fog_describe(&note);
    mp_fog_viewers_describe(&note);
    mp_actor_loop_write(&note);   /* the loops the actors' scripts keep */
    /* Last, so the changes read in this substep are in it. */
    mp_level_journal_to_note(&state.host.journal, &note);
    state.last.fog_flags   = (note.parts & MP_LEVEL_STATE_PART_FOG) != 0u ? note.fog.flags : 0u;
    state.last.viewer_runs = note.fog_viewers;
    bytes = mp_level_state_encode(&note, buffer, capacity);
    /* The encoder refuses a note it would write wrong; a host that built one says so, because
     * otherwise its clients would simply stop hearing about the level. */
    if (bytes == 0u) {
        ++state.n.unencodable;
    }
    return bytes;
}

void mp_level_state_send(uint32_t tick, uint32_t generation, mp_level_state_send_fn_t send)
{
    host_side_t    *h = &state.host;
    uint8_t         note[MP_LEVEL_STATE_MAX_BYTES];
    size_t          bytes;
    bool            changed;
    mp_level_send_t due;

    if (send == NULL) {
        return;
    }
    /* The substep's fog first: a ramp counts down, the rooms' runs are found and this side's own
     * player is taken through them, and the readings of the fog as drawn fall due. */
    mp_level_state_fog_host_tick();
    mp_fog_viewers_host_tick();
    mp_level_state_drawn_tick();
    bytes = build(tick, generation, note, sizeof note);
    if (bytes == 0u) {
        return;
    }
    if (bytes > MP_CHANNEL_MESSAGE_BYTES) {
        ++state.n.too_large;
        if (!state.too_large_said) {
            state.too_large_said = true;
            log_warning("the level's state came to %u bytes against the %u one message carries, so "
                        "it was not sent; a client keeps the state it has", (unsigned)bytes,
                        (unsigned)MP_CHANNEL_MESSAGE_BYTES);
        }
        return;
    }
    changed = !mp_level_state_same(note, bytes, h->last_sent, h->last_sent_bytes);
    due     = mp_level_state_due(changed, h->sent_before, tick - h->last_sent_tick,
                                 h->change_before, tick - h->last_change_tick);
    if (due == MP_LEVEL_SEND_NONE) {
        return;
    }
    if (due == MP_LEVEL_SEND_HELD) {
        ++state.n.held_back;
        return;
    }
    /* Sent or counted, never kept: a note the channel had no room for is built again from the
     * level as it stands, with the journal's window, and the difference to the last one sent makes
     * it go out again. */
    if (!send(note, bytes)) {
        ++state.n.unsent;
        return;
    }
    ++state.n.sent;
    if (due == MP_LEVEL_SEND_CHANGE) {
        ++state.n.on_change;
        h->change_before    = true;
        h->last_change_tick = tick;
    } else {
        ++state.n.as_repeat;
    }
    h->sent_before    = true;
    h->last_sent_tick = tick;
    memcpy(h->last_sent, note, bytes);
    h->last_sent_bytes = bytes;
}

/* ==============================================================================================
 * A client: keep the newest, apply it from the substep.
 * ============================================================================================ */

bool mp_level_state_take(bool as_client, const uint8_t *note, size_t bytes)
{
    mp_level_state_note_t arrived;

    if (!mp_level_state_is_note(note, bytes)) {
        return false;
    }
    if (!mp_level_state_decode(note, bytes, &arrived)) {
        ++state.n.torn;
        return true;
    }
    if (!as_client) {
        ++state.n.at_host;   /* the level has one writer, and it is the side this arrived at */
        return true;
    }
    ++state.n.taken;
    /* The journal's window is what makes a replaced note cost nothing; counted, because a note
     * with more than sixteen changes behind it would cost a jump. */
    state.n.taken_while_held += state.client.holding ? 1u : 0u;
    state.client.held             = arrived;
    state.client.holding          = true;
    state.client.no_level_counted = false;
    return true;
}

static uint32_t apply_held(void)
{
    client_side_t              *c = &state.client;
    const mp_level_state_note_t *note = &c->held;
    mp_level_journal_plan_t     plan;
    mp_level_state_apply_t      apply;
    bool                        first = false;

    if (!c->holding) {
        return 0u;
    }
    memset(&apply, 0, sizeof apply);
    apply.n     = &state.n;
    apply.world = mp_level_state_bind_world();
    if (apply.world == 0u || !state.level_known ||
        !mp_level_state_bind_counts(apply.world, &apply.emitters, &apply.lights)) {
        /* Kept for the next substep, counted once. */
        if (!c->no_level_counted) {
            c->no_level_counted = true;
            ++state.n.no_level;
        }
        return 0u;
    }
    if (!mp_level_state_can_match()) {
        ++state.n.cannot_apply;
        c->holding = false;
        return 0u;
    }
    c->holding = false;
    switch (mp_level_state_order(&c->taken, state.level, note->level, note->generation,
                                 note->tick, &first)) {
    case MP_LEVEL_ORDER_OTHER_LEVEL:
        ++state.n.other_level;
        return 0u;
    case MP_LEVEL_ORDER_OLD_GENERATION:
        ++state.n.old_generation;
        return 0u;
    case MP_LEVEL_ORDER_OLD_TICK:
        ++state.n.old_tick;
        return 0u;
    case MP_LEVEL_ORDER_TAKE:
    default:
        break;
    }
    c->taken.any        = true;
    c->taken.level      = note->level;
    c->taken.generation = note->generation;
    c->taken.tick       = note->tick;
    if (note->generation > c->taken.newest_generation) {
        c->taken.newest_generation = note->generation;
    }
    if (first) {
        c->journal_known = false;
        c->journal_last  = 0u;
    }
    ++state.n.applied;
    plan = mp_level_journal_plan(note, c->journal_known, c->journal_last);
    mp_level_state_apply_journal(&apply, note, &plan);
    mp_level_state_apply_state(&apply, note);
    /* The fog is said once, in the journal; only a first note or a jump takes it from the state. */
    if (plan.first_snapshot || plan.jump) {
        mp_level_state_fog_heal(note);
    }
    mp_fog_viewers_client_take(note, first);
    mp_actor_loop_take(note);
    c->journal_known = true;
    c->journal_last  = plan.newest;
    if (first) {
        log_info("the level's state arrived: level %u, generation %u; %u emitter placement(s), %u "
                 "light(s), the shared fog's flags %02X, %u room fog run(s), the journal up to %u; "
                 "%u change(s) made here to match, %u that could not be made",
                 (unsigned)note->level, (unsigned)note->generation, (unsigned)note->emitters,
                 (unsigned)note->lights,
                 (unsigned)((note->parts & MP_LEVEL_STATE_PART_FOG) != 0u ? note->fog.flags : 0u),
                 (unsigned)note->fog_viewers, (unsigned)plan.newest, (unsigned)apply.made,
                 (unsigned)apply.failed);
    }
    return apply.made;
}

uint32_t mp_level_state_flush(void)
{
    uint32_t made = apply_held();

    /* Every substep, note or none: this player walks in and out of a room between notes. */
    mp_fog_viewers_client_tick();
    mp_level_state_drawn_tick();
    return made;
}

void mp_level_state_reset(void)
{
    memset(&state.host, 0, sizeof state.host);
    memset(&state.client, 0, sizeof state.client);
    state.level_known = false;
    state.level       = 0u;
    mp_level_state_fog_reset();
    mp_fog_viewers_reset();
}

bool mp_level_state_holds_a_note(void)
{
    return state.client.holding;
}

/* ==============================================================================================
 * The report, written in mp_level_state_report.c and the fog's own files from what each keeps.
 * ============================================================================================ */

void mp_level_state_report(bool host)
{
    mp_level_state_report_switches(host, state.arms_hulled);
    mp_level_state_fog_report(host);
    mp_level_state_report_counts(host, &state.n, &state.last);
    mp_fog_viewers_report(host);
    mp_level_state_drawn_report(host);
    mp_level_state_report_placements(host, state.host_changes, mp_level_state_apply_changes(),
                                     mp_level_state_apply_not_owned());
}
