/* Whose a script's doors are on the host: the rules of mp_scene_claim_rule, driven over their
 * edges with no game in the process.
 *
 * SIZE NOTE: over 600 lines, one section for each of the six rules of the module, each closed in
 * itself. The next seam is the latch with a far player's lock, the two longest, into a file of
 * their own.
 */
#include "unittest.h"

#include "mp_scene_claim_rule.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* ==============================================================================================
 * The run.
 * ============================================================================================ */

/* An actor of a session somebody is joined in, with nothing known about it. */
static mp_scene_run_evidence_t nothing_known(void)
{
    mp_scene_run_evidence_t evidence;

    memset(&evidence, 0, sizeof evidence);
    evidence.joined    = true;
    evidence.actor.own = MP_SCENE_ANSWER_NONE;
    return evidence;
}

/* The rule written a second time, the plainest way, for the walk over every input. */
static mp_scene_run_rule_t the_plain_rule(const mp_scene_run_evidence_t *e, uint8_t *bank)
{
    *bank = 0u;
    if (!e->joined) {
        return MP_SCENE_RUN_ALONE;
    }
    if (e->of_the_scene) {
        return MP_SCENE_RUN_OF_THE_SCENE;
    }
    if (e->taker) {
        return MP_SCENE_RUN_OF_A_TAKER;
    }
    if (e->actor.own == MP_SCENE_ANSWER_PLAYER) {
        *bank = e->actor.own_bank;
        return MP_SCENE_RUN_BY_OWN_ANSWER;
    }
    if (e->actor.died && e->actor.attacker_known) {
        *bank = e->actor.attacker_bank;
        return MP_SCENE_RUN_BY_LAST_ATTACKER;
    }
    if (e->actor.own != MP_SCENE_ANSWER_NOT_A_PLAYER && e->woke_for_far && e->woke_bank != 0u) {
        *bank = e->woke_bank;
        return MP_SCENE_RUN_BY_THE_WAKING;
    }
    return MP_SCENE_RUN_BY_HOST_ANCHOR;
}

