/* mp_enemy_interest.c: the interest rule of the enemy block, with no block and no engine.
 *
 * What would be silent if it were wrong:
 *
 *   a class that disagrees with the range gate at the radius, so that the host wakes an enemy for
 *   a peer and calls it far in the same substep;
 *
 *   an event, a death or a new life that waits behind a crowd and misses its window, which loses it
 *   for good rather than late;
 *
 *   a new life that counts as one that may not wait at a level's first substep, when every key is
 *   new and nobody could have described the one before;
 *
 *   a key that waits for ever because nearer ones fill every block;
 *
 *   a far key a peer was never told about described anyway, which fills that peer's pool with the
 *   enemies of every other player's corner;
 *
 *   and a wake radius scaled in a way the activation site does not scale it.
 *
 * The block itself, with the bitmap, the mirror and the acknowledgements, is the test
 * mp_enemy_sync_interest.
 */
#include "unittest.h"

#include "mp_enemy_interest_rule.h"
#include "mp_enemy_wire.h"
#include "mp_range_gate_rule.h"

#include "mods/view_distance_fix/npc_range.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* A peer at the origin and an enemy on the x axis. */
static mp_enemy_viewer_t viewer_at_origin(void)
{
    mp_enemy_viewer_t viewer;

    memset(&viewer, 0, sizeof viewer);
    viewer.placed = true;
    viewer.body   = 0x1000u;
    viewer.slot   = 1u;
    return viewer;
}

static mp_enemy_subject_t subject_at(float x, float wake, float keep)
{
    mp_enemy_subject_t subject;

    memset(&subject, 0, sizeof subject);
    subject.read          = true;
    subject.has_placement = true;
    subject.placement[0]  = x;
    subject.position[0]   = x;
    subject.wake          = wake;
    subject.keep          = keep;
    return subject;
}

static mp_enemy_interest_ask_t ask_of(mp_enemy_reach_t reach, uint8_t generation)
{
    mp_enemy_interest_ask_t ask;

    memset(&ask, 0, sizeof ask);
    ask.reach      = reach;
    ask.generation = generation;
    return ask;
}

