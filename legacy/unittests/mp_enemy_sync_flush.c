/* mp_enemy_sync_flush.c: a client's flush over replicas made up in memory, as it writes them once a
 * substep.
 *
 * SIZE NOTE: over 600 lines. Two hundred of them are the harness the flush needs to write into
 * memory at all: the kept log, the engine's cell table, the pool and the host's blocks. The
 * sections that follow share it and nothing else; the next one that grows the file takes the
 * harness into a header of its own for a second flush test.
 *
 * Two defects this test exists for, and neither shows in any line of a report without the
 * counters this test reads back:
 *
 *   two records of one replica taken between two flushes fell together. The flush wrote the newer,
 *   the older never reached the body, and whatever the host played between them, a clip begun
 *   again, a throw, a step, was never seen here;
 *
 *   a substep without a record left the body's two pose pairs as the last write had opened them.
 *   The engine commits no pose for a parked actor, so every frame of such a substep drew the last
 *   turn again from its start.
 *
 * The pool is made up the way mp_enemy_census.c makes it, and the engine's cell table is played by
 * this file, so the binding installs on this memory and every write the flush makes lands here.
 * Three actors: placement 7; placement 8, whose actor carries the player's body; placement 9. Each
 * has a slot with its link word, a placement record whose live word names it, and a body with the
 * pose pair at +0x18 and +0x3C and the previous pose at +0x54 and +0x60.
 *
 * The host's side is played by hand as far as the block's frame goes: the count, the level, the
 * bitmap and an empty head for the world events. The records inside are the codec's own, each a
 * delta against the one the host sent before for the same life, as a host's view keeps them.
 */
#include "unittest.h"

#include "mp_cadence.h"
#include "mp_cells.h"
#include "mp_enemy_bind.h"
#include "mp_enemy_body_rule.h"
#include "mp_enemy_dead_watch.h"
#include "mp_enemy_sync.h"
#include "mp_enemy_sync_internal.h"
#include "mp_enemy_wire.h"

#include "common/logging.h"
#include "common/text.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* ==============================================================================================
 * The log, kept. Every entry of common/logging is defined here, so the linker never takes
 * the library's file and nothing is written to disk.
 * ============================================================================================ */

#define LINES_KEPT 96u
#define LINE_BYTES 1100u

static char   kept[LINES_KEPT][LINE_BYTES];
static size_t kept_count;

static void keep(const char *format, va_list arguments)
{
    char *line = kept[kept_count % LINES_KEPT];

    (void)text_vformat(line, LINE_BYTES, format, arguments);
    ++kept_count;
}

void log_init(const char *feature_name, bool truncate)
{
    (void)feature_name;
    (void)truncate;
}

void log_shutdown(void)
{
}

void log_info(const char *format, ...)
{
    va_list arguments;

    va_start(arguments, format);
    keep(format, arguments);
    va_end(arguments);
}

void log_warning(const char *format, ...)
{
    va_list arguments;

    va_start(arguments, format);
    keep(format, arguments);
    va_end(arguments);
}

void log_error(const char *format, ...)
{
    va_list arguments;

    va_start(arguments, format);
    keep(format, arguments);
    va_end(arguments);
}

const char *log_path(void)
{
    return "";
}

/* The newest kept line that begins with `head`, or NULL. */
static const char *line_of(const char *head)
{
    size_t count = kept_count < LINES_KEPT ? kept_count : LINES_KEPT;
    size_t i;

    for (i = count; i-- > 0;) {
        if (strncmp(kept[i], head, strlen(head)) == 0) {
            return kept[i];
        }
    }
    return NULL;
}

/* The number written right in front of `phrase` in the newest line that begins with `head`, or
 * -1 when there is no such line or no such number. */
static long number_before(const char *head, const char *phrase)
{
    const char *line = line_of(head);
    const char *at;

    if (line == NULL || (at = strstr(line, phrase)) == NULL || at == line) {
        return -1;
    }
    --at;
    while (at > line && *at == ' ') {
        --at;
    }
    while (at > line && at[-1] >= '0' && at[-1] <= '9') {
        --at;
    }
    return (*at >= '0' && *at <= '9') ? strtol(at, NULL, 10) : -1;
}

