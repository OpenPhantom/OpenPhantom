/* session_lock.c: which rows a running multiplayer session takes away, and which it does not.
 *
 * The entity spawner is the one group a session does NOT take when the multiplayer runs the NPC
 * copies in it. One function answers that, and both the panel and the placement mode ask it: a
 * mode that asked only whether a session ran at all refused, in a field run, on both machines of
 * a co-op session with "the session runs no copies" while the multiplayer ran them.
 *
 * The session note is the real one, published into this process, because that is the channel the
 * lock reads and a stub of it would prove nothing about it. Whether the copies run is stubbed: it
 * is the multiplayer's answer, told through the spawner's link, and this file is about what the
 * lock makes of the two together.
 */
#include "unittest.h"

#include "cheats_original.h"
#include "cheats_original_actions.h"
#include "overlay_reason.h"
#include "overlay_model.h"
#include "session_lock.h"

#include "common/session_note.h"
#include "common/shared_note.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

static bool copies_run;

bool npc_spawn_link_active(void)
{
    return copies_run;
}

/* The shipped console's table, standing in for the executable's.
 *
 * cheats_original_name() reads the names out of the running game, so in a test process it
 * answers NULL for every index and the lock cannot be driven at all. That seam is the one
 * thing this program has to stand in for, which is why the real cheats_original.c is not
 * linked here: linking it would put the NULL-answering version back and there would be
 * nothing to test.
 *
 * These are the eleven codes the game has, in the order cheats_original.c writes them down, not
 * invented ones, so the eight the lock leaves alone are eight real ones. That this list and the
 * lock's own three are codes the game actually has is checked where both are reachable, in
 * unittests/overlay_groups.c. */
static const char *const TOGGLES[] = {
    "turntables", "beyond cinema", "slowmo", "perfection", "but i feel so good",
    "60fps", "perf", "naughty naughty", "from above", "happy", "oldcode"
};
#define TOGGLE_COUNT ((uint32_t)(sizeof TOGGLES / sizeof TOGGLES[0]))

/* Where each of the three the lock takes sits in the list above. */
#define TOGGLE_FORCE_PUSH  4u
#define TOGGLE_60FPS       5u
#define TOGGLE_WEAPON_3    9u

/* The three the lock takes, where they sit in the table above and why each is on the list. */
static const struct {
    uint32_t    index;
    const char *why;
} TAKEN_TOGGLES[] = {
    { TOGGLE_WEAPON_3,
      "\"happy\" is taken: a session holds it on the host's value, and it remaps a shot kind" },
    { TOGGLE_FORCE_PUSH,
      "\"but i feel so good\" is taken: the other cell a session holds on the host's value" },
    { TOGGLE_60FPS,
      "\"60fps\" is taken: it writes a sixty-fourth into the frame delta cell while a "
          "session counts in thirty-seconds, so it moves the ladder both machines agree on" }
};
#define TAKEN_TOGGLE_COUNT ((uint32_t)(sizeof TAKEN_TOGGLES / sizeof TAKEN_TOGGLES[0]))

const char *cheats_original_name(uint32_t index)
{
    return index < TOGGLE_COUNT ? TOGGLES[index] : NULL;
}

static void session(bool running)
{
    session_note_t note;

    memset(&note, 0, sizeof note);
    note.running = running;
    note.is_host = running;
    ut_check(session_note_publish(&note), running ? "a session is published as running"
                                                  : "and then as ended");
}

/* A row of a kind the lock can take, as the panel would hand it over. */
static void a_row(overlay_row_t *row)
{
    memset(row, 0, sizeof *row);
    row->kind      = OVERLAY_ROW_ACTION;
    row->available = true;
}