static void check_the_classes(void)
{
    const mp_enemy_viewer_t viewer = viewer_at_origin();
    const float             origin[3] = { 0.0f, 0.0f, 0.0f };
    mp_enemy_subject_t      subject;
    mp_enemy_viewer_t       nobody;
    float                   x;

    ut_section("the class is the engine's own test, asked of the peer's player");
    for (x = 9.5f; x <= 10.5f; x += 0.25f) {
        const float at[3] = { x, 0.0f, 0.0f };

        subject = subject_at(x, 10.0f, 30.0f);
        ut_checkf((mp_enemy_interest_reach(&viewer, &subject, MP_ENEMY_REACH_NONE) ==
                   MP_ENEMY_REACH_NEAR) == mp_range_gate_within(at, origin, 10.0f),
                  "at %.2f the wake test answers as the range gate does", (double)x);
    }
    subject = subject_at(10.0f, 10.0f, 30.0f);
    ut_check(mp_enemy_interest_reach(&viewer, &subject, MP_ENEMY_REACH_NONE) ==
                 MP_ENEMY_REACH_MIDDLE,
             "exactly on the wake radius is not near: the test is strictly less than");
    subject = subject_at(30.0f, 10.0f, 30.0f);
    ut_check(mp_enemy_interest_reach(&viewer, &subject, MP_ENEMY_REACH_NONE) ==
                 MP_ENEMY_REACH_FAR,
             "exactly on the keep radius is far");

    subject = subject_at(200.0f, 0.0f, 30.0f);
    ut_check(mp_enemy_interest_reach(&viewer, &subject, MP_ENEMY_REACH_NONE) ==
                 MP_ENEMY_REACH_NEAR,
             "a wake radius of zero is no distance test: near wherever it stands");
    subject = subject_at(200.0f, 10.0f, 0.0f);
    ut_check(mp_enemy_interest_reach(&viewer, &subject, MP_ENEMY_REACH_NONE) ==
                 MP_ENEMY_REACH_MIDDLE,
             "a keep radius of zero is never removed: never far");

    subject = subject_at(5.0f, 10.0f, 30.0f);
    subject.placement[0] = 50.0f;
    ut_check(mp_enemy_interest_reach(&viewer, &subject, MP_ENEMY_REACH_NONE) ==
                 MP_ENEMY_REACH_MIDDLE,
             "the wake test measures the placement's own position, not where the actor walked");
    subject = subject_at(50.0f, 10.0f, 30.0f);
    subject.placement[0] = 5.0f;
    ut_check(mp_enemy_interest_reach(&viewer, &subject, MP_ENEMY_REACH_NONE) ==
                 MP_ENEMY_REACH_NEAR,
             "and an actor that walked off from a placement near the peer is still near");
    subject.wake = 1.0f;
    ut_check(mp_enemy_interest_reach(&viewer, &subject, MP_ENEMY_REACH_NONE) ==
                 MP_ENEMY_REACH_FAR,
             "while the keep test measures the actor itself");

    nobody = viewer;
    nobody.placed = false;
    subject = subject_at(200.0f, 10.0f, 30.0f);
    ut_check(mp_enemy_interest_reach(&nobody, &subject, MP_ENEMY_REACH_NONE) ==
                 MP_ENEMY_REACH_NONE,
             "with no position for the peer nothing is measured");
    subject.read = false;
    ut_check(mp_enemy_interest_reach(&viewer, &subject, MP_ENEMY_REACH_NONE) ==
                 MP_ENEMY_REACH_NONE,
             "and with nothing read of the enemy neither");
    subject = subject_at(200.0f, 10.0f, 30.0f);
    subject.has_placement = false;
    ut_check(mp_enemy_interest_reach(&viewer, &subject, MP_ENEMY_REACH_NONE) ==
                 MP_ENEMY_REACH_MIDDLE,
             "a copy with no placement is middle wherever it stands");

    ut_section("hysteresis keeps a key on the line in its class");
    subject = subject_at(10.5f, 10.0f, 30.0f);
    ut_check(mp_enemy_interest_reach(&viewer, &subject, MP_ENEMY_REACH_NEAR) ==
                 MP_ENEMY_REACH_NEAR,
             "a key that was near stays near a twentieth past the wake radius");
    ut_check(mp_enemy_interest_reach(&viewer, &subject, MP_ENEMY_REACH_MIDDLE) ==
                 MP_ENEMY_REACH_MIDDLE,
             "one that was not does not become near there");
    subject = subject_at(11.5f, 10.0f, 30.0f);
    ut_check(mp_enemy_interest_reach(&viewer, &subject, MP_ENEMY_REACH_NEAR) ==
                 MP_ENEMY_REACH_MIDDLE,
             "and a tenth and a half past it the near class is left");
    subject = subject_at(32.0f, 10.0f, 30.0f);
    ut_check(mp_enemy_interest_reach(&viewer, &subject, MP_ENEMY_REACH_MIDDLE) ==
                 MP_ENEMY_REACH_MIDDLE,
             "a key that was kept stays kept a fifteenth past the keep radius");
    ut_check(mp_enemy_interest_reach(&viewer, &subject, MP_ENEMY_REACH_FAR) ==
                 MP_ENEMY_REACH_FAR,
             "and one that was far stays far there");
}

