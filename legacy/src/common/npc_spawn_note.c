#include "npc_spawn_note.h"

#include "shared_note.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Bumped when a record below changes shape. A reader that finds a version it does not know
 * answers false rather than reading a field that has moved, which here would mean building a copy
 * nobody asked for under a key somebody else holds. */
#define NOTE_VERSION 1u

/* How many answers the wish record keeps: the bits of `refused`. */
#define ANSWERS_KEPT 32u

/* Filed through a shared note and read back by another DLL, so every layout is a contract: each
 * field is held to its offset and each record to its size, none of them with a byte of padding. */
#define FIELD_AT(type, field, offset) \
    _Static_assert(offsetof(type, field) == (offset), "a moved field is a new NOTE_VERSION")
#define SIZE_IS(type, size) \
    _Static_assert(sizeof(type) == (size), "a new size is a new NOTE_VERSION")

FIELD_AT(npc_spawn_note_desc_t, source, 0u);
FIELD_AT(npc_spawn_note_desc_t, behaviour, 1u);
FIELD_AT(npc_spawn_note_desc_t, flags, 2u);
FIELD_AT(npc_spawn_note_desc_t, reserved, 3u);
FIELD_AT(npc_spawn_note_desc_t, position, 4u);
FIELD_AT(npc_spawn_note_desc_t, facing, 16u);
FIELD_AT(npc_spawn_note_desc_t, file, 20u);
SIZE_IS(npc_spawn_note_desc_t, 32u);

FIELD_AT(npc_spawn_wish_t, kind, 0u);
FIELD_AT(npc_spawn_wish_t, epoch, 1u);
FIELD_AT(npc_spawn_wish_t, reserved, 2u);
FIELD_AT(npc_spawn_wish_t, desc, 4u);
SIZE_IS(npc_spawn_wish_t, 36u);

FIELD_AT(npc_spawn_wish_record_t, version, 0u);
FIELD_AT(npc_spawn_wish_record_t, count, 2u);
FIELD_AT(npc_spawn_wish_record_t, panel, 3u);
FIELD_AT(npc_spawn_wish_record_t, first, 4u);
FIELD_AT(npc_spawn_wish_record_t, grants_done, 8u);
FIELD_AT(npc_spawn_wish_record_t, refused, 12u);
FIELD_AT(npc_spawn_wish_record_t, reserved, 16u);
FIELD_AT(npc_spawn_wish_record_t, entry, 20u);
SIZE_IS(npc_spawn_wish_record_t, 236u);

FIELD_AT(npc_spawn_grant_t, kind, 0u);
FIELD_AT(npc_spawn_grant_t, generation, 1u);
FIELD_AT(npc_spawn_grant_t, key, 2u);
FIELD_AT(npc_spawn_grant_t, owner, 4u);
FIELD_AT(npc_spawn_grant_t, reason, 5u);
FIELD_AT(npc_spawn_grant_t, reserved, 6u);
FIELD_AT(npc_spawn_grant_t, wish, 8u);
FIELD_AT(npc_spawn_grant_t, desc, 12u);
SIZE_IS(npc_spawn_grant_t, 44u);

FIELD_AT(npc_spawn_grant_record_t, version, 0u);
FIELD_AT(npc_spawn_grant_record_t, count, 2u);
FIELD_AT(npc_spawn_grant_record_t, own_slot, 3u);
FIELD_AT(npc_spawn_grant_record_t, first, 4u);
FIELD_AT(npc_spawn_grant_record_t, wishes_taken, 8u);
FIELD_AT(npc_spawn_grant_record_t, cap, 12u);
FIELD_AT(npc_spawn_grant_record_t, epoch, 14u);
FIELD_AT(npc_spawn_grant_record_t, reserved, 15u);
FIELD_AT(npc_spawn_grant_record_t, entry, 16u);
SIZE_IS(npc_spawn_grant_record_t, 236u);

FIELD_AT(npc_spawn_anchor_record_t, version, 0u);
FIELD_AT(npc_spawn_anchor_record_t, valid, 2u);
FIELD_AT(npc_spawn_anchor_record_t, epoch, 4u);
FIELD_AT(npc_spawn_anchor_record_t, reserved, 5u);
FIELD_AT(npc_spawn_anchor_record_t, point, 8u);
SIZE_IS(npc_spawn_anchor_record_t, 200u);

