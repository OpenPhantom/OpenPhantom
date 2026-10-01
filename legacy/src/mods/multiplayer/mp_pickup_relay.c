/* mp_pickup_relay.c: the claim, the grant, and the effect on the claimant alone. */
#include "mp_pickup_relay.h"

#include "mp_enemy_bind.h"
#include "mp_enemy_spawn.h"
#include "mp_enemy_sync.h"
#include "mp_events.h"
#include "mp_placements.h"
#include "mp_player_sound.h"
#include "mp_taken.h"
#include "mp_signatures.h"
#include "mp_trust.h"
#include "mp_wire.h"

#include "common/detour.h"
#include "common/logging.h"
#include "common/memory.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The pickup's body: its flags word, whose bit 3 is "taken" (the routine's own gate reads it with
 * `and ecx, 8` at 0x0044889F and sets it with `or al, 8` at 0x0044898F), its shooter class, which
 * is the kind the player's handler dispatches on, and the actor it belongs to. */
#define BODY_FLAGS          0x00u
#define BODY_SHOOTER_CLASS  0x08u
#define BODY_OWNER          0xA0u
#define BODY_TAKEN          0x08u

/* The actor's body, and where the actor stands. */
#define ACTOR_BODY          0x34u
#define ACTOR_POS           0xD0u

/* The contact code the pickup's own task message carries: message 1, "I was taken". The routine
 * has three arms by kind: health (0x0D to 0x10) through the health pickup, weapons (0x11 to 0x1B)
 * through the weapon grant plus an auto-equip from unarmed, and the shield (0x0A). On success it
 * plays the pickup sound, sets bit 3 on the BODY's flags word and sends the pickup's own task
 * message 1, whose handler is the enemy contact handler; its arm for message 1 on a class in the
 * band with bit 3 set writes a removal reason of 1 and a health of 0, and the enemy tick then
 * deletes the actor with reason 1. The effect lands in the LOCAL player's status record. */
#define PICKUP_TAKEN_CODE   1u

/* push ebp; mov ebp,esp; sub esp,0xc: six bytes, no relative operand. The head of the routine at
 * 0x00448894 in the retail image, which is what the pattern is cut from:
 *
 *     00448894  55 8B EC 83 EC 0C        push ebp; mov ebp,esp; sub esp,0xc   the prologue
 *     0044889A  8B 45 0C 8B 08           mov eax,[ebp+0xc]; mov ecx,[eax]     the body's flags
 *     0044889F  83 E1 08 85 C9 74 05     and ecx,8; test ecx,ecx; je +5       bit 3 = taken
 *     004488A6  E9 rel32                 jmp epilogue                         masked
 *     004488AB  83 7D 08 0D 72 10        cmp [ebp+8],0xd; jb                  the first arm
 *
 * Twenty nine bytes with the one relative operand masked, matching once per image: 0x448894 in
 * both retail builds and 0x448834 in the recompile. The prologue ends on an instruction boundary
 * after the `sub`, so the detour's jump displaces three whole instructions. */
#define PLR_PICKUP_PROLOGUE 6u

/* __cdecl, two arguments, no return: the one caller, the player's contact handler at 0x004484D2,
 * pushes the pickup's body (the message's own object) and the kind and adds 8. */
typedef void(__cdecl *plr_pickup_fn_t)(int32_t kind, uintptr_t body);

