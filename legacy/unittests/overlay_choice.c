/* overlay_choice.c: a choice is drawn as a choice, and only one of it is ever picked.
 *
 * A list built out of switches says the wrong thing. Five window shapes, four of them reading
 * OFF beside one reading ON, say the window has five settings that happen to be off; it has one
 * setting with five values. The size list, the level list, the entity spawner's kinds and the
 * model swap's roster are the same shape, and a roster of twenty drawn as switches reads as
 * twenty things that could be on at once.
 *
 * So this program holds three things. Every entry of every list is the CHOICE kind, so no list
 * can quietly go back to being switches. At most one entry of a list is marked, and a list with
 * nothing chosen marks none. And the one choice short enough to show whole, the behaviour of a
 * spawned entity, is held to its words, to the room it costs beside its name, and to the hit test
 * that turns a pixel into one of those words.
 *
 * Nothing here resolves an engine site, which is what makes the answers stable: no level, no
 * models bound, and the spawner group unavailable throughout. What is under test is the shape of
 * the rows and not what the game would put in them.
 */
#include "unittest.h"

#include "overlay_choice.h"
#include "overlay_levels.h"
#include "overlay_model.h"
#include "overlay_row_ids.h"
#include "overlay_spawn.h"
#include "overlay_window.h"

#include "common/text.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* The same number unittests/overlay_width.c checks every row against: the room the panel leaves
 * for a name and whatever stands beside it, in characters. Written here too rather than shared,
 * because the two programs do not share a header and a second copy of one number is cheaper than
 * one more header for one define; if it moves there it moves here, and the margin below says by
 * how much there is room to be wrong. */
#define ROW_BUDGET 48u

static void open_the_tab(void)
{
    uint32_t group;

    overlay_model_reset();
    overlay_model_set_tab(OVERLAY_TAB_OPENPHANTOM);
    for (group = 0; group < (uint32_t)OVERLAY_GROUP_COUNT; ++group) {
        overlay_model_toggle_group(group);
    }
    overlay_model_rebuild();
}

/* How many entries of a list one group draws, and how many of them are marked. */
typedef struct tally {
    uint32_t entries;
    uint32_t marked;
    uint32_t with_a_chip;
} tally_t;

static tally_t tally_group(overlay_group_t group)
{
    const uint32_t count = overlay_model_row_count();
    tally_t        out;
    uint32_t       i;

    memset(&out, 0, sizeof out);
    for (i = 0; i < count; ++i) {
        overlay_row_t row;

        if (!overlay_model_row(i, &row) || row.group != (uint32_t)group ||
            row.kind != OVERLAY_ROW_CHOICE) {
            continue;
        }
        ++out.entries;
        if (row.on) {
            ++out.marked;
        }
        if (row.value[0] != '\0') {
            ++out.with_a_chip;
        }
    }
    return out;
}

/* The first row of a kind on screen, or -1. */
static int32_t first_of_kind(overlay_row_kind_t kind)
{
    const uint32_t count = overlay_model_row_count();
    uint32_t       i;

    for (i = 0; i < count; ++i) {
        overlay_row_t row;

        if (overlay_model_row(i, &row) && row.kind == kind) {
            return (int32_t)i;
        }
    }
    return -1;
}

