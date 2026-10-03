/* unittests/mp_voice_rule.c: whether this machine sees and hears a spoken line, and what the
 * engine answered when it was handed one.
 *
 * The earshot tests. What cannot be measured is not shown. A line is presented within four full
 * volume radii of a voice, sixteen units, and a host keeps a line alive within the engine's
 * admission of a hundred and no farther; the checks here hold that nobody keeps a line alive
 * past the admission.
 *
 * The engine's answer is read against a bank of twelve channels, the number the engine has, so
 * that "every channel busy" is asked of the size a real call sees.
 *
 * SIZE NOTE: over 600 lines. One pure module and every edge of its two answers, the verdict and the
 * engine's; the next seam is the engine's answer, whose checks share nothing with the verdict's.
 */
#include "unittest.h"

#include "mp_voice_rule.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

/* The engine's own numbers, as its immediates carry them: the admission 100.0f, the full volume
 * radius 4.0f outside a scene's lock and 8.0f under it, and the lock level 5. */
#define ADMIT_BITS      0x42C80000u
#define ADMIT           100.0f
#define MIN_FREE_BITS   0x40800000u
#define MIN_SCENE_BITS  0x41000000u
#define LOCK_LEVEL      5
#define HEAR_FREE       16.0f
#define HEAR_SCENE      32.0f
#define CHANNELS        12u

/* A NaN without a header: infinity less infinity. */
static float not_a_number(void)
{
    return (float)(1.0 / 0.0) - (float)(1.0 / 0.0);
}

static float infinity(void)
{
    const uint32_t bits = 0x7F800000u;
    float          value;

    memcpy(&value, &bits, sizeof value);
    return value;
}

static void ask(mp_voice_question_t *q, const float source[3], const float body[3], bool host)
{
    memset(q, 0, sizeof *q);
    q->hear_free   = HEAR_FREE;
    q->hear_scene  = HEAR_SCENE;
    q->admit       = ADMIT;
    q->keeps_alive = host;
    if (source != NULL) {
        q->source_known = true;
        memcpy(q->source, source, sizeof q->source);
    }
    if (body != NULL) {
        q->body_known = true;
        memcpy(q->body, body, sizeof q->body);
    }
}

static mp_voice_verdict_t verdict_of(const float source[3], const float body[3], bool host)
{
    mp_voice_question_t q;

    ask(&q, source, body, host);
    return mp_voice_judge(&q).verdict;
}

/* ==============================================================================================
 * The hearing radius.
 * ============================================================================================ */

