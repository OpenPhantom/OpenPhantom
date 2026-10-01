/* mp_level_state_count.c: every entry into a level switch arm, counted. See the header. */
#include "mp_level_state_count.h"

#include "mp_enemy_bind.h"
#include "mp_enemy_sync.h"
#include "mp_enemy_wire.h"

#include "common/logging.h"
#include "common/memory.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The two words of an actor a line names: the script it runs and that script's state. */
#define A_SCRIPT  0x00u
#define A_AI_MODE 0x7Cu

/* The calls written out one by one, per side of the question. */
#define CALLS_WRITTEN 8u

/* The placements listed in the report. */
#define PLACEMENTS_LISTED 16u

typedef struct kind_count {
    uint32_t entered;
    uint32_t turned_on;
    uint32_t turned_off;
    uint32_t in_session;   /* a client of a started session */
    uint32_t withheld;
    uint32_t fail_open;
    uint32_t unparked;     /* of those in a session, from an actor this machine had not parked */
    uint32_t without;
    uint32_t unreadable;   /* the actor's key did not read */
} kind_count_t;

typedef struct key_count {
    uint32_t entered;
    uint32_t in_session;
    uint32_t unparked;
} key_count_t;

typedef struct count_state {
    kind_count_t kind[MP_LEVEL_KIND_COUNT];
    key_count_t  key[MP_LEVEL_KIND_COUNT][MP_ENEMY_SYNC_KEYS];
    uint32_t     written_in_session;
    uint32_t     written_without;
} count_state_t;

static count_state_t count;

static const char *const KIND_NAMES[MP_LEVEL_KIND_COUNT] = {
    "emitter placements", "level lights", "sound placements"
};

static const char *verdict_said(mp_level_verdict_t verdict)
{
    switch (verdict) {
    case MP_LEVEL_VERDICT_WITHHELD:
        return "withheld";
    case MP_LEVEL_VERDICT_FAIL_OPEN:
        return "let through because this side cannot match the host";
    case MP_LEVEL_VERDICT_WITHOUT:
    default:
        return "let through";
    }
}

/* What the host's table says of this actor's key right now. */
static const char *table_said(uint32_t key, uintptr_t actor)
{
    mp_enemy_record_t mirror;

    if (!mp_enemy_sync_mirror(key, &mirror)) {
        return "does not name its key";
    }
    return mp_enemy_sync_replica_for(key) == actor ? "names this actor"
                                                   : "names another actor under its key";
}

static void write_call(mp_level_kind_t kind, uintptr_t actor, int32_t mode, bool key_read,
                       uint32_t key, bool in_session, mp_level_verdict_t verdict, bool parked,
                       bool host)
{
    int32_t script  = -1;
    int32_t ai_mode = -1;

    if (!key_read) {
        log_info("the level's own switches: %s %s by an actor that did not read, %s and %s",
                 KIND_NAMES[kind], mode != 0 ? "on" : "off",
                 in_session ? "in a started session as a client"
                            : (host ? "as the host of a started session" : "without one"),
                 verdict_said(verdict));
        return;
    }
    (void)memory_try_read(actor + A_SCRIPT, &script, sizeof script);
    (void)memory_try_read(actor + A_AI_MODE, &ai_mode, sizeof ai_mode);
    /* The host's table, the block and the let-go are a client's questions: the host writes the
     * table, and on a side with no session there is none. */
    if (!in_session) {
        log_info("the level's own switches: %s %s by placement %u (script %d, state %d), %s and "
                 "%s, %s; %s", KIND_NAMES[kind], mode != 0 ? "on" : "off", (unsigned)key,
                 (int)script, (int)ai_mode,
                 host ? "as the host of a started session" : "without one",
                 verdict_said(verdict), parked ? "parked" : "not parked",
                 host ? "the host writes the table" : "there is no table without a session");
        return;
    }
    log_info("the level's own switches: %s %s by placement %u (script %d, state %d), %s and %s, "
             "%s; the host's table %s; %s block of this level applied here; the actor was %s",
             KIND_NAMES[kind], mode != 0 ? "on" : "off", (unsigned)key, (int)script,
             (int)ai_mode, "in a started session as a client", verdict_said(verdict),
             parked ? "parked" : "not parked", table_said(key, actor),
             mp_enemy_sync_block_applied() ? "a" : "no",
             mp_enemy_sync_let_go_before(actor) ? "let go before" : "never let go");
}