static void the_spawner(void)
{
    overlay_row_t row;

    ut_section("the entity spawner in a session");

    session(true);
    copies_run = true;
    session_lock_refresh();
    ut_check(!session_lock_holds_the_spawner(),
             "a session that runs the copies does not hold the spawner");
    ut_check(!session_lock_holds_group((uint32_t)OVERLAY_GROUP_OPENPHANTOM_SPAWN),
             "so the group writes no sentence under its rows");
    a_row(&row);
    ut_check(!session_lock_take((uint32_t)OVERLAY_GROUP_OPENPHANTOM_SPAWN, 0u, &row) &&
                 row.available,
             "and every row of it stays as the group built it");

    copies_run = false;
    session_lock_refresh();
    ut_check(session_lock_holds_the_spawner(),
             "a session that runs none holds it");
    ut_check(session_lock_holds_group((uint32_t)OVERLAY_GROUP_OPENPHANTOM_SPAWN),
             "the group is held");
    ut_check(session_lock_word((uint32_t)OVERLAY_GROUP_OPENPHANTOM_SPAWN) == NULL,
             "but the sentence under its rows is not the lock's to write: the group says which of "
             "its four states this is, out of the rule its rows, the placement mode and the log "
             "all read, and a second sentence here would be one state spelled twice a few rows "
             "apart");
    a_row(&row);
    ut_check(session_lock_take((uint32_t)OVERLAY_GROUP_OPENPHANTOM_SPAWN, 0u, &row) &&
                 !row.available,
             "and its rows are taken");
    a_row(&row);
    row.reason = (uint32_t)OVERLAY_REASON_SPAWNER;
    ut_check(session_lock_take((uint32_t)OVERLAY_GROUP_OPENPHANTOM_SPAWN, 0u, &row) &&
                 row.reason == (uint32_t)OVERLAY_REASON_SPAWNER,
             "with the group's own word left on them rather than relabelled `session`, which is "
             "the same rule the three held-back codes get");

    session(false);
    copies_run = false;
    session_lock_refresh();
    ut_check(!session_lock_holds_the_spawner() && !session_lock_running(),
             "with no session at all the spawner is nobody's to hold");
    a_row(&row);
    ut_check(!session_lock_take((uint32_t)OVERLAY_GROUP_OPENPHANTOM_SPAWN, 0u, &row) &&
                 row.available,
             "and the rows are the panel's own again");
}

/* The groups a session takes whatever else is true, so that the exception above stays an
 * exception. */
static void the_other_groups(void)
{
    overlay_row_t row;

    ut_section("the groups a session takes whatever the copies do");

    session(true);
    copies_run = true;
    session_lock_refresh();
    a_row(&row);
    ut_check(session_lock_take((uint32_t)OVERLAY_GROUP_OPENPHANTOM_FRAMERATE, 0u, &row) &&
                 !row.available,
             "the frame rate group is taken even while the copies run");
    a_row(&row);
    ut_check(session_lock_take((uint32_t)OVERLAY_GROUP_ORIGINAL_TOGGLES, TOGGLE_WEAPON_3,
                               &row) &&
                 !row.available,
             "and the first of the console's three toggles");
    a_row(&row);
    ut_check(!session_lock_take((uint32_t)OVERLAY_GROUP_ORIGINAL_TOGGLES, 0u, &row) &&
                 row.available,
             "but not a toggle that costs the session nothing");
    ut_check(strcmp(session_lock_word((uint32_t)OVERLAY_GROUP_OPENPHANTOM_FRAMERATE),
                    SESSION_LOCK_WORD) == 0,
             "and those groups keep the sentence they always wrote");
    session(false);
    session_lock_refresh();
}

/* The placement mode runs while the panel is HIDDEN, so between two pictures nothing has read
 * the note for it and `session_lock_refresh` has not run. `session_lock_holds_the_spawner` takes
 * its own reading for exactly that, and the whole of this section turns on it: not one check below
 * calls `session_lock_refresh` itself, so an answer out of the last picture is a wrong answer
 * here. Without that one line all of this passes on stale cells. */