static void check_the_radius_is_read(void)
{
    mp_voice_hearing_t h;
    bool               as_given = true;

    ut_section("the hearing radius is four full volume radii of the voice, read out of its code");

    ut_check(mp_voice_hear_from(MIN_FREE_BITS, MIN_SCENE_BITS, LOCK_LEVEL, LOCK_LEVEL,
                                MP_VOICE_HEAR_FACTOR_DEFAULT, ADMIT, &h),
             "4.0 and 8.0 at lock level 5 make a hearing");
    ut_check(h.free == HEAR_FREE && h.scene == HEAR_SCENE,
             "sixteen units outside a scene's lock and thirty two under it");
    ut_check(h.admit == ADMIT && h.lock == LOCK_LEVEL && h.min_free == 4.0f && h.min_scene == 8.0f,
             "and it carries the admission, the lock level and the two radii it was made of");
    ut_near(20.0 * log10((double)(h.free / h.min_free)), 12.04, 0.01,
            "the default is a quarter of the amplitude, 12.04 dB down");

    ut_section("and refuses what is not the voice it was written for");

    ut_check(!mp_voice_hear_from(MIN_FREE_BITS, MIN_SCENE_BITS, 4, LOCK_LEVEL, 4.0f, ADMIT, &h),
             "a lock level other than the scenes' is refused");
    ut_check(!mp_voice_hear_from(MIN_SCENE_BITS, MIN_FREE_BITS, LOCK_LEVEL, LOCK_LEVEL, 4.0f, ADMIT,
                                 &h),
             "a scene's radius smaller than the free one is refused");
    ut_check(!mp_voice_hear_from(0x7FC00000u, MIN_SCENE_BITS, LOCK_LEVEL, LOCK_LEVEL, 4.0f, ADMIT,
                                 &h),
             "a radius that is not a number is refused");
    ut_check(!mp_voice_hear_from(0u, MIN_SCENE_BITS, LOCK_LEVEL, LOCK_LEVEL, 4.0f, ADMIT, &h),
             "a radius of nought is refused");
    ut_check(!mp_voice_hear_from(MIN_FREE_BITS, 0x7F800000u, LOCK_LEVEL, LOCK_LEVEL, 4.0f, ADMIT,
                                 &h),
             "an infinite one is refused");
    ut_check(!mp_voice_hear_from(MIN_FREE_BITS, MIN_SCENE_BITS, LOCK_LEVEL, LOCK_LEVEL, 0.5f, ADMIT,
                                 &h),
             "a factor below one full volume radius is refused");
    ut_check(mp_voice_hear_from(MIN_FREE_BITS, MIN_SCENE_BITS, LOCK_LEVEL, LOCK_LEVEL, 20.0f, ADMIT,
                                &h) && h.free == 80.0f && h.scene == ADMIT,
             "a radius past the admission stops at it");

    ut_section("the ini's factor: a number in range is taken, anything else is the default");

    ut_check(mp_voice_hear_factor(4.0f, &as_given) == 4.0f && as_given, "4 is taken as given");
    ut_check(mp_voice_hear_factor(2.5f, &as_given) == 2.5f && as_given, "and so is 2.5");
    ut_check(mp_voice_hear_factor(MP_VOICE_HEAR_FACTOR_MIN, &as_given) ==
                     MP_VOICE_HEAR_FACTOR_MIN && as_given &&
                 mp_voice_hear_factor(MP_VOICE_HEAR_FACTOR_MAX, &as_given) ==
                     MP_VOICE_HEAR_FACTOR_MAX && as_given,
             "and so are both ends of the range");
    ut_check(mp_voice_hear_factor(not_a_number(), &as_given) == MP_VOICE_HEAR_FACTOR_DEFAULT &&
                 !as_given,
             "a NaN becomes the default");
    ut_check(mp_voice_hear_factor(infinity(), &as_given) == MP_VOICE_HEAR_FACTOR_DEFAULT &&
                 !as_given,
             "and so does an infinity");
    ut_check(mp_voice_hear_factor(0.0f, &as_given) == MP_VOICE_HEAR_FACTOR_DEFAULT && !as_given,
             "nought is no factor in the range and becomes the default, not the minimum");
    ut_check(mp_voice_hear_factor(0.99f, &as_given) == MP_VOICE_HEAR_FACTOR_DEFAULT && !as_given,
             "and so does a factor just under it");
    ut_check(mp_voice_hear_factor(40.0f, &as_given) == MP_VOICE_HEAR_FACTOR_DEFAULT && !as_given,
             "and forty, past the maximum, becomes the default as well");
    ut_check(MP_VOICE_HEAR_FACTOR_MAX * 8.0f == ADMIT,
             "the maximum puts the scene's radius at the admission and no farther");

    ut_section("the ini's text: what is no number there is the default, as the ini says");

    ut_check(mp_voice_hear_number(MP_VOICE_HEAR_FACTOR_DEFAULT_TEXT) ==
                 MP_VOICE_HEAR_FACTOR_DEFAULT,
             "the text an absent key reads as is the default");
    ut_check(mp_voice_hear_number("2.5") == 2.5f && mp_voice_hear_number("1e1") == 10.0f,
             "a number reads as itself");
    ut_check(mp_voice_hear_number(" 8 ") == 8.0f, "with blanks around it");
    ut_check(isnan(mp_voice_hear_number("four")), "a word is no number, not nought");
    ut_check(isnan(mp_voice_hear_number("")), "nor is an empty value");
    ut_check(isnan(mp_voice_hear_number("4.0x")), "nor a number with a word behind it");
    ut_check(isnan(mp_voice_hear_number(NULL)), "nor no text at all");
    ut_check(mp_voice_hear_factor(mp_voice_hear_number("four"), &as_given) ==
                     MP_VOICE_HEAR_FACTOR_DEFAULT && !as_given,
             "so a word in the ini is read as 4.0, with the warning that says so");
}

