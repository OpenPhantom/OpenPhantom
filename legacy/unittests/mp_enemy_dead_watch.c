/* mp_enemy_dead_watch.c: the host's half of the dead that stood, and what its report can still say
 * once the enemy table has been reset.
 *
 * The rule has its own test; this one drives the module over actors and bodies made up in memory
 * and reads its report back. Level end is the case: the head node takes message 6 first and resets
 * the table, and only then does the tail node write the report. A report that forgot the old table
 * before it wrote would print a level's names and its open runs as if the level had had none.
 *
 * There is no world clock here, so no life stands for any time and none passes the twelve second
 * limit; the corpse another hand stood up is detected by census alone, which is what lets its name
 * stand for all of them.
 */
#include "unittest.h"

#include "mp_enemy_dead_watch.h"
#include "mp_enemy_sync.h"
#include "mp_enemy_wire.h"

#include "common/logging.h"
#include "common/text.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The log, kept so the report can be read back. Every entry of common/logging is defined here,
 * so the linker never takes the library's file and nothing is written to disk. Each line is cut
 * where the real log's buffer of 1024 would cut it. */
static char   s_kept[32][1024];
static size_t s_kept_count;

static void keep(const char *format, va_list arguments)
{
    char *line = s_kept[s_kept_count % 32u];

    (void)text_vformat(line, sizeof s_kept[0], format, arguments);
    ++s_kept_count;
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

static void log_forget(void)
{
    s_kept_count = 0u;
}

static bool log_has(const char *text)
{
    size_t i;

    for (i = 0; i < s_kept_count && i < 32u; ++i) {
        if (strstr(s_kept[i], text) != NULL) {
            return true;
        }
    }
    return false;
}

/* ==============================================================================================
 * Two actors with a body each, as the module reads them: the body at actor+0x34, its drawn bit at
 * body+0x00 and its class at body+0x04.
 * ============================================================================================ */

static uint8_t s_actor[2][0x200];
static uint8_t s_body[2][0x10];

static void put32(uint8_t *at, uint32_t value)
{
    memcpy(at, &value, sizeof value);
}

static uint32_t actor(size_t i)
{
    return (uint32_t)(uintptr_t)s_actor[i];
}

static void build(void)
{
    size_t i;

    memset(s_actor, 0, sizeof s_actor);
    memset(s_body, 0, sizeof s_body);
    for (i = 0; i < 2u; ++i) {
        put32(s_actor[i] + 0x34u, (uint32_t)(uintptr_t)s_body[i]);
        put32(s_body[i], 1u);        /* drawn */
        put32(s_body[i] + 4u, 2u);   /* a class: it blocks */
    }
}

static mp_enemy_record_t record(int32_t health, uint32_t state, uint32_t clip)
{
    mp_enemy_record_t r;

    memset(&r, 0, sizeof r);
    r.value[MP_ENEMY_F_HEALTH] = mp_enemy_wire_put_health(health);
    r.value[MP_ENEMY_F_STATE]  = state;
    r.value[MP_ENEMY_F_CLIP]   = clip;
    return r;
}

/* One census as the host's reads it, keys upward: placement 15 falls in the script's state and
 * stays up, placement 16 dies into state 14 and has its corpse's clip changed by somebody else. */
static void census(int32_t health15, int32_t health16, uint32_t state16, uint32_t clip16)
{
    mp_enemy_record_t a = record(health15, 1u, 5u);
    mp_enemy_record_t b = record(health16, state16, clip16);

    mp_enemy_dead_watch_row(15u, 1u, actor(0), &a);
    mp_enemy_dead_watch_row(16u, 1u, actor(1), &b);
}

static void check_the_names_outlive_the_reset(void)
{
    ut_section("a level's names and open runs are in the report the reset comes before");

    build();
    mp_enemy_sync_reset();
    census(100, 100, 1u, 0u);
    census(0, 0, 14u, 21u);     /* both fall; 16 in the corpse state */
    census(0, 0, 14u, 21u);     /* the corpse's clip is held */
    census(0, 0, 14u, 0u);      /* and changed by another hand */

    /* Message 6 runs forward: the head node resets the table, then the tail node reports. */
    mp_enemy_sync_reset();
    log_forget();
    mp_enemy_dead_watch_report(true);
    ut_check(log_has("the corpse of placement 16 (life 1) changed clip from 21 to 0"),
             "the corpse another hand stood up is named at the level's end");
    ut_check(log_has("corpses stood up by another hand: 1 corpse(s)"),
             "and counted, which the counters did even while the names were lost");
    ut_check(log_has("and 1 still standing at the report"),
             "the life that fell and stayed up is still standing at the report, not forgotten");

    /* The first row of the next level forgets the old table, and its report says so. */
    {
        mp_enemy_record_t fresh = record(100, 1u, 0u);

        mp_enemy_dead_watch_row(15u, 1u, actor(0), &fresh);
    }
    log_forget();
    mp_enemy_dead_watch_report(true);
    ut_check(!log_has("the corpse of placement 16"),
             "the next level's first row forgets the last level's names");
    ut_check(log_has("and 0 still standing at the report"), "and its open runs");
}

int main(void)
{
    check_the_names_outlive_the_reset();
    return ut_summary("mp_enemy_dead_watch");
}
