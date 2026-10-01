/* An NPC's bolt on the wire, and the rules the relay leans on.
 *
 * The codec says a bolt again and alike and refuses what is not one; the kinds that travel are
 * the plain bolts and the bolts with a handler, the thermal detonator only where its sub shots are
 * tied, and never the area blast; a held bolt waits for the replay, fires when it is reached and
 * is dropped when the replay is far past it; and the memory of the bolts that travel forgets by
 * age, by reuse of the object and by kind, lets a bolt a sabre turned back go, and ties the
 * fireball of a detonator's impact to the detonator.
 *
 * The rule and the memory before the detonator travelled are kept below, word for word in what
 * they decided, and every kind but the detonator is held to deciding as they did.
 */
#include "unittest.h"

#include "mp_npc_shot.h"
#include "mp_shot_sites.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The rule as it stood while the detonator stayed on the host. */
static bool old_kind_travels(uint32_t kind, bool has_handler)
{
    if (kind >= MP_NPC_SHOT_KINDS || kind == 0x23u) {
        return false;
    }
    if (!has_handler) {
        return true;
    }
    return kind != 10u && kind != 24u;
}

/* The memory as it stood: one entry of the class a bolt was fired with, compared with the side. */
typedef struct old_memory {
    uint32_t object[MP_NPC_SHOT_MEMORY];
    uint32_t tick[MP_NPC_SHOT_MEMORY];
    uint8_t  shooter_class[MP_NPC_SHOT_MEMORY];
    size_t   next;
} old_memory_t;

static void old_remember(old_memory_t *memory, uint32_t object, uint8_t shooter_class,
                         uint32_t tick)
{
    size_t at = memory->next % MP_NPC_SHOT_MEMORY;

    memory->object[at]        = object;
    memory->tick[at]          = tick;
    memory->shooter_class[at] = shooter_class;
    memory->next              = (at + 1u) % MP_NPC_SHOT_MEMORY;
}

static bool old_still_npcs(const old_memory_t *memory, uint32_t object, uint32_t side_now,
                           uint32_t tick_now)
{
    size_t step;

    if (object == 0u) {
        return false;
    }
    for (step = 1u; step <= MP_NPC_SHOT_MEMORY; ++step) {
        size_t at = (memory->next + MP_NPC_SHOT_MEMORY - step) % MP_NPC_SHOT_MEMORY;

        if (memory->object[at] != object) {
            continue;
        }
        if ((uint32_t)(tick_now - memory->tick[at]) > MP_NPC_SHOT_LIFETIME) {
            return false;
        }
        return side_now == (uint32_t)memory->shooter_class[at];
    }
    return false;
}

/* What the engine's spawn leaves in a bolt's side: the class it was fired with, except for the
 * detonator, whose spawn arm clears it. */
static uint32_t side_after_spawn(uint32_t kind, uint32_t shooter_class)
{
    return kind == MP_NPC_SHOT_THERMAL ? 0u : shooter_class;
}

static mp_npc_shot_t a_shot(void)
{
    mp_npc_shot_t shot;

    memset(&shot, 0, sizeof shot);
    shot.tick          = 123456u;
    shot.level         = 0x0123u;
    shot.kind          = 2u;
    shot.shooter_class = 3u;
    shot.muzzle[0]     = 12.5f;
    shot.muzzle[1]     = -40.25f;
    shot.muzzle[2]     = 3.0f;
    shot.pitch         = 10.0f;
    shot.yaw           = 270.0f;
    return shot;
}

