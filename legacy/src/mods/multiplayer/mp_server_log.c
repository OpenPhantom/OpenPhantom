/* mp_server_log.c: the ring and the writer thread. See the header for why nothing is formatted on
 * the server's own thread.
 */
#include "mp_server_log.h"

#include "common/text.h"

#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Enough for four seconds of everything at sixteen players, which is far more than the writer
 * needs to catch up after a disk hiccup. */
#define RING_DEFAULT 8192u

/* How long the writer sleeps when it finds the ring empty. Short enough that a line reaches the
 * screen while the reader is still looking, long enough that an idle server is idle. */
#define WRITER_IDLE_MS 20u

typedef struct server_log {
    mp_server_log_record_t *ring;
    size_t                  mask;        /* capacity minus one; capacity is a power of two */
    volatile LONG           head;        /* written ONLY by the server thread */
    volatile LONG           tail;        /* written ONLY by the writer thread */
    volatile LONG           categories;
    volatile LONG           stop;

    HANDLE writer;
    FILE  *file;

    mp_server_log_line_fn_t listener;
    void                   *listener_context;

    volatile LONG offered;
    volatile LONG written;
    volatile LONG dropped;
    volatile LONG filtered;
} server_log_t;

static server_log_t lg;

/* ==============================================================================================
 * The producer side, which is the side that must stay cheap.
 * ============================================================================================ */

bool mp_server_log_wants(uint32_t category)
{
    return lg.ring != NULL && ((uint32_t)lg.categories & category) != 0u;
}

void mp_server_log_put(uint32_t category, const mp_server_log_record_t *record)
{
    LONG head;
    LONG tail;

    if (record == NULL) {
        return;
    }
    if (!mp_server_log_wants(category)) {
        InterlockedIncrement(&lg.filtered);
        return;
    }
    InterlockedIncrement(&lg.offered);

    head = lg.head;
    tail = lg.tail;
    /* One slot is left empty so that a full ring and an empty one are told apart by the indices
     * alone, with no third field for the two threads to disagree about. */
    if ((size_t)((head + 1) - tail) > lg.mask) {
        InterlockedIncrement(&lg.dropped);
        return;
    }
    lg.ring[(size_t)head & lg.mask] = *record;
    /* The record is published only after it is complete. Without this the writer can read an
     * index that promises a record the store buffer has not delivered yet. On x86 a compiler
     * barrier would do for both sides; the full barrier is used because it says what is meant
     * and costs one locked instruction per record, which at some five hundred records a second
     * is not measurable. */
    MemoryBarrier();
    lg.head = head + 1;
}

/* ==============================================================================================
 * Building a record. Each of these checks its category BEFORE it touches one.
 * ============================================================================================ */

static void begin(mp_server_log_record_t *record, uint8_t kind, uint8_t slot)
{
    memset(record, 0, sizeof *record);
    record->ms   = GetTickCount();
    record->kind = kind;
    record->slot = slot;
}

static void put_text(mp_server_log_record_t *record, const char *text)
{
    size_t i;

    if (text == NULL) {
        return;
    }
    for (i = 0; i + 1u < MP_SERVER_LOG_TEXT_MAX && text[i] != '\0'; ++i) {
        /* Printable only, and no line breaks: one record is one line, and a name carrying a
         * newline would otherwise write two and break every parser downstream. */
        record->text[i] = (text[i] >= 0x20 && text[i] < 0x7F) ? text[i] : '?';
    }
    record->text[i] = '\0';
}

void mp_server_log_text(uint32_t category, const char *line)
{
    mp_server_log_record_t record;

    if (!mp_server_log_wants(category)) {
        InterlockedIncrement(&lg.filtered);
        return;
    }
    begin(&record, (uint8_t)MP_SERVER_LOG_KIND_TEXT, 0u);
    put_text(&record, line);
    mp_server_log_put(category, &record);
}

