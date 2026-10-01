/* mp_enemy_dead_watch_client.c: the dead that stood on a client, measured. See the second half of
 * mp_enemy_dead_watch.h.
 *
 * The host's half and this one share the rule and nothing else. The host reads its census, a
 * sample for every live key every substep; a client reads what its flush writes, a sample for a
 * key only when a record of it arrived. So a pass of the flush is recognised the way the host
 * recognises a census, by the keys going upward and the world clock, and a life whose key the
 * table no longer knows is closed at the next pass, where the host closes one its census skipped.
 */
#include "mp_enemy_dead_watch.h"

#include "mp_cells.h"
#include "mp_enemy_body_rule.h"
#include "mp_enemy_dead_watch_rule.h"
#include "mp_enemy_sync.h"
#include "mp_enemy_wire.h"
#include "mp_wire.h"

#include "common/logging.h"
#include "common/memory.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The replica's body: bit 0 of its flag word is what the renderer draws by, a class of 0 is a
 * body that blocks nothing. The actor holds the body at +0x34. */
#define ACTOR_BODY       0x34u
#define BODY_FLAGS       0x00u
#define BODY_CLASS       0x04u
#define BODY_FLAG_DRAWN  0x01u

/* The world clock inside the level record, the clock the engine's own timers compare against. */
#define LEVEL_WORLD_CLOCK 0x54u

/* How many runs past the limit are named, between two reports. */
#define NAMED_LIMIT 8u

/* The ways a run ends, indexed by mp_dead_watch_end_t. */
#define ENDS 6u

typedef struct replica_name {
    uint16_t key;
    uint8_t  generation;
    uint8_t  state;          /* what the host said when the run passed the limit */
    uint8_t  clip;
    int32_t  health;
    bool     host_has;
    bool     host_drawn;
    bool     host_solid;
    bool     closed;
    uint8_t  end;
    float    seconds;
    uint32_t censuses;
} replica_name_t;

typedef struct client_watch {
    mp_dead_watch_life_t life[MP_WIRE_KEY_COUNT];

    uint32_t resets;        /* the enemy table's reset the lives belong to */
    bool     begun;
    uint32_t census;        /* the flush pass being read, counted here */
    uint32_t last_key;
    bool     clock_known;
    float    clock;

    uint32_t records;
    uint32_t passes;
    uint32_t no_clock;
    uint32_t unjudged;      /* records whose health or whose replica's body did not read */
    uint32_t fell;
    uint32_t over;
    uint32_t over_host_down;     /* the host's body no longer drawn or solid: must be 0 */
    uint32_t over_host_up;       /* the host's stood as well */
    uint32_t over_host_unknown;  /* the host read no body */
    uint32_t ended[MP_DEAD_WATCH_BUCKETS];
    uint32_t ends[ENDS];
    uint32_t standing_at_reset;
    float    longest;

    replica_name_t named[NAMED_LIMIT];
    uint32_t       named_count;
} client_watch_t;

static client_watch_t watch;

static const char *key_kind(uint32_t key)
{
    return mp_wire_key_is_copy(key) ? "copy" : "placement";
}

static uint32_t key_index(uint32_t key)
{
    return mp_wire_key_is_copy(key) ? key - MP_WIRE_KEY_COPY_BASE : key;
}

static bool read_clock(float *now)
{
    uintptr_t cell  = mp_cells_address(MP_CELL_LEVEL);
    uint32_t  level = 0;

    return cell != 0u && memory_try_read(cell, &level, sizeof level) && level != 0u &&
           memory_try_read((uintptr_t)level + LEVEL_WORLD_CLOCK, now, sizeof *now);
}

static void close_name(uint32_t key, const mp_dead_watch_life_t *life)
{
    uint32_t i;

    for (i = 0; i < watch.named_count; ++i) {
        replica_name_t *n = &watch.named[i];

        if (n->key == key && n->generation == life->generation && !n->closed) {
            n->closed   = true;
            n->end      = life->end;
            n->seconds  = mp_dead_watch_stood_seconds(life);
            n->censuses = mp_dead_watch_stood_censuses(life);
        }
    }
}

static void count_closed(uint32_t key, const mp_dead_watch_life_t *life)
{
    ++watch.ended[mp_dead_watch_bucket(mp_dead_watch_stood_seconds(life))];
    ++watch.ends[life->end < ENDS ? life->end : 0u];
    close_name(key, life);
}

/* A reset of the enemy table ends every life it held; one still standing then is counted as
 * standing at the level's end, which is where a reset comes. */