static void check_the_codec(void)
{
    mp_npc_shot_t shot = a_shot();
    mp_npc_shot_t back;
    uint8_t       note[MP_NPC_SHOT_BYTES + 4u];
    size_t        bytes;
    int           axis;

    ut_section("the codec");
    bytes = mp_npc_shot_encode(&shot, note, sizeof note);
    ut_checkf(bytes == MP_NPC_SHOT_BYTES, "a bolt is %u bytes", (unsigned)bytes);
    ut_check(note[0] == (uint8_t)MP_NPC_SHOT_TAG, "and begins with its tag");
    ut_check(mp_npc_shot_is(note, bytes), "the recogniser knows it");
    ut_check(mp_npc_shot_decode(note, bytes, &back), "and the decoder takes it");
    ut_check(back.tick == shot.tick && back.level == shot.level && back.kind == shot.kind &&
             back.shooter_class == shot.shooter_class, "tick, level, kind and class come back");
    for (axis = 0; axis < 3; ++axis) {
        ut_near(back.muzzle[axis], shot.muzzle[axis], 0.01, "the muzzle comes back");
    }
    ut_near(back.pitch, shot.pitch, 0.01, "the pitch comes back");
    ut_near(back.yaw, shot.yaw, 0.01, "the yaw comes back");

    ut_check(!mp_npc_shot_is(note, bytes - 1u), "one byte short is not a bolt");
    ut_check(mp_npc_shot_encode(&shot, note, MP_NPC_SHOT_BYTES - 1u) == 0u,
             "a buffer too small is refused rather than written short");
    note[0] = 0x81u;
    ut_check(!mp_npc_shot_is(note, bytes), "another tag is not a bolt");

    shot.shooter_class = 1u;
    bytes = mp_npc_shot_encode(&shot, note, sizeof note);
    ut_check(mp_npc_shot_decode(note, bytes, &back) && back.shooter_class == 1u,
             "class 1 travels, as an ally's bolt: a player's own never comes here");
    shot.shooter_class = 0u;
    bytes = mp_npc_shot_encode(&shot, note, sizeof note);
    ut_check(!mp_npc_shot_decode(note, bytes, &back), "the engine's own effects are refused");
    shot = a_shot();
    shot.kind = (uint8_t)MP_NPC_SHOT_KINDS;
    bytes = mp_npc_shot_encode(&shot, note, sizeof note);
    ut_check(!mp_npc_shot_decode(note, bytes, &back), "a kind past the table is refused");
}

static void check_the_kinds(void)
{
    uint32_t kind;
    int      handler;
    bool     as_before_untied = true;
    bool     as_before_tied   = true;

    ut_section("which kinds travel");
    ut_check(mp_npc_shot_kind_travels(2u, false, true), "a plain bolt travels");
    ut_check(mp_npc_shot_kind_travels(14u, true, true), "the zapper travels, handler and all");
    ut_check(mp_npc_shot_kind_travels(16u, true, true) && mp_npc_shot_kind_travels(19u, true, true),
             "and so do the shattering bolt and the spinning ricochet");
    /* The explosives travel. The rocket, the grenade and the energy ball post their area ring from
     * the shot's own object, which is the object the sender remembers, so the guard against
     * hurting twice still sees them. */
    ut_check(mp_npc_shot_kind_travels(8u, true, true) && mp_npc_shot_kind_travels(5u, true, true),
             "the rocket and the grenade travel, area ring and all");
    ut_check(mp_npc_shot_kind_travels(12u, true, true) &&
                 mp_npc_shot_kind_travels(11u, true, true),
             "and so do the energy ball and the force wave");

    /* The detonator was held back because its cleared side defeated the guard and its fireball
     * was nobody's. It travels now where the machine ties the fireball to it, and only there. */
    ut_check(!mp_npc_shot_kind_travels(10u, true, false),
             "the thermal detonator stays where its impact is not hulled: a fireball nobody owns "
             "would hurt twice");
    ut_check(mp_npc_shot_kind_travels(10u, true, true),
             "and travels where the fireball of its impact is tied to it");
    ut_check(!mp_npc_shot_kind_travels(24u, true, true),
             "the player zap stays either way: a copy would hurt the client's own player");
    ut_check(!mp_npc_shot_kind_travels(0x23u, false, true), "the area blast is not a bolt");
    ut_check(!mp_npc_shot_kind_travels(MP_NPC_SHOT_KINDS, false, true),
             "a kind past the table is nothing");

    /* Against the old rule, over every kind with and without a handler and a few past the table:
     * untied it decides exactly as before, and tied it differs in the detonator alone. */
    for (kind = 0u; kind < MP_NPC_SHOT_KINDS + 4u; ++kind) {
        for (handler = 0; handler < 2; ++handler) {
            bool old = old_kind_travels(kind, handler != 0);

            if (mp_npc_shot_kind_travels(kind, handler != 0, false) != old) {
                as_before_untied = false;
            }
            if (mp_npc_shot_kind_travels(kind, handler != 0, true) !=
                (old || (kind == 10u && handler != 0))) {
                as_before_tied = false;
            }
        }
    }
    ut_check(as_before_untied, "without the tie every kind decides as the old rule did");
    ut_check(as_before_tied, "with it the detonator with its handler is the only difference");
}

