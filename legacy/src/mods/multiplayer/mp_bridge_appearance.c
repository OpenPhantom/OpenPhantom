/* mp_bridge_appearance.c: what a far player looks like. See the header. */
#include "mp_bridge_appearance.h"

#include "mp_actions.h"
#include "mp_bank.h"
#include "mp_body.h"
#include "mp_bridge_far.h"
#include "mp_bridge_roster.h"
#include "mp_cells.h"
#include "mp_bridge_world.h"
#include "mp_roster.h"

#include "common/logging.h"
#include "common/memory.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct bridge_appearance {
    uint32_t sent;          /* this side's own changes queued as events */
    uint32_t taken;         /* appearance events taken off the reliable channel */
    uint32_t applied;       /* handed to a far body and accepted */
    uint32_t refused;       /* the body module refused one */
    uint32_t dropped;       /* for a slot no bank shows, or this side's own echo */
    uint32_t from_roster;   /* appearances read out of a repeated roster instead of an edge */
    uint32_t models;        /* changes to a model a far body wears over its hero's body */
    bool     dropped_logged;
} bridge_appearance_t;

static bridge_appearance_t appearance;

/* Hand an appearance to a far body. Both sources end here, and the idempotency is the body
 * module's: it compares what it wears or last tried to wear against what is asked for, and does
 * nothing when they agree, which is what lets the roster repeat the same answer every second
 * without taking a standing body down for it. */
/* How big that body is drawn. Nobody said is its own size, which is what a sender without the two
 * player scales says and what every sender said before the field existed. */
static void wear_scale(size_t bank, uint16_t hundredths)
{
    float scale = (hundredths == (uint16_t)MP_WIRE_SCALE_NONE)
                      ? 1.0f : (float)hundredths / 100.0f;

    if (!mp_body_set_scale_at(bank, scale)) {
        ++appearance.refused;
    }
}

/* The bank decides the look (mp_bridge_far_note_look), and the body is asked for exactly what it
 * decided: a character is its own actor, and a model keeps the body that is there, so the clip
 * ordinals the wire names still index the table they were taken from. The model itself is said
 * once per change; the body is drawn in it only when the developer overlay listens and puts it on
 * (mp_body_wear.c). The roster repeats every second, so nothing here may speak for an answer that
 * did not change. */
static void wear(size_t bank, uint8_t hero, uint8_t kind, const char *name)
{
    char        model[MP_EVENT_ASSET_MAX];
    const char *actor;

    if (mp_bridge_far_note_look(bank, kind, name, hero) &&
        mp_bridge_far_model(bank, model, sizeof model)) {
        actor = mp_bridge_far_actor(bank);
        ++appearance.models;
        log_info("bank %u wears the model %s over hero %u's body, built as %s; the body shows "
                 "the model only when the developer overlay listens and puts it on",
                 (unsigned)bank, model, (unsigned)hero,
                 actor[0] != '\0' ? actor : "that hero's own asset");
    }
    if (mp_body_set_asset_at(bank, (int32_t)hero, mp_bridge_far_actor(bank))) {
        ++appearance.applied;
    } else {
        ++appearance.refused;
    }
}

/* The hero on the wire is a sample out of the interpolator, so it is a replay delay behind the
 * appearance event that rebuilt the body: for the first samples after a rebuild it names the hero
 * the far player wore a moment ago, and that is not a disagreement, it is the delay.
 *
 * Judging on the first sample reported a mismatch every time somebody swapped character inside a
 * session, which is exactly the case that is working. The field log carries four of those, each
 * naming the hero of the swap before it, while the roster line above them already showed the new
 * one. So a difference waits out the grace, and only then is it said. */
mp_appearance_verdict_t mp_appearance_hero_verdict(int32_t built_for, int32_t far_hero,
                                                   uint32_t samples_since_build,
                                                   uint32_t grace_samples)
{
    if (built_for < 0) {
        return MP_APPEARANCE_WAIT;   /* no body built yet, so there is nothing to hold against */
    }
    if (far_hero == built_for) {
        return MP_APPEARANCE_AGREED;
    }
    return samples_since_build < grace_samples ? MP_APPEARANCE_WAIT : MP_APPEARANCE_DISAGREES;
}