_Static_assert(sizeof(npc_spawn_anchor_record_t) <= SHARED_NOTE_BYTES &&
               sizeof(npc_spawn_wish_record_t) <= SHARED_NOTE_BYTES &&
               sizeof(npc_spawn_grant_record_t) <= SHARED_NOTE_BYTES,
               "A record must fit one shared note");
_Static_assert(NPC_SPAWN_WORLD_SLOTS <= 16u, "The valid mask is sixteen bits");
_Static_assert(NPC_SPAWN_KEY_END - 1u <= UINT16_MAX, "A key must fit its sixteen bits");

bool npc_spawn_key_is_copy(uint32_t key)
{
    return key >= NPC_SPAWN_KEY_FIRST && key < NPC_SPAWN_KEY_END;
}

/* A name the archive could hold: 1 to 12 printable characters without a space, and nothing but
 * NUL after it, so that two machines that describe the same file hold the same bytes. */
static bool file_is_sound(const char *file)
{
    size_t length = 0;
    size_t i;

    while (length < NPC_SPAWN_FILE_MAX && file[length] != '\0') {
        if (file[length] <= ' ' || file[length] > '~') {
            return false;
        }
        ++length;
    }
    for (i = length; i < NPC_SPAWN_FILE_MAX; ++i) {
        if (file[i] != '\0') {
            return false;
        }
    }
    return length > 0u;
}

bool npc_spawn_note_desc_is_sound(const npc_spawn_note_desc_t *desc)
{
    bool archive;

    if (desc == NULL) {
        return false;
    }
    archive = (desc->flags & NPC_SPAWN_DESC_ARCHIVE) != 0u;
    return (desc->flags & ~NPC_SPAWN_DESC_ARCHIVE) == 0u && desc->reserved == 0u &&
           desc->behaviour < NPC_SPAWN_BEHAVIOURS &&
           (archive || desc->source != NPC_SPAWN_NO_SOURCE) && isfinite(desc->position[0]) &&
           isfinite(desc->position[1]) && isfinite(desc->position[2]) &&
           isfinite(desc->facing) && file_is_sound(desc->file);
}

/* A kind that carries no description carries a zero one, so that equal entries are equal bytes. */
static bool desc_is_zero(const npc_spawn_note_desc_t *desc)
{
    static const npc_spawn_note_desc_t zero = {0};

    return memcmp(desc, &zero, sizeof zero) == 0;
}

bool npc_spawn_note_wish_is_sound(const npc_spawn_wish_t *wish)
{
    if (wish == NULL || wish->reserved != 0u) {
        return false;
    }
    switch (wish->kind) {
    case NPC_SPAWN_WISH_SPAWN:
    case NPC_SPAWN_WISH_RESTORE:
        return npc_spawn_note_desc_is_sound(&wish->desc);
    case NPC_SPAWN_WISH_REMOVE_OWN:
    case NPC_SPAWN_WISH_REMOVE_ALL:
        return desc_is_zero(&wish->desc);
    default:
        return false;
    }
}

bool npc_spawn_note_grant_is_sound(const npc_spawn_grant_t *grant)
{
    if (grant == NULL || grant->owner >= NPC_SPAWN_WORLD_SLOTS || grant->reserved != 0u) {
        return false;
    }
    switch (grant->kind) {
    case NPC_SPAWN_GRANT_BUILD:
        return npc_spawn_key_is_copy(grant->key) && npc_spawn_note_desc_is_sound(&grant->desc);
    case NPC_SPAWN_GRANT_CANCEL:
    case NPC_SPAWN_GRANT_OWNER:
        return npc_spawn_key_is_copy(grant->key) && desc_is_zero(&grant->desc);
    case NPC_SPAWN_GRANT_REFUSED:
        return grant->key == 0u && grant->wish != 0u && grant->reason != 0u &&
               desc_is_zero(&grant->desc);
    default:
        return false;
    }
}

static bool wishes_are_sound(const npc_spawn_wish_record_t *record)
{
    size_t i;

    if (record->count > NPC_SPAWN_WISH_SLOTS) {
        return false;
    }
    for (i = 0; i < record->count; ++i) {
        if (!npc_spawn_note_wish_is_sound(&record->entry[i])) {
            return false;
        }
    }
    return true;
}