static void check_when_a_held_bolt_fires(void)
{
    ut_section("when a held bolt fires");
    ut_check(mp_npc_shot_due(100u, 100u, true) == MP_NPC_SHOT_FIRE, "on its tick");
    ut_check(mp_npc_shot_due(101u, 100u, true) == MP_NPC_SHOT_WAIT, "not before it");
    ut_check(mp_npc_shot_due(100u, 100u + MP_NPC_SHOT_LATE_TICKS, true) == MP_NPC_SHOT_FIRE,
             "a little late it is still fired");
    ut_check(mp_npc_shot_due(100u, 101u + MP_NPC_SHOT_LATE_TICKS, true) == MP_NPC_SHOT_STALE,
             "past the margin it is dropped");
    ut_check(mp_npc_shot_due(101u + MP_NPC_SHOT_AHEAD_TICKS, 100u, true) == MP_NPC_SHOT_FIRE,
             "a replay that has lost its place does not hold everything behind it");
    ut_check(mp_npc_shot_due(5000u, 100u, false) == MP_NPC_SHOT_FIRE,
             "with no replay known yet it is fired as it comes");
    ut_check(mp_npc_shot_due(2u, 0xFFFFFFFEu, true) == MP_NPC_SHOT_WAIT,
             "and the comparison survives the counter wrapping");
}

