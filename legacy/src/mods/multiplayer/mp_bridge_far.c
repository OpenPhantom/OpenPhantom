/* mp_bridge_far.c: what this machine holds for each far player. See the header. */
#include "mp_bridge_far.h"

#include "mp_bank.h"
#include "mp_events.h"
#include "mp_interp.h"
#include "mp_puppet_sabre.h"
#include "mp_timeline.h"
#include "mp_wire.h"

#include "common/logging.h"
#include "common/text.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct far_bank {
    uint64_t             connection_id;   /* whose history this is, 0 for nobody's */
    uint32_t             states;          /* states taken into it over the run */
    uint32_t             moments;         /* moments queued for its puppet over the run */
    uint32_t             passed_on;       /* on a host, moments of the others passed to this
                                           * bank's player since its connection began */
    uint32_t             passed_last;     /* the host's substep of the newest of them */
    uint32_t             starts;          /* new players it started over for */
    bool                 seated;          /* the bank shows a world slot */
    bool                 shown;           /* it has shown one at some time, `slot` the last */
    uint8_t              slot;
    bool                 placed;          /* a pose has been resolved for this player once */
    bool                 pose_known;
    mp_bridge_far_pose_t pose;
    bool                 hero_known;      /* an appearance event has named this player's hero */
    uint8_t              hero;
    bool                 look_known;      /* an appearance has been worn here */
    uint8_t              look_hero;       /* the hero the actor below was chosen for */
    char                 actor[MP_EVENT_ASSET_MAX];   /* built from, "" = the hero's own */
    char                 model[MP_EVENT_ASSET_MAX];   /* worn over it, "" = none */
    mp_interp_t          interp;
} far_bank_t;

/* Index 0 is this machine's own player and never used. Not called `far`: windows.h defines that
 * as an empty macro, and a name that vanishes at the preprocessor reads as a syntax error. */
static far_bank_t far_banks[MP_BANK_FAR_MAX + 1u];
static bool       far_auto_lag;
static uint32_t   far_departures;   /* histories described as their player left */
static bool       far_relaying;     /* a listen host, whose bank N is the player of peer N - 1 */

/* The world this machine entered, and what the poses of another one were refused for. */
typedef struct far_world {
    bool     entered;
    uint8_t  world;                            /* the wire's four bits of the generation */
    uint32_t unplaced;                         /* resolved poses kept off a puppet */
    uint32_t refused[MP_FAR_READERS];
    uint32_t on_its_way[MP_FAR_READERS];       /* banks answered as standing in another world */
} far_world_t;

static far_world_t far_world;

/* "bank 2 (slot 3)", or "(no slot)" for a bank that never showed one. Behind the colon of every
 * line that names a bank, so that the name the line is found by, when two runs' reports are
 * compared, stays the same whoever sits where. */
static void bank_name(size_t bank, char *out, size_t bytes)
{
    const far_bank_t *entry = &far_banks[bank];

    if (entry->shown) {
        text_format(out, bytes, "bank %u (slot %u)", (unsigned)bank, (unsigned)entry->slot);
    } else {
        text_format(out, bytes, "bank %u (no slot)", (unsigned)bank);
    }
}

/* On a listen host, how many of the other players' moments were passed on to this bank's player
 * and on which of the host's substeps the last one went. Said as the player leaves as well, so a
 * count that went on past a drop shows its last substep beside the moment the connection went. */
static void describe_passed(size_t bank, const char *when)
{
    uint32_t last   = 0u;
    uint32_t passed = mp_bridge_far_passed_on(bank, &last);

    if (far_relaying) {
        log_info("  moments passed on to peer %u (slot %u) %s: %u, the last at substep %u",
                 (unsigned)(bank - 1u), (unsigned)bank, when, (unsigned)passed, (unsigned)last);
    }
}

/* One bank's buffer, timeline and blade, `when` saying at which moment. The same words as the
 * report's lines for the first bank, with the bank and its slot right after the colon, so that a
 * bank whose player leaves is described before its history goes rather than reported as the
 * nought it holds afterwards. */