static void test_a_list_marks_one_entry(void)
{
    tally_t window;
    tally_t models;
    tally_t levels;

    ut_section("a list marks the entry that is chosen, and marks no other");
    open_the_tab();

    window = tally_group(OVERLAY_GROUP_OPENPHANTOM_WINDOW);
    ut_check(window.entries == 5u,
             "the window's five shapes are five entries of a list: fullscreen, borderless, a "
             "fixed window, a resizable one, and borderless at the size below");
    ut_check(window.marked == 1u,
             "and exactly one of them is marked, which is the shape the file names");
    ut_check(window.with_a_chip == 0u,
             "none of them carries a chip: the mark is the state, and a chip beside it would be "
             "the same state written twice");

    models = tally_group(OVERLAY_GROUP_OPENPHANTOM_MODELSWAP);
    ut_check(models.entries >= 5u, "the roster is a list too, one entry per model");
    ut_check(models.marked == 0u,
             "and none of it is marked here: nothing binds a body in a test process, so the "
             "player wears none of them, which is the case a list must also be able to show");

    /* The level list hangs off an action row the model will not press here, the same as the
     * spawner's does: nothing resolves, so the row that opens it reads unavailable. Opened
     * through the group itself, which is what the panel's own click reaches in a game. */
    (void)overlay_levels_toggle(OVERLAY_LEVELS_START_SLOT);
    overlay_model_rebuild();
    levels = tally_group(OVERLAY_GROUP_OPENPHANTOM_LEVELS);
    ut_check(levels.entries == OVERLAY_LEVELS_ENTRY_COUNT,
             "the level list holds one entry per level in the game");
    ut_check(levels.marked == 1u,
             "and one of them is marked: a file that has never been written to still names the "
             "level a new game starts at");
    ut_check(levels.with_a_chip == 0u, "and no entry of it carries a chip either");
    (void)overlay_levels_toggle(OVERLAY_LEVELS_START_SLOT);
    overlay_model_rebuild();
}

/* The window's SIZE list.
 *
 * Without this section, putting its entries back to switches leaves every program green,
 * because no walk opens the list and the window group's own tally counts the five shapes above
 * it. The list is the display's own modes, so how many there are is the machine's business and
 * nothing here counts them; what is pinned is the shape, which is the thing that can silently go
 * back to being wrong.
 *
 * Reached through the group, by the row's id, because the row that opens the list is unavailable
 * while fullscreen is the mode and a test process presses nothing. */
static void test_the_size_list(void)
{
    uint32_t entries = 0;
    uint32_t marked = 0;
    uint32_t chips = 0;
    uint32_t switches = 0;
    int32_t  size_row = -1;
    uint32_t i;
    char     note[200];

    ut_section("the window's size list, which is a choice and not a shelf of switches");
    open_the_tab();
    for (i = 0; i < overlay_model_row_count(); ++i) {
        overlay_row_t row;

        if (overlay_model_row(i, &row) && row.group == (uint32_t)OVERLAY_GROUP_OPENPHANTOM_WINDOW &&
            strcmp(row.label, "  Window size") == 0) {
            size_row = (int32_t)i;
            break;
        }
    }
    ut_check(size_row >= 0, "the row that opens the list is on screen, shut");

    {
        overlay_row_t opener;

        ut_check(overlay_model_row((uint32_t)size_row, &opener), "and it can be read");
        (void)overlay_window_toggle(opener.id - WINDOW_FIRST_ID);
    }
    overlay_model_rebuild();

    for (i = 0; i < overlay_model_row_count(); ++i) {
        overlay_row_t row;

        /* The entries and not the five shapes above them: a list entry is indented and a shape
         * row is not, which is the same thing the drawing reads them apart by. */
        if (!overlay_model_row(i, &row) ||
            row.group != (uint32_t)OVERLAY_GROUP_OPENPHANTOM_WINDOW ||
            row.label[0] != ' ' || row.label[1] != ' ' || row.label[2] != ' ') {
            continue;
        }
        if (row.kind == OVERLAY_ROW_CHEAT) {
            ++switches;
            continue;
        }
        if (row.kind != OVERLAY_ROW_CHOICE) {
            continue;
        }
        ++entries;
        if (row.on) {
            ++marked;
        }
        if (row.value[0] != '\0') {
            ++chips;
        }
    }
    text_format(note, sizeof note, "the open list holds %u entries, %u of them switches",
                (unsigned)entries, (unsigned)switches);
    note[sizeof note - 1] = '\0';
    ut_check(entries >= 1u && switches == 0u, note);
    ut_check(marked == 1u,
             "exactly one of them is marked: a fresh file names no size, and the auto entry at "
             "the top of the list is what that means");
    ut_check(chips == 0u, "and no entry of it carries a chip either");

    /* Shut again, and the panel says so on the row that opens it. */
    {
        overlay_row_t opener;

        ut_check(overlay_model_row((uint32_t)size_row, &opener) &&
                     strcmp(opener.label, "  Window size (pick one)") == 0,
                 "the row that opens the list says it is open");
        (void)overlay_window_toggle(opener.id - WINDOW_FIRST_ID);
        overlay_model_rebuild();
    }
}

