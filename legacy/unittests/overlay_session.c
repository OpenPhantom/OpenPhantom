/* overlay_session.c: which rows of the panel a running session takes, named rather than numbered.
 *
 * Renaming, reordering or merging the headings of the OpenPhantom tab must not change what a
 * session takes away, and it easily could: session_lock.c decides by a group number and a slot
 * number inside that group, so a source whose rows moved by one would hand a player a setting
 * the content check hashes in the middle of a session, and the panel would look right while
 * doing it. Nothing in the build would say so.
 *
 * So this program is the guard. It runs the panel with a session published, walks every row on
 * screen, and compares the rows that were taken against a list written out by NAME. A slot number
 * that moves shows up here as a row that should have been taken and was not, or one that was
 * taken and should not have been, with the label in the failure text.
 *
 * The second half is the other risk. The panel keeps its tab, its folds and its search when it
 * closes, so a group can be open across the moment a session begins. A remembered fold must not
 * carry an open row into a session with it.
 *
 * Two more things. The five one-shot codes a session takes on the shipped console's tab are
 * pinned below by the symbol each is written with, which is also what the lock decides by,
 * because a test process resolves none of that tab and every row of it is built with an empty
 * name. And the entity spawner's kind list hangs off an ACTION row, which the walk that opens
 * everything cannot press while the group is unavailable, so it is opened by hand below.
 *
 * And the chip of a taken row on a CLIENT, which reads the host's value where the host decides
 * the setting. Every pass above publishes a host, because that is who takes the most; the client
 * is the one machine where a taken row has a number to show. The folded heading over those rows
 * counts the host's switches there too, not this machine's own.
 *
 * SIZE NOTE. Over six hundred lines, and what is long is the lists, each row named with the words
 * it has to read. The seam, when it grows again, is the client's half, the last section: it shares
 * with the rest only the walk that opens everything.
 */
#include "unittest.h"

#include "cheats_original_actions.h"
#include "overlay_chip.h"
#include "overlay_draw.h"
#include "overlay_model.h"
#include "overlay_reason.h"
#include "overlay_spawn.h"
#include "session_lock.h"
#include "spawn_place.h"

#include "common/host_settings_note.h"
#include "common/session_note.h"
#include "common/text.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* What a taken row looks like from outside: the heading it is drawn under and its own name. A
 * track has no name of its own, on purpose, and is listed as the empty string directly after the
 * value row it belongs to, which is the only place one is ever drawn.
 *
 * A name ending in a star is matched up to the star. One row here carries a number this machine
 * answers with, the screen's own rate, and pinning that would pin the display the test ran on. */
typedef struct taken_row {
    const char *heading;
    const char *label;
} taken_row_t;

static bool name_is(const char *label, const char *expected)
{
    size_t length = strlen(expected);

    if (length > 0u && expected[length - 1u] == '*') {
        return strncmp(label, expected, length - 1u) == 0;
    }
    return strcmp(label, expected) == 0;
}

/* Every row a running session takes on the OpenPhantom tab, in the order they are drawn, with the
 * multiplayer not running the NPC copies. Nothing resolves in a test process, so the rows read as
 * they do on an executable this build does not know; what is being pinned here is which of them
 * the LOCK takes, which does not depend on any of that.
 *
 * Written out rather than counted: a list of names is the only form of this that survives a
 * heading being renamed or a group being drawn somewhere else. */