static void check_the_wake_scale(void)
{
    static const float SCALES[] = { 1.0f, 1.25f, 1.5f, 2.0f };
    static const float WAKES[]  = { -1.0f, 0.0f, 5.0f, 10.0f, 20.0f, 25.0f, 33.3f };
    static const float KEEPS[]  = { -1.0f, 0.0f, 10.0f, 11.2f, 22.0f, 40.0f, 100.0f };
    size_t             s;
    size_t             w;
    size_t             k;
    size_t             apart = 0;

    ut_section("the wake radius is scaled as view_distance_fix scales it at the activation site");
    for (s = 0; s < sizeof SCALES / sizeof SCALES[0]; ++s) {
        for (w = 0; w < sizeof WAKES / sizeof WAKES[0]; ++w) {
            for (k = 0; k < sizeof KEEPS / sizeof KEEPS[0]; ++k) {
                bool  capped = false;
                float theirs = npc_range_scaled(WAKES[w], KEEPS[k], SCALES[s], &capped);
                float ours   = mp_enemy_interest_wake(WAKES[w], KEEPS[k], SCALES[s]);

                apart += (theirs != ours) ? 1u : 0u;
            }
        }
    }
    ut_checkf(apart == 0u,
              "over %u scales, %u wake radii and %u keep radii the two answer alike (%u apart)",
              (unsigned)(sizeof SCALES / sizeof SCALES[0]),
              (unsigned)(sizeof WAKES / sizeof WAKES[0]),
              (unsigned)(sizeof KEEPS / sizeof KEEPS[0]), (unsigned)apart);
    ut_check(mp_enemy_interest_wake(10.0f, 30.0f, 1.0f) == 10.0f,
             "a scale of one is the authored radius");
}