typedef struct pickup_relay_state {
    bool                      installed;
    bool                      host;
    bool                      granting;   /* the routine running for a grant, not for a touch */
    detour_t                  detour;
    plr_pickup_fn_t           original;
    mp_pickup_relay_send_fn_t send;
    mp_pickup_relay_perform_fn_t perform;
    mp_pickup_relay_far_body_fn_t far_body;
    uint32_t                  tick;
    uint8_t                   my_slot;    /* the world slot this machine holds */
    uint32_t                  last_claim_tick[256];
    uint8_t                   last_claim_generation[256];

    /* The absolute list, and the substep it last went out on. The host builds it, the client
     * applies it, and both keep the last one so a repeat that says nothing new costs nothing. */
    mp_taken_t taken;
    bool       taken_known;
    uint32_t   taken_last_tick;
    uint32_t   taken_sent;
    uint32_t   taken_unsent;
    uint32_t   taken_full;      /* a level with more pickups than one note can name */
    uint32_t   taken_applied;   /* client: placements buried because the host had them gone */
    uint32_t   taken_elsewhere; /* a note for a level this side is not in */
    uint32_t   taken_torn;

    uint32_t claimed;
    uint32_t claims_held;       /* a touch inside the repeat interval */
    uint32_t claims_unsent;
    uint32_t taken_locally;     /* a pickup this machine owns, taken as in single player */
    uint32_t already_taken;
    uint32_t granted;           /* host: claims granted */
    uint32_t applied;           /* client: grants applied */
    uint32_t not_mine;          /* client: grants for another player, left alone */
    uint32_t refused_untaken;   /* host: claim for a pickup already taken or not a pickup */
    uint32_t no_actor;
    uint32_t stale;
    uint32_t other_level;
    uint32_t no_level;
    uint32_t torn;
    uint32_t not_performed;     /* host: the contact replay had no attacker */
    uint32_t out_of_reach;      /* host: a deathmatch claim from too far away */
    bool     claimed_logged;
    bool     granted_logged;
    bool     applied_logged;
} pickup_relay_state_t;

static pickup_relay_state_t relay;

/* Used before it is defined: the absolute list is answered by the same dispatcher as the events,
 * and it is built out of the placement table further down. */
static bool take_taken(const uint8_t *note, size_t bytes);

bool mp_pickup_relay_may_claim(uint32_t last, uint32_t now)
{
    return last == 0u || now < last || now - last >= MP_PICKUP_RELAY_CLAIM_INTERVAL;
}

bool mp_pickup_relay_in_reach(const float claimant[3], const float pickup[3])
{
    float dx = claimant[0] - pickup[0];
    float dy = claimant[1] - pickup[1];
    float dz = claimant[2] - pickup[2];

    return dx * dx + dy * dy + dz * dz <= MP_PICKUP_RELAY_REACH * MP_PICKUP_RELAY_REACH;
}

/* ==============================================================================================
 * Sending one message, which both halves do.
 * ============================================================================================ */

static bool send_pickup(uint16_t key, uint8_t generation, uint8_t kind, uint8_t slot)
{
    mp_event_t event;
    uint8_t    note[MP_EVENT_PICKUP_BYTES];
    uint16_t   level = 0;
    size_t     bytes;

    if (relay.send == NULL || !mp_enemy_spawn_level_identity(&level)) {
        return false;
    }
    memset(&event, 0, sizeof event);
    event.kind             = MP_EVENT_PICKUP;
    event.tick             = relay.tick;
    event.source_slot      = slot;
    event.level_id         = level;
    event.actor_index      = key;   /* the codec refuses anything but a placement */
    event.actor_generation = generation;
    event.pickup_kind      = kind;
    bytes = mp_event_encode(&event, note, sizeof note);
    return bytes == MP_EVENT_PICKUP_BYTES && relay.send(note, bytes);
}

/* ==============================================================================================
 * The client's half: the detour.
 * ============================================================================================ */

/* The engine's own pickup, on this machine's player. What it took, it played the pickup's sound
 * for, and the far side hears the same through a moment of this player's body. Taken is bit 3 of
 * the item's flags going up in the call. */
static void take_here(int32_t kind, uintptr_t body)
{
    uint32_t before = 0u;
    uint32_t after = 0u;
    bool     read_before = body != 0u && memory_try_read_u32(body + BODY_FLAGS, &before);

    relay.original(kind, body);
    if (read_before && (before & BODY_TAKEN) == 0u &&
        memory_try_read_u32(body + BODY_FLAGS, &after) && (after & BODY_TAKEN) != 0u) {
        mp_player_sound_note_pickup(kind);
    }
}

