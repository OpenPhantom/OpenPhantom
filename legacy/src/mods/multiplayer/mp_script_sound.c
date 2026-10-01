/* mp_script_sound.c: the sounds an actor's script plays, at the one call its opcode makes, and the
 * host's side of them. See the header; a client's side is mp_script_sound_client.c.
 */
#include "mp_script_sound.h"

#include "mp_actor_loop.h"
#include "mp_bridge_drain.h"
#include "mp_bridge_far.h"
#include "mp_cells.h"
#include "mp_enemy_bind.h"
#include "mp_enemy_sync.h"
#include "mp_script_sound_internal.h"
#include "mp_script_sound_rule.h"
#include "mp_session.h"
#include "mp_session_now.h"
#include "mp_signatures.h"
#include "mp_signatures_script_sound.h"
#include "mp_sound.h"
#include "mp_target.h"
#include "mp_wire.h"
#include "mp_world_event.h"
#include "mp_world_event_rule.h"

#include "common/logging.h"
#include "common/memory.h"
#include "common/patch.h"
#include "common/signature.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

_Static_assert(MP_SCRIPT_SOUND_RECORD_BYTES == MP_SOUND_RECORD_BYTES,
               "the record the play routine indexes is the one the funnel copies");
_Static_assert(MP_SCRIPT_SOUND_RECORD_BYTES == 16u * sizeof(uint32_t),
               "a record is read whole into sixteen words");

/* The two music channels a peer's claim has, in the order the block's head carries them. */
enum { MUSIC_STATE = 0, MUSIC_SEQUENCE, MUSIC_CHANNELS };

typedef struct music_claim {
    bool     claimed;
    uint16_t call;
    uint32_t made;      /* the substep the call that made it ran in */
} music_claim_t;

typedef struct script_sound_state {
    bool                     installed;
    uintptr_t                call_site;
    mp_script_sound_engine_t engine;

    mp_script_sound_edges_t edges;
    music_claim_t           claim[MP_ENEMY_SYNC_VIEWS][MUSIC_CHANNELS];
    uint32_t                resets;        /* the enemy table's reset count the claims are of */
    bool                    resets_known;

    uint32_t calls;
    uint32_t by_host;
    uint32_t by_class[MP_SCRIPT_SOUND_CLASSES];
    uint32_t edge_posts;
    uint32_t again_posts;
    uint32_t held;
    uint32_t refused;          /* the world events took no event */
    uint32_t untold;           /* a sound played once while no census ran: nobody to tell */
    uint32_t no_memory;        /* of the edges, with no row to keep them in */
    uint32_t loop_other_cell;  /* a loop record whose handle was not the actor's own cell */
    uint32_t music_here;
    uint32_t music_withheld;
    uint32_t meant[MP_SCRIPT_MUSIC_REASONS];
    uint32_t claims;
    uint32_t left_to_engine;
    uint32_t withheld;         /* a client's own call for an actor of the host's */
    uint32_t let_through;      /* a client's own call for an actor of its own */
    uint32_t unreadable;
} script_sound_state_t;

static script_sound_state_t ss;

const mp_script_sound_engine_t *mp_script_sound_engine(void)
{
    return &ss.engine;
}

/* ==============================================================================================
 * The records.
 * ============================================================================================ */

bool mp_script_sound_record(uint32_t call, uint32_t record[16])
{
    uint32_t world = 0u;
    uint32_t count = 0u;
    uint32_t table = 0u;

    if (record == NULL || ss.engine.level_cell == 0u ||
        !memory_try_read(ss.engine.level_cell, &world, sizeof world) || world == 0u ||
        !memory_try_read(world + MP_SCRIPT_SOUND_LEVEL_CALL_COUNT, &count, sizeof count) ||
        call >= count ||
        !memory_try_read(world + MP_SCRIPT_SOUND_LEVEL_CALLS, &table, sizeof table) ||
        table == 0u) {
        return false;
    }
    return memory_try_read(table + call * MP_SCRIPT_SOUND_RECORD_BYTES, record,
                           MP_SCRIPT_SOUND_RECORD_BYTES);
}