void mp_level_state_count_switch(mp_level_kind_t kind, uintptr_t actor, int32_t mode,
                                 mp_level_verdict_t verdict, bool host)
{
    kind_count_t *row;
    uint32_t      key        = 0;
    bool          key_read;
    bool          in_session = verdict != MP_LEVEL_VERDICT_WITHOUT;
    bool          parked     = false;

    if ((size_t)kind >= (size_t)MP_LEVEL_KIND_COUNT) {
        return;
    }
    row = &count.kind[kind];
    ++row->entered;
    if (mode != 0) {
        ++row->turned_on;
    } else {
        ++row->turned_off;
    }
    key_read = actor != 0u && mp_enemy_bind_index(actor, &key) && key < MP_ENEMY_SYNC_KEYS;
    if (!key_read) {
        ++row->unreadable;
    } else {
        parked = mp_enemy_bind_is_parked(actor);
        ++count.key[kind][key].entered;
    }
    if (in_session) {
        ++row->in_session;
        row->withheld += verdict == MP_LEVEL_VERDICT_WITHHELD ? 1u : 0u;
        row->fail_open += verdict == MP_LEVEL_VERDICT_FAIL_OPEN ? 1u : 0u;
        if (key_read) {
            ++count.key[kind][key].in_session;
            if (!parked) {
                ++row->unparked;
                ++count.key[kind][key].unparked;
            }
        }
    } else {
        ++row->without;
    }
    if (in_session ? count.written_in_session < CALLS_WRITTEN
                   : count.written_without < CALLS_WRITTEN) {
        if (in_session) {
            ++count.written_in_session;
        } else {
            ++count.written_without;
        }
        write_call(kind, actor, mode, key_read, key, in_session, verdict, parked, host);
    }
}

void mp_level_state_count_report(bool host)
{
    size_t   kind;
    size_t   key;
    uint32_t listed = 0;
    uint32_t more   = 0;

    for (kind = 0; kind < (size_t)MP_LEVEL_KIND_COUNT; ++kind) {
        const kind_count_t *row = &count.kind[kind];

        log_info("  the level's own switches, %s (%s): %u entered, %u on, %u off; in a started "
                 "session as a client %u (%u withheld here, %u let through because this side "
                 "cannot match the host, %u from an actor this machine had NOT parked); without "
                 "one %u; %u whose actor did not read",
                 KIND_NAMES[kind], host ? "host" : "client", (unsigned)row->entered,
                 (unsigned)row->turned_on, (unsigned)row->turned_off, (unsigned)row->in_session,
                 (unsigned)row->withheld, (unsigned)row->fail_open, (unsigned)row->unparked,
                 (unsigned)row->without, (unsigned)row->unreadable);
    }
    for (kind = 0; kind < (size_t)MP_LEVEL_KIND_COUNT; ++kind) {
        for (key = 0; key < (size_t)MP_ENEMY_SYNC_KEYS; ++key) {
            const key_count_t *k = &count.key[kind][key];

            if (k->entered == 0u) {
                continue;
            }
            if (listed >= PLACEMENTS_LISTED) {
                ++more;
                continue;
            }
            ++listed;
            log_info("  the level's own switches by placement, %s %u: %u entered, %u in a started "
                     "session as a client, %u of those from an actor this machine had NOT parked",
                     KIND_NAMES[kind], (unsigned)key, (unsigned)k->entered,
                     (unsigned)k->in_session, (unsigned)k->unparked);
        }
    }
    if (more != 0u) {
        log_info("  the level's own switches by placement: %u more placement(s) with a switch, "
                 "not listed", (unsigned)more);
    }
}