static const taken_row_t EXPECTED[] = {
    { "Entity spawner", "Place with the mouse" },
    { "Entity spawner", "    Key: place with the mouse" },
    { "Entity spawner", "    Key: turn to face you" },
    { "Entity spawner", "Entity to spawn" },
    /* The behaviour, which is one row carrying its four words and not a list of four. It is the
     * SEGMENT kind, so it also holds the lock to that kind. */
    { "Entity spawner", "Spawned entities" },
    { "Entity spawner", "Remove spawned entities" },
    { "Level selection", "Skip to next level (debug)" },
    { "Engine", "Draw distance (1.0 to 2.5)" },
    { "Engine", "" },
    { "Engine", "Draw distance follows the frame rate" },
    { "Engine", "Keep the draw distance (costs frame rate)" },
    /* The fog's rows, drawn under the engine's heading and still the fog group's own slots. The
     * switch at the top of them, "No fog", writes this mod's own section and is NOT taken. */
    { "Engine", "Fog thickness (0.25 to 1.0)" },
    { "Engine", "" },
    { "Engine", "Fog follows the draw distance" },
    { "Frame rate", "Match the screen*" },
    { "Frame rate", "  Fraction of the screen's rate" },
    { "Frame rate", "Frame rate limit" },
    { "Dismemberment", "Lightsaber dismemberment" }
};
#define EXPECTED_COUNT (sizeof EXPECTED / sizeof EXPECTED[0])

static void session(bool running)
{
    session_note_t note;

    memset(&note, 0, sizeof note);
    note.running = running;
    note.is_host = running;
    ut_check(session_note_publish(&note), running ? "a session is published as running"
                                                  : "and then as ended");
}

/* Every heading and every fold on the open tab, opened, so the walk below sees every row the tab
 * can put on screen. Found by what they look like, the same way the model's own test does it. */
static void open_everything(void)
{
    bool opened = true;

    while (opened) {
        const uint32_t count = overlay_model_row_count();
        uint32_t       i;

        opened = false;
        for (i = 0; i < count; ++i) {
            overlay_row_t row;

            if (!overlay_model_row(i, &row) || row.expanded) {
                continue;
            }
            if (row.kind != OVERLAY_ROW_GROUP &&
                !(row.kind == OVERLAY_ROW_INFO && row.label[0] == '+')) {
                continue;
            }
            if (overlay_model_activate(i)) {
                overlay_model_rebuild();
                opened = true;
                break;
            }
        }
    }
}

/* Walks the tab and checks the taken rows against EXPECTED, in order. The heading is carried down
 * from the last heading row seen, which is what a player reads a row under. */
static void check_the_taken_rows(const char *when)
{
    const uint32_t count = overlay_model_row_count();
    /* Copied rather than pointed at: a row is handed over by value into a variable that goes out
     * of scope with the loop, so a pointer to its label would be read after the row it named had
     * gone. It looked right for one section and reported every heading as the row itself in the
     * next, which is how that showed up at all. */
    char           heading[OVERLAY_LABEL_MAX] = "";
    uint32_t       seen = 0;
    uint32_t       i;
    char           note[200];

    for (i = 0; i < count; ++i) {
        overlay_row_t row;

        if (!overlay_model_row(i, &row)) {
            continue;
        }
        if (row.kind == OVERLAY_ROW_GROUP) {
            text_format(heading, sizeof heading, "%s", row.label);
            continue;
        }
        if (row.available || row.kind == OVERLAY_ROW_INFO) {
            continue;
        }
        if (row.reason != (uint32_t)OVERLAY_REASON_SESSION &&
            row.reason != (uint32_t)OVERLAY_REASON_SPAWNER) {
            continue;    /* unavailable for a reason of its own, which this program is not about */
        }
        text_format(note, sizeof note, "%s: taken row %u is \"%s\" under \"%s\", and the list says "
                    "\"%s\" under \"%s\"", when, (unsigned)seen, row.label, heading,
                    seen < EXPECTED_COUNT ? EXPECTED[seen].label : "nothing more",
                    seen < EXPECTED_COUNT ? EXPECTED[seen].heading : "nothing more");
        note[sizeof note - 1] = '\0';
        ut_check(seen < EXPECTED_COUNT && name_is(row.label, EXPECTED[seen].label) &&
                     strcmp(heading, EXPECTED[seen].heading) == 0, note);
        ++seen;
    }
    text_format(note, sizeof note, "%s: %u rows were taken and the list has %u", when,
                (unsigned)seen, (unsigned)EXPECTED_COUNT);
    note[sizeof note - 1] = '\0';
    ut_check(seen == EXPECTED_COUNT, note);
}

