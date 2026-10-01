/* npc_spawn_session.c: see npc_spawn_session.h. */
#include "npc_spawn_session.h"

#include "npc_spawn_block.h"
#include "npc_spawn_desc.h"

#include "common/npc_spawn_note.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

void npc_spawn_session_init(npc_spawn_session_t *session, uint8_t panel)
{
    if (session == NULL) {
        return;
    }
    memset(session, 0, sizeof *session);
    session->record.first = 1u;   /* serials start at 1: 0 names no wish */
    session->record.panel = panel;
    session->changed      = true;
}

void npc_spawn_session_set_panel(npc_spawn_session_t *session, uint8_t panel)
{
    if (session != NULL && session->record.panel != panel) {
        session->record.panel = panel;
        session->changed      = true;
    }
}

void npc_spawn_session_set_world(npc_spawn_session_t *session, uint32_t world)
{
    uint32_t k;

    if (session == NULL || session->world == world) {
        return;
    }
    session->world = world;
    for (k = 0; k < NPC_SPAWN_COPIES_MAX; ++k) {
        if (session->leftover[k]) {
            session->leftover[k] = false;
            ++session->counters.old_gone;
        }
    }
}

/* ---- The kinds of the last wishes, for the line that says one was refused. ---- */

static void remember_kind(npc_spawn_session_t *session, uint32_t serial, uint8_t kind)
{
    uint32_t at = serial % NPC_SPAWN_SESSION_KINDS;

    session->kind_serial[at] = serial;
    session->kind[at]        = kind;
}

static uint8_t kind_of(const npc_spawn_session_t *session, uint32_t serial)
{
    uint32_t at = serial % NPC_SPAWN_SESSION_KINDS;

    return serial != 0u && session->kind_serial[at] == serial ? session->kind[at] : 0u;
}

bool npc_spawn_session_to_note(const npc_spawn_desc_t *desc, npc_spawn_note_desc_t *out)
{
    size_t length;

    if (desc == NULL || out == NULL) {
        return false;
    }
    length = strlen(desc->file);
    if (length == 0u || length > NPC_SPAWN_FILE_MAX) {
        return false;
    }
    memset(out, 0, sizeof *out);
    out->source      = desc->source;
    out->behaviour   = desc->behaviour;
    out->flags       = desc->archive ? (uint8_t)NPC_SPAWN_DESC_ARCHIVE : 0u;
    out->position[0] = desc->position[0];
    out->position[1] = desc->position[1];
    out->position[2] = desc->position[2];
    out->facing      = desc->facing;
    memcpy(out->file, desc->file, length);
    return npc_spawn_note_desc_is_sound(out);
}

bool npc_spawn_session_from_note(const npc_spawn_note_desc_t *desc, npc_spawn_desc_t *out)
{
    if (desc == NULL || out == NULL || !npc_spawn_note_desc_is_sound(desc)) {
        return false;
    }
    memset(out, 0, sizeof *out);
    out->source      = desc->source;
    out->behaviour   = desc->behaviour;
    out->archive     = (desc->flags & NPC_SPAWN_DESC_ARCHIVE) != 0u;
    out->position[0] = desc->position[0];
    out->position[1] = desc->position[1];
    out->position[2] = desc->position[2];
    out->facing      = desc->facing;
    memcpy(out->file, desc->file, NPC_SPAWN_FILE_MAX);   /* the thirteenth byte stays NUL */
    return true;
}

/* ---- The restores: how a saved copy stood, kept under the serial of the wish that asks. ---- */

static void keep_restore(npc_spawn_session_t *session, uint32_t serial,
                         const npc_spawn_saved_t *saved)
{
    if (session->restores >= NPC_SPAWN_SESSION_QUEUE) {
        ++session->counters.restores_lost;   /* built at the spawn point, at full health */
        return;
    }
    session->restore[session->restores]        = *saved;
    session->restore_serial[session->restores] = serial;
    ++session->restores;
}

