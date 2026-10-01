/* Which sabre vectors a Jedi body keeps, what a puppet's blade does, and the four vertices a far
 * blade is drawn from, with no game.
 *
 * The readings are the two heroes' own blades as their files carry them: node sabreblad01 of
 * obiwan.baf and quigon.baf, the hilt vertices to four places and the two tip deltas by their
 * lengths, 0.4269 and 0.4274 against 0.3881 and 0.3886. A reading at a length below 1 is what a
 * spawn reads off a mesh another body has half grown, and a folded one is the hilt twice over.
 * Every case below is one a simpler rule gets wrong or leaves open.
 */
#include "unittest.h"

#include "mp_blade_rule.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define OBI_WAN "obiwan.baf"
#define QUI_GON "quigon.baf"

/* A hero's blade as its file carries it: two hilt vertices and two deltas, along +y. */
typedef struct hero_blade {
    float hilt[2][3];
    float length[2];
} hero_blade_t;

static const hero_blade_t OBI_WAN_BLADE = {
    { { -0.0654f, 0.0100f, 0.0546f }, { -0.0412f, 0.0103f, 0.0536f } }, { 0.4269f, 0.4274f }
};
static const hero_blade_t QUI_GON_BLADE = {
    { { -0.0644f, 0.0111f, 0.0547f }, { -0.0423f, 0.0114f, 0.0538f } }, { 0.3881f, 0.3886f }
};

/* What a spawn reads off that hero's mesh while it stands at `size` of its length. */
static mp_blade_vectors_t reading(const hero_blade_t *hero, float size)
{
    mp_blade_vectors_t out;
    int                edge;
    int                axis;

    memset(&out, 0, sizeof out);
    for (edge = 0; edge < 2; ++edge) {
        for (axis = 0; axis < 3; ++axis) {
            out.vert[edge][axis]     = hero->hilt[edge][axis];
            out.vert[edge + 2][axis] = hero->hilt[edge][axis];
        }
        out.delta[edge][1]     = hero->length[edge] * size;
        out.vert[edge + 2][1] += hero->length[edge] * size;
    }
    return out;
}

static mp_blade_body_t body(const char *name, uint32_t hero, const mp_blade_vectors_t *vectors)
{
    mp_blade_body_t made;

    memset(&made, 0, sizeof made);
    made.name    = name;
    made.hero    = hero;
    made.vectors = *vectors;
    return made;
}

static bool same(const mp_blade_vectors_t *a, const mp_blade_vectors_t *b)
{
    return memcmp(a, b, sizeof *a) == 0;
}

static void check_the_layout(void)
{
    ut_section("the vectors are the block's 0x48 bytes at +0x1C8");
    ut_check(sizeof(mp_blade_vectors_t) == 0x48u, "four vertices and two deltas are 0x48 bytes");
    ut_check(offsetof(mp_blade_vectors_t, delta) == 0x30u,
             "the deltas start where the block's do, 0x30 past the first vertex");
}