/* ==============================================================================================
 * The edges of the radius, and who keeps a line alive.
 * ============================================================================================ */

static void check_the_edges_of_the_radius(void)
{
    const float where[3]    = { 100.0f, 200.0f, 30.0f };
    const float at_free[3]  = { 100.0f, 200.0f + HEAR_FREE, 30.0f };
    const float past[3]     = { 100.0f, 200.0f + HEAR_FREE + 0.01f, 30.0f };
    const float diagonal[3] = { 100.0f + 9.0f, 200.0f + 12.0f, 30.0f };   /* 15 away */
    const float corner[3]   = { 100.0f + 10.0f, 200.0f + 13.0f, 30.0f };  /* 16.4 away */
    const float in_lock[3]  = { 100.0f, 200.0f + HEAR_SCENE, 30.0f };
    const float at_admit[3] = { 100.0f, 200.0f + ADMIT, 30.0f };
    const float beyond[3]   = { 100.0f, 200.0f + ADMIT + 0.5f, 30.0f };
    mp_voice_question_t q;
    mp_voice_answer_t   a;

    ut_section("a line is presented within the hearing radius of this body");

    ut_check(verdict_of(where, where, true) == MP_VOICE_PRESENTED,
             "standing on the speaker is presented");
    ut_check(verdict_of(where, at_free, false) == MP_VOICE_PRESENTED,
             "exactly at sixteen units is presented");
    ut_check(verdict_of(where, diagonal, false) == MP_VOICE_PRESENTED &&
                 verdict_of(where, corner, false) == MP_VOICE_WITHHELD,
             "and the radius is a sphere, not a box: ten and thirteen units off is past it");
    ut_check(verdict_of(where, past, false) == MP_VOICE_WITHHELD,
             "a hundredth of a unit past it a client does not see or hear the line");

    ask(&q, where, in_lock, false);
    a = mp_voice_judge(&q);
    ut_check(a.verdict == MP_VOICE_WITHHELD && a.hear == HEAR_FREE,
             "thirty two units away is past the free radius");
    q.lock_at_scene = true;
    a = mp_voice_judge(&q);
    ut_check(a.verdict == MP_VOICE_PRESENTED && a.hear == HEAR_SCENE,
             "and within it while this machine's lock stands at the scene's level");

    ut_section("a host keeps a line alive within the admission, and nobody else does");

    ut_check(verdict_of(where, past, true) == MP_VOICE_SILENT,
             "a host past the radius keeps it alive at no volume");
    ut_check(verdict_of(where, at_admit, true) == MP_VOICE_SILENT,
             "exactly at a hundred units a host still keeps it alive");
    ut_check(verdict_of(where, beyond, true) == MP_VOICE_WITHHELD,
             "half a unit past the admission nobody is near, and the host withholds it");
    ut_check(verdict_of(where, past, false) == MP_VOICE_WITHHELD &&
                 verdict_of(where, at_admit, false) == MP_VOICE_WITHHELD,
             "a client keeps nothing alive: its scripts are not the world's pace");

    ask(&q, where, past, false);
    a = mp_voice_judge(&q);
    ut_near(a.distance, HEAR_FREE + 0.01, 0.001, "the answer carries this body's distance");
    ut_check(!a.unknown && !a.by_scene && !a.of_scene, "a measured line is neither unknown nor "
             "the scene's");

    ut_section("a radius of nought hears only the speaker's own spot");

    ask(&q, where, where, false);
    q.hear_free = 0.0f;
    ut_check(mp_voice_judge(&q).verdict == MP_VOICE_PRESENTED, "on the spot is presented");
    ask(&q, where, at_free, false);
    q.hear_free = 0.0f;
    ut_check(mp_voice_judge(&q).verdict == MP_VOICE_WITHHELD, "anywhere else is not");
}