static void test_the_taken_rows(void)
{
    ut_section("the rows a session takes on the OpenPhantom tab, by name");
    overlay_model_reset();
    overlay_model_set_tab(OVERLAY_TAB_OPENPHANTOM);
    session(true);
    overlay_model_rebuild();
    open_everything();
    check_the_taken_rows("in a session");
}

/* The spawner's list, which the walk above cannot reach: it hangs off an ACTION row, and
 * overlay_model_activate() refuses a row that is not available; in a session the whole group is
 * unavailable, so the walk sees the row shut and never what is inside it.
 *
 * What it can be held to here is the row that carries it: a test process has no level and the
 * spawner answers no kinds, so the list opens EMPTY. What stands in it can only be seen in the
 * game, which is why overlay_dump.c prints it as a run of its own. */
static void test_the_kind_list_is_taken_too(void)
{
    const uint32_t count_when_shut = (uint32_t)EXPECTED_COUNT;
    uint32_t       count;
    uint32_t       i;
    uint32_t       taken = 0;
    bool           found = false;

    ut_section("the spawner's kind list, opened");
    overlay_model_reset();
    overlay_model_set_tab(OVERLAY_TAB_OPENPHANTOM);
    session(true);
    overlay_model_rebuild();
    open_everything();
    (void)overlay_spawn_toggle(OVERLAY_SPAWN_KIND_SLOT);
    overlay_model_rebuild();

    count = overlay_model_row_count();
    for (i = 0; i < count; ++i) {
        overlay_row_t row;

        if (!overlay_model_row(i, &row)) {
            continue;
        }
        if (row.group == (uint32_t)OVERLAY_GROUP_OPENPHANTOM_SPAWN &&
            strcmp(row.label, "Entity to spawn (pick one)") == 0) {
            found = true;
            ut_check(!row.available && row.reason == (uint32_t)OVERLAY_REASON_SPAWNER,
                     "the open kind list's own row is taken, the same as the shut one was");
        }
        if (!row.available && row.kind != OVERLAY_ROW_GROUP && row.kind != OVERLAY_ROW_INFO &&
            (row.reason == (uint32_t)OVERLAY_REASON_SESSION ||
             row.reason == (uint32_t)OVERLAY_REASON_SPAWNER)) {
            ++taken;
        }
    }
    ut_check(found, "the kind list opens, and says so in its own name");
    ut_check(taken == count_when_shut,
             "and the same rows are taken as with it shut: the list itself is empty here, because "
             "a test process has no level to offer kinds from");
    session(false);
}

/* The shipped console's tab.
 *
 * Its rows are pinned by the SYMBOL each code is written with and not by its name, and that is not
 * a preference: nothing of the console resolves in a test process, so every row of that tab is
 * built with an empty label and a walk by name would compare one empty string against another.
 * The symbol is also what the lock decides by for these rows (session_lock.c, action_is_locked),
 * so it is the thing worth pinning. The eleven toggles are not here at all, because the table they
 * come from never resolved and the group builds no rows; they are pinned by name in
 * unittests/session_lock.c, against the console's table written out. */