void mp_server_log_join(uint8_t slot, const char *name)
{
    mp_server_log_record_t record;

    if (!mp_server_log_wants(MP_SERVER_LOG_SESSION)) {
        return;
    }
    begin(&record, (uint8_t)MP_SERVER_LOG_KIND_JOIN, slot);
    put_text(&record, name);
    mp_server_log_put(MP_SERVER_LOG_SESSION, &record);
}

void mp_server_log_leave(uint8_t slot, const char *name, const char *why)
{
    mp_server_log_record_t record;

    if (!mp_server_log_wants(MP_SERVER_LOG_SESSION)) {
        return;
    }
    begin(&record, (uint8_t)MP_SERVER_LOG_KIND_LEAVE, slot);
    put_text(&record, name);
    record.a = (uint8_t)(why != NULL && strcmp(why, MP_SERVER_LEAVE_SENT_AWAY) == 0);
    mp_server_log_put(MP_SERVER_LOG_SESSION, &record);
}

void mp_server_log_position(uint8_t slot, const float pos[3])
{
    mp_server_log_record_t record;

    if (!mp_server_log_wants(MP_SERVER_LOG_POSITION)) {
        return;   /* the whole point: position logging off costs one load and one branch */
    }
    begin(&record, (uint8_t)MP_SERVER_LOG_KIND_POSITION, slot);
    if (pos != NULL) {
        record.pos[0] = pos[0];
        record.pos[1] = pos[1];
        record.pos[2] = pos[2];
    }
    mp_server_log_put(MP_SERVER_LOG_POSITION, &record);
}

void mp_server_log_ping(uint8_t slot, const char *name, uint32_t rtt_ms)
{
    mp_server_log_record_t record;

    if (!mp_server_log_wants(MP_SERVER_LOG_PING)) {
        return;
    }
    begin(&record, (uint8_t)MP_SERVER_LOG_KIND_PING, slot);
    put_text(&record, name);
    record.n = (int32_t)rtt_ms;
    mp_server_log_put(MP_SERVER_LOG_PING, &record);
}

void mp_server_log_death(uint8_t victim, uint8_t killer, uint8_t reason)
{
    mp_server_log_record_t record;

    if (!mp_server_log_wants(MP_SERVER_LOG_MATCH)) {
        return;
    }
    begin(&record, (uint8_t)MP_SERVER_LOG_KIND_DEATH, victim);
    record.a = killer;
    record.b = reason;
    mp_server_log_put(MP_SERVER_LOG_MATCH, &record);
}

void mp_server_log_pickup(uint8_t slot, uint32_t placement_index)
{
    mp_server_log_record_t record;

    if (!mp_server_log_wants(MP_SERVER_LOG_PICKUP)) {
        return;
    }
    begin(&record, (uint8_t)MP_SERVER_LOG_KIND_PICKUP, slot);
    record.n = (int32_t)placement_index;
    mp_server_log_put(MP_SERVER_LOG_PICKUP, &record);
}

void mp_server_log_team(uint8_t slot, const char *name, uint8_t team, bool asked_for_it)
{
    mp_server_log_record_t record;

    if (!mp_server_log_wants(MP_SERVER_LOG_MATCH)) {
        return;
    }
    begin(&record, (uint8_t)MP_SERVER_LOG_KIND_TEAM, slot);
    put_text(&record, name);
    record.a = team;
    record.b = (uint8_t)(asked_for_it ? 1u : 0u);
    mp_server_log_put(MP_SERVER_LOG_MATCH, &record);
}

void mp_server_log_round(uint8_t outcome, uint8_t winner, uint32_t generation)
{
    mp_server_log_record_t record;

    if (!mp_server_log_wants(MP_SERVER_LOG_MATCH)) {
        return;
    }
    begin(&record, (uint8_t)MP_SERVER_LOG_KIND_ROUND, 0u);
    record.a = outcome;
    record.b = winner;
    record.n = (int32_t)generation;
    mp_server_log_put(MP_SERVER_LOG_MATCH, &record);
}

/* ==============================================================================================
 * The consumer side, which may take as long as it likes.
 * ============================================================================================ */

