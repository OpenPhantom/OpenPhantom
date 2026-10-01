/* mp_sound.c: give a sound a place when the body that caused it is not the one listening.
 * See mp_sound.h. */
#include "mp_sound.h"

#include "mp_armed.h"
#include "mp_cells.h"
#include "mp_enemy_block.h"
#include "mp_signatures.h"

#include "common/detour.h"
#include "common/logging.h"
#include "common/memory.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The actor's own world position, the same three floats the target resolver measures with. */
#define ACTOR_POS 0xD0u

/* The prologues the two detours copy. Both are `push ebp / mov ebp,esp / sub esp,imm8`, which is
 * 55 / 8B EC / 83 EC xx: the sub is three bytes, so six is an instruction boundary and the five a
 * jump needs would cut it in half. */
#define SOUND_PLAY_PROLOGUE      6u
#define BLOCK_IMPACT_FX_PROLOGUE 6u

typedef int32_t(__cdecl *sound_play_fn_t)(const void *record, int32_t *handle,
                                          const float *position);
typedef void(__cdecl *block_impact_fx_fn_t)(uintptr_t actor, int32_t kind);

typedef struct sound_state {
    bool                 installed;
    detour_t             play_detour;
    detour_t             block_detour;
    sound_play_fn_t      play_original;
    block_impact_fx_fn_t block_original;

    mp_sound_anchor_t anchor;

    /* The point a re-pointed call is handed. One cell, because the engine copies it during the
     * call and never reads the pointer again; see the header. */
    float slot[3];

    uint32_t placed;       /* positionless sounds given the anchor's point */
    uint32_t out_of_range; /* of those, refused by the engine's own start-distance gate */
    uint32_t already_set;  /* sounds that carried a point of their own and were left alone */
    uint32_t unreadable;   /* a record the copy could not read; the call went through unchanged */
    uint32_t block_calls;                          /* every call, before any gate */
    uint32_t block_by_kind[MP_SOUND_BLOCK_KINDS];  /* 0 ricochet, 1 blade, 2 armour */
    uint32_t fights;       /* blade clangs given the blocking actor's place */
    uint32_t fights_lost;  /* of those, actors whose position did not read */

    /* The clang's cooldown cell, 0 when it did not resolve, and what it said of each call the
     * engine made in a session. */
    uintptr_t cooldown;
    uint32_t  block_let_through;
    uint32_t  block_held;
    uint32_t  block_unread;
    uint32_t  replayed;      /* a host's clang played again on a replica here */
    uint32_t  replay_moved;  /* of those, the cooldown moved as it does for a played clang */
} sound_state_t;

static sound_state_t sound;

/* ==============================================================================================
 * The decisions.
 * ============================================================================================ */

bool mp_sound_anchor_applies(bool anchor_held, const void *record, const float *position)
{
    return anchor_held && record != NULL && position == NULL;
}

uint32_t mp_sound_anchored_flags(uint32_t flags)
{
    return flags | MP_SOUND_FLAG_DISTANCE | MP_SOUND_FLAG_STATIC_POS;
}

/* ==============================================================================================
 * The engine half.
 * ============================================================================================ */

/* The funnel. Every route into audio ends here, and the argument that decides whether a sound has
 * a place at all is the third one. A call that brought its own point is the engine placing a body's
 * sound correctly and is none of this module's business.
 *
 * The record is copied rather than edited: one of the five callers hands over a pointer into the
 * loaded animation asset, and raising two bits there would change every later play of that event
 * on every body. The copy is 0x40 bytes because that is the whole record, `bapsound_playByName`
 * builds one of exactly that size on its own stack, and the channel keeps none of it, only the
 * fields it reads out during the call. */
static int32_t __cdecl hook_sound_play(const void *record, int32_t *handle, const float *position)
{
    uint32_t copy[MP_SOUND_RECORD_BYTES / sizeof(uint32_t)];
    int32_t  result;

    if (!mp_sound_anchor_applies(sound.anchor.active, record, position)) {
        if (sound.anchor.active && position != NULL) {
            ++sound.already_set;
        }
        return sound.play_original(record, handle, position);
    }
    if (!memory_try_read((uintptr_t)record, copy, sizeof copy)) {
        ++sound.unreadable;
        return sound.play_original(record, handle, position);
    }

    copy[MP_SOUND_RECORD_FLAGS / sizeof(uint32_t)] =
        mp_sound_anchored_flags(copy[MP_SOUND_RECORD_FLAGS / sizeof(uint32_t)]);

    sound.slot[0] = sound.anchor.position[0];
    sound.slot[1] = sound.anchor.position[1];
    sound.slot[2] = sound.anchor.position[2];

    result = sound.play_original(copy, handle, sound.slot);
    ++sound.placed;
    if (result < 0) {
        ++sound.out_of_range;
    }
    return result;
}