/* The band word a choice produces, and the cut it goes through to get there. Both live in
 * overlay_choice.c so that the heading's rule is one rule; this is where the rule is written
 * down. */
static void test_the_name_of_the_chosen(void)
{
    overlay_row_t row = { 0 };
    char          word[16];
    char          tight[8];
    int32_t       at;

    ut_section("the name a choice puts on the band above it");
    open_the_tab();
    at = first_of_kind(OVERLAY_ROW_SEGMENT);
    ut_check(at >= 0 && overlay_model_row((uint32_t)at, &row), "the row of words is on screen");
    ut_check(!row.available && !overlay_choice_chosen_name(&row, word, sizeof word),
             "and it cannot be used here, so it lends the band NO word: the panel greys that row "
             "and shows a reason where its words would be, and a band saying Stand over it would "
             "be the two halves disagreeing about one state");
    /* The same row as a player with a level meets it. Set by hand because nothing in a test
     * process can raise a spawner; unittests/overlay_spawner.c drives the real thing. */
    row.available = true;
    ut_check(overlay_choice_chosen_name(&row, word, sizeof word) && strcmp(word, "Stand") == 0,
             "usable, it answers with the word it stands on");

    /* A list entry: the chosen one answers, and no other does. */
    {
        uint32_t marked = 0;
        uint32_t named = 0;
        uint32_t i;

        for (i = 0; i < overlay_model_row_count(); ++i) {
            overlay_row_t entry;

            if (!overlay_model_row(i, &entry) ||
                entry.group != (uint32_t)OVERLAY_GROUP_OPENPHANTOM_WINDOW ||
                entry.kind != OVERLAY_ROW_CHOICE) {
                continue;
            }
            if (entry.on) {
                ++marked;
            }
            if (overlay_choice_chosen_name(&entry, word, sizeof word)) {
                ++named;
                ut_check(word[0] != ' ',
                         "and it answers without the indent the entry is drawn with: a band word "
                         "stands on its own");
            }
        }
        ut_check(marked == 1u && named == 1u,
                 "one shape of the window is marked, and it is the only one that answers with a "
                 "name");
    }

    /* The cut. Sixteen characters is what a row's value holds, and a name longer than the buffer
     * is shortened with the two dots the drawing uses on a label, never truncated silently. */
    text_format(row.label, sizeof row.label, "%s", "    Borderless at that size");
    row.kind = OVERLAY_ROW_CHOICE;
    row.on   = true;
    ut_check(overlay_choice_chosen_name(&row, tight, sizeof tight) &&
                 strcmp(tight, "Borde..") == 0,
             "a name too long for the buffer is cut to it and ends in the panel's two dots");
    ut_check(overlay_choice_chosen_name(&row, word, sizeof word) &&
                 strcmp(word, "Borderless at..") == 0,
             "and the same name in a row's own value buffer is cut to fifteen, dots included");
    row.on = false;
    ut_check(!overlay_choice_chosen_name(&row, word, sizeof word),
             "an entry that is not the chosen one answers nothing, so a list with nothing chosen "
             "contributes no name at all");
}

/* The behaviour of a spawned entity: four words, short enough to stand on the row itself. */
static void test_the_behaviour_row(void)
{
    const char   *words[OVERLAY_CHOICE_SEGMENTS_MAX + 1u];
    char          strip[64];
    overlay_row_t row;
    int32_t       at;

    ut_section("the one choice short enough to be shown whole");
    open_the_tab();
    at = first_of_kind(OVERLAY_ROW_SEGMENT);
    ut_check(at >= 0 && overlay_model_row((uint32_t)at, &row) &&
                 strcmp(row.label, "Spawned entities") == 0,
             "the entity spawner's behaviour is the one row of segments the panel has");
    ut_check(row.value[0] == '\0',
             "it carries no chip: the words are on the row, and a chip beside them would stand "
             "for the same state");
    ut_check(overlay_choice_segments(&row, words, OVERLAY_CHOICE_SEGMENTS_MAX) == 4u &&
                 strcmp(words[0], "Stand") == 0 && strcmp(words[1], "Follow") == 0 &&
                 strcmp(words[2], "Attack") == 0 && strcmp(words[3], "Help") == 0,
             "its four words come from the behaviour table, in the order it lists them");
    ut_check(row.chosen == 0u && overlay_choice_is_chosen(&row, 0u) &&
                 !overlay_choice_is_chosen(&row, 1u) && !overlay_choice_is_chosen(&row, 2u) &&
                 !overlay_choice_is_chosen(&row, 3u),
             "the first of them is the chosen one to start with, and it is the only one");
    ut_check(!overlay_choice_is_chosen(&row, 4u),
             "a word past the end is not chosen, whatever the row's index happens to be");

    ut_check(overlay_choice_strip(&row, strip, sizeof strip) &&
                 strcmp(strip, " Stand  Follow  Attack  Help ") == 0,
             "the strip is those words with a space at each side, which is the box's own padding "
             "written as text: what is measured is what is drawn");
}

