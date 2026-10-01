/* mp_script_sound.c: the scripts' sounds on the engine, both halves, over an engine the test plays.
 *
 * SIZE NOTE: over six hundred lines, more than half of them the engine and the rest of the
 * multiplayer the two files call, each answered here. The seam, if this grows, is that engine
 * into a stand-in file of its own, as the voice's tests keep theirs; the checks are about two
 * hundred lines.
 *
 * The real modules run here: the call's hull and the host's handling of it, a client's player of
 * the host's sounds, its music, its hand for command 17, and the loops' engine it hands to the
 * loops, which run for real too. What they call is played in memory: the sound opcode's call site,
 * a call written into this process's own image so the install repoints it the way it repoints the
 * engine's; the play routine, the funnel and the pin; a level with five sound call records; a bank
 * of twelve channels laid out as the engine's, each with its sound reference and its owner's
 * cell; the actors; the census's answers; the session's role; the world events, whose posts and
 * players are recorded and whose one question for a client's script outputs is answered from the
 * census's replicas; and the director.
 *
 * What would be silent if it were wrong:
 *
 *   a host that stops playing its own sounds, or posts one of them every substep it is held;
 *   a loop the host's engine started that never reaches its table, or one from another cell;
 *   a host that plays the music of a fight a peer is in, or a peer that is never claimed for it;
 *   a client that plays its own script's sound for an actor whose life the host describes;
 *   a sound the host said, played at no place, at the wrong place, or counted as started when the
 *   engine refused it;
 *   command 17 of a client's own script ending a loop that is the host's;
 *   a loop the replica's own script had started, played a second time into the same cell.
 */
#include "unittest.h"

#include "mp_actor_loop.h"
#include "mp_bridge_drain.h"
#include "mp_bridge_far.h"
#include "mp_cells.h"
#include "mp_director_rule.h"
#include "mp_enemy_bind.h"
#include "mp_enemy_sync.h"
#include "mp_enemy_wire.h"
#include "mp_level_state_bind.h"
#include "mp_level_state_rule.h"
#include "mp_script_sound.h"
#include "mp_script_sound_rule.h"
#include "mp_session.h"
#include "mp_session_now.h"
#include "mp_signatures.h"
#include "mp_signatures_script_sound.h"
#include "mp_target.h"
#include "mp_world_door.h"
#include "mp_world_event.h"
#include "mp_world_event_rule.h"

#include "common/host_image.h"
#include "common/logging.h"
#include "common/patch.h"
#include "common/text.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* The records of the test's level: a sound played once, two loops, and the two kinds of music. */
enum { CALL_ONCE = 0, CALL_STEAM, CALL_STATE, CALL_HUM, CALL_SEQUENCE, CALLS };

#define RECORD_NAME    0x00u
#define RECORD_FLAGS   0x18u
#define RECORD_REACH   0x2Cu
#define REF_NAME       0x04u
#define REF_BYTES      0x3Cu
#define STATIC_POS     0x20u
#define ACTORS         4u
#define ACTOR_BYTES    0x100u
#define EVENTS_KEPT    16u

typedef struct posted {
    uint8_t   kind;
    uintptr_t actor;
    uint16_t  a;
    bool      placed;
    float     at[3];
    float     reach;
} posted_t;

typedef struct called {
    uint32_t     call;
    int32_t     *handle;
    const float *position;
} called_t;

typedef struct engine {
    uint8_t  call_site[8];
    uint8_t  world[0xD00];
    uint8_t  records[CALLS][0x40];
    uint8_t  refs[CALLS][REF_BYTES];
    uint8_t  bank[MP_SCRIPT_SOUND_CHANNELS][MP_SCRIPT_SOUND_CHANNEL_STRIDE];
    uint32_t level_cell;
    uint8_t  actor[ACTORS][ACTOR_BYTES];
    bool     admit;

    called_t calls[64];
    size_t   call_count;
    uint32_t funnel_flags;
    float    funnel_at[3];
    size_t   funnel_count;

    bool      runs;
    bool      client;
    bool      describing;
    uint32_t  substep;
    uint8_t   life[ACTORS];
    uintptr_t replica[ACTORS];
    size_t    peer_bank[4];   /* by slot */
    bool      answered;
    uint8_t   answered_bank;

    posted_t posts[EVENTS_KEPT];
    size_t   post_count;
    uint16_t heard_state;
    int32_t  directed[8];
    size_t   directed_count;

    mp_world_event_music_fn_t  music_source;
    mp_world_event_player_fn_t player;
    mp_world_door_hand_fn_t    stop_hand;
} engine_t;

