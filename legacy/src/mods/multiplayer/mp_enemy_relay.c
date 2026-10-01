/* mp_enemy_relay.c: the host's removals, sent with their reason; the host's level seeing both
 * players. */
#include "mp_enemy_relay.h"

#include "mp_armed.h"
#include "mp_bank.h"
#include "mp_cells.h"
#include "mp_enemy_bind.h"
#include "mp_enemy_burst.h"
#include "mp_enemy_spawn.h"
#include "mp_enemy_sync.h"
#include "mp_events.h"
#include "mp_signatures.h"

#include "common/detour.h"
#include "common/logging.h"
#include "common/memory.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The placement an actor was made from, which is how a removal names the life it ends. The
 * offset is the enemy binding's. The state flags and the body are read for a burst: the body is
 * what the burst was noted by, and a flag says whether the actor carries the player's body. */
#define ACTOR_PLACEMENT      0x10u
#define ACTOR_STATE_FLAGS    0x14u
#define ACTOR_BODY           0x34u

/* The placement's deactivation radius and spawn state come from mp_cells.h: the removal test
 * compares the ACTOR's position against the PLACEMENT's radius, which is the engine's own
 * asymmetry, and the spawn state is what the removal leaves behind. */
#define PLACEMENT_SPAWN_STATE MP_PLACEMENT_SPAWN_STATE

/* push ebp; mov ebp,esp; sub esp,0xc: six bytes, one instruction boundary, no relative operand.
 * The head of the removal, the forty four bytes the pattern is cut from:
 *
 *     00437850  55 8B EC 83 EC 0C        push ebp; mov ebp,esp; sub esp,0xc
 *     00437856  8B 45 08 8B 48 14        mov eax,[ebp+8]; mov ecx,[eax+0x14]
 *     0043785C  81 E1 00 20 00 00        and ecx,0x2000              hosts the player?
 *     00437862  85 C9 74 0C              test ecx,ecx; je +0xc
 *     00437866  E8 rel32                 call player_resume          masked
 *     0043786B  C7 45 0C 03 00 00 00     mov [ebp+0xc],3             reason := 3
 *     00437872  8B 55 08 A1 abs32        mov edx,[ebp+8]; mov eax,[g_speakerLock]   masked
 *     0043787A  3B 42 34                 cmp eax,[edx+0x34]
 *
 * Two masked operands, the call distance and the speaker lock's address, and one match in each
 * of the three images: 00437850 in the retail link and the recompile, 00437848 in the alternate
 * link. The six callers all go `push reason; push actor; call; add esp,8` and read no return:
 * 0043203F, 00433299, 004332B2, 00433304, 00435BB6 and 00437710. */
#define ENEMY_DELETE_PROLOGUE 6u

/* __cdecl, proven at all six callers: push reason, push actor, call, add esp 8. No return. */
typedef void(__cdecl *enemy_delete_fn_t)(uintptr_t actor, int32_t reason);

typedef struct enemy_relay_state {
    bool                         installed;
    bool                         enabled;
    bool                         host;
    bool                         performing;   /* this module calling the removal on purpose */
    detour_t                     detour;
    enemy_delete_fn_t            original;
    mp_enemy_relay_send_fn_t     send;
    mp_enemy_relay_far_body_fn_t far_body;
    uint32_t                     tick;

    uint32_t sent;
    uint32_t unsent;
    uint32_t not_travelling;     /* removals for a reason that stays local */
    uint32_t performed;
    uint32_t performed_kept;     /* of them, removals that keep the body as a corpse */
    uint32_t freed_after_kept;   /* of them, a freeing removal on a body one kept earlier */
    uint32_t across_lives;       /* of them, on a corpse whose placement lives again */
    uint32_t no_actor;
    uint32_t already_gone;   /* the placement had no live actor left when the note arrived */
    uint32_t stale;              /* a despawn for a life this side no longer has */
    uint32_t other_level;
    uint32_t no_level;
    uint32_t refused;
    uint32_t dropped_on_host;
    uint32_t named;         /* removals reported one by one, capped */
    uint32_t repeated;      /* removals sent for a life a removal was already sent for */
    mp_enemy_relay_lives_t lives;
    bool     performed_logged;

    mp_enemy_relay_copy_fn_t copy_listener;
    uint32_t                 copies_taken;     /* a copy's removal from the host */
    uint32_t                 copies_unheard;   /* with no copies' module to hear it */
    uint32_t                 copy_corpses;     /* a replica made a corpse */
} enemy_relay_state_t;