/* The restore a wish carried, taken out: an answer ends it either way. */
static bool take_restore(npc_spawn_session_t *session, uint32_t serial, npc_spawn_saved_t *out)
{
    uint32_t i;

    for (i = 0; serial != 0u && i < session->restores; ++i) {
        if (session->restore_serial[i] == serial) {
            *out = session->restore[i];
            --session->restores;
            memmove(&session->restore[i], &session->restore[i + 1u],
                    (session->restores - i) * sizeof session->restore[0]);
            memmove(&session->restore_serial[i], &session->restore_serial[i + 1u],
                    (session->restores - i) * sizeof session->restore_serial[0]);
            return true;
        }
    }
    return false;
}

/* ---- The wishes: a queue behind the six places, in order, each under its serial. ---- */

static void fill_record(npc_spawn_session_t *session)
{
    while (session->queued > 0u && session->record.count < NPC_SPAWN_WISH_SLOTS) {
        npc_spawn_session_queued_t *next   = &session->queue[0];
        uint32_t                    serial = session->record.first + session->record.count;

        if (serial == 0u) {
            return;   /* the counter's end: nothing more is written, thirty years from now */
        }
        if (npc_spawn_note_wishes_append(&session->record, serial, &next->wish)) {
            if (next->restore) {
                keep_restore(session, serial, &next->saved);
            }
            remember_kind(session, serial, next->wish.kind);
            session->changed = true;
        }
        --session->queued;
        memmove(&session->queue[0], &session->queue[1],
                session->queued * sizeof session->queue[0]);
    }
}

bool npc_spawn_session_wish(npc_spawn_session_t *session, uint8_t kind,
                            const npc_spawn_desc_t *desc, const npc_spawn_saved_t *saved)
{
    npc_spawn_session_queued_t *slot;
    bool builds = kind == NPC_SPAWN_WISH_SPAWN || kind == NPC_SPAWN_WISH_RESTORE;

    if (session == NULL) {
        return false;
    }
    if (session->queued >= NPC_SPAWN_SESSION_QUEUE) {
        ++session->counters.queue_full;
        return false;
    }
    slot = &session->queue[session->queued];
    memset(slot, 0, sizeof *slot);
    slot->wish.kind  = kind;
    slot->wish.epoch = session->epoch;   /* stamped once, as it is described */
    if ((builds && !npc_spawn_session_to_note(desc, &slot->wish.desc)) ||
        !npc_spawn_note_wish_is_sound(&slot->wish)) {
        ++session->counters.unsound;
        return false;
    }
    if (kind == NPC_SPAWN_WISH_RESTORE && saved != NULL) {
        slot->saved   = *saved;
        slot->restore = true;
    }
    ++session->queued;
    ++session->counters.wishes;
    fill_record(session);
    return true;
}

/* A new epoch ends every wish of another one, sent or queued: the multiplayer throws them away when
 * it takes them, and this panel answers them itself. They were described in order, so the sent
 * ones of an old epoch are the front of the record. */
static void end_old_wishes(npc_spawn_session_t *session)
{
    uint32_t kept = 0;
    uint32_t i;

    while (session->record.count > 0u && session->record.entry[0].epoch != session->epoch) {
        npc_spawn_saved_t unused;

        (void)take_restore(session, session->record.first, &unused);
        (void)npc_spawn_note_wishes_drop(&session->record, session->record.first);
        ++session->counters.ended;
        session->changed = true;
    }
    for (i = 0; i < session->queued; ++i) {
        if (session->queue[i].wish.epoch == session->epoch) {
            session->queue[kept++] = session->queue[i];
        } else {
            ++session->counters.ended;
        }
    }
    session->queued = kept;
}

/* A client's copies belong to its host's world: those of the old epoch are left over, to go as
 * soon as they can. The host's stay in its world, and this panel holds no life of either any more:
 * whatever the new epoch grants under their keys is a new life. */
static void leave_the_old_world(npc_spawn_session_t *session)
{
    uint32_t k;

    for (k = 0; k < NPC_SPAWN_COPIES_MAX; ++k) {
        if (session->built[k] != 0u && session->built_as_client[k]) {
            session->leftover[k] = true;
            ++session->counters.old_found;
        }
        session->built[k]           = 0u;
        session->built_as_client[k] = false;
    }
}