static bool grants_are_sound(const npc_spawn_grant_record_t *record)
{
    size_t i;

    if (record->count > NPC_SPAWN_GRANT_SLOTS || record->own_slot >= NPC_SPAWN_WORLD_SLOTS) {
        return false;
    }
    for (i = 0; i < record->count; ++i) {
        if (!npc_spawn_note_grant_is_sound(&record->entry[i])) {
            return false;
        }
    }
    return true;
}

static bool anchors_are_sound(const npc_spawn_anchor_record_t *record)
{
    size_t slot;

    for (slot = 0; slot < NPC_SPAWN_WORLD_SLOTS; ++slot) {
        const float *p = record->point[slot];

        if ((record->valid & (1u << slot)) != 0u &&
            !(isfinite(p[0]) && isfinite(p[1]) && isfinite(p[2]))) {
            return false;
        }
    }
    return true;
}

bool npc_spawn_note_publish_wishes(const npc_spawn_wish_record_t *record)
{
    npc_spawn_wish_record_t out;

    if (record == NULL || !wishes_are_sound(record)) {
        return false;
    }
    memset(&out, 0, sizeof out);
    out.version     = NOTE_VERSION;
    out.count       = record->count;
    out.panel       = record->panel;
    out.first       = record->first;
    out.grants_done = record->grants_done;
    out.refused     = record->refused;
    memcpy(out.entry, record->entry, record->count * sizeof out.entry[0]);
    return shared_note_publish(NPC_SPAWN_WISH_NOTE_NAME, &out, sizeof out);
}

bool npc_spawn_note_publish_grants(const npc_spawn_grant_record_t *record)
{
    npc_spawn_grant_record_t out;

    if (record == NULL || !grants_are_sound(record)) {
        return false;
    }
    memset(&out, 0, sizeof out);
    out.version      = NOTE_VERSION;
    out.count        = record->count;
    out.own_slot     = record->own_slot;
    out.first        = record->first;
    out.wishes_taken = record->wishes_taken;
    out.cap          = record->cap;
    out.epoch        = record->epoch;
    memcpy(out.entry, record->entry, record->count * sizeof out.entry[0]);
    return shared_note_publish(NPC_SPAWN_GRANT_NOTE_NAME, &out, sizeof out);
}

bool npc_spawn_note_publish_anchors(const npc_spawn_anchor_record_t *record)
{
    npc_spawn_anchor_record_t out;
    size_t                    slot;

    if (record == NULL || !anchors_are_sound(record)) {
        return false;
    }
    memset(&out, 0, sizeof out);
    out.version = NOTE_VERSION;
    out.valid   = record->valid;
    out.epoch   = record->epoch;
    for (slot = 0; slot < NPC_SPAWN_WORLD_SLOTS; ++slot) {
        if ((record->valid & (1u << slot)) != 0u) {
            memcpy(out.point[slot], record->point[slot], sizeof out.point[slot]);
        }
    }
    return shared_note_publish(NPC_SPAWN_ANCHOR_NOTE_NAME, &out, sizeof out);
}

/* One read into a local record, so that a refusal never touches the caller's. */
static bool read_record(const char *name, void *record, size_t size)
{
    size_t count = 0;

    return shared_note_read(name, record, size, &count, NULL) && count == size;
}

bool npc_spawn_note_read_wishes(npc_spawn_wish_record_t *out)
{
    npc_spawn_wish_record_t record;

    if (out == NULL || !read_record(NPC_SPAWN_WISH_NOTE_NAME, &record, sizeof record) ||
        record.version != NOTE_VERSION || !wishes_are_sound(&record)) {
        return false;
    }
    *out = record;
    return true;
}

bool npc_spawn_note_read_grants(npc_spawn_grant_record_t *out)
{
    npc_spawn_grant_record_t record;

    if (out == NULL || !read_record(NPC_SPAWN_GRANT_NOTE_NAME, &record, sizeof record) ||
        record.version != NOTE_VERSION || !grants_are_sound(&record)) {
        return false;
    }
    *out = record;
    return true;
}