uint32_t mp_script_sound_record_flags(const uint32_t record[16])
{
    return record[MP_SOUND_RECORD_FLAGS / sizeof(uint32_t)];
}

float mp_script_sound_record_reach(const uint32_t record[16])
{
    float reach;

    memcpy(&reach, &record[MP_SCRIPT_SOUND_RECORD_MAX_DIST / sizeof(uint32_t)], sizeof reach);
    return reach;
}

/* The host's own substep, the same count on every call inside one substep. */
static uint32_t now(void)
{
    return mp_bridge_drain_substeps();
}

/* Claims and edges are of one enemy table; its reset, which every way out of a level or a session
 * takes, forgets them. */
static void settle_reset(void)
{
    uint32_t resets = mp_enemy_sync_resets();

    if (ss.resets_known && ss.resets == resets) {
        return;
    }
    memset(&ss.edges, 0, sizeof ss.edges);
    memset(ss.claim, 0, sizeof ss.claim);
    ss.resets       = resets;
    ss.resets_known = true;
}

/* ==============================================================================================
 * The host: a sound played once.
 * ============================================================================================ */

/* An edge becomes a world event at the actor, heard where it is by the record's own reach. A call
 * held since the last substep makes none, and goes out again once a second while it is held. */
static void post_once(uintptr_t actor, uint32_t call, bool placed, const float at[3],
                      float reach)
{
    int32_t                row = -1;
    mp_script_sound_edge_t edge;
    bool                   posted;

    edge = mp_script_sound_edge_note(&ss.edges, actor, (uint16_t)call, now(), &row);
    if (edge == MP_SCRIPT_SOUND_HELD) {
        ++ss.held;
        return;
    }
    ss.no_memory += row < 0 ? 1u : 0u;
    posted = placed ? mp_world_event_post_heard(MP_WORLD_EVENT_SCRIPT_SOUND, actor,
                                                (uint16_t)call, at, reach)
                    : mp_world_event_post_at_actor(MP_WORLD_EVENT_SCRIPT_SOUND, actor,
                                                   (uint16_t)call, NULL, 0u);
    if (!posted) {
        ++ss.refused;
        return;
    }
    mp_script_sound_edge_out(&ss.edges, row, now());
    if (edge == MP_SCRIPT_SOUND_EDGE) {
        ++ss.edge_posts;
    } else {
        ++ss.again_posts;
    }
}

/* ==============================================================================================
 * The host: music.
 * ============================================================================================ */

/* The view whose peer's player the far bank `bank` shows, by the one slot rule. */
static bool view_of_bank(size_t bank, size_t *view)
{
    size_t v;

    for (v = 0; v < MP_ENEMY_SYNC_VIEWS; ++v) {
        if (mp_bridge_far_bank_of_slot(mp_session_slot_of_peer(v)) == bank) {
            *view = v;
            return true;
        }
    }
    return false;
}

/* Whom the music of `actor` is meant for, as bank bits: asked of the target memory the resolver
 * keeps, of where every player stands, and of the interest rule's near class. */
static uint8_t meant_for(uintptr_t actor, bool key_known, uint32_t key, bool placed,
                         const float at[3])
{
    mp_script_music_ask_t ask;
    mp_script_music_why_t why = MP_SCRIPT_MUSIC_FOR_EVERYBODY;
    float                 where[MP_SCRIPT_MUSIC_BANKS][3];
    bool                  stands[MP_SCRIPT_MUSIC_BANKS];
    size_t                bank;
    size_t                view = 0;
    uint8_t               meant;

    memset(&ask, 0, sizeof ask);
    memset(where, 0, sizeof where);
    ask.fresh    = MP_TARGET_AGGRO_SUBSTEPS;
    ask.answered = mp_target_last_answer(actor, &ask.answered_player, &ask.answered_bank,
                                         &ask.answered_age);
    ask.attacked = mp_target_last_attacker(actor, &ask.attacker_bank);
    ask.listeners = 0x01u;   /* this machine's own player hears on this machine */
    for (bank = 0; bank < MP_SCRIPT_MUSIC_BANKS; ++bank) {
        if (bank != 0u && view_of_bank(bank, &view)) {
            ask.listeners |= (uint8_t)(1u << bank);
        }
        stands[bank] = mp_target_player_position((uint8_t)bank, where[bank]);
        if (stands[bank] && key_known && mp_enemy_sync_wakes_for(key, where[bank])) {
            ask.awake_for |= (uint8_t)(1u << bank);
        }
    }
    ask.nearest = placed ? mp_script_music_nearest(at, where, stands, MP_SCRIPT_MUSIC_BANKS)
                         : MP_SCRIPT_MUSIC_NO_BANK;
    meant = mp_script_music_meant(&ask, &why);
    ++ss.meant[why];
    return meant;
}

