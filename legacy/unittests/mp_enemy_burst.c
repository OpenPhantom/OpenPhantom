/* mp_enemy_burst.c: an enemy that bursts into its pieces on the host bursts on a client too.
 *
 * The burst rides the removal it belongs to, as the last byte of the removal note, so what can go
 * wrong is on both ends of that byte. A speed that does not survive the quarters arrives as
 * another burst or none. A host that takes a burst for the wrong body sends a burst for an actor
 * that did not burst. And a client that bursts on the wrong slot bursts a corpse it keeps for an
 * older life, or changes a removal it performed correctly before this byte existed: that last one
 * is checked against the old decision, written out here as it stood.
 */
#include "unittest.h"

#include "mp_enemy_bind.h"
#include "mp_enemy_burst.h"
#include "mp_enemy_burst_rule.h"
#include "mp_events.h"
#include "mp_level_state_rule.h"
#include "mp_shot_sites.h"
#include "mp_world_state.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static void check_the_quarters(void)
{
    static const float EXACT[] = { 0.5f, 1.0f, 2.0f, 2.75f, 63.75f };
    uint8_t            q = 99u;
    size_t             i;
    unsigned           v;
    bool               finite_and_rising = true;

    ut_section("the speed travels in quarters, and a speed the byte cannot hold travels as none");
    for (i = 0; i < sizeof EXACT / sizeof EXACT[0]; ++i) {
        ut_checkf(mp_enemy_burst_quarters(EXACT[i], &q) && mp_enemy_burst_speed(q) == EXACT[i],
                  "%u/100 comes back exactly", (unsigned)(EXACT[i] * 100.0f));
    }
    ut_check(mp_enemy_burst_quarters(1.0f, &q) && q == 4u, "the engine's 1.0 is 4");
    ut_check(mp_enemy_burst_quarters(2.0f, &q) && q == 8u, "and its 2.0 is 8");
    ut_check(!mp_enemy_burst_quarters(0.0f, &q) && q == 0u, "no speed is no burst");
    ut_check(!mp_enemy_burst_quarters(-1.0f, &q) && q == 0u, "a negative one is refused as 0");
    ut_check(!mp_enemy_burst_quarters(NAN, &q) && q == 0u, "and so is one that is not a number");
    ut_check(!mp_enemy_burst_quarters(0.1f, &q) && q == 0u,
             "a speed that would round to no quarter at all is refused, not sent as no burst");
    ut_check(!mp_enemy_burst_quarters(64.0f, &q) && q == 0u,
             "above 63.75 it is refused rather than cut down to the largest byte");
    ut_check(!mp_enemy_burst_quarters(INFINITY, &q) && q == 0u, "and so is an infinity");
    for (v = 1u; v < 256u; ++v) {
        float here = mp_enemy_burst_speed((uint8_t)v);
        float less = mp_enemy_burst_speed((uint8_t)(v - 1u));

        finite_and_rising = finite_and_rising && isfinite(here) && here > less;
    }
    ut_check(finite_and_rising && mp_enemy_burst_speed(0u) == 0.0f,
             "every byte a stranger can send decodes to a finite speed, 0 to none");
}

static void check_the_observation(void)
{
    ut_section("the engine burst a body when bit 0 of its flags went from set to clear");
    ut_check(mp_enemy_burst_observe(true, 0x03u, true, 0x02u) == MP_ENEMY_BURST_SEEN_BURST,
             "drawn before and hidden after is a burst");
    ut_check(mp_enemy_burst_observe(true, 0x03u, true, 0x03u) == MP_ENEMY_BURST_SEEN_DRAWN,
             "drawn after as well is a gate, or the engine giving up inside");
    ut_check(mp_enemy_burst_observe(true, 0x02u, true, 0x02u) == MP_ENEMY_BURST_SEEN_HIDDEN,
             "hidden before is the engine's own gate for an invisible body");
    ut_check(mp_enemy_burst_observe(false, 0u, true, 0u) == MP_ENEMY_BURST_SEEN_HIDDEN,
             "and a body that did not read before is counted with those");
    ut_check(mp_enemy_burst_observe(true, 0x01u, false, 0u) == MP_ENEMY_BURST_SEEN_DRAWN,
             "a body that did not read after is not called a burst");
}