/* What the row costs the panel, and how much room is left over. The number is written out rather
 * than compared against a budget alone, because a row that fits today and fits by one character
 * is a row the next word breaks. */
static void test_the_row_still_fits(void)
{
    overlay_row_t row;
    char          note[200];
    int32_t       at;
    uint32_t      width;

    ut_section("the room a row of segments costs, and what is left of the panel");
    open_the_tab();
    at = first_of_kind(OVERLAY_ROW_SEGMENT);
    ut_check(at >= 0 && overlay_model_row((uint32_t)at, &row), "the row is on screen");
    width = overlay_choice_row_width(&row);
    text_format(note, sizeof note, "the name and the four words are %u characters, the panel has "
                "room for %u, and the margin is %u", (unsigned)width, (unsigned)ROW_BUDGET,
                (unsigned)(ROW_BUDGET - width));
    note[sizeof note - 1] = '\0';
    ut_check(width == 45u && width < ROW_BUDGET, note);
    ut_check(width > strlen(row.label),
             "and the words are part of that: a row of segments measured by its name alone is "
             "the widest row of its group counted as one of the narrowest");

    /* The rule the other direction. The words are the source's and cannot be made longer from
     * here, so the name is: a row whose name grows into the room the words need stops fitting,
     * and that is the answer a longer name has to get. */
    text_format(row.label, sizeof row.label, "%s",
                "A name long enough to crowd the words beside");
    ut_check(overlay_choice_row_width(&row) > ROW_BUDGET,
             "a longer name and the same words no longer fit, which is the answer that keeps a "
             "strip from being drawn over its own row");
}

static void test_picking_a_word(void)
{
    overlay_row_t row;
    int32_t       at;

    ut_section("picking one of the words, and reading it back off the row");
    open_the_tab();
    at = first_of_kind(OVERLAY_ROW_SEGMENT);
    ut_check(at >= 0 && overlay_model_row((uint32_t)at, &row) && !row.available,
             "the row is unavailable here, because nothing of the spawner resolved");
    ut_check(!overlay_choice_pick(&row, 2u),
             "and a pick on it is refused: a row a player cannot act on is not acted on through "
             "its words either");

    /* The write itself, asked of the group rather than through the row, which is the only way to
     * reach it in a process where the whole group reads unavailable. */
    ut_check(overlay_spawn_choose(row.id - SPAWN_FIRST_ID, 2u),
             "the group takes the third word");
    overlay_model_rebuild();
    ut_check(overlay_model_row((uint32_t)at, &row) && row.chosen == 2u &&
                 overlay_choice_is_chosen(&row, 2u) && !overlay_choice_is_chosen(&row, 0u),
             "and the row reads it back: the third is chosen now, and the first is not");
    ut_check(!overlay_spawn_choose(row.id - SPAWN_FIRST_ID, 4u),
             "a word past the end is refused rather than clamped onto the last one");

    ut_check(overlay_choice_step(&row, 1) == 3 && overlay_choice_step(&row, -1) == 1,
             "an arrow moves one word along from the one chosen");
    ut_check(overlay_spawn_choose(row.id - SPAWN_FIRST_ID, 3u), "at the last word");
    overlay_model_rebuild();
    ut_check(overlay_model_row((uint32_t)at, &row) && overlay_choice_step(&row, 1) == -1 &&
                 overlay_choice_step(&row, -1) == 2,
             "the arrow that would leave the strip answers nothing, so the key means what it "
             "means everywhere else in the panel rather than wrapping round");
    (void)overlay_spawn_choose(row.id - SPAWN_FIRST_ID, 0u);   /* put back where it was found */
    overlay_model_rebuild();
}

