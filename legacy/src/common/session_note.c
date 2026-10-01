#include "session_note.h"

#include "logging.h"
#include "shared_note.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Bumped when the record below changes shape. A reader that finds a version it does not know
 * answers false rather than reading a field that has moved, which here would mean unlocking a row
 * in the middle of a session.
 *
 * The input hold did not bump it, on purpose. It took a byte every publisher already wrote as
 * zero, so the shape is the same and so is the meaning of every field a reader built before it
 * knows. Every mod links its own copy of this file, and a bump would have made a DLL from the
 * previous build refuse every note of a newer multiplayer in the middle of a session. */
#define NOTE_VERSION 1u

/* Bytes rather than bools on the wire between two DLLs: `bool` is one byte in both of them today,
 * and a record whose layout is a contract should not rest on that. */
typedef struct session_record {
    uint32_t version;
    uint8_t  running;
    uint8_t  is_host;
    uint8_t  input_held;   /* was reserved, and zero from every publisher, until the hold */
    uint8_t  reserved;
} session_record_t;

_Static_assert(sizeof(session_record_t) == 8u, "Unexpected session_record_t size");
_Static_assert(offsetof(session_record_t, input_held) == 6u,
               "the input hold is the first byte that used to be reserved");

/* Whether a read has ever found the note in this module, and when the last miss was. Each DLL
 * links its own copy of this file, so each keeps its own. */
typedef struct session_reader {
    bool     found_once;
    bool     missed_once;
    uint32_t missed_at_ms;
    bool     unreadable;           /* the last read found a note filed in another shape */
    bool     unreadable_reported;  /* and the log has been told so once */
} session_reader_t;

static session_reader_t reader;

bool session_note_publish(const session_note_t *note)
{
    session_record_t record;

    if (note == NULL) {
        return false;
    }
    memset(&record, 0, sizeof record);
    record.version = NOTE_VERSION;
    record.running = note->running ? 1u : 0u;
    /* A host that is not running is not a host. Publishing the two independently would leave the
     * second reading true after a session ended, which is the one combination nothing can use.
     * The hold is the same: a pause menu that holds input with no session under it holds nothing
     * anybody else should stop for. */
    record.is_host    = (note->running && note->is_host) ? 1u : 0u;
    record.input_held = (note->running && note->input_held) ? 1u : 0u;
    return shared_note_publish(SESSION_NOTE_NAME, &record, sizeof record);
}

bool session_note_read(session_note_t *out)
{
    session_record_t record;
    uint8_t          raw[SHARED_NOTE_BYTES];
    size_t           count = 0;

    if (out == NULL) {
        return false;
    }
    /* Read into room for any note, so that one longer than this build's record is seen as filed
     * rather than refused as if nobody had published. */
    if (!shared_note_read(SESSION_NOTE_NAME, raw, sizeof raw, &count, NULL)) {
        return false;
    }
    memset(&record, 0, sizeof record);
    memcpy(&record, raw, count < sizeof record ? count : sizeof record);
    /* Filed, but not in the shape this build reads: a module from another build is on the other
     * end. The refusal is the same as for no note at all, so it is said once, and a reader that
     * locks what it cannot judge can ask for it. */
    if (count != sizeof record || record.version != NOTE_VERSION) {
        reader.unreadable = true;
        if (!reader.unreadable_reported) {
            reader.unreadable_reported = true;
            log_warning("the session note is filed but not in the shape this module reads: %u "
                        "byte(s) against %u, version %u against %u. Another build published it, "
                        "and this module takes it as no session",
                        (unsigned)count, (unsigned)sizeof record,
                        count >= sizeof(uint32_t) ? (unsigned)record.version : 0u,
                        (unsigned)NOTE_VERSION);
        }
        return false;
    }
    reader.unreadable = false;
    out->running    = record.running != 0u;
    out->is_host    = record.is_host != 0u;
    out->input_held = record.running != 0u && record.input_held != 0u;
    return true;
}

bool session_note_input_held(uint32_t now_ms)
{
    session_note_t note;

    /* A name nobody filed is a failed mapping lookup in the operating system, and a caller on the
     * frame path asks every frame. Once a read has found the note the mapping stays open and a read
     * is a copy, so only the misses before that first find are spaced out. */
    if (!reader.found_once && reader.missed_once &&
        now_ms - reader.missed_at_ms < SESSION_NOTE_RETRY_MS) {
        return false;
    }
    memset(&note, 0, sizeof note);
    if (!session_note_read(&note)) {
        if (!reader.found_once) {
            reader.missed_once  = true;
            reader.missed_at_ms = now_ms;
        }
        return false;
    }
    reader.found_once = true;
    return note.input_held;
}

bool session_note_unreadable(void)
{
    return reader.unreadable;
}