static void check_what_cannot_be_measured(void)
{
    const float where[3] = { 100.0f, 200.0f, 30.0f };
    const float near[3]  = { 100.0f, 210.0f, 30.0f };
    float       nan_body[3];
    mp_voice_question_t q;
    mp_voice_answer_t   a;

    ut_section("what cannot be measured is not shown, and is counted");

    ask(&q, NULL, near, false);
    a = mp_voice_judge(&q);
    ut_check(a.verdict == MP_VOICE_WITHHELD && a.unknown,
             "a line with no place is withheld and said to be unknown");

    ask(&q, where, NULL, false);
    a = mp_voice_judge(&q);
    ut_check(a.verdict == MP_VOICE_WITHHELD && a.unknown,
             "a line is withheld when this machine's own body is not known");
    ut_check(a.distance < 0.0f, "and no distance is claimed for it");

    memcpy(nan_body, near, sizeof nan_body);
    nan_body[0] = not_a_number();
    ask(&q, where, nan_body, false);
    a = mp_voice_judge(&q);
    ut_check(a.verdict == MP_VOICE_WITHHELD && a.unknown,
             "a distance that is not a number withholds the line rather than playing it");

    ask(&q, where, NULL, true);
    q.others = 1u;
    memcpy(q.other[0], near, sizeof q.other[0]);
    a = mp_voice_judge(&q);
    ut_check(a.verdict == MP_VOICE_SILENT && a.unknown,
             "a host that cannot read its own body keeps a line alive for a far player near it");

    ask(&q, NULL, near, true);
    q.others = 1u;
    memcpy(q.other[0], where, sizeof q.other[0]);
    ut_check(mp_voice_judge(&q).verdict == MP_VOICE_WITHHELD,
             "a line with no place is near nobody, however near a far player stands to anything");
}

static void check_the_far_players(void)
{
    const float where[3]   = { 0.0f, 0.0f, 0.0f };
    const float far_off[3] = { 1000.0f, 0.0f, 0.0f };
    const float edge[3]    = { ADMIT, 0.0f, 0.0f };
    const float outside[3] = { ADMIT + 0.5f, 0.0f, 0.0f };
    mp_voice_question_t q;

    ut_section("a host keeps a line alive while any player stands within the admission");

    ask(&q, where, far_off, true);
    q.others = 2u;
    memcpy(q.other[0], outside, sizeof q.other[0]);
    memcpy(q.other[1], edge, sizeof q.other[1]);
    ut_check(mp_voice_judge(&q).verdict == MP_VOICE_SILENT,
             "the second far player at exactly a hundred units is enough");

    q.others = 1u;
    ut_check(mp_voice_judge(&q).verdict == MP_VOICE_WITHHELD,
             "only the rows the question counts are read");

    ask(&q, where, far_off, false);
    q.others = 1u;
    memcpy(q.other[0], edge, sizeof q.other[0]);
    ut_check(mp_voice_judge(&q).verdict == MP_VOICE_WITHHELD,
             "a client keeps nothing alive for a far player either");

    ask(&q, where, far_off, true);
    q.others = 1u;
    q.other[0][0] = not_a_number();
    ut_check(mp_voice_judge(&q).verdict == MP_VOICE_WITHHELD,
             "a far player whose place is not a number is near nothing");
}

/* A scene of the host's stands; `speaker` says whether the script that speaks is a run of the
 * host's, `own` whether the scene is this machine's player's own. */
static void in_a_scene(mp_voice_question_t *q, bool speaker, bool own)
{
    q->scene_for_all = true;
    q->scene_speaker = speaker;
    q->gathered      = own;
    q->lock_at_scene = true;
}