static void check_the_run(void)
{
    mp_scene_run_evidence_t e;
    uint8_t                 bank = 9u;
    unsigned                code;
    unsigned                wrong = 0u;
    unsigned                far_alone = 0u;

    ut_section("a far player's run: his own fresh answer, his kill, or his waking");
    e                = nothing_known();
    e.actor.own      = MP_SCENE_ANSWER_PLAYER;
    e.actor.own_bank = 2u;
    ut_check(mp_scene_run(&e, &bank) == MP_SCENE_RUN_BY_OWN_ANSWER && bank == 2u,
             "the actor's own fresh answer was the far player of bank 2: the run is his");
    e.actor.own_bank = 0u;
    ut_check(mp_scene_run(&e, &bank) == MP_SCENE_RUN_BY_OWN_ANSWER && bank == 0u,
             "its own fresh answer was the host: the run is the host's, by the same rule");
    e                      = nothing_known();
    e.actor.died           = true;
    e.actor.attacker_known = true;
    e.actor.attacker_bank  = 3u;
    ut_check(mp_scene_run(&e, &bank) == MP_SCENE_RUN_BY_LAST_ATTACKER && bank == 3u,
             "it died with no fresh answer and bank 3 hurt it lately: the run is his");
    e              = nothing_known();
    e.woke_for_far = true;
    e.woke_bank    = 1u;
    ut_check(mp_scene_run(&e, &bank) == MP_SCENE_RUN_BY_THE_WAKING && bank == 1u,
             "no answer on record and its placement woke for bank 1: the run is his");
    e.actor.own = MP_SCENE_ANSWER_STALE;
    ut_check(mp_scene_run(&e, &bank) == MP_SCENE_RUN_BY_THE_WAKING && bank == 1u,
             "a stale answer is no fresh one: the waking still speaks");
    e.actor.own = MP_SCENE_ANSWER_NOT_A_PLAYER;
    ut_check(mp_scene_run(&e, &bank) == MP_SCENE_RUN_BY_HOST_ANCHOR && bank == 0u,
             "a fresh answer that named an ally or nobody is an answer: the waking is not asked");
    e.actor.own      = MP_SCENE_ANSWER_PLAYER;
    e.actor.own_bank = 0u;
    ut_check(mp_scene_run(&e, &bank) == MP_SCENE_RUN_BY_OWN_ANSWER && bank == 0u,
             "a fresh answer for the host beats a waking for a far player");
    e           = nothing_known();
    e.woke_bank = 2u;
    ut_check(mp_scene_run(&e, &bank) == MP_SCENE_RUN_BY_HOST_ANCHOR && bank == 0u,
             "a bank with no waking behind it names nobody");
    e.woke_for_far = true;
    e.woke_bank    = 0u;
    ut_check(mp_scene_run(&e, &bank) == MP_SCENE_RUN_BY_HOST_ANCHOR && bank == 0u,
             "and a waking for the host's own bank is no far player's");

    ut_section("nobody joined means no");
    e                = nothing_known();
    e.joined         = false;
    e.actor.own      = MP_SCENE_ANSWER_PLAYER;
    e.actor.own_bank = 2u;
    e.woke_for_far   = true;
    e.woke_bank      = 2u;
    ut_check(mp_scene_run(&e, &bank) == MP_SCENE_RUN_ALONE && bank == 0u,
             "with nobody else in the session every run is the host's, whatever is on record");

    ut_section("the host's own actors are his whatever they last heard");
    e                = nothing_known();
    e.actor.own      = MP_SCENE_ANSWER_PLAYER;
    e.actor.own_bank = 2u;
    e.of_the_scene   = true;
    ut_check(mp_scene_run(&e, &bank) == MP_SCENE_RUN_OF_THE_SCENE && bank == 0u,
             "the actor of the host's scene, a far player its fresh answer: the host's");
    e.of_the_scene = false;
    e.taker        = true;
    ut_check(mp_scene_run(&e, &bank) == MP_SCENE_RUN_OF_A_TAKER && bank == 0u,
             "an actor that took here and has not given back, the same answer: the host's");
    e.taker = false;
    ut_check(mp_scene_run(&e, &bank) == MP_SCENE_RUN_BY_OWN_ANSWER && bank == 2u,
             "and once it is neither, that answer decides again");

    ut_section("nothing known is the host's, and so is nothing asked");
    e = nothing_known();
    ut_check(mp_scene_run(&e, &bank) == MP_SCENE_RUN_BY_HOST_ANCHOR && bank == 0u,
             "an actor that asked nothing, was hurt by nobody and woke for nobody");
    ut_check(mp_scene_run(NULL, &bank) == MP_SCENE_RUN_ALONE && bank == 0u,
             "no evidence at all");
    e.actor.own      = MP_SCENE_ANSWER_PLAYER;
    e.actor.own_bank = 1u;
    ut_check(mp_scene_run(&e, NULL) == MP_SCENE_RUN_BY_OWN_ANSWER,
             "the rule is answered with nowhere to write the bank");

    ut_section("every input: the rule against its plainest writing");
    /* Bits: joined, of the scene, taker, died, attacker known, woke; then the own answer in four
     * and the three banks in three values each. */
    for (code = 0u; code < 64u * 4u * 27u; ++code) {
        uint8_t                 want_bank = 0u;
        uint8_t                 got_bank  = 0u;
        mp_scene_run_rule_t     want;
        mp_scene_run_rule_t     got;
        unsigned                rest = code;
        mp_scene_run_evidence_t walk;

        memset(&walk, 0, sizeof walk);
        walk.joined               = (rest & 1u) != 0u;
        walk.of_the_scene         = (rest & 2u) != 0u;
        walk.taker                = (rest & 4u) != 0u;
        walk.actor.died           = (rest & 8u) != 0u;
        walk.actor.attacker_known = (rest & 16u) != 0u;
        walk.woke_for_far         = (rest & 32u) != 0u;
        rest >>= 6u;
        walk.actor.own = (mp_scene_answer_t)(rest % 4u);
        rest /= 4u;
        walk.actor.own_bank = (uint8_t)(rest % 3u);
        rest /= 3u;
        walk.actor.attacker_bank = (uint8_t)(rest % 3u);
        rest /= 3u;
        walk.woke_bank = (uint8_t)(rest % 3u);
        want = the_plain_rule(&walk, &want_bank);
        got  = mp_scene_run(&walk, &got_bank);
        if (want != got || want_bank != got_bank) {
            ++wrong;
        }
        if (got_bank != 0u && (!walk.joined || walk.of_the_scene || walk.taker)) {
            ++far_alone;
        }
    }
    ut_checkf(wrong == 0u, "%u of %u inputs answered otherwise", wrong, 64u * 4u * 27u);
    ut_checkf(far_alone == 0u, "a far player's run with nobody joined, or for an actor of the "
              "host's scene or a taker, %u time(s)", far_alone);

    ut_section("every rule has words of its own");
    {
        unsigned a;
        unsigned b;
        bool     apart = true;

        for (a = 0u; a < (unsigned)MP_SCENE_RUN_RULES; ++a) {
            for (b = a + 1u; b < (unsigned)MP_SCENE_RUN_RULES; ++b) {
                apart = apart && strcmp(mp_scene_run_text((mp_scene_run_rule_t)a),
                                        mp_scene_run_text((mp_scene_run_rule_t)b)) != 0;
            }
        }
        ut_check(apart, "the line that names the rule tells all seven apart");
    }
}

/* ==============================================================================================
 * The marks.
 * ============================================================================================ */