static const char *reason_name(uint8_t reason)
{
    switch (reason) {
    case 0u:  return "hit";
    case 1u:  return "suicide";
    case 2u:  return "fall";
    case 3u:  return "the level";
    default:  return "unknown";
    }
}

static const char *outcome_name(uint8_t outcome)
{
    switch (outcome) {
    case 1u:  return "won by player";
    case 2u:  return "won by team";
    case 3u:  return "drawn";
    default:  return "running";
    }
}

static void format(const mp_server_log_record_t *r, char *out, size_t capacity)
{
    unsigned hours   = (r->ms / 3600000u) % 24u;
    unsigned minutes = (r->ms / 60000u) % 60u;
    unsigned seconds = (r->ms / 1000u) % 60u;
    unsigned millis  = r->ms % 1000u;
    char     stamp[20];

    text_format(stamp, sizeof stamp, "%02u:%02u:%02u.%03u", hours, minutes, seconds, millis);

    switch (r->kind) {
    case MP_SERVER_LOG_KIND_JOIN:
        text_format(out, capacity, "%s  join     slot %u  %s", stamp, (unsigned)r->slot, r->text);
        break;
    case MP_SERVER_LOG_KIND_LEAVE:
        text_format(out, capacity, "%s  leave    slot %u  %s (%s)", stamp, (unsigned)r->slot,
                    r->text, r->a != 0u ? MP_SERVER_LEAVE_SENT_AWAY : MP_SERVER_LEAVE_GONE);
        break;
    case MP_SERVER_LOG_KIND_DENY:
        text_format(out, capacity, "%s  refused  %s", stamp, r->text);
        break;
    case MP_SERVER_LOG_KIND_POSITION:
        text_format(out, capacity, "%s  pos      slot %u  %.1f %.1f %.1f", stamp,
                    (unsigned)r->slot, (double)r->pos[0], (double)r->pos[1], (double)r->pos[2]);
        break;
    case MP_SERVER_LOG_KIND_PING:
        text_format(out, capacity, "%s  ping     slot %u  %s  %d ms", stamp, (unsigned)r->slot,
                    r->text, (int)r->n);
        break;
    case MP_SERVER_LOG_KIND_DEATH:
        if (r->a == 0xFFu) {
            text_format(out, capacity, "%s  death    slot %u  by %s", stamp, (unsigned)r->slot,
                        reason_name(r->b));
        } else {
            text_format(out, capacity, "%s  kill     slot %u killed slot %u  by %s", stamp,
                        (unsigned)r->a, (unsigned)r->slot, reason_name(r->b));
        }
        break;
    case MP_SERVER_LOG_KIND_PICKUP:
        text_format(out, capacity, "%s  pickup   slot %u  placement %d", stamp, (unsigned)r->slot,
                    (int)r->n);
        break;
    case MP_SERVER_LOG_KIND_TEAM:
        text_format(out, capacity, "%s  team     slot %u  %s  -> team %u (%s)", stamp,
                    (unsigned)r->slot, r->text, (unsigned)r->a,
                    r->b != 0u ? "asked for it" : "placed");
        break;
    case MP_SERVER_LOG_KIND_ROUND:
        text_format(out, capacity, "%s  round    %s, winner %u, generation %d", stamp,
                    outcome_name(r->a), (unsigned)r->b, (int)r->n);
        break;
    case MP_SERVER_LOG_KIND_TEXT:
    default:
        text_format(out, capacity, "%s  %s", stamp, r->text);
        break;
    }
}

/* Drains what is in the ring right now and returns how many lines it produced. It reads `head`
 * once per pass rather than per record, so a burst arriving during the pass is left for the next
 * one instead of keeping this thread inside the loop indefinitely. */