static void check_usable(void)
{
    mp_blade_vectors_t v;
    const float        inf = (float)INFINITY;

    ut_section("a reading with a blade in it");
    v = reading(&QUI_GON_BLADE, 1.0f);
    ut_check(mp_blade_rule_usable(&v), "Qui-Gon's full blade, 0.388 long, has one");
    v = reading(&QUI_GON_BLADE, 0.0f);
    ut_check(!mp_blade_rule_usable(&v), "a folded one, deltas of exactly 0, has none");
    memset(&v, 0, sizeof v);
    ut_check(!mp_blade_rule_usable(&v), "nor a block no spawn read, 0 everywhere");
    ut_check(!mp_blade_rule_usable(NULL), "nor nothing");
    v = reading(&QUI_GON_BLADE, 1.0f);
    v.delta[1][1] = (float)NAN;
    ut_check(!mp_blade_rule_usable(&v), "a NaN in a delta refuses it");
    v = reading(&QUI_GON_BLADE, 1.0f);
    v.delta[0][2] = inf;
    ut_check(!mp_blade_rule_usable(&v), "an infinite delta refuses it");
    v = reading(&QUI_GON_BLADE, 1.0f);
    v.vert[1][0] = inf;
    ut_check(!mp_blade_rule_usable(&v), "an infinite hilt vertex refuses it");
    v = reading(&QUI_GON_BLADE, 1.0f);
    v.vert[0][1] = -inf;
    ut_check(!mp_blade_rule_usable(&v), "and so does one at minus infinity");
    v = reading(&QUI_GON_BLADE, 1.0f);
    memcpy(v.vert[1], v.vert[0], sizeof v.vert[1]);
    ut_check(!mp_blade_rule_usable(&v), "a hilt of no width is a point, not a hilt");
    v = reading(&QUI_GON_BLADE, 1.0f / 16.0f);
    ut_check(mp_blade_rule_usable(&v),
             "one substep into growing, 6.25 per cent, still has one: usable cannot tell a short "
             "blade from a full one, which is why the longest reading wins");
    v = reading(&QUI_GON_BLADE, 0.02f);
    ut_check(!mp_blade_rule_usable(&v), "0.0078 long is under the 0.01 floor");
    v = reading(&QUI_GON_BLADE, 1.0f);
    v.delta[1][2] = 11.0f;
    ut_check(!mp_blade_rule_usable(&v), "a delta 11 long is past any blade the engine grows");
    v = reading(&QUI_GON_BLADE, 1.0f);
    v.delta[0][0] = 1.0e20f;
    ut_check(!mp_blade_rule_usable(&v), "and one whose square overflows refuses it too");
    v = reading(&QUI_GON_BLADE, 1.0f);
    v.vert[1][0] = 1.0e20f;
    ut_check(!mp_blade_rule_usable(&v), "as does a hilt whose width overflows");
}

static void check_the_names(void)
{
    ut_section("one asset, one name");
    ut_check(mp_blade_rule_same_asset("quigon.baf", "QuiGon.BAF"), "compared lower case");
    ut_check(!mp_blade_rule_same_asset("quigon.baf", "quigon.ba"), "a shorter name is another");
    ut_check(!mp_blade_rule_same_asset("", "") && !mp_blade_rule_same_asset(NULL, "quigon.baf"),
             "an empty or missing name is no asset");
    ut_check(mp_blade_rule_same_asset("abcdefghijklmnopqrstuvwxyz01234X",
                                      "abcdefghijklmnopqrstuvwxyz01234Y"),
             "and only 31 characters count, the header's field less its terminator");
    ut_check(mp_blade_rule_jedi(0u) && mp_blade_rule_jedi(1u) && !mp_blade_rule_jedi(2u) &&
                 !mp_blade_rule_jedi(3u) && !mp_blade_rule_jedi(MP_BLADE_NO_HERO),
             "heroes 0 and 1 are the spawn's sabre arm, and nothing else is");
}

