/* The three decisions the lobby's engine side makes on its own, and the way the module behaves in
 * a process with no game in it.
 *
 * The decisions are worth pinning because each one fails quietly in play. The order inside the
 * wish step decides whether a request that becomes possible on its last frame is carried out or
 * thrown away. The hero clamp stands between a number that arrived over the wire and a table the
 * engine indexes raw, where one entry past the end ends the process. And the pose check is the
 * only thing between a value another machine computed and a teleport that writes whatever it is
 * handed.
 *
 * The rest of the file drives the entry points with nothing resolved, which is the same state an
 * unsupported executable produces: every one of them has to answer with a counted refusal rather
 * than a call through a null pointer.
 */
#include "unittest.h"

#include "mp_lobby.h"
#include "mp_signatures.h"
#include "mp_start.h"
#include "mp_start_phase.h"

#include "common/signature.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static void check_the_wish_step(void)
{
    ut_section("what one tick does with one wish");

    ut_check(mp_start_wish_step(false, false, 0u) == MP_START_WISH_IDLE,
             "nothing is wanted, so nothing is decided");
    ut_check(mp_start_wish_step(false, true, MP_START_WISH_DEADLINE_FRAMES + 1u) ==
                 MP_START_WISH_IDLE,
             "and an open gate does not invent a wish that was never made");

    ut_check(mp_start_wish_step(true, false, 0u) == MP_START_WISH_WAIT,
             "a fresh wish against a shut gate waits");
    ut_check(mp_start_wish_step(true, false, MP_START_WISH_DEADLINE_FRAMES - 1u) ==
                 MP_START_WISH_WAIT,
             "and it goes on waiting up to the frame before the deadline");

    ut_check(mp_start_wish_step(true, true, 0u) == MP_START_WISH_RUN,
             "an open gate carries the wish out");

    ut_check(mp_start_wish_step(true, false, MP_START_WISH_DEADLINE_FRAMES) == MP_START_WISH_DROP,
             "the deadline frame itself is where a wish is dropped");
    ut_check(mp_start_wish_step(true, false, 0xFFFFFFFFu) == MP_START_WISH_DROP,
             "and it stays dropped however long it has stood");

    ut_section("the order of the two questions, which is the decision");
    ut_check(mp_start_wish_step(true, true, MP_START_WISH_DEADLINE_FRAMES) == MP_START_WISH_RUN,
             "a wish that becomes possible on the very frame it runs out is carried out, not "
             "thrown away");
    ut_check(mp_start_wish_step(true, true, 0xFFFFFFFFu) == MP_START_WISH_RUN,
             "and the gate beats the deadline however far past it the wish is");
}

static void check_the_hero_clamp(void)
{
    unsigned index;

    ut_section("the hero index, which the engine does not clamp itself");

    for (index = 0u; index <= MP_LOBBY_HERO_MAX; ++index) {
        ut_checkf(mp_start_clamp_hero((uint8_t)index) == (uint8_t)index,
                  "hero %u is one the game ships and passes through untouched", index);
    }

    ut_check(mp_start_clamp_hero((uint8_t)(MP_LOBBY_HERO_MAX + 1u)) == (uint8_t)MP_LOBBY_HERO_MAX,
             "the first index past the last hero is pulled back onto the last one");
    ut_check(mp_start_clamp_hero(0x7Fu) == (uint8_t)MP_LOBBY_HERO_MAX,
             "and so is a value in the middle of the byte");
    ut_check(mp_start_clamp_hero(0xFFu) == (uint8_t)MP_LOBBY_HERO_MAX,
             "and so is the largest byte there is, which is what a torn wire field looks like");
}

/* A NaN and an infinity, built from their BITS rather than from a division.
 *
 * The division was the readable way to write it and the compiler refuses it: dividing by a
 * variable it can see is zero is a warning, and this tree builds warnings as errors. Writing the
 * bit patterns is not a workaround for the check, it is the more honest test, what these
 * functions have to produce is exactly the value that arrives over a wire in those bytes, and a
 * division only produces it if the compiler and the processor agree about what it should fold to.
 * */
static float from_bits(uint32_t bits)
{
    float value;

    memcpy(&value, &bits, sizeof value);
    return value;
}

static float not_a_number(void)
{
    return from_bits(0x7FC00000u);   /* a quiet NaN */
}

