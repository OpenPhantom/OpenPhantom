/* mp_level_state_apply.c: a client brings its level to the host's. See the header. */
#include "mp_level_state_apply.h"

#include "mp_level_state_bind.h"
#include "mp_level_state_fog.h"

#include "common/logging.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The lines a client writes one by one: the first eight changes and the first eight it could not
 * make. */
#define LINES_WRITTEN 8u

/* The director's crawling text, and the seconds it passes, which the crawl never reads: it shows
 * every line for four. */
#define COMMAND_CRAWL      18
#define CRAWL_SECONDS_BITS 0x40800000u

typedef struct apply_state {
    uint32_t changes[MP_LEVEL_STATE_MAX_EMITTERS];
    uint32_t not_owned[MP_LEVEL_STATE_MAX_EMITTERS];
    uint32_t changes_written;
    uint32_t failures_written;
} apply_state_t;

static apply_state_t applied;

const uint32_t *mp_level_state_apply_changes(void)
{
    return applied.changes;
}

const uint32_t *mp_level_state_apply_not_owned(void)
{
    return applied.not_owned;
}

static void write_change(const char *what, uint32_t index, bool on)
{
    if (applied.changes_written >= LINES_WRITTEN) {
        return;
    }
    ++applied.changes_written;
    log_info("the level's state changed here: %s %u is now %s, as the host has it", what,
             (unsigned)index, on ? "on" : "off");
}

static void write_failure(const char *what, uint32_t index, bool on, const char *because)
{
    if (applied.failures_written >= LINES_WRITTEN) {
        return;
    }
    ++applied.failures_written;
    log_info("the level's state could NOT change here: %s %u stays %s against the host's %s, "
             "because %s", what, (unsigned)index, on ? "off" : "on", on ? "on" : "off", because);
}

/* One placement brought to what the host has, from the note's state or from a journal entry.
 * True when this side switched it. */
static bool match_emitter(mp_level_state_apply_t *a, uint32_t i, mp_level_emitter_t host)
{
    mp_level_emitter_t here = mp_level_state_bind_emitter(a->world, i);
    mp_level_act_t     act  = mp_level_state_emitter_act(host, here);
    bool               on   = act == MP_LEVEL_ACT_ON;

    if (act == MP_LEVEL_ACT_NONE) {
        return false;
    }
    if (act == MP_LEVEL_ACT_NOT_OWNED) {
        ++a->n->not_owned;
        ++applied.not_owned[i];
        return false;
    }
    (void)mp_level_state_bind_set_emitter(i, on);
    here = mp_level_state_bind_emitter(a->world, i);
    if ((here == MP_LEVEL_EMITTER_ON) == on) {
        if (on) {
            ++a->n->emitters_on;
        } else {
            ++a->n->emitters_off;
        }
        ++applied.changes[i];
        ++a->made;
        write_change("emitter placement", i, on);
        return true;
    }
    ++a->failed;
    if (on && here == MP_LEVEL_EMITTER_NEVER) {
        ++a->n->spawned_nothing;
        write_failure("emitter placement", i, on, "the engine spawned nothing");
    } else {
        write_failure("emitter placement", i, on, "the engine kept its own value");
    }
    return false;
}

static bool match_light(mp_level_state_apply_t *a, uint32_t i, bool want)
{
    bool have = false;

    if (!mp_level_state_bind_light(a->world, i, &have) || have == want) {
        return false;
    }
    (void)mp_level_state_bind_set_light(a->world, i, want);
    if (mp_level_state_bind_light(a->world, i, &have) && have == want) {
        if (want) {
            ++a->n->lights_on;
        } else {
            ++a->n->lights_off;
        }
        ++a->made;
        write_change("light", i, want);
        return true;
    }
    /* The engine's own switch off gives the light's slot back only while the slot still carries
     * this light's tag, and leaves the light marked active otherwise. */
    ++a->n->kept_own;
    ++a->failed;
    write_failure("light", i, want, "the engine kept its own value");
    return false;
}

/* The bar brought to the host's: up at its health, or down. Nothing when both are down, whatever
 * health each engine left from an earlier level. */