/* Where the boxes sit and which of them a point falls in. The widths are made up here: the
 * drawing measures the font and hands them in, so the arithmetic can be checked without one. */
static void test_the_hit_test(void)
{
    static const float WIDTHS[4] = { 10.0f, 20.0f, 30.0f, 40.0f };
    float              edges[OVERLAY_CHOICE_SEGMENTS_MAX + 1u];
    uint32_t           written;

    ut_section("which word a point falls on");
    written = overlay_choice_edges(WIDTHS, 4u, 1.0f, 200.0f, edges,
                                  OVERLAY_CHOICE_SEGMENTS_MAX + 1u);
    ut_check(written == 5u, "four boxes have five edges");
    /* 10 + 20 + 30 + 40 of words and two of padding each is 108, ending at 200. */
    ut_check(edges[0] == 92.0f && edges[1] == 104.0f && edges[2] == 126.0f &&
                 edges[3] == 158.0f && edges[4] == 200.0f,
             "each box is its own word plus its padding, and the strip is right aligned: it ends "
             "where the chip would have ended and begins wherever that leaves it");

    ut_check(overlay_choice_hit(edges, 4u, 92.0f) == 0, "the strip's left edge is the first word");
    ut_check(overlay_choice_hit(edges, 4u, 103.9f) == 0, "and so is the pixel before the second");
    ut_check(overlay_choice_hit(edges, 4u, 104.0f) == 1,
             "the edge between two boxes belongs to the one it opens, so no pixel answers twice");
    ut_check(overlay_choice_hit(edges, 4u, 140.0f) == 2, "the middle of the third is the third");
    ut_check(overlay_choice_hit(edges, 4u, 200.0f) == 3,
             "the strip's right edge is the last word, which nothing else can claim");
    ut_check(overlay_choice_hit(edges, 4u, 91.9f) == -1 &&
                 overlay_choice_hit(edges, 4u, 200.1f) == -1,
             "a point past either end is no word at all, so a click beside the strip falls "
             "through to whatever the row itself does");
    ut_check(overlay_choice_edges(WIDTHS, 4u, 1.0f, 200.0f, edges, 4u) == 0u,
             "and a strip with nowhere to write its last edge is not laid out at all");
}

/* Nothing that is not a choice answers as one. The dispatch is by group and by kind, and a row of
 * another group reaching it would be a row whose words came from somewhere else entirely. */
static void test_nothing_else_has_words(void)
{
    const char *words[OVERLAY_CHOICE_SEGMENTS_MAX];
    uint32_t    with_words = 0;
    uint32_t    i;

    ut_section("no other row of either tab has words of its own");
    open_the_tab();
    for (i = 0; i < overlay_model_row_count(); ++i) {
        overlay_row_t row;

        if (overlay_model_row(i, &row) &&
            overlay_choice_segments(&row, words, OVERLAY_CHOICE_SEGMENTS_MAX) > 0u) {
            ++with_words;
        }
    }
    ut_check(with_words == 1u, "exactly one row of the OpenPhantom tab has segments");

    overlay_model_set_tab(OVERLAY_TAB_ORIGINAL);
    overlay_model_rebuild();
    with_words = 0;
    for (i = 0; i < overlay_model_row_count(); ++i) {
        overlay_row_t row;

        if (overlay_model_row(i, &row) &&
            overlay_choice_segments(&row, words, OVERLAY_CHOICE_SEGMENTS_MAX) > 0u) {
            ++with_words;
        }
    }
    ut_check(with_words == 0u, "and none of the shipped console's tab has any");
}