static void check_what_may_not_wait(void)
{
    mp_enemy_interest_row_t row;
    mp_enemy_interest_ask_t ask;
    mp_enemy_record_t       record;

    ut_section("what may not wait");
    memset(&row, 0, sizeof row);
    ask = ask_of(MP_ENEMY_REACH_NEAR, 1u);
    ut_check(mp_enemy_interest_must(&row, &ask) == 0u,
             "a life nobody described before is no new life: at a level's first substep every "
             "key is new, and none of them may not wait for that");
    mp_enemy_interest_written(&row, 1u, 0u);
    ask = ask_of(MP_ENEMY_REACH_FAR, 2u);
    ut_check(mp_enemy_interest_must(&row, &ask) == MP_ENEMY_MUST_LIFE,
             "a new life of a key this peer had described may not wait, far or not");
    ask = ask_of(MP_ENEMY_REACH_MIDDLE, 1u);
    ut_check(mp_enemy_interest_must(&row, &ask) == 0u, "the same life, unchanged, may wait");

    memset(&record, 0, sizeof record);
    record.value[MP_ENEMY_F_STATE] = MP_ENEMY_STATE_DEATH;
    ask.watched = mp_enemy_interest_watched(&record);
    ut_check(mp_enemy_interest_must(&row, &ask) == MP_ENEMY_MUST_STATE, "a death may not wait");
    record.value[MP_ENEMY_F_STATE] = 6u;
    ask.watched = mp_enemy_interest_watched(&record);
    ut_check(mp_enemy_interest_must(&row, &ask) == MP_ENEMY_MUST_STATE, "nor a throw");
    record.value[MP_ENEMY_F_STATE] = 1u;
    record.value[MP_ENEMY_F_SHIELD] = 0x23u;
    ask.watched = mp_enemy_interest_watched(&record);
    ut_check(mp_enemy_interest_must(&row, &ask) == MP_ENEMY_MUST_STATE, "nor a shield");
    record.value[MP_ENEMY_F_SHIELD] = 0x0100u;
    ask.watched = mp_enemy_interest_watched(&record);
    ut_check(mp_enemy_interest_must(&row, &ask) == MP_ENEMY_MUST_STATE,
             "nor a shield whose change is in its second byte, which the field's sixteen bits "
             "carry and the one byte of before did not");
    record.value[MP_ENEMY_F_SHIELD] = 0u;
    record.value[MP_ENEMY_F_BODY]   = MP_ENEMY_BODY_IS_DRAWN;
    ask.watched = mp_enemy_interest_watched(&record);
    ut_check(mp_enemy_interest_must(&row, &ask) == MP_ENEMY_MUST_STATE,
             "nor a body that is drawn where it was not");
    record.value[MP_ENEMY_F_BODY] = MP_ENEMY_BODY_IS_SOLID;
    ask.watched = mp_enemy_interest_watched(&record);
    ut_check(mp_enemy_interest_must(&row, &ask) == MP_ENEMY_MUST_STATE,
             "nor one that collides where it did not");
    record.value[MP_ENEMY_F_BODY]     = 0x00FFFF00u;
    ask.watched = mp_enemy_interest_watched(&record);
    ut_check(mp_enemy_interest_must(&row, &ask) == 0u,
             "but its alpha and its dissolve may wait: they change in every substep of a fade");
    record.value[MP_ENEMY_F_BODY]     = 0u;
    record.value[MP_ENEMY_F_NODES_HI] = 1u << 3;
    ask.watched = mp_enemy_interest_watched(&record);
    ut_check(mp_enemy_interest_must(&row, &ask) == MP_ENEMY_MUST_STATE,
             "a node hidden or shown may not wait, node 35 included");
    record.value[MP_ENEMY_F_NODES_HI]  = 0u;
    record.value[MP_ENEMY_F_MESHES_LO] = 1u << 3;
    ask.watched = mp_enemy_interest_watched(&record);
    ut_check(mp_enemy_interest_must(&row, &ask) == MP_ENEMY_MUST_STATE, "nor a mesh");
    record.value[MP_ENEMY_F_MESHES_LO] = 0u;
    mp_enemy_interest_written(&row, 1u, mp_enemy_interest_watched(&record));
    record.value[MP_ENEMY_F_NODES_LO]  = 1u << 3;
    record.value[MP_ENEMY_F_MESHES_LO] = 1u << 3;
    ask.watched = mp_enemy_interest_watched(&record);
    ut_check(mp_enemy_interest_must(&row, &ask) == MP_ENEMY_MUST_STATE,
             "and the same node hidden in the node mask and the mesh mask does not cancel out");
    record.value[MP_ENEMY_F_MESHES_LO] = 0u;
    record.value[MP_ENEMY_F_NODES_HI]  = 1u << 3;
    ask.watched = mp_enemy_interest_watched(&record);
    ut_check(mp_enemy_interest_must(&row, &ask) == MP_ENEMY_MUST_STATE,
             "nor node 3 against node 35");
    record.value[MP_ENEMY_F_NODES_LO] = 0u;
    record.value[MP_ENEMY_F_NODES_HI] = 0u;
    record.value[MP_ENEMY_F_STATE]     = 1u | MP_ENEMY_HAS_NODES | MP_ENEMY_HAS_MESHES;
    ask.watched = mp_enemy_interest_watched(&record);
    ut_check(mp_enemy_interest_must(&row, &ask) == 0u,
             "the masks' presence bits are not watched: a drawn sabre is a node shown, and the "
             "node masks carry it");
    record.value[MP_ENEMY_F_STATE] = 1u | MP_ENEMY_FLAG_FORCE_THROWN;
    ask.watched = mp_enemy_interest_watched(&record);
    ut_check(mp_enemy_interest_must(&row, &ask) == MP_ENEMY_MUST_STATE, "nor a force throw");
    record.value[MP_ENEMY_F_STATE] = 4u;
    ask.watched = mp_enemy_interest_watched(&record);
    ut_check(mp_enemy_interest_must(&row, &ask) == 0u,
             "a hit reaction is an ordinary state to a watcher and may wait");
    mp_enemy_interest_written(&row, 1u, mp_enemy_interest_watched(&record));
    record.value[MP_ENEMY_F_STATE] = MP_ENEMY_STATE_DEATH;
    mp_enemy_interest_written(&row, 1u, mp_enemy_interest_watched(&record));
    ask.watched = mp_enemy_interest_watched(&record);
    ut_check(mp_enemy_interest_must(&row, &ask) == 0u,
             "once written, the same death is no change any more");

    ut_section("a world event on a key this peer does not hold brings the record along");
    memset(&row, 0, sizeof row);
    ask = ask_of(MP_ENEMY_REACH_MIDDLE, 1u);
    ask.event_waiting = true;
    ut_check(mp_enemy_interest_must(&row, &ask) == 0u &&
                 mp_enemy_interest_tier(&row, &ask, 0u) == MP_ENEMY_TIER_FIRST,
             "it goes with the first ones, which may overflow but not wait behind the rest, and "
             "is no record that may not wait: the event itself rides in front of the records");
    mp_enemy_interest_written(&row, 1u, 0u);
    ut_check(mp_enemy_interest_tier(&row, &ask, 0u) == MP_ENEMY_TIER_RANKED,
             "a key this peer holds needs no record for its event, and is ranked as ever");
    memset(&row, 0, sizeof row);
    ask = ask_of(MP_ENEMY_REACH_FAR, 1u);
    ask.event_waiting = true;
    row.age           = 40u;   /* however long it has stood there */
    ut_check(mp_enemy_interest_must(&row, &ask) == 0u &&
                 mp_enemy_interest_tier(&row, &ask, 0u) == MP_ENEMY_TIER_SKIP,
             "but not for a far key this peer was never told about: it gets no record at all, "
             "however long it has waited, and the event does not concern it either");
    (void)record;
}

