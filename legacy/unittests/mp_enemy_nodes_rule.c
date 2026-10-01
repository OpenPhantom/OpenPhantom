/* mp_enemy_nodes_rule.c: hidden nodes and meshes in the engine's saved form, and the entries a
 * receiver writes to follow them.
 *
 * The two mistakes the rule exists to avoid are both silent. A receiver that writes every entry
 * fights whatever wrote one locally and moves nodes it had no word about; one that writes past its
 * own node count writes past the array. And the form has to be the engine's own, bit idx & 31 of
 * word idx / 32, or a mask saved by one side is read shifted by the other.
 *
 * The sabre used to travel as two bits of its own, and the rule that carried them is kept below
 * word for word in what it decided, as the reference: for the sabre the masks must give what it
 * gave, and for the rest of the model what the host has.
 */
#include "unittest.h"

#include "mp_enemy_nodes_rule.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* ==============================================================================================
 * The form.
 * ============================================================================================ */

static void check_the_engine_form(void)
{
    int32_t               entries[70];
    mp_enemy_nodes_mask_t mask;

    ut_section("the save's form: bit idx & 31 of word idx / 32 for every entry that is not zero");

    memset(entries, 0, sizeof entries);
    entries[0]  = 1;
    entries[3]  = 2;    /* any nonzero entry is hidden, as the engine's save reads it */
    entries[31] = 1;
    entries[32] = 1;
    entries[47] = -1;
    mask = mp_enemy_nodes_rule_fold(entries, 48u);
    ut_checkf(mask.word[0] == 0x80000009u && mask.word[1] == 0x00008001u,
              "entries 0, 3, 31 in the low word and 32, 47 in the high one (%08X %08X)",
              (unsigned)mask.word[0], (unsigned)mask.word[1]);
    ut_check(mp_enemy_nodes_rule_bit(&mask, 32u) && !mp_enemy_nodes_rule_bit(&mask, 33u),
             "the bit of entry 32 is bit 0 of the high word");
    ut_check(mp_enemy_nodes_rule_count(&mask) == 5u && mp_enemy_nodes_rule_first(&mask) == 0u,
             "five set, the first of them entry 0");

    entries[50] = 1;
    mask = mp_enemy_nodes_rule_fold(entries, 48u);
    ut_check(!mp_enemy_nodes_rule_bit(&mask, 50u), "an entry past the count is not read");

    entries[65] = 1;
    mask = mp_enemy_nodes_rule_fold(entries, 70u);
    ut_check(mp_enemy_nodes_rule_bit(&mask, 50u) && !mp_enemy_nodes_rule_bit(&mask, 65u),
             "and an entry past 64 is dropped, as the engine's two words drop it");
    ut_check(!mp_enemy_nodes_rule_bit(&mask, 64u) && !mp_enemy_nodes_rule_bit(NULL, 0u),
             "no bit is read past the form or out of nothing");

    mask = mp_enemy_nodes_rule_fold(NULL, 10u);
    ut_check(mask.word[0] == 0u && mask.word[1] == 0u &&
                 mp_enemy_nodes_rule_first(&mask) == MP_ENEMY_NODES_MAX,
             "no array reads as nothing hidden, and has no first entry");
}

/* ==============================================================================================
 * The step.
 * ============================================================================================ */

static void check_only_what_differs(void)
{
    mp_enemy_nodes_mask_t want;
    mp_enemy_nodes_mask_t here;
    mp_enemy_nodes_step_t step;

    ut_section("a receiver writes only the entries that differ, and none at or past its count");

    memset(&want, 0, sizeof want);
    memset(&here, 0, sizeof here);
    want.word[0] = 0x0000000Fu;   /* 0..3 hidden on the host */
    here.word[0] = 0x00000013u;   /* 0, 1 and 4 hidden here */
    step = mp_enemy_nodes_rule_step(&want, &here, 48u);
    ut_checkf(step.hide.word[0] == 0x0000000Cu && step.show.word[0] == 0x00000010u &&
                  step.hide.word[1] == 0u && step.show.word[1] == 0u,
              "2 and 3 hidden, 4 shown, 0 and 1 left alone (hide %08X, show %08X)",
              (unsigned)step.hide.word[0], (unsigned)step.show.word[0]);

    step = mp_enemy_nodes_rule_step(&want, &want, 48u);
    ut_check(mp_enemy_nodes_rule_count(&step.hide) + mp_enemy_nodes_rule_count(&step.show) == 0u,
             "a receiver that already has what the host has writes nothing");

    memset(&here, 0, sizeof here);
    want.word[0] = 0u;
    want.word[1] = 0x00000003u;   /* 32 and 33 */
    step = mp_enemy_nodes_rule_step(&want, &here, 33u);
    ut_check(mp_enemy_nodes_rule_bit(&step.hide, 32u) && !mp_enemy_nodes_rule_bit(&step.hide, 33u),
             "a model of 33 nodes takes 32 and is never told about 33");
    ut_check(mp_enemy_nodes_rule_past(&want, 33u) && !mp_enemy_nodes_rule_past(&want, 34u),
             "and 33 is past a count of 33, which is the refusal's question");
    ut_check(!mp_enemy_nodes_rule_past(&want, 64u) && !mp_enemy_nodes_rule_past(&want, 200u),
             "nothing is past a count at or over the form");

    step = mp_enemy_nodes_rule_step(NULL, &here, 48u);
    ut_check(mp_enemy_nodes_rule_count(&step.hide) == 0u, "and nothing is asked of nothing");
}