static void check_the_memory(void)
{
    static mp_npc_shot_memory_t memory;   /* forty kilobytes, off the stack */
    uint32_t                    i;
    bool                        filling_quiet = true;

    ut_section("the memory of the bolts that travel");
    mp_npc_shot_memory_clear(&memory);
    ut_check(!mp_npc_shot_still_npcs(&memory, 0x1000u, 3u, 2u, 10u),
             "an empty memory knows nothing");
    mp_npc_shot_remember(&memory, 0x1000u, 3u, 2u, 10u);
    ut_check(mp_npc_shot_still_npcs(&memory, 0x1000u, 3u, 2u, 10u), "a bolt just sent is known");
    ut_check(!mp_npc_shot_still_npcs(&memory, 0x1000u, 1u, 2u, 11u),
             "one a player's sabre turned back is that player's and no longer the NPC's");
    ut_check(!mp_npc_shot_still_npcs(&memory, 0x1000u, 3u, 5u, 11u),
             "an object that flies as another kind now is another shot");
    ut_check(!mp_npc_shot_still_npcs(&memory, 0x1000u, 3u, 0xFFFFFFFFu, 11u),
             "and one that is no shot at all is nobody's bolt");
    ut_check(mp_npc_shot_still_npcs(&memory, 0x1000u, 3u, 2u, 10u + MP_NPC_SHOT_LIFETIME),
             "one as old as its lifetime is still known");
    ut_check(!mp_npc_shot_still_npcs(&memory, 0x1000u, 3u, 2u, 11u + MP_NPC_SHOT_LIFETIME),
             "and one older is forgotten");
    ut_check(!mp_npc_shot_still_npcs(&memory, 0u, 3u, 2u, 10u), "a null object is never a bolt");
    for (i = 0; i < MP_NPC_SHOT_MEMORY; ++i) {
        mp_npc_shot_remember(&memory, 0x2000u + i * 4u, 3u, 2u, 20u);
    }
    ut_check(!mp_npc_shot_still_npcs(&memory, 0x1000u, 3u, 2u, 20u),
             "a full ring forgets the oldest");
    ut_check(mp_npc_shot_still_npcs(&memory, 0x2000u, 3u, 2u, 20u) &&
             mp_npc_shot_still_npcs(&memory, 0x2000u + (MP_NPC_SHOT_MEMORY - 1u) * 4u, 3u, 2u,
                                    20u),
             "and keeps the newest");
    mp_npc_shot_remember(&memory, 0x2000u, 9u, 2u, 30u);
    ut_check(mp_npc_shot_still_npcs(&memory, 0x2000u, 9u, 2u, 30u),
             "an object the engine handed out again is its latest bolt, with that bolt's side");

    ut_section("the memory forgets an object the engine makes a new shot on");
    mp_npc_shot_memory_clear(&memory);
    mp_npc_shot_remember(&memory, 0x3000u, 3u, 2u, 40u);
    mp_npc_shot_remember(&memory, 0x3000u, 4u, 2u, 41u);
    mp_npc_shot_remember(&memory, 0x3004u, 3u, 2u, 41u);
    ut_check(mp_npc_shot_forget(&memory, 0x3000u, 42u) == 2u, "both entries of the object go");
    ut_check(!mp_npc_shot_still_npcs(&memory, 0x3000u, 3u, 2u, 42u) &&
                 !mp_npc_shot_still_npcs(&memory, 0x3000u, 4u, 2u, 42u),
             "and neither answers any more, whatever side it reads");
    ut_check(mp_npc_shot_still_npcs(&memory, 0x3004u, 3u, 2u, 42u), "the neighbour stays");
    ut_check(mp_npc_shot_forget(&memory, 0x5000u, 42u) == 0u &&
                 mp_npc_shot_forget(&memory, 0u, 42u) == 0u,
             "an object it never held, or none, drops nothing");
    ut_check(mp_npc_shot_forget(&memory, 0x3004u, 42u + MP_NPC_SHOT_LIFETIME) == 0u,
             "and one past the lifetime answers nothing and is left alone");

    ut_section("a full memory says when it writes over an entry still young");
    mp_npc_shot_memory_clear(&memory);
    for (i = 0; i < MP_NPC_SHOT_MEMORY; ++i) {
        filling_quiet = filling_quiet &&
                        !mp_npc_shot_remember(&memory, 0x8000u + i * 4u, 3u, 2u, 50u);
    }
    ut_check(filling_quiet, "filling the ring once pushes nothing out");
    ut_check(mp_npc_shot_remember(&memory, 0x9000u, 3u, 2u, 51u),
             "the first entry past the ring's size pushes out a young one, and says so");
    ut_check(!mp_npc_shot_remember(&memory, 0x9004u, 3u, 2u, 51u + MP_NPC_SHOT_LIFETIME),
             "an entry past the lifetime is written over without a word");
    mp_npc_shot_memory_clear(&memory);
    mp_npc_shot_remember(&memory, 0x9008u, 3u, 2u, 60u);
    (void)mp_npc_shot_forget(&memory, 0x9008u, 60u);
    for (i = 1; i < MP_NPC_SHOT_MEMORY; ++i) {
        mp_npc_shot_remember(&memory, 0xA000u + i * 4u, 3u, 2u, 60u);
    }
    ut_check(!mp_npc_shot_remember(&memory, 0xB000u, 3u, 2u, 61u),
             "and a slot already forgotten is no loss either");

    ut_section("which side the memory may hold");
    ut_check(mp_npc_shot_rememberable(0u), "a cleared side, the detonator's, is held");
    ut_check(!mp_npc_shot_rememberable(1u), "a player's side is not: every player carries it");
    ut_check(mp_npc_shot_rememberable(2u) && mp_npc_shot_rememberable(255u),
             "an actor's class is, up to the widest a byte holds");
    ut_check(!mp_npc_shot_rememberable(256u), "and nothing past it");
}