static void check_the_marks(void)
{
    static const uint8_t BITS[3] = { MP_SCENE_MARK_CAMERA, MP_SCENE_MARK_LOCK,
                                     MP_SCENE_MARK_BARS };
    uint8_t  marks;
    unsigned taken;
    unsigned wrong = 0u;
    size_t   i;

    ut_section("a run of the host's takes and gives back, bit by bit");
    marks = mp_scene_mark_taken(0u, MP_SCENE_MARK_BARS, false);
    marks = mp_scene_mark_taken(marks, MP_SCENE_MARK_CAMERA, false);
    marks = mp_scene_mark_taken(marks, MP_SCENE_MARK_LOCK, false);
    ut_check(marks == (MP_SCENE_MARK_BARS | MP_SCENE_MARK_CAMERA | MP_SCENE_MARK_LOCK),
             "the lock opcode's three takes set three bits");
    ut_check(mp_scene_mark_given_back(&marks, MP_SCENE_MARK_CAMERA, true) &&
                 marks == (MP_SCENE_MARK_BARS | MP_SCENE_MARK_LOCK),
             "the camera given back clears the camera's bit and no other");
    ut_check(mp_scene_mark_given_back(&marks, MP_SCENE_MARK_LOCK, true) &&
                 marks == MP_SCENE_MARK_BARS,
             "then the lock's");
    ut_check(mp_scene_mark_given_back(&marks, MP_SCENE_MARK_BARS, true) && marks == 0u,
             "then the bars'");
    ut_check(mp_scene_mark_given_back(&marks, MP_SCENE_MARK_LOCK, true) && marks == 0u,
             "and a release for nothing taken goes through in a run of the host's");

    ut_section("a far player's run gives back only what its own actor took");
    marks = 0u;
    for (i = 0u; i < 3u; ++i) {
        ut_check(!mp_scene_mark_given_back(&marks, BITS[i], false) && marks == 0u,
                 "a release with no mark is refused and changes nothing");
    }
    /* The end of a camera dolly is three calls in this order: the camera, the lock, the bars. An
     * actor that took all three for the host gives all three back with a far player nearer. */
    marks = MP_SCENE_MARK_BARS | MP_SCENE_MARK_CAMERA | MP_SCENE_MARK_LOCK;
    ut_check(mp_scene_mark_given_back(&marks, MP_SCENE_MARK_CAMERA, false) &&
                 mp_scene_mark_given_back(&marks, MP_SCENE_MARK_LOCK, false) &&
                 mp_scene_mark_given_back(&marks, MP_SCENE_MARK_BARS, false) && marks == 0u,
             "all three calls of a dolly's end go through: none spends the mark of the next");
    ut_check(!mp_scene_mark_given_back(&marks, MP_SCENE_MARK_LOCK, false),
             "and the same end a second time gives nothing back");
    /* The lock opcode with a release and bars on in one: the bars go up, the camera and the lock
     * are given back, and the bars' bit stands. */
    marks = MP_SCENE_MARK_CAMERA | MP_SCENE_MARK_LOCK;
    marks = mp_scene_mark_taken(marks, MP_SCENE_MARK_BARS, false);
    (void)mp_scene_mark_given_back(&marks, MP_SCENE_MARK_CAMERA, true);
    (void)mp_scene_mark_given_back(&marks, MP_SCENE_MARK_LOCK, true);
    ut_check(marks == MP_SCENE_MARK_BARS && mp_scene_mark_keeps_the_host(marks),
             "bars taken in the opcode that gives camera and lock back stay marked");
    marks = MP_SCENE_MARK_CAMERA;
    ut_check(!mp_scene_mark_given_back(&marks, MP_SCENE_MARK_LOCK, false) &&
                 !mp_scene_mark_given_back(&marks, MP_SCENE_MARK_BARS, false) &&
                 marks == MP_SCENE_MARK_CAMERA,
             "an actor that took the camera alone gives back neither a lock nor bars");

    ut_section("a spoken line's camera is marked, and flagged");
    marks = mp_scene_mark_taken(0u, MP_SCENE_MARK_CAMERA, true);
    ut_check(marks == (MP_SCENE_MARK_CAMERA | MP_SCENE_MARK_SPOKEN) &&
                 !mp_scene_mark_keeps_the_host(marks),
             "the camera bit stands, and it holds no answer for the host");
    ut_check(mp_scene_mark_given_back(&marks, MP_SCENE_MARK_CAMERA, false) && marks == 0u,
             "its speaker gives the camera back in a far player's run, and the flag goes with it");
    marks = mp_scene_mark_taken(0u, MP_SCENE_MARK_CAMERA, false);
    marks = mp_scene_mark_taken(marks, MP_SCENE_MARK_CAMERA, true);
    ut_check(marks == MP_SCENE_MARK_CAMERA && mp_scene_mark_keeps_the_host(marks),
             "a line spoken by an actor that took the camera by a dolly leaves it unflagged");
    marks = mp_scene_mark_taken(0u, MP_SCENE_MARK_CAMERA, true);
    marks = mp_scene_mark_taken(marks, MP_SCENE_MARK_CAMERA, false);
    ut_check(marks == MP_SCENE_MARK_CAMERA && mp_scene_mark_keeps_the_host(marks),
             "and a dolly after a spoken line takes the flag away");
    marks = mp_scene_mark_taken(0u, MP_SCENE_MARK_CAMERA, true);
    marks = mp_scene_mark_taken(marks, MP_SCENE_MARK_LOCK, false);
    ut_check(marks == (MP_SCENE_MARK_CAMERA | MP_SCENE_MARK_SPOKEN | MP_SCENE_MARK_LOCK) &&
                 mp_scene_mark_keeps_the_host(marks),
             "a lock beside a spoken line's camera keeps the host for the lock's sake");

    ut_section("what keeps the host's answer");
    ut_check(!mp_scene_mark_keeps_the_host(0u), "no mark keeps nothing");
    ut_check(mp_scene_mark_keeps_the_host(MP_SCENE_MARK_CAMERA) &&
                 mp_scene_mark_keeps_the_host(MP_SCENE_MARK_LOCK) &&
                 mp_scene_mark_keeps_the_host(MP_SCENE_MARK_BARS),
             "each of the three, taken by a script's own opcode, does");

    ut_section("every set of marks: a release never touches another bit");
    for (taken = 0u; taken < 16u; ++taken) {
        for (i = 0u; i < 3u; ++i) {
            int hosts;

            for (hosts = 0; hosts < 2; ++hosts) {
                uint8_t before = (uint8_t)taken;
                uint8_t after  = before;
                uint8_t own    = BITS[i] == MP_SCENE_MARK_CAMERA
                                     ? (uint8_t)(MP_SCENE_MARK_CAMERA | MP_SCENE_MARK_SPOKEN)
                                     : BITS[i];
                bool    went   = mp_scene_mark_given_back(&after, BITS[i], hosts != 0);
                bool    want   = hosts != 0 || (before & BITS[i]) != 0u;

                if (went != want || (after & (uint8_t)~own) != (before & (uint8_t)~own) ||
                    (went && (after & own) != 0u) || (!went && after != before)) {
                    ++wrong;
                }
            }
        }
    }
    ut_checkf(wrong == 0u, "%u of 96 releases answered or wrote otherwise", wrong);
    ut_check(mp_scene_mark_given_back(NULL, MP_SCENE_MARK_LOCK, true) &&
                 !mp_scene_mark_given_back(NULL, MP_SCENE_MARK_LOCK, false),
             "an actor with no marks kept gives back in a run of the host's and in no other");
}