static engine_t eng;

static uintptr_t actor(size_t i)
{
    return (uintptr_t)eng.actor[i];
}

static int32_t *cell(size_t i)
{
    return (int32_t *)(void *)(eng.actor[i] + MP_ACTOR_LOOP_HANDLE);
}

static float *place_of(size_t i)
{
    return (float *)(void *)(eng.actor[i] + MP_ACTOR_SOUND_POS);
}

static uint8_t *channel(int32_t ch)
{
    return eng.bank[ch];
}

static void put_u32(uint8_t *at, uint32_t value)
{
    memcpy(at, &value, sizeof value);
}

static uint32_t at_u32(const uint8_t *at)
{
    uint32_t value;

    memcpy(&value, at, sizeof value);
    return value;
}

/* ==============================================================================================
 * The log, kept so the report can be read back; every entry of common/logging.
 * ============================================================================================ */

#define LINES_KEPT 64u
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

/* The number written right before `marker` in the newest line that begins with `prefix`, after a
 * fresh report; -1 when there is none. */
static long reported(const char *prefix, const char *marker)
{
    size_t      i;
    const char *line = NULL;
    const char *at;
    const char *digits;

    mp_script_sound_report();
    for (i = 0; i < LINES_KEPT && i < kept_count && line == NULL; ++i) {
        const char *candidate = kept[(kept_count - 1u - i) % LINES_KEPT];

        line = strncmp(candidate, prefix, strlen(prefix)) == 0 ? candidate : NULL;
    }
    at = line != NULL ? strstr(line, marker) : NULL;
    if (at == NULL) {
        return -1;
    }
    for (digits = at; digits > line && digits[-1] >= '0' && digits[-1] <= '9'; --digits) {
    }
    return digits == at ? -1 : strtol(digits, NULL, 10);
}

#define HOST_LINE   "the scripts' sounds (host):"
#define OWN_LINE    "the scripts' sounds (this side's own scripts in a session):"
#define CLIENT_LINE "the scripts' sounds (client):"
#define LOOPS_LINE  "the actors' loops (client):"

/* ==============================================================================================
 * The engine's three routines.
 * ============================================================================================ */

static int32_t free_channel(void)
{
    int32_t ch;

    for (ch = 0; ch < MP_SCRIPT_SOUND_CHANNELS; ++ch) {
        if (at_u32(channel(ch) + MP_SCRIPT_SOUND_CHANNEL_REF) == 0u) {
            return ch;
        }
    }
    return -1;
}

/* The start the engine makes for a record: a channel with the record's sound reference, the
 * caller's cell as its owner, and its number in that cell; -1 in the cell when the gate refuses. */
static int32_t start_channel(uint32_t call, int32_t *handle)
{
    int32_t ch = eng.admit ? free_channel() : -1;

    if (handle != NULL) {
        *handle = ch;
    }
    if (ch >= 0) {
        put_u32(channel(ch) + MP_SCRIPT_SOUND_CHANNEL_REF, (uint32_t)(uintptr_t)eng.refs[call]);
        put_u32(channel(ch) + MP_SCRIPT_SOUND_CHANNEL_OWNER, (uint32_t)(uintptr_t)handle);
    }
    return ch;
}

static void __cdecl fake_play_call(uint32_t call, int32_t *handle, const float *position)
{
    if (eng.call_count < sizeof eng.calls / sizeof eng.calls[0]) {
        eng.calls[eng.call_count].call     = call;
        eng.calls[eng.call_count].handle   = handle;
        eng.calls[eng.call_count].position = position;
        ++eng.call_count;
    }
    if (call < CALLS && handle != NULL) {
        (void)start_channel(call, handle);
    }
}

static int32_t __cdecl fake_play(const void *record, int32_t *handle, const float *position)
{
    eng.funnel_flags = at_u32((const uint8_t *)record + RECORD_FLAGS);
    memcpy(eng.funnel_at, position, sizeof eng.funnel_at);
    ++eng.funnel_count;
    return start_channel(CALL_ONCE, handle);
}