static void test_the_taken_rows_on_the_original_tab(void)
{
    /* In the order they are drawn, which for this group is the order of the enum. */
    static const cheats_action_id_t EXPECTED_ACTIONS[] = {
        CHEATS_ACTION_LOWER_DIFFICULTY_A, CHEATS_ACTION_LOWER_DIFFICULTY_B,
        CHEATS_ACTION_INCREASE_DIFFICULTY, CHEATS_ACTION_VIEW_CREDITS,
        CHEATS_ACTION_GRAPHICS_DETAIL
    };
    const uint32_t wanted = sizeof EXPECTED_ACTIONS / sizeof EXPECTED_ACTIONS[0];
    uint32_t       count;
    uint32_t       seen = 0;
    uint32_t       i;
    char           note[200];

    ut_section("the rows a session takes on the Original tab");
    overlay_model_reset();
    overlay_model_set_tab(OVERLAY_TAB_ORIGINAL);
    session(true);
    overlay_model_rebuild();
    open_everything();
    count = overlay_model_row_count();

    for (i = 0; i < count; ++i) {
        overlay_row_t row;

        if (!overlay_model_row(i, &row) || row.available ||
            row.kind == OVERLAY_ROW_GROUP || row.kind == OVERLAY_ROW_INFO ||
            row.reason != (uint32_t)OVERLAY_REASON_SESSION) {
            continue;
        }
        text_format(note, sizeof note, "taken row %u of the Original tab is code %u, and the list "
                    "says %u", (unsigned)seen, (unsigned)row.id,
                    seen < wanted ? (unsigned)EXPECTED_ACTIONS[seen] : 0u);
        note[sizeof note - 1] = '\0';
        ut_check(seen < wanted && row.id == (uint32_t)EXPECTED_ACTIONS[seen] &&
                     row.group == (uint32_t)OVERLAY_GROUP_ORIGINAL_ACTIONS, note);
        ++seen;
    }
    text_format(note, sizeof note, "%u one-shot codes were taken and the list has %u",
                (unsigned)seen, (unsigned)wanted);
    note[sizeof note - 1] = '\0';
    ut_check(seen == wanted, note);
    ut_check(overlay_model_row_count() > wanted,
             "and the rest of the tab is still there beside them");
}

/* The word on each taken row and the sentence under the first of them. A player who cannot act on
 * a row is owed the reason on the row itself: `n/a` was one word for four different situations and
 * only two of them were anything they could do something about. */
static void test_the_words(void)
{
    uint32_t       count;
    uint32_t       i;
    bool           found_sentence = false;
    int32_t        first_taken = -1;

    ut_section("what a taken row says about itself");
    /* What the placement mode works out once a frame; the panel only repeats it. Nothing drives
     * the mode in a test process, so it is written here as the mode would have left it in a
     * session whose multiplayer does not run the copies. */
    spawn_place_state()->unavailable = "the session runs no copies";
    overlay_model_rebuild();
    count = overlay_model_row_count();

    for (i = 0; i < count && first_taken < 0; ++i) {
        overlay_row_t row;

        if (overlay_model_row(i, &row) && !row.available &&
            row.reason == (uint32_t)OVERLAY_REASON_SPAWNER) {
            first_taken = (int32_t)i;
        }
    }
    ut_check(first_taken >= 0, "the spawner's first taken row is found");
    {
        overlay_row_t row;
        uint32_t      at;

        ut_check(overlay_model_row((uint32_t)first_taken, &row) &&
                     strcmp(overlay_reason_word(row.reason), "see why") == 0,
                 "its chip points at the group's own sentence rather than saying `session`: a "
                 "session is not what takes this group, a session running no copies is, and that "
                 "is one of the four states the sentence tells apart");
        /* One sentence, in the group's own words, written out of the rule its rows, the placement
         * mode and the log all read. The lock used to write a second one under the first taken row
         * as well, three rows below this, and the two were one state with two spellings. */
        for (at = 0; at < count && !found_sentence; ++at) {
            found_sentence = overlay_model_row(at, &row) && row.kind == OVERLAY_ROW_INFO &&
                             strcmp(row.label, "    Why: the session runs no copies") == 0;
        }
        ut_check(found_sentence, "and the group says why, once, in its own words");
        found_sentence = false;
        for (at = 0; at < count; ++at) {
            if (overlay_model_row(at, &row) && row.kind == OVERLAY_ROW_INFO &&
                row.group == (uint32_t)OVERLAY_GROUP_OPENPHANTOM_SPAWN &&
                strcmp(row.label, SESSION_LOCK_WORD) == 0) {
                found_sentence = true;
            }
        }
        ut_check(!found_sentence,
                 "and the lock adds no second sentence of its own under the same rows");
    }
    {
        /* The same floor unittests/overlay_width.c carries over its own walk: this breaks at the
         * first hit, so with no hit at all it ran over nothing and the section stayed green
         * having checked nothing. */
        bool found_one = false;

        for (i = 0; i < count; ++i) {
            overlay_row_t row;

            if (overlay_model_row(i, &row) && !row.available &&
                row.reason == (uint32_t)OVERLAY_REASON_SESSION) {
                found_one = true;
                ut_check(strcmp(overlay_reason_word(row.reason), "session") == 0,
                         "every row a session itself takes carries the same word");
                break;
            }
        }
        ut_check(found_one, "and there was a row for it to read: a walk that found none would "
                            "have said nothing about the word at all");
    }
    spawn_place_state()->unavailable = NULL;
    overlay_model_rebuild();
}