/* The one place the engine's clang is called in a session, by the hull and by a replay alike: the
 * cooldown cell read before and after the call, and the anchor around it. `at` NULL is no anchor,
 * which only the host's own call with a position that did not read may use; the window then
 * leaves whatever anchor was open around it standing. The cell, `[0x006C4CEC]` in retail, is one
 * float for every actor in the level: the engine plays only while it is at or below the world
 * clock, and re-arms it to the clock plus 0.2 s. */
static mp_enemy_block_gate_t clang(uintptr_t actor, int32_t kind, const float *at)
{
    mp_sound_anchor_t previous;
    uint32_t          before = 0;
    uint32_t          after = 0;
    bool              read_before;
    bool              read_after;

    read_before = sound.cooldown != 0u && memory_try_read(sound.cooldown, &before, sizeof before);
    mp_sound_anchor_open(at, &previous);
    sound.block_original(actor, kind);
    mp_sound_anchor_close(&previous);
    read_after = sound.cooldown != 0u && memory_try_read(sound.cooldown, &after, sizeof after);
    return mp_enemy_block_gate(read_before, before, read_after, after);
}

/* The blade clangs of every actor in the level, and the one member of the class the engine plays
 * on its own rather than through a call of ours. It is worth its own detour because it is the
 * loudest: a host hears a fight the client is having a hundred units away exactly as loudly as one
 * of its own, and the 0.2 second cooldown that rations the sound sits in a SINGLE cell for all
 * actors together, so two fights share one budget. That cell is [0x6C4CEC], and it is re-armed
 * whether or not the voice was admitted, so a far player's block still silences a nearby actor's
 * block for 0.2 seconds. That is unchanged by this detour, it happens today as well with the far
 * clang audible on top, and it is why the clang route cannot be made perfect from here.
 *
 * The anchor is the actor's own position rather than the weapon node the engine samples four lines
 * later for the spark. The node is the better point and the engine already has it, but it reads it
 * AFTER the sound, and the node resolver has two early returns that leave its out vector untouched,
 * so reading it early would mean either reordering two engine calls or anchoring at stack contents,
 * to buy a difference smaller than the near field.
 *
 * In a session every call also hands the cooldown's verdict to the enemy module, which stamps a
 * call it let through as an event on the actor's record when this side describes its enemies.
 *
 * engine: void npc_blockImpactFx(character *actor, int kind) */
static void __cdecl hook_block_impact_fx(uintptr_t actor, int32_t kind)
{
    mp_enemy_block_gate_t verdict;
    float                 at[3];
    bool                  placed;

    /* Counted before every gate below, including the one that leaves for single player,
     * because the question these three answer is whether the engine reaches this function
     * at all. `blade clang(s) placed` stands at zero in nearly every field log, host lines
     * included, and the parked state of a replica explains only the client half. If the
     * host never calls it either, then every effect hanging off it is invisible in coop and
     * nothing should be built for it. A replay never comes through here. */
    ++sound.block_calls;
    if (kind >= 0 && kind < (int32_t)MP_SOUND_BLOCK_KINDS) {
        ++sound.block_by_kind[kind];
    }
    if (!mp_armed_transport()) {
        sound.block_original(actor, kind);   /* single player hears the engine's own clang */
        return;
    }
    placed = actor != 0 && memory_try_read(actor + ACTOR_POS, at, sizeof at);
    if (placed) {
        ++sound.fights;
    } else {
        ++sound.fights_lost;
    }
    verdict = clang(actor, kind, placed ? at : NULL);
    sound.block_let_through += (verdict == MP_ENEMY_BLOCK_PASSED) ? 1u : 0u;
    sound.block_held += (verdict == MP_ENEMY_BLOCK_HELD) ? 1u : 0u;
    sound.block_unread += (verdict == MP_ENEMY_BLOCK_UNREAD) ? 1u : 0u;
    /* On a client this is a blocker the client runs itself, and the module keeps nothing: only a
     * side that describes its enemies stamps an event. */
    mp_enemy_block_note((uint32_t)actor, kind, verdict);
}

bool mp_sound_block_replayable(void)
{
    return sound.block_original != NULL && sound.cooldown != 0u;
}

bool mp_sound_block_replay(uintptr_t actor, int32_t kind)
{
    const uint32_t open = 0u;   /* the bits of 0.0f */
    uint32_t       kept = 0;
    float          at[3];

    if (!mp_sound_block_replayable() || actor == 0u ||
        !memory_try_read(actor + ACTOR_POS, at, sizeof at) ||
        !memory_try_read(sound.cooldown, &kept, sizeof kept) ||
        !memory_try_write(sound.cooldown, &open, sizeof open)) {
        return false;
    }
    ++sound.replayed;
    if (clang(actor, kind, at) == MP_ENEMY_BLOCK_PASSED) {
        ++sound.replay_moved;
    }
    (void)memory_try_write(sound.cooldown, &kept, sizeof kept);
    return true;
}

void mp_sound_anchor_open(const float position[3], mp_sound_anchor_t *previous)
{
    if (previous != NULL) {
        *previous = sound.anchor;
    }
    if (position == NULL) {
        return;
    }
    sound.anchor.position[0] = position[0];
    sound.anchor.position[1] = position[1];
    sound.anchor.position[2] = position[2];
    sound.anchor.active      = true;
}

