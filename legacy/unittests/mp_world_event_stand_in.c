/* unittests/mp_world_event_stand_in.c: the world and the log the tests of mp_world_event run in.
 * See the header. */
#include "mp_world_event_stand_in.h"

#include "mp_enemy_interest_rule.h"
#include "mp_enemy_sync.h"
#include "mp_enemy_sync_internal.h"
#include "mp_enemy_wire.h"
#include "mp_world_event.h"
#include "mp_world_event_rule.h"

#include "common/logging.h"
#include "common/text.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define HERE 57u

world_stand_in_t ws;

/* ==============================================================================================
 * The log, kept here so a report can be read back. Every entry of common/logging is defined,
 * so the linker never takes the library's file and nothing is written to disk. Every line is cut
 * where the real log's buffer of 1024 would cut it. The last sixteen are kept in order, which holds
 * a whole report of this module, and the newest line of each peer beside them.
 * ============================================================================================ */

#define LINES_KEPT 16u
#define LINE_BYTES 1024u
#define PEER_LINE  "the world events to one peer: peer "
#define HOST_LINE  "the world events (host):"

static char   kept[LINES_KEPT][LINE_BYTES];
static size_t kept_count;
static char   peer_line[MP_ENEMY_SYNC_VIEWS][LINE_BYTES];