/* The panel keeps its folds across a close now. A group left open before a session begins must
 * still have its rows taken when the panel comes back up: what is remembered is where the player
 * was, never what they may touch. */
static void test_a_remembered_fold_unlocks_nothing(void)
{
    ut_section("a remembered fold does not carry an open row into a session");
    session(false);
    overlay_model_reset();
    overlay_model_set_tab(OVERLAY_TAB_OPENPHANTOM);
    overlay_model_rebuild();
    open_everything();
    /* What closing the panel does now: the half typed number and the waiting key capture go, and
     * nothing else. */
    overlay_model_forget_edits();
    ut_check(overlay_model_tab() == OVERLAY_TAB_OPENPHANTOM,
             "closing the panel leaves the tab where the player put it");
    session(true);
    overlay_model_rebuild();
    ut_check(overlay_model_row_count() > 11u,
             "and the groups they opened are still open when it comes back up");
    check_the_taken_rows("with every group opened before the session began");
    session(false);
}

/* The chat's key stays free in a session, which is the one time anybody needs it. It writes
 * [multiplayer] ChatKey, which the two sides never compare, and which key opens the chat is a
 * matter for the machine it is pressed on. The walk above would list the row among the taken ones
 * if the lock took it; this says the other half, that the row is there under its own heading and
 * can be pressed. */
static void test_the_chat_key_stays_free(void)
{
    uint32_t count;
    uint32_t i;
    uint32_t found = 0;

    ut_section("the chat\'s key in a session");
    overlay_model_reset();
    overlay_model_set_tab(OVERLAY_TAB_OPENPHANTOM);
    session(true);
    overlay_model_rebuild();
    open_everything();
    count = overlay_model_row_count();
    for (i = 0; i < count; ++i) {
        overlay_row_t row;

        if (!overlay_model_row(i, &row) ||
            row.group != (uint32_t)OVERLAY_GROUP_OPENPHANTOM_MULTIPLAYER ||
            row.kind != OVERLAY_ROW_HOTKEY) {
            continue;
        }
        ++found;
        ut_check(row.available && strcmp(row.label, "Key that opens the chat") == 0,
                 "the multiplayer group\'s key row is offered in a session");
        ut_check(overlay_model_activate(i) && overlay_model_is_capturing_hotkey(),
                 "and pressing it waits for a key, as it does with no session");
        overlay_model_forget_edits();
    }
    ut_check(found == 1u, "there is exactly one such row on the tab, and the walk found it");
    session(false);
}