static enemy_relay_state_t relay;

/* ==============================================================================================
 * The host's half: the detour.
 * ============================================================================================ */

/* True when the note went out. `burst` is the speed in quarters of a burst into pieces just
 * before, 0 for none; a speed the byte cannot hold arrives here as 0, so the removal always
 * travels and `unsent` never moves for a burst. */
static bool send_despawn(uint16_t key, uint8_t generation, int32_t reason, uint8_t burst)
{
    mp_event_t event;
    uint8_t    note[MP_EVENT_DESPAWN_BYTES];
    uint16_t   level = 0;
    size_t     bytes;

    if (!relay.enabled) {
        return false;   /* no transport that can replicate: the loopback, or no session at all */
    }
    if (relay.send == NULL || !mp_enemy_spawn_level_identity(&level)) {
        ++relay.unsent;
        return false;
    }
    memset(&event, 0, sizeof event);
    event.kind             = MP_EVENT_DESPAWN;
    event.tick             = relay.tick;
    event.level_id         = level;
    event.actor_index      = key;
    event.actor_generation = generation;
    event.actor_reason     = (uint8_t)reason;
    event.actor_burst      = burst;
    bytes = mp_event_encode(&event, note, sizeof note);
    if (bytes == MP_EVENT_DESPAWN_BYTES && relay.send(note, bytes)) {
        ++relay.sent;
        if (mp_enemy_relay_life_sent_before(&relay.lives, level, key, generation)) {
            ++relay.repeated;
        }
        return true;
    }
    ++relay.unsent;
    return false;
}

/* What the removal DID, read off the actor and its placement after the call, rather than what
 * the caller asked for.
 *
 * The reason the caller passes need not be the reason that runs. Another hook on the same function
 * may rewrite it, as a mod that keeps corpses does when it turns a cull (1) into a kept corpse
 * (0xE) and frees the oldest kept body with a 1 of its own, and which hook the engine reaches first
 * depends on which DLL installed first. So the outcome is observed: a freed slot carries -1 in its
 * link word, which is what list_free writes and nothing else does; a slot still linked with the
 * actor in the corpse state is a corpse kept; and the placement's spawn state says whether a freed
 * one was buried (2) or left respawnable (0). */
/* How many removals a run names before it goes back to counting. Enough to see what a level
 * start does, few enough that a long level does not fill the file. */
#define NAMED_REMOVALS_MAX 12u

static const char *reason_name(int32_t reason)
{
    switch ((uint32_t)reason) {
    case MP_EVENT_REMOVE_OUT_OF_RANGE: return "out of range";
    case MP_EVENT_REMOVE_DELETED:      return "deleted and buried";
    case MP_EVENT_REMOVE_HOST_RELEASE: return "released to the player";
    case MP_EVENT_REMOVE_LEVEL_END:    return "a level end";
    case MP_EVENT_REMOVE_LEAVE_CORPSE: return "kept as a corpse";
    default:                           return "a reason this build cannot name";
    }
}

#define LINK_WORD_FREE 0xFFFFFFFFu
#define ACTOR_STATE    0x20u
#define STATE_CORPSE   0x0Eu

static int32_t observed_reason(uintptr_t actor, uintptr_t placement)
{
    uint32_t link  = 0;
    uint32_t state = 0;
    uint32_t spawn_state = 0;

    if (!memory_try_read(actor - 4u, &link, sizeof link) ||
        !memory_try_read(placement + PLACEMENT_SPAWN_STATE, &spawn_state, sizeof spawn_state)) {
        return -1;
    }
    if (link == LINK_WORD_FREE) {
        return spawn_state == 2u ? MP_EVENT_REMOVE_DELETED : MP_EVENT_REMOVE_OUT_OF_RANGE;
    }
    if (memory_try_read(actor + ACTOR_STATE, &state, sizeof state) && state == STATE_CORPSE) {
        return MP_EVENT_REMOVE_LEAVE_CORPSE;
    }
    /* Still in the pool and not a corpse: a release to the player, which stays local. */
    return -1;
}