static void __cdecl fake_pin(uint32_t ch, const float *position)
{
    (void)ch;
    (void)position;
}

/* The director's command 17 on an actor: the channel its cell names is freed, and the engine's
 * free writes -1 through the owner. */
bool mp_level_state_bind_call_director(uintptr_t who, int32_t command, int32_t a1, int32_t a2)
{
    int32_t ch = *(int32_t *)(who + MP_ACTOR_LOOP_HANDLE);

    (void)a1;
    (void)a2;
    if (eng.directed_count < sizeof eng.directed / sizeof eng.directed[0]) {
        eng.directed[eng.directed_count++] = command;
    }
    if (command == 17 && ch >= 0 && ch < MP_SCRIPT_SOUND_CHANNELS) {
        int32_t *owner = (int32_t *)(uintptr_t)at_u32(channel(ch) + MP_SCRIPT_SOUND_CHANNEL_OWNER);

        if (owner != NULL) {
            *owner = -1;
        }
        memset(channel(ch), 0, MP_SCRIPT_SOUND_CHANNEL_STRIDE);
    }
    return true;
}

/* ==============================================================================================
 * The sites, the cells and the rest of the multiplayer, as this test answers them.
 * ============================================================================================ */

static const signature_t NO_SITES[MP_SCRIPT_SOUND_SITE_COUNT] = { { NULL, NULL, NULL, 0u, 0u,
                                                                      0u } };

size_t mp_signatures_script_sound_resolve(void)
{
    return MP_SCRIPT_SOUND_SITE_COUNT;
}

uintptr_t mp_signatures_script_sound_call(void)
{
    return (uintptr_t)eng.call_site;
}

uintptr_t mp_signatures_script_sound_address(mp_script_sound_site_t site)
{
    switch (site) {
    case MP_SCRIPT_SOUND_SITE_PLAY_CALL:   return (uintptr_t)&fake_play_call;
    case MP_SCRIPT_SOUND_SITE_PIN_CHANNEL: return (uintptr_t)&fake_pin;
    default:                               return 0u;
    }
}

bool mp_signatures_script_sound_level_cell(uintptr_t *out)
{
    *out = (uintptr_t)&eng.level_cell;
    return true;
}

bool mp_signatures_script_sound_channel_bank(uintptr_t *out)
{
    *out = (uintptr_t)eng.bank;
    return true;
}

const signature_t *mp_signatures_script_sound_sites(size_t *count)
{
    *count = MP_SCRIPT_SOUND_SITE_COUNT;
    return NO_SITES;
}

uintptr_t mp_signatures_address(mp_site_t site)
{
    return site == MP_SITE_BAPSOUND_PLAY ? (uintptr_t)&fake_play : 0u;
}

uintptr_t mp_cells_address(mp_cell_t cell)
{
    return cell == MP_CELL_LEVEL ? (uintptr_t)&eng.level_cell : 0u;
}

bool mp_session_now_client_of_a_started_session(bool *runs, uint8_t *generation)
{
    if (runs != NULL) {
        *runs = eng.runs;
    }
    if (generation != NULL) {
        *generation = 1u;
    }
    return eng.client;
}

uint8_t mp_session_slot_of_peer(size_t index)
{
    return (uint8_t)(index + 1u);
}

size_t mp_bridge_far_bank_of_slot(uint8_t slot)
{
    return slot < 4u ? eng.peer_bank[slot] : 0u;
}

uint32_t mp_bridge_drain_substeps(void)
{
    return eng.substep;
}

bool mp_target_last_answer(uintptr_t who, bool *player, uint8_t *bank, uint32_t *age)
{
    (void)who;
    *player = true;
    *bank   = eng.answered_bank;
    *age    = 0u;
    return eng.answered;
}

bool mp_target_last_attacker(uintptr_t who, uint8_t *bank)
{
    (void)who;
    (void)bank;
    return false;
}

bool mp_target_player_position(uint8_t bank, float out[3])
{
    (void)bank;
    out[0] = out[1] = out[2] = 0.0f;
    return false;
}

