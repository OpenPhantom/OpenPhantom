/* The blade rule's engine half over blocks, actors, bodies, render handles and models made up
 * here, with no game.
 *
 * What this pins is what no test of the rule can: the offsets the blocks are read at, which blocks
 * a choice is held against, the two fields a puppet's blade is judged by, and where the chosen
 * vectors are written. Every made up thing carries a decoy where a wrong offset or one read too
 * few would land, so a mistake answers wrongly instead of by luck. The far banks' blocks are the
 * bank module's own, which exist without an installed bank; the bank refuses every write without
 * one, so a far body's write shows here as a counted fault, which is exactly the write attempted.
 * The player's block is made up and takes its write.
 */
#include "unittest.h"

#include "mp_bank.h"
#include "mp_blade.h"
#include "mp_body.h"
#include "mp_body_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define BLOCK_BYTES  0x3ACu
#define BLOCK_ACTOR  0x00u
#define BLOCK_OBJECT 0x0Cu
#define BLOCK_NODE   0x4Cu
#define BLOCK_HERO   0x6Cu
#define BLOCK_WEAPON 0x84u
#define BLOCK_BLADE  0x1C8u
#define ACTOR_NAME   0x08u
#define ACTOR_MODEL  0xE0u
#define OBJECT_ACTOR 0x14u
#define OBJECT_THING 0x9Cu
#define THING_MODEL  0x04u
#define BLADE_NODE   36u

/* An actor of the asset `name`, whose bind hands its things `model`. At +0xE4, one word past the
 * model, stands another model, where a read one word off would land. */
typedef struct made_actor {
    uint8_t bytes[0x100];
} made_actor_t;

/* A body: the object with its actor at +0x14 and its render handle at +0x9C; the handle with the
 * model it draws at +4 and a decoy model at +0. */
typedef struct made_body {
    uint8_t object[0x100];
    uint8_t thing[0x20];
} made_body_t;

static made_actor_t s_obiwan;
static made_actor_t s_quigon;
static made_actor_t s_panaka;
static made_actor_t s_twin;
static made_actor_t s_lone;
static made_actor_t s_solo;
static made_actor_t s_pair;
static const mp_blade_vectors_t NO_BLADE = { { { 0.0f } } };
static uint8_t      s_model[4][0x10];   /* obiwan, quigon, panaka, and the decoy */
static uint8_t      s_local[BLOCK_BYTES];
static made_body_t  s_local_body;
static made_body_t  s_far_body;
static uint8_t      s_record[BLOCK_BYTES];

static void put_word(uint8_t *at, uint32_t offset, uint32_t value)
{
    memcpy(at + offset, &value, sizeof value);
}

static uint32_t address_of(const void *thing)
{
    return (uint32_t)(uintptr_t)thing;
}

static void make_actor(made_actor_t *actor, const char *name, const void *model)
{
    memset(actor, 0, sizeof *actor);
    memcpy(actor->bytes + ACTOR_NAME, name, strlen(name));
    put_word(actor->bytes, ACTOR_MODEL, address_of(model));
    put_word(actor->bytes, ACTOR_MODEL + 4u, address_of(s_model[3]));
}

/* A reading of a blade `length` long along +y from a hilt that `hilt` tells apart, at `size`. */
static mp_blade_vectors_t reading(float hilt, float length, float size)
{
    mp_blade_vectors_t out;
    int                edge;

    memset(&out, 0, sizeof out);
    for (edge = 0; edge < 2; ++edge) {
        out.vert[edge][0]     = hilt + 0.024f * (float)edge;
        out.vert[edge][2]     = 0.05f;
        out.vert[edge + 2][0] = out.vert[edge][0];
        out.vert[edge + 2][1] = length * size;
        out.vert[edge + 2][2] = 0.05f;
        out.delta[edge][1]    = length * size;
    }
    return out;
}

/* A player block of hero `hero` wearing `actor`, standing with `object` and holding `vectors`.
 * The words beside each field are decoys a read one off would find. */
static void make_block(uint8_t *block, uint32_t hero, const made_actor_t *actor, uint32_t object,
                       const mp_blade_vectors_t *vectors)
{
    memset(block, 0, BLOCK_BYTES);
    put_word(block, BLOCK_ACTOR, address_of(actor));
    put_word(block, BLOCK_ACTOR + 4u, address_of(&s_panaka));
    put_word(block, BLOCK_OBJECT, object);
    put_word(block, BLOCK_HERO, hero);
    put_word(block, BLOCK_HERO - 4u, 7u);
    put_word(block, BLOCK_NODE, BLADE_NODE);
    memcpy(block + BLOCK_BLADE, vectors, sizeof *vectors);
    memset(block + BLOCK_BLADE - 8u, 0x3F, 8u);
}