/* A key offered for a block, every field set; its distance behind the cursor is its key. */
static void pick(mp_enemy_interest_pick_t *p, uint16_t key, uint8_t tier, uint16_t accumulator,
                 uint8_t age)
{
    p->key         = key;
    p->turn        = key;
    p->tier        = tier;
    p->accumulator = accumulator;
    p->age         = age;
}

static void check_the_tiers_and_the_order(void)
{
    mp_enemy_interest_row_t  rows[4];
    mp_enemy_interest_ask_t  ask;
    mp_enemy_interest_pick_t picks[4];
    mp_enemy_interest_pick_t again[4];

    ut_section("a first description near goes before any accumulated key");
    memset(rows, 0, sizeof rows);
    ask = ask_of(MP_ENEMY_REACH_NEAR, 1u);
    mp_enemy_interest_step(&rows[0], &ask);
    ut_check(mp_enemy_interest_tier(&rows[0], &ask, 0u) == MP_ENEMY_TIER_FIRST,
             "a near key this peer has not been told of is a first description");
    pick(&picks[0], 7u, MP_ENEMY_TIER_RANKED, 60000u, 1u);
    pick(&picks[1], 9u, MP_ENEMY_TIER_FIRST, 8u, 1u);
    pick(&picks[2], 3u, MP_ENEMY_TIER_MUST, 1u, 1u);
    pick(&picks[3], 5u, MP_ENEMY_TIER_STARVED, 24u, 8u);
    memcpy(again, picks, sizeof picks);
    mp_enemy_interest_order(picks, 4u, false);
    ut_check(picks[0].key == 3u && picks[1].key == 5u && picks[2].key == 9u &&
                 picks[3].key == 7u,
             "the one that may not wait, then the starved by age, then the first description, "
             "then the accumulated one, however much it holds");
    mp_enemy_interest_order(again, 4u, false);
    ut_check(memcmp(picks, again, sizeof picks) == 0, "and the same offer orders the same way");

    ut_section("a silent peer gets what may not wait and at most four more, the oldest first");
    ut_check(mp_enemy_interest_admits(&picks[0], true, 99u),
             "what may not wait is admitted whatever went before");
    ut_check(mp_enemy_interest_admits(&picks[1], true, 3u) &&
                 !mp_enemy_interest_admits(&picks[1], true, 4u),
             "four more are, a fifth is not");
    picks[0].tier        = MP_ENEMY_TIER_RANKED;
    picks[0].age         = 2u;
    picks[0].accumulator = 900u;
    picks[1].tier        = MP_ENEMY_TIER_FIRST;
    picks[1].age         = 1u;
    picks[2].tier        = MP_ENEMY_TIER_RANKED;
    picks[2].age         = 30u;
    picks[2].accumulator = 1u;
    mp_enemy_interest_order(picks, 3u, true);
    ut_check(picks[0].age == 30u && picks[1].age == 2u && picks[2].age == 1u,
             "for a silent peer age alone orders them");
}