static void describe_bank(size_t bank, const char *when)
{
    const far_bank_t    *entry    = &far_banks[bank];
    const mp_timeline_t *timeline = mp_interp_timeline(&entry->interp);
    char                 name[40];

    bank_name(bank, name, sizeof name);
    log_info("  the buffer: %s %s, target lag %u tick(s), %s, deepest dip in the window %u, %u "
             "change(s) to the target", name, when, (unsigned)mp_timeline_target_lag(timeline),
             mp_timeline_auto_lag(timeline) ? "measured" : "held",
             (unsigned)mp_timeline_dip(timeline), (unsigned)mp_timeline_target_changes(timeline));
    log_info("  the timeline: %s %s, %u inserted, %u skipped, %u resync(s), %u halt(s), render "
             "tick %u against the newest %u; %u underrun(s)", name, when,
             (unsigned)mp_timeline_inserted(timeline), (unsigned)mp_timeline_skipped(timeline),
             (unsigned)mp_timeline_resyncs(timeline), (unsigned)mp_timeline_halts(timeline),
             (unsigned)mp_timeline_render_tick(timeline),
             (unsigned)mp_timeline_newest_tick(timeline),
             (unsigned)mp_interp_underruns(&entry->interp));
    log_info("  the far blade: %s %s, %s", name, when,
             mp_puppet_sabre_armed(bank) ? "armed" : "at rest");
    describe_passed(bank, when);
}

/* Whether a bank's history holds anything a description would be worth. */
static bool history_held(const far_bank_t *entry)
{
    return mp_interp_newest(&entry->interp, NULL, NULL);
}

static void start_history(far_bank_t *entry)
{
    mp_interp_init(&entry->interp, MP_INTERP_DEFAULT_LAG);
    mp_interp_set_auto_lag(&entry->interp, far_auto_lag);
    entry->placed     = false;
    entry->pose_known = false;
    entry->hero_known = false;
    entry->look_known = false;
    entry->actor[0]   = '\0';
    entry->model[0]   = '\0';
}

void mp_bridge_far_reset(void)
{
    size_t i;

    for (i = 1u; i <= MP_BANK_FAR_MAX; ++i) {
        if (history_held(&far_banks[i])) {
            describe_bank(i, "as the session was reset");
        }
        far_banks[i].connection_id = 0u;
        start_history(&far_banks[i]);
    }
}

void mp_bridge_far_set_auto_lag(bool enabled)
{
    size_t i;

    far_auto_lag = enabled;
    for (i = 1u; i <= MP_BANK_FAR_MAX; ++i) {
        mp_interp_set_auto_lag(&far_banks[i].interp, enabled);
    }
}

uint64_t mp_bridge_far_connection(size_t bank)
{
    return mp_bank_index_ok(bank) ? far_banks[bank].connection_id : 0u;
}

void mp_bridge_far_start_over(size_t bank, uint64_t connection_id)
{
    if (!mp_bank_index_ok(bank)) {
        return;
    }
    /* Every way a bank loses its player comes through here, a departure, a replacement in place
     * and a client giving a missing player up, so this is where the history is described before
     * it goes. */
    if (history_held(&far_banks[bank])) {
        describe_bank(bank, "as its player left");
        ++far_departures;
    } else if (far_banks[bank].passed_on != 0u) {
        describe_passed(bank, "as its player left");   /* one who never sent a state here */
    }
    far_banks[bank].connection_id = connection_id;
    far_banks[bank].passed_on     = 0u;
    far_banks[bank].passed_last   = 0u;
    if (connection_id != 0u) {
        ++far_banks[bank].starts;
    }
    start_history(&far_banks[bank]);
}

void mp_bridge_far_set_relaying(bool relaying)
{
    far_relaying = relaying;
}