/* The hook runs inside the enemy tick, task slot 0, before this feature's own task in slot 4, so
 * a removal in substep N leaves on N's packet carrying the tick told in N-1. The tick is
 * informational: a removal is performed on arrival, not when a timeline reaches it.
 *
 * engine: void enemy_delete(character *actor, int reason) */
static void __cdecl hook_enemy_delete(uintptr_t actor, int32_t reason)
{
    uint32_t index = 0;
    uint32_t placement = 0;
    uint32_t body = 0;
    uint32_t flags = 0;
    uint8_t  generation = 0;
    bool     watched = false;
    bool     sent = false;

    if (relay.host && !relay.performing && mp_armed_transport()) {
        /* A burst is noted by its body, and the actor is only an actor until the call. */
        (void)memory_try_read(actor + ACTOR_BODY, &body, sizeof body);
        (void)memory_try_read(actor + ACTOR_STATE_FLAGS, &flags, sizeof flags);
        /* A removal by distance is NOT held back here any more, and that is the point. The engine
         * decides it with one range test, and that test now measures against the nearest player
         * (mp_range_gate.h), so an actor a far player stands beside is never proposed for removal
         * in the first place. Refusing it here instead meant the engine asked again on the next
         * substep, and on every substep after that, for as long as the body stood there: 49733
         * refusals in one field run, and every ask that was not refused put another despawn on
         * the wire, 551 of 652 of them for a life already removed.
         *
         * The identity is read BEFORE the call, while the actor is still an actor; the outcome
         * AFTER it. A level teardown (4) is every machine's own business and is not watched. */
        if (reason != MP_EVENT_REMOVE_LEVEL_END &&
            mp_enemy_bind_index(actor, &index) && index < MP_WIRE_KEY_COUNT &&
            memory_try_read_u32(actor + ACTOR_PLACEMENT, &placement) && placement != 0u &&
            mp_enemy_sync_generation(index, &generation)) {
            watched = true;
        } else if (reason != MP_EVENT_REMOVE_LEVEL_END) {
            ++relay.unsent;
        }
    }
    relay.original(actor, reason);
    if (watched) {
        int32_t effective = observed_reason(actor, (uintptr_t)placement);

        if (effective < 0) {
            ++relay.not_travelling;
        } else {
            if (relay.named < NAMED_REMOVALS_MAX) {
                ++relay.named;
                log_info("a removal travels: placement %u, life %u, asked as %s and it "
                         "was %s", (unsigned)index, (unsigned)generation,
                         reason_name(reason), reason_name(effective));
            }
            sent = send_despawn((uint16_t)index, generation, effective,
                                mp_enemy_burst_speed_for(body, flags));
        }
    }
    /* The burst of this body, taken only now that it is known whether its removal went out. */
    mp_enemy_burst_claim(body, flags, index, generation, sent);
}

/* ==============================================================================================
 * The host's other half: the far body's activation pass.
 * ============================================================================================ */

void mp_enemy_relay_set_enabled(bool enabled)
{
    relay.enabled = enabled;
}

void mp_enemy_relay_tick(uint32_t tick)
{
    if (!relay.enabled) {
        return;
    }
    /* The tick is the stamp every message sent until the next call carries, and that is all
     * this does now. It used to run a second pass over the whole placement table for every far
     * body, once a substep, to wake what the engine's own scan had not. The engine's scan asks
     * one range test, that test now measures against the nearest player, and so it wakes them
     * itself. A second table walk for the same answer is the repetition this feature keeps
     * finding in itself. */
    relay.tick = tick;
}

/* ==============================================================================================
 * The client's half: perform what the host removed.
 * ============================================================================================ */

/* Performs a removal the host sent, on this side's body for it, and has the sync remember what it
 * left behind. Both the placements' path and the copies' take it, so a corpse a copy's removal
 * kept is written on exactly as a placement's is. */