/* Every kind of row a player can act on is a kind a session takes, held kind by kind.
 *
 * The lock and the panel's own summary read one predicate for this (overlay_model.h), and it is
 * written as what is NOT acted on, so a kind added later is taken until somebody says otherwise.
 * That direction is the whole of the guard, and nothing held it: the two files carried a list
 * each, the lists disagreed about the slider, and a kind on neither of them would have walked
 * into a session with its row left open.
 *
 * The last line of the table is a kind that does not exist. It is the one that goes red if the
 * rule is ever turned back into a list of what IS acted on, whatever that list holds on the day,
 * which is what makes this a guard for the next row kind and not only for today's.
 *
 * Driven against a group the lock takes whole, so what is under test is the kind and not which
 * slot it sits in. */
static void test_every_acting_kind_is_taken(void)
{
    static const struct {
        overlay_row_kind_t kind;
        bool               taken;
        const char        *what;
    } KINDS[] = {
        { OVERLAY_ROW_GROUP,  false, "a heading, which is not a control" },
        { OVERLAY_ROW_CHEAT,  true,  "a switch" },
        { OVERLAY_ROW_ACTION, true,  "a button" },
        { OVERLAY_ROW_HOTKEY, true,  "a key binding" },
        { OVERLAY_ROW_VALUE,  true,  "a typed number" },
        { OVERLAY_ROW_INFO,   false, "a note under the row above it" },
        { OVERLAY_ROW_SLIDER,  true, "a track" },
        { OVERLAY_ROW_CHOICE,  true, "one entry of a list" },
        { OVERLAY_ROW_SEGMENT, true, "a choice drawn as a row of words" },
        { (overlay_row_kind_t)(OVERLAY_ROW_SEGMENT + 1), true, "a kind that does not exist yet" }
    };
    const uint32_t count = (uint32_t)(sizeof KINDS / sizeof KINDS[0]);
    uint32_t       i;

    ut_section("every kind a player can act on is a kind a session takes");
    session(true);
    session_lock_refresh();
    for (i = 0; i < count; ++i) {
        overlay_row_t row;
        char          note[200];

        memset(&row, 0, sizeof row);
        row.kind      = KINDS[i].kind;
        row.available = true;
        row.reason    = (uint32_t)OVERLAY_REASON_NONE;

        text_format(note, sizeof note, "%s: the lock %s", KINDS[i].what,
                    KINDS[i].taken ? "takes it" : "leaves it alone");
        note[sizeof note - 1] = '\0';
        ut_check(session_lock_take((uint32_t)OVERLAY_GROUP_OPENPHANTOM_FRAMERATE, 0u, &row) ==
                     KINDS[i].taken, note);
        text_format(note, sizeof note, "%s: and the row reads %s afterwards", KINDS[i].what,
                    KINDS[i].taken ? "unavailable" : "as it did");
        note[sizeof note - 1] = '\0';
        ut_check(row.available == !KINDS[i].taken, note);
    }
    session(false);
}

/* The chip's word measures through the drawing, which a test process does not have. The word is
 * what is checked here and it measures nothing; these answer for the two measures the rest of
 * overlay_chip.c reaches, which nothing below calls. */
float overlay_draw_note_width(const char *text)
{
    return (text != NULL) ? (float)strlen(text) : 0.0f;
}

float overlay_draw_note_height(void)
{
    return 1.0f;
}

/* A session this machine is a CLIENT of, and the two records the host's values arrive in: the
 * multiplayer's, and the one view_distance_fix files for what it applied. `present` is which of
 * the four settings the host named. */
