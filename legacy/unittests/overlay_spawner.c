/* overlay_spawner.c: the entity spawner group with a spawner standing behind it.
 *
 * Every other programme here links the real npc_spawner.c, which resolves nothing in a process
 * with no game: no level, no player, no spawn routine, so npc_spawner_is_available() is false and
 * every row of this group reads unavailable. That is a state worth pinning and the other
 * programmes pin it. What none of them could reach is the other one, and it is the one a player
 * is in the whole time: the group USABLE.
 *
 * What only a usable group can check:
 *
 *   the only row of words in the panel. Left and Right on it, a press on one of the words, and
 *   the tab it falls through to at either end: a branch removed from overlay_keys_sideways()
 *   leaves every other programme green, because on a row that cannot be used the branch and its
 *   absence do the same thing;
 *
 *   the same row LOCKED while the group around it is not, which is what a pickup does: it runs
 *   one script whatever the behaviour says. A key or a press that got through there would write a
 *   behaviour the copy will not run, and the row says so on the line under it;
 *
 *   the word the group's heading carries when it is folded, in both of those states;
 *
 *   and a list entry that is offered but not chosen, which no overlay_dump.c pass holds.
 *
 * So npc_spawner.c is replaced here by the stand in below, the one file of the model's sources
 * this programme does not link. Everything else is real, the row builder included.
 */
#include "unittest.h"

#include "entity_offer.h"
#include "npc_spawn_desc.h"
#include "npc_spawn_save.h"
#include "npc_spawner.h"
#include "overlay_choice.h"
#include "overlay_keys.h"
#include "overlay_layout.h"
#include "overlay_model.h"
#include "overlay_spawn.h"
#include "session_lock.h"
#include "spawn_place.h"
#include "spawn_scripts.h"

#include "common/session_note.h"
#include "common/text.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define KEY_LEFT  0x25
#define KEY_RIGHT 0x27

/* ============================================================================================
 * The stand in.
 *
 * Three kinds, one per shelf the group draws a heading for, and the third is a pickup: that is
 * the kind whose script ignores the behaviour, so choosing it is how a player locks the row of
 * words without locking anything else.
 * ========================================================================================== */

typedef struct fake_kind {
    const char *name;
    const char *file;
    uint8_t     section;
} fake_kind_t;

static const fake_kind_t KINDS[] = {
    { "ddroid", "ddroid.baf", (uint8_t)ENTITY_SECTION_LEVEL   },
    { "tusken", "tusken.baf", (uint8_t)ENTITY_SECTION_FIGURES },
    { "health", "health.baf", (uint8_t)ENTITY_SECTION_PICKUPS }
};
#define KIND_COUNT ((uint32_t)(sizeof KINDS / sizeof KINDS[0]))

/* The pickup's place in the list above, so a check can say which one it means by name. */
#define A_FIGHTER 0u
#define A_PICKUP  2u

static struct {
    bool     available;
    uint32_t behaviour;
    int32_t  chosen;
} st;

uint32_t npc_spawner_behaviour(void)             { return st.behaviour; }
void     npc_spawner_set_behaviour(uint32_t b)   { st.behaviour = b; }
bool     npc_spawner_is_available(void)          { return st.available; }
void     npc_spawner_refresh(void)               { }
uint32_t npc_spawner_kind_count(void)            { return st.available ? KIND_COUNT : 0u; }
int32_t  npc_spawner_chosen(void)                { return st.chosen; }
uint32_t npc_spawner_alive(void)                 { return 0u; }
uint32_t npc_spawner_remove_all(void)            { return 0u; }
bool     npc_spawner_can_raise(void)             { return st.available; }
bool     npc_spawner_names_any(void)             { return false; }

void npc_spawner_choose(int32_t index)
{
    st.chosen = (index >= 0 && (uint32_t)index < KIND_COUNT) ? index : -1;
}

bool npc_spawner_kind(uint32_t index, npc_spawner_kind_t *out)
{
    if (out == NULL || index >= npc_spawner_kind_count()) {
        return false;
    }
    memset(out, 0, sizeof *out);
    text_format(out->name, sizeof out->name, "%s", KINDS[index].name);
    text_format(out->file, sizeof out->file, "%s", KINDS[index].file);
    out->placements = 1u;
    out->section    = KINDS[index].section;
    return true;
}