bool mp_enemy_bind_index(uintptr_t who, uint32_t *out)
{
    size_t i;

    for (i = 0; i < ACTORS; ++i) {
        if (who == actor(i)) {
            *out = (uint32_t)i;
            return true;
        }
    }
    return false;
}

bool mp_enemy_slot_is_actor(mp_enemy_slot_t slot)
{
    return slot == MP_ENEMY_SLOT_LIVE || slot == MP_ENEMY_SLOT_KEPT;
}

bool mp_enemy_sync_describing(void)
{
    return eng.describing;
}

uint32_t mp_enemy_sync_resets(void)
{
    return 1u;
}

bool mp_enemy_sync_wakes_for(uint32_t key, const float position[3])
{
    (void)key;
    (void)position;
    return false;
}

bool mp_enemy_sync_generation(uint32_t key, uint8_t *out)
{
    if (key >= ACTORS || eng.life[key] == 0u) {
        return false;
    }
    *out = eng.life[key];
    return true;
}

uintptr_t mp_enemy_sync_actor_for(uint32_t key)
{
    return key < ACTORS && eng.life[key] != 0u ? actor(key) : 0u;
}

uintptr_t mp_enemy_sync_replica_for(uint32_t key)
{
    return key < ACTORS ? eng.replica[key] : 0u;
}

mp_enemy_slot_t mp_enemy_sync_slot(uint32_t key, uintptr_t who, uint8_t generation)
{
    (void)generation;
    return key < ACTORS && who == eng.replica[key] ? MP_ENEMY_SLOT_LIVE : MP_ENEMY_SLOT_FREED;
}

bool mp_world_event_post_heard(uint8_t kind, uintptr_t who, uint16_t a, const float place[3],
                               float radius)
{
    posted_t *p;

    if (eng.post_count >= EVENTS_KEPT) {
        return false;
    }
    p         = &eng.posts[eng.post_count++];
    p->kind   = kind;
    p->actor  = who;
    p->a      = a;
    p->placed = true;
    memcpy(p->at, place, sizeof p->at);
    p->reach = radius;
    return true;
}

bool mp_world_event_post_at_actor(uint8_t kind, uintptr_t who, uint16_t a, const uint8_t *tail,
                                  size_t tail_bytes)
{
    (void)tail;
    (void)tail_bytes;
    if (eng.post_count >= EVENTS_KEPT) {
        return false;
    }
    memset(&eng.posts[eng.post_count], 0, sizeof eng.posts[0]);
    eng.posts[eng.post_count].kind  = kind;
    eng.posts[eng.post_count].actor = who;
    eng.posts[eng.post_count].a     = a;
    ++eng.post_count;
    return true;
}

void mp_world_event_set_music_source(mp_world_event_music_fn_t source)
{
    eng.music_source = source;
}

bool mp_world_event_set_player(uint8_t kind, mp_world_event_player_fn_t player)
{
    if (kind == MP_WORLD_EVENT_SCRIPT_SOUND) {
        eng.player = player;
    }
    return true;
}

void mp_world_event_music_heard(uint16_t *state, uint16_t *sequence)
{
    *state    = eng.heard_state;
    *sequence = 0u;
}

/* The one question every script output of a client asks, answered as the world events answer it:
 * the actor is the replica this side holds for a key whose life the host described. The rule
 * behind it is the real one. */
bool mp_world_event_output_is_the_hosts(uintptr_t who, bool client_of_started)
{
    bool    described = false;
    uint8_t life      = 0u;
    size_t  key;

    for (key = 0; client_of_started && who != 0u && key < ACTORS; ++key) {
        if (eng.replica[key] == who) {
            described = mp_enemy_sync_generation((uint32_t)key, &life);
            break;
        }
    }
    return mp_script_sound_output_is_the_hosts(client_of_started, described);
}

bool mp_world_door_hand(mp_director_class_t cls, mp_world_door_hand_fn_t hand, const char *name)
{
    (void)name;
    if (cls == MP_DIRECTOR_SOUND_STOP) {
        eng.stop_hand = hand;
    }
    return true;
}

/* ==============================================================================================
 * The level, and the call site in this process's own image.
 * ============================================================================================ */