/* ==============================================================================================
 * The reference: the sabre's own two bits, the rule the masks replaced.
 * ============================================================================================ */

/* The rig the shipped Qui-Gon wears, as far as the old rule knew it: a weapon node whose direct
 * children are a gun marker and the sabre, the blade under the sabre, and a waist node
 * elsewhere. */
#define RIG_NODES  7u
#define NODE_GUN   2u
#define NODE_SABRE 4u
#define NODE_BLADE 5u

#define OLD_HAS_HAND    0x4000u
#define OLD_SABRE_SHOWN 0x8000u

typedef enum old_step { OLD_SILENT, OLD_SAME, OLD_SHOW, OLD_HIDE } old_step_t;

/* What the old sender put on its record for a model with the pair. */
static uint32_t old_bits(const int32_t *hidden)
{
    return OLD_HAS_HAND | (hidden[NODE_SABRE] != 0 ? 0u : OLD_SABRE_SHOWN);
}

/* What the old receiver decided for a model with the pair. */
static old_step_t old_decide(uint32_t word, const int32_t *hidden)
{
    bool shown;

    if ((word & OLD_HAS_HAND) == 0u) {
        return OLD_SILENT;
    }
    shown = (word & OLD_SABRE_SHOWN) != 0u;
    if (shown && hidden[NODE_SABRE] != 0) {
        return OLD_SHOW;
    }
    if (!shown && hidden[NODE_SABRE] == 0) {
        return OLD_HIDE;
    }
    return OLD_SAME;
}

/* What the old receiver wrote: the engine's draw clears the sabre's own flag, its put away sets
 * the flag of every direct child of the weapon node. */
static void old_apply(int32_t *hidden, old_step_t step)
{
    if (step == OLD_SHOW) {
        hidden[NODE_SABRE] = 0;
    } else if (step == OLD_HIDE) {
        hidden[NODE_GUN]   = 1;
        hidden[NODE_SABRE] = 1;
    }
}

static void new_apply(int32_t *hidden, const int32_t *host)
{
    mp_enemy_nodes_mask_t want = mp_enemy_nodes_rule_fold(host, RIG_NODES);
    mp_enemy_nodes_mask_t here = mp_enemy_nodes_rule_fold(hidden, RIG_NODES);
    mp_enemy_nodes_step_t step = mp_enemy_nodes_rule_step(&want, &here, RIG_NODES);
    uint32_t              index;

    for (index = 0; index < RIG_NODES; ++index) {
        if (mp_enemy_nodes_rule_bit(&step.hide, index)) {
            hidden[index] = 1;
        } else if (mp_enemy_nodes_rule_bit(&step.show, index)) {
            hidden[index] = 0;
        }
    }
}

/* One side's array after a history: never touched (all shown), put away, or put away and drawn
 * again, which leaves the gun marker hidden because the draw clears the sabre alone. */
static void history(int32_t *hidden, int which)
{
    memset(hidden, 0, RIG_NODES * sizeof hidden[0]);
    if (which >= 1) {
        hidden[NODE_GUN]   = 1;
        hidden[NODE_SABRE] = 1;
    }
    if (which == 2) {
        hidden[NODE_SABRE] = 0;
    }
}

static void check_the_sabre_against_the_old_rule(void)
{
    int host;
    int receiver;

    ut_section("the sabre: the masks give the old two bits' flag, and the rest as the host");

    for (host = 0; host < 3; ++host) {
        for (receiver = 0; receiver < 3; ++receiver) {
            int32_t on_host[RIG_NODES];
            int32_t by_old[RIG_NODES];
            int32_t by_new[RIG_NODES];

            history(on_host, host);
            history(by_old, receiver);
            history(by_new, receiver);
            old_apply(by_old, old_decide(old_bits(on_host), by_old));
            new_apply(by_new, on_host);
            ut_checkf((by_new[NODE_SABRE] != 0) == (by_old[NODE_SABRE] != 0),
                      "host history %d, receiver history %d: the sabre agrees with the old rule",
                      host, receiver);
            ut_checkf(memcmp(by_new, on_host, sizeof on_host) == 0,
                      "host history %d, receiver history %d: the whole array is the host's",
                      host, receiver);
            ut_checkf(by_new[NODE_BLADE] == 0,
                      "host history %d, receiver history %d: the blade under the sabre is never "
                      "hidden on its own, it goes with its parent", host, receiver);
        }
    }
}

int main(void)
{
    check_the_engine_form();
    check_only_what_differs();
    check_the_sabre_against_the_old_rule();
    return ut_summary("mp_enemy_nodes_rule");
}