bool npc_spawn_note_read_anchors(npc_spawn_anchor_record_t *out)
{
    npc_spawn_anchor_record_t record;

    if (out == NULL || !read_record(NPC_SPAWN_ANCHOR_NOTE_NAME, &record, sizeof record) ||
        record.version != NOTE_VERSION || !anchors_are_sound(&record)) {
        return false;
    }
    *out = record;
    return true;
}

bool npc_spawn_note_serial_after(uint32_t serial, uint32_t mark)
{
    uint32_t ahead = serial - mark;

    return ahead != 0u && ahead <= (uint32_t)INT32_MAX;
}

bool npc_spawn_note_grants_append(npc_spawn_grant_record_t *record, uint32_t serial,
                                  const npc_spawn_grant_t *grant)
{
    if (record == NULL || grant == NULL || record->count >= NPC_SPAWN_GRANT_SLOTS ||
        serial == 0u || serial != record->first + record->count ||
        !npc_spawn_note_grant_is_sound(grant)) {
        return false;
    }
    record->entry[record->count++] = *grant;
    return true;
}

uint32_t npc_spawn_note_grants_drop(npc_spawn_grant_record_t *record, uint32_t done)
{
    uint32_t dropped = 0;

    while (record != NULL && record->count > 0u &&
           !npc_spawn_note_serial_after(record->first, done)) {
        memmove(&record->entry[0], &record->entry[1],
                (size_t)(record->count - 1u) * sizeof record->entry[0]);
        memset(&record->entry[record->count - 1u], 0, sizeof record->entry[0]);
        --record->count;
        ++record->first;
        ++dropped;
    }
    return dropped;
}

void npc_spawn_note_grants_restart(npc_spawn_grant_record_t *record)
{
    if (record != NULL) {
        record->first += record->count;
        record->count = 0u;
        memset(record->entry, 0, sizeof record->entry);
    }
}

bool npc_spawn_note_wishes_append(npc_spawn_wish_record_t *record, uint32_t serial,
                                  const npc_spawn_wish_t *wish)
{
    if (record == NULL || wish == NULL || record->count >= NPC_SPAWN_WISH_SLOTS ||
        serial == 0u || serial != record->first + record->count ||
        !npc_spawn_note_wish_is_sound(wish)) {
        return false;
    }
    record->entry[record->count++] = *wish;
    return true;
}

uint32_t npc_spawn_note_wishes_drop(npc_spawn_wish_record_t *record, uint32_t taken)
{
    uint32_t dropped = 0;

    while (record != NULL && record->count > 0u &&
           !npc_spawn_note_serial_after(record->first, taken)) {
        memmove(&record->entry[0], &record->entry[1],
                (size_t)(record->count - 1u) * sizeof record->entry[0]);
        memset(&record->entry[record->count - 1u], 0, sizeof record->entry[0]);
        --record->count;
        ++record->first;
        ++dropped;
    }
    return dropped;
}

bool npc_spawn_note_acknowledge(npc_spawn_wish_record_t *wishes, uint32_t serial, bool refused)
{
    uint32_t ahead;
    uint32_t bits;

    if (wishes == NULL || !npc_spawn_note_serial_after(serial, wishes->grants_done)) {
        return false;
    }
    /* Bit 0 is the grant answered now; the ones between it and the old mark were emptied by an
     * epoch before the overlay saw them, and are marked not done. */
    ahead = serial - wishes->grants_done;
    if (ahead >= ANSWERS_KEPT) {
        bits = ~1u;
    } else {
        bits = (wishes->refused << ahead) | (((1u << ahead) - 1u) & ~1u);
    }
    wishes->refused     = bits | (refused ? 1u : 0u);
    wishes->grants_done = serial;
    return true;
}

npc_spawn_answer_t npc_spawn_note_answer(const npc_spawn_wish_record_t *wishes, uint32_t serial)
{
    uint32_t behind;

    if (wishes == NULL || npc_spawn_note_serial_after(serial, wishes->grants_done)) {
        return NPC_SPAWN_ANSWER_OPEN;
    }
    behind = wishes->grants_done - serial;
    if (behind >= ANSWERS_KEPT) {
        return NPC_SPAWN_ANSWER_LOST;
    }
    return ((wishes->refused >> behind) & 1u) != 0u ? NPC_SPAWN_ANSWER_REFUSED
                                                     : NPC_SPAWN_ANSWER_DONE;
}