/* A client's report of the flush and of the dead that stood, read afresh. */
static void report(void)
{
    kept_count = 0;
    mp_cadence_report(false);
    mp_enemy_dead_watch_client_report(true);
}

/* ==============================================================================================
 * The engine's cell table, played by the test.
 * ============================================================================================ */

static uint32_t pool_cell;

uintptr_t mp_cells_address(mp_cell_t cell)
{
    return cell == MP_CELL_ENEMY_POOL ? (uintptr_t)&pool_cell : 0u;
}

size_t mp_cells_resolve(bool log_every_cell)
{
    (void)log_every_cell;
    return 0u;
}

const char *mp_cells_name(mp_cell_t cell)
{
    (void)cell;
    return "cell";
}

const mp_operand_t *mp_cells_operands(size_t *count)
{
    if (count != NULL) {
        *count = 0u;
    }
    return NULL;
}

uint32_t mp_cells_damage_table_hash(const uint32_t impacts[MP_CELLS_SHOT_ROWS])
{
    (void)impacts;
    return 0u;
}

bool mp_cells_hero_position(float out[3])
{
    (void)out;
    return false;
}

/* ==============================================================================================
 * The pool: three actors, their slots, bodies and placement records.
 * ============================================================================================ */

#define LIST_HEAD_OFFSET     0x04u
#define LIST_CAPACITY_OFFSET 0x10u
#define NODE_TO_ACTOR        4u
#define A_PLACEMENT          0x10u
#define A_STATE_FLAGS        0x14u
#define A_INDEX              0x18u
#define A_STATE              0x20u
#define A_BODY               0x34u
#define A_HEALTH             0x38u
#define A_POS                0xD0u
#define B_FLAGS              0x00u
#define B_CLASS              0x04u
#define B_SHOOTER_CLASS      0x08u
#define B_POSE_POS           0x18u
#define B_POSE_ROT           0x3Cu
#define B_PREV_POS           0x54u
#define B_PREV_ROT           0x60u
#define B_BASE_SLOT          0xECu
#define B_OVERLAY_SLOT       0xF8u
#define R_LIVE_WORD          0xD0u
#define FLAG_HOSTS_PLAYER    0x2000u
#define LINK_FREE            0xFFFFFFFFu
#define STATE_ACTIVE         1u
#define STATE_PARKED         3u

#define ACTORS       3u
#define SLOT_BYTES   0x100u
#define BODY_BYTES   0x180u
#define RECORD_BYTES 0x100u

/* The level the client is in, as the bridge would tell it. */
#define HERE 57u

enum { REPLICA = 0, PLAYER = 1, OTHER = 2 };

static const uint8_t KEY[ACTORS] = { 7u, 8u, 9u };

static uint8_t arena[0x40u + ACTORS * (SLOT_BYTES + BODY_BYTES + RECORD_BYTES)];

typedef struct pool {
    uint8_t *list;
    uint8_t *slot[ACTORS];
    uint8_t *body[ACTORS];
    uint8_t *record[ACTORS];
} pool_t;

static pool_t pool;

static void put32(uint8_t *at, uint32_t value)
{
    memcpy(at, &value, sizeof value);
}

static void putf3(uint8_t *at, float x, float y, float z)
{
    float v[3];

    v[0] = x;
    v[1] = y;
    v[2] = z;
    memcpy(at, v, sizeof v);
}

static uint32_t addr(const void *p)
{
    return (uint32_t)(uintptr_t)p;
}

static uint8_t *actor_of(size_t i)
{
    return pool.slot[i] + NODE_TO_ACTOR;
}

static float body_float(size_t i, uint32_t offset, size_t axis)
{
    float v;

    memcpy(&v, pool.body[i] + offset + 4u * axis, sizeof v);
    return v;
}

/* Both pairs of body `i` brought to one value each: previous rotation equal to the current one,
 * all twelve bytes, and the previous position equal to the current one. */
static bool pairs_closed(size_t i)
{
    return memcmp(pool.body[i] + B_PREV_ROT, pool.body[i] + B_POSE_ROT, 12u) == 0 &&
           memcmp(pool.body[i] + B_PREV_POS, pool.body[i] + B_POSE_POS, 12u) == 0;
}