static void perform(uintptr_t actor, const mp_event_t *event)
{
    relay.performing = true;
    relay.original(actor, (int32_t)event->actor_reason);
    relay.performing = false;
    mp_enemy_sync_performed(event->actor_index, actor, event->actor_generation,
                            (uint8_t)event->actor_reason);
}

/* A removal the host sent for a copy. Only the overlay removes a copy (common/npc_spawn_note.h),
 * so it goes to the copies' module, which answers the replica of this very life for a removal that
 * keeps a corpse, and makes a corpse of nothing else. A copy's replica is never released. */
static void take_copy_removal(const mp_event_t *event)
{
    uintptr_t actor;
    uint32_t  index = 0;

    ++relay.copies_taken;
    if (relay.copy_listener == NULL) {
        ++relay.copies_unheard;   /* no copies' module in this session, so nothing of it stands */
        return;
    }
    actor = relay.copy_listener((uint32_t)event->actor_index - MP_WIRE_KEY_COPY_BASE,
                                event->actor_generation, (uint8_t)event->actor_reason);
    if (actor == 0u) {
        return;
    }
    if (!mp_enemy_bind_index(actor, &index) || index != event->actor_index ||
        !mp_enemy_spawn_actor_is_live(actor, event->actor_index)) {
        ++relay.already_gone;
        return;
    }
    perform(actor, event);
    ++relay.copy_corpses;
}

/* A copy's burst, before its removal, on the replica of the note's own life: the life the note
 * names has to be the one this side holds, and the replica has to carry the copy's key and be
 * alive. The copies' table answers for the life it holds, not for the note's, which is why the
 * life is asked here first. */
static void burst_copy(const mp_event_t *event)
{
    uintptr_t actor;
    uint32_t  index = 0;
    uint8_t   generation = 0;

    if (event->actor_burst == 0u) {
        return;
    }
    if (!mp_enemy_sync_generation(event->actor_index, &generation)) {
        mp_enemy_burst_unmet(event, MP_ENEMY_BURST_UNMET_NO_REPLICA);
        return;
    }
    if (generation != event->actor_generation) {
        mp_enemy_burst_unmet(event, MP_ENEMY_BURST_UNMET_MOVED_ON);
        return;
    }
    actor = mp_enemy_sync_replica_for(event->actor_index);
    if (actor == 0u || !mp_enemy_bind_index(actor, &index) || index != event->actor_index ||
        !mp_enemy_spawn_actor_is_live(actor, event->actor_index)) {
        mp_enemy_burst_unmet(event, MP_ENEMY_BURST_UNMET_NO_REPLICA);
        return;
    }
    mp_enemy_burst_replica(actor, event);
}