static void record(uint32_t call, const char *name, uint32_t flags, float reach)
{
    memset(eng.records[call], 0, sizeof eng.records[call]);
    memcpy(eng.records[call] + RECORD_NAME, name, strlen(name));
    put_u32(eng.records[call] + RECORD_FLAGS, flags);
    memcpy(eng.records[call] + RECORD_REACH, &reach, sizeof reach);
    memset(eng.refs[call], 0, sizeof eng.refs[call]);
    memcpy(eng.refs[call] + REF_NAME, name, strlen(name));
}

static void build_the_engine(void)
{
    uint32_t displacement;

    memset(&eng, 0, sizeof eng);
    record(CALL_ONCE, "drdfall4", 0u, 24.0f);
    record(CALL_STEAM, "steamlp", 0x10u, 30.0f);
    record(CALL_STATE, "fight", 0x8u, 0.0f);
    record(CALL_HUM, "airdroid", 0x10u, 30.0f);
    record(CALL_SEQUENCE, "sting", 0x400u, 0.0f);
    put_u32(eng.world + MP_SCRIPT_SOUND_LEVEL_CALL_COUNT, CALLS);
    put_u32(eng.world + MP_SCRIPT_SOUND_LEVEL_CALLS, (uint32_t)(uintptr_t)eng.records);
    eng.level_cell = (uint32_t)(uintptr_t)eng.world;
    eng.admit      = true;
    eng.call_site[0] = 0xE8u;
    displacement = (uint32_t)((uintptr_t)&fake_play_call - ((uintptr_t)eng.call_site + 5u));
    memcpy(eng.call_site + 1, &displacement, sizeof displacement);
}

/* Fresh actors and a fresh bank, everything else of the engine left as the install set it. */
static void fresh(void)
{
    size_t i;

    memset(eng.actor, 0, sizeof eng.actor);
    memset(eng.bank, 0, sizeof eng.bank);
    for (i = 0; i < ACTORS; ++i) {
        float at[3] = { 10.0f * (float)i, 5.0f, 7.0f };

        *cell(i) = -1;
        memcpy(place_of(i), at, sizeof at);
    }
    memset(eng.life, 0, sizeof eng.life);
    memset(eng.replica, 0, sizeof eng.replica);
    memset(eng.peer_bank, 0, sizeof eng.peer_bank);
    eng.call_count     = 0u;
    eng.funnel_count   = 0u;
    eng.post_count     = 0u;
    eng.directed_count = 0u;
    eng.answered       = false;
    eng.admit          = true;
    eng.heard_state    = 0u;
}

typedef void(__cdecl *hook_fn_t)(uint32_t call, int32_t *handle, const float *position);

static hook_fn_t s_hook;

static void script_calls(uint32_t call, int32_t *handle, size_t who)
{
    s_hook(call, handle, place_of(who));
}

/* ==============================================================================================
 * The checks.
 * ============================================================================================ */

static void check_the_install(void)
{
    uintptr_t target = 0u;

    ut_section("the install repoints the sound opcode's call and hands every part its engine");
    build_the_engine();
    ut_check(mp_script_sound_install(), "the call names the play routine, and it is repointed");
    ut_check(patch_read_call_target((uintptr_t)eng.call_site, &target) &&
                 target != (uintptr_t)&fake_play_call,
             "the call no longer reaches the play routine directly");
    s_hook = (hook_fn_t)target;
    ut_check(eng.music_source != NULL && eng.player != NULL && eng.stop_hand != NULL,
             "the world events have the music and the player, the director the hand for 17");
}