static void check_the_book(void)
{
    mp_blade_book_t    book;
    mp_blade_vectors_t full = reading(&QUI_GON_BLADE, 1.0f);
    mp_blade_vectors_t half = reading(&QUI_GON_BLADE, 0.5f);
    mp_blade_vectors_t other = reading(&OBI_WAN_BLADE, 1.0f);
    mp_blade_vectors_t folded = reading(&QUI_GON_BLADE, 0.0f);
    mp_blade_vectors_t mixed;
    const mp_blade_vectors_t *found;
    char               name[] = "hero0.baf";
    unsigned           i;

    ut_section("the book: lower case to the first zero, raised and never lowered");
    memset(&book, 0, sizeof book);
    ut_check(mp_blade_book_learn(&book, "QUIGON.BAF", &half) == MP_BLADE_LEARNED_NEW,
             "a new name is kept");
    found = mp_blade_book_find(&book, QUI_GON);
    ut_check(found != NULL && same(found, &half) && strcmp(book.entry[0].name, QUI_GON) == 0,
             "under its lower case spelling");
    ut_check(mp_blade_book_learn(&book, QUI_GON, &full) == MP_BLADE_LEARNED_RAISED &&
                 same(mp_blade_book_find(&book, QUI_GON), &full) && book.raised == 1u,
             "a longer reading with the same hilt raises it");
    ut_check(mp_blade_book_learn(&book, QUI_GON, &half) == MP_BLADE_LEARNED_KEPT &&
                 same(mp_blade_book_find(&book, QUI_GON), &full),
             "a shorter one never lowers it");
    ut_check(mp_blade_book_learn(&book, QUI_GON, &full) == MP_BLADE_LEARNED_KEPT,
             "an equal one keeps it");
    mixed = full;
    mixed.delta[0][1] *= 1.1f;
    mixed.delta[1][1] *= 0.9f;
    ut_check(mp_blade_book_learn(&book, QUI_GON, &mixed) == MP_BLADE_LEARNED_KEPT &&
                 same(mp_blade_book_find(&book, QUI_GON), &full),
             "so does one with one delta longer and the other shorter: both have to be");
    ut_check(mp_blade_book_learn(&book, QUI_GON, &other) == MP_BLADE_LEARNED_FOREIGN &&
                 same(mp_blade_book_find(&book, QUI_GON), &full),
             "and one with another hilt, though longer, is not taken");
    ut_check(mp_blade_book_learn(&book, OBI_WAN, &folded) == MP_BLADE_LEARNED_REFUSED &&
                 mp_blade_book_find(&book, OBI_WAN) == NULL,
             "a reading with no blade is never learned");
    ut_check(mp_blade_book_learn(&book, "", &full) == MP_BLADE_LEARNED_REFUSED &&
                 mp_blade_book_learn(&book, NULL, &full) == MP_BLADE_LEARNED_REFUSED &&
                 book.known == 1u,
             "nor one under no name");
    for (i = 1u; i < MP_BLADE_BOOK_SIZE; ++i) {
        name[4] = (char)('0' + i);
        (void)mp_blade_book_learn(&book, name, &other);
    }
    ut_check(book.known == MP_BLADE_BOOK_SIZE && book.refused == 0u, "eight names fill it");
    ut_check(mp_blade_book_learn(&book, OBI_WAN, &other) == MP_BLADE_LEARNED_REFUSED &&
                 book.refused == 1u && mp_blade_book_find(&book, OBI_WAN) == NULL,
             "a ninth is refused and counted");
    ut_check(mp_blade_book_learn(&book, QUI_GON, &full) == MP_BLADE_LEARNED_KEPT,
             "while a name it knows is still answered");
}

static void check_the_choice(void)
{
    mp_blade_book_t    book;
    mp_blade_choice_t  c;
    mp_blade_vectors_t qui_full = reading(&QUI_GON_BLADE, 1.0f);
    mp_blade_vectors_t qui_folded = reading(&QUI_GON_BLADE, 0.0f);
    mp_blade_vectors_t obi_full = reading(&OBI_WAN_BLADE, 1.0f);
    mp_blade_vectors_t zeros;
    mp_blade_body_t    self;
    mp_blade_body_t    others[3];

    memset(&zeros, 0, sizeof zeros);

    ut_section("a body of hero 2 is no Jedi block");
    memset(&book, 0, sizeof book);
    self = body("panaka.baf", 2u, &zeros);
    others[0] = body(QUI_GON, 1u, &qui_full);
    mp_blade_rule_choose(&self, others, 1u, &book, &c);
    ut_check(c.source == MP_BLADE_NONE && !mp_blade_rule_writes(c.source) &&
                 same(&c.vectors, &zeros) && book.known == 0u,
             "nothing is chosen, nothing written, nothing learned");

    ut_section("the same asset beside it, its own reading folded, the book empty");
    self = body(QUI_GON, 1u, &qui_folded);
    others[0] = body(QUI_GON, 1u, &qui_full);
    mp_blade_rule_choose(&self, others, 1u, &book, &c);
    ut_check(c.source == MP_BLADE_DONOR && c.donor == 0u && same(&c.vectors, &qui_full),
             "the other body's vectors: a body of the same hero is a donor");
    ut_check(c.learned == MP_BLADE_LEARNED_NEW && same(mp_blade_book_find(&book, QUI_GON),
                                                        &qui_full),
             "and the book has them");

    ut_section("another Jedi beside it");
    memset(&book, 0, sizeof book);
    self = body(QUI_GON, 1u, &qui_full);
    others[0] = body(OBI_WAN, 0u, &obi_full);
    mp_blade_rule_choose(&self, others, 1u, &book, &c);
    ut_check(c.source == MP_BLADE_OWN && same(&c.vectors, &qui_full) && c.foreign == 0u,
             "its own reading, not Obi-Wan's longer blade: another asset is no candidate");
    ut_check(c.learned == MP_BLADE_LEARNED_NEW && mp_blade_book_find(&book, OBI_WAN) == NULL,
             "learned under its own name only");

    ut_section("a non-Jedi player beside it");
    memset(&book, 0, sizeof book);
    others[0] = body("panaka.baf", 2u, &zeros);
    mp_blade_rule_choose(&self, others, 1u, &book, &c);
    ut_check(c.source == MP_BLADE_OWN && same(&c.vectors, &qui_full),
             "its own reading, never Panaka's zeros");

    ut_section("two far bodies of one hero");
    memset(&book, 0, sizeof book);
    self = body(QUI_GON, 1u, &qui_folded);
    others[0] = body(OBI_WAN, 0u, &obi_full);
    others[1] = body(QUI_GON, 1u, &qui_full);
    mp_blade_rule_choose(&self, others, 2u, &book, &c);
    ut_check(c.source == MP_BLADE_DONOR && c.donor == 1u && same(&c.vectors, &qui_full),
             "the second far body's vectors, found past the local Obi-Wan");
}