static void check_the_scene(void)
{
    const float where[3]   = { 0.0f, 0.0f, 0.0f };
    const float far_off[3] = { 5000.0f, 0.0f, 0.0f };
    const float beside[3]  = { 10.0f, 0.0f, 0.0f };
    const float listens[3] = { 0.0f, 20.0f, 0.0f };             /* inside the scene's radius */
    mp_voice_question_t q;
    mp_voice_answer_t   a;

    ut_section("a scene of the host's presents every line a run of the host's speaks");

    ask(&q, where, far_off, true);
    in_a_scene(&q, true, true);
    a = mp_voice_judge(&q);
    ut_check(a.verdict == MP_VOICE_PRESENTED && a.by_scene && a.of_scene && !a.beside,
             "a line a run of the host's speaks is the scene's, however far this body stands");

    ask(&q, NULL, where, true);
    in_a_scene(&q, true, true);
    a = mp_voice_judge(&q);
    ut_check(a.verdict == MP_VOICE_PRESENTED && a.by_scene && a.unknown,
             "and so is one with no place: the scene's line is the scene's by its speaker");

    ask(&q, where, NULL, true);
    in_a_scene(&q, true, true);
    a = mp_voice_judge(&q);
    ut_check(a.verdict == MP_VOICE_PRESENTED && a.by_scene && a.unknown,
             "and one spoken while this body cannot be measured");

    ut_section("a line a far player's run speaks while the scene stands is judged by the radius");

    ask(&q, where, far_off, true);
    in_a_scene(&q, false, true);
    a = mp_voice_judge(&q);
    ut_check(a.verdict == MP_VOICE_WITHHELD && !a.by_scene && !a.of_scene && a.beside,
             "far from this body and from everybody it is withheld, as outside a scene");

    ask(&q, where, far_off, true);
    in_a_scene(&q, false, true);
    q.others = 1u;
    memcpy(q.other[0], beside, sizeof q.other[0]);
    a = mp_voice_judge(&q);
    ut_check(a.verdict == MP_VOICE_SILENT && a.beside,
             "with the far player it is spoken to beside it, the host keeps it alive at no "
             "volume");

    ask(&q, where, listens, true);
    in_a_scene(&q, false, true);
    a = mp_voice_judge(&q);
    ut_check(a.verdict == MP_VOICE_PRESENTED && !a.by_scene && a.beside && a.hear == HEAR_SCENE,
             "and this body near it presents it by the radius, the scene's under the scene's "
             "lock, not by the scene");

    ask(&q, NULL, where, true);
    in_a_scene(&q, false, true);
    a = mp_voice_judge(&q);
    ut_check(a.verdict == MP_VOICE_WITHHELD && !a.of_scene && a.beside,
             "a far player's line with no place is near nobody");

    ut_section("a player whose own the scene is not judges its lines by the radius");

    ask(&q, where, far_off, false);
    in_a_scene(&q, true, false);
    a = mp_voice_judge(&q);
    ut_check(a.verdict == MP_VOICE_WITHHELD && !a.by_scene && a.of_scene && a.ungathered,
             "a line of the scene far from that player is withheld there");

    ask(&q, where, listens, false);
    in_a_scene(&q, true, false);
    a = mp_voice_judge(&q);
    ut_check(a.verdict == MP_VOICE_PRESENTED && !a.by_scene && a.ungathered,
             "and presented by the scene's radius when that player stands near it anyway");

    ut_section("with no scene standing whose run speaks decides nothing");

    ask(&q, where, far_off, false);
    q.gathered      = true;
    q.scene_speaker = true;
    a = mp_voice_judge(&q);
    ut_check(a.verdict == MP_VOICE_WITHHELD && !a.by_scene && !a.beside && !a.of_scene,
             "a line of a run of the host's far from this body is withheld like any other");
}