/* The new memory against the old, over the kinds, classes, sides and ages a sender sees. Where the
 * spawn leaves the class in the side, which is every kind but the detonator, the two answer alike;
 * the detonator is the one the old memory never recognised, and the new one does. */
static void check_the_memory_against_the_old(void)
{
    static mp_npc_shot_memory_t memory;
    static old_memory_t         old;
    uint32_t                    kind;
    uint32_t                    shooter_class;
    uint32_t                    side_now;
    uint32_t                    age;
    bool                        alike          = true;
    bool                        thermal_known  = true;
    bool                        thermal_old    = false;

    ut_section("the memory against the one before it");
    for (kind = 0u; kind < MP_NPC_SHOT_KINDS; ++kind) {
        for (shooter_class = 2u; shooter_class <= 9u; ++shooter_class) {
            uint32_t side = side_after_spawn(kind, shooter_class);

            mp_npc_shot_memory_clear(&memory);
            memset(&old, 0, sizeof old);
            old_remember(&old, 0x4000u, (uint8_t)shooter_class, 100u);
            if (mp_npc_shot_rememberable(side)) {
                mp_npc_shot_remember(&memory, 0x4000u, (uint8_t)side, (uint8_t)kind, 100u);
            }
            for (side_now = 0u; side_now <= 10u; ++side_now) {
                for (age = 0u; age <= MP_NPC_SHOT_LIFETIME + 2u; age += 18u) {
                    bool before = old_still_npcs(&old, 0x4000u, side_now, 100u + age);
                    bool now = mp_npc_shot_still_npcs(&memory, 0x4000u, side_now, kind,
                                                      100u + age);

                    if (kind != MP_NPC_SHOT_THERMAL && before != now) {
                        alike = false;
                    }
                    if (kind == MP_NPC_SHOT_THERMAL && side_now == 0u &&
                        age <= MP_NPC_SHOT_LIFETIME) {
                        thermal_known = thermal_known && now;
                        thermal_old   = thermal_old || before;
                    }
                }
            }
        }
    }
    ut_check(alike, "every kind but the detonator is recognised exactly as before");
    ut_check(!thermal_old, "the old memory never recognised a detonator by its cleared side");
    ut_check(thermal_known, "the new one does, for its whole lifetime");
}

/* The same memory searched to its end, the way it was before the search stopped at the first
 * entry past the lifetime, and forgetting at every age. */
static bool full_still_npcs(const mp_npc_shot_memory_t *memory, uint32_t object, uint32_t side_now,
                            uint32_t kind_now, uint32_t tick_now)
{
    size_t step;

    for (step = 1u; step <= MP_NPC_SHOT_MEMORY; ++step) {
        size_t at = (memory->next + MP_NPC_SHOT_MEMORY - step) % MP_NPC_SHOT_MEMORY;

        if (memory->object[at] != object) {
            continue;
        }
        if ((uint32_t)(tick_now - memory->tick[at]) > MP_NPC_SHOT_LIFETIME) {
            return false;
        }
        return side_now == memory->side[at] && kind_now == memory->kind[at];
    }
    return false;
}

static void full_forget(mp_npc_shot_memory_t *memory, uint32_t object)
{
    size_t at;

    for (at = 0u; at < MP_NPC_SHOT_MEMORY; ++at) {
        if (memory->object[at] == object) {
            memory->object[at] = 0u;
        }
    }
}

/* A long run of bolts made on a small pool of objects, so that objects are handed out again, the
 * ring goes round several times and most entries age past the lifetime; every question is asked of
 * both memories. The search that stops early has to answer exactly as the one that does not. */