static void check_the_host(void)
{
    int32_t               once_cell = -1;
    mp_level_state_note_t note;
    uint16_t              state    = 0u;
    uint16_t              sequence = 0u;

    ut_section("with no session every call is the engine's, and nothing is said");
    fresh();
    script_calls(CALL_ONCE, &once_cell, 0u);
    ut_check(eng.call_count == 1u && eng.calls[0].call == CALL_ONCE &&
                 eng.calls[0].handle == &once_cell && eng.calls[0].position == place_of(0u) &&
                 eng.post_count == 0u,
             "the engine plays it with the script's own arguments");

    ut_section("the host plays what it played, and says a sound played once at its edge");
    fresh();
    eng.runs       = true;
    eng.describing = true;
    eng.substep    = 100u;
    script_calls(CALL_ONCE, &once_cell, 0u);
    ut_checkf(eng.call_count == 1u && eng.post_count == 1u &&
                  eng.posts[0].kind == MP_WORLD_EVENT_SCRIPT_SOUND &&
                  eng.posts[0].actor == actor(0u) && eng.posts[0].a == CALL_ONCE &&
                  eng.posts[0].at[0] == 0.0f && eng.posts[0].at[2] == 7.0f &&
                  eng.posts[0].reach == 24.0f,
              "played, and posted at the actor's place with the record's reach (%u post(s))",
              (unsigned)eng.post_count);
    eng.substep = 101u;
    script_calls(CALL_ONCE, &once_cell, 0u);
    ut_check(eng.call_count == 2u && eng.post_count == 1u,
             "the same call in the next substep is held: played again, not said again");
    eng.substep = 104u;
    script_calls(CALL_ONCE, &once_cell, 0u);
    ut_check(eng.post_count == 2u, "after a pause it is an edge again");
    eng.describing = false;
    eng.substep    = 110u;
    script_calls(CALL_ONCE, &once_cell, 0u);
    ut_check(eng.post_count == 2u && reported(HOST_LINE, " with no peer to tell") >= 1,
             "with no census running there is nobody to tell, and it is counted");

    ut_section("the host's loops go into its table only from the actor's own cell");
    eng.describing = true;
    script_calls(CALL_STEAM, cell(0u), 0u);
    eng.life[0] = 1u;
    mp_level_state_note_init(&note, 1u, 57u, 1u);
    mp_actor_loop_write(&note);
    ut_check(*cell(0u) >= 0 && note.loops == 1u && note.loop[0].key == 0u &&
                 note.loop[0].call == CALL_STEAM,
             "the engine started it in the actor's cell, and the note says it");
    script_calls(CALL_HUM, &once_cell, 1u);
    ut_check(reported(HOST_LINE, " loop(s) not on the actor's own cell") >= 1,
             "a loop record handed another cell is played and counted, not put on record");

    ut_section("the host plays its scripts' music only when it is meant for its own player");
    fresh();
    eng.runs       = true;
    eng.describing = true;
    eng.substep    = 200u;
    script_calls(CALL_STATE, NULL, 0u);
    ut_check(eng.call_count == 1u, "with no peer it is everybody's, and the host's own");
    eng.peer_bank[1]  = 1u;   /* peer 0's player is far bank 1 */
    eng.answered      = true;
    eng.answered_bank = 1u;
    script_calls(CALL_STATE, NULL, 0u);
    ut_check(eng.call_count == 1u, "the script's answer is the peer's player: the host plays none");
    eng.music_source(0u, &state, &sequence);
    ut_check(state == CALL_STATE && sequence == 0u, "and peer 0's blocks claim it");
    eng.substep = 210u;
    eng.music_source(0u, &state, &sequence);
    ut_check(state == 0u, "for ten substeps after the last call, and no longer");
}

static void check_a_clients_own_scripts(void)
{
    int32_t once_cell = -1;

    ut_section("a client's own script is silent for an actor whose life the host describes");
    fresh();
    eng.runs       = true;
    eng.client     = true;
    eng.life[0]    = 1u;
    eng.replica[0] = actor(0u);   /* the replica this side holds for the host's life */
    script_calls(CALL_ONCE, &once_cell, 0u);
    ut_check(eng.call_count == 0u, "the host's life: withheld, the host says it");
    script_calls(CALL_ONCE, &once_cell, 1u);
    ut_check(eng.call_count == 1u, "an actor the host never described plays its own");
    ut_check(eng.stop_hand(eng.actor[0], 17, 0, 0) && !eng.stop_hand(eng.actor[1], 17, 0, 0),
             "command 17 of its own script is withheld for the host's life, and not otherwise");
    eng.client = false;
    ut_check(!eng.stop_hand(eng.actor[0], 17, 0, 0), "on the host it goes to the engine");
}