bool mp_enemy_relay_take_message(const uint8_t *note, size_t bytes)
{
    mp_event_t               event;
    mp_enemy_record_t        last;
    mp_enemy_burst_verdict_t verdict;
    uintptr_t                actor;
    mp_enemy_slot_t          slot;
    uint32_t                 index = 0;
    uint16_t                 level = 0;
    uint8_t                  generation = 0;
    bool                     newer;

    if (note == NULL || bytes != MP_EVENT_DESPAWN_BYTES || note[0] != MP_EVENT_DESPAWN) {
        return false;
    }
    if (!mp_event_decode(note, bytes, &event)) {
        ++relay.refused;
        return true;   /* ours by tag and length, and torn: taken so nothing else reads it */
    }
    if (!relay.installed) {
        return true;
    }
    if (relay.host) {
        ++relay.dropped_on_host;   /* removals come from here; one arriving here is a loop */
        return true;
    }
    if (!mp_enemy_spawn_level_identity(&level)) {
        ++relay.no_level;
        return true;
    }
    if (level != event.level_id) {
        ++relay.other_level;
        return true;
    }
    if (mp_wire_key_is_copy(event.actor_index)) {
        burst_copy(&event);
        take_copy_removal(&event);
        return true;
    }
    actor = mp_enemy_sync_replica_for(event.actor_index);
    if (actor == 0 || !mp_enemy_bind_index(actor, &index) || index != event.actor_index) {
        ++relay.no_actor;   /* nothing here for it, which for a client that never woke it is fine */
        mp_enemy_burst_unmet(&event, MP_ENEMY_BURST_UNMET_NO_REPLICA);
        return true;
    }

    /* And it must still be there. The pointer above is the last census's, and between that
     * census and this note the engine's own tick can have removed the actor: the block that
     * stopped listing it let it go, an actor let go far from THIS player is out of range, and
     * the tick removes it in the same substep the note arrives in. Neither test above can see
     * that. A freed pool slot keeps its bytes, so it still answers with its placement index and
     * still looks alive, and the removal would be the SECOND on that body: the fourth field run
     * ended in the engine's own assert `bapobj.c(1178): pObj->pThing != NULL`, which is exactly
     * a body freed twice, one replica, one removal sent.
     *
     * So the slot is asked, at the moment of the call and for the life the note names: its
     * link word says whether the pool took it back, the actor's index and record say whether
     * another placement or another life holds it now, and a body an earlier removal kept as a
     * corpse counts only for the life this side kept it for. The host's later removal of that
     * corpse is performed on it. Asking whether the actor was alive refused exactly that one,
     * and such a corpse then stayed on this side for good.
     *
     * The life comes first. A note for a life the table has moved on from touches nothing,
     * because the mirror is the newer life's now, with one exception: the corpse this side
     * kept for the note's life. The host starts a new life only after the old body left its
     * chain, so the newer life waits for exactly this removal before it gets a body here. */
    slot    = mp_enemy_sync_slot(event.actor_index, actor, event.actor_generation);
    newer   = !mp_enemy_sync_generation(event.actor_index, &generation) ||
              generation != event.actor_generation;
    verdict = mp_enemy_burst_decide_for(slot, newer, event.actor_burst);
    if (verdict.removal == MP_ENEMY_BURST_REMOVAL_STALE) {
        ++relay.stale;      /* a removal of a life this side has already replaced */
        mp_enemy_burst_unmet(&event, MP_ENEMY_BURST_UNMET_MOVED_ON);
        return true;
    }
    if (verdict.removal == MP_ENEMY_BURST_REMOVAL_GONE) {
        ++relay.already_gone;
        mp_enemy_burst_unmet(&event, MP_ENEMY_BURST_UNMET_NO_REPLICA);
        mp_enemy_sync_forget(event.actor_index, event.actor_generation);
        return true;
    }

    /* The burst first, on the living actor of this life, and the removal after it in the same
     * call, as on the host. A corpse this side keeps is only removed. */
    if (verdict.burst) {
        mp_enemy_burst_replica(actor, &event);
    } else {
        mp_enemy_burst_unmet(&event, MP_ENEMY_BURST_UNMET_KEPT);
    }

    /* Reason 0 reads the actor's own state to decide whether the placement stays buried, and a
     * parked replica's state is the parking. So it is let go first, in the state the host last
     * reported, and the removal then sees a corpse where the host saw one. The other two reasons
     * do not read it and leave the parking alone: a corpse stays a corpse, parked, until the
     * host's cull arrives as a reason 1 of its own. The note rides the reliable channel and the
     * block the unreliable payload, so the block that first omits the actor may arrive before
     * the note, and the removal then finds an unparked actor, fine for every reason; or after
     * it, and finds a parked one, which is what this release is for. */
    if (event.actor_reason == MP_EVENT_REMOVE_OUT_OF_RANGE) {
        (void)mp_enemy_bind_release(actor, !newer && mp_enemy_sync_mirror(event.actor_index, &last)
                                               ? &last
                                               : NULL);
    }
    perform(actor, &event);
    ++relay.performed;
    if (event.actor_reason == MP_EVENT_REMOVE_LEAVE_CORPSE) {
        ++relay.performed_kept;
    } else {
        relay.freed_after_kept += (slot == MP_ENEMY_SLOT_KEPT) ? 1u : 0u;
    }
    relay.across_lives += newer ? 1u : 0u;
    if (!relay.performed_logged) {
        relay.performed_logged = true;
        log_info("placement %u was removed here for reason %u because the host removed it",
                 (unsigned)event.actor_index, (unsigned)event.actor_reason);
    }
    return true;
}