static void between_two_pictures(void)
{
    ut_section("the mode's own reading, with no picture to ride on");

    session(false);
    copies_run = false;
    session_lock_refresh();   /* the last picture: no session at all */
    session(true);            /* and a session begins before the next one is drawn */
    ut_check(session_lock_holds_the_spawner(),
             "a session begun since the last picture is seen, out of the note itself");
    ut_check(session_lock_running(),
             "and the reading it took is the one every other answer then uses");

    copies_run = true;
    session_lock_refresh();   /* a picture: a session, and the multiplayer runs the copies */
    copies_run = false;       /* and it lets go of them before the next one is drawn */
    ut_check(session_lock_holds_the_spawner(),
             "the multiplayer letting go of the copies since the last picture is seen too");
    copies_run = true;
    ut_check(!session_lock_holds_the_spawner(),
             "and taking them up again, without a picture in between either way");

    session(false);
    copies_run = false;
    session_lock_refresh();
}

/* The shipped console's own rows, by name and by code rather than by number.
 *
 * This is the one place where they can be checked at all: on an executable a test process never
 * has, the console's table resolves nothing, so the panel builds no toggle rows and the walk in
 * unittests/overlay_session.c sees none of them. Taking "60fps" out of the list below leaves
 * every other program green, and that switch writes one sixty-fourth into the frame delta cell
 * while a session counts in thirty-seconds, so the two machines would be on different ladders
 * with nothing said.
 *
 * Checked by NAME because that is how the lock decides: the console's table is the image's and
 * its order is not this file's to assume. */
static void the_console_toggles(void)
{
    overlay_row_t row;
    uint32_t      i;

    ut_section("the shipped console's toggles, by the name each one is written with");
    session(true);
    session_lock_refresh();
    for (i = 0; i < TAKEN_TOGGLE_COUNT; ++i) {
        a_row(&row);
        ut_check(session_lock_take((uint32_t)OVERLAY_GROUP_ORIGINAL_TOGGLES,
                                   TAKEN_TOGGLES[i].index, &row) && !row.available,
                 TAKEN_TOGGLES[i].why);
    }

    /* And every other code the game has is left alone. Walked over the whole eleven rather
     * than sampled: the list is a list, not "every code the console has", and the eight it
     * does not name are eight real ones. */
    {
        uint32_t left_alone = 0u;

        for (i = 0; i < TOGGLE_COUNT; ++i) {
            if (i == TOGGLE_WEAPON_3 || i == TOGGLE_FORCE_PUSH || i == TOGGLE_60FPS) {
                continue;
            }
            a_row(&row);
            if (!session_lock_take((uint32_t)OVERLAY_GROUP_ORIGINAL_TOGGLES, i, &row) &&
                row.available) {
                ++left_alone;
            }
        }
        ut_check(left_alone == TOGGLE_COUNT - TAKEN_TOGGLE_COUNT,
                 "and the other eight codes the game has are left alone, every one of them");
    }

    a_row(&row);
    ut_check(!session_lock_take((uint32_t)OVERLAY_GROUP_ORIGINAL_TOGGLES, TOGGLE_COUNT,
                                &row) && row.available,
             "and an index past the end of the console's table takes nothing");
}

/* The one-shot codes, by the symbol each one is written with. The lock switches over the enum
 * here rather than over a number, so a code inserted into the middle of it cannot silently take
 * another one's place; what this pins is the membership of the list. */