static size_t drain_once(void)
{
    LONG   head = lg.head;
    size_t made = 0;

    /* The records this index promises are complete: the producer published it after storing
     * them, and this barrier is the other half of that pair. */
    MemoryBarrier();
    while (lg.tail != head) {
        mp_server_log_record_t record = lg.ring[(size_t)lg.tail & lg.mask];
        char                   line[192];   /* the stamp and a prefix, plus the whole text field */

        format(&record, line, sizeof line);
        if (lg.file != NULL) {
            fputs(line, lg.file);
            fputc('\n', lg.file);
        }
        if (lg.listener != NULL) {
            lg.listener(line, lg.listener_context);
        }
        InterlockedIncrement(&lg.written);
        ++made;
        /* The slot is released only after the record has been used. */
        MemoryBarrier();
        lg.tail = lg.tail + 1;
    }
    return made;
}

static DWORD WINAPI writer_thread(LPVOID unused)
{
    LONG last_dropped = 0;

    (void)unused;
    while (!lg.stop) {
        if (drain_once() == 0u) {
            Sleep(WRITER_IDLE_MS);
        } else if (lg.file != NULL) {
            fflush(lg.file);
        }
        /* A reader is told what they are missing rather than quietly given less. The count is
         * reported by this thread so that noticing a drop costs the server thread nothing. */
        if (lg.dropped != last_dropped) {
            char line[96];

            text_format(line, sizeof line, "  [log] %ld record(s) dropped: the ring was full",
                        (long)(lg.dropped - last_dropped));
            last_dropped = lg.dropped;
            if (lg.file != NULL) {
                fputs(line, lg.file);
                fputc('\n', lg.file);
            }
            if (lg.listener != NULL) {
                lg.listener(line, lg.listener_context);
            }
        }
    }
    (void)drain_once();   /* whatever was still in it when the stop was asked for */
    if (lg.file != NULL) {
        fflush(lg.file);
    }
    return 0;
}

/* ==============================================================================================
 * Opening and closing.
 * ============================================================================================ */

static size_t round_up_to_power_of_two(size_t value)
{
    size_t result = 1u;

    while (result < value && result < 0x400000u) {
        result <<= 1;
    }
    return result;
}

bool mp_server_log_open(const char *path, uint32_t categories, size_t ring_records)
{
    size_t capacity;

    mp_server_log_close();
    memset(&lg, 0, sizeof lg);

    capacity = round_up_to_power_of_two(ring_records != 0u ? ring_records : RING_DEFAULT);
    lg.ring  = (mp_server_log_record_t *)calloc(capacity, sizeof *lg.ring);
    if (lg.ring == NULL) {
        return false;
    }
    lg.mask       = capacity - 1u;
    lg.categories = (LONG)categories;

    if (path != NULL && path[0] != '\0') {
        lg.file = fopen(path, "a");
        if (lg.file == NULL) {
            free(lg.ring);
            lg.ring = NULL;
            return false;
        }
    }
    lg.writer = CreateThread(NULL, 0, &writer_thread, NULL, 0, NULL);
    if (lg.writer == NULL) {
        if (lg.file != NULL) {
            fclose(lg.file);
            lg.file = NULL;
        }
        free(lg.ring);
        lg.ring = NULL;
        return false;   /* nothing is logged rather than logged on the server's own thread */
    }
    return true;
}

void mp_server_log_close(void)
{
    if (lg.writer != NULL) {
        InterlockedExchange(&lg.stop, 1);
        WaitForSingleObject(lg.writer, 2000);
        CloseHandle(lg.writer);
        lg.writer = NULL;
    }
    if (lg.file != NULL) {
        fclose(lg.file);
        lg.file = NULL;
    }
    if (lg.ring != NULL) {
        free(lg.ring);
        lg.ring = NULL;
    }
}

void mp_server_log_set_categories(uint32_t categories)
{
    InterlockedExchange(&lg.categories, (LONG)(categories & MP_SERVER_LOG_ALL));
}

void mp_server_log_set_line_listener(mp_server_log_line_fn_t listener, void *context)
{
    lg.listener_context = context;
    lg.listener         = listener;
}

void mp_server_log_counters(mp_server_log_counters_t *out)
{
    if (out == NULL) {
        return;
    }
    out->offered  = (uint32_t)lg.offered;
    out->written  = (uint32_t)lg.written;
    out->dropped  = (uint32_t)lg.dropped;
    out->filtered = (uint32_t)lg.filtered;
}