/* Bank `bank`'s own block, which the bank module keeps whether or not it is installed. */
static uint8_t *bank_block(size_t bank)
{
    return (uint8_t *)mp_bank_block_at(bank);
}

static void stands(size_t bank, bool standing)
{
    mp_body_far_t *far = mp_body_far_at(bank);

    if (far != NULL) {
        far->spawned = standing;
    }
}

static mp_blade_counters_t counted(void)
{
    mp_blade_counters_t out;

    mp_blade_get_counters(&out);
    return out;
}

static bool same(const mp_blade_vectors_t *a, const void *at)
{
    return memcmp(a, at, sizeof *a) == 0;
}

static void set_up(void)
{
    make_actor(&s_obiwan, "ObiWan.baf", s_model[0]);
    make_actor(&s_quigon, "quigon.baf", s_model[1]);
    make_actor(&s_panaka, "panaka.baf", s_model[2]);
    make_actor(&s_twin, "twin.baf", s_model[1]);
    make_actor(&s_lone, "lone.baf", s_model[1]);
    make_actor(&s_solo, "solo.baf", s_model[1]);
    make_actor(&s_pair, "pair.baf", s_model[1]);
}

static void check_a_far_spawn(void)
{
    mp_blade_vectors_t  obi_full = reading(-0.0654f, 0.4269f, 1.0f);
    mp_blade_vectors_t  qui_full = reading(-0.0644f, 0.3881f, 1.0f);
    mp_blade_vectors_t  qui_folded = reading(-0.0644f, 0.3881f, 0.0f);
    mp_blade_vectors_t  full = reading(-0.05f, 0.40f, 1.0f);
    mp_blade_vectors_t  folded = reading(-0.05f, 0.40f, 0.0f);
    mp_blade_counters_t before = counted();
    mp_blade_choice_t   c;

    ut_section("a far Jedi body beside another Jedi keeps its own reading");
    ut_check(bank_block(1u) != NULL && bank_block(3u) != NULL,
             "the three far blocks exist without an installed bank");
    make_block(s_local, 0u, &s_obiwan, 0x1234u, &obi_full);
    make_block(bank_block(1u), 1u, &s_quigon, 0x5678u, &qui_full);
    stands(1u, true);   /* its own bank standing must not make it its own donor */
    ut_check(mp_blade_far_spawn_over(1u, (uintptr_t)s_local, &c) == MP_BLADE_OWN &&
                 same(&c.vectors, &qui_full) && c.learned == MP_BLADE_LEARNED_NEW,
             "Qui-Gon keeps what his spawn read at +0x1C8, not Obi-Wan's, and the book keeps it");
    ut_check(counted().far_own == before.far_own + 1u &&
                 counted().far_jedi == before.far_jedi + 1u && counted().book_known == 1u &&
                 counted().write_faults == before.write_faults,
             "counted as its own, learned under quigon.baf, and nothing written");
    stands(1u, false);

    ut_section("a far body whose reading is folded, the book knowing its asset");
    make_block(bank_block(2u), 1u, &s_quigon, 0x5679u, &qui_folded);
    ut_check(mp_blade_far_spawn_over(2u, (uintptr_t)s_local, &c) == MP_BLADE_BOOK &&
                 same(&c.vectors, &qui_full),
             "the book's full blade for quigon.baf, the name read at the actor's +0x08");
    ut_check(counted().far_book == before.far_book + 1u &&
                 counted().write_faults == before.write_faults + 1u,
             "and a write is attempted, which the uninstalled bank refuses and the module counts");

    ut_section("a far body beside another far body of its asset");
    make_block(bank_block(1u), 1u, &s_twin, 0x5680u, &full);
    make_block(bank_block(2u), 1u, &s_twin, 0x5681u, &folded);
    ut_check(mp_blade_far_spawn_over(2u, (uintptr_t)s_local, &c) == MP_BLADE_FOLDED,
             "a bank whose body does not stand is not asked, whatever its block holds");
    stands(1u, true);
    ut_check(mp_blade_far_spawn_over(2u, (uintptr_t)s_local, &c) == MP_BLADE_DONOR &&
                 same(&c.vectors, &full) && c.learned == MP_BLADE_LEARNED_NEW,
             "once it stands, bank 1's vectors, read out of bank 1's block");
    ut_check(counted().far_bank == before.far_bank + 1u, "counted as another bank's");
    stands(1u, false);
    make_block(bank_block(2u), 1u, &s_twin, 0x5681u, &folded);
    ut_check(mp_blade_far_spawn_over(2u, (uintptr_t)s_local, &c) == MP_BLADE_BOOK,
             "and when it no longer stands, the book answers");

    ut_section("a far body of the local player's asset");
    make_block(s_local, 0u, &s_solo, 0x1234u, &full);
    make_block(bank_block(3u), 0u, &s_solo, 0x5682u, &folded);
    ut_check(mp_blade_far_spawn_over(3u, (uintptr_t)s_local, &c) == MP_BLADE_DONOR &&
                 same(&c.vectors, &full),
             "the local player's vectors, read out of the hero block");
    ut_check(counted().far_local == before.far_local + 1u, "counted as the local player's");

    ut_section("a local player with no body is not asked");
    make_block(s_local, 0u, &s_lone, 0u, &full);
    make_block(bank_block(3u), 0u, &s_lone, 0x5683u, &folded);
    ut_check(mp_blade_far_spawn_over(3u, (uintptr_t)s_local, &c) == MP_BLADE_FOLDED,
             "his block's +0x0C is 0, so his vectors are nobody's to give");
    ut_check(counted().far_folded == before.far_folded + 2u,
             "and the folded body is counted, the second of this test");
    ut_check(mp_blade_far_spawn_over(3u, 0u, &c) == MP_BLADE_FOLDED,
             "no hero block at all is the same");

    ut_section("a far body that is no Jedi");
    make_block(bank_block(1u), 3u, &s_quigon, 0x5684u, &qui_full);
    ut_check(mp_blade_far_spawn_over(1u, (uintptr_t)s_local, &c) == MP_BLADE_NONE &&
                 counted().far_jedi == before.far_jedi + 8u,
             "hero 3 at +0x6C, the foreign slot, is left alone and not counted");
    ut_check(mp_blade_far_spawn_over(0u, (uintptr_t)s_local, &c) == MP_BLADE_NONE &&
                 mp_blade_far_spawn_over(4u, (uintptr_t)s_local, &c) == MP_BLADE_NONE,
             "an index that is no far bank is nothing");
}