/* The rest of what the model's sources reach for, none of which this programme drives: the save
 * block, the module node and the multiplayer link all ask the spawner about live copies, and
 * there are none here. */
const uint8_t *npc_spawner_ring_actor(uint32_t slot)
{
    (void)slot;
    return NULL;
}

bool npc_spawner_owns(uintptr_t actor)
{
    (void)actor;
    return false;
}

bool npc_spawner_delete(uintptr_t actor)
{
    (void)actor;
    return false;
}

bool npc_spawner_raise_saved(const npc_spawn_saved_t *saved)
{
    (void)saved;
    return false;
}

bool npc_spawner_description(uintptr_t actor, npc_spawn_desc_t *out)
{
    (void)actor;
    (void)out;
    return false;
}

bool npc_spawner_raise_granted(const npc_spawn_desc_t *desc, uint32_t key,
                               const npc_spawn_saved_t *saved)
{
    (void)desc;
    (void)key;
    (void)saved;
    return false;
}

/* ============================================================================================ */

static overlay_row_t row;

/* overlay_keys.c reads `visible_rows` to keep the selection on screen, and before the first build
 * that number is zero. A 1280 by 720 display at the font's own size. */
static void a_screen(void)
{
    const float tabs[OVERLAY_LAYOUT_TABS] = { 64.0f, 96.0f };

    overlay_layout_build(16.0f, 320.0f, overlay_model_row_count(), tabs, 1280.0f, 720.0f);
}

static int32_t first_row_of_kind(overlay_row_kind_t kind)
{
    const uint32_t count = overlay_model_row_count();
    uint32_t       i;

    for (i = 0; i < count; ++i) {
        overlay_row_t candidate;

        if (overlay_model_row(i, &candidate) && candidate.kind == kind) {
            return (int32_t)i;
        }
    }
    return -1;
}

/* The group's own heading, found by its name: it is not the first on the tab, and the word it
 * carries is what half of this programme is about. */
static int32_t the_heading(void)
{
    const uint32_t count = overlay_model_row_count();
    uint32_t       i;

    for (i = 0; i < count; ++i) {
        overlay_row_t candidate;

        if (overlay_model_row(i, &candidate) && candidate.kind == OVERLAY_ROW_GROUP &&
            strcmp(candidate.label, "Entity spawner") == 0) {
            return (int32_t)i;
        }
    }
    return -1;
}

/* A level, a player, a fighter chosen and the behaviour on its first word: the state a player is
 * in from the moment they open the group in a level. */
static void a_level(void)
{
    st.available = true;
    st.behaviour = (uint32_t)SPAWN_BEHAVIOUR_STAND;
    st.chosen    = (int32_t)A_FIGHTER;
    overlay_model_reset();
    overlay_model_set_tab(OVERLAY_TAB_OPENPHANTOM);
    overlay_model_toggle_group((uint32_t)OVERLAY_GROUP_OPENPHANTOM_SPAWN);
    overlay_model_rebuild();
    a_screen();
}

static void a_session_that_runs_no_copies(bool on)
{
    session_note_t note;

    memset(&note, 0, sizeof note);
    note.running = on;
    note.is_host = on;
    (void)session_note_publish(&note);
    session_lock_refresh();
    overlay_model_rebuild();
}

static void test_the_group_a_player_meets(void)
{
    int32_t at;

    ut_section("the spawner group with a level under it");
    a_level();
    at = first_row_of_kind(OVERLAY_ROW_SEGMENT);
    ut_check(at >= 0 && overlay_model_row((uint32_t)at, &row),
             "the row of words is on screen");
    ut_check(strcmp(row.label, "Spawned entities") == 0, "and it is the behaviour row");
    ut_check(row.available && overlay_choice_is_strip(&row),
             "it can be used, so it is DRAWN as a strip of words: the state no other programme "
             "here can reach, because nothing else can stand a spawner up");
    ut_check(row.chosen == (uint32_t)SPAWN_BEHAVIOUR_STAND, "standing on the first of them");
    {
        const char *words[OVERLAY_CHOICE_SEGMENTS_MAX];

        ut_check(overlay_choice_segments(&row, words, OVERLAY_CHOICE_SEGMENTS_MAX) ==
                     (uint32_t)SPAWN_BEHAVIOUR_COUNT,
                 "with one word per behaviour the spawner has");
    }
}

