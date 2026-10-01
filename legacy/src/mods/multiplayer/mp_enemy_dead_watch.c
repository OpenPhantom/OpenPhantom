/* mp_enemy_dead_watch.c: the dead that stood on the host, measured. See the header. */
#include "mp_enemy_dead_watch.h"

#include "mp_cells.h"
#include "mp_enemy_dead_watch_rule.h"
#include "mp_enemy_sync.h"
#include "mp_wire.h"

#include "common/logging.h"
#include "common/memory.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The cells read of a life that has fallen: the actor's flags, body, script mode and death latch,
 * and the body's flags and class. Bit 0 of the body's flags is what the renderer draws by, a class
 * of 0 is what a body that blocks nothing has, and 0x10000 in the actor's flags is the script
 * owning its death. */
#define ACTOR_STATE_FLAGS        0x14u
#define ACTOR_BODY               0x34u
#define ACTOR_SCRIPT_MODE        0x7Cu
#define ACTOR_DEATH_LATCH        0x1FCu
#define BODY_FLAGS               0x00u
#define BODY_CLASS               0x04u
#define BODY_FLAG_DRAWN          0x01u
#define STATE_FLAG_SCRIPT_DEATH  0x10000u

/* The world clock inside the level record: seconds since the level began, the clock the engine's
 * own timers compare against. */
#define LEVEL_WORLD_CLOCK        0x54u

/* How many of each kind are named in the report, for each level. */
#define NAMED_LIMIT 8u

typedef struct stood_name {
    uint16_t key;
    uint8_t  generation;
    uint8_t  state;
    uint8_t  state_before;
    uint8_t  clip;
    bool     script_death;
    bool     guard_fired;
    int32_t  health;
    int32_t  script_mode;
    int32_t  body_class;
    bool     closed;         /* filled when the run closes */
    uint8_t  end;
    uint8_t  end_state;
    float    seconds;
    uint32_t censuses;
} stood_name_t;

typedef struct moved_name {
    uint16_t key;
    uint8_t  generation;
    uint8_t  from;
    uint8_t  to;
    float    after;
} moved_name_t;

typedef struct dead_watch_state {
    mp_dead_watch_life_t life[MP_WIRE_KEY_COUNT];

    uint32_t resets;        /* the enemy table's reset the lives belong to */
    bool     begun;         /* a row has been read since the lives were last forgotten */
    uint32_t census;        /* the census being read, counted here */
    uint32_t last_key;      /* the last key read: a census reads them upward */
    bool     clock_known;
    float    clock;         /* the world clock of the census being read */

    uint32_t rows;
    uint32_t censuses;
    uint32_t no_clock;      /* censuses that read no world clock */
    uint32_t unjudged;      /* rows whose health or whose fallen body did not read */
    uint32_t fell;
    uint32_t over;
    uint32_t over_phase[MP_DEAD_WATCH_PHASES];
    uint32_t ended[MP_DEAD_WATCH_BUCKETS];
    float    longest;
    uint32_t corpses_held;
    uint32_t corpses_moved;

    stood_name_t stood[NAMED_LIMIT];
    uint32_t     stood_count;
    moved_name_t moved[NAMED_LIMIT];
    uint32_t     moved_count;
} dead_watch_state_t;

static dead_watch_state_t watch;

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

/* The table's reset is the one way out of a level or a session, and every life and name it held
 * belongs to the table before it. Noticed at the first row of the new table and nowhere else. */
static void forget_an_old_table(void)
{
    uint32_t resets = mp_enemy_sync_resets();

    if (resets == watch.resets) {
        return;
    }
    memset(watch.life, 0, sizeof watch.life);
    watch.resets      = resets;
    watch.begun       = false;
    watch.stood_count = 0u;
    watch.moved_count = 0u;
}