static void client_session(uint16_t present)
{
    session_note_t        note;
    host_settings_t       host;
    host_settings_taken_t taken;

    memset(&host, 0, sizeof host);
    host.running = true;
    host.generation = 1u;
    host.present = present;
    host.values[HOST_SETTING_VIEW_RANGE_SCALE]   = 1.5f;
    host.values[HOST_SETTING_FOG_BAND_SCALE]     = 0.5f;
    host.values[HOST_SETTING_AUTHORED_FOG_BAND]  = 1.0f;
    host.values[HOST_SETTING_DISMEMBERMENT_MODE] = 2.0f;
    ut_check(host_settings_publish(&host), "the multiplayer files the host's values");
    memset(&taken, 0, sizeof taken);
    taken.in_force = (uint16_t)(present & (1u << HOST_SETTING_VIEW_RANGE_SCALE));
    taken.generation = 1u;
    taken.effective[HOST_SETTING_VIEW_RANGE_SCALE] = 1.25f;
    ut_check(host_settings_publish_taken("view_distance_fix", &taken),
             "and view_distance_fix files what it applied, below the host's target");
    memset(&note, 0, sizeof note);
    note.running = true;
    note.is_host = false;
    ut_check(session_note_publish(&note), "a session is published with this machine a client");
}

/* Both records back to a session that does not run, as the one exit leaves them. */
static void no_client_session(void)
{
    host_settings_t       host;
    host_settings_taken_t taken;

    memset(&host, 0, sizeof host);
    memset(&taken, 0, sizeof taken);
    ut_check(host_settings_publish(&host) &&
                 host_settings_publish_taken("view_distance_fix", &taken),
             "the host's values are withdrawn");
    session(false);
}

/* The chip each row reads, found by heading and name, and the note under the draw distance. */
static void check_the_chips(const char *when, const char *const *words, const char *in_force)
{
    static const struct { const char *heading; const char *label; } ROWS[] = {
        { "Engine", "Draw distance (1.0 to 2.5)" },
        { "Engine", "Draw distance follows the frame rate" },
        { "Engine", "Keep the draw distance (costs frame rate)" },
        { "Engine", "Fog thickness (0.25 to 1.0)" },
        { "Engine", "Fog follows the draw distance" },
        { "Frame rate", "Frame rate limit" },
        { "Dismemberment", "Lightsaber dismemberment" }
    };
    const uint32_t count = (uint32_t)(sizeof ROWS / sizeof ROWS[0]);
    char           heading[OVERLAY_LABEL_MAX] = "";
    char           note[220];
    uint32_t       found = 0;
    bool           note_seen = false;
    uint32_t       i;
    uint32_t       r;

    overlay_model_rebuild();
    for (i = 0; i < overlay_model_row_count(); ++i) {
        overlay_row_t row;

        if (!overlay_model_row(i, &row)) {
            continue;
        }
        if (row.kind == OVERLAY_ROW_GROUP) {
            text_format(heading, sizeof heading, "%s", row.label);
            continue;
        }
        if (strncmp(row.label, "  in force: ", 12) == 0) {
            note_seen = true;
            text_format(note, sizeof note, "%s: the note under the draw distance reads \"%s\"",
                        when, row.label);
            ut_check(strcmp(row.label + 12, in_force) == 0, note);
        }
        for (r = 0; r < count; ++r) {
            if (strcmp(heading, ROWS[r].heading) != 0 || strcmp(row.label, ROWS[r].label) != 0) {
                continue;
            }
            ++found;
            text_format(note, sizeof note, "%s: \"%s\" reads \"%s\" and should read \"%s\"", when,
                        row.label, overlay_chip_word(&row), words[r]);
            ut_check(!row.available && strcmp(overlay_chip_word(&row), words[r]) == 0, note);
        }
    }
    text_format(note, sizeof note, "%s: all %u rows and the note were there to read", when,
                (unsigned)count);
    ut_check(found == count && note_seen, note);
}

/* The word on a heading, folded: the heading is found by name, folded, read and opened again, so
 * the walk after it sees the tab as it was. False when there is no such heading. */
static bool folded_heading_word(const char *heading, char *out, size_t size)
{
    uint32_t i;

    out[0] = '\0';
    overlay_model_rebuild();
    for (i = 0; i < overlay_model_row_count(); ++i) {
        overlay_row_t row;

        if (!overlay_model_row(i, &row) || row.kind != OVERLAY_ROW_GROUP ||
            strcmp(row.label, heading) != 0) {
            continue;
        }
        if (row.expanded && overlay_model_activate(i)) {
            overlay_model_rebuild();
        }
        if (!overlay_model_row(i, &row) || row.expanded) {
            return false;
        }
        text_format(out, size, "%s", overlay_chip_word(&row));
        if (overlay_model_activate(i)) {
            overlay_model_rebuild();
        }
        return true;
    }
    return false;
}