static float infinity(void)
{
    return from_bits(0x7F800000u);
}

static void check_the_pose(void)
{
    float ordinary[3] = { 12.5f, -3.0f, 400.25f };
    float broken[3]   = { 0.0f, 0.0f, 0.0f };
    int   axis;

    ut_section("a pose another machine computed");

    ut_check(mp_start_pose_is_usable(ordinary, 1.5f), "an ordinary point and heading is usable");
    ut_check(mp_start_pose_is_usable(ordinary, 0.0f), "and so is a heading of zero");
    ut_check(!mp_start_pose_is_usable(NULL, 0.0f), "no point at all is not a pose");
    ut_check(!mp_start_pose_is_usable(ordinary, not_a_number()),
             "a heading that is not a number is refused");
    ut_check(!mp_start_pose_is_usable(ordinary, infinity()), "and so is an infinite one");

    for (axis = 0; axis < 3; ++axis) {
        broken[0] = broken[1] = broken[2] = 0.0f;
        broken[axis] = not_a_number();
        ut_checkf(!mp_start_pose_is_usable(broken, 0.0f),
                  "axis %d being not a number is enough to refuse the whole pose", axis);

        broken[axis] = infinity();
        ut_checkf(!mp_start_pose_is_usable(broken, 0.0f),
                  "and so is axis %d being infinite", axis);
    }
}

/* The placement holds one pose, and an arrival and a scene can both want it. The one it used to
 * be was "the last one wins": a client arriving after the host's savegame while a scene gathered
 * took whichever was asked for second. The scene's seat is where every other player is brought. */
static void check_who_owns_the_one_pose(void)
{
    ut_section("a scene's seat goes before an arrival's point");
    ut_check(mp_start_pose_take(false, false, false) == MP_START_POSE_TAKE &&
                 mp_start_pose_take(false, false, true) == MP_START_POSE_TAKE,
             "with nothing held, either is taken");
    ut_check(mp_start_pose_take(true, false, true) == MP_START_POSE_REPLACE_ARRIVAL,
             "a scene's seat replaces an arrival still held, and says so");
    ut_check(mp_start_pose_take(true, true, false) == MP_START_POSE_SET_ASIDE,
             "an arrival while a scene's seat is held is set aside");
    ut_check(mp_start_pose_take(true, true, true) == MP_START_POSE_TAKE,
             "a newer scene's seat replaces an older one, as a newer scene is gathered anew");
    ut_check(mp_start_pose_take(true, false, false) == MP_START_POSE_TAKE,
             "and a second arrival replaces the first, as it always did");
}

/* Nothing has resolved the host image in this process, so every site comes back as zero. That is
 * the same state an unsupported executable produces, and what these entry points owe the caller
 * there is a refusal rather than a call through a null pointer. */
static void check_nothing_runs_without_a_game(void)
{
    float pose[3] = { 1.0f, 2.0f, 3.0f };

    ut_section("a process with no game in it");

    ut_check(!mp_start_install(), "the install refuses, because the focus setter does not resolve");
    ut_check(!mp_start_level_running(), "and no level is running");

    ut_check(!mp_start_shipped(0u), "no shipped level can be started");
    ut_check(!mp_start_by_path("level\\swamp.b3d"), "no path can be started");
    ut_check(!mp_start_save("save\\Zanzi01.sav"), "no savegame can be restored");
    ut_check(!mp_start_place_at(pose, 0.5f), "no player can be placed");
    ut_check(!mp_start_place_for_scene(pose, 0.5f), "not for a scene either");
    ut_check(!mp_start_apply_hero(1u), "and no hero can be applied");

    ut_check(!mp_start_pending(), "a refused request leaves nothing pending");
    ut_check(mp_start_kind() == MP_START_NONE, "and names no kind");

    /* Both of these walk the whole module with nothing resolved. The point is that they turn back
     * rather than dereference, which a refusal alone would not show. */
    mp_start_tick();
    mp_start_report();
    ut_check(!mp_start_pending(), "a tick over an empty module leaves it empty");

    mp_start_cancel();
    ut_check(!mp_start_pending() && mp_start_kind() == MP_START_NONE,
             "and a cancel on an empty module is harmless");

    mp_start_cancel_pose();
    mp_start_tick();
    ut_check(!mp_start_pending(),
             "forgetting a pose nobody is holding writes no counter and starts nothing");
}