static void close_name(uint32_t key, const mp_dead_watch_life_t *life)
{
    uint32_t i;

    for (i = 0; i < watch.stood_count; ++i) {
        stood_name_t *n = &watch.stood[i];

        if (n->key == key && n->generation == life->generation && !n->closed) {
            n->closed    = true;
            n->end       = life->end;
            n->end_state = life->state;
            n->seconds   = mp_dead_watch_stood_seconds(life);
            n->censuses  = mp_dead_watch_stood_censuses(life);
        }
    }
}

static void count_closed(uint32_t key, const mp_dead_watch_life_t *life)
{
    ++watch.ended[mp_dead_watch_bucket(mp_dead_watch_stood_seconds(life))];
    close_name(key, life);
}

/* A life not seen in the census before `census` has left it: the census reads every live key
 * every substep, so a key it skipped was removed, or its record did not read. */
static void sweep(uint32_t census)
{
    uint32_t key;

    for (key = 0; key < MP_WIRE_KEY_COUNT; ++key) {
        mp_dead_watch_life_t *life = &watch.life[key];

        if (life->standing && life->last_census + 1u < census &&
            mp_dead_watch_gone(life) != 0u) {
            count_closed(key, life);
        }
    }
}

/* A census reads its keys upward, so a key at or below the last one begins the next; so does a
 * world clock that moved, for a census whose keys all happen to lie above the last one's. */
static void note_census(uint32_t key)
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
    ++watch.censuses;
    if (clock) {
        watch.clock       = now;
        watch.clock_known = true;
    } else {
        ++watch.no_clock;   /* the lives keep the last clock read, and so stand for no time */
    }
    sweep(watch.census);
}

static bool read_body(uint32_t actor, mp_dead_watch_sample_t *sample)
{
    uint32_t body = 0;
    uint32_t flags = 0;
    int32_t  body_class = 0;
    uint32_t state_flags = 0;
    int32_t  latch = 0;

    if (!memory_try_read((uintptr_t)actor + ACTOR_BODY, &body, sizeof body) || body == 0u ||
        !memory_try_read((uintptr_t)body + BODY_FLAGS, &flags, sizeof flags) ||
        !memory_try_read((uintptr_t)body + BODY_CLASS, &body_class, sizeof body_class) ||
        !memory_try_read((uintptr_t)actor + ACTOR_STATE_FLAGS, &state_flags, sizeof state_flags) ||
        !memory_try_read((uintptr_t)actor + ACTOR_DEATH_LATCH, &latch, sizeof latch)) {
        return false;
    }
    sample->drawn        = (flags & BODY_FLAG_DRAWN) != 0u;
    sample->solid        = body_class != 0;
    sample->script_death = (state_flags & STATE_FLAG_SCRIPT_DEATH) != 0u;
    sample->guard_fired  = latch != 0;
    return true;
}

/* The first eight of a level that pass the limit, as they stood when they passed it. */
static void name_stood(uint32_t key, const mp_dead_watch_life_t *life,
                       const mp_dead_watch_sample_t *sample, uint32_t actor)
{
    stood_name_t *n;
    uint32_t      body = 0;

    if (watch.stood_count >= NAMED_LIMIT) {
        return;
    }
    n = &watch.stood[watch.stood_count++];
    memset(n, 0, sizeof *n);
    n->key          = (uint16_t)key;
    n->generation   = life->generation;
    n->state        = life->state;
    n->state_before = life->state_before;
    n->clip         = sample->clip;
    n->health       = sample->health;
    n->script_death = sample->script_death;
    n->guard_fired  = sample->guard_fired;
    n->script_mode  = -1;
    n->body_class   = -1;
    (void)memory_try_read((uintptr_t)actor + ACTOR_SCRIPT_MODE, &n->script_mode,
                          sizeof n->script_mode);
    if (memory_try_read((uintptr_t)actor + ACTOR_BODY, &body, sizeof body) && body != 0u) {
        (void)memory_try_read((uintptr_t)body + BODY_CLASS, &n->body_class,
                              sizeof n->body_class);
    }
}