static void build_pool(void)
{
    size_t i;

    memset(arena, 0, sizeof arena);
    pool.list = arena;
    for (i = 0; i < ACTORS; ++i) {
        pool.slot[i]   = arena + 0x40u + i * SLOT_BYTES;
        pool.body[i]   = arena + 0x40u + ACTORS * SLOT_BYTES + i * BODY_BYTES;
        pool.record[i] = arena + 0x40u + ACTORS * (SLOT_BYTES + BODY_BYTES) + i * RECORD_BYTES;
    }
    pool_cell = addr(pool.list);
    put32(pool.list + LIST_CAPACITY_OFFSET, ACTORS);
    put32(pool.list + LIST_HEAD_OFFSET, addr(pool.slot[0]));
    for (i = 0; i < ACTORS; ++i) {
        uint8_t *actor = actor_of(i);
        uint8_t *body  = pool.body[i];

        put32(pool.slot[i], i + 1u < ACTORS ? addr(pool.slot[i + 1u]) : 0u);
        put32(actor + A_PLACEMENT, addr(pool.record[i]));
        put32(pool.record[i] + R_LIVE_WORD, addr(actor));
        put32(actor + A_INDEX, KEY[i]);
        put32(actor + A_STATE, STATE_ACTIVE);
        put32(actor + A_STATE_FLAGS, i == PLAYER ? FLAG_HOSTS_PLAYER : 0u);
        put32(actor + A_BODY, addr(body));
        put32(actor + A_HEALTH, 100u);
        putf3(actor + A_POS, 1.0f, 2.0f, 3.0f);
        put32(body + B_FLAGS, 0x3u);
        put32(body + B_CLASS, 5u);
        put32(body + B_SHOOTER_CLASS, 5u);
        put32(body + B_BASE_SLOT, 0xFFFFFFFFu);
        put32(body + B_OVERLAY_SLOT, 0xFFFFFFFFu);
        /* Every body starts with both pairs open, so that a close anybody made shows. */
        putf3(body + B_POSE_POS, 1.0f, 2.0f, 3.0f);
        putf3(body + B_PREV_POS, 0.5f, 2.0f, 3.0f);
        putf3(body + B_POSE_ROT, 0.0f, 45.0f, 0.0f);
        putf3(body + B_PREV_ROT, 0.0f, 5.0f, 0.0f);
    }
}

/* ==============================================================================================
 * The host, as far as its blocks go.
 * ============================================================================================ */

static mp_enemy_record_t sent[16];
static bool              sent_known[16];
static uint32_t          wire_tick = 1000u;
static uint8_t           block[2048];

static mp_enemy_record_t record_of(uint8_t key, uint8_t life, float x, float heading,
                                   int32_t health, uint32_t state, bool standing)
{
    mp_enemy_record_t     record;
    mp_enemy_body_state_t body;

    memset(&record, 0, sizeof record);
    memset(&body, 0, sizeof body);
    record.value[MP_ENEMY_F_INDEX]      = key;
    record.value[MP_ENEMY_F_GENERATION] = life;
    (void)mp_enemy_wire_put_position(x, &record.value[MP_ENEMY_F_POS_X]);
    (void)mp_enemy_wire_put_position(5.0f, &record.value[MP_ENEMY_F_POS_Y]);
    (void)mp_enemy_wire_put_position(7.0f, &record.value[MP_ENEMY_F_POS_Z]);
    record.value[MP_ENEMY_F_HEADING] = (uint32_t)(int32_t)(heading * 182.044444f) & 0xFFFFu;
    record.value[MP_ENEMY_F_STATE]   = state;
    record.value[MP_ENEMY_F_HEALTH]  = mp_enemy_wire_put_health(health);
    record.value[MP_ENEMY_F_CLIP]    = state;   /* a clip per state, so a record is recognisable */
    body.has      = true;
    body.drawn    = standing;
    body.solid    = standing;
    body.shadow   = standing;
    body.alpha    = MP_ENEMY_BODY_OPAQUE;
    body.dissolve = MP_ENEMY_BODY_WHOLE;
    record.value[MP_ENEMY_F_BODY] = mp_enemy_body_pack(&body);
    return record;
}

static mp_enemy_record_t walking(uint8_t key, float x, float heading)
{
    return record_of(key, 1u, x, heading, 100, STATE_ACTIVE, true);
}