/* Every peer the music is meant for holds it from this substep on; the host plays it itself only
 * when it is meant for the host's own player. */
static bool music(uintptr_t actor, bool key_known, uint32_t key, bool placed, const float at[3],
                  uint32_t call, mp_script_sound_class_t cls)
{
    uint8_t meant   = meant_for(actor, key_known, key, placed, at);
    size_t  channel = cls == MP_SCRIPT_SOUND_MUSIC_STATE ? MUSIC_STATE : MUSIC_SEQUENCE;
    size_t  bank;
    size_t  view = 0;

    for (bank = 1; bank < MP_SCRIPT_MUSIC_BANKS; ++bank) {
        if ((meant & (1u << bank)) == 0u || !view_of_bank(bank, &view)) {
            continue;
        }
        ss.claim[view][channel].claimed = true;
        ss.claim[view][channel].call    = (uint16_t)call;
        ss.claim[view][channel].made    = now();
        ++ss.claims;
    }
    if (mp_script_sound_host_plays(cls, (meant & 0x01u) != 0u)) {
        ++ss.music_here;
        return true;
    }
    ++ss.music_withheld;
    return false;
}

/* What a peer's block says of its music, asked for every block. */
static void music_for_view(size_t view, uint16_t *state, uint16_t *sequence)
{
    const music_claim_t *claim;

    settle_reset();
    if (view >= MP_ENEMY_SYNC_VIEWS || state == NULL || sequence == NULL) {
        return;
    }
    claim     = ss.claim[view];
    *state    = mp_script_music_held(claim[MUSIC_STATE].claimed, claim[MUSIC_STATE].call,
                                     claim[MUSIC_STATE].made, now());
    *sequence = mp_script_music_held(claim[MUSIC_SEQUENCE].claimed, claim[MUSIC_SEQUENCE].call,
                                     claim[MUSIC_SEQUENCE].made, now());
}

/* ==============================================================================================
 * The call.
 * ============================================================================================ */

/* A call the host handles: it plays what the engine would, music only when it is its own player's,
 * and says what it played. */
static void host_call(uint32_t call, int32_t *handle, const float *position, uintptr_t actor)
{
    uint32_t                record[16];
    uint32_t                key = 0u;
    float                   at[3] = { 0.0f, 0.0f, 0.0f };
    bool                    key_known;
    bool                    placed;
    mp_script_sound_class_t cls;

    ++ss.by_host;
    settle_reset();
    if (!mp_script_sound_record(call, record) || call > 0xFFFFu) {
        ++ss.unreadable;
        ss.engine.play_call(call, handle, position);
        return;
    }
    cls = mp_script_sound_classify(mp_script_sound_record_flags(record));
    ++ss.by_class[cls];
    key_known = mp_enemy_bind_index(actor, &key) && key < MP_WIRE_KEY_COUNT;
    placed    = memory_try_read(actor + MP_ACTOR_SOUND_POS, at, sizeof at);
    switch (cls) {
    case MP_SCRIPT_SOUND_ONCE:
        ss.engine.play_call(call, handle, position);
        if (!mp_enemy_sync_describing()) {
            ++ss.untold;   /* no peer yet, or no level: a moment nobody can be told of later */
            return;
        }
        post_once(actor, call, placed, at, mp_script_sound_record_reach(record));
        return;
    case MP_SCRIPT_SOUND_LOOP:
        ss.engine.play_call(call, handle, position);
        /* The opcode hands the actor's own cell for exactly the records with the loop bit; the
         * cell is what the channel is the actor's by. */
        if (handle != (int32_t *)(actor + MP_ACTOR_LOOP_HANDLE)) {
            ++ss.loop_other_cell;
            return;
        }
        mp_actor_loop_started(actor, key_known ? key : MP_WIRE_KEY_COUNT, (uint16_t)call);
        return;
    case MP_SCRIPT_SOUND_MUSIC_STATE:
    case MP_SCRIPT_SOUND_MUSIC_SEQUENCE:
    default:
        if (music(actor, key_known, key, placed, at, call, cls)) {
            ss.engine.play_call(call, handle, position);
        }
        return;
    }
}