static void name_moved(uint32_t key, const mp_dead_watch_life_t *life)
{
    moved_name_t *n;

    if (watch.moved_count >= NAMED_LIMIT) {
        return;
    }
    n = &watch.moved[watch.moved_count++];
    n->key        = (uint16_t)key;
    n->generation = life->generation;
    n->from       = life->corpse_clip;
    n->to         = life->corpse_moved_to;
    n->after      = life->corpse_moved_at - life->corpse_at;
}

static void count_events(uint32_t key, const mp_dead_watch_life_t *life,
                         const mp_dead_watch_sample_t *sample, uint32_t actor, uint32_t events)
{
    if ((events & MP_DEAD_WATCH_FELL) != 0u) {
        ++watch.fell;
    }
    if ((events & MP_DEAD_WATCH_PASSED_LIMIT) != 0u) {
        ++watch.over;
        ++watch.over_phase[mp_dead_watch_phase(life->state, sample->script_death)];
        name_stood(key, life, sample, actor);
    }
    if ((events & MP_DEAD_WATCH_CLOSED) != 0u) {
        count_closed(key, life);
    }
    if ((events & MP_DEAD_WATCH_CORPSE_HELD) != 0u) {
        ++watch.corpses_held;
    }
    if ((events & MP_DEAD_WATCH_CORPSE_MOVED) != 0u) {
        ++watch.corpses_moved;
        name_moved(key, life);
    }
    if (life->fell && mp_dead_watch_stood_seconds(life) > watch.longest) {
        watch.longest = mp_dead_watch_stood_seconds(life);
    }
}

void mp_enemy_dead_watch_row(uint32_t key, uint8_t generation, uint32_t actor,
                             const mp_enemy_record_t *record)
{
    mp_dead_watch_life_t  *life;
    mp_dead_watch_sample_t sample;

    if (record == NULL || actor == 0u || key >= MP_WIRE_KEY_COUNT) {
        return;
    }
    forget_an_old_table();
    note_census(key);
    ++watch.rows;

    life = &watch.life[key];
    if (!life->known || life->generation != generation) {
        if (life->known && mp_dead_watch_gone(life) != 0u) {
            count_closed(key, life);     /* the key has another life: the last one was removed */
        }
        mp_dead_watch_begin(life, generation);
    }
    memset(&sample, 0, sizeof sample);
    sample.now    = watch.clock;
    sample.census = watch.census;
    sample.health = mp_enemy_wire_health(record);
    sample.state  = (uint8_t)(record->value[MP_ENEMY_F_STATE] & MP_ENEMY_STATE_MASK);
    sample.clip   = (uint8_t)(record->value[MP_ENEMY_F_CLIP] & 0xFFu);
    if (!mp_enemy_wire_health_known(record) ||
        (mp_dead_watch_needs_body(life, sample.health) && !read_body(actor, &sample))) {
        /* Judged on the next sample that reads. It was in this census all the same, and the
         * sweep must not take it for removed. */
        ++watch.unjudged;
        life->last_census = watch.census;
        return;
    }
    count_events(key, life, &sample, actor, mp_dead_watch_step(life, &sample));
}

static const char *end_words(uint8_t end)
{
    switch (end) {
    case MP_DEAD_WATCH_DEATH_STATE: return "it ended in a death state";
    case MP_DEAD_WATCH_HIDDEN:      return "it ended no longer drawn";
    case MP_DEAD_WATCH_UNSOLID:     return "it ended no longer solid";
    case MP_DEAD_WATCH_REVIVED:     return "it ended with its health above zero again";
    case MP_DEAD_WATCH_REMOVED:     return "it ended gone from the census";
    default:                        return "it still stood at the report";
    }
}