void mp_sound_anchor_close(const mp_sound_anchor_t *previous)
{
    if (previous != NULL) {
        sound.anchor = *previous;
        return;
    }
    sound.anchor.active = false;
}

/* How many rows of the cell table name the cooldown, for the binding line; 0 when the cell did not
 * resolve. Every row of a resolved cell agreed, or it would not have resolved. */
static uint32_t cooldown_operands(void)
{
    const mp_operand_t *rows;
    size_t              count = 0;
    size_t              i;
    uint32_t            named = 0;

    if (sound.cooldown == 0u) {
        return 0u;
    }
    rows = mp_cells_operands(&count);
    for (i = 0; i < count; ++i) {
        named += (rows[i].cell == MP_CELL_BLOCK_FX_COOLDOWN) ? 1u : 0u;
    }
    return named;
}

bool mp_sound_install(void)
{
    uintptr_t play;
    uintptr_t block;

    if (sound.installed) {
        return true;
    }
    play  = mp_signatures_address(MP_SITE_BAPSOUND_PLAY);
    block = mp_signatures_address(MP_SITE_NPC_BLOCK_IMPACT_FX);
    if (play == 0) {
        log_warning("the sound funnel did not resolve, so a far player's sabre, weapon and impact "
                    "sounds keep playing at full volume across the whole level");
        return false;
    }
    if (!detour_install(&sound.play_detour, play, (const void *)&hook_sound_play,
                        SOUND_PLAY_PROLOGUE)) {
        log_error("the sound funnel at %08X refused the detour", (unsigned)play);
        return false;
    }
    sound.play_original = (sound_play_fn_t)sound.play_detour.original;
    sound.installed     = true;
    log_info("the sound funnel is bound at %08X: a sound played for a far body is given that "
             "body's place and the engine's own falloff", (unsigned)play);

    /* The clang route is a second, independent gain. Without it the funnel still repairs every
     * sound this feature causes itself, so a site that did not resolve costs that one route and
     * not the module. */
    if (block == 0) {
        log_warning("the blade clang site did not resolve: a fight fought far from this machine "
                    "will still be heard here at full volume");
        return true;
    }
    if (!detour_install(&sound.block_detour, block, (const void *)&hook_block_impact_fx,
                        BLOCK_IMPACT_FX_PROLOGUE)) {
        log_error("the blade clang site at %08X refused the detour", (unsigned)block);
        return true;
    }
    sound.block_original = (block_impact_fx_fn_t)sound.block_detour.original;
    sound.cooldown       = mp_cells_address(MP_CELL_BLOCK_FX_COOLDOWN);
    mp_enemy_block_set_replay(&mp_sound_block_replayable, &mp_sound_block_replay);
    log_info("the blade clangs are bound at %08X: a clang is heard where the fight is; its "
             "cooldown is the cell %08X, read from %u operand(s) that agree%s", (unsigned)block,
             (unsigned)sound.cooldown, (unsigned)cooldown_operands(),
             sound.cooldown != 0u ? "" : "; with no cell no clang travels from here and none "
                                         "from the host is replayed");
    return true;
}

bool mp_sound_installed(void)
{
    return sound.installed;
}

void mp_sound_reset(void)
{
    /* The anchor only, never the counters: the report is written at the end of a level and a
     * memset here would hand back zeroes for a session that did work. An anchor, on the other
     * hand, must not survive a level change, because the point it holds is in the old level. */
    sound.anchor.active = false;
}

void mp_sound_report(void)
{
    if (!sound.installed) {
        log_info("the sound funnel is not bound: every sound this feature causes plays at full "
                 "volume everywhere, as it does in the single player game");
        return;
    }
    /* Its own sentence, and its whole purpose is to answer one question before anything is built
     * on it: does the engine reach the blade clang at all on this machine? The clang's own
     * counter stands at zero in nearly every field log, host lines included, and a replica being
     * parked explains only the client half of that. */
    log_info("  the blade clang was called %u time(s): %u ricochet, %u blade on blade, "
             "%u blade on armour; the engine's cooldown let %u through, held %u, %u unread | "
             "replayed from the host: %u, %u of them moved the cooldown as a played clang does",
             (unsigned)sound.block_calls, (unsigned)sound.block_by_kind[0],
             (unsigned)sound.block_by_kind[1], (unsigned)sound.block_by_kind[2],
             (unsigned)sound.block_let_through, (unsigned)sound.block_held,
             (unsigned)sound.block_unread, (unsigned)sound.replayed,
             (unsigned)sound.replay_moved);
    log_info("sounds given a place: %u placed (%u of them beyond the record's own range and "
             "refused by the engine), %u already had one, %u record(s) unreadable; %u blade "
             "clang(s) placed, %u actor(s) whose position did not read",
             (unsigned)sound.placed, (unsigned)sound.out_of_range, (unsigned)sound.already_set,
             (unsigned)sound.unreadable, (unsigned)sound.fights, (unsigned)sound.fights_lost);
}