static void check_the_search_stops_early_and_answers_alike(void)
{
    static mp_npc_shot_memory_t fast;
    static mp_npc_shot_memory_t full;
    static const uint8_t        sides[4] = { 0u, 2u, 3u, 5u };
    static const uint8_t        kinds[4] = { 2u, 8u, 10u, 0x1Eu };
    uint32_t                    seed     = 12345u;
    uint32_t                    tick     = 1000u;
    uint32_t                    step;
    uint32_t                    asked    = 0u;
    uint32_t                    known    = 0u;
    bool                        alike    = true;

    ut_section("the search stops at the first entry past the lifetime and answers alike");
    mp_npc_shot_memory_clear(&fast);
    mp_npc_shot_memory_clear(&full);
    for (step = 0u; step < 3u * MP_NPC_SHOT_MEMORY; ++step) {
        uint32_t object;
        uint8_t  side;
        uint8_t  kind;

        seed   = seed * 1103515245u + 12345u;
        tick  += (seed >> 16) % 3u;
        object = 0x10000u + ((seed >> 8) % 96u) * 4u;
        side   = sides[(seed >> 4) % 4u];
        kind   = kinds[(seed >> 12) % 4u];
        if (((seed >> 20) & 1u) == 0u) {
            (void)mp_npc_shot_forget(&fast, object, tick);
            full_forget(&full, object);
            (void)mp_npc_shot_remember(&fast, object, side, kind, tick);
            (void)mp_npc_shot_remember(&full, object, side, kind, tick);
        } else {
            bool a = mp_npc_shot_still_npcs(&fast, object, side, kind, tick);
            bool b = full_still_npcs(&full, object, side, kind, tick);

            ++asked;
            known += a ? 1u : 0u;
            alike = alike && a == b;
        }
    }
    ut_checkf(alike, "%u question(s) over %u step(s), each answered alike", (unsigned)asked,
              (unsigned)(3u * MP_NPC_SHOT_MEMORY));
    ut_checkf(known > 0u && known < asked, "and the run held both answers: %u known of %u",
              (unsigned)known, (unsigned)asked);
}

/* A sub shot is a class 0 spawn inside an impact. */
static void check_the_sub_shots(void)
{
    ut_section("what a shot made inside an impact is");
    ut_check(mp_npc_shot_sub_verdict(true, true, 0, MP_NPC_SHOT_FIREBALL) ==
                 MP_NPC_SHOT_SUB_TIED,
             "the fireball of a remembered detonator's impact is its child");
    ut_check(mp_npc_shot_sub_verdict(true, true, 0, MP_NPC_SHOT_EXPLOSION_RING) ==
                 MP_NPC_SHOT_SUB_TIED,
             "and so is the ring of a remembered rocket's");
    ut_check(mp_npc_shot_sub_verdict(true, false, 0, MP_NPC_SHOT_FIREBALL) ==
                 MP_NPC_SHOT_SUB_OF_ANOTHER,
             "the fireball of a bolt not held here is somebody else's");
    ut_check(mp_npc_shot_sub_verdict(false, false, 0, MP_NPC_SHOT_FIREBALL) ==
                 MP_NPC_SHOT_SUB_ORPHAN &&
                 mp_npc_shot_sub_verdict(false, true, 0, MP_NPC_SHOT_EXPLOSION_RING) ==
                     MP_NPC_SHOT_SUB_ORPHAN,
             "a ring or a fireball outside every impact has no parent");
    ut_check(mp_npc_shot_sub_verdict(false, false, 0, 0x20u) == MP_NPC_SHOT_SUB_NONE &&
                 mp_npc_shot_sub_verdict(false, false, 0, 0x22u) == MP_NPC_SHOT_SUB_NONE &&
                 mp_npc_shot_sub_verdict(false, false, 0, 0x25u) == MP_NPC_SHOT_SUB_NONE,
             "a muzzle flare, a bubble or an energy ball's burst is no sub shot of an impact");
    ut_check(mp_npc_shot_sub_verdict(true, true, 3, MP_NPC_SHOT_FIREBALL) ==
                 MP_NPC_SHOT_SUB_NONE &&
                 mp_npc_shot_sub_verdict(false, false, 1, MP_NPC_SHOT_EXPLOSION_RING) ==
                     MP_NPC_SHOT_SUB_NONE,
             "and a shot fired with a class is nobody's sub shot, inside an impact or not");
}