static void report_names(void)
{
    uint32_t i;

    /* A name not closed is a run still open on its key: a run is closed before its key takes
     * another life, and the names go with the lives when the table is reset. */
    for (i = 0; i < watch.stood_count; ++i) {
        const stood_name_t         *n = &watch.stood[i];
        const mp_dead_watch_life_t *life = &watch.life[n->key];
        float    seconds = n->closed ? n->seconds : mp_dead_watch_stood_seconds(life);
        uint32_t censuses = n->closed ? n->censuses : mp_dead_watch_stood_censuses(life);

        log_info("%s %u (life %u) stood dead %u substeps on the host (%.1f s of world time): "
                 "state %u after %u, clip %u, health %d, script mode %d, script owns the death "
                 "%s, guard fired %s, class %d; %s (state %u)",
                 key_kind(n->key), (unsigned)key_index(n->key), (unsigned)n->generation,
                 (unsigned)censuses, (double)seconds, (unsigned)n->state,
                 (unsigned)n->state_before, (unsigned)n->clip, (int)n->health,
                 (int)n->script_mode, n->script_death ? "yes" : "no",
                 n->guard_fired ? "yes" : "no", (int)n->body_class,
                 end_words(n->closed ? n->end : (uint8_t)MP_DEAD_WATCH_STILL_STANDING),
                 (unsigned)(n->closed ? n->end_state : life->state));
    }
    for (i = 0; i < watch.moved_count; ++i) {
        const moved_name_t *n = &watch.moved[i];

        log_info("the corpse of %s %u (life %u) changed clip from %u to %u %.1f s after it lay "
                 "down", key_kind(n->key), (unsigned)key_index(n->key), (unsigned)n->generation,
                 (unsigned)n->from, (unsigned)n->to, (double)n->after);
    }
}

/* The report does not forget an old table. At a level's end the head node takes message 6 and
 * resets the table before the tail node writes this, so forgetting here would drop the names and
 * the open runs of the very level the report is about. The first row of the next table forgets
 * them. */
void mp_enemy_dead_watch_report(bool described)
{
    uint32_t standing = 0;
    uint32_t key;

    if (!described && watch.rows == 0u) {
        return;
    }
    /* What the next census would find gone, found now, so the lines do not wait a substep. */
    sweep(watch.census + 1u);
    for (key = 0; key < MP_WIRE_KEY_COUNT; ++key) {
        standing += watch.life[key].standing ? 1u : 0u;
    }
    log_info("the dead that stood (host): %u life/lives fell from above zero to zero or below; %u "
             "of them stayed drawn and solid outside states 11 to 14 for more than 12 s of world "
             "time (must be 0: %u in the landing state 7, %u alive in the script's state with no "
             "script death, %u with the script's death begun and not finished, %u in another "
             "state), the longest %.1f s; ended within 2 s %u, 5 s %u, 12 s %u, later %u, and %u "
             "still standing at the report; %u row(s) read in %u census(es), %u census(es) with "
             "no world clock, %u row(s) not judged for a health or a body that did not read",
             (unsigned)watch.fell, (unsigned)watch.over,
             (unsigned)watch.over_phase[MP_DEAD_WATCH_PHASE_LANDING],
             (unsigned)watch.over_phase[MP_DEAD_WATCH_PHASE_UNDEAD],
             (unsigned)watch.over_phase[MP_DEAD_WATCH_PHASE_SCRIPTED],
             (unsigned)watch.over_phase[MP_DEAD_WATCH_PHASE_OTHER], (double)watch.longest,
             (unsigned)watch.ended[0], (unsigned)watch.ended[1], (unsigned)watch.ended[2],
             (unsigned)watch.ended[3], (unsigned)standing, (unsigned)watch.rows,
             (unsigned)watch.censuses, (unsigned)watch.no_clock, (unsigned)watch.unjudged);
    log_info("corpses stood up by another hand: %u corpse(s) whose clip changed after they lay "
             "down, from their second substep in state 14 on (must be 0: the engine never changes "
             "a corpse's clip), of %u corpse(s) watched that long",
             (unsigned)watch.corpses_moved, (unsigned)watch.corpses_held);
    report_names();
}