void mp_bridge_appearance_note_own(const char *worn, uint8_t kind)
{
    uintptr_t block = mp_cells_address(MP_CELL_HERO_BLOCK);
    uint32_t  hero  = 0;

    ++appearance.sent;
    mp_bridge_roster_set_own_asset(worn, kind);

    /* And the hero this player is wearing, read where the engine keeps it rather than taken from
     * the lobby: the list is read by everybody in the session, and after the first level the pick
     * and the figure in the world are two different answers. */
    if (block != 0u && memory_read_u32(block + MP_HERO_BLOCK_HERO_INDEX, &hero)) {
        mp_bridge_roster_set_own_hero((uint8_t)hero);
    }
    /* And how big this side is drawn, from the same sample the event was built out of. */
    mp_bridge_roster_set_own_scale(mp_actions_own_scale());
}

void mp_bridge_appearance_take_event(uint8_t my_slot, size_t peer_index, const mp_event_t *event)
{
    size_t bank;

    if (event == NULL) {
        return;
    }
    ++appearance.taken;
    bank = mp_bridge_far_bank_of_slot(event->skin_slot);
    if (event->skin_slot == my_slot || bank == 0u) {
        ++appearance.dropped;
        if (!appearance.dropped_logged) {
            appearance.dropped_logged = true;
            log_info("an appearance arrived for world slot %u, which is this side's own or a "
                     "player no far bank here shows; it is dropped and later ones are counted",
                     (unsigned)event->skin_slot);
        }
        return;
    }

    /* The hero is kept because the repeated roster carries the far player's LOBBY choice, which is
     * not the same number once a character has been swapped in: without this the next roster would
     * rebuild the body against the lobby's hero and undo the change that just arrived. */
    mp_bridge_far_note_hero(bank, event->skin_hero);
    wear(bank, event->skin_hero, event->skin_kind, event->skin_asset);
    wear_scale(bank, event->skin_scale);
    mp_bridge_roster_note_peer_hero(peer_index, event->skin_hero);
    mp_bridge_roster_note_peer_scale(peer_index, event->skin_scale);

    /* And into the table this side publishes. Only an authority reads it back, so on a client the
     * write is kept and never sent, which costs nothing and keeps the two roles one path. */
    mp_bridge_roster_note_peer_asset(peer_index, event->skin_asset, event->skin_kind);
}

void mp_bridge_appearance_take_roster(void)
{
    mp_roster_t table;
    size_t      i;
    size_t      bank;
    uint8_t     hero = 0;

    if (!mp_bridge_roster_current(&table)) {
        return;
    }
    for (i = 0; i < table.count; ++i) {
        const mp_roster_entry_t *entry = &table.entry[i];

        bank = mp_bridge_far_bank_of_slot(entry->slot);
        if (bank == 0u || entry->asset[0] == '\0') {
            continue;   /* a player no bank here shows, or an asset not known yet */
        }
        if (!mp_bridge_far_hero(bank, &hero)) {
            hero = entry->hero;
        }
        /* The roster's codec does not bound the hero byte the way the appearance event's does, and
         * this arrives once a second: an unusable value passed on would be one warning per second
         * from the body module for the rest of the session. */
        if (hero >= MP_EVENT_HERO_SLOTS) {
            ++appearance.refused;
            continue;
        }
        ++appearance.from_roster;
        wear(bank, hero, entry->asset_kind, entry->asset);
        wear_scale(bank, entry->scale);
    }
}

void mp_bridge_appearance_report(void)
{
    size_t bank;

    log_info("  the appearance: %u change(s) of this player's sent (%u name(s) the codec would "
             "not carry), %u taken off the channel and %u read out of a repeated roster; %u "
             "applied to the far body, %u refused by it, %u dropped for a slot with no body here; "
             "%u change(s) to a model worn over the hero's body. This side wears %s",
             (unsigned)appearance.sent, (unsigned)mp_actions_appearances_refused(),
             (unsigned)appearance.taken, (unsigned)appearance.from_roster,
             (unsigned)appearance.applied, (unsigned)appearance.refused,
             (unsigned)appearance.dropped, (unsigned)appearance.models,
             mp_bridge_world_local_asset()[0] != '\0' ? mp_bridge_world_local_asset()
                                                       : "nothing sampled yet");
    for (bank = 1u; bank <= MP_BANK_FAR_MAX; ++bank) {
        char        model[MP_EVENT_ASSET_MAX];
        const char *actor = mp_bridge_far_actor(bank);
        bool        worn  = mp_bridge_far_model(bank, model, sizeof model);

        if (actor[0] != '\0' || worn) {
            log_info("    bank %u was last asked for %s%s%s", (unsigned)bank,
                     actor[0] != '\0' ? actor : "its hero's own asset",
                     worn ? " with the model over it " : "", worn ? model : "");
        }
    }
}