void mp_bridge_far_note_passed_on(size_t bank, uint32_t tick)
{
    if (mp_bank_index_ok(bank)) {
        ++far_banks[bank].passed_on;
        far_banks[bank].passed_last = tick;
    }
}

uint32_t mp_bridge_far_passed_on(size_t bank, uint32_t *last_tick)
{
    if (!mp_bank_index_ok(bank)) {
        return 0u;
    }
    if (last_tick != NULL) {
        *last_tick = far_banks[bank].passed_last;
    }
    return far_banks[bank].passed_on;
}

mp_interp_t *mp_bridge_far_interp(size_t bank)
{
    return mp_bank_index_ok(bank) ? &far_banks[bank].interp : NULL;
}

void mp_bridge_far_note_state(size_t bank)
{
    if (mp_bank_index_ok(bank)) {
        ++far_banks[bank].states;
    }
}

void mp_bridge_far_note_moment(size_t bank)
{
    if (mp_bank_index_ok(bank)) {
        ++far_banks[bank].moments;
    }
}

uint32_t mp_bridge_far_moments(size_t bank)
{
    return mp_bank_index_ok(bank) ? far_banks[bank].moments : 0u;
}

void mp_bridge_far_unseat_all(void)
{
    size_t i;

    for (i = 1u; i <= MP_BANK_FAR_MAX; ++i) {
        far_banks[i].seated = false;
    }
}

void mp_bridge_far_seat(size_t bank, uint8_t slot)
{
    size_t i;

    if (!mp_bank_index_ok(bank)) {
        return;
    }
    for (i = 1u; i <= MP_BANK_FAR_MAX; ++i) {
        if (i != bank && far_banks[i].seated && far_banks[i].slot == slot) {
            far_banks[i].seated = false;   /* one bank per slot, or an appearance finds two */
        }
    }
    far_banks[bank].seated = true;
    far_banks[bank].shown  = true;
    far_banks[bank].slot   = slot;
}

bool mp_bridge_far_slot_of(size_t bank, uint8_t *slot)
{
    if (!mp_bank_index_ok(bank) || !far_banks[bank].seated) {
        return false;
    }
    if (slot != NULL) {
        *slot = far_banks[bank].slot;
    }
    return true;
}

size_t mp_bridge_far_bank_of_slot(uint8_t slot)
{
    size_t i;

    for (i = 1u; i <= MP_BANK_FAR_MAX; ++i) {
        if (far_banks[i].seated && far_banks[i].slot == slot) {
            return i;
        }
    }
    return 0u;
}

size_t mp_bridge_far_seat_slot(uint8_t slot, uint8_t my_slot)
{
    size_t bank = mp_bridge_far_bank_of_slot(slot);
    size_t preferred;
    size_t i;

    if (bank != 0u || slot == my_slot) {
        return bank;
    }
    preferred = slot < my_slot ? (size_t)slot + 1u : (size_t)slot;
    if (mp_bank_index_ok(preferred) && !far_banks[preferred].seated) {
        bank = preferred;
    }
    for (i = 1u; bank == 0u && i <= MP_BANK_FAR_MAX; ++i) {
        if (!far_banks[i].seated) {
            bank = i;
        }
    }
    if (bank != 0u) {
        mp_bridge_far_start_over(bank, 0u);
        mp_bridge_far_seat(bank, slot);
        /* A client has no connection per far player; a new seat is where its history starts. */
        ++far_banks[bank].starts;
    }
    return bank;
}

void mp_bridge_far_unseat(size_t bank)
{
    if (mp_bank_index_ok(bank)) {
        mp_bridge_far_start_over(bank, 0u);
        far_banks[bank].seated = false;
    }
}

void mp_bridge_far_enter_world(uint8_t generation)
{
    far_world.entered = true;
    far_world.world   = mp_wire_world_of(generation);
}

uint8_t mp_bridge_far_world(void)
{
    return far_world.world;
}