/* The host forgets what it sent of `key`, so its next record goes whole: what a host does for a
 * replica the client let go of or lost. */
static void host_forgets(uint8_t key)
{
    sent_known[key] = false;
}

/* One block of the host's: `count` records, and every actor of the pool listed but `unlisted`. */
static bool deliver(const mp_enemy_record_t *records, size_t count, int unlisted)
{
    size_t at = MP_ENEMY_SYNC_HEADER_BYTES;
    size_t i;

    memset(block, 0, sizeof block);
    block[0] = (uint8_t)count;
    block[1] = (uint8_t)(HERE & 0xFFu);
    block[2] = (uint8_t)(HERE >> 8);
    for (i = 0; i < ACTORS; ++i) {
        if ((int)KEY[i] != unlisted) {
            block[1u + MP_ENEMY_SYNC_LEVEL_BYTES + (KEY[i] >> 3)] |= (uint8_t)(1u << (KEY[i] & 7u));
        }
    }
    for (i = 0; i < count; ++i) {
        uint8_t                  key  = (uint8_t)records[i].value[MP_ENEMY_F_INDEX];
        uint8_t                  life = (uint8_t)records[i].value[MP_ENEMY_F_GENERATION];
        const mp_enemy_record_t *base = NULL;
        size_t                   bytes = 0;

        if (sent_known[key] && sent[key].value[MP_ENEMY_F_GENERATION] == life) {
            base = &sent[key];
        }
        block[at]      = key;
        block[at + 1u] = life;
        /* With no base, the whole record, as the host sends a key or a life the client lacks. */
        if (base == NULL
                ? !mp_enemy_wire_encode_whole(&records[i], block + at + 2u,
                                              sizeof block - at - 2u, &bytes)
                : !mp_enemy_wire_encode(&records[i], base, block + at + 2u,
                                        sizeof block - at - 2u, &bytes)) {
            return false;
        }
        at += 2u + bytes;
        sent[key]       = records[i];
        sent_known[key] = true;
    }
    return mp_enemy_sync_apply(block, at, ++wire_tick);
}

static bool deliver_one(mp_enemy_record_t record)
{
    return deliver(&record, 1u, -1);
}

/* A fresh level: the table reset, the level told, the pool as the engine built it, and the host
 * starting its view over. */
static void begin_level(void)
{
    mp_enemy_sync_reset();
    mp_enemy_sync_set_level(true, HERE);
    build_pool();
    memset(sent_known, 0, sizeof sent_known);
}

/* ==============================================================================================
 * Two records of one replica in one substep.
 * ============================================================================================ */

static const char *const SECOND = "enemies, the second record (client): ";
static const char *const PAIRS  = "enemies, the pairs the flush closed (client): ";
static const char *const FAULTS = "enemies, the second record, must be 0: ";