/* ==============================================================================================
 * Installation and the report.
 * ============================================================================================ */

bool mp_enemy_relay_install(void)
{
    uintptr_t target;

    if (relay.installed) {
        return true;
    }
    target = mp_signatures_address(MP_SITE_ENEMY_DELETE);
    if (target == 0) {
        log_warning("the removal site did not resolve, so a removal on the host neither travels "
                    "nor is held back for the far player");
        return false;
    }
    if (!detour_install(&relay.detour, target, (const void *)&hook_enemy_delete,
                        ENEMY_DELETE_PROLOGUE)) {
        log_error("the removal site at %08X refused the detour", (unsigned)target);
        return false;
    }
    relay.original  = (enemy_delete_fn_t)relay.detour.original;
    relay.installed = true;
    log_info("the enemy relay is bound at %08X: a removal on the host travels with its reason, "
             "and the host keeps what the far player is near", (unsigned)target);
    /* A burst travels on the removal, so it stands with the removal or not at all. */
    (void)mp_enemy_burst_install();
    return true;
}

bool mp_enemy_relay_life_sent_before(mp_enemy_relay_lives_t *lives, uint16_t level,
                                     uint16_t key, uint8_t generation)
{
    uint8_t *known;
    uint8_t  bit;

    if (lives == NULL || key >= MP_WIRE_KEY_COUNT) {
        return false;
    }
    known = &lives->known[key >> 3];
    bit   = (uint8_t)(1u << (key & 7u));
    if ((*known & bit) != 0u && lives->level[key] == level &&
        lives->generation[key] == generation) {
        return true;
    }
    *known                 = (uint8_t)(*known | bit);
    lives->level[key]      = level;
    lives->generation[key] = generation;
    return false;
}

bool mp_enemy_relay_installed(void)
{
    return relay.installed;
}

void mp_enemy_relay_set_host(bool host)
{
    relay.host = host;
    mp_enemy_burst_set_host(host);   /* only the side that sends removals keeps a burst for one */
}

void mp_enemy_relay_set_send(mp_enemy_relay_send_fn_t send)
{
    relay.send = send;
}

void mp_enemy_relay_set_far_body(mp_enemy_relay_far_body_fn_t far_body)
{
    relay.far_body = far_body;
}

void mp_enemy_relay_set_copy_listener(mp_enemy_relay_copy_fn_t listener)
{
    relay.copy_listener = listener;
}

void mp_enemy_relay_report(void)
{
    if (!relay.installed) {
        log_info("the enemy relay is not bound, so a removal on the host neither travels nor is "
                 "held back");
        return;
    }
    log_info("the enemy relay (%s): %u removal(s) sent, %u unsent, %u for a reason that stays "
             "local | performed %u, %u "
             "with no actor here, %u already gone when the note arrived, %u stale, %u about "
             "another level, %u with no level open, %u torn, %u dropped on a host; "
             "%u of the removals sent "
             "were for a life a removal had already been sent for; of the removals performed, "
             "%u kept a corpse, %u freed a corpse kept before, %u on a corpse whose placement "
             "the host has since started again",
             relay.host ? "host" : "client", (unsigned)relay.sent, (unsigned)relay.unsent,
             (unsigned)relay.not_travelling,
             (unsigned)relay.performed, (unsigned)relay.no_actor,
             (unsigned)relay.already_gone, (unsigned)relay.stale, (unsigned)relay.other_level,
             (unsigned)relay.no_level,
             (unsigned)relay.refused, (unsigned)relay.dropped_on_host,
             (unsigned)relay.repeated,
             (unsigned)relay.performed_kept, (unsigned)relay.freed_after_kept,
             (unsigned)relay.across_lives);
    log_info("the enemy relay, the copies: %u removal(s) of a copy from the host, %u of them "
             "made a corpse here, %u with no copies' module to hear them",
             (unsigned)relay.copies_taken, (unsigned)relay.copy_corpses,
             (unsigned)relay.copies_unheard);
}
