#include "shared_note.h"

#include "logging.h"
#include "text.h"

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Bumped when the record below changes shape. A reader that finds a version it does not know
 * refuses rather than reading fields that have moved. */
#define NOTE_VERSION 1u

/* 'OBIN', so a mapping that happens to carry this name for some other reason is not read as one
 * of ours. */
#define NOTE_MAGIC 0x4E49424Fu

/* How many open handles one process keeps. Each distinct name costs one; a mod that files more
 * than this many kinds of note is doing something this layer was not built for, and is told so
 * once rather than silently losing the last one. The multiplayer opens the most, fourteen names:
 * the session, the appearance, the spawner's wishes, grants and anchors, the two model wear
 * records, the two movie records, the host's settings with the acknowledgements of the two mods
 * that take them, and the ask and the answer of the player help. So the count is sixteen, and a
 * fifteenth and sixteenth name still fit; it sizes this module's own table of handles and
 * nothing that crosses to another DLL. */
#define NOTE_SLOTS 16u

typedef struct note_record {
    uint32_t magic;
    uint32_t version;
    volatile uint32_t serial;   /* odd while a write is in progress */
    uint32_t count;
    uint8_t  bytes[SHARED_NOTE_BYTES];
} note_record_t;

/* The record crosses a DLL boundary through the mapping, so its layout is a contract between
 * builds and not a detail of this one. */
_Static_assert(sizeof(note_record_t) == 16u + SHARED_NOTE_BYTES, "Unexpected note_record_t size");

typedef struct note_slot {
    char          name[SHARED_NOTE_NAME_MAX];
    HANDLE        mapping;
    note_record_t *view;
} note_slot_t;

static note_slot_t notes[NOTE_SLOTS];
static bool        slots_full_logged;

bool shared_note_name_is_sound(const char *name)
{
    size_t i;

    if (name == NULL || name[0] == '\0') {
        return false;
    }
    for (i = 0; name[i] != '\0'; ++i) {
        char c = name[i];

        if (i + 1u >= SHARED_NOTE_NAME_MAX) {
            return false;
        }
        if (!(c >= 'a' && c <= 'z') && !(c >= 'A' && c <= 'Z') && !(c >= '0' && c <= '9') &&
            c != '_') {
            return false;
        }
    }
    return true;
}

/* The kernel object's name. The process id is in it on purpose: two processes of the game on one
 * machine must never share a note, or each would show the other's player as its own model. */
static void object_name(const char *name, char *out, size_t out_size)
{
    text_format(out, out_size, "Local\\obi_note_%s_%lu", name,
                (unsigned long)GetCurrentProcessId());
}

static note_slot_t *find_slot(const char *name)
{
    size_t i;

    for (i = 0; i < NOTE_SLOTS; ++i) {
        if (notes[i].view != NULL && strcmp(notes[i].name, name) == 0) {
            return &notes[i];
        }
    }
    return NULL;
}

/* Opens or creates the mapping for a name. `create` says what to do when nobody has made it yet:
 * a publisher makes one, a reader does not, so a read before anybody published answers false
 * instead of filing an empty note that a later publisher would then have to notice. */
static note_slot_t *acquire(const char *name, bool create)
{
    char         object[SHARED_NOTE_NAME_MAX + 64u];
    note_slot_t *slot = find_slot(name);
    size_t       i;
    HANDLE       mapping;
    void        *view;

    if (slot != NULL) {
        return slot;
    }
    for (i = 0; i < NOTE_SLOTS; ++i) {
        if (notes[i].view == NULL) {
            slot = &notes[i];
            break;
        }
    }
    if (slot == NULL) {
        if (!slots_full_logged) {
            slots_full_logged = true;
            log_warning("this module holds shared notes under %u names already and cannot open "
                        "'%s'; the note is not published or read", (unsigned)NOTE_SLOTS, name);
        }
        return NULL;
    }

    object_name(name, object, sizeof object);
    if (create) {
        mapping = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0,
                                     (DWORD)sizeof(note_record_t), object);
    } else {
        mapping = OpenFileMappingA(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, object);
    }
    if (mapping == NULL) {
        return NULL;
    }
    view = MapViewOfFile(mapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, sizeof(note_record_t));
    if (view == NULL) {
        CloseHandle(mapping);
        return NULL;
    }
    memcpy(slot->name, name, strlen(name) + 1u);
    slot->mapping = mapping;
    slot->view    = (note_record_t *)view;
    return slot;
}

bool shared_note_publish(const char *name, const void *bytes, size_t count)
{
    note_slot_t   *slot;
    note_record_t *record;

    if (!shared_note_name_is_sound(name) || count > SHARED_NOTE_BYTES ||
        (count != 0u && bytes == NULL)) {
        return false;
    }
    slot = acquire(name, true);
    if (slot == NULL) {
        return false;
    }
    record = slot->view;

    /* Odd first, even last, and the payload strictly between them. A reader that sees an odd
     * serial, or two different ones, knows it looked while this ran. A fresh mapping is all
     * zeroes, so the first publication takes the serial from 0 to 3 while it writes and to 4 when
     * it is done, and never leaves it at a value that means "in progress" if this process dies
     * mid write: the next publisher rounds up from whatever it finds. */
    record->serial = (record->serial | 1u) + 2u;
    record->magic   = NOTE_MAGIC;
    record->version = NOTE_VERSION;
    record->count   = (uint32_t)count;
    if (count != 0u) {
        memcpy(record->bytes, bytes, count);
    }
    record->serial += 1u;
    return true;
}

bool shared_note_read(const char *name, void *bytes, size_t capacity, size_t *out_count,
                      uint32_t *out_serial)
{
    note_slot_t   *slot;
    note_record_t *record;
    unsigned       attempt;

    if (!shared_note_name_is_sound(name) || bytes == NULL || out_count == NULL) {
        return false;
    }
    slot = acquire(name, false);
    if (slot == NULL) {
        return false;
    }
    record = slot->view;

    /* Twice, because one lost race is ordinary and two in a row would mean the publisher writes
     * far more often than it should. Neither attempt blocks. */
    for (attempt = 0; attempt < 2u; ++attempt) {
        uint32_t before = record->serial;
        uint32_t count;
        uint8_t  copy[SHARED_NOTE_BYTES];

        if ((before & 1u) != 0u || before == 0u) {
            continue;   /* a write is in progress, or nobody has published yet */
        }
        if (record->magic != NOTE_MAGIC || record->version != NOTE_VERSION) {
            return false;
        }
        count = record->count;
        if (count > SHARED_NOTE_BYTES) {
            return false;
        }
        if (count != 0u) {
            memcpy(copy, record->bytes, count);
        }
        if (record->serial != before) {
            continue;
        }
        if (count > capacity) {
            return false;
        }
        if (count != 0u) {
            memcpy(bytes, copy, count);
        }
        *out_count = count;
        if (out_serial != NULL) {
            *out_serial = before;
        }
        return true;
    }
    return false;
}

void shared_note_forget(const char *name)
{
    note_slot_t *slot;

    if (!shared_note_name_is_sound(name)) {
        return;
    }
    slot = find_slot(name);
    if (slot == NULL) {
        return;
    }
    (void)UnmapViewOfFile(slot->view);
    CloseHandle(slot->mapping);
    slot->view    = NULL;
    slot->mapping = NULL;
    slot->name[0] = '\0';
}