static void test_two_records_two_substeps(void)
{
    long windows;
    long late;
    long third;

    ut_section("two records of one replica in one substep are written in two substeps");
    report();
    windows = number_before(SECOND, "window(s) in which");
    late    = number_before(SECOND, "record(s) written one substep late");
    third   = number_before(SECOND, "dropped for a third");
    begin_level();
    ut_check(deliver_one(walking(7u, 10.0f, 0.0f)), "the first record is taken");
    (void)mp_enemy_sync_flush();
    ut_check(body_float(REPLICA, B_POSE_POS, 0) == 10.0f, "and written: x = 10");

    ut_check(deliver_one(walking(7u, 20.0f, 0.0f)) && deliver_one(walking(7u, 21.0f, 0.0f)),
             "two records arrive before the next flush, x = 20 and x = 21");
    (void)mp_enemy_sync_flush();
    ut_checkf(body_float(REPLICA, B_POSE_POS, 0) == 20.0f,
              "the first flush writes the older, x = 20 (x = %.2f)",
              (double)body_float(REPLICA, B_POSE_POS, 0));
    (void)mp_enemy_sync_flush();
    ut_checkf(body_float(REPLICA, B_POSE_POS, 0) == 21.0f &&
                  body_float(REPLICA, B_PREV_POS, 0) == 20.0f,
              "and the next flush the newer, stepping from the older (x = %.2f from %.2f)",
              (double)body_float(REPLICA, B_POSE_POS, 0),
              (double)body_float(REPLICA, B_PREV_POS, 0));

    ut_check(deliver_one(walking(7u, 30.0f, 0.0f)) && deliver_one(walking(7u, 31.0f, 0.0f)) &&
                 deliver_one(walking(7u, 32.0f, 0.0f)),
             "three records before the next flush, x = 30, 31, 32");
    (void)mp_enemy_sync_flush();
    ut_checkf(body_float(REPLICA, B_POSE_POS, 0) == 31.0f,
              "a third drops the oldest: the flush writes the middle one, x = 31 (x = %.2f)",
              (double)body_float(REPLICA, B_POSE_POS, 0));
    (void)mp_enemy_sync_flush();
    ut_checkf(body_float(REPLICA, B_POSE_POS, 0) == 32.0f &&
                  body_float(REPLICA, B_PREV_POS, 0) == 31.0f,
              "and the newest one substep later, so no step is longer than one without the queue "
              "(x = %.2f from %.2f)",
              (double)body_float(REPLICA, B_POSE_POS, 0),
              (double)body_float(REPLICA, B_PREV_POS, 0));
    (void)mp_enemy_sync_flush();
    ut_check(body_float(REPLICA, B_POSE_POS, 0) == 32.0f,
             "and a flush with nothing new writes nothing again");

    report();
    ut_checkf(number_before(SECOND, "window(s) in which") == windows + 2 &&
                  number_before(SECOND, "record(s) written one substep late") == late + 2 &&
                  number_before(SECOND, "dropped for a third") == third + 1,
              "the flush told the lines: two windows of two or more, x = 21 and x = 32 one "
              "substep late, x = 30 fallen to the third (%ld, %ld, %ld)",
              number_before(SECOND, "window(s) in which") - windows,
              number_before(SECOND, "record(s) written one substep late") - late,
              number_before(SECOND, "dropped for a third") - third);
}

static void test_never_across_a_life(void)
{
    uint8_t life = 0;
    long    new_life;

    ut_section("a new life is written at once, and nothing of the old one after it");
    report();
    new_life = number_before(SECOND, "dropped at a new life");
    begin_level();
    (void)deliver_one(walking(7u, 40.0f, 0.0f));
    (void)mp_enemy_sync_flush();
    ut_check(deliver_one(walking(7u, 41.0f, 0.0f)) &&
                 deliver_one(record_of(7u, 2u, 50.0f, 0.0f, 100, STATE_ACTIVE, true)),
             "life 1 at x = 41 and life 2 at x = 50 arrive before the next flush");
    (void)mp_enemy_sync_flush();
    ut_checkf(body_float(REPLICA, B_POSE_POS, 0) == 50.0f &&
                  mp_enemy_sync_generation(7u, &life) && life == 2u,
              "the flush writes the new life at once (x = %.2f, life %u)",
              (double)body_float(REPLICA, B_POSE_POS, 0), (unsigned)life);
    (void)mp_enemy_sync_flush();
    ut_check(body_float(REPLICA, B_POSE_POS, 0) == 50.0f,
             "and the old life's record never follows it");
    ut_check(!mp_enemy_sync_state()->placement[7].queue.held,
             "nothing of the old life is kept for the key");
    report();
    ut_checkf(number_before(SECOND, "dropped at a new life") == new_life + 1,
              "the old life's unwritten record is counted as fallen to the new one (%ld)",
              number_before(SECOND, "dropped at a new life") - new_life);
}

/* ==============================================================================================
 * The pairs of a parked replica in a substep without a write.
 * ============================================================================================ */