static void check_the_client_plays(void)
{
    mp_world_event_t event;
    uint32_t         packed = 0u;
    long             refused;

    ut_section("a client plays the host's sound at the replica, or at the place the host named");
    fresh();
    eng.runs   = true;
    eng.client = true;
    memset(&event, 0, sizeof event);
    event.kind = MP_WORLD_EVENT_SCRIPT_SOUND;
    event.a    = CALL_ONCE;
    ut_check(eng.player(&event, actor(2u)) && eng.call_count == 1u &&
                 eng.calls[0].position == place_of(2u) && eng.calls[0].handle != NULL &&
                 eng.calls[0].handle != cell(2u),
             "at the replica through the engine's call, with a cell of the module's own");
    refused    = reported(CLIENT_LINE, " refused by the engine's start gate");
    eng.admit  = false;
    (void)eng.player(&event, actor(2u));
    ut_check(reported(CLIENT_LINE, " refused by the engine's start gate") == refused + 1,
             "a start the gate refuses is counted as refused, not started");
    eng.admit = true;

    event.has_place = true;
    (void)mp_enemy_wire_put_position(40.0f, &packed);
    event.place[0] = (uint16_t)packed;
    event.place[1] = (uint16_t)packed;
    event.place[2] = (uint16_t)packed;
    ut_check(eng.player(&event, 0u) && eng.funnel_count == 1u &&
                 (eng.funnel_flags & STATIC_POS) != 0u && eng.funnel_at[0] > 39.0f &&
                 eng.funnel_at[0] < 41.0f,
             "with no replica, through the funnel at the place, the record copied with a fixed "
             "place");
    event.a = CALL_STEAM;
    ut_check(!eng.player(&event, actor(2u)) && eng.call_count == 2u,
             "a call that is not a sound played once here plays nothing");

    ut_section("a client asks the engine for the music the host holds, in every substep");
    eng.heard_state = CALL_STATE;
    mp_script_sound_flush();
    mp_script_sound_flush();
    ut_check(eng.call_count == 4u && eng.calls[3].call == CALL_STATE &&
                 eng.calls[3].handle == NULL && eng.calls[3].position != NULL,
             "twice, never with a cell and never without a place");
    eng.heard_state = CALL_STEAM;
    mp_script_sound_flush();
    ut_check(eng.call_count == 4u, "a call that is not music here is not played as music");
}

static void check_the_cell_of_a_replica(void)
{
    mp_level_state_note_t note;
    long                  taken;
    long                  stopped;

    ut_section("the loops' engine reads the replica's cell the way the engine keeps it");
    fresh();
    eng.runs    = true;
    eng.client  = true;
    eng.life[2] = 1u;
    eng.life[3] = 1u;
    eng.replica[2] = actor(2u);
    eng.replica[3] = actor(3u);
    (void)start_channel(CALL_STEAM, cell(2u));   /* its own script ran before it was parked */
    (void)start_channel(CALL_HUM, cell(3u));
    taken   = reported(LOOPS_LINE, " taken over where");
    stopped = reported(LOOPS_LINE, " of another sound");
    mp_level_state_note_init(&note, 1u, 57u, 1u);
    note.parts |= (uint8_t)MP_LEVEL_STATE_PART_ACTOR_LOOPS;
    note.loops = 2u;
    note.loop[0].key  = 2u;
    note.loop[0].life = 1u;
    note.loop[0].call = CALL_STEAM;
    note.loop[1].key  = 3u;
    note.loop[1].life = 1u;
    note.loop[1].call = CALL_STEAM;
    mp_actor_loop_take(&note);
    mp_script_sound_flush();
    ut_check(reported(LOOPS_LINE, " taken over where") == taken + 1 &&
                 eng.directed_count == 1u && eng.directed[0] == 17,
             "a channel of its own cell with the wanted call's sound is taken over; one with "
             "another sound is stopped with command 17");
    ut_check(reported(LOOPS_LINE, " of another sound") == stopped + 1 && eng.call_count == 1u &&
                 eng.calls[0].call == CALL_STEAM && eng.calls[0].handle == cell(3u) &&
                 *cell(3u) >= 0,
             "and the host's sound is then started in that cell, the only one it owns");
}

int main(void)
{
    ut_check(host_image_resolve(), "this process's image stands in for the game's");
    check_the_install();
    check_the_host();
    check_a_clients_own_scripts();
    check_the_client_plays();
    check_the_cell_of_a_replica();
    return ut_summary("the scripts' sounds on the engine");
}