/* A detonator's life on the host, the way the relay walks it: made, sent and remembered by its
 * cleared side; its impact makes a fireball, which is tied; the fireball's rings are the NPC's
 * for as long as it posts them; and the engine hands the detonator's object to the host player's
 * own detonator afterwards, which the memory drops as soon as the new shot is made. */
static void check_a_detonator_on_the_host(void)
{
    static mp_npc_shot_memory_t memory;
    const uint32_t              thermal  = 0x6000u;
    const uint32_t              fireball = 0x6100u;
    uint32_t                    tick     = 500u;

    ut_section("a detonator on the host, from the throw to the reuse of its object");
    mp_npc_shot_memory_clear(&memory);
    (void)mp_npc_shot_forget(&memory, thermal, tick);
    mp_npc_shot_remember(&memory, thermal, (uint8_t)side_after_spawn(10u, 3u), 10u, tick);
    ut_check(mp_npc_shot_still_npcs(&memory, thermal, 0u, 10u, tick + 20u),
             "a hit by the detonator itself is the NPC's, cleared side and all");

    tick += 40u;
    (void)mp_npc_shot_forget(&memory, fireball, tick);
    ut_check(mp_npc_shot_sub_verdict(true,
                                     mp_npc_shot_still_npcs(&memory, thermal, 0u, 10u, tick), 0,
                                     MP_NPC_SHOT_FIREBALL) == MP_NPC_SHOT_SUB_TIED,
             "its impact's fireball is tied to it");
    mp_npc_shot_remember(&memory, fireball, 0u, (uint8_t)MP_NPC_SHOT_FIREBALL, tick);
    ut_check(mp_npc_shot_still_npcs(&memory, fireball, 0u, MP_NPC_SHOT_FIREBALL, tick + 43u),
             "and the fireball's last ring, two thirds of two seconds later, is the NPC's too");

    tick += 60u;
    ut_check(mp_npc_shot_forget(&memory, thermal, tick) == 1u,
             "the host player's own detonator made on the old object drops the entry");
    ut_check(!mp_npc_shot_still_npcs(&memory, thermal, 0u, 10u, tick),
             "so its hits, the same kind with the same cleared side, are the player's again");
}

/* A detonator a player turned back with the sabre before it went off is that player's, and so is
 * the fireball of its impact. */
static void check_a_detonator_turned_back(void)
{
    static mp_npc_shot_memory_t memory;

    ut_section("a detonator turned back before it went off");
    mp_npc_shot_memory_clear(&memory);
    mp_npc_shot_remember(&memory, 0x7000u, 0u, 10u, 900u);
    ut_check(!mp_npc_shot_still_npcs(&memory, 0x7000u, 1u, 10u, 910u),
             "the sabre wrote the player's side, so the detonator is no longer the NPC's");
    ut_check(mp_npc_shot_sub_verdict(true,
                                     mp_npc_shot_still_npcs(&memory, 0x7000u, 1u, 10u, 910u), 0,
                                     MP_NPC_SHOT_FIREBALL) == MP_NPC_SHOT_SUB_OF_ANOTHER,
             "and the fireball of its impact is not tied to it");
}