static void test_a_closed_pair_draws_still(void)
{
    uint8_t turned[12];
    long    rotations;
    long    open_before;

    ut_section("a substep without a record closes both pairs");
    report();
    rotations   = number_before(PAIRS, "rotation pair(s) and");
    open_before = number_before(PAIRS, "of the rotation pairs open before");
    begin_level();
    (void)deliver_one(walking(7u, 10.0f, 0.0f));
    (void)mp_enemy_sync_flush();
    (void)deliver_one(walking(7u, 12.0f, 90.0f));
    (void)mp_enemy_sync_flush();
    ut_check(!pairs_closed(REPLICA), "a record that turns and moves the body opens both pairs");
    memcpy(turned, pool.body[REPLICA] + B_POSE_ROT, sizeof turned);

    (void)mp_enemy_sync_flush();
    ut_check(memcmp(pool.body[REPLICA] + B_PREV_ROT, pool.body[REPLICA] + B_POSE_ROT, 12u) == 0,
             "a flush with no record for it brings the rotation pair to one value, all twelve "
             "bytes, so the engine draws the body still instead of that turn again");
    ut_check(memcmp(pool.body[REPLICA] + B_PREV_POS, pool.body[REPLICA] + B_POSE_POS, 12u) == 0,
             "and the position pair too");
    ut_check(memcmp(pool.body[REPLICA] + B_POSE_ROT, turned, sizeof turned) == 0 &&
                 body_float(REPLICA, B_POSE_POS, 0) == 12.0f,
             "without moving the body: the current pose is what the last record wrote");
    report();
    ut_checkf(number_before(PAIRS, "rotation pair(s) and") == rotations + 1 &&
                  number_before(PAIRS, "of the rotation pairs open before") == open_before + 1,
              "the pairs line counts the close, and that the turn had left the pair open (%ld, "
              "%ld)", number_before(PAIRS, "rotation pair(s) and") - rotations,
              number_before(PAIRS, "of the rotation pairs open before") - open_before);

    (void)deliver_one(walking(7u, 13.0f, 100.0f));
    (void)mp_enemy_sync_flush();
    ut_check(memcmp(pool.body[REPLICA] + B_PREV_ROT, turned, sizeof turned) == 0 &&
                 body_float(REPLICA, B_PREV_POS, 0) == 12.0f &&
                 body_float(REPLICA, B_POSE_POS, 0) == 13.0f,
             "the next record opens the pair from where the body stood: one step, not two");
}

static void test_the_player_body_is_never_closed(void)
{
    uint8_t before[BODY_BYTES];
    int     flush;
    long    alone;

    ut_section("a body that carries this player is never closed");
    report();
    alone = number_before(PAIRS, "left alone because");
    begin_level();
    ut_check(deliver_one(record_of(8u, 1u, 60.0f, 30.0f, 100, STATE_ACTIVE, true)),
             "the host describes placement 8, whose actor here carries this player's body");
    memcpy(before, pool.body[PLAYER], sizeof before);
    (void)mp_enemy_sync_flush();
    ut_check(*(uint32_t *)(actor_of(PLAYER) + A_STATE) == STATE_PARKED,
             "its actor is parked like every replica");
    for (flush = 0; flush < 3; ++flush) {
        (void)mp_enemy_sync_flush();
    }
    (void)deliver_one(record_of(8u, 1u, 61.0f, 40.0f, 100, STATE_ACTIVE, true));
    (void)mp_enemy_sync_flush();
    (void)mp_enemy_sync_flush();
    ut_check(memcmp(before, pool.body[PLAYER], sizeof before) == 0,
             "and the player's body is byte for byte what it was through every flush, its open "
             "pairs included: neither written nor closed");
    report();
    ut_checkf(number_before(PAIRS, "left alone because") >= alone + 4,
              "each substep it went without a write is counted as left alone (%ld)",
              number_before(PAIRS, "left alone because") - alone);
}

static void test_a_slot_that_is_not_the_actor(void)
{
    uint8_t before[BODY_BYTES];

    ut_section("a slot the pool took back, or another placement's, is not closed");
    begin_level();
    (void)deliver_one(record_of(9u, 1u, 70.0f, 0.0f, 100, STATE_ACTIVE, true));
    (void)mp_enemy_sync_flush();
    (void)deliver_one(record_of(9u, 1u, 72.0f, 80.0f, 100, STATE_ACTIVE, true));
    (void)mp_enemy_sync_flush();
    ut_check(!pairs_closed(OTHER), "placement 9 stands written, its pairs open");

    memcpy(before, pool.body[OTHER], sizeof before);
    put32(pool.slot[OTHER], LINK_FREE);
    (void)mp_enemy_sync_flush();
    ut_check(memcmp(before, pool.body[OTHER], sizeof before) == 0,
             "a slot whose link word says the pool took it back is not touched");
    put32(pool.slot[OTHER], 0u);
    put32(actor_of(OTHER) + A_INDEX, 10u);
    (void)mp_enemy_sync_flush();
    ut_check(memcmp(before, pool.body[OTHER], sizeof before) == 0,
             "nor one that another placement holds now");
    put32(actor_of(OTHER) + A_INDEX, 9u);
}