/* A peer with room for `room` records a substep, `count` keys of one class, run for `substeps`.
 * Returns the longest any key waited between two of its records, in substeps. */
static uint32_t run_crowd(mp_enemy_reach_t reach, size_t count, size_t room, size_t substeps,
                          uint32_t *writes_of_key0)
{
    mp_enemy_interest_row_t  rows[16];
    mp_enemy_interest_pick_t picks[16];
    uint32_t                 last[16];
    uint32_t                 longest = 0;
    size_t                   next    = 0;
    size_t                   t;

    memset(rows, 0, sizeof rows);
    memset(last, 0, sizeof last);
    *writes_of_key0 = 0;
    /* A far key is only offered to a peer that was told of it once; these were, before t = 1. */
    for (t = 0; reach == MP_ENEMY_REACH_FAR && t < count; ++t) {
        mp_enemy_interest_written(&rows[t], 1u, 0u);
    }
    for (t = 1; t <= substeps; ++t) {
        size_t n = 0;
        size_t i;
        size_t taken = 0;

        for (i = 0; i < count; ++i) {
            mp_enemy_interest_ask_t ask = ask_of(reach, 1u);
            uint32_t                must = mp_enemy_interest_must(&rows[i], &ask);
            mp_enemy_tier_t         tier;

            mp_enemy_interest_step(&rows[i], &ask);
            tier = mp_enemy_interest_tier(&rows[i], &ask, must);
            if (tier == MP_ENEMY_TIER_SKIP) {
                continue;
            }
            picks[n].key         = (uint16_t)i;
            picks[n].turn        = (uint16_t)((i + count - next) % count);
            picks[n].accumulator = rows[i].accumulator;
            picks[n].tier        = (uint8_t)tier;
            picks[n].age         = rows[i].age;
            ++n;
        }
        mp_enemy_interest_order(picks, n, false);
        for (i = 0; i < n && taken < room; ++i, ++taken) {
            size_t key = picks[i].key;

            if (last[key] != 0u && t - last[key] > longest) {
                longest = (uint32_t)(t - last[key]);
            }
            last[key] = (uint32_t)t;
            mp_enemy_interest_written(&rows[key], 1u, 0u);
            next = (key + 1u) % count;
            *writes_of_key0 += key == 0u ? 1u : 0u;
        }
    }
    return longest;
}