/* ==============================================================================================
 * The doors of a run.
 * ============================================================================================ */

static void check_the_doors_of_a_run(void)
{
    mp_scene_run_doors_t doors;
    size_t               i;

    ut_section("refused takes are kept in their order");
    memset(&doors, 0, sizeof doors);
    ut_check(mp_scene_run_doors_keep(&doors, MP_SCENE_MARK_BARS, 1) &&
                 mp_scene_run_doors_keep(&doors, MP_SCENE_MARK_CAMERA, 5),
             "the lock opcode's bars and its camera, refused in a far player's run");
    ut_check(doors.count == 2u && doors.door[0].bit == MP_SCENE_MARK_BARS &&
                 doors.door[0].argument == 1 && doors.door[1].bit == MP_SCENE_MARK_CAMERA &&
                 doors.door[1].argument == 5,
             "the bars first and the camera second, each with what the engine was handed");

    ut_section("six are kept, and a seventh is counted");
    for (i = 2u; i < MP_SCENE_RUN_DOORS; ++i) {
        (void)mp_scene_run_doors_keep(&doors, MP_SCENE_MARK_CAMERA, (int32_t)i);
    }
    ut_check(doors.count == MP_SCENE_RUN_DOORS && doors.left_out == 0u, "six kept");
    ut_check(!mp_scene_run_doors_keep(&doors, MP_SCENE_MARK_CAMERA, 99) &&
                 doors.count == MP_SCENE_RUN_DOORS && doors.left_out == 1u &&
                 doors.door[MP_SCENE_RUN_DOORS - 1u].argument == (int32_t)MP_SCENE_RUN_DOORS - 1,
             "the seventh is refused room, counted, and overwrites nothing");

    ut_section("a run that ends takes them with it");
    mp_scene_run_doors_forget(&doors);
    ut_check(doors.count == 0u && doors.left_out == 1u,
             "nothing is remembered, and the count of the report stays");
    ut_check(mp_scene_run_doors_keep(&doors, MP_SCENE_MARK_BARS, 1) && doors.count == 1u,
             "the next run keeps its own from the first row");
    mp_scene_run_doors_forget(NULL);
    ut_check(!mp_scene_run_doors_keep(NULL, MP_SCENE_MARK_BARS, 1), "no list keeps nothing");
}

/* ==============================================================================================
 * The latch.
 * ============================================================================================ */