/* The one way in. No row puts a copy down where the player stands, so the placement mode is the
 * only thing here that puts one down. Driven with a level under it, because a usable placement
 * row is the state no other programme here can reach. */
static void test_the_mouse_is_the_only_way_in(void)
{
    uint32_t count;
    uint32_t i;
    int32_t  place  = -1;
    bool     direct = false;

    ut_section("the placement mode is the only way a copy goes down");
    a_session_that_runs_no_copies(false);
    a_level();
    spawn_place_state()->available = true;
    overlay_model_rebuild();
    a_screen();

    count = overlay_model_row_count();
    for (i = 0; i < count; ++i) {
        if (!overlay_model_row(i, &row)) {
            continue;
        }
        if (strncmp(row.label, "Spawn entity", 12) == 0) {
            direct = true;
        }
        if (row.kind == OVERLAY_ROW_ACTION && strcmp(row.label, "Place with the mouse") == 0) {
            place = (int32_t)i;
        }
    }
    ut_check(!direct, "no row of the open tab puts a copy down where the player stands");
    ut_check(place >= 0 && overlay_model_row((uint32_t)place, &row) && row.available,
             "and the placement row is there and can be used, with a level and a kind chosen");

    spawn_place_state()->asked_on = false;
    ut_check(overlay_model_activate((uint32_t)place) && spawn_place_state()->asked_on,
             "pressing it asks for the mode, which is what hides the panel and frees the pointer");

    spawn_place_state()->asked_on  = false;
    spawn_place_state()->asked_off = false;
    spawn_place_state()->available = false;
}

/* Left and Right on a usable row of words. A branch taken out of overlay_keys_sideways() leaves
 * every other programme green: on a row that cannot be used the branch falls through and changes
 * the tab, which is exactly what its absence does. */
static void test_left_and_right_walk_the_words(void)
{
    int32_t at;

    ut_section("Left and Right walk the words, and fall through to the tab at the end");
    a_level();
    at = first_row_of_kind(OVERLAY_ROW_SEGMENT);
    overlay_model_set_selected(at);

    ut_check(overlay_keys_navigate(KEY_RIGHT, true, false) &&
                 overlay_model_tab() == OVERLAY_TAB_OPENPHANTOM,
             "Right takes the key rather than handing it to the tab");
    ut_check(overlay_model_row((uint32_t)at, &row) &&
                 row.chosen == (uint32_t)SPAWN_BEHAVIOUR_FOLLOW,
             "and the row stands on the next word");
    ut_check(npc_spawner_behaviour() == (uint32_t)SPAWN_BEHAVIOUR_FOLLOW,
             "which is the behaviour a copy raised now would run: the row and the spawner are "
             "one value and not two");

    ut_check(overlay_keys_navigate(KEY_LEFT, true, false) &&
                 overlay_model_row((uint32_t)at, &row) &&
                 row.chosen == (uint32_t)SPAWN_BEHAVIOUR_STAND,
             "Left walks back the same way");

    /* To the end of the strip and one press further. A row of words behaves like a heading here:
     * once there is no word left in that direction the key belongs to the tab, which is what lets
     * somebody hold one arrow down and walk the panel rather than stopping on the first row that
     * answers. Walked with Left, because this tab is the right hand one and Right at the end of
     * it has no tab to go to. */
    ut_check(overlay_model_row((uint32_t)at, &row) &&
                 row.chosen == (uint32_t)SPAWN_BEHAVIOUR_STAND,
             "the row is back on the first word");
    ut_check(overlay_keys_navigate(KEY_LEFT, true, false) &&
                 overlay_model_tab() == OVERLAY_TAB_ORIGINAL,
             "and one press further falls through to the tab rather than stopping there");
    ut_check(npc_spawner_behaviour() == (uint32_t)SPAWN_BEHAVIOUR_STAND,
             "with the word left where it was: the press that changed the tab wrote nothing");
}

