/* unittests/mp_bridge_statement_stand_in.c: the stand-ins the tests of mp_bridge_statement run
 * against. See the header. */
#include "mp_bridge_statement_stand_in.h"

#include "mp_bridge.h"
#include "mp_bridge_content.h"
#include "mp_bridge_shared.h"
#include "mp_content.h"
#include "mp_mod_allow.h"
#include "mp_mod_census.h"
#include "mp_mod_manifest_rule.h"
#include "mp_session.h"
#include "mp_wire.h"
#include "mp_world_values.h"

#include "common/logging.h"
#include "common/text.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Static, not on the stack: a session is megabytes. */
static mp_session_t       s_host;
static mp_session_t       s_client;
static mp_bridge_state_t  s_state;
static mp_bridge_shared_t s_shared = { &s_state, NULL, NULL, NULL, &s_host, &s_client, NULL, NULL };

static struct {
    bool               resolves;
    bool               seen;
    uint32_t           damage;
    unsigned           census_calls;
    unsigned           connects;
    const char *const *foreign;
    size_t             foreign_listed;
    unsigned           foreign_count;
    bool               foreign_unjudged;
    bool               statement_refused;
    bool               armed_by_menu;
} s_world;

void stand_in_set_damage(bool resolves, bool seen, uint32_t damage)
{
    s_world.resolves = resolves;
    s_world.seen     = seen;
    s_world.damage   = damage;
}

void stand_in_set_foreign(const char *const *names, size_t listed, unsigned count, bool judged)
{
    s_world.foreign          = names;
    s_world.foreign_listed   = listed;
    s_world.foreign_count    = count;
    s_world.foreign_unjudged = !judged;
}

void stand_in_set_statement_taken(bool taken)
{
    s_world.statement_refused = !taken;
}

void stand_in_set_armed_by_menu(bool by_menu)
{
    s_world.armed_by_menu = by_menu;
}

unsigned stand_in_census_calls(void) { return s_world.census_calls; }
unsigned stand_in_connects(void) { return s_world.connects; }
mp_bridge_state_t *stand_in_state(void) { return &s_state; }
mp_session_t *stand_in_host(void) { return &s_host; }
mp_session_t *stand_in_client(void) { return &s_client; }

/* ===================================== What the module calls ============================== */

const mp_bridge_shared_t *mp_bridge_shared(void)
{
    return &s_shared;
}

bool mp_bridge_armed_by_menu(void)
{
    return s_world.armed_by_menu;
}

bool mp_world_values_damage_table(uint32_t *hash, bool *seen)
{
    if (!s_world.resolves) {
        return false;
    }
    *hash = s_world.damage;
    *seen = s_world.seen;
    return true;
}

uint32_t mp_content_roster_fingerprint(void)
{
    return STAND_IN_ROSTER;
}

void mp_mod_census_required_builds(mp_mod_manifest_t *out)
{
    ++s_world.census_calls;
    out->count         = 1u;
    out->mods[0].id    = MP_WIRE_MOD_MULTIPLAYER;
    out->mods[0].stamp = STAND_IN_BUILD_STAMP;
    out->mods[0].image = STAND_IN_BUILD_IMAGE;
}

size_t mp_mod_census_foreign_names(const char **names, size_t capacity, unsigned *count,
                                   bool *judged)
{
    size_t i;

    for (i = 0; i < s_world.foreign_listed && i < capacity; ++i) {
        names[i] = s_world.foreign[i];
    }
    *count  = s_world.foreign_count;
    *judged = !s_world.foreign_unjudged;
    return i;
}

const char *mp_mod_allow_read(void)
{
    return "";
}

uint8_t mp_bridge_content_judge(const uint8_t *own, size_t own_bytes, const uint8_t *far,
                                size_t far_bytes, uint8_t *detail, size_t capacity,
                                size_t *detail_bytes)
{
    (void)own;
    (void)own_bytes;
    (void)far;
    (void)far_bytes;
    (void)detail;
    (void)capacity;
    *detail_bytes = 0u;
    return 0u;
}

bool mp_session_set_statement(mp_session_t *session, const uint8_t *bytes, size_t length)
{
    if (s_world.statement_refused || length > MP_SESSION_STATEMENT_BYTES) {
        return false;
    }
    memcpy(session->statement, bytes, length);
    session->statement_bytes = (uint16_t)length;
    return true;
}

void mp_session_set_judge(mp_session_t *session, mp_session_judge_fn judge)
{
    session->judge = judge;
}

const mp_peer_t *mp_session_peer(const mp_session_t *session, size_t index)
{
    return &session->peers[index];
}

void mp_session_connect(mp_session_t *session, uint32_t endpoint)
{
    (void)endpoint;
    ++s_world.connects;
    session->peers[0].state = MP_PEER_CONNECTING;
}

/* ===================================== The log, kept ====================================== */

/* Every entry of common/logging is defined here, so the linker never takes the library's
 * file and nothing is written to disk. */
static char   s_kept[64][1024];
static size_t s_kept_count;

static void keep(const char *format, va_list arguments)
{
    char *line = s_kept[s_kept_count % 64u];

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

bool stand_in_log_has(const char *text)
{
    size_t i;

    for (i = 0; i < s_kept_count && i < 64u; ++i) {
        if (strstr(s_kept[i], text) != NULL) {
            return true;
        }
    }
    return false;
}

void stand_in_log_clear(void)
{
    s_kept_count = 0u;
}