static bool match_escort(mp_level_state_apply_t *a, bool want_shown, uint8_t want_health)
{
    uint8_t health = 0;
    bool    shown = false;

    if (!mp_level_state_bind_escort() || !mp_level_state_bind_escort_read(&health, &shown)) {
        ++a->n->escort_unread;
        return false;
    }
    if (shown == want_shown && (!shown || health == want_health)) {
        return false;
    }
    (void)mp_level_state_bind_escort_set(want_shown ? want_health : 0u);
    if (mp_level_state_bind_escort_read(&health, &shown) && shown == want_shown &&
        (!shown || health == want_health)) {
        ++a->n->escort_set;
        return true;
    }
    ++a->n->escort_kept;
    return false;
}

/* One journal entry, in the host's order. */
static void play_entry(mp_level_state_apply_t *a, const mp_level_journal_entry_t *entry)
{
    bool played = false;

    switch (entry->kind) {
    case MP_LEVEL_JOURNAL_EMITTER:
    case MP_LEVEL_JOURNAL_LIGHT:
        if (entry->b >= (entry->kind == (uint8_t)MP_LEVEL_JOURNAL_EMITTER ? a->emitters
                                                                          : a->lights)) {
            ++a->n->not_taken;
            return;
        }
        played = entry->kind == (uint8_t)MP_LEVEL_JOURNAL_EMITTER
                     ? match_emitter(a, entry->b,
                                     entry->a != 0u ? MP_LEVEL_EMITTER_ON : MP_LEVEL_EMITTER_OFF)
                     : match_light(a, entry->b, entry->a != 0u);
        break;
    case MP_LEVEL_JOURNAL_FOG:
        if (!mp_level_state_fog_replay(entry)) {
            ++a->n->not_taken;
            return;
        }
        played = true;
        break;
    case MP_LEVEL_JOURNAL_CRAWL:
        if (!mp_level_state_bind_call_director(mp_level_state_bind_stand_in(), COMMAND_CRAWL,
                                               (int32_t)entry->b, (int32_t)CRAWL_SECONDS_BITS)) {
            ++a->n->not_taken;
            return;
        }
        played = true;
        break;
    case MP_LEVEL_JOURNAL_ESCORT:
        played = match_escort(a, entry->a != 0u, entry->a);
        break;
    default:
        return;
    }
    if (played) {
        ++a->n->replayed[entry->kind];
    } else {
        ++a->n->already;
    }
}

void mp_level_state_apply_journal(mp_level_state_apply_t *apply, const mp_level_state_note_t *note,
                                  const mp_level_journal_plan_t *plan)
{
    size_t k;

    if (apply == NULL || note == NULL || plan == NULL) {
        return;
    }
    if (plan->first_snapshot) {
        ++apply->n->firsts;
        for (k = 0; k < plan->count; ++k) {
            apply->n->crawls_first +=
                note->journal[k].kind == (uint8_t)MP_LEVEL_JOURNAL_CRAWL ? 1u : 0u;
        }
        return;
    }
    if (plan->jump) {
        ++apply->n->jumps;
        apply->n->lost += plan->lost;
    }
    for (k = 0; k < plan->count; ++k) {
        const mp_level_journal_entry_t *entry = &note->journal[plan->first + k];

        if (plan->jump && !mp_level_journal_is_moment(entry->kind)) {
            continue;
        }
        play_entry(apply, entry);
    }
}

void mp_level_state_apply_state(mp_level_state_apply_t *apply, const mp_level_state_note_t *note)
{
    uint32_t i;

    if (apply == NULL || note == NULL) {
        return;
    }
    if ((note->parts & MP_LEVEL_STATE_PART_EMITTERS) != 0u) {
        if (note->emitters != apply->emitters) {
            ++apply->n->counts_differ;
        } else {
            for (i = 0; i < note->emitters; ++i) {
                (void)match_emitter(apply, i, (mp_level_emitter_t)note->emitter[i]);
            }
        }
    }
    if ((note->parts & MP_LEVEL_STATE_PART_LIGHTS) != 0u) {
        if (note->lights != apply->lights) {
            ++apply->n->counts_differ;
        } else {
            for (i = 0; i < note->lights; ++i) {
                (void)match_light(apply, i, mp_level_state_light_on(note, i));
            }
        }
    }
    if ((note->parts & MP_LEVEL_STATE_PART_ESCORT) != 0u) {
        (void)match_escort(apply, (note->escort_flags & MP_LEVEL_STATE_ESCORT_SHOWN) != 0u,
                           note->escort_health);
    }
}