static void __cdecl hook_plr_pickup(int32_t kind, uintptr_t body)
{
    uint32_t owner = 0;
    uint32_t index = 0;
    uint32_t flags = 0;
    uint8_t  generation = 0;

    if (relay.host || relay.granting || body == 0u ||
        !memory_try_read_u32(body + BODY_OWNER, &owner) || owner == 0u ||
        !mp_enemy_bind_is_parked((uintptr_t)owner) ||
        !mp_enemy_bind_index((uintptr_t)owner, &index) || !mp_wire_key_is_placement(index) ||
        !mp_enemy_sync_generation(index, &generation)) {
        /* A host, a grant being applied, or a pickup this machine owns: single player behaviour. */
        if (!relay.host && !relay.granting) {
            ++relay.taken_locally;
        }
        take_here(kind, body);
        return;
    }

    /* The host's. Nothing is taken here; the same gate the routine has, then a claim. */
    if (memory_try_read_u32(body + BODY_FLAGS, &flags) && (flags & BODY_TAKEN) != 0u) {
        ++relay.already_taken;
        return;
    }
    if (relay.last_claim_generation[index] == generation &&
        !mp_pickup_relay_may_claim(relay.last_claim_tick[index], relay.tick)) {
        ++relay.claims_held;
        return;
    }
    if (kind < 0 || kind > 255) {
        ++relay.claims_unsent;
        return;
    }
    if (send_pickup((uint16_t)index, generation, (uint8_t)kind, relay.my_slot)) {
        relay.last_claim_tick[index]       = relay.tick == 0u ? 1u : relay.tick;
        relay.last_claim_generation[index] = generation;
        ++relay.claimed;
        if (!relay.claimed_logged) {
            relay.claimed_logged = true;
            log_info("a pickup on placement %u was claimed from the host rather than taken",
                     (unsigned)index);
        }
    } else {
        ++relay.claims_unsent;   /* the codec refused the class, or the channel is full */
    }
}

/* ==============================================================================================
 * Both halves of the message.
 * ============================================================================================ */

static bool find_body(const mp_event_t *event, uintptr_t *actor, uint32_t *body)
{
    uint32_t index = 0;
    uint16_t level = 0;
    uint8_t  generation = 0;

    if (!mp_enemy_spawn_level_identity(&level)) {
        ++relay.no_level;
        return false;
    }
    if (level != event->level_id) {
        ++relay.other_level;
        return false;
    }
    *actor = mp_enemy_sync_actor_for(event->actor_index);
    if (*actor == 0 || !mp_enemy_bind_index(*actor, &index) || index != event->actor_index ||
        !memory_read_u32(*actor + ACTOR_BODY, body) || *body == 0u) {
        ++relay.no_actor;
        return false;
    }
    if (!mp_enemy_sync_generation(event->actor_index, &generation) ||
        generation != event->actor_generation) {
        ++relay.stale;
        return false;
    }
    return true;
}

/* The claimant's body here and the pickup, within reach of each other. No position for either is
 * no grant: the claim comes again while the player stands there. */
static bool claimant_in_reach(size_t bank, uintptr_t actor)
{
    float claimant[3];
    float pickup[3];

    return relay.far_body != NULL && relay.far_body(bank, claimant) &&
           memory_try_read(actor + ACTOR_POS, pickup, sizeof pickup) &&
           mp_pickup_relay_in_reach(claimant, pickup);
}

/* A claim, on the host, from the peer whose far bank is `bank` and whose world slot is `slot`:
 * its body touches the pickup in the replay, and the grant names it. */