/* The leftovers, removed while nothing holds the pool, each by a fresh walk. One the player rides,
 * or one a walk could not reach, is asked again next frame; a level's close takes the rest
 * (npc_spawn_session_set_world). Before the grants are answered, so a grant of the same key in the
 * new epoch finds it free. */
static void remove_leftovers(npc_spawn_session_t *session, bool now,
                             const npc_spawn_session_ops_t *ops)
{
    uint32_t k;

    if (!now || ops == NULL) {
        return;
    }
    for (k = 0; k < NPC_SPAWN_COPIES_MAX; ++k) {
        if (session->leftover[k] &&
            ops->remove(ops->user, NPC_SPAWN_KEY_FIRST + k) == NPC_SPAWN_OUTCOME_DONE) {
            session->leftover[k] = false;
            ++session->counters.old_removed;
        }
    }
}

/* ---- The grants. ---- */

static npc_spawn_outcome_t build(npc_spawn_session_t *session, const npc_spawn_grant_t *grant,
                                 const npc_spawn_session_ops_t *ops)
{
    uint32_t            k = (uint32_t)grant->key - NPC_SPAWN_KEY_FIRST;
    npc_spawn_desc_t    desc;
    npc_spawn_saved_t   saved;
    bool                restored;
    npc_spawn_outcome_t outcome;

    if (!npc_spawn_session_from_note(&grant->desc, &desc)) {
        ++session->counters.build_refused;
        return NPC_SPAWN_OUTCOME_REFUSED;
    }
    restored = grant->wish != 0u && take_restore(session, grant->wish, &saved);
    outcome  = ops->build(ops->user, &desc, grant->key, restored ? &saved : NULL);
    if (outcome == NPC_SPAWN_OUTCOME_NOT_NOW) {
        if (restored) {
            keep_restore(session, grant->wish, &saved);   /* asked again with the grant */
        }
        return outcome;
    }
    if (outcome == NPC_SPAWN_OUTCOME_DONE) {
        session->built[k]           = grant->generation;
        session->built_as_client[k] = session->cap == 0u;
        session->owner[k]           = grant->owner;
        session->leftover[k]        = false;   /* the key was free, so the old copy is gone */
        ++session->counters.built;
        session->counters.restores_placed += restored ? 1u : 0u;
    } else {
        ++session->counters.build_refused;
    }
    return outcome;
}

static npc_spawn_outcome_t cancel(npc_spawn_session_t *session, const npc_spawn_grant_t *grant,
                                  const npc_spawn_session_ops_t *ops)
{
    uint32_t            k = (uint32_t)grant->key - NPC_SPAWN_KEY_FIRST;
    npc_spawn_outcome_t outcome;

    /* A cancel of a life this panel did not build, or no longer holds, touches nothing: the key
     * may carry a newer copy by now. */
    if (session->built[k] != grant->generation) {
        ++session->counters.cancel_stale;
        return NPC_SPAWN_OUTCOME_DONE;
    }
    outcome = ops->remove(ops->user, grant->key);
    if (outcome == NPC_SPAWN_OUTCOME_DONE) {
        session->built[k] = 0u;
        ++session->counters.cancelled;
    } else if (outcome == NPC_SPAWN_OUTCOME_REFUSED) {
        ++session->counters.cancel_ridden;
    }
    return outcome;
}

static npc_spawn_outcome_t answer(npc_spawn_session_t *session, const npc_spawn_grant_t *grant,
                                  const npc_spawn_session_ops_t *ops)
{
    npc_spawn_saved_t unused;

    switch (grant->kind) {
    case NPC_SPAWN_GRANT_BUILD:
        return build(session, grant, ops);
    case NPC_SPAWN_GRANT_CANCEL:
        return cancel(session, grant, ops);
    case NPC_SPAWN_GRANT_OWNER:
        ++session->counters.owners;
        /* Of the life built here only: a late change for a life gone says nothing of a new one. */
        if (session->built[grant->key - NPC_SPAWN_KEY_FIRST] == grant->generation) {
            session->owner[grant->key - NPC_SPAWN_KEY_FIRST] = grant->owner;
        }
        return NPC_SPAWN_OUTCOME_DONE;
    case NPC_SPAWN_GRANT_REFUSED:
    default:
        ++session->counters.refusals;
        (void)take_restore(session, grant->wish, &unused);
        if (ops->refused != NULL) {
            ops->refused(ops->user, grant->wish, kind_of(session, grant->wish), grant->reason);
        }
        return NPC_SPAWN_OUTCOME_DONE;
    }
}