static void forget_an_old_table(void)
{
    uint32_t resets = mp_enemy_sync_resets();
    uint32_t key;

    if (resets == watch.resets) {
        return;
    }
    for (key = 0; key < MP_WIRE_KEY_COUNT; ++key) {
        if (watch.life[key].standing) {
            ++watch.standing_at_reset;
            close_name(key, &watch.life[key]);
        }
    }
    memset(watch.life, 0, sizeof watch.life);
    watch.resets = resets;
    watch.begun  = false;
}

/* A standing life whose key the table has stopped holding went with a removal or with the host
 * no longer listing it. */
static void sweep(void)
{
    uint32_t key;

    for (key = 0; key < MP_WIRE_KEY_COUNT; ++key) {
        mp_dead_watch_life_t *life = &watch.life[key];
        mp_enemy_record_t     mirror;

        if (life->standing && !mp_enemy_sync_mirror(key, &mirror) &&
            mp_dead_watch_gone(life) != 0u) {
            count_closed(key, life);
        }
    }
}

static void note_pass(uint32_t key)
{
    float now   = 0.0f;
    bool  clock = read_clock(&now);
    bool  fresh = !watch.begun || key <= watch.last_key ||
                  (clock && (!watch.clock_known || now != watch.clock));

    watch.last_key = key;
    if (!fresh) {
        return;
    }
    watch.begun = true;
    ++watch.census;
    ++watch.passes;
    if (clock) {
        watch.clock       = now;
        watch.clock_known = true;
    } else {
        ++watch.no_clock;
    }
    sweep();
}

static bool read_replica(uint32_t actor, mp_dead_watch_sample_t *sample)
{
    uint32_t body  = 0;
    uint32_t flags = 0;
    int32_t  cls   = 0;

    if (!memory_try_read((uintptr_t)actor + ACTOR_BODY, &body, sizeof body) || body == 0u ||
        !memory_try_read((uintptr_t)body + BODY_FLAGS, &flags, sizeof flags) ||
        !memory_try_read((uintptr_t)body + BODY_CLASS, &cls, sizeof cls)) {
        return false;
    }
    sample->drawn = (flags & BODY_FLAG_DRAWN) != 0u;
    sample->solid = cls != 0;
    return true;
}

static void name_run(uint32_t key, const mp_dead_watch_life_t *life,
                     const mp_dead_watch_sample_t *sample, const mp_enemy_body_state_t *host)
{
    replica_name_t *n;

    if (watch.named_count >= NAMED_LIMIT) {
        return;
    }
    n = &watch.named[watch.named_count++];
    memset(n, 0, sizeof *n);
    n->key        = (uint16_t)key;
    n->generation = life->generation;
    n->state      = sample->state;
    n->clip       = sample->clip;
    n->health     = sample->health;
    n->host_has   = host->has;
    n->host_drawn = host->drawn;
    n->host_solid = host->solid;
}

static void count_events(uint32_t key, const mp_dead_watch_life_t *life,
                         const mp_dead_watch_sample_t *sample, const mp_enemy_body_state_t *host,
                         uint32_t events)
{
    if ((events & MP_DEAD_WATCH_FELL) != 0u) {
        ++watch.fell;
    }
    if ((events & MP_DEAD_WATCH_PASSED_LIMIT) != 0u) {
        ++watch.over;
        if (!host->has) {
            ++watch.over_host_unknown;
        } else if (host->drawn && host->solid) {
            ++watch.over_host_up;
        } else {
            ++watch.over_host_down;
        }
        name_run(key, life, sample, host);
    }
    if ((events & MP_DEAD_WATCH_CLOSED) != 0u) {
        count_closed(key, life);
    }
    if (life->fell && mp_dead_watch_stood_seconds(life) > watch.longest) {
        watch.longest = mp_dead_watch_stood_seconds(life);
    }
}

void mp_enemy_dead_watch_replica(uint32_t key, uint8_t generation, uint32_t actor,
                                 const mp_enemy_record_t *record)
{
    mp_dead_watch_life_t  *life;
    mp_dead_watch_sample_t sample;
    mp_enemy_body_state_t  host;

    if (record == NULL || key >= MP_WIRE_KEY_COUNT) {
        return;
    }
    forget_an_old_table();
    note_pass(key);
    ++watch.records;

    life = &watch.life[key];
    if (!life->known || life->generation != generation) {
        if (life->known && mp_dead_watch_gone(life) != 0u) {
            count_closed(key, life);   /* the key has another life: the last one went */
        }
        mp_dead_watch_begin(life, generation);
    }
    if (actor == 0u) {
        if (mp_dead_watch_gone(life) != 0u) {
            count_closed(key, life);
        }
        return;
    }
    memset(&sample, 0, sizeof sample);
    sample.now     = watch.clock;
    sample.census  = watch.census;
    sample.health  = mp_enemy_wire_health(record);
    sample.state   = (uint8_t)(record->value[MP_ENEMY_F_STATE] & MP_ENEMY_STATE_MASK);
    sample.clip    = (uint8_t)(record->value[MP_ENEMY_F_CLIP] & 0xFFu);
    sample.by_body = true;
    if (!mp_enemy_wire_health_known(record) ||
        (mp_dead_watch_needs_body(life, sample.health) && !read_replica(actor, &sample))) {
        ++watch.unjudged;
        life->last_census = watch.census;
        return;
    }
    mp_enemy_body_unpack(record->value[MP_ENEMY_F_BODY], &host);
    count_events(key, life, &sample, &host, mp_dead_watch_step(life, &sample));
}