static void decide(size_t bank, uint8_t slot, const mp_event_t *event)
{
    uintptr_t actor = 0;
    uint32_t  body = 0;
    uint32_t  flags = 0;
    uint32_t  class_word = 0;

    if (!find_body(event, &actor, &body)) {
        return;
    }
    if (!memory_read_u32((uintptr_t)body + BODY_FLAGS, &flags) ||
        !memory_read_u32((uintptr_t)body + BODY_SHOOTER_CLASS, &class_word) ||
        (flags & BODY_TAKEN) != 0u ||
        class_word < MP_PICKUP_KIND_MIN || class_word > MP_PICKUP_KIND_MAX) {
        ++relay.refused_untaken;
        return;
    }
    if (mp_trust_checking() && !claimant_in_reach(bank, actor)) {
        ++relay.out_of_reach;
        return;
    }

    /* Taken, on this machine's body, before the contact handler is run: its pickup arm tests the
     * bit and marks the actor for removal only when it is set. Then the removal travels through
     * the enemy relay, and the grant tells the claimant to apply the effect to itself. */
    flags |= BODY_TAKEN;
    if (!memory_try_write((uintptr_t)body + BODY_FLAGS, &flags, sizeof flags)) {
        ++relay.refused_untaken;
        return;
    }
    if (relay.perform == NULL || !relay.perform(bank, event->actor_index, PICKUP_TAKEN_CODE)) {
        ++relay.not_performed;   /* the bit is set all the same, so nobody takes it twice */
    }
    if (send_pickup(event->actor_index, event->actor_generation, (uint8_t)class_word, slot)) {
        ++relay.granted;
        if (!relay.granted_logged) {
            relay.granted_logged = true;
            log_info("a claim on placement %u was granted: the far player takes it, this one "
                     "does not", (unsigned)event->actor_index);
        }
    } else {
        ++relay.claims_unsent;
    }
}

/* A grant, on the client: the routine this side refused to run for the touch, run now.
 *
 * The grant leaves in the host's substep N and the removal in N+1, both on the reliable channel,
 * which is ordered, so the body still exists here when the grant is applied; the routine sets
 * bit 3 on it and sends the message to the parked actor, which turns back, and the removal
 * arrives a substep later as its own message. The block of N+1, which omits the actor, may
 * arrive before either: the client then lets the replica go, the grant finds an unparked actor,
 * and the routine's message reaches a handler that marks it for removal here as well; the
 * removal message then finds nothing, which is counted and harmless. */
static void apply(const mp_event_t *event)
{
    uintptr_t actor = 0;
    uint32_t  body = 0;

    if (event->source_slot != relay.my_slot) {
        ++relay.not_mine;   /* somebody else's claim, granted to them */
        return;
    }
    if (!find_body(event, &actor, &body)) {
        return;
    }
    relay.granting = true;
    take_here((int32_t)event->pickup_kind, (uintptr_t)body);
    relay.granting = false;
    ++relay.applied;
    if (!relay.applied_logged) {
        relay.applied_logged = true;
        log_info("the grant for placement %u was applied here: the effect landed on this player",
                 (unsigned)event->actor_index);
    }
}

bool mp_pickup_relay_take_message(size_t bank, uint8_t slot, const uint8_t *note, size_t bytes)
{
    mp_event_t event;

    /* The absolute list first. It is a different message with a different tag, and it is the one
     * a joiner is told the whole truth with; the events below only describe changes since. */
    if (take_taken(note, bytes)) {
        return true;
    }
    if (note == NULL || bytes != MP_EVENT_PICKUP_BYTES || note[0] != MP_EVENT_PICKUP) {
        return false;
    }
    if (!mp_event_decode(note, bytes, &event)) {
        ++relay.torn;
        return true;
    }
    if (!relay.installed) {
        return true;
    }
    if (relay.host) {
        decide(bank, slot, &event);
    } else {
        apply(&event);
    }
    return true;
}

/* ==============================================================================================
 * Installation and the report.
 * ============================================================================================ */