static void check_the_book_heals(void)
{
    mp_blade_book_t    book;
    mp_blade_choice_t  c;
    mp_blade_vectors_t qui_full = reading(&QUI_GON_BLADE, 1.0f);
    mp_blade_vectors_t qui_half = reading(&QUI_GON_BLADE, 0.5f);
    mp_blade_vectors_t qui_step = reading(&QUI_GON_BLADE, 1.0f / 16.0f);
    mp_blade_vectors_t qui_folded = reading(&QUI_GON_BLADE, 0.0f);
    mp_blade_body_t    self;
    mp_blade_body_t    others[1];

    ut_section("a half grown reading is learned, and the book heals");
    memset(&book, 0, sizeof book);
    self = body(QUI_GON, 1u, &qui_half);
    others[0] = body(QUI_GON, 1u, &qui_folded);
    mp_blade_rule_choose(&self, others, 1u, &book, &c);
    ut_check(c.source == MP_BLADE_OWN && same(&c.vectors, &qui_half) &&
                 c.learned == MP_BLADE_LEARNED_NEW,
             "a folded co-owner and its own half blade: the half blade, the longest there is");
    self = body(QUI_GON, 1u, &qui_folded);
    others[0] = body(QUI_GON, 1u, &qui_half);
    mp_blade_rule_choose(&self, others, 1u, &book, &c);
    ut_check(c.source == MP_BLADE_BOOK && same(&c.vectors, &qui_half),
             "the body that kept it is no better a donor than the book it taught");
    self = body(QUI_GON, 1u, &qui_full);
    mp_blade_rule_choose(&self, others, 1u, &book, &c);
    ut_check(c.source == MP_BLADE_OWN && c.learned == MP_BLADE_LEARNED_RAISED &&
                 same(mp_blade_book_find(&book, QUI_GON), &qui_full),
             "the first full reading raises the book");
    self = body(QUI_GON, 1u, &qui_folded);
    mp_blade_rule_choose(&self, others, 1u, &book, &c);
    ut_check(c.source == MP_BLADE_BOOK && same(&c.vectors, &qui_full),
             "and from then on the half grown body beside it gets the full blade too");

    ut_section("a folded reading, the book knows the asset, nobody beside it");
    mp_blade_rule_choose(&self, NULL, 0u, &book, &c);
    ut_check(c.source == MP_BLADE_BOOK && same(&c.vectors, &qui_full) &&
                 mp_blade_rule_writes(c.source),
             "the book's blade, written: the frozen template's case");

    ut_section("a shorter reading of its own, the book knows the asset, nobody beside it");
    self = body(QUI_GON, 1u, &qui_step);
    mp_blade_rule_choose(&self, NULL, 0u, &book, &c);
    ut_check(c.source == MP_BLADE_BOOK && same(&c.vectors, &qui_full) &&
                 c.learned == MP_BLADE_LEARNED_KEPT,
             "the book's, although its own has a blade: the longer wins, not the own");
}