/* The one test every reader of a far pose goes through. A machine that has entered no world of a
 * session yet judges nothing: no pose is resolved outside a level of one, and a way into a session
 * this module was not told of must not cost it every far body. */
bool mp_bridge_far_is_this_world(uint8_t world)
{
    return !far_world.entered || world == far_world.world;
}

static bool of_this_world(const mp_bridge_far_pose_t *pose)
{
    return mp_bridge_far_is_this_world(pose->world);
}

bool mp_bridge_far_note_pose(size_t bank, const mp_bridge_far_pose_t *pose)
{
    if (!mp_bank_index_ok(bank) || pose == NULL) {
        return false;
    }
    far_banks[bank].pose       = *pose;
    far_banks[bank].pose_known = true;
    if (!of_this_world(pose)) {
        ++far_world.unplaced;
        return false;
    }
    far_banks[bank].placed = true;
    return true;
}

bool mp_bridge_far_pose(size_t bank, mp_bridge_far_reader_t reader, mp_bridge_far_pose_t *out)
{
    if (out == NULL || !mp_bank_index_ok(bank) || !far_banks[bank].pose_known) {
        return false;
    }
    if (!of_this_world(&far_banks[bank].pose)) {
        if (reader < MP_FAR_READERS) {
            ++far_world.refused[reader];
        }
        return false;
    }
    *out = far_banks[bank].pose;
    return true;
}

bool mp_bridge_far_in_another_world(size_t bank, mp_bridge_far_reader_t reader, uint8_t *slot)
{
    if (!mp_bank_index_ok(bank) || !far_banks[bank].seated || !far_banks[bank].pose_known ||
        of_this_world(&far_banks[bank].pose)) {
        return false;
    }
    if (reader < MP_FAR_READERS) {
        ++far_world.on_its_way[reader];
    }
    if (slot != NULL) {
        *slot = far_banks[bank].slot;
    }
    return true;
}

uint32_t mp_bridge_far_starts(size_t bank)
{
    return mp_bank_index_ok(bank) ? far_banks[bank].starts : 0u;
}

bool mp_bridge_far_pose_stands(const mp_bridge_far_pose_t *pose)
{
    return pose != NULL && pose->alive && !pose->dead;
}

bool mp_bridge_far_placed(size_t bank)
{
    return mp_bank_index_ok(bank) && far_banks[bank].placed;
}

bool mp_bridge_far_occupied(size_t bank)
{
    return mp_bank_index_ok(bank) && far_banks[bank].seated && far_banks[bank].pose_known &&
           of_this_world(&far_banks[bank].pose);
}

void mp_bridge_far_note_hero(size_t bank, uint8_t hero)
{
    if (mp_bank_index_ok(bank)) {
        far_banks[bank].hero       = hero;
        far_banks[bank].hero_known = true;
    }
}

bool mp_bridge_far_hero(size_t bank, uint8_t *hero)
{
    if (!mp_bank_index_ok(bank) || !far_banks[bank].hero_known) {
        return false;
    }
    if (hero != NULL) {
        *hero = far_banks[bank].hero;
    }
    return true;
}

/* A name that does not fit is kept as none, because a name cut short is another name. `name` may
 * be the field itself, which is what a kept actor is. */
static void keep_name(char *field, size_t bytes, const char *name)
{
    size_t length = name != NULL ? strlen(name) : 0u;

    if (length >= bytes) {
        length = 0u;
    }
    if (length != 0u) {
        memmove(field, name, length);
    }
    field[length] = '\0';
}

bool mp_bridge_far_note_look(size_t bank, uint8_t kind, const char *name, uint8_t hero)
{
    far_bank_t *entry;
    const char *model = (kind == (uint8_t)MP_SKIN_MODEL && name != NULL) ? name : "";
    bool        changed;

    if (!mp_bank_index_ok(bank)) {
        return false;
    }
    entry = &far_banks[bank];
    keep_name(entry->actor, sizeof entry->actor,
              mp_skin_body_actor(kind, name, hero, entry->look_known ? entry->actor : NULL,
                                 entry->look_hero));
    changed = strcmp(entry->model, model) != 0;
    keep_name(entry->model, sizeof entry->model, model);
    entry->look_hero  = hero;
    entry->look_known = true;
    return changed;
}