/* The other hand on the same row. A press lands on one word, which is not a step from wherever
 * the row stands: the pointer says which word it wants. */
static void test_a_press_on_a_word(void)
{
    int32_t at;

    ut_section("a press on one of the words");
    a_level();
    at = first_row_of_kind(OVERLAY_ROW_SEGMENT);
    ut_check(at >= 0 && overlay_model_row((uint32_t)at, &row), "the row of words is on screen");

    ut_check(overlay_choice_pick(&row, (uint32_t)SPAWN_BEHAVIOUR_ATTACK) &&
                 npc_spawner_behaviour() == (uint32_t)SPAWN_BEHAVIOUR_ATTACK,
             "a press on the third word picks the third word, whatever the row was standing on");
    ut_check(!overlay_choice_pick(&row, (uint32_t)SPAWN_BEHAVIOUR_COUNT) &&
                 npc_spawner_behaviour() == (uint32_t)SPAWN_BEHAVIOUR_ATTACK,
             "and a word past the end of the strip picks nothing rather than the last one");
}

/* The row locked while the group around it is not. A pickup runs one script whatever the
 * behaviour says, so the row is greyed and a line under it says why; a key or a press that got
 * through would write a behaviour nothing will run. */
static void test_a_locked_row_of_words(void)
{
    int32_t at;

    ut_section("the row of words locked by the kind that ignores it");
    a_level();
    at = first_row_of_kind(OVERLAY_ROW_SEGMENT);
    overlay_model_set_selected(at);
    ut_check(overlay_choice_pick(&row, (uint32_t)SPAWN_BEHAVIOUR_STAND) ||
                 npc_spawner_behaviour() == (uint32_t)SPAWN_BEHAVIOUR_STAND,
             "the behaviour starts on the first word");

    npc_spawner_choose((int32_t)A_PICKUP);
    overlay_model_rebuild();
    ut_check(overlay_model_row((uint32_t)at, &row) && !row.available,
             "with a pickup chosen the row cannot be used, while the group around it still can");
    ut_check(!overlay_choice_is_strip(&row),
             "so it is not drawn as a strip either: it reads its reason where the words would be");

    ut_check(!overlay_choice_pick(&row, (uint32_t)SPAWN_BEHAVIOUR_ATTACK),
             "a press on a word is refused");
    ut_check(overlay_keys_navigate(KEY_LEFT, true, false) &&
                 overlay_model_tab() == OVERLAY_TAB_ORIGINAL,
             "and the key changes the tab, the way it does on every other row nobody can act on");
    ut_check(npc_spawner_behaviour() == (uint32_t)SPAWN_BEHAVIOUR_STAND,
             "with the behaviour untouched by either of them");

    /* And the whole group taken, which is the other lock and a different rule: a session that
     * does not run the copies hands out no keys for them. */
    overlay_model_set_tab(OVERLAY_TAB_OPENPHANTOM);
    npc_spawner_choose((int32_t)A_FIGHTER);
    a_session_that_runs_no_copies(true);
    at = first_row_of_kind(OVERLAY_ROW_SEGMENT);
    ut_check(at >= 0 && overlay_model_row((uint32_t)at, &row) && !row.available,
             "a session that runs no copies takes the row as well, with a fighter chosen");
    overlay_model_set_selected(at);
    ut_check(!overlay_choice_pick(&row, (uint32_t)SPAWN_BEHAVIOUR_HELP) &&
                 npc_spawner_behaviour() == (uint32_t)SPAWN_BEHAVIOUR_STAND,
             "and a press writes nothing there either");
    a_session_that_runs_no_copies(false);
}

/* The word the heading carries while the group is folded, which is the only thing a folded group
 * says about itself. It is read off the rows, so it is read off the same `available` the paint
 * reads: a band saying Stand over a row the panel has greyed is the two halves of the panel
 * disagreeing about one state, and it said exactly that for every pickup in the game. */