static void check_the_folded_and_the_donor(void)
{
    mp_blade_book_t    book;
    mp_blade_choice_t  c;
    mp_blade_vectors_t qui_full = reading(&QUI_GON_BLADE, 1.0f);
    mp_blade_vectors_t qui_half = reading(&QUI_GON_BLADE, 0.5f);
    mp_blade_vectors_t qui_step = reading(&QUI_GON_BLADE, 1.0f / 16.0f);
    mp_blade_vectors_t qui_folded = reading(&QUI_GON_BLADE, 0.0f);
    mp_blade_vectors_t obi_full = reading(&OBI_WAN_BLADE, 1.0f);
    mp_blade_body_t    self;
    mp_blade_body_t    others[1];

    ut_section("folded, nobody beside it, the book empty");
    memset(&book, 0, sizeof book);
    self = body(QUI_GON, 1u, &qui_folded);
    mp_blade_rule_choose(&self, NULL, 0u, &book, &c);
    ut_check(c.source == MP_BLADE_FOLDED && !mp_blade_rule_writes(c.source) &&
                 same(&c.vectors, &qui_folded) && book.known == 0u,
             "its own is kept, nothing written or learned");

    ut_section("folded, beside a folded body of its own asset, the book empty");
    others[0] = body(QUI_GON, 1u, &qui_folded);
    mp_blade_rule_choose(&self, others, 1u, &book, &c);
    ut_check(c.source == MP_BLADE_FOLDED && same(&c.vectors, &qui_folded),
             "a folded donor is no donor");

    ut_section("a block of the same asset that is no Jedi block");
    others[0] = body(QUI_GON, 3u, &qui_full);
    mp_blade_rule_choose(&self, others, 1u, &book, &c);
    ut_check(c.source == MP_BLADE_FOLDED && book.known == 0u,
             "a body riding the foreign slot out of quigon.baf is no donor, whatever it holds: "
             "its spawn never read a blade");

    ut_section("the book against a donor");
    memset(&book, 0, sizeof book);
    (void)mp_blade_book_learn(&book, QUI_GON, &qui_half);
    others[0] = body(QUI_GON, 1u, &qui_step);
    mp_blade_rule_choose(&self, others, 1u, &book, &c);
    ut_check(c.source == MP_BLADE_BOOK && same(&c.vectors, &qui_half),
             "a donor shorter than the book loses");
    others[0] = body(QUI_GON, 1u, &qui_half);
    mp_blade_rule_choose(&self, others, 1u, &book, &c);
    ut_check(c.source == MP_BLADE_BOOK, "an equal one loses too: the book comes first");
    others[0] = body(QUI_GON, 1u, &qui_full);
    mp_blade_rule_choose(&self, others, 1u, &book, &c);
    ut_check(c.source == MP_BLADE_DONOR && same(&c.vectors, &qui_full) &&
                 c.learned == MP_BLADE_LEARNED_RAISED,
             "a longer one wins and raises the book");

    ut_section("a reading with another hilt under the asset's name");
    self = body(QUI_GON, 1u, &obi_full);
    mp_blade_rule_choose(&self, NULL, 0u, &book, &c);
    ut_check(c.source == MP_BLADE_BOOK && c.foreign == 1u && same(&c.vectors, &qui_full) &&
                 same(mp_blade_book_find(&book, QUI_GON), &qui_full),
             "is counted and passed over though it is longer, and the book keeps its own");
    memset(&book, 0, sizeof book);
    others[0] = body(QUI_GON, 1u, &qui_half);
    mp_blade_rule_choose(&self, others, 1u, &book, &c);
    ut_check(c.source == MP_BLADE_OWN && c.foreign == 1u && same(&c.vectors, &obi_full),
             "with the book empty the longest reading sets the hilt, and the other is foreign");

    ut_section("only the book and a donor write");
    ut_check(mp_blade_rule_writes(MP_BLADE_BOOK) && mp_blade_rule_writes(MP_BLADE_DONOR) &&
                 !mp_blade_rule_writes(MP_BLADE_OWN) && !mp_blade_rule_writes(MP_BLADE_FOLDED) &&
                 !mp_blade_rule_writes(MP_BLADE_NONE),
             "own and folded keep what the spawn read, none has nothing");

    ut_section("the edges of the call");
    mp_blade_rule_choose(NULL, others, 1u, &book, &c);
    ut_check(c.source == MP_BLADE_NONE, "no body is no Jedi");
    mp_blade_rule_choose(&self, NULL, 5u, &book, NULL);
    self = body("", 0u, &qui_full);
    mp_blade_rule_choose(&self, others, 1u, &book, &c);
    ut_check(c.source == MP_BLADE_OWN && c.learned == MP_BLADE_LEARNED_REFUSED,
             "a body whose name did not read keeps its own and teaches the book nothing");
}