static void check_the_place_handed_to_the_engine(void)
{
    mp_voice_voice_t v;
    float            eye[3] = { 1.0f, 2.0f, 3.0f };
    float            out[3];

    ut_section("a presented line is admitted by the engine");

    v = mp_voice_voice_for(MP_VOICE_PRESENTED, true, ADMIT - 1.0f, ADMIT);
    ut_check(v.place == MP_VOICE_AT_SOURCE && !v.silent,
             "with the eye inside the admission the line keeps its own place and its volume");
    v = mp_voice_voice_for(MP_VOICE_PRESENTED, true, ADMIT + 1.0f, ADMIT);
    ut_check(v.place == MP_VOICE_AT_EYE && !v.silent,
             "with the eye past the admission it is given the eye's place, once, before the call");
    v = mp_voice_voice_for(MP_VOICE_PRESENTED, true, ADMIT - MP_VOICE_EYE_EDGE * 0.5f, ADMIT);
    ut_check(v.place == MP_VOICE_AT_EYE,
             "within the band where two measures may disagree the eye's place is the safe one");
    v = mp_voice_voice_for(MP_VOICE_PRESENTED, false, 0.0f, ADMIT);
    ut_check(v.place == MP_VOICE_AT_SOURCE && !v.silent,
             "with the eye unknown the engine is left its own answer");

    ut_section("a line kept alive is admitted at no volume");

    v = mp_voice_voice_for(MP_VOICE_SILENT, true, 5000.0f, ADMIT);
    ut_check(v.place == MP_VOICE_AT_EYE && v.silent, "at the eye's place, with field 0 at nought");
    v = mp_voice_voice_for(MP_VOICE_SILENT, false, 0.0f, ADMIT);
    ut_check(v.silent, "and silent even when the eye cannot be read");

    ut_section("a withheld line is refused by the engine");

    v = mp_voice_voice_for(MP_VOICE_WITHHELD, true, ADMIT + 1.0f, ADMIT);
    ut_check(v.place == MP_VOICE_AT_SOURCE && !v.silent,
             "with the eye past the admission the engine refuses it by itself");
    v = mp_voice_voice_for(MP_VOICE_WITHHELD, true, 10.0f, ADMIT);
    ut_check(v.place == MP_VOICE_BEYOND_REACH,
             "with the eye near a line nobody hears it is put past the admission");
    v = mp_voice_voice_for(MP_VOICE_WITHHELD, true, ADMIT + MP_VOICE_EYE_EDGE * 0.5f, ADMIT);
    ut_check(v.place == MP_VOICE_BEYOND_REACH, "and in the band it is put past it as well");
    v = mp_voice_voice_for(MP_VOICE_WITHHELD, false, 0.0f, ADMIT);
    ut_check(v.place == MP_VOICE_AT_SOURCE && v.silent,
             "with the eye unknown it goes at no volume, so it cannot be heard either way");

    mp_voice_place_beyond(eye, ADMIT, out);
    ut_check(out[0] == eye[0] && out[1] == eye[1], "the place past the admission is above the eye");
    ut_check(out[2] - eye[2] > ADMIT, "and farther from it than the admission");
}

static void check_the_admission_is_read(void)
{
    float reach = 0.0f;

    ut_section("the admission is read out of the voice's own two immediates");

    ut_check(mp_voice_reach_from(ADMIT_BITS, ADMIT_BITS, &reach) && reach == 100.0f,
             "two pushes of 0x42C80000 give 100 units");
    ut_check(!mp_voice_reach_from(ADMIT_BITS, 0x42C00000u, &reach),
             "two numbers for one cell are no admission");
    ut_check(!mp_voice_reach_from(0x7FC00000u, 0x7FC00000u, &reach), "a NaN is none");
    ut_check(!mp_voice_reach_from(0x7F800000u, 0x7F800000u, &reach), "an infinity is none");
    ut_check(!mp_voice_reach_from(0u, 0u, &reach), "nought is none");
    ut_check(!mp_voice_reach_from(0xC2C80000u, 0xC2C80000u, &reach), "a negative is none");
}