static void check_nobody_waits_for_ever(void)
{
    uint32_t writes = 0;
    uint32_t longest;

    ut_section("no key waits past its class limit, and the accumulator is Fiedler's");
    longest = run_crowd(MP_ENEMY_REACH_MIDDLE, 12u, 3u, 400u, &writes);
    ut_checkf(longest <= 12u / 3u + 1u,
              "twelve middle keys, room for three: none waits longer than ceil(n/k) + 1 (%u)",
              (unsigned)longest);
    longest = run_crowd(MP_ENEMY_REACH_NONE, 10u, 4u, 400u, &writes);
    ut_checkf(longest <= 10u / 4u + 1u + 1u,
              "unmeasured keys are the round robin of middle ones (%u)", (unsigned)longest);
    longest = run_crowd(MP_ENEMY_REACH_FAR, 4u, 4u, 320u, &writes);
    ut_checkf(longest == MP_ENEMY_INTEREST_FAR_CADENCE && writes == 320u / 8u,
              "far keys with room for all go every eighth substep and no more often (%u, %u "
              "records of one key in 320 substeps)", (unsigned)longest, (unsigned)writes);

    {
        mp_enemy_interest_row_t row;
        mp_enemy_interest_ask_t near = ask_of(MP_ENEMY_REACH_NEAR, 1u);

        memset(&row, 0, sizeof row);
        mp_enemy_interest_written(&row, 1u, 0u);
        mp_enemy_interest_step(&row, &near);
        ut_check(row.accumulator == MP_ENEMY_INTEREST_BASE_NEAR && row.age == 1u,
                 "a substep adds the priority and a substep of age");
        mp_enemy_interest_step(&row, &near);
        ut_check(row.accumulator == 2u * MP_ENEMY_INTEREST_BASE_NEAR,
                 "a key that did not fit keeps what it had and adds to it");
        mp_enemy_interest_written(&row, 1u, 0u);
        ut_check(row.accumulator == 0u && row.age == 0u,
                 "a key that fitted starts again at nought");
        mp_enemy_interest_step(&row, &near);
        ut_check(mp_enemy_interest_tier(&row, &near, 0u) == MP_ENEMY_TIER_RANKED,
                 "one substep after its record a near key is ranked");
        mp_enemy_interest_step(&row, &near);
        ut_check(mp_enemy_interest_tier(&row, &near, 0u) == MP_ENEMY_TIER_STARVED,
                 "a second substep without one puts it ahead of every ranked key");
    }
    {
        mp_enemy_interest_row_t row;
        mp_enemy_interest_ask_t far = ask_of(MP_ENEMY_REACH_FAR, 1u);
        size_t                  t;

        memset(&row, 0, sizeof row);
        mp_enemy_interest_written(&row, 1u, 0u);
        for (t = 0; t < MP_ENEMY_INTEREST_LIMIT_FAR; ++t) {
            mp_enemy_interest_step(&row, &far);
        }
        ut_check(mp_enemy_interest_tier(&row, &far, 0u) == MP_ENEMY_TIER_STARVED,
                 "a far key thirty two substeps without a record goes ahead of the ranked ones, "
                 "however full the near ones keep the block");
    }
}

static void check_the_priority(void)
{
    mp_enemy_interest_ask_t ask = ask_of(MP_ENEMY_REACH_MIDDLE, 1u);

    ut_section("the priority: 8, 3 and 1 by class, doubled twice at the most");
    ut_check(mp_enemy_interest_priority(&ask) == MP_ENEMY_INTEREST_BASE_MIDDLE, "middle is 3");
    ask.reach = MP_ENEMY_REACH_NONE;
    ut_check(mp_enemy_interest_priority(&ask) == MP_ENEMY_INTEREST_BASE_MIDDLE,
             "an unmeasured key counts as middle");
    ask.reach   = MP_ENEMY_REACH_NEAR;
    ask.engaged = true;
    ut_check(mp_enemy_interest_priority(&ask) == 2u * MP_ENEMY_INTEREST_BASE_NEAR,
             "going for this peer's player doubles it");
    ask.shooting = true;
    ut_check(mp_enemy_interest_priority(&ask) ==
                 MP_ENEMY_INTEREST_BOOST_CAP * MP_ENEMY_INTEREST_BASE_NEAR,
             "and a bolt in flight doubles it again, to the cap");
    ask.reach = MP_ENEMY_REACH_FAR;
    ut_check(mp_enemy_interest_priority(&ask) == 4u * MP_ENEMY_INTEREST_BASE_FAR,
             "a far one that goes for this peer and shoots is worth half a near one alone");

    {
        mp_enemy_interest_row_t row;
        size_t                  t;

        memset(&row, 0, sizeof row);
        ask.reach = MP_ENEMY_REACH_NEAR;
        for (t = 0; t < 5000u; ++t) {
            mp_enemy_interest_step(&row, &ask);
        }
        ut_check(row.accumulator == 0xFFFFu && row.age == 0xFFu,
                 "the accumulator and the age stop at their top rather than wrap");
    }
    {
        mp_enemy_interest_row_t row;
        mp_enemy_interest_ask_t far = ask_of(MP_ENEMY_REACH_FAR, 1u);

        memset(&row, 0, sizeof row);
        mp_enemy_interest_step(&row, &far);
        mp_enemy_interest_step(&row, &far);
        ut_check(row.accumulator == 0u && row.age == 0u,
                 "a far key nobody told this peer of gathers nothing while it stays far");
        row.accumulator = 9u;
        row.age         = 9u;
        row.reach       = (uint8_t)MP_ENEMY_REACH_NEAR;
        mp_enemy_interest_idle(&row);
        ut_check(row.accumulator == 0u && row.age == 0u && row.reach == MP_ENEMY_REACH_NONE,
                 "and a key that is not live starts over");
    }
}