static void answer_grants(npc_spawn_session_t *session, const npc_spawn_grant_record_t *grants,
                          bool now, const npc_spawn_session_ops_t *ops)
{
    uint32_t i;

    for (i = 0; i < grants->count; ++i) {
        uint32_t            serial = grants->first + i;
        npc_spawn_outcome_t outcome;

        if (!npc_spawn_note_serial_after(serial, session->record.grants_done)) {
            continue;
        }
        if (!now || ops == NULL) {
            ++session->counters.held_back;
            return;   /* what holds one grant holds them all, and the order stays */
        }
        outcome = answer(session, &grants->entry[i], ops);
        if (outcome == NPC_SPAWN_OUTCOME_NOT_NOW) {
            ++session->counters.held_back;
            return;
        }
        (void)npc_spawn_note_acknowledge(&session->record, serial,
                                         outcome == NPC_SPAWN_OUTCOME_REFUSED);
        session->changed = true;
    }
}

bool npc_spawn_session_frame(npc_spawn_session_t *session,
                             const npc_spawn_grant_record_t *grants, bool now,
                             const npc_spawn_session_ops_t *ops)
{
    if (session == NULL) {
        return false;
    }
    if (grants != NULL) {
        bool moved = session->read && grants->epoch != session->epoch;

        session->read     = true;
        session->epoch    = grants->epoch;
        session->own_slot = grants->own_slot;
        session->cap      = grants->cap;
        if (moved) {
            ++session->counters.epochs;
            end_old_wishes(session);
            /* Every restore still kept belongs to a wish of the old epoch, including one the
             * multiplayer took and whose grant the new epoch emptied before it was answered. */
            session->restores = 0u;
            leave_the_old_world(session);
        }
    }
    remove_leftovers(session, now, ops);
    if (grants != NULL) {
        answer_grants(session, grants, now, ops);
        if (npc_spawn_note_wishes_drop(&session->record, grants->wishes_taken) != 0u) {
            session->changed = true;
        }
    }
    fill_record(session);
    return session->changed;
}

bool npc_spawn_session_active(const npc_spawn_session_t *session)
{
    return session != NULL && session->read && (session->cap != 0u || session->own_slot != 0u);
}

bool npc_spawn_session_is_client(const npc_spawn_session_t *session)
{
    return npc_spawn_session_active(session) && session->cap == 0u;
}

bool npc_spawn_session_owner(const npc_spawn_session_t *session, uint32_t k, uint8_t *owner)
{
    if (session == NULL || owner == NULL || k >= NPC_SPAWN_COPIES_MAX || session->built[k] == 0u) {
        return false;
    }
    *owner = session->owner[k];
    return true;
}

npc_spawn_flyer_t npc_spawn_session_flyer(const npc_spawn_session_t *session,
                                          const npc_spawn_anchor_record_t *anchors, uint32_t key,
                                          float out[3])
{
    uint8_t owner = 0;

    if (!npc_spawn_session_active(session) || npc_spawn_session_is_client(session) ||
        !npc_spawn_key_is_copy(key) ||
        !npc_spawn_session_owner(session, key - NPC_SPAWN_KEY_FIRST, &owner)) {
        return NPC_SPAWN_FLYER_LOCAL;
    }
    if (anchors == NULL || anchors->epoch != session->epoch || owner >= NPC_SPAWN_WORLD_SLOTS ||
        (anchors->valid & (1u << owner)) == 0u || out == NULL) {
        return NPC_SPAWN_FLYER_KEEP;
    }
    memcpy(out, anchors->point[owner], 3u * sizeof(float));
    return NPC_SPAWN_FLYER_ANCHOR;
}

const npc_spawn_wish_record_t *npc_spawn_session_record(const npc_spawn_session_t *session)
{
    return session != NULL ? &session->record : NULL;
}

void npc_spawn_session_published(npc_spawn_session_t *session, bool taken)
{
    if (session != NULL && taken) {
        session->changed = false;
    }
}