bool mp_pickup_relay_install(void)
{
    uintptr_t target;

    if (relay.installed) {
        return true;
    }
    target = mp_signatures_address(MP_SITE_PLR_PICKUP);
    if (target == 0) {
        log_warning("the pickup routine did not resolve, so a client takes pickups locally and "
                    "the host still has them");
        return false;
    }
    if (!detour_install(&relay.detour, target, (const void *)&hook_plr_pickup,
                        PLR_PICKUP_PROLOGUE)) {
        log_error("the pickup routine at %08X refused the detour", (unsigned)target);
        return false;
    }
    relay.original  = (plr_pickup_fn_t)relay.detour.original;
    relay.installed = true;
    log_info("the pickup relay is bound at %08X: a client claims what the host owns, the host "
             "grants it, and the effect lands on the claimant", (unsigned)target);
    return true;
}

bool mp_pickup_relay_installed(void)
{
    return relay.installed;
}

void mp_pickup_relay_set_host(bool host)
{
    relay.host = host;
}

void mp_pickup_relay_set_send(mp_pickup_relay_send_fn_t send)
{
    relay.send = send;
}

void mp_pickup_relay_set_perform(mp_pickup_relay_perform_fn_t perform)
{
    relay.perform = perform;
}

void mp_pickup_relay_set_far_body(mp_pickup_relay_far_body_fn_t far_body)
{
    relay.far_body = far_body;
}

void mp_pickup_relay_set_slot(uint8_t slot)
{
    relay.my_slot = slot;
}

/* ==============================================================================================
 * The absolute list of what is gone, which is the half an event stream cannot do.
 * ============================================================================================ */

/* How often the host repeats it. The same cadence as the roster and the setup: a second is what a
 * screen is worth redrawing at, and this is not a latency-sensitive fact. */
#define TAKEN_REPEAT_SUBSTEPS 32u

/* Walk the placement table and collect the pickups that are gone. Buried is the durable state the
 * engine's own savegame keeps for a placement, and it is what a taken pickup is left in. The
 * eleven shipped levels place 177 pickups, 133 of them with a deactivation radius, and a removal
 * for reason 0 (out of range) puts one of those back with a fresh flags word, takeable again;
 * that is the retail behaviour and, in a deathmatch, an ammunition source. Whoever wants
 * otherwise needs a book of taken placements rather than a bit on an object that dies with its
 * actor, which is what this list is the beginning of. */
static bool build_taken(mp_taken_t *out)
{
    uint32_t count = 0;
    uint32_t table = 0;
    uint32_t i;

    memset(out, 0, sizeof *out);
    if (!mp_enemy_spawn_level_identity(&out->level) ||
        !mp_placements_table(NULL, &count, &table)) {
        return false;
    }
    /* The index travels in one byte, so a placement past 255 cannot be named. The engine's own
     * bound on a placement id is the same 256, and the largest shipped level authors 255, so this
     * is the engine's limit rather than the wire's; a level that went past it would lose the
     * pickups above the line, which is why the shortfall is counted rather than passed over. */
    for (i = 0; i < count; ++i) {
        mp_placement_t placement;

        if (i > 0xFFu) {
            ++relay.taken_full;
            break;
        }
        if (!mp_placements_read(table, i, &placement) ||
            !mp_placements_is_pickup(placement.class_id) ||
            placement.state != MP_PLACEMENT_STATE_BURIED) {
            continue;
        }
        if (!mp_taken_add(out, (uint8_t)i)) {
            ++relay.taken_full;
            break;   /* the rest would be a list of what is LEFT, which is the wrong answer */
        }
    }
    return true;
}

static void send_taken(void)
{
    uint8_t    note[MP_TAKEN_MAX_BYTES];
    mp_taken_t built;
    size_t     bytes;

    if (relay.send == NULL || !build_taken(&built)) {
        return;
    }
    /* Repeated even when nothing changed, because the note is what a JOINER is told with and a
     * joiner arrives at a moment nobody chose. What the comparison saves is only the log line. */
    if (!relay.taken_known || !mp_taken_equal(&relay.taken, &built)) {
        log_info("%u pickup(s) are gone in level %u", (unsigned)built.count,
                 (unsigned)built.level);
    }
    relay.taken       = built;
    relay.taken_known = true;

    bytes = mp_taken_encode(&built, note, sizeof note);
    if (bytes == 0u) {
        return;
    }
    if (relay.send(note, bytes)) {
        ++relay.taken_sent;
    } else {
        ++relay.taken_unsent;
    }
}