static const char *end_words(uint8_t end)
{
    switch (end) {
    case MP_DEAD_WATCH_HIDDEN:  return "it ended no longer drawn here";
    case MP_DEAD_WATCH_UNSOLID: return "it ended no longer solid here";
    case MP_DEAD_WATCH_REVIVED: return "it ended with its health above zero again";
    case MP_DEAD_WATCH_REMOVED: return "it ended gone from this side's table";
    default:                    return "it still stood at the report or the level's end";
    }
}

static const char *host_body_words(const replica_name_t *n)
{
    if (!n->host_has) {
        return "not read on the host";
    }
    if (n->host_drawn && n->host_solid) {
        return "drawn and solid on the host too";
    }
    return n->host_drawn ? "no longer solid on the host" : "no longer drawn on the host";
}

static void report_names(void)
{
    uint32_t i;

    /* A name not closed is a run still open on its key: a run is closed before its key takes
     * another life and when the table is reset. */
    for (i = 0; i < watch.named_count; ++i) {
        const replica_name_t       *n    = &watch.named[i];
        const mp_dead_watch_life_t *life = &watch.life[n->key];
        float    seconds  = n->closed ? n->seconds : mp_dead_watch_stood_seconds(life);
        uint32_t censuses = n->closed ? n->censuses : mp_dead_watch_stood_censuses(life);

        log_info("%s %u (life %u) stood dead %u substeps here (%.1f s of world time): the host "
                 "said state %u, clip %u, health %d, its body %s; %s",
                 key_kind(n->key), (unsigned)key_index(n->key), (unsigned)n->generation,
                 (unsigned)censuses, (double)seconds, (unsigned)n->state, (unsigned)n->clip,
                 (int)n->health, host_body_words(n),
                 end_words(n->closed ? n->end : (uint8_t)MP_DEAD_WATCH_STILL_STANDING));
    }
    watch.named_count = 0u;
}

void mp_enemy_dead_watch_client_report(bool applied)
{
    uint32_t standing = 0;
    uint32_t key;

    forget_an_old_table();
    if (!applied && watch.records == 0u) {
        return;
    }
    sweep();
    for (key = 0; key < MP_WIRE_KEY_COUNT; ++key) {
        standing += watch.life[key].standing ? 1u : 0u;
    }
    log_info("the dead that stood (client): %u life/lives the host reported falling from above "
             "zero to zero or below; %u of them stayed drawn and solid here for more than 12 s of "
             "world time (%u while the host's body was no longer drawn or solid, must be 0; %u "
             "while the host's stood too; %u with no body from the host), the longest %.1f s; "
             "ended within 2 s %u, 5 s %u, 12 s %u, later %u: %u no longer solid here, %u no "
             "longer drawn, %u alive again, %u gone, and %u still standing at the report or the "
             "level's end; %u record(s) read in %u pass(es), %u pass(es) with no world clock, %u "
             "record(s) not judged for a health or a body that did not read",
             (unsigned)watch.fell, (unsigned)watch.over, (unsigned)watch.over_host_down,
             (unsigned)watch.over_host_up, (unsigned)watch.over_host_unknown,
             (double)watch.longest, (unsigned)watch.ended[0], (unsigned)watch.ended[1],
             (unsigned)watch.ended[2], (unsigned)watch.ended[3],
             (unsigned)watch.ends[MP_DEAD_WATCH_UNSOLID],
             (unsigned)watch.ends[MP_DEAD_WATCH_HIDDEN],
             (unsigned)watch.ends[MP_DEAD_WATCH_REVIVED],
             (unsigned)watch.ends[MP_DEAD_WATCH_REMOVED],
             (unsigned)(standing + watch.standing_at_reset), (unsigned)watch.records,
             (unsigned)watch.passes, (unsigned)watch.no_clock, (unsigned)watch.unjudged);
    report_names();
}