/* The folded headings over the rows the host decides count what the session plays with:
 * "Fog follows the draw distance" is on in this machine's file and reads "host OFF" on a client, so
 * "Engine" counts one switch fewer there than on the host. "Dismemberment" holds nothing a session
 * leaves free and says so on both. */
static void check_the_folded_headings(const char *when, const char *engine,
                                      const char *dismemberment)
{
    char word[32];
    char note[160];
    bool found;

    found = folded_heading_word("Engine", word, sizeof word);
    text_format(note, sizeof note, "%s: the folded \"Engine\" reads \"%s\" and should read \"%s\"",
                when, word, engine);
    ut_check(found && strcmp(word, engine) == 0, note);
    found = folded_heading_word("Dismemberment", word, sizeof word);
    text_format(note, sizeof note, "%s: the folded \"Dismemberment\" reads \"%s\" and should read "
                "\"%s\"", when, word, dismemberment);
    ut_check(found && strcmp(word, dismemberment) == 0, note);
}

static void test_the_host_values_on_a_client(void)
{
    static const char *const ON_A_HOST[] = {
        "session", "session", "session", "session", "session", "session", "session"
    };
    static const char *const ON_A_CLIENT[] = {
        "host 1.50x", "session", "session", "host 0.50x", "host OFF", "session", "host ON"
    };
    static const char *const ONLY_THE_DRAW_DISTANCE[] = {
        "host 1.50x", "session", "session", "session", "session", "session", "session"
    };

    ut_section("the chip of a taken row reads the host's value on a client");
    overlay_model_reset();
    overlay_model_set_tab(OVERLAY_TAB_OPENPHANTOM);
    session(true);
    overlay_model_rebuild();
    open_everything();
    check_the_chips("on the host", ON_A_HOST, "not reported");
    check_the_folded_headings("on the host", "2 on", "session");

    client_session((uint16_t)((1u << HOST_SETTING_COUNT) - 1u));
    check_the_chips("on a client whose host named all four", ON_A_CLIENT, "1.25x");
    check_the_folded_headings("on a client whose host named all four", "1 on", "session");

    client_session((uint16_t)(1u << HOST_SETTING_VIEW_RANGE_SCALE));
    check_the_chips("on a client whose host named the draw distance alone",
                    ONLY_THE_DRAW_DISTANCE, "1.25x");
    check_the_folded_headings("on a client whose host named the draw distance alone", "2 on",
                              "session");

    /* The records say a client's session runs, and the session note says this machine hosts it.
     * A host decides its own values and shows them nowhere as the host's, whatever a record
     * left behind says. */
    session(true);
    check_the_chips("on the host, with a client's records still filed", ON_A_HOST,
                    "not reported");

    no_client_session();
    overlay_model_rebuild();
    {
        overlay_row_t row;
        uint32_t      i;
        uint32_t      marked = 0;

        for (i = 0; i < overlay_model_row_count(); ++i) {
            if (overlay_model_row(i, &row) && row.host_value) {
                ++marked;
            }
        }
        ut_check(marked == 0u, "and when the session ends no row carries the host's value");
    }
}

int main(void)
{
    test_every_acting_kind_is_taken();
    test_the_taken_rows();
    test_the_words();
    test_the_kind_list_is_taken_too();
    test_the_taken_rows_on_the_original_tab();
    test_a_remembered_fold_unlocks_nothing();
    test_the_chat_key_stays_free();
    test_the_host_values_on_a_client();
    return ut_summary("the panel in a session");
}