static mp_blade_tick_facts_t facts(bool worn, uint32_t hero, uint32_t node)
{
    mp_blade_tick_facts_t f;

    f.worn = worn;
    f.hero = hero;
    f.node = node;
    return f;
}

static void check_the_tick(void)
{
    const uint32_t        node = 36u;
    mp_blade_tick_facts_t f;

    ut_section("what a puppet's blade does, every pairing");
    f = facts(false, 1u, node);
    ut_check(mp_blade_rule_tick(&f) == MP_BLADE_TICK_STEP,
             "a far Qui-Gon steps his own length in his block, whatever the local player holds, "
             "the same hero with the local sabre out included");
    f = facts(false, 0u, node);
    ut_check(mp_blade_rule_tick(&f) == MP_BLADE_TICK_STEP, "and so does a far Obi-Wan");
    f = facts(false, 2u, 0u);
    ut_check(mp_blade_rule_tick(&f) == MP_BLADE_TICK_BLADELESS,
             "a far Panaka has no blade: withheld");
    f = facts(false, 3u, node);
    ut_check(mp_blade_rule_tick(&f) == MP_BLADE_TICK_BLADELESS,
             "a far character on the foreign slot with a blade in its rig, but no reading of it: "
             "withheld, it has no vectors to step");
    f = facts(false, 0u, 0u);
    ut_check(mp_blade_rule_tick(&f) == MP_BLADE_TICK_BLADELESS,
             "a Jedi block whose rig has no blade node: withheld, its light has no sphere");
    f = facts(true, 1u, node);
    ut_check(mp_blade_rule_tick(&f) == MP_BLADE_TICK_WORN_STEP,
             "a worn Jedi body steps its length in the block as well, and is told apart, because "
             "the report counts a worn body's steps in a line of their own");
    f = facts(true, 1u, 0u);
    ut_check(mp_blade_rule_tick(&f) == MP_BLADE_TICK_WORN_STEP,
             "and its blade node is not asked for: the overlay puts that word back to 0 when it "
             "dresses a body, so the hero index is what says the block carries a blade");
    f = facts(true, 2u, node);
    ut_check(mp_blade_rule_tick(&f) == MP_BLADE_TICK_WORN,
             "a worn Panaka has no blade to step: withheld whole, and not as bladeless");
    f = facts(true, 3u, node);
    ut_check(mp_blade_rule_tick(&f) == MP_BLADE_TICK_WORN,
             "and so is a worn character on the foreign slot, whose block read no vectors");
    ut_check(mp_blade_rule_tick(NULL) == MP_BLADE_TICK_BLADELESS, "no facts, no length");

    ut_section("the light follows the same answer");
    ut_check(mp_blade_rule_light_whole(MP_BLADE_TICK_STEP),
             "whole for a Jedi body in its hero's own model, whose blade node has its sphere");
    ut_check(!mp_blade_rule_light_whole(MP_BLADE_TICK_WORN) &&
                 !mp_blade_rule_light_whole(MP_BLADE_TICK_BLADELESS) &&
                 !mp_blade_rule_light_whole(MP_BLADE_TICK_WORN_STEP),
             "only as far as its release for every other, a worn body whose length is stepped "
             "included: its blade node names a joint of the hero's rig, which it does not draw");
}

/* The vertices a hero's blade is drawn from at `size`, worked out the long way. */
static void expected_verts(const hero_blade_t *hero, float size, float out[12])
{
    int edge;
    int axis;

    for (edge = 0; edge < 2; ++edge) {
        for (axis = 0; axis < 3; ++axis) {
            out[3 * edge + axis]     = hero->hilt[edge][axis];
            out[6 + 3 * edge + axis] = hero->hilt[edge][axis];
        }
        out[6 + 3 * edge + 1] += hero->length[edge] * size;
    }
}

static bool near_all(const float *a, const float *b, int count)
{
    int i;

    for (i = 0; i < count; ++i) {
        if (fabs((double)a[i] - (double)b[i]) > 1e-6) {
            return false;
        }
    }
    return true;
}