/* The word on every band of the OpenPhantom tab, written out.
 *
 * The summary counts SWITCHES, so a group whose switches are entries of a list does not count
 * them: the window group reads ON, which is the stretch alone. THREE other bands do the same where
 * a test process cannot see them: Appearance falls to nothing the moment a model
 * is worn, and so do Level selection and Entity spawner once their lists are open. A band with no
 * switches under it and exactly one chosen entry carries that entry's NAME for that reason, which
 * is why the spawner reads "Stand" in a level; here its row is greyed and the band is empty.
 *
 * What this section can and cannot hold: the words below are the words in a TEST PROCESS,
 * where nothing resolves, no model is worn and no level is loaded. The words a player sees in a
 * game are not checked here.
 *
 * Written out rather than counted, and by heading rather than by group number, because the words
 * are what a player reads off a folded panel. The settings are whatever a test process finds,
 * which is the defaults; a program that wrote a settings key before this ran would move the
 * picture group's count, so this section resets and reads nothing else. */
static void test_the_word_on_every_band(void)
{
    static const struct { const char *heading; const char *word; } BANDS[] = {
        { "Cheats",          ""      },
        { "Free camera",     ""      },
        /* One choice under it and no switch, so in a level the band reads the word the behaviour
         * settled on. EMPTY here, and that is the rule rather than a gap: with no level the
         * behaviour row cannot be used, the panel greys it, and a band reading Stand over a
         * greyed row is the panel's two halves saying different things about one state. The same
         * happens in the game with a pickup chosen, which ignores the behaviour. Both of those
         * states are driven in unittests/overlay_spawner.c, which can stand a spawner up. Open
         * the kind list as well and the group carries two choices, which no one word can say. */
        { "Entity spawner",  ""      },
        { "Appearance",      ""      },  /* a list with nothing chosen: no model is worn here */
        { "Level selection", ""      },  /* its list is shut, so there is no entry to name */
        { "Engine",          "2 on"  },
        { "Controls",        ""      },
        { "Multiplayer",     ""      },  /* one key row: no switch and no choice to name */
        { "Window",          "ON"    },  /* the stretch switch: the five shapes are a choice */
        { "Frame rate",      "ON"    },
        { "Menus",           "OFF"   },
        { "Dismemberment",   "OFF"   }
    };
    const uint32_t wanted = (uint32_t)(sizeof BANDS / sizeof BANDS[0]);
    uint32_t       count;
    uint32_t       seen = 0;
    uint32_t       i;
    char           note[200];

    ut_section("the word on every band of the tab, so none of them moves unnoticed");
    overlay_model_reset();
    overlay_model_set_tab(OVERLAY_TAB_OPENPHANTOM);
    overlay_model_rebuild();
    count = overlay_model_row_count();
    for (i = 0; i < count; ++i) {
        overlay_row_t row;

        if (!overlay_model_row(i, &row) || row.kind != OVERLAY_ROW_GROUP) {
            continue;
        }
        text_format(note, sizeof note, "band %u is \"%s\" reading \"%s\", and the list says "
                    "\"%s\" reading \"%s\"", (unsigned)seen, row.label, row.value,
                    seen < wanted ? BANDS[seen].heading : "nothing more",
                    seen < wanted ? BANDS[seen].word : "nothing more");
        note[sizeof note - 1] = '\0';
        ut_check(seen < wanted && strcmp(row.label, BANDS[seen].heading) == 0 &&
                     strcmp(row.value, BANDS[seen].word) == 0, note);
        /* And the colour the band is drawn in, which is the state and not the spelling. */
        if (seen < wanted) {
            ut_check(row.on == (strcmp(BANDS[seen].word, "ON") == 0 ||
                                strcmp(BANDS[seen].word, "2 on") == 0),
                     "and it is lit when it says something is SWITCHED ON, read off the state "
                     "rather than out of the letters: a band naming the entry a choice settled "
                     "on is not lit, because nothing under it was switched on");
        }
        ++seen;
    }
    text_format(note, sizeof note, "%u bands were drawn and the list has %u", (unsigned)seen,
                (unsigned)wanted);
    note[sizeof note - 1] = '\0';
    ut_check(seen == wanted, note);
}

int main(void)
{
    test_a_list_marks_one_entry();
    test_the_size_list();
    test_the_name_of_the_chosen();
    test_the_behaviour_row();
    test_the_row_still_fits();
    test_picking_a_word();
    test_the_hit_test();
    test_nothing_else_has_words();
    test_the_word_on_every_band();
    return ut_summary("a choice drawn as a choice");
}