static void test_the_word_on_the_heading(void)
{
    ut_section("the word the folded heading carries");
    a_level();
    ut_check(overlay_model_row((uint32_t)the_heading(), &row) &&
                 strcmp(row.label, "Entity spawner") == 0,
             "the group's heading is on screen");
    ut_check(strcmp(row.value, "Stand") == 0,
             "and it reads the behaviour the group settled on");
    ut_check(!row.on,
             "unlit, because an entry that was chosen is not a switch that was turned on");

    npc_spawner_choose((int32_t)A_PICKUP);
    overlay_model_rebuild();
    ut_check(overlay_model_row((uint32_t)the_heading(), &row) && row.value[0] == '\0',
             "with a pickup chosen the band is EMPTY: the behaviour row is greyed, and a heading "
             "naming a behaviour nothing would run is worse than a heading saying nothing");

    npc_spawner_choose((int32_t)A_FIGHTER);
    overlay_model_rebuild();
    ut_check(overlay_model_row((uint32_t)the_heading(), &row) &&
                 strcmp(row.value, "Stand") == 0,
             "and choosing a fighter again gives the word back");
}

/* The kind list, which is the second choice this group carries. Two things no overlay_dump.c pass
 * holds: an entry that is offered and NOT chosen, and a heading with two choices under it. */
static void test_the_kind_list(void)
{
    uint32_t entries = 0;
    uint32_t marked = 0;
    uint32_t offered = 0;
    uint32_t count;
    uint32_t i;
    int32_t  kind_row;

    ut_section("the list of what can be spawned");
    a_level();
    kind_row = -1;
    count = overlay_model_row_count();
    for (i = 0; i < count; ++i) {
        if (overlay_model_row(i, &row) && row.kind == OVERLAY_ROW_ACTION &&
            strcmp(row.label, "Entity to spawn") == 0) {
            kind_row = (int32_t)i;
            break;
        }
    }
    ut_check(kind_row >= 0, "the row that opens the list is on screen");
    ut_check(overlay_model_activate((uint32_t)kind_row), "and it opens");
    overlay_model_rebuild();

    count = overlay_model_row_count();
    for (i = 0; i < count; ++i) {
        if (!overlay_model_row(i, &row) || row.kind != OVERLAY_ROW_CHOICE) {
            continue;
        }
        ++entries;
        if (row.available) {
            ++offered;
        }
        if (row.on) {
            ++marked;
        }
    }
    ut_check(entries == KIND_COUNT, "every kind the level offers is an entry of the list");
    ut_check(offered == entries,
             "all of them can be used, which is the case no other programme here reaches");
    ut_check(marked == 1u,
             "and exactly one is marked: the rest are entries a player can press and has not, "
             "which is the state no overlay_dump.c pass holds");

    /* Two choices under one heading cannot be one word, so the band goes empty while the list is
     * open. That is the rule and not a gap, and it comes back the moment a kind is picked. */
    ut_check(overlay_model_row((uint32_t)the_heading(), &row) && row.value[0] == '\0',
             "with the list open the heading's band is empty: the group carries two choices now");

    count = overlay_model_row_count();
    for (i = 0; i < count; ++i) {
        if (overlay_model_row(i, &row) && row.kind == OVERLAY_ROW_CHOICE && !row.on) {
            ut_check(overlay_model_activate(i), "an entry that is not the chosen one is pressed");
            break;
        }
    }
    overlay_model_rebuild();
    ut_check(npc_spawner_chosen() != (int32_t)A_FIGHTER,
             "and the spawner is on the kind that was pressed");
    ut_check(first_row_of_kind(OVERLAY_ROW_CHOICE) < 0,
             "with the list shut, because the answer is on the row above it now");
}

int main(void)
{
    test_the_group_a_player_meets();
    test_the_mouse_is_the_only_way_in();
    test_left_and_right_walk_the_words();
    test_a_press_on_a_word();
    test_a_locked_row_of_words();
    test_the_word_on_the_heading();
    test_the_kind_list();
    return ut_summary("the entity spawner group with a spawner behind it");
}