/* The one call the sound opcode makes. Its single caller hands the actor's own position, so the
 * actor is that pointer less the position's offset; nothing else reaches this, because only this
 * call site is repointed and the routine's other two callers still call its head.
 *
 * engine: void bapsound_playCall(u32 soundcall, i32 *pHandle, const vec3 *pPos) */
static void __cdecl hook_script_sound(uint32_t call, int32_t *handle, const float *position)
{
    uintptr_t actor = (uintptr_t)position - MP_ACTOR_SOUND_POS;
    bool      runs  = false;
    bool      client;

    ++ss.calls;
    if (position == NULL) {
        ++ss.left_to_engine;
        ss.engine.play_call(call, handle, position);
        return;
    }
    client = mp_session_now_client_of_a_started_session(&runs, NULL);
    switch (mp_script_sound_route(runs && !client, client,
                                  mp_world_event_output_is_the_hosts(actor, client))) {
    case MP_SCRIPT_SOUND_BY_HOST:
        host_call(call, handle, position, actor);
        return;
    case MP_SCRIPT_SOUND_WITHHELD:
        ++ss.withheld;
        return;
    case MP_SCRIPT_SOUND_TO_ENGINE:
    default:
        if (client) {
            ++ss.let_through;
        } else {
            ++ss.left_to_engine;
        }
        ss.engine.play_call(call, handle, position);
        return;
    }
}

/* ==============================================================================================
 * The install.
 * ============================================================================================ */

static void say_what_did_not_resolve(void)
{
    size_t             count = 0;
    const signature_t *sites = mp_signatures_script_sound_sites(&count);
    size_t             i;

    for (i = 0; i < count; ++i) {
        if (sites[i].address == 0u) {
            log_warning("the scripts' sounds stay on the host: %s did not resolve, so a client "
                        "hears no sound, loop or music an actor's script plays", sites[i].name);
            return;
        }
    }
}

/* The call has to name the play routine its pattern found, and the world cell that routine reads
 * has to be the one the cell table resolved. */
static bool read_the_engine(void)
{
    uintptr_t target = 0u;
    uintptr_t cells  = mp_cells_address(MP_CELL_LEVEL);

    ss.call_site = mp_signatures_script_sound_call();
    if (ss.call_site == 0u || !patch_read_call_target(ss.call_site, &target) ||
        target != mp_signatures_script_sound_address(MP_SCRIPT_SOUND_SITE_PLAY_CALL) ||
        !mp_signatures_script_sound_level_cell(&ss.engine.level_cell) ||
        (cells != 0u && cells != ss.engine.level_cell)) {
        log_warning("the scripts' sounds stay on the host: the sound opcode's call at %08X names "
                    "%08X, the play routine was found at %08X, and its world cell %08X against "
                    "%08X resolved", (unsigned)ss.call_site, (unsigned)target,
                    (unsigned)mp_signatures_script_sound_address(MP_SCRIPT_SOUND_SITE_PLAY_CALL),
                    (unsigned)ss.engine.level_cell, (unsigned)cells);
        return false;
    }
    ss.engine.play_call = (mp_script_sound_play_call_fn_t)target;
    ss.engine.play = (mp_script_sound_play_fn_t)mp_signatures_address(MP_SITE_BAPSOUND_PLAY);
    ss.engine.pin  = (mp_script_sound_pin_fn_t)mp_signatures_script_sound_address(
        MP_SCRIPT_SOUND_SITE_PIN_CHANNEL);
    if (ss.engine.pin != NULL && !mp_signatures_script_sound_channel_bank(&ss.engine.bank)) {
        ss.engine.pin = NULL;
    }
    return true;
}