static void check_the_verdict_held(void)
{
    mp_voice_held_t held;

    ut_section("one verdict a line, taken at the engine's own edge");

    memset(&held, 0, sizeof held);
    ut_check(mp_voice_adopt(&held, 7, true, MP_VOICE_WITHHELD), "a line started is judged");
    ut_check(!mp_voice_adopt(&held, 7, false, MP_VOICE_PRESENTED),
             "the same line held on from tick to tick keeps its verdict");
    ut_check(held.verdict == MP_VOICE_WITHHELD, "and the verdict is the first one");
    ut_check(mp_voice_adopt(&held, 7, true, MP_VOICE_PRESENTED),
             "the same line started again is judged again");
    ut_check(mp_voice_adopt(&held, 8, false, MP_VOICE_SILENT),
             "a new line without a start is judged, because its subtitle is new");

    ut_section("the subtitle on show is held back for exactly that line");

    ut_check(mp_voice_hides_subtitle(&held, true, 8), "a silent line's subtitle is held back");
    ut_check(!mp_voice_hides_subtitle(&held, true, 9), "another line on show is not touched");
    ut_check(mp_voice_hides_subtitle(&held, false, 0),
             "with the line on show unknown the held verdict answers");
    (void)mp_voice_adopt(&held, 9, true, MP_VOICE_PRESENTED);
    ut_check(!mp_voice_hides_subtitle(&held, true, 9), "a presented line is shown");

    ut_section("the one exit");

    (void)mp_voice_adopt(&held, 10, true, MP_VOICE_WITHHELD);
    mp_voice_forget(&held);
    ut_check(!mp_voice_hides_subtitle(&held, true, 10), "a forgotten verdict holds nothing back");
    ut_check(mp_voice_adopt(&held, 10, false, MP_VOICE_PRESENTED),
             "and the same line after the block closed is judged anew");
}

/* ==============================================================================================
 * The engine's answer, against a bank of twelve.
 * ============================================================================================ */

#define LINE 3219

/* A call that reached the voice and was refused, with every channel busy at priority 90. */
static void a_full_bank(mp_voice_call_t *c, uint32_t priority)
{
    size_t index;

    memset(c, 0, sizeof *c);
    c->asked        = true;
    c->voices_known = true;
    c->voices_on    = true;
    c->latch_known  = true;
    c->latch_before = LINE - 1;
    c->latch_after  = LINE;
    c->line         = LINE;
    c->handle_known = true;
    c->handle       = -1;
    c->priority_known = true;
    c->priority     = priority;
    c->free_known   = true;
    c->free_before  = 0u;
    c->channels     = CHANNELS;
    for (index = 0; index < CHANNELS; ++index) {
        c->channel[index].busy     = true;
        c->channel[index].playing  = true;
        c->channel[index].priority = 90u;
    }
}