static void check_the_latch(void)
{
    mp_scene_latch_t latch;
    uint32_t         keys[MP_SCENE_LATCH_KEYS + 2u];
    uint32_t         key;

    ut_section("with no window open, a door is nobody's to refuse");
    memset(&latch, 0, sizeof latch);
    ut_check(!mp_scene_latch_door(&latch, 7u, 100u, true) &&
                 !mp_scene_latch_door(&latch, 7u, 100u, false) && latch.count == 0u,
             "a door before any release goes through and writes nothing down, in either run");
    ut_check(mp_scene_latch_holds(&latch, 7u) == MP_SCENE_REFUSAL_NONE,
             "and its placement is not held");

    ut_section("in the window every actor that opens a door is written down and refused");
    mp_scene_latch_open(&latch, 100u);
    ut_check(mp_scene_latch_door(&latch, 7u, 100u, true) && latch.count == 1u,
             "a door in the substep of the release: refused, written down, a run of the host's "
             "or not");
    ut_check(mp_scene_latch_door(&latch, 7u, 101u, true) && latch.count == 1u,
             "the same placement again: refused, written down once");
    ut_check(mp_scene_latch_door(&latch, 9u, 100u + MP_SCENE_LATCH_SUBSTEPS - 1u, false) &&
                 latch.count == 2u,
             "another placement on the last substep of the window: refused, written down");
    ut_check(!mp_scene_latch_door(&latch, 11u, 100u + MP_SCENE_LATCH_SUBSTEPS, true) &&
                 latch.count == 2u && !latch.open,
             "a third one substep later: the window has run out, and the door goes through");

    ut_section("a placement written down stays refused until the level ends");
    ut_check(mp_scene_latch_door(&latch, 7u, 100000u, true) &&
                 mp_scene_latch_door(&latch, 9u, 100000u, true),
             "long after the window both are still refused, in a run of the host's as well");
    ut_check(mp_scene_latch_holds(&latch, 7u) == MP_SCENE_REFUSAL_REPAIRED &&
                 mp_scene_latch_holds(&latch, 9u) == MP_SCENE_REFUSAL_REPAIRED &&
                 mp_scene_latch_holds(&latch, 11u) == MP_SCENE_REFUSAL_NONE,
             "and asked without opening a door, the same two are held and the third is not");
    ut_checkf(latch.refused == 5u && latch.left_out == 0u,
              "five doors refused so far, none for want of room (%u, %u)",
              (unsigned)latch.refused, (unsigned)latch.left_out);
    ut_check(mp_scene_latch_keys(&latch, keys, MP_SCENE_LATCH_KEYS) == 2u && keys[0] == 7u &&
                 keys[1] == 9u,
             "the line names them in the order they were written down");
    ut_check(mp_scene_latch_keys(&latch, keys, 1u) == 1u && keys[0] == 7u,
             "and no more of them than it has room for");

    ut_section("a second release opens the window again and keeps what was written down");
    mp_scene_latch_open(&latch, 200000u);
    ut_check(mp_scene_latch_door(&latch, 11u, 200010u, true) && latch.count == 3u &&
                 mp_scene_latch_holds(&latch, 7u) == MP_SCENE_REFUSAL_REPAIRED,
             "the third placement is written down now, beside the first two");

    ut_section("the set holds eight placements, and a ninth is refused only while the window "
               "stands");
    memset(&latch, 0, sizeof latch);
    mp_scene_latch_open(&latch, 0u);
    for (key = 0u; key < MP_SCENE_LATCH_KEYS; ++key) {
        (void)mp_scene_latch_door(&latch, 100u + key, 1u, true);
    }
    ut_check(latch.count == MP_SCENE_LATCH_KEYS, "eight written down");
    ut_check(mp_scene_latch_door(&latch, 999u, 2u, true) && latch.count == MP_SCENE_LATCH_KEYS &&
                 latch.left_out == 1u && latch.refused == MP_SCENE_LATCH_KEYS,
             "a ninth in the window: refused and counted apart, with no room to write it down");
    ut_check(!mp_scene_latch_door(&latch, 999u, MP_SCENE_LATCH_SUBSTEPS, true) &&
                 mp_scene_latch_holds(&latch, 999u) == MP_SCENE_REFUSAL_NONE,
             "and through once the window has run out");
    ut_check(mp_scene_latch_keys(&latch, keys, MP_SCENE_LATCH_KEYS + 2u) == MP_SCENE_LATCH_KEYS,
             "the line never names more than the set holds");

    ut_section("a window opened after now, by a counter started over, is run out and not open");
    memset(&latch, 0, sizeof latch);
    mp_scene_latch_open(&latch, 5000u);
    ut_check(!mp_scene_latch_door(&latch, 3u, 10u, true) && !latch.open,
             "a door 4990 substeps before the window began goes through");

    ut_section("the one exit empties the set and closes the window");
    memset(&latch, 0, sizeof latch);
    mp_scene_latch_open(&latch, 0u);
    (void)mp_scene_latch_door(&latch, 4u, 1u, true);
    (void)mp_scene_latch_foreign(&latch, 5u);
    mp_scene_latch_clear(&latch);
    ut_check(latch.count == 0u && !latch.open &&
                 mp_scene_latch_holds(&latch, 4u) == MP_SCENE_REFUSAL_NONE &&
                 mp_scene_latch_holds(&latch, 5u) == MP_SCENE_REFUSAL_NONE &&
                 !mp_scene_latch_door(&latch, 4u, 2u, false) && latch.refused == 1u &&
                 latch.foreign == 1u,
             "nothing is held, a door in what was the window goes through, and the counts of "
             "the report stay");

    ut_section("nothing to ask is nothing held");
    ut_check(!mp_scene_latch_door(NULL, 1u, 1u, false) &&
                 mp_scene_latch_holds(NULL, 1u) == MP_SCENE_REFUSAL_NONE &&
                 !mp_scene_latch_foreign(NULL, 1u) && mp_scene_latch_keys(NULL, keys, 4u) == 0u &&
                 mp_scene_latch_keys(&latch, NULL, 4u) == 0u,
             "no latch refuses nothing and names nothing");
    mp_scene_latch_open(NULL, 1u);
    mp_scene_latch_clear(NULL);
}

/* A lock of a far player's that was no scene of the host's: a lift he rides. Its script raises
 * the lock again on its next run, so its doors are refused until the host himself is the player
 * it means. */