static void check_the_ring(void)
{
    mp_enemy_burst_ring_t    ring;
    mp_enemy_burst_callers_t callers;
    float                    speed = 0.0f;
    uint32_t                 i;

    ut_section("the ring holds a substep's bursts until the removal of the same body takes one");
    memset(&ring, 0, sizeof ring);
    memset(&callers, 0, sizeof callers);

    ut_check(mp_enemy_burst_ring_put(&ring, 0x5000u, 1.0f, 40u, 0x00435C1Eu), "a burst is kept");
    ut_check(!mp_enemy_burst_ring_find(&ring, 0x6000u, 40u, &speed),
             "a removal of another body finds nothing");
    ut_check(!mp_enemy_burst_ring_find(&ring, 0x5000u, 41u, &speed),
             "and neither does one in another substep: an address can be taken again");
    ut_check(mp_enemy_burst_ring_find(&ring, 0x5000u, 40u, &speed) && speed == 1.0f,
             "the same body in the same substep finds its speed");
    ut_check(mp_enemy_burst_ring_take(&ring, 0x5000u, 40u), "and takes it");
    ut_check(!mp_enemy_burst_ring_take(&ring, 0x5000u, 40u), "once");

    ut_check(mp_enemy_burst_ring_put(&ring, 0x5000u, 1.0f, 41u, 0x00435C1Eu) &&
                 mp_enemy_burst_ring_put(&ring, 0x5000u, 2.0f, 41u, 0x00435C1Eu) &&
                 ring.count == 1u,
             "the same body twice in one substep keeps one entry");
    ut_check(mp_enemy_burst_ring_find(&ring, 0x5000u, 41u, &speed) && speed == 2.0f,
             "and it is the newer");

    for (i = 1u; i < MP_ENEMY_BURST_RING_SLOTS; ++i) {
        (void)mp_enemy_burst_ring_put(&ring, 0x7000u + i * 0x10u, 0.5f, 41u, 0x00456079u);
    }
    ut_check(ring.count == MP_ENEMY_BURST_RING_SLOTS, "the ring fills");
    ut_check(!mp_enemy_burst_ring_put(&ring, 0x9000u, 0.5f, 41u, 0x00454BB2u),
             "a burst past a full ring is refused, for the caller to count");
    ut_check(mp_enemy_burst_ring_find(&ring, 0x5000u, 41u, &speed) && speed == 2.0f,
             "and nothing in it was overwritten");

    ut_checkf(mp_enemy_burst_ring_drop(&ring, &callers) == MP_ENEMY_BURST_RING_SLOTS &&
                  ring.count == 0u,
              "the end of the substep drops what no removal took, and counts it");
    ut_check(callers.address[0] == 0x00435C1Eu && callers.count[0] == 1u &&
                 callers.address[1] == 0x00456079u &&
                 callers.count[1] == MP_ENEMY_BURST_RING_SLOTS - 1u,
             "with the callers the bursts came from, for the line");
}

static void check_the_host_attaches(void)
{
    uint8_t q = 99u;

    ut_section("the host attaches a burst to a removal only when the note can carry it");
    ut_check(mp_enemy_burst_attach(false, 1.0f, 0u, &q) == MP_ENEMY_BURST_ATTACH_NONE && q == 0u,
             "no burst of this body in this substep attaches nothing");
    ut_check(mp_enemy_burst_attach(true, 1.0f, 0u, &q) == MP_ENEMY_BURST_ATTACH_CARRIED && q == 4u,
             "a burst at 1.0 rides as 4");
    ut_check(mp_enemy_burst_attach(true, 1.0f, 0x2000u, &q) == MP_ENEMY_BURST_ATTACH_PLAYER &&
                 q == 0u,
             "an actor that carries the player gets none: the client reads the same bit");
    ut_check(mp_enemy_burst_attach(true, 99.0f, 0u, &q) == MP_ENEMY_BURST_ATTACH_NO_SPEED &&
                 q == 0u,
             "a speed the byte cannot hold rides as none, and the removal still goes");
    ut_check(mp_enemy_burst_carries_player(0x2000u) && !mp_enemy_burst_carries_player(0x1FFFu),
             "the bit both ends read is 0x2000");
}

static void check_the_gates_are_named(void)
{
    mp_enemy_burst_reading_t r;

    ut_section("a burst that did not happen is named in the engine's own order");
    memset(&r, 0, sizeof r);
    r.flags_read  = true;
    r.flags       = 0x01u;
    r.radius_read = true;
    r.radius      = 0.139f;
    r.nodes_read  = true;
    r.nodes       = 18u;
    r.free_read   = true;
    r.free_things = 100u;
    ut_check(mp_enemy_burst_gate_name(&r) == MP_ENEMY_BURST_GATE_NONE,
             "a body every gate lets through names none: the engine stopped inside");
    r.free_things = 9u;
    ut_check(mp_enemy_burst_gate_name(&r) == MP_ENEMY_BURST_GATE_CROWDED,
             "18 nodes against 9 free objects is too few: nodes at or above twice the free");
    r.free_things = 0u;
    ut_check(mp_enemy_burst_gate_name(&r) == MP_ENEMY_BURST_GATE_CROWDED,
             "none free is always too few");
    r.flags = 0x02u;
    ut_check(mp_enemy_burst_gate_name(&r) == MP_ENEMY_BURST_GATE_HIDDEN,
             "a hidden body is named before the free objects, as the engine tests it");
    r.radius = 1.0f;
    ut_check(mp_enemy_burst_gate_name(&r) == MP_ENEMY_BURST_GATE_WIDE,
             "a radius of 1.0 is too wide, and named before hidden");
    r.player_body = true;
    ut_check(mp_enemy_burst_gate_name(&r) == MP_ENEMY_BURST_GATE_PLAYER,
             "and the player's body is the first gate of all");
    memset(&r, 0, sizeof r);
    ut_check(mp_enemy_burst_gate_name(&r) == MP_ENEMY_BURST_GATE_NONE,
             "readings that did not read name nothing rather than a guess");
}