static void check_the_engine_answer(void)
{
    mp_voice_call_t         c;
    mp_voice_heard_answer_t a;

    ut_section("the engine's answer to a line, in the engine's own order");

    a_full_bank(&c, 90u);
    c.asked = false;
    ut_check(mp_voice_heard_of(&c).heard == MP_VOICE_NOT_ASKED,
             "a line that did not start was never asked for a voice");
    ut_check(mp_voice_heard_of(NULL).heard == MP_VOICE_NOT_ASKED, "nor is no call at all");

    a_full_bank(&c, 90u);
    c.voices_on = false;
    ut_check(mp_voice_heard_of(&c).heard == MP_VOICE_VOICES_OFF,
             "with voices off the engine leaves before anything else");

    a_full_bank(&c, 90u);
    c.latch_before = LINE;
    ut_check(mp_voice_heard_of(&c).heard == MP_VOICE_LATCHED,
             "the same line still latched is refused before the voice");

    a_full_bank(&c, 90u);
    c.handle = 7;
    a = mp_voice_heard_of(&c);
    ut_check(a.heard == MP_VOICE_VOICED && a.channel == 7,
             "a handle names the channel it voiced on");
    ut_check(a.stole, "and with no channel free before, a lower sound gave its channel up");
    c.free_before = 3u;
    ut_check(!mp_voice_heard_of(&c).stole, "with a free channel nothing was taken");

    a_full_bank(&c, 90u);
    a = mp_voice_heard_of(&c);
    ut_check(a.heard == MP_VOICE_NO_CHANNEL,
             "twelve busy channels at ninety refuse a voice of ninety: stealing is strictly lower");
    a_full_bank(&c, 101u);
    ut_check(mp_voice_heard_of(&c).heard != MP_VOICE_NO_CHANNEL,
             "the same bank does not refuse a voice of a hundred and one for a channel");
    a_full_bank(&c, 101u);
    c.priority_known = false;
    ut_check(mp_voice_heard_of(&c).heard == MP_VOICE_NO_CHANNEL,
             "with the voice's own priority unknown, a bank with no free channel is all it says");
    a_full_bank(&c, 90u);
    c.channel[11].busy = false;
    ut_check(mp_voice_heard_of(&c).heard != MP_VOICE_NO_CHANNEL,
             "one free channel of twelve is no want of a channel");
    a_full_bank(&c, 90u);
    c.channel[5].priority = 50u;
    ut_check(mp_voice_heard_of(&c).heard != MP_VOICE_NO_CHANNEL,
             "and neither is one busy with an ambience of fifty");

    a_full_bank(&c, 90u);
    c.channel[4].busy     = false;
    c.channel[9].of_line  = true;
    a = mp_voice_heard_of(&c);
    ut_check(a.heard == MP_VOICE_WAV_HELD && a.channel == 9,
             "a refusal with a channel free while an older voice of a line plays names that voice");
    c.channel[9].busy = false;
    ut_check(mp_voice_heard_of(&c).heard == MP_VOICE_REFUSED_OTHER,
             "and with no older voice playing it is some other refusal");

    a_full_bank(&c, 90u);
    c.channel[4].busy = false;
    c.latch_after     = LINE - 1;
    ut_check(mp_voice_heard_of(&c).heard == MP_VOICE_REFUSED_OTHER,
             "a latch the call did not move says the engine left before it: no record");

    a_full_bank(&c, 90u);
    c.channels = 0u;
    ut_check(mp_voice_heard_of(&c).heard == MP_VOICE_REFUSED_OTHER,
             "a bank that could not be read claims no cause");
}

static void check_how_a_voice_ends(void)
{
    mp_voice_channel_t ch;

    ut_section("how a line's voice ended, told from its handle and its channel");

    memset(&ch, 0, sizeof ch);
    ch.busy    = true;
    ch.playing = true;
    ch.of_line = true;
    ut_check(mp_voice_end_of(true, 3, true, &ch, true) == MP_VOICE_END_PLAYING,
             "a handle that names a channel is still playing");
    ut_check(mp_voice_end_of(true, -1, true, &ch, true) == MP_VOICE_END_TAKEN,
             "a handle gone while its channel plays on with the block open was taken by an older "
             "voice's end");
    ut_check(mp_voice_end_of(true, -1, true, &ch, false) == MP_VOICE_END_CLOSED,
             "with the block shut it was the close that let it go");
    ch.busy = false;
    ut_check(mp_voice_end_of(true, -1, true, &ch, true) == MP_VOICE_END_OWN,
             "a freed channel ended at its own end");
    ch.busy    = true;
    ch.of_line = false;
    ut_check(mp_voice_end_of(true, -1, true, &ch, true) == MP_VOICE_END_OWN,
             "and so did one another sound took over since");
    ut_check(mp_voice_end_of(false, 0, false, NULL, true) == MP_VOICE_END_OWN,
             "an unreadable handle claims nothing else");
}

int main(void)
{
    check_the_radius_is_read();
    check_the_edges_of_the_radius();
    check_what_cannot_be_measured();
    check_the_far_players();
    check_the_scene();
    check_the_place_handed_to_the_engine();
    check_the_admission_is_read();
    check_the_verdict_held();
    check_the_engine_answer();
    check_how_a_voice_ends();
    return ut_summary("mp_voice_rule");
}