static void keep(const char *format, va_list arguments)
{
    char         *line = kept[kept_count % LINES_KEPT];
    unsigned long peer;

    (void)text_vformat(line, LINE_BYTES, format, arguments);
    ++kept_count;
    if (strncmp(line, PEER_LINE, strlen(PEER_LINE)) != 0) {
        return;
    }
    peer = strtoul(line + strlen(PEER_LINE), NULL, 10);
    if (peer < MP_ENEMY_SYNC_VIEWS) {
        memcpy(peer_line[peer], line, LINE_BYTES);
    }
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

unsigned ws_host_count(const char *phrase)
{
    size_t i;

    kept_count = 0u;
    mp_world_event_report();
    for (i = 0; i < kept_count && i < LINES_KEPT; ++i) {
        const char *line = kept[i];
        const char *at   = strstr(line, phrase);

        if (strncmp(line, HOST_LINE, strlen(HOST_LINE)) != 0 || at == NULL) {
            continue;
        }
        while (at > line && at[-1] >= '0' && at[-1] <= '9') {
            --at;
        }
        return (unsigned)strtoul(at, NULL, 10);
    }
    return 0xFFFFFFFFu;
}

long ws_peer_says(size_t view, const char *marker)
{
    const char *line = peer_line[view];
    const char *at;
    const char *digits;

    peer_line[view][0] = '\0';
    mp_world_event_report();
    at = strstr(line, marker);
    if (at == NULL) {
        return -1;
    }
    for (digits = at; digits > line && digits[-1] >= '0' && digits[-1] <= '9'; --digits) {
    }
    return digits == at ? -1 : strtol(digits, NULL, 10);
}

/* ==============================================================================================
 * The world the host reads.
 * ============================================================================================ */

static uint32_t           s_tick = 5000u;
static mp_enemy_subject_t s_subjects[MP_ENEMY_SYNC_KEYS];
static mp_enemy_record_t  s_records[MP_ENEMY_SYNC_KEYS];

static bool fake_viewer(size_t view, mp_enemy_viewer_t *out)
{
    *out = ws.viewers[view];
    return out->placed;
}

static bool fake_subject(size_t key, uintptr_t actor, mp_enemy_subject_t *out)
{
    (void)actor;
    *out = s_subjects[key];
    return out->read;
}

static const mp_enemy_sync_interest_t SOURCE = { &fake_viewer, &fake_subject, NULL, NULL };

bool ws_test_player(const mp_world_event_t *event, uintptr_t replica)
{
    ++ws.played[event->kind];
    ws.last_replica = replica;
    ws.last_a       = event->a;
    return true;
}

uintptr_t ws_actor_of(size_t key)
{
    return (uintptr_t)(0x00A01000u + 16u * key);
}

void ws_enemy(size_t key, float x, float wake, float keep)
{
    uint32_t packed = 0;

    ws.live[key] = true;
    memset(&s_records[key], 0, sizeof s_records[key]);
    (void)mp_enemy_wire_put_position(x, &packed);
    s_records[key].value[MP_ENEMY_F_POS_X] = packed;
    (void)mp_enemy_wire_put_position(5.0f, &packed);
    s_records[key].value[MP_ENEMY_F_POS_Y] = packed;
    (void)mp_enemy_wire_put_position(7.0f, &packed);
    s_records[key].value[MP_ENEMY_F_POS_Z]  = packed;
    s_records[key].value[MP_ENEMY_F_STATE]  = 1u;
    s_records[key].value[MP_ENEMY_F_HEALTH] = mp_enemy_wire_put_health(60);
    memset(&s_subjects[key], 0, sizeof s_subjects[key]);
    s_subjects[key].read          = true;
    s_subjects[key].has_placement = true;
    s_subjects[key].placement[0]  = x;
    s_subjects[key].position[0]   = x;
    s_subjects[key].wake          = wake;
    s_subjects[key].keep          = keep;
}

void ws_fresh_host(void)
{
    size_t view;

    mp_enemy_sync_reset();
    mp_enemy_sync_set_enabled(true);
    mp_enemy_sync_set_level(true, HERE);
    (void)mp_enemy_sync_begin_send();
    memset(ws.live, 0, sizeof ws.live);
    memset(s_subjects, 0, sizeof s_subjects);
    memset(ws.viewers, 0, sizeof ws.viewers);
    for (view = 0; view < MP_ENEMY_SYNC_VIEWS; ++view) {
        ws.viewers[view].placed = true;
        ws.viewers[view].slot   = (uint8_t)(view + 1u);
    }
    mp_enemy_sync_set_interest(&SOURCE);
    memset(ws.played, 0, sizeof ws.played);
    ws.beside = 0u;
    (void)mp_world_event_set_player(MP_WORLD_EVENT_SCRIPT_EMITTER, &ws_test_player);
    (void)mp_world_event_set_player(MP_WORLD_EVENT_NPC_CLANG, &ws_test_player);
    (void)mp_world_event_set_player(MP_WORLD_EVENT_LIMB_FLY, &ws_test_player);
    (void)mp_world_event_set_player(MP_WORLD_EVENT_EXPLODE_AT, &ws_test_player);
    (void)mp_world_event_set_player(MP_WORLD_EVENT_ZAP_ARCS, &ws_test_player);
}

void ws_census(void)
{
    enemy_sync_state_t *s = mp_enemy_sync_state();
    size_t              key;

    for (key = 0; key < MP_ENEMY_SYNC_KEYS; ++key) {
        placement_t *p = &s->placement[key];

        p->was_live = p->live;
        p->live     = ws.live[key];
        p->actor    = ws.live[key] ? ws_actor_of(key) : 0u;
        if (ws.live[key]) {
            mp_world_event_census_saw(ws_actor_of(key));
        }
    }
    if (ws.beside != 0u) {
        mp_world_event_census_saw(ws.beside);
    }
    s->send_ready = true;
    mp_enemy_sync_describe_census();
    for (key = 0; key < MP_ENEMY_SYNC_KEYS; ++key) {
        placement_t *p = &s->placement[key];

        if (!ws.live[key]) {
            continue;
        }
        p->current                              = s_records[key];
        p->current.value[MP_ENEMY_F_INDEX]      = mp_enemy_sync_wire_index(key);
        p->current.value[MP_ENEMY_F_GENERATION] = p->generation;
        p->current_ok                           = true;
    }
    mp_enemy_sync_set_interest(&SOURCE);
}

bool ws_post(uint8_t kind, size_t key, uint16_t a)
{
    return mp_world_event_post_at_actor(kind, ws_actor_of(key), a, NULL, 0u);
}

uint32_t ws_send_blocks(unsigned views, size_t capacity)
{
    uint32_t tick = ++s_tick;
    size_t   view;

    ws_census();
    for (view = 0; view < MP_ENEMY_SYNC_VIEWS; ++view) {
        ws.bytes[view] = 0u;
        if ((views & (1u << view)) == 0u) {
            continue;
        }
        (void)mp_enemy_sync_floor_bytes(view);
        if (mp_enemy_sync_encode_for(view, ws.block[view], capacity, &ws.bytes[view])) {
            mp_enemy_sync_sent_for(view, tick);
        } else {
            mp_enemy_sync_abandon_for(view);
        }
    }
    mp_world_event_census_done();
    return tick;
}

unsigned ws_events_in(size_t view)
{
    return ws.bytes[view] > WS_PART_AT ? ws.block[view][WS_PART_AT] : 0u;
}

void ws_client_takes(size_t view, uint32_t tick)
{
    size_t at = WS_PART_AT;

    if (mp_world_event_stage(ws.block[view], ws.bytes[view], &at)) {
        mp_world_event_take_staged(tick);
    }
}