static void check_a_foreign_lock(void)
{
    mp_scene_latch_t latch;
    uint32_t         keys[MP_SCENE_LATCH_KEYS];
    uint32_t         key;

    ut_section("a foreign lock's placement is refused while its run is a far player's");
    memset(&latch, 0, sizeof latch);
    ut_check(mp_scene_latch_foreign(&latch, 89u) && latch.count == 1u && latch.foreign == 1u &&
                 mp_scene_latch_holds(&latch, 89u) == MP_SCENE_REFUSAL_FOREIGN,
             "written down with its reason, no window needed");
    ut_check(mp_scene_latch_door(&latch, 89u, 10u, false) &&
                 mp_scene_latch_door(&latch, 89u, 5000u, false) && latch.refused == 2u,
             "its door on the next run and its door minutes later: refused");
    ut_check(mp_scene_latch_keys(&latch, keys, MP_SCENE_LATCH_KEYS) == 0u,
             "the line of the placements a window wrote down does not name it");

    ut_section("a run of the host's takes it back");
    ut_check(!mp_scene_latch_door(&latch, 89u, 5001u, true) && latch.count == 0u &&
                 latch.taken_back == 1u &&
                 mp_scene_latch_holds(&latch, 89u) == MP_SCENE_REFUSAL_NONE,
             "the host rides the lift himself: the door goes through and the placement is free");
    ut_check(!mp_scene_latch_door(&latch, 89u, 5002u, false),
             "and a far player's run after that is judged like any other placement's");

    ut_section("taken back inside a window, it is written down again as the window's");
    (void)mp_scene_latch_foreign(&latch, 89u);
    mp_scene_latch_open(&latch, 6000u);
    ut_check(mp_scene_latch_door(&latch, 89u, 6001u, true) &&
                 mp_scene_latch_holds(&latch, 89u) == MP_SCENE_REFUSAL_REPAIRED,
             "a run of the host's lets the foreign reason go, and the open window takes it");

    ut_section("a placement a window wrote down is not made foreign, and the set has its room");
    ut_check(mp_scene_latch_foreign(&latch, 89u) &&
                 mp_scene_latch_holds(&latch, 89u) == MP_SCENE_REFUSAL_REPAIRED &&
                 mp_scene_latch_door(&latch, 89u, 900000u, true),
             "it keeps the window's reason and stays refused for the host's runs too");
    memset(&latch, 0, sizeof latch);
    for (key = 0u; key < MP_SCENE_LATCH_KEYS; ++key) {
        (void)mp_scene_latch_foreign(&latch, key);
    }
    ut_check(!mp_scene_latch_foreign(&latch, 500u) && latch.left_out == 1u &&
                 latch.foreign == MP_SCENE_LATCH_KEYS &&
                 !mp_scene_latch_door(&latch, 500u, 1u, false),
             "a ninth is counted and not written down, and its doors are not refused");
    ut_check(!mp_scene_latch_door(&latch, 3u, 2u, true) &&
                 latch.count == MP_SCENE_LATCH_KEYS - 1u &&
                 mp_scene_latch_holds(&latch, 4u) == MP_SCENE_REFUSAL_FOREIGN &&
                 mp_scene_latch_holds(&latch, 7u) == MP_SCENE_REFUSAL_FOREIGN,
             "one taken back out of the middle leaves the others written down");
}

/* A placement written down by the release itself, with no door opened: who had taken by then,
 * and the hero the release sent away. */
static void check_a_placement_written_down(void)
{
    mp_scene_latch_t latch;
    uint32_t         keys[MP_SCENE_LATCH_KEYS];
    uint32_t         key;

    ut_section("a placement is written down with no window and no door");
    memset(&latch, 0, sizeof latch);
    ut_check(mp_scene_latch_repaired(&latch, 215u) && latch.count == 1u && !latch.open &&
                 mp_scene_latch_holds(&latch, 215u) == MP_SCENE_REFUSAL_REPAIRED,
             "it stands as repaired, and no window was opened for it");
    ut_check(mp_scene_latch_door(&latch, 215u, 1u, true) &&
                 mp_scene_latch_door(&latch, 215u, 900000u, false) && latch.count == 1u,
             "its doors are refused in the host's runs and in a far player's, for the level");
    ut_check(mp_scene_latch_keys(&latch, keys, MP_SCENE_LATCH_KEYS) == 1u && keys[0] == 215u,
             "and the line of the placements written down names it");
    ut_check(mp_scene_latch_repaired(&latch, 215u) && latch.count == 1u,
             "written down a second time it is still one row");

    ut_section("a foreign placement written down becomes a repaired one");
    memset(&latch, 0, sizeof latch);
    (void)mp_scene_latch_foreign(&latch, 89u);
    ut_check(mp_scene_latch_repaired(&latch, 89u) && latch.count == 1u &&
                 mp_scene_latch_holds(&latch, 89u) == MP_SCENE_REFUSAL_REPAIRED &&
                 mp_scene_latch_door(&latch, 89u, 7u, true) && latch.taken_back == 0u,
             "a run of the host's no longer takes it back");

    ut_section("the set has its room, and nothing to ask is nothing written");
    memset(&latch, 0, sizeof latch);
    for (key = 0u; key < MP_SCENE_LATCH_KEYS; ++key) {
        (void)mp_scene_latch_repaired(&latch, key);
    }
    ut_check(!mp_scene_latch_repaired(&latch, 500u) && latch.left_out == 1u &&
                 mp_scene_latch_holds(&latch, 500u) == MP_SCENE_REFUSAL_NONE,
             "a ninth is counted and not written down");
    ut_check(!mp_scene_latch_repaired(NULL, 1u), "no latch: false");

    ut_section("a foreign row goes with its actor, a repaired one stays");
    memset(&latch, 0, sizeof latch);
    (void)mp_scene_latch_foreign(&latch, 171u);
    (void)mp_scene_latch_repaired(&latch, 215u);
    mp_scene_latch_forget_foreign(&latch, 171u);
    mp_scene_latch_forget_foreign(&latch, 215u);
    mp_scene_latch_forget_foreign(&latch, 7u);
    mp_scene_latch_forget_foreign(NULL, 171u);
    ut_check(latch.count == 1u && mp_scene_latch_holds(&latch, 171u) == MP_SCENE_REFUSAL_NONE &&
                 mp_scene_latch_holds(&latch, 215u) == MP_SCENE_REFUSAL_REPAIRED,
             "the lift trigger that removed itself frees its row; the placement a release wrote "
             "down keeps its own for the next actor on it");
}