/* The client's half: everything the host says is gone, buried here too.
 *
 * Writing the spawn state is enough and deleting a standing actor is not this module's business:
 * the state is what the engine's own activation scan reads before it builds anything, so a
 * placement buried here is never built again. One that is ALREADY standing keeps standing, and
 * that is the honest outcome: the pickup relay's claim path owns a body that exists, and taking
 * it out from under that path would be two modules writing the same object. */
static bool take_taken(const uint8_t *note, size_t bytes)
{
    mp_taken_t heard;
    uint16_t   level = 0;
    uint32_t   count = 0;
    uint32_t   table = 0;
    size_t     i;

    if (!mp_taken_is(note, bytes)) {
        return false;
    }
    if (!relay.installed) {
        return true;   /* addressed here, and this module is not running: not somebody else's */
    }
    if (!mp_taken_decode(note, bytes, &heard)) {
        ++relay.taken_torn;
        return true;   /* addressed here, and refused here */
    }
    if (relay.host) {
        return true;   /* the authority does not take its own description back */
    }
    if (!mp_enemy_spawn_level_identity(&level) || level != heard.level) {
        ++relay.taken_elsewhere;
        return true;
    }
    if (!mp_placements_table(NULL, &count, &table)) {
        return true;   /* no level open yet; the next repeat is a second away */
    }
    for (i = 0; i < heard.count; ++i) {
        mp_placement_t placement;
        uint32_t       put = MP_PLACEMENT_STATE_BURIED;

        if (heard.index[i] >= count || !mp_placements_read(table, heard.index[i], &placement) ||
            !mp_placements_is_pickup(placement.class_id) ||
            placement.state == MP_PLACEMENT_STATE_BURIED) {
            continue;
        }
        if (memory_try_write(placement.address + MP_PLACEMENT_SPAWN_STATE, &put, sizeof put)) {
            ++relay.taken_applied;
        }
    }
    relay.taken       = heard;
    relay.taken_known = true;
    return true;
}

void mp_pickup_relay_tick(uint32_t tick)
{
    relay.tick = tick;
    /* And the host's repeat, on the substep clock the whole session already shares. It is the
     * host's only half: a client sends nothing here, because what is gone is the authority's
     * statement and two of them would fight. */
    if (relay.installed && relay.host &&
        (relay.taken_last_tick == 0u || tick < relay.taken_last_tick ||
         tick - relay.taken_last_tick >= TAKEN_REPEAT_SUBSTEPS)) {
        relay.taken_last_tick = tick == 0u ? 1u : tick;
        send_taken();
    }
}

void mp_pickup_relay_report(void)
{
    if (!relay.installed) {
        log_info("the pickup relay is not bound, so a client takes pickups locally and the host "
                 "still has them");
        return;
    }
    log_info("the pickup relay (%s): %u claim(s) sent, %u held inside the interval, %u unsent, "
             "%u taken locally, %u already taken | %u granted, %u refused as taken or not a "
             "pickup, %u out of reach in a deathmatch, %u not performed | %u grant(s) "
             "applied, %u for another player | %u with no actor, %u stale, %u about another "
             "level, %u with no level open, %u torn",
             relay.host ? "host" : "client", (unsigned)relay.claimed, (unsigned)relay.claims_held,
             (unsigned)relay.claims_unsent, (unsigned)relay.taken_locally,
             (unsigned)relay.already_taken, (unsigned)relay.granted,
             (unsigned)relay.refused_untaken, (unsigned)relay.out_of_reach,
             (unsigned)relay.not_performed,
             (unsigned)relay.applied, (unsigned)relay.not_mine, (unsigned)relay.no_actor,
             (unsigned)relay.stale,
             (unsigned)relay.other_level, (unsigned)relay.no_level, (unsigned)relay.torn);
}