static void check_a_local_spawn(void)
{
    mp_blade_vectors_t  qui_full = reading(-0.0644f, 0.3881f, 1.0f);
    mp_blade_vectors_t  qui_folded = reading(-0.0644f, 0.3881f, 0.0f);
    mp_blade_counters_t before = counted();
    mp_blade_choice_t   c;

    ut_section("the player's own spawn, off a mesh a far body folded");
    make_block(s_local, 1u, &s_quigon, 0x1234u, &qui_folded);
    ut_check(mp_blade_local_over((uintptr_t)s_local, false, &c) == MP_BLADE_BOOK &&
                 same(&qui_full, s_local + BLOCK_BLADE),
             "the book's full blade is written into the hero block at +0x1C8");
    ut_check(counted().local_settled == before.local_settled + 1u &&
                 counted().local_put_right == before.local_put_right + 1u,
             "counted as settled and put right");
    ut_check(mp_blade_local_over((uintptr_t)s_local, true, &c) == MP_BLADE_BOOK &&
                 counted().local_put_right == before.local_put_right + 1u,
             "a savegame that holds the full blade already is settled and not written again");

    ut_section("the player's spawn beside a far body of his asset");
    make_block(s_local, 0u, &s_pair, 0x1234u, &qui_folded);
    make_block(bank_block(2u), 0u, &s_pair, 0x5690u, &qui_full);
    stands(2u, true);
    ut_check(mp_blade_local_over((uintptr_t)s_local, false, &c) == MP_BLADE_DONOR &&
                 same(&qui_full, s_local + BLOCK_BLADE) && c.learned == MP_BLADE_LEARNED_NEW,
             "bank 2's vectors, read out of its block and written into the hero block");
    ut_check(counted().local_put_right == before.local_put_right + 2u,
             "the second put right");
    stands(2u, false);

    ut_section("the player as Panaka");
    make_block(s_local, 2u, &s_panaka, 0x1234u, &qui_folded);
    ut_check(mp_blade_local_over((uintptr_t)s_local, false, &c) == MP_BLADE_NONE &&
                 same(&qui_folded, s_local + BLOCK_BLADE),
             "hero 2 has no blade to settle, and nothing is written");
    ut_check(mp_blade_local_over(0u, false, &c) == MP_BLADE_NONE, "no block is nothing");

    ut_section("outside a session");
    before = counted();
    mp_blade_after_local_spawn();
    mp_blade_after_local_restore();
    ut_check(counted().local_settled == before.local_settled,
             "the hulls' calls do nothing with no transport standing");
}