/* ==============================================================================================
 * The waking.
 * ============================================================================================ */

static void check_the_waking(void)
{
    mp_scene_woke_t woke;
    uint8_t         bank = 9u;
    uintptr_t       record;

    ut_section("a placement woken for a far player is his for two seconds");
    memset(&woke, 0, sizeof woke);
    ut_check(!mp_scene_woke_for(&woke, 0x5000u, 10u, &bank) && bank == 9u,
             "nothing on record names nobody and writes no bank");
    mp_scene_woke_note(&woke, 0x5000u, 2u, 10u);
    ut_check(mp_scene_woke_for(&woke, 0x5000u, 10u, &bank) && bank == 2u,
             "in the substep of the waking, which is the actor's first run");
    ut_check(mp_scene_woke_for(&woke, 0x5000u, 10u + MP_SCENE_WOKE_SUBSTEPS, &bank),
             "and sixty four substeps later");
    ut_check(!mp_scene_woke_for(&woke, 0x5000u, 11u + MP_SCENE_WOKE_SUBSTEPS, &bank),
             "and not at sixty five");
    ut_check(!mp_scene_woke_for(&woke, 0x5004u, 10u, &bank),
             "another placement's record is not his");
    ut_check(mp_scene_woke_for(&woke, 0x5000u, 10u, NULL),
             "the question is answered with nowhere to write the bank");

    ut_section("a second answer for the same record stamps it again");
    mp_scene_woke_note(&woke, 0x5000u, 3u, 60u);
    ut_check(mp_scene_woke_for(&woke, 0x5000u, 60u + MP_SCENE_WOKE_SUBSTEPS, &bank) &&
                 bank == 3u && woke.noted == 2u,
             "the newer bank and the newer substep, in the one row");
    ut_check(woke.row[1].record == 0u, "and no second row was taken for it");

    ut_section("a stamp after now reads as old");
    ut_check(!mp_scene_woke_for(&woke, 0x5000u, 5u, &bank),
             "a counter started over does not make a waking fresh for an age");

    ut_section("thirty two are kept, and the oldest row gives way");
    memset(&woke, 0, sizeof woke);
    for (record = 1u; record <= MP_SCENE_WOKE_ROWS; ++record) {
        mp_scene_woke_note(&woke, 0x1000u * record, 1u, 100u);
    }
    ut_check(woke.replaced == 0u && mp_scene_woke_for(&woke, 0x1000u, 100u, &bank),
             "thirty two written down, the first still there");
    mp_scene_woke_note(&woke, 0x99000u, 2u, 101u);
    ut_check(woke.replaced == 1u && !mp_scene_woke_for(&woke, 0x1000u, 101u, &bank) &&
                 mp_scene_woke_for(&woke, 0x99000u, 101u, &bank) && bank == 2u &&
                 mp_scene_woke_for(&woke, 0x2000u, 101u, &bank),
             "a thirty third takes the first one's row and is counted, the second stays");
    mp_scene_woke_note(&woke, 0x77000u, 1u, 100u + 2u * MP_SCENE_WOKE_SUBSTEPS);
    ut_check(woke.replaced == 1u, "a row whose waking is old is taken without a count");

    ut_section("what names no far player is not written down");
    memset(&woke, 0, sizeof woke);
    mp_scene_woke_note(&woke, 0x5000u, 0u, 10u);
    mp_scene_woke_note(&woke, 0u, 2u, 10u);
    mp_scene_woke_note(NULL, 0x5000u, 2u, 10u);
    ut_check(woke.noted == 0u && !mp_scene_woke_for(&woke, 0x5000u, 10u, &bank) &&
                 !mp_scene_woke_for(&woke, 0u, 10u, &bank) &&
                 !mp_scene_woke_for(NULL, 0x5000u, 10u, &bank),
             "the host's own bank, no record and no table");
}

/* ==============================================================================================
 * A release owed.
 * ============================================================================================ */