static void the_console_actions(void)
{
    static const cheats_action_id_t TAKEN[] = {
        CHEATS_ACTION_LOWER_DIFFICULTY_A, CHEATS_ACTION_LOWER_DIFFICULTY_B,
        CHEATS_ACTION_INCREASE_DIFFICULTY, CHEATS_ACTION_GRAPHICS_DETAIL,
        CHEATS_ACTION_VIEW_CREDITS
    };
    static const char *const WHY[] = {
        "the first way of lowering the difficulty is taken: a session holds the host's",
        "and the second, which writes the same cell by hand",
        "and raising it, for the same reason",
        "the graphics detail level is taken: it gates what the host's activation scan wakes",
        "and the credits, which write the campaign's own place in the world"
    };
    static const cheats_action_id_t LEFT[] = {
        CHEATS_ACTION_KILL_SELF, CHEATS_ACTION_FULL_HEALTH, CHEATS_ACTION_PLAY_OBI,
        CHEATS_ACTION_TECH_BONUS
    };
    static const char *const LEFT_WHY[] = {
        "killing yourself is left: a death travels like any other",
        "health is left: it is this machine's own business",
        "playing as somebody else is left: what a player wears is sampled every substep",
        "and the tech bonus message, which costs the session nothing at all"
    };
    overlay_row_t row;
    uint32_t      i;

    ut_section("the shipped console's one-shot codes, by the symbol each one is written with");
    for (i = 0; i < sizeof TAKEN / sizeof TAKEN[0]; ++i) {
        a_row(&row);
        ut_check(session_lock_take((uint32_t)OVERLAY_GROUP_ORIGINAL_ACTIONS, (uint32_t)TAKEN[i],
                                   &row) && !row.available, WHY[i]);
    }
    for (i = 0; i < sizeof LEFT / sizeof LEFT[0]; ++i) {
        a_row(&row);
        ut_check(!session_lock_take((uint32_t)OVERLAY_GROUP_ORIGINAL_ACTIONS, (uint32_t)LEFT[i],
                                    &row) && row.available, LEFT_WHY[i]);
    }
}

/* A reason the row carries in its own right outranks the session.
 *
 * Three codes of the console resolve, run and are still not offered, and one of them, the
 * credits, is on this file's list as well. The lock TAKES that row but does not relabel it: in a
 * session `session` would name a state that has nothing to do with why it is really refused,
 * and hide the sentence that says why. */
static void a_reason_of_its_own(void)
{
    overlay_row_t row;

    ut_section("a row that already knows why it is refused keeps its answer");
    a_row(&row);
    row.reason = (uint32_t)OVERLAY_REASON_HELD_MISBEHAVES;
    ut_check(session_lock_take((uint32_t)OVERLAY_GROUP_ORIGINAL_ACTIONS,
                               (uint32_t)CHEATS_ACTION_VIEW_CREDITS, &row) && !row.available,
             "the credits are still taken away in a session");
    ut_check(row.reason == (uint32_t)OVERLAY_REASON_HELD_MISBEHAVES,
             "and still say what is really wrong with them, not that a session is running");

    a_row(&row);
    row.reason = (uint32_t)OVERLAY_REASON_NEEDS_ROW;
    ut_check(session_lock_take((uint32_t)OVERLAY_GROUP_OPENPHANTOM_FRAMERATE, 0u, &row) &&
                 row.reason == (uint32_t)OVERLAY_REASON_SESSION,
             "while a reason that is only about this moment gives way to the session, which is "
             "the one the player cannot do anything about");
    session(false);
    session_lock_refresh();
}

/* A note of another shape under the same name: a multiplayer from a build this one does not
 * read. The panel cannot judge what it says, so it locks. */
static void a_note_this_build_cannot_read(void)
{
    const uint8_t odd[6] = { 1u, 0u, 0u, 0u, 1u, 1u };
    overlay_row_t row;

    ut_section("a session note filed in another shape");

    session(false);
    session_lock_refresh();
    ut_check(!session_lock_running(), "an ended session leaves the panel open");

    ut_check(shared_note_publish(SESSION_NOTE_NAME, odd, sizeof odd),
             "a note of another shape is filed");
    session_lock_refresh();
    ut_check(session_note_unreadable(), "the reader reports the shape it cannot read");
    ut_check(session_lock_running(), "and the panel treats it as a session that runs");
    ut_check(!session_lock_is_host(), "without claiming to be the host");

    a_row(&row);
    ut_check(session_lock_take((uint32_t)OVERLAY_GROUP_OPENPHANTOM_PICTURE, 0u, &row),
             "a row a session takes is taken");

    session(false);
    session_lock_refresh();
    ut_check(!session_lock_running(), "a readable note ends it again");
}

int main(void)
{
    the_spawner();
    a_note_this_build_cannot_read();
    the_other_groups();
    the_console_toggles();
    the_console_actions();
    a_reason_of_its_own();
    between_two_pictures();
    return ut_summary("session lock");
}