static void check_whose_a_shot_is(void)
{
    ut_section("whose a shot is, as the hull sees it made");
    ut_check(mp_shot_whose(1, false, false) == MP_SHOT_PLAYERS,
             "class 1 from anywhere but the AI is the player's");
    ut_check(mp_shot_whose(1, true, false) == MP_SHOT_NPCS,
             "class 1 out of the AI is an ally's, told as an NPC's bolt");
    ut_check(mp_shot_whose(1, false, true) == MP_SHOT_NOBODYS,
             "this machine's copy of an ally's bolt is nobody's to tell again");
    ut_check(mp_shot_whose(2, false, false) == MP_SHOT_NPCS &&
                 mp_shot_whose(9, true, true) == MP_SHOT_NPCS,
             "a class above 1 is an actor's, copy or not, as it always was");
    ut_check(mp_shot_whose(0, true, false) == MP_SHOT_NOBODYS &&
                 mp_shot_whose(-1, false, false) == MP_SHOT_NOBODYS,
             "and the engine's own effects are nobody's");
    ut_check(!mp_shot_sites_resolved() && !mp_shot_sites_is_ai(0x0042C470u),
             "with no image the nine calls are unknown, and no caller is the AI");
}

/* The zap's arcs are replayed at a client, and the engine's arc tick hurts the first class 1 body
 * an arc hangs on. Every combination of the two ends' classes is walked, and none may leave an end
 * of class 1 in the arcs. */
static void check_the_zap_ends(void)
{
    int32_t shooter;
    int32_t victim;
    int     here;
    bool    clean = true;
    uint8_t slot  = 0;

    ut_section("a replayed zap never hangs an arc on a body of class 1");

    for (shooter = -1; shooter <= 10; ++shooter) {
        for (victim = -1; victim <= 10; ++victim) {
            for (here = 0; here < 2; ++here) {
                mp_npc_shot_zap_ends_t ends = mp_npc_shot_zap_ends(shooter, here != 0, victim);

                if ((ends != MP_NPC_SHOT_ZAP_NONE && shooter == 1) ||
                    (ends == MP_NPC_SHOT_ZAP_BOTH && (victim == 1 || here == 0))) {
                    clean = false;
                }
            }
        }
    }
    ut_check(clean, "over every class from -1 to 10 on either end, with or without a body here");
    ut_check(mp_npc_shot_zap_ends(2, true, 5) == MP_NPC_SHOT_ZAP_BOTH,
             "a droid's zap at the host's far body, bank 1 and class 5, reaches it");
    ut_check(mp_npc_shot_zap_ends(2, false, 0) == MP_NPC_SHOT_ZAP_SHOOTER_ALONE,
             "a zap at this machine's own player hangs on the shooter alone");
    ut_check(mp_npc_shot_zap_ends(2, true, 1) == MP_NPC_SHOT_ZAP_SHOOTER_ALONE &&
                 mp_npc_shot_zap_ends(1, true, 5) == MP_NPC_SHOT_ZAP_NONE,
             "and a class 1 end is left out, the shooter's included");

    ut_check(mp_npc_shot_zap_unpack(mp_npc_shot_zap_pack(0u), &slot) && slot == 0u,
             "slot 0, the listen host's, goes round");
    ut_check(mp_npc_shot_zap_unpack(mp_npc_shot_zap_pack(3u), &slot) && slot == 3u,
             "and so does slot 3");
    ut_check(!mp_npc_shot_zap_unpack(0u, &slot) && !mp_npc_shot_zap_unpack(0x0101u, &slot),
             "0 names nobody, and a number past the byte is not a slot");
    ut_check(mp_npc_shot_zap_pack(255u) == 0u, "a slot past the byte is not packed");
}

int main(void)
{
    check_the_zap_ends();
    check_the_codec();
    check_the_kinds();
    check_when_a_held_bolt_fires();
    check_the_memory();
    check_the_memory_against_the_old();
    check_the_search_stops_early_and_answers_alike();
    check_the_sub_shots();
    check_a_detonator_on_the_host();
    check_a_detonator_turned_back();
    check_whose_a_shot_is();
    return ut_summary("mp_npc_shot");
}