/* The client's decision as it stood before the burst byte existed, written out: a note for a life
 * this side has replaced touches nothing unless it is the corpse kept for that life, and a slot
 * that holds no actor of the life any more is only forgotten. */
static mp_enemy_burst_removal_t old_decision(mp_enemy_slot_t slot, bool newer)
{
    if (newer && slot != MP_ENEMY_SLOT_KEPT) {
        return MP_ENEMY_BURST_REMOVAL_STALE;
    }
    if (!(slot == MP_ENEMY_SLOT_LIVE || slot == MP_ENEMY_SLOT_KEPT)) {
        return MP_ENEMY_BURST_REMOVAL_GONE;
    }
    return MP_ENEMY_BURST_REMOVAL_PERFORM;
}

static void check_the_client_decides(void)
{
    static const mp_enemy_slot_t SLOTS[] = { MP_ENEMY_SLOT_UNREAD, MP_ENEMY_SLOT_FREED,
                                             MP_ENEMY_SLOT_OTHER, MP_ENEMY_SLOT_KEPT,
                                             MP_ENEMY_SLOT_LIVE };
    static const uint8_t QUARTERS[] = { 0u, 4u, 255u };
    size_t               s;
    size_t               q;
    int                  newer;
    bool                 removal_same = true;
    bool                 burst_right  = true;

    ut_section("the removal is decided as before, and only a live actor of this life bursts");
    for (s = 0; s < sizeof SLOTS / sizeof SLOTS[0]; ++s) {
        for (newer = 0; newer <= 1; ++newer) {
            for (q = 0; q < sizeof QUARTERS / sizeof QUARTERS[0]; ++q) {
                mp_enemy_burst_verdict_t v =
                    mp_enemy_burst_decide_for(SLOTS[s], newer != 0, QUARTERS[q]);
                bool expect_burst = SLOTS[s] == MP_ENEMY_SLOT_LIVE && newer == 0 &&
                                    QUARTERS[q] != 0u;

                removal_same = removal_same && v.removal == old_decision(SLOTS[s], newer != 0);
                burst_right  = burst_right && v.burst == expect_burst;
            }
        }
    }
    ut_check(removal_same,
             "over every slot, both lives and three speeds the removal is the old decision");
    ut_check(burst_right, "a kept corpse is never burst, not even one of this life");
}

static void check_the_note(void)
{
    mp_event_t event;
    mp_event_t back;
    uint8_t    note[MP_EVENT_MAX_BYTES];
    size_t     bytes;
    unsigned   v;
    bool       all_taken = true;

    ut_section("the removal note is twelve bytes and its last byte is the burst");
    memset(&event, 0, sizeof event);
    event.kind             = MP_EVENT_DESPAWN;
    event.tick             = 77u;
    event.level_id         = 528u;
    event.actor_index      = 300u;
    event.actor_generation = 3u;
    event.actor_reason     = MP_EVENT_REMOVE_DELETED;
    event.actor_burst      = 8u;
    bytes = mp_event_encode(&event, note, sizeof note);
    ut_checkf(bytes == 12u && MP_EVENT_DESPAWN_BYTES == 12u, "it encodes to 12 bytes (%u)",
              (unsigned)bytes);
    ut_check(mp_event_decode(note, bytes, &back) && back.actor_burst == 8u &&
                 back.actor_index == 300u && back.actor_reason == MP_EVENT_REMOVE_DELETED &&
                 back.actor_generation == 3u && back.level_id == 528u,
             "and comes back whole, the burst included");
    for (v = 0u; v < 256u; ++v) {
        note[11] = (uint8_t)v;
        all_taken = all_taken && mp_event_decode(note, bytes, &back) && back.actor_burst == v;
    }
    ut_check(all_taken, "every value of the burst byte decodes: the speed is checked where used");
    ut_check(!mp_event_is_event(note, 11u),
             "an eleven byte note, a removal of wire 29, is not taken for a removal any more");
    ut_check(MP_LEVEL_STATE_TAG != MP_EVENT_DESPAWN && MP_WORLD_STATE_TAG != MP_EVENT_DESPAWN,
             "the two twelve byte headers on the channel carry other tags");
}

static void check_the_pieces_are_nobodys(void)
{
    ut_section("the pieces are class 0, and a class 0 shot is nobody's");
    ut_check(mp_shot_whose(0, true, false) == MP_SHOT_NOBODYS &&
                 mp_shot_whose(0, false, false) == MP_SHOT_NOBODYS,
             "so a piece never enters the NPC bolt path and never travels");
}

static void check_without_a_game(void)
{
    ut_section("with no game in the process the hull does not stand");
    ut_check(!mp_enemy_burst_install(), "the site does not resolve, so nothing is detoured");
}

int main(void)
{
    check_the_quarters();
    check_the_observation();
    check_the_ring();
    check_the_host_attaches();
    check_the_gates_are_named();
    check_the_client_decides();
    check_the_note();
    check_the_pieces_are_nobodys();
    check_without_a_game();
    return ut_summary("mp_enemy_burst");
}