const char *mp_bridge_far_actor(size_t bank)
{
    return mp_bank_index_ok(bank) ? far_banks[bank].actor : "";
}

bool mp_bridge_far_model(size_t bank, char *out, size_t bytes)
{
    size_t length;

    if (!mp_bank_index_ok(bank) || far_banks[bank].model[0] == '\0') {
        return false;
    }
    length = strlen(far_banks[bank].model);
    if (out != NULL && bytes > length) {
        memcpy(out, far_banks[bank].model, length + 1u);
    }
    return true;
}

size_t mp_bridge_far_states_text(char *out, size_t bytes)
{
    size_t at = 0;
    size_t i;

    if (out == NULL || bytes == 0u) {
        return 0u;
    }
    out[0] = '\0';
    for (i = 1u; i <= MP_BANK_FAR_MAX; ++i) {
        const far_bank_t *entry = &far_banks[i];
        unsigned          resyncs =
            (unsigned)mp_timeline_resyncs(mp_interp_timeline(&entry->interp));

        if (entry->starts == 0u && entry->states == 0u) {
            continue;
        }
        /* The world slot the bank showed, which on a listen host is its own number and on a client
         * is not: a client shows the host in bank 1. */
        if (entry->shown) {
            at += text_format(out + at, bytes - at,
                              "%sslot %u %u (history started %u time(s), %u resync(s) since)",
                              at == 0u ? "" : ", ", (unsigned)entry->slot,
                              (unsigned)entry->states, (unsigned)entry->starts, resyncs);
        } else {
            at += text_format(out + at, bytes - at,
                              "%sbank %u (no slot) %u (history started %u time(s), %u resync(s) "
                              "since)", at == 0u ? "" : ", ", (unsigned)i,
                              (unsigned)entry->states, (unsigned)entry->starts, resyncs);
        }
        if (at + 1u >= bytes) {
            return at;   /* the buffer is full, and what follows would be cut */
        }
    }
    return at;
}

void mp_bridge_far_describe_banks(void)
{
    size_t i;

    for (i = 1u; i <= MP_BANK_FAR_MAX; ++i) {
        if (mp_bridge_far_occupied(i)) {
            describe_bank(i, "at the report");
        } else if (far_banks[i].connection_id != 0u) {
            describe_passed(i, "at the report");   /* connected, and in no body of this world */
        }
    }
}

uint32_t mp_bridge_far_departures(void)
{
    return far_departures;
}

void mp_bridge_far_report_worlds(void)
{
    uint32_t holds      = 0u;
    uint32_t on_its_way = 0u;
    size_t   i;

    for (i = 1u; i <= MP_BANK_FAR_MAX; ++i) {
        holds += mp_interp_world_holds(&far_banks[i].interp);
    }
    for (i = 0u; i < (size_t)MP_FAR_READERS; ++i) {
        on_its_way += far_world.on_its_way[i];
    }
    log_info("  the far poses of another world: %u resolved and kept off a puppet; refused to the "
             "arrival %u, the re-entry %u, the range gate %u, the scene %u, the others %u; a far "
             "player was answered as on its way here %u time(s); %u blend(s) held at a change of "
             "world", (unsigned)far_world.unplaced,
             (unsigned)far_world.refused[MP_FAR_READER_ARRIVAL],
             (unsigned)far_world.refused[MP_FAR_READER_REENTRY],
             (unsigned)far_world.refused[MP_FAR_READER_RANGE_GATE],
             (unsigned)far_world.refused[MP_FAR_READER_SCENE],
             (unsigned)far_world.refused[MP_FAR_READER_OTHER], (unsigned)on_its_way,
             (unsigned)holds);
}