/* The site table is a positional array indexed by the enumeration, so a row inserted at one place
 * and an enumerator at another resolve one site under the other's name and nothing complains. The
 * teleport is the row this module brought with it, so it is the row held against its own
 * enumerator here. */
static void check_the_teleport_row(void)
{
    const signature_t *site = mp_signatures_site(MP_SITE_PLAYER_TELEPORT);

    ut_section("the site this module brought with it");

    ut_check(site != NULL, "the teleport enumerator is inside the table");
    if (site == NULL) {
        return;
    }
    ut_check(site->name != NULL && strcmp(site->name, "player_teleport") == 0,
             "and it names the teleport row rather than one of its neighbours");
    ut_check(site->mask != NULL,
             "the row carries its mask, without which every wildcard would have to match "
             "literally and the pattern would find nothing");
    ut_check(site->detour_prologue >= 5u && site->detour_prologue < site->size,
             "and its prologue covers the five bytes a branch overwrites while leaving a tail "
             "for the second stage to recognise");
}

/* The drive must end, and this test does NOT prove the case that was broken. Read the last
 * paragraph before trusting it.
 *
 * The nav hull asks mp_start_pending BEFORE it asks whether the menu key was pressed, and returns
 * on a yes. So a drive that never puts its step down does not merely leak a flag: it takes the
 * multiplayer menu away for the rest of the process. That is what happened after every map
 * started out of the lobby, because step one clears the kind for anything but a savegame and the
 * guard at the top of the drive then returned without touching the step it had left at two.
 *
 * The module refuses to install without the engine, so what can be driven here is the path a
 * request takes when nothing resolved: it must still end, with nothing pending. */
static void check_a_drive_always_ends(void)
{
    int32_t code;
    int     spins;

    ut_section("a drive over a module with nothing armed leaves nothing pending");

    for (spins = 0; spins < 8; ++spins) {
        code = mp_start_drive(1234);
        ut_checkf(code == 1234, "spin %d hands the navigation code back untouched", spins);
    }
    ut_check(!mp_start_pending(),
             "and nothing is left pending");

    /* What this does not reach. The module refuses to install without the engine, so every call
     * above turns back at the first guard, `!start.installed`, and the line that was wrong sits
     * two guards further in. The defect was a step left standing after the kind had been cleared,
     * and that state cannot be built here at all: reaching it needs an accepted start, which
     * needs the engine's own title widgets.
     *
     * It is left in because the shape it checks is still worth holding, a drive over an idle
     * module hands the code back and leaves nothing pending, and because saying plainly that the
     * regression is uncovered is better than a green line that pretends otherwise. */
}

/* The drive's one value, against the two fields it replaced.
 *
 * The reference is the drive as it stood before, the kind and the step, written out once here
 * with the same bounds. Every sequence of up to six events, a map asked for, a savegame asked for,
 * a frame whose focus holds, a frame whose focus is elsewhere, the load screen taking the save and
 * a cancel, is played on both: they must do the same with every frame and agree after every event
 * on whether a start is pending. From wherever a sequence ends, frames alone must then bring it to
 * rest, whether the focus holds or not, which is the defect that once kept the menu shut. */

typedef struct old_drive {
    mp_start_kind_t kind;
    int             step;
    uint32_t        rounds;
    uint32_t        misses;
} old_drive_t;

static void old_cancel(old_drive_t *d)
{
    d->kind   = MP_START_NONE;
    d->step   = 0;
    d->rounds = 0u;
}

static mp_start_action_t old_frame(old_drive_t *d, bool holds)
{
    if (d->kind == MP_START_NONE) {
        d->step = 0;
        return MP_START_ACT_PASS;
    }
    if (d->step == 0) {
        d->step = 1;
        return MP_START_ACT_FOCUS;
    }
    if (d->step == 1) {
        if (!holds) {
            if (++d->misses >= MP_START_FOCUS_MISSES_MAX) {
                old_cancel(d);
                return MP_START_ACT_GIVE_UP;
            }
            return MP_START_ACT_FOCUS;
        }
        d->step = 2;
        if (d->kind != MP_START_SAVE) {
            d->kind = MP_START_NONE;
        }
        return MP_START_ACT_ACCEPT;
    }
    if (++d->rounds >= MP_START_DRIVE_ROUNDS) {
        old_cancel(d);
        return MP_START_ACT_GIVE_UP;
    }
    d->step = 0;
    return MP_START_ACT_PASS;
}