/* ==============================================================================================
 * The watchers read what was written.
 * ============================================================================================ */

static void test_the_watchers_read_the_written_record(void)
{
    enemy_sync_state_t *s = mp_enemy_sync_state();
    uint32_t            throws;
    uint32_t            deaths;
    long                fell;

    ut_section("the watchers read the record the flush wrote, not the newest taken");
    begin_level();
    (void)deliver_one(walking(7u, 10.0f, 0.0f));
    (void)mp_enemy_sync_flush();
    report();
    fell   = number_before("the dead that stood (client): ", "life/lives the host reported");
    deaths = s->death_count;

    ut_check(deliver_one(record_of(7u, 1u, 11.0f, 0.0f, 50, STATE_ACTIVE, true)) &&
                 deliver_one(record_of(7u, 1u, 11.0f, 0.0f, -5, MP_ENEMY_STATE_DEATH, false)),
             "standing with health 50, then dead and no longer drawn, before one flush");
    (void)mp_enemy_sync_flush();
    report();
    ut_checkf(number_before("the dead that stood (client): ", "life/lives the host reported") ==
                  fell,
              "after the flush that wrote the standing record the dead watch saw no fall (%ld "
              "before, %ld now)", fell,
              number_before("the dead that stood (client): ", "life/lives the host reported"));
    ut_checkf(s->death_count == deaths,
              "and the death watch began no death (%u before, %u now)", (unsigned)deaths,
              (unsigned)s->death_count);
    (void)mp_enemy_sync_flush();
    report();
    ut_checkf(number_before("the dead that stood (client): ", "life/lives the host reported") ==
                  fell + 1,
              "the flush that wrote the dead record is the one the fall is counted at (%ld)",
              number_before("the dead that stood (client): ", "life/lives the host reported"));
    ut_checkf(s->death_count == deaths + 1u &&
                  s->deaths[deaths].last_clip == MP_ENEMY_STATE_DEATH,
              "and the death watch began it there, with the clip written with it (%u)",
              (unsigned)(s->death_count > deaths ? s->deaths[deaths].last_clip : 0u));

    ut_section("a throw is counted once, at the write that brings it");
    begin_level();
    throws = s->knockback.throws;
    (void)deliver_one(walking(7u, 10.0f, 0.0f));
    (void)mp_enemy_sync_flush();
    ut_check(deliver_one(record_of(7u, 1u, 10.0f, 0.0f, 100, 5u, true)) &&
                 deliver_one(record_of(7u, 1u, 10.0f, 0.0f, 100, 6u, true)),
             "a record in state 5, then one entering state 6, before one flush");
    (void)mp_enemy_sync_flush();
    ut_check(s->knockback.throws == throws, "the flush that wrote state 5 counts no throw");
    (void)mp_enemy_sync_flush();
    ut_checkf(s->knockback.throws == throws + 1u,
              "the flush that wrote state 6 counts exactly one (%u)",
              (unsigned)(s->knockback.throws - throws));
}

/* ==============================================================================================
 * Every way out of holding a key.
 * ============================================================================================ */

/* Whether placement 7 keeps a record in front of its mirror. */
static bool kept_for_seven(void)
{
    return mp_enemy_sync_state()->placement[7].queue.held;
}

/* Two records of placement 7 in one window, the older of them kept. */
static void two_in_a_window(float x)
{
    (void)deliver_one(walking(7u, x, 0.0f));
    (void)deliver_one(walking(7u, x + 1.0f, 0.0f));
    ut_check(kept_for_seven(), "two records in one window: the older is kept");
}

/* Every way out of holding a key empties its queue at the moment it is taken, not at the next
 * record: a kept record left behind by one of them would be written after the key came back, a
 * step backwards, or onto another body. */