static void check_a_hit_counts_for_a_second(void)
{
    mp_enemy_interest_row_t row;
    mp_enemy_interest_ask_t ask = ask_of(MP_ENEMY_REACH_MIDDLE, 1u);
    uint32_t                struck = 0;
    size_t                  t;

    ut_section("a hit on this peer's player counts as going for it, for a second");
    ask.struck = true;
    ut_check(mp_enemy_interest_priority(&ask) == 2u * MP_ENEMY_INTEREST_BASE_MIDDLE,
             "a hit doubles the priority as going for the player does");
    ask.engaged = true;
    ut_check(mp_enemy_interest_priority(&ask) == 2u * MP_ENEMY_INTEREST_BASE_MIDDLE,
             "and one that also goes for the player is doubled once: it is one reason, not two");

    memset(&row, 0, sizeof row);
    mp_enemy_interest_written(&row, 1u, 0u);
    ut_check(!mp_enemy_interest_struck(&row), "a key that hit nobody is not struck");
    mp_enemy_interest_note_struck(&row);
    ask = ask_of(MP_ENEMY_REACH_MIDDLE, 1u);
    for (t = 0; t < MP_ENEMY_INTEREST_STRUCK_SUBSTEPS + 8u; ++t) {
        ask.struck = mp_enemy_interest_struck(&row);
        struck += ask.struck ? 1u : 0u;
        mp_enemy_interest_step(&row, &ask);
    }
    ut_checkf(struck == MP_ENEMY_INTEREST_STRUCK_SUBSTEPS,
              "the hit counts for %u substeps and then no more", (unsigned)struck);
    ut_checkf(row.accumulator == MP_ENEMY_INTEREST_STRUCK_SUBSTEPS * 2u *
                                     MP_ENEMY_INTEREST_BASE_MIDDLE +
                                 8u * MP_ENEMY_INTEREST_BASE_MIDDLE,
              "a second at twice the priority, then the plain one: %u", (unsigned)row.accumulator);
    mp_enemy_interest_note_struck(&row);
    mp_enemy_interest_written(&row, 1u, 0u);
    ut_check(mp_enemy_interest_struck(&row),
             "a record of the key going out does not end the second: the hit was on the player");
    mp_enemy_interest_idle(&row);
    ut_check(!mp_enemy_interest_struck(&row), "a key that is not live any more starts over");

    {
        mp_enemy_interest_row_t far_row;
        mp_enemy_interest_ask_t far = ask_of(MP_ENEMY_REACH_FAR, 1u);

        memset(&far_row, 0, sizeof far_row);
        mp_enemy_interest_note_struck(&far_row);
        for (t = 0; t < MP_ENEMY_INTEREST_STRUCK_SUBSTEPS; ++t) {
            mp_enemy_interest_step(&far_row, &far);
        }
        ut_check(!mp_enemy_interest_struck(&far_row),
                 "the second runs out for a far key this peer was never told of as well");
    }
}

int main(void)
{
    check_the_classes();
    check_the_wake_scale();
    check_what_may_not_wait();
    check_the_tiers_and_the_order();
    check_nobody_waits_for_ever();
    check_the_priority();
    check_a_hit_counts_for_a_second();
    return ut_summary("mp_enemy_interest");
}
