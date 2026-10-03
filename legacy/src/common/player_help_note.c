/* player_help_note.c: the two records between the developer menu's buttons and the multiplayer.
 * See the header. */
#include "player_help_note.h"

#include "shared_note.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The version the records carry, spelled once in the header so that both sides and their tests
 * read the same number. */
#define NOTE_VERSION PLAYER_HELP_NOTE_VERSION

/* Filed through a shared note and read by another DLL, so every layout is a contract: each field
 * is held to its offset and each record to its size, neither with a byte of padding. Bytes carry
 * what would be a bool or an enum elsewhere, because the width of those is the compiler's and a
 * record two DLLs share should not rest on it. */
#define FIELD_AT(type, field, offset) \
    _Static_assert(offsetof(type, field) == (offset), "a moved field is a new NOTE_VERSION")
#define SIZE_IS(type, size) \
    _Static_assert(sizeof(type) == (size), "a new size is a new NOTE_VERSION")

FIELD_AT(player_help_ask_t, version, 0u);
FIELD_AT(player_help_ask_t, kind, 2u);
FIELD_AT(player_help_ask_t, flags, 3u);
FIELD_AT(player_help_ask_t, serial, 4u);
SIZE_IS(player_help_ask_t, 8u);

FIELD_AT(player_help_answer_t, version, 0u);
FIELD_AT(player_help_answer_t, ready, 2u);
FIELD_AT(player_help_answer_t, outcome, 3u);
FIELD_AT(player_help_answer_t, serial, 4u);
FIELD_AT(player_help_answer_t, kind, 8u);
FIELD_AT(player_help_answer_t, reason, 9u);
FIELD_AT(player_help_answer_t, released, 10u);
SIZE_IS(player_help_answer_t, 12u);

_Static_assert(sizeof(player_help_ask_t) <= SHARED_NOTE_BYTES &&
               sizeof(player_help_answer_t) <= SHARED_NOTE_BYTES,
               "a record must fit one shared note");
_Static_assert(PLAYER_HELP_REASON_COUNT <= UINT8_MAX, "a reason must fit its byte");

#define ASK_FLAGS_KNOWN  PLAYER_HELP_ASK_F_OVERLAY_HOLDS
#define READY_BITS_KNOWN (PLAYER_HELP_READY_LISTENING | PLAYER_HELP_READY_CAN_REPAIR | \
                          PLAYER_HELP_READY_CAN_TELEPORT)

static bool kind_is_known(uint8_t kind)
{
    return kind == PLAYER_HELP_KIND_NONE || kind == PLAYER_HELP_KIND_REPAIR ||
           kind == PLAYER_HELP_KIND_TELEPORT;
}

/* The reader acts on an ask, so the one thing it must be able to rely on is held here: a serial
 * stands on a press and on nothing else. An ask with a kind and no serial could never be told
 * from the one before it, and a serial on the empty ask would be a press of nothing. */
static bool ask_is_sound(const player_help_ask_t *ask)
{
    if (ask == NULL || ask->version != NOTE_VERSION || !kind_is_known(ask->kind) ||
        ((unsigned)ask->flags & ~ASK_FLAGS_KNOWN) != 0u) {
        return false;
    }
    if (ask->kind == PLAYER_HELP_KIND_NONE) {
        return ask->serial == 0u && ask->flags == 0u;
    }
    return ask->serial != 0u;
}

/* Ranges and known bits only. Which outcome goes with which serial is the writer's to decide: an
 * answer that names a serial and says nothing about it yet is one the panel reads as no answer,
 * and refusing it here would only hide the ready bits it carries. */
static bool answer_is_sound(const player_help_answer_t *answer)
{
    return answer != NULL && answer->version == NOTE_VERSION && kind_is_known(answer->kind) &&
           answer->outcome <= PLAYER_HELP_OUTCOME_REFUSED &&
           answer->reason < PLAYER_HELP_REASON_COUNT &&
           ((unsigned)answer->ready & ~READY_BITS_KNOWN) == 0u;
}

bool player_help_ask_publish(const player_help_ask_t *ask)
{
    return ask_is_sound(ask) && shared_note_publish(PLAYER_HELP_ASK_NOTE_NAME, ask, sizeof *ask);
}

bool player_help_answer_publish(const player_help_answer_t *answer)
{
    return answer_is_sound(answer) &&
           shared_note_publish(PLAYER_HELP_ANSWER_NOTE_NAME, answer, sizeof *answer);
}

/* One read into a local record, so that a refusal never touches the caller's. A note of another
 * size is another build's and is refused whole, whatever its first bytes say. */
static bool read_record(const char *name, void *record, size_t size)
{
    size_t count = 0;

    return shared_note_read(name, record, size, &count, NULL) && count == size;
}

bool player_help_ask_read(player_help_ask_t *out)
{
    player_help_ask_t record;

    if (out == NULL || !read_record(PLAYER_HELP_ASK_NOTE_NAME, &record, sizeof record) ||
        !ask_is_sound(&record)) {
        return false;
    }
    *out = record;
    return true;
}

bool player_help_answer_read(player_help_answer_t *out)
{
    player_help_answer_t record;

    if (out == NULL || !read_record(PLAYER_HELP_ANSWER_NOTE_NAME, &record, sizeof record) ||
        !answer_is_sound(&record)) {
        return false;
    }
    *out = record;
    return true;
}

/* The distance from the mark to the serial, taken in the counter's own arithmetic: a press is
 * after the mark while it is less than half the range ahead of it. So a serial of 1 is after a
 * mark of 0xFFFFFFFF, which is where the counter stands once it has wrapped past the 0 no press
 * carries, and the mark itself is not after itself. */
bool player_help_serial_after(uint32_t serial, uint32_t mark)
{
    uint32_t ahead = serial - mark;

    return ahead != 0u && ahead <= (uint32_t)INT32_MAX;
}