static void test_every_exit(void)
{
    long let_go;

    ut_section("every way out of holding a key");
    report();
    let_go = number_before(SECOND, "dropped at a let go or a removal");

    begin_level();
    (void)deliver_one(walking(7u, 10.0f, 0.0f));
    (void)mp_enemy_sync_flush();
    two_in_a_window(60.0f);
    ut_check(deliver(NULL, 0u, 7), "a let go: the host stops listing placement 7");
    ut_check(!kept_for_seven(), "the let go empties the queue where it is decided");
    (void)mp_enemy_sync_flush();
    ut_check(*(uint32_t *)(actor_of(REPLICA) + A_STATE) != STATE_PARKED,
             "the replica is handed back");
    host_forgets(7u);
    (void)deliver_one(walking(7u, 70.0f, 0.0f));
    (void)mp_enemy_sync_flush();
    ut_checkf(body_float(REPLICA, B_POSE_POS, 0) == 70.0f,
              "listed again, it takes the host's newest record and nothing held before the let "
              "go (x = %.2f)", (double)body_float(REPLICA, B_POSE_POS, 0));

    two_in_a_window(80.0f);
    mp_enemy_sync_performed(7u, (uintptr_t)actor_of(REPLICA), 1u, 1u);
    ut_check(!kept_for_seven(), "a removal of the life empties it");
    (void)mp_enemy_sync_flush();
    host_forgets(7u);
    (void)deliver_one(walking(7u, 90.0f, 0.0f));
    (void)mp_enemy_sync_flush();
    ut_checkf(body_float(REPLICA, B_POSE_POS, 0) == 90.0f,
              "after a removal the next life of the key starts from the host's newest record "
              "(x = %.2f)", (double)body_float(REPLICA, B_POSE_POS, 0));

    two_in_a_window(100.0f);
    put32(pool.slot[REPLICA], LINK_FREE);
    (void)mp_enemy_sync_flush();
    ut_check(!kept_for_seven(), "a replica gone by the time of the write empties it");
    put32(pool.slot[REPLICA], addr(pool.slot[PLAYER]));
    (void)deliver_one(walking(7u, 110.0f, 0.0f));
    (void)mp_enemy_sync_flush();
    ut_checkf(body_float(REPLICA, B_POSE_POS, 0) == 110.0f,
              "and what was held for it is not written when it is back (x = %.2f)",
              (double)body_float(REPLICA, B_POSE_POS, 0));

    two_in_a_window(120.0f);
    mp_enemy_sync_release_all();
    ut_check(*(uint32_t *)(actor_of(REPLICA) + A_STATE) != STATE_PARKED && !kept_for_seven(),
             "a session's end hands every replica back and empties its queue");
    ut_check(!mp_enemy_sync_state()->placement[7].dirty,
             "and owes the body no record any more: a flush after the hand-back writes nothing "
             "into a body the engine has back");

    begin_level();
    (void)deliver_one(walking(7u, 130.0f, 0.0f));
    (void)mp_enemy_sync_flush();
    two_in_a_window(131.0f);
    mp_enemy_sync_reset();
    ut_check(!kept_for_seven(), "a reset empties it with the table");
    begin_level();
    (void)deliver_one(walking(7u, 140.0f, 0.0f));
    (void)mp_enemy_sync_flush();
    ut_checkf(body_float(REPLICA, B_POSE_POS, 0) == 140.0f,
              "and the next level starts every key over (x = %.2f)",
              (double)body_float(REPLICA, B_POSE_POS, 0));

    report();
    ut_checkf(number_before(SECOND, "dropped at a let go or a removal") == let_go + 5,
              "the let go, the removal, the replica gone, the session's end and the reset, which "
              "lets every replica go before it clears the table, each counted the record they "
              "dropped (%ld)",
              number_before(SECOND, "dropped at a let go or a removal") - let_go);
    ut_check(line_of(FAULTS) != NULL &&
                 strstr(line_of(FAULTS), "0 write(s) of a record for a life other than the "
                                         "table's, 0 substep(s) with two writes of one replica") !=
                     NULL,
             "and through every flush of this test no record of another life was written and no "
             "replica twice in one substep");
}

int main(void)
{
    build_pool();
    ut_check(mp_enemy_bind_install(), "the binding installs on the pool made up here");
    mp_enemy_sync_set_enabled(true);

    test_two_records_two_substeps();
    test_never_across_a_life();
    test_a_closed_pair_draws_still();
    test_the_player_body_is_never_closed();
    test_a_slot_that_is_not_the_actor();
    test_the_watchers_read_the_written_record();
    test_every_exit();

    return ut_summary("the enemy flush over replicas in memory");
}