static void check_the_vertices(void)
{
    static const float sizes[4] = { 0.0f, 1.0f / 16.0f, 0.5f, 1.0f };
    const hero_blade_t *heroes[2] = { &OBI_WAN_BLADE, &QUI_GON_BLADE };
    mp_blade_vectors_t  full;
    mp_blade_vectors_t  bad;
    float               got[MP_BLADE_VERT_FLOATS];
    float               want[MP_BLADE_VERT_FLOATS];
    float               untouched[MP_BLADE_VERT_FLOATS];
    int                 h;
    int                 s;

    ut_section("the four vertices a far blade is drawn from, the setter's formula");
    ut_check(MP_BLADE_VERT_FLOATS == 12u, "four vertices of three floats, the setter's 0x30 bytes");
    for (h = 0; h < 2; ++h) {
        full = reading(heroes[h], 1.0f);
        for (s = 0; s < 4; ++s) {
            expected_verts(heroes[h], sizes[s], want);
            ut_checkf(mp_blade_rule_verts(&full, sizes[s], got) && near_all(got, want, 12),
                      "%s at %d/1000 of its length: the hilt as it stands, each tip its own hilt "
                      "vertex plus its own delta times the length", h == 0 ? OBI_WAN : QUI_GON,
                      (int)(sizes[s] * 1000.0f));
        }
    }
    full = reading(&OBI_WAN_BLADE, 1.0f);
    ut_check(mp_blade_rule_verts(&full, 0.5f, got) &&
                 memcmp(got, full.vert[0], sizeof full.vert[0]) == 0 &&
                 memcmp(got + 3, full.vert[1], sizeof full.vert[1]) == 0,
             "the two hilt vertices are copied bit for bit, never recomputed");
    ut_check(fabs((double)got[9] - (double)full.vert[1][0]) < 1e-6 &&
                 fabs((double)got[10] - (double)(full.vert[1][1] + 0.5f * 0.4274f)) < 1e-6,
             "the second tip grows from the second hilt vertex by the second delta, not from "
             "the first");
    ut_check(mp_blade_rule_verts(&full, 0.0f, got) &&
                 memcmp(got + 6, got, 6u * sizeof(float)) == 0,
             "at length 0 each tip lies on its hilt vertex: a folded blade, "
             "as the setter folds it");

    memset(untouched, 0x5A, sizeof untouched);
    memcpy(got, untouched, sizeof got);
    ut_check(!mp_blade_rule_verts(&full, NAN, got) && !mp_blade_rule_verts(&full, INFINITY, got),
             "a length that is not a number is refused");
    bad = full;
    bad.delta[1][2] = NAN;
    ut_check(!mp_blade_rule_verts(&bad, 1.0f, got), "a delta that is not a number is refused");
    bad = full;
    bad.vert[0][0] = -INFINITY;
    ut_check(!mp_blade_rule_verts(&bad, 1.0f, got), "and so is a hilt vertex at infinity");
    bad = full;
    bad.delta[0][1] = 3.0e38f;
    ut_check(!mp_blade_rule_verts(&bad, 3.0e38f, got),
             "and a tip that overflows on the way out");
    ut_check(memcmp(got, untouched, sizeof got) == 0, "and nothing is written for any of them");
    ut_check(!mp_blade_rule_verts(NULL, 1.0f, got) && !mp_blade_rule_verts(&full, 1.0f, NULL),
             "no vectors or no place to write is refused");
    bad = full;
    memset(bad.vert[2], 0xFF, sizeof bad.vert[2]);
    memset(bad.vert[3], 0xFF, sizeof bad.vert[3]);
    ut_check(mp_blade_rule_verts(&bad, 1.0f, got),
             "the block's own tip vertices are not read, not a number as they are here: the "
             "setter overwrites them");

    ut_section("which far blade is drawn from vertices of its own");
    ut_check(mp_blade_rule_draws_own(false, true, 4u),
             "a Jedi body in its hero's own model with a blade mesh of four");
    ut_check(!mp_blade_rule_draws_own(true, true, 4u),
             "never a worn body: the overlay draws what it put on");
    ut_check(!mp_blade_rule_draws_own(false, false, 4u), "never a body that is no Jedi");
    ut_check(!mp_blade_rule_draws_own(false, true, 0u) &&
                 !mp_blade_rule_draws_own(false, true, 3u) &&
                 !mp_blade_rule_draws_own(false, true, 8u),
             "never a mesh of another count: the setter's buffer is four vertices");
}

int main(void)
{
    check_the_layout();
    check_usable();
    check_the_names();
    check_the_book();
    check_the_choice();
    check_the_book_heals();
    check_the_folded_and_the_donor();
    check_the_tick();
    check_the_vertices();

    return ut_summary("mp_blade_rule");
}