bool mp_script_sound_install(void)
{
    if (ss.installed) {
        return true;
    }
    (void)mp_signatures_script_sound_resolve();
    if (mp_signatures_script_sound_call() == 0u ||
        mp_signatures_script_sound_address(MP_SCRIPT_SOUND_SITE_PLAY_CALL) == 0u) {
        say_what_did_not_resolve();
        return false;
    }
    if (!read_the_engine()) {
        return false;
    }
    if (patch_redirect_call(ss.call_site, (const void *)&hook_script_sound) != PATCH_RESULT_OK) {
        log_error("the sound opcode's call at %08X did not move, so the scripts' sounds stay on "
                  "the host", (unsigned)ss.call_site);
        return false;
    }
    ss.installed = true;
    mp_world_event_set_music_source(&music_for_view);
    mp_script_sound_client_install();
    log_info("the scripts' sounds are bound at the sound opcode's call %08X, which plays through "
             "%08X: a host says each sound, loop and music its scripts play, a client plays them "
             "on its replicas%s", (unsigned)ss.call_site, (unsigned)(uintptr_t)ss.engine.play_call,
             ss.engine.pin != NULL ? "" : "; the pin routine did not resolve, so a loop whose "
                                          "replica is removed keeps its cell as its owner");
    return true;
}

/* ==============================================================================================
 * The report.
 * ============================================================================================ */

void mp_script_sound_report(void)
{
    if (!ss.installed) {
        log_info("the scripts' sounds: the site is not bound on this side, so every sound an "
                 "actor's script plays is heard on the machine that ran the script alone");
        return;
    }
    log_info("the scripts' sounds (host): %u call(s) at the script's sound site, %u handled here "
             "(%u once, %u loop, %u music), %u event(s) posted (%u on an edge, %u again after a "
             "hold of %u substeps), %u held as the same actor's same call in the next substep, %u "
             "the world events refused, %u edge(s) with no memory left, %u with no peer to tell; "
             "music on this host: %u played here, %u withheld (not this player's), meant for the "
             "answer %u, the attacker %u, everybody %u, the nearest %u, %u claim(s) for a peer; %u "
             "loop(s) not on the actor's own cell, %u record(s) that did not read, %u left to the "
             "engine (no session)",
             (unsigned)ss.calls, (unsigned)ss.by_host,
             (unsigned)ss.by_class[MP_SCRIPT_SOUND_ONCE],
             (unsigned)ss.by_class[MP_SCRIPT_SOUND_LOOP],
             (unsigned)(ss.by_class[MP_SCRIPT_SOUND_MUSIC_STATE] +
                        ss.by_class[MP_SCRIPT_SOUND_MUSIC_SEQUENCE]),
             (unsigned)(ss.edge_posts + ss.again_posts), (unsigned)ss.edge_posts,
             (unsigned)ss.again_posts, (unsigned)MP_SCRIPT_SOUND_HELD_REPEAT, (unsigned)ss.held,
             (unsigned)ss.refused, (unsigned)ss.no_memory, (unsigned)ss.untold,
             (unsigned)ss.music_here,
             (unsigned)ss.music_withheld, (unsigned)ss.meant[MP_SCRIPT_MUSIC_FOR_THE_ANSWER],
             (unsigned)ss.meant[MP_SCRIPT_MUSIC_FOR_THE_ATTACKER],
             (unsigned)ss.meant[MP_SCRIPT_MUSIC_FOR_EVERYBODY],
             (unsigned)ss.meant[MP_SCRIPT_MUSIC_FOR_THE_NEAREST], (unsigned)ss.claims,
             (unsigned)ss.loop_other_cell, (unsigned)ss.unreadable, (unsigned)ss.left_to_engine);
    log_info("the scripts' sounds (this side's own scripts in a session): %u call(s) withheld for "
             "an actor whose life the host describes, %u let through for an actor of this side's "
             "own", (unsigned)ss.withheld, (unsigned)ss.let_through);
    mp_script_sound_client_report();
    mp_actor_loop_report();
}