static bool old_pending(const old_drive_t *d)
{
    return d->kind != MP_START_NONE || d->step != 0;
}

enum { EV_ASK_MAP, EV_ASK_SAVE, EV_HOLDS, EV_AWAY, EV_CONSUME, EV_CANCEL, EV_COUNT };

#define SEQUENCE_MAX 6

/* One event on both. False when the two did different things with it. */
static bool play(int event, mp_start_machine_t *m, old_drive_t *d)
{
    switch (event) {
    case EV_ASK_MAP:
    case EV_ASK_SAVE:
        mp_start_machine_ask(m, event == EV_ASK_MAP ? MP_START_SHIPPED : MP_START_SAVE);
        d->kind   = event == EV_ASK_MAP ? MP_START_SHIPPED : MP_START_SAVE;
        d->step   = 0;
        d->rounds = 0u;
        d->misses = 0u;
        return true;
    case EV_HOLDS:
    case EV_AWAY:
        return mp_start_machine_drive(m, event == EV_HOLDS) == old_frame(d, event == EV_HOLDS);
    case EV_CONSUME:
        mp_start_machine_consume(m);
        if (d->kind == MP_START_SAVE) {
            d->kind = MP_START_NONE;
        }
        return true;
    case EV_CANCEL:
    default:
        mp_start_machine_cancel(m);
        old_cancel(d);
        return true;
    }
}

/* Frames alone, all holding or all away, until the start is at rest or the bound runs out. */
static bool comes_to_rest(mp_start_machine_t m, bool holds)
{
    uint32_t frames;
    uint32_t bound = MP_START_FOCUS_MISSES_MAX + 3u * MP_START_DRIVE_ROUNDS + 3u;

    for (frames = 0u; frames < bound && mp_start_machine_pending(&m); ++frames) {
        (void)mp_start_machine_drive(&m, holds);
    }
    return !mp_start_machine_pending(&m);
}

static void check_every_sequence_of_the_drive(void)
{
    int      length;
    uint32_t sequences = 0u;
    uint32_t disagreed = 0u;
    uint32_t stuck     = 0u;
    uint32_t accepts   = 0u;

    ut_section("every sequence of up to six events: the one value does what the two fields did");
    for (length = 0; length <= SEQUENCE_MAX; ++length) {
        uint32_t count = 1u;
        uint32_t code;
        int      k;

        for (k = 0; k < length; ++k) {
            count *= (uint32_t)EV_COUNT;
        }
        for (code = 0u; code < count; ++code) {
            mp_start_machine_t m;
            old_drive_t        d;
            uint32_t           rest = code;
            bool               same = true;

            memset(&m, 0, sizeof m);
            memset(&d, 0, sizeof d);
            for (k = 0; k < length; ++k) {
                int event = (int)(rest % (uint32_t)EV_COUNT);

                rest /= (uint32_t)EV_COUNT;
                if (event == EV_HOLDS && mp_start_machine_wants_focus(&m)) {
                    ++accepts;
                }
                same = same && play(event, &m, &d) &&
                       mp_start_machine_pending(&m) == old_pending(&d) &&
                       m.kind == d.kind;
            }
            ++sequences;
            disagreed += same ? 0u : 1u;
            stuck += (comes_to_rest(m, true) && comes_to_rest(m, false)) ? 0u : 1u;
        }
    }
    ut_checkf(disagreed == 0u,
              "%u sequence(s) of up to six events, %u where the two did different things",
              (unsigned)sequences, (unsigned)disagreed);
    ut_checkf(stuck == 0u, "and from every one frames alone bring the start to rest: %u did not",
              (unsigned)stuck);
    ut_checkf(accepts != 0u, "the sequences reached the accept %u time(s)", (unsigned)accepts);
}

int main(void)
{
    check_the_wish_step();
    check_the_hero_clamp();
    check_the_pose();
    check_who_owns_the_one_pose();
    check_the_teleport_row();
    check_nothing_runs_without_a_game();
    check_a_drive_always_ends();
    check_every_sequence_of_the_drive();
    return ut_summary("mp_start");
}