static void check_a_release_owed(void)
{
    mp_scene_owed_t owed;
    size_t          row;

    ut_section("an actor that was refused a release and takes itself away leaves it owed");
    memset(&owed, 0, sizeof owed);
    ut_check(mp_scene_owed_step(&owed, 0u, 10u, true, true) == MP_SCENE_OWED_NOTHING,
             "an empty row owes nothing");
    mp_scene_owed_note(&owed, 172u, 0x8000u, 10u);
    ut_check(owed.noted == 1u && owed.row[0].have && owed.row[0].key == 172u,
             "the refused release is kept with its placement and its actor");
    ut_check(mp_scene_owed_step(&owed, 0u, 10u, true, true) == MP_SCENE_OWED_WAITS &&
                 owed.row[0].have,
             "while the actor lives it may still give back itself: nothing is done");
    ut_check(mp_scene_owed_step(&owed, 0u, 11u, false, true) == MP_SCENE_OWED_DUE &&
                 !owed.row[0].have && owed.due == 1u,
             "it is gone a substep later: the release is due, and the row is empty after");
    ut_check(mp_scene_owed_step(&owed, 0u, 12u, false, true) == MP_SCENE_OWED_NOTHING,
             "and is not due a second time");

    ut_section("an actor that stays owes nothing in the end");
    mp_scene_owed_note(&owed, 38u, 0x9000u, 100u);
    ut_check(mp_scene_owed_step(&owed, 0u, 99u + MP_SCENE_OWED_SUBSTEPS, true, true) ==
                 MP_SCENE_OWED_WAITS,
             "alive a substep short of two seconds: still kept");
    ut_check(mp_scene_owed_step(&owed, 0u, 100u + MP_SCENE_OWED_SUBSTEPS, true, true) ==
                     MP_SCENE_OWED_FORGOTTEN &&
                 !owed.row[0].have && owed.forgotten == 1u && owed.due == 1u,
             "alive at two seconds: forgotten, because its release was its own to repeat");

    ut_section("with no scene of the host's standing there is nothing to give back");
    mp_scene_owed_note(&owed, 38u, 0x9000u, 300u);
    ut_check(mp_scene_owed_step(&owed, 0u, 301u, false, false) == MP_SCENE_OWED_FORGOTTEN &&
                 owed.due == 1u && owed.forgotten == 2u,
             "the actor gone and the scene over: forgotten, not due");

    ut_section("the same actor refused again is stamped again, and four are kept");
    memset(&owed, 0, sizeof owed);
    mp_scene_owed_note(&owed, 1u, 0x1000u, 10u);
    mp_scene_owed_note(&owed, 1u, 0x1000u, 50u);
    ut_check(owed.noted == 1u && owed.row[0].at == 50u && !owed.row[1].have,
             "one row, with the later substep");
    for (row = 1u; row < MP_SCENE_OWED_ROWS; ++row) {
        mp_scene_owed_note(&owed, (uint32_t)(row + 1u), 0x1000u * (row + 1u), 60u);
    }
    mp_scene_owed_note(&owed, 99u, 0x99000u, 61u);
    ut_check(owed.noted == MP_SCENE_OWED_ROWS && owed.left_out == 1u,
             "a fifth has no room and is counted");
    ut_check(mp_scene_owed_step(&owed, 1u, 62u, false, true) == MP_SCENE_OWED_DUE,
             "each row is looked at by itself");
    mp_scene_owed_note(&owed, 99u, 0x99000u, 63u);
    ut_check(owed.row[1].have && owed.row[1].key == 99u, "and a row that emptied is taken again");

    ut_section("an actor refused again a run later gives back on every run and is owed nothing");
    ut_check(!mp_scene_owed_again(0u, 500u), "never refused before: not again");
    ut_check(!mp_scene_owed_again(500u + 1u, 500u),
             "the second and third call of one end fall in the substep of the first: one refusal");
    ut_check(mp_scene_owed_again(500u + 1u, 501u), "a substep later: again");
    ut_check(mp_scene_owed_again(500u + 1u, 500u + MP_SCENE_OWED_SUBSTEPS) &&
                 !mp_scene_owed_again(500u + 1u, 501u + MP_SCENE_OWED_SUBSTEPS),
             "two seconds later still again, a substep past that a first refusal once more");
    ut_check(!mp_scene_owed_again(900u, 10u),
             "a refusal stamped after now, by a counter started over, is not again");
    memset(&owed, 0, sizeof owed);
    mp_scene_owed_note(&owed, 90u, 0x2000u, 500u);
    mp_scene_owed_note(&owed, 172u, 0x3000u, 500u);
    mp_scene_owed_drop(&owed, 90u);
    ut_check(!owed.row[0].have && owed.row[1].have && owed.habitual == 1u,
             "its row is given up and counted, and the other actor's stays");
    ut_check(mp_scene_owed_step(&owed, 0u, 501u, false, true) == MP_SCENE_OWED_NOTHING &&
                 mp_scene_owed_step(&owed, 1u, 501u, false, true) == MP_SCENE_OWED_DUE,
             "so its going away frees nobody, and the lift's releaser beside it still does");
    mp_scene_owed_drop(&owed, 90u);
    mp_scene_owed_drop(NULL, 90u);
    ut_check(owed.habitual == 1u, "a placement with no row, and no table: nothing given up");

    ut_section("the one exit, and nothing to ask");
    mp_scene_owed_clear(&owed);
    for (row = 0u; row < MP_SCENE_OWED_ROWS; ++row) {
        ut_check(mp_scene_owed_step(&owed, row, 70u, false, true) == MP_SCENE_OWED_NOTHING,
                 "nothing is owed after it");
    }
    mp_scene_owed_note(&owed, 5u, 0u, 70u);
    mp_scene_owed_note(NULL, 5u, 0x1000u, 70u);
    mp_scene_owed_clear(NULL);
    ut_check(!owed.row[0].have &&
                 mp_scene_owed_step(&owed, MP_SCENE_OWED_ROWS, 70u, false, true) ==
                     MP_SCENE_OWED_NOTHING &&
                 mp_scene_owed_step(NULL, 0u, 70u, false, true) == MP_SCENE_OWED_NOTHING,
             "no actor is not kept, and a row past the last or no table owes nothing");
}

int main(void)
{
    check_the_run();
    check_the_marks();
    check_the_doors_of_a_run();
    check_the_latch();
    check_a_foreign_lock();
    check_a_placement_written_down();
    check_the_waking();
    check_a_release_owed();
    return ut_summary("whose a script's doors are");
}