/* The far body the window installed: a record of hero `hero` with blade node `node`, whose
 * object's render handle draws `drawn`. The words beside the two the answer reads are decoys. */
static uintptr_t far_record(uint32_t hero, uint32_t node, const void *drawn)
{
    memset(&s_far_body, 0, sizeof s_far_body);
    put_word(s_far_body.object, OBJECT_ACTOR, address_of(&s_panaka));
    put_word(s_far_body.object, OBJECT_THING, address_of(s_far_body.thing));
    put_word(s_far_body.thing, 0u, address_of(s_model[3]));
    put_word(s_far_body.thing, THING_MODEL, address_of(drawn));
    memset(s_record, 0, sizeof s_record);
    put_word(s_record, BLOCK_OBJECT, address_of(s_far_body.object));
    put_word(s_record, BLOCK_HERO, hero);
    put_word(s_record, BLOCK_HERO - 4u, 1u);
    put_word(s_record, BLOCK_NODE, node);
    put_word(s_record, BLOCK_NODE + 4u, BLADE_NODE);
    return (uintptr_t)s_record;
}

/* The local player standing as `actor` with his sabre out, which no answer may ask about. */
static void local_player(const made_actor_t *actor)
{
    memset(&s_local_body, 0, sizeof s_local_body);
    make_block(s_local, 0u, actor, address_of(s_local_body.object), &NO_BLADE);
    put_word(s_local, BLOCK_WEAPON, 1u);
    put_word(s_local, BLOCK_WEAPON + 4u, 1u);
}

static void check_the_puppet_tick(void)
{
    mp_blade_counters_t before = counted();

    ut_section("the puppet's blade, every pairing, read off the engine");
    local_player(&s_quigon);
    ut_check(mp_blade_puppet_tick(far_record(1u, BLADE_NODE, s_model[1]), false) ==
                 MP_BLADE_TICK_STEP,
             "the same hero as the local player, whose sabre is out: the far body steps its own "
             "length, nothing of the two is shared any more");
    ut_check(mp_blade_puppet_tick(far_record(1u, BLADE_NODE, s_model[0]), false) ==
                 MP_BLADE_TICK_STEP,
             "another Jedi: the same answer");
    ut_check(mp_blade_puppet_tick(far_record(0u, BLADE_NODE, s_model[0]), false) ==
                 MP_BLADE_TICK_STEP,
             "hero 0 as much as hero 1, the hero read at +0x6C and not the word before it");
    ut_check(mp_blade_puppet_tick(far_record(2u, 0u, s_model[2]), false) ==
                 MP_BLADE_TICK_BLADELESS,
             "a far Panaka: withheld");
    ut_check(mp_blade_puppet_tick(far_record(3u, BLADE_NODE, s_model[1]), false) ==
                 MP_BLADE_TICK_BLADELESS,
             "a far character on the foreign slot with a blade node in its rig: withheld, its "
             "spawn read no blade");
    ut_check(mp_blade_puppet_tick(far_record(1u, 0u, s_model[1]), false) ==
                 MP_BLADE_TICK_BLADELESS,
             "a Jedi record without a blade node at +0x4C: withheld, the word after it is a decoy");
    ut_check(mp_blade_puppet_tick(far_record(1u, BLADE_NODE, s_model[0]), true) ==
                 MP_BLADE_TICK_WORN_STEP,
             "a worn Jedi body: stepped in the block, and told apart from one in its own model");
    ut_check(mp_blade_puppet_tick(far_record(2u, BLADE_NODE, s_model[0]), true) ==
                 MP_BLADE_TICK_WORN,
             "a worn body whose hero carries no blade: withheld whole, there is no length");
    ut_check(mp_blade_puppet_tick(0u, false) == MP_BLADE_TICK_BLADELESS,
             "no record: withheld");
    ut_check(counted().write_faults == before.write_faults && counted().far_jedi == before.far_jedi,
             "and asking counts nothing of the vectors");
}

int main(void)
{
    set_up();
    check_a_far_spawn();
    check_a_local_spawn();
    check_the_puppet_tick();

    return ut_summary("mp_blade");
}
