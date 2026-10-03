/* overlay_spawn.c: see overlay_spawn.h. */
#include "overlay_spawn.h"

#include "entity_names.h"
#include "entity_offer.h"
#include "npc_spawn_link.h"
#include "overlay_key_name.h"
#include "overlay_reason.h"
#include "overlay_row_fill.h"
#include "session_lock.h"
#include "spawn_keys.h"
#include "spawn_place.h"
#include "spawn_reason.h"
#include "spawn_scripts.h"

#include "common/text.h"

#include <stdint.h>
#include <string.h>

static bool list_open;
static bool fold_open;

/* Whether anything in this group can be used at all. It is the shared rule's answer, taken once a
 * rebuild rather than per row, because the group draws up to two hundred of them and the rule asks
 * the multiplayer's note. Every row then adds only what IT needs on top; none of them spells this
 * part out for itself, which is how the mode and the rows came to disagree in the first place. */
static bool group_usable;

static void read_the_group(void)
{
    spawn_reason_facts_t facts;

    /* The four the group's half of the rule reads, written by spawn_reason.c rather than
     * listed here: the placement mode gathers the same four for the sentence this band's chip
     * points at, and two lists of one state are the pair that drifts. */
    spawn_reason_group_facts(&facts, session_lock_holds_the_spawner(), npc_spawn_link_active(),
                             npc_spawner_is_available(), npc_spawner_kind_count());
    group_usable = spawn_reason_why(&facts, SPAWN_REASON_GROUP) == NULL;
}

/* The kind list's rows while it is open: a kind's index, or a shelf's heading written as -1 minus
 * the shelf. Built once a rebuild, when the group is counted, because every row is asked for once a
 * frame and a walk of the kinds per row would be a walk per row of every row. */
static int32_t  list_row[OVERLAY_SPAWN_LIST_MAX];
static uint32_t list_rows;

/* The fold's own text, one row per line, each kept to what fits the panel. */
static const char *const SPAWN_INFO_LINES[OVERLAY_SPAWN_LINE_COUNT] = {
    "Place with the mouse: the menu hides and",
    "  the entity follows the pointer",
    "Left click places, again and again;",
    "  the wheel turns it, Shift fine, Ctrl 90",
    "Right click removes the copy under it,",
    "  not yet on a client; Esc shows the menu",
    "Any safe actor in the game, the level's own",
    "  first, then figures, creatures, pickups",
    "Stand turns to face you; Help follows you",
    "Pickups and the tripod ignore the behaviour",
    "Spawned entities go with the level;",
    "  a save keeps them where they stand",
    "Up to 16 alive at once in single player",
    "In a session the host decides how many.",
    "  A pickup the host takes is gone for all;",
    "  one a client takes, for that client only"
};

/* What a drawn slot is. The fixed rows come first, so the two key rows keep their slot whatever
 * opens below them: a key is captured against the row it was clicked on, and a row that moved
 * between the click and the key would bind the other key. The lists follow the rows that open them
 * and push everything below down by their length, and the fold's lines follow the summary. */
typedef enum spawn_what {
    SPAWN_ALIVE, SPAWN_PLACE, SPAWN_PLACE_KEY, SPAWN_FACE_KEY, SPAWN_WHY, SPAWN_REFUSED,
    SPAWN_KIND, SPAWN_HEADING, SPAWN_ENTRY, SPAWN_BEHAVIOUR, SPAWN_BEHAVIOUR_NOTE, SPAWN_REMOVE,
    SPAWN_REMOVE_ALL, SPAWN_SUMMARY, SPAWN_LINE, SPAWN_NOTHING
} spawn_what_t;

/* Under the keys, while something stands in the way of the group. The placement mode works the
 * sentence out once a frame, before the panel is rebuilt, and the row repeats it: the greyed rows
 * and the log then cannot give a player two different reasons for one state. */
static const char *why_not_usable(void)
{
    return spawn_place_state()->unavailable;
}

static uint32_t why_rows(void)
{
    return why_not_usable() != NULL ? OVERLAY_SPAWN_WHY_ROWS : 0u;
}

/* Under that, while this player's last wish stands refused, in a session or here. */
static uint32_t refusal_rows(void)
{
    return npc_spawn_link_refusal() != NULL ? OVERLAY_SPAWN_REFUSAL_ROWS : 0u;
}

/* The host of a session that runs the copies removes everybody's on a row of its own. */
static uint32_t host_rows(void)
{
    return npc_spawn_link_active() && !npc_spawn_link_is_client() ? OVERLAY_SPAWN_HOST_ROWS : 0u;
}

static void build_list(void)
{
    uint32_t kinds = npc_spawner_kind_count();
    int32_t  shelf = -1;
    uint32_t k;

    list_rows = 0;
    for (k = 0; k < kinds && list_rows + 2u <= OVERLAY_SPAWN_LIST_MAX; ++k) {
        npc_spawner_kind_t kind;

        if (!npc_spawner_kind(k, &kind)) {
            continue;
        }
        if ((int32_t)kind.section != shelf) {
            shelf = (int32_t)kind.section;
            list_row[list_rows++] = -1 - shelf;
        }
        list_row[list_rows++] = (int32_t)k;
    }
}

static spawn_what_t what_is(uint32_t slot, uint32_t *index)
{
    static const spawn_what_t FIXED[] = { SPAWN_ALIVE, SPAWN_PLACE, SPAWN_PLACE_KEY,
                                          SPAWN_FACE_KEY };
    uint32_t entries    = list_open ? list_rows : 0u;
    uint32_t lines      = fold_open ? OVERLAY_SPAWN_LINE_COUNT : 0u;
    uint32_t host       = host_rows();
    uint32_t why        = why_rows();
    uint32_t refused    = refusal_rows();

    *index = 0;
    if (slot < sizeof FIXED / sizeof FIXED[0]) {
        return FIXED[slot];
    }
    slot -= (uint32_t)(sizeof FIXED / sizeof FIXED[0]);
    if (slot < why) {
        return SPAWN_WHY;
    }
    slot -= why;
    if (slot < refused) {
        return SPAWN_REFUSED;
    }
    slot -= refused;
    if (slot == 0) {
        return SPAWN_KIND;
    }
    slot -= 1u;
    if (slot < entries) {
        *index = (list_row[slot] < 0) ? (uint32_t)(-1 - list_row[slot]) : (uint32_t)list_row[slot];
        return (list_row[slot] < 0) ? SPAWN_HEADING : SPAWN_ENTRY;
    }
    slot -= entries;
    if (slot == 0) {
        return SPAWN_BEHAVIOUR;
    }
    slot -= 1u;
    if (slot == 0) {
        return SPAWN_BEHAVIOUR_NOTE;
    }
    slot -= 1u;
    if (slot == 0) {
        return SPAWN_REMOVE;
    }
    slot -= 1u;
    if (slot < host) {
        return SPAWN_REMOVE_ALL;
    }
    slot -= host;
    if (slot == 0) {
        return SPAWN_SUMMARY;
    }
    slot -= 1u;
    if (slot < lines) {
        *index = slot;
        return SPAWN_LINE;
    }
    return SPAWN_NOTHING;
}

uint32_t overlay_spawn_row_count(void)
{
    read_the_group();   /* once a rebuild, before any row of it is built */
    if (list_open) {
        build_list();
    }
    return OVERLAY_SPAWN_FIXED_ROWS + host_rows() + why_rows() + refusal_rows() +
           (list_open ? list_rows : 0u) +
           (fold_open ? OVERLAY_SPAWN_LINE_COUNT : 0u);
}

void overlay_spawn_reset(void)
{
    list_open = false;
    fold_open = false;
}

/* The chosen kind, or false for none. */
static bool chosen_kind(npc_spawner_kind_t *out)
{
    int32_t chosen = npc_spawner_chosen();

    return chosen >= 0 && npc_spawner_kind((uint32_t)chosen, out);
}

/* The words a kind is shown by: the name a person reads and the stem behind it, or the stem. */
static void kind_words(const npc_spawner_kind_t *kind, bool with_stem, char *out,
                       uint32_t out_size)
{
    const char *name = entity_name_of(kind->file);

    if (name == NULL) {
        text_format(out, out_size, "%s", kind->name);
    } else if (with_stem) {
        text_format(out, out_size, "%s (%s)", name, kind->name);
    } else {
        text_format(out, out_size, "%s", name);
    }
}

/* A pickup and the gun run one script whatever the behaviour row says (spawn_scripts.c), so for
 * those the row reads n/a rather than promising something that will not happen. */
static bool behaviour_is_ignored(void)
{
    npc_spawner_kind_t kind;

    return chosen_kind(&kind) && (kind.section == ENTITY_SECTION_PICKUPS ||
                                  kind.section == ENTITY_SECTION_GUNS);
}

static void key_chip(spawn_key_t which, bool capturing, overlay_row_t *out)
{
    int32_t vk = spawn_keys_get(which);

    if (capturing) {
        text_format(out->value, sizeof out->value, "...");
    } else if (vk != 0) {
        overlay_key_name(vk, out->value, sizeof out->value);
    } else {
        text_format(out->value, sizeof out->value, "Set");
    }
}

static void alive_row(overlay_row_t *out)
{
    char line[OVERLAY_LABEL_MAX];

    out->kind = OVERLAY_ROW_INFO;
    if (npc_spawn_link_active() && npc_spawn_link_cap() == 0u) {
        text_format(line, sizeof line, "    %u spawned entities alive, the host's to allow",
                    npc_spawner_alive());
    } else {
        text_format(line, sizeof line, "    %u of %u spawned entities alive", npc_spawner_alive(),
                    npc_spawn_link_active() ? npc_spawn_link_cap() : NPC_SPAWNER_ALIVE_MAX);
    }
    overlay_row_label(out->label, line);
    out->available = false;   /* a note, never clicked */
}

static void entry_row(uint32_t index, overlay_row_t *out)
{
    npc_spawner_kind_t kind;
    char               words[OVERLAY_LABEL_MAX];
    char               line[OVERLAY_LABEL_MAX];

    /* One entry of a list, not a switch: the mark says which kind is the one to spawn, and a
     * green ON would have said this kind was turned on. */
    out->kind = OVERLAY_ROW_CHOICE;
    if (!npc_spawner_kind(index, &kind)) {
        overlay_row_label(out->label, "");
        out->available = false;
        return;
    }
    out->available = group_usable;
    out->on = (npc_spawner_chosen() == (int32_t)index);
    /* The name and the stem, which a search finds either way, and for the level's own kinds how
     * many it placed, which says at a glance whether this is the level's crowd or its one boss. */
    kind_words(&kind, true, words, sizeof words);
    if (kind.foreign) {
        text_format(line, sizeof line, "    %s", words);
    } else {
        text_format(line, sizeof line, "    %s x%u", words, kind.placements);
    }
    overlay_row_label(out->label, line);
}

/* The four behaviours on one row, the chosen one filled, instead of a fold with a row each.
 *
 * They were four rows of a list, each drawn as a switch, which said that four things could be
 * switched on when only one of them can. Four words fit beside the name with characters to spare,
 * so the choice is shown whole and there is nothing to open.
 *
 * What that cost: the four rows carried a sentence each, saying what the behaviour does. Three of
 * the four say it in their own word. The fourth pair does not, because Attack and Help are both
 * fighting and the difference is who is fought, so that one difference is written under the row
 * and the other three sentences are gone. */
static void behaviour_row(overlay_row_t *out)
{
    out->kind      = OVERLAY_ROW_SEGMENT;
    out->available = group_usable && !behaviour_is_ignored();
    out->chosen    = npc_spawner_behaviour();
    overlay_row_label(out->label, "Spawned entities");
    /* No chip. An unavailable row still reads its reason, which for a pickup is the plain `n/a`
     * it has always read, and an available one has its words and needs no second word beside
     * them saying the same thing (overlay_choice.h). */
}

/* The rows that act or open something. */
static void action_row(spawn_what_t what, overlay_row_t *out)
{
    npc_spawner_kind_t kind;

    out->kind = OVERLAY_ROW_ACTION;
    switch (what) {
    case SPAWN_PLACE:
        /* The frame asks the engine side whether it can place and says so here a frame later;
         * the chip names what would be placed. */
        overlay_row_label(out->label, "Place with the mouse");
        out->available = group_usable && npc_spawner_chosen() >= 0 &&
                         spawn_place_state()->available;
        if (chosen_kind(&kind)) {
            kind_words(&kind, false, out->value, sizeof out->value);
        }
        return;
    case SPAWN_KIND:
        overlay_row_label(out->label, list_open ? "Entity to spawn (pick one)" : "Entity to spawn");
        out->available = group_usable;   /* the rule already asked for a kind to offer */
        if (chosen_kind(&kind)) {
            kind_words(&kind, false, out->value, sizeof out->value);
        } else {
            text_format(out->value, sizeof out->value, "None");
        }
        return;
    case SPAWN_REMOVE:
        /* In a session this player's own, which may stand on another machine only: the host
         * finds them, so the row does not wait for one standing here. */
        overlay_row_label(out->label, npc_spawn_link_active() ? "Remove your spawned entities"
                                                              : "Remove spawned entities");
        out->available = group_usable &&
                         (npc_spawn_link_active() || npc_spawner_alive() > 0u);
        return;
    case SPAWN_REMOVE_ALL:
    default:
        overlay_row_label(out->label, "Remove every player's spawned entities");
        out->available = group_usable;
        return;
    }
}

void overlay_spawn_row(uint32_t slot, bool capturing, overlay_row_t *out)
{
    uint32_t     index;
    spawn_what_t what;
    char         line[OVERLAY_LABEL_MAX];

    if (out == NULL) {
        return;
    }
    overlay_row_defaults(out);
    /* One place for the whole group. A row of this group that cannot be used while the group
     * itself cannot be used is refused for the group's reason, and that reason is a sentence the
     * group writes into itself, a few rows above. Read only when the row says it is unavailable,
     * so a row that is fine ignores it, and a row refused for something of its own while the
     * group is usable keeps the plain `n/a` it always had. */
    if (!group_usable) {
        out->reason = (uint32_t)OVERLAY_REASON_SPAWNER;
    }
    what = what_is(slot, &index);
    switch (what) {
    case SPAWN_ALIVE:
        alive_row(out);
        return;
    case SPAWN_PLACE_KEY:
    case SPAWN_FACE_KEY:
        /* Settings in the file, so available whatever resolved; the mode says why it cannot run. */
        out->kind = OVERLAY_ROW_HOTKEY;
        overlay_row_label(out->label, what == SPAWN_PLACE_KEY ? "    Key: place with the mouse"
                                                              : "    Key: turn to face you");
        key_chip(what == SPAWN_PLACE_KEY ? SPAWN_KEY_PLACE : SPAWN_KEY_FACE, capturing, out);
        return;
    case SPAWN_WHY:
        out->kind = OVERLAY_ROW_INFO;
        text_format(line, sizeof line, "    Why: %s",
                    why_not_usable() != NULL ? why_not_usable() : "");
        overlay_row_label(out->label, line);
        out->available = false;   /* a note, never clicked */
        return;
    case SPAWN_REFUSED:
        out->kind = OVERLAY_ROW_INFO;
        text_format(line, sizeof line, "    Refused: %s",
                    npc_spawn_link_refusal() != NULL ? npc_spawn_link_refusal() : "");
        overlay_row_label(out->label, line);
        out->available = false;   /* a note, never clicked */
        out->warn      = true;    /* a refusal, and the only note of this group that is one */
        return;
    case SPAWN_HEADING:
        out->kind = OVERLAY_ROW_INFO;
        text_format(line, sizeof line, "  %s", entity_section_title((entity_section_t)index));
        overlay_row_label(out->label, line);
        out->available = false;   /* a heading, never clicked */
        return;
    case SPAWN_ENTRY:
        entry_row(index, out);
        return;
    case SPAWN_BEHAVIOUR:
        behaviour_row(out);
        return;
    case SPAWN_BEHAVIOUR_NOTE:
        out->kind = OVERLAY_ROW_INFO;
        /* Both verbs carry their object. It read "Help follows and fights", and standing behind
         * "Attack fights you" that second verb reads as fighting YOU as well, which is the one
         * thing this line exists to tell apart. Help's following went into the fold, which has
         * the room for it; see SPAWN_INFO_LINES. */
        overlay_row_label(out->label, "    Attack fights you; Help fights for you");
        out->available = false;   /* a note, never clicked */
        return;
    case SPAWN_SUMMARY:
        /* A fold on one row, the free camera's shape: the marker is a character in the label. */
        out->kind = OVERLAY_ROW_INFO;
        overlay_row_label(out->label, fold_open ? "- About spawned entities"
                                                : "+ About spawned entities");
        out->expanded = fold_open;
        return;
    case SPAWN_LINE:
        out->kind = OVERLAY_ROW_INFO;
        text_format(line, sizeof line, "    %s", SPAWN_INFO_LINES[index]);
        overlay_row_label(out->label, line);
        return;    /* a note, never clicked */
    case SPAWN_NOTHING:
        overlay_row_label(out->label, "");    /* past the end; a blank shows the caller's bug */
        out->available = false;
        return;
    default:
        action_row(what, out);
        return;
    }
}

uint32_t overlay_spawn_segments(uint32_t slot, const char **out, uint32_t max)
{
    uint32_t index;
    uint32_t i;

    if (out == NULL || what_is(slot, &index) != SPAWN_BEHAVIOUR) {
        return 0;
    }
    if (max > (uint32_t)SPAWN_BEHAVIOUR_COUNT) {
        max = (uint32_t)SPAWN_BEHAVIOUR_COUNT;
    }
    for (i = 0; i < max; ++i) {
        out[i] = spawn_behaviour_name((spawn_behaviour_t)i);
    }
    return max;
}

/* The row says whether it can be used (behaviour_row above: the group is usable and the chosen
 * kind does not ignore the behaviour), and overlay_choice_pick() refuses a row that says it
 * cannot. That is the entire gate and there is no second one here to keep in step with it; this
 * asked behaviour_is_ignored() a second time and WITHOUT the group, so the two were already one
 * state with two answers. Left is what only this function can know: that the slot is the
 * behaviour row at all, and that the word exists. */
bool overlay_spawn_choose(uint32_t slot, uint32_t index)
{
    uint32_t at;

    if (what_is(slot, &at) != SPAWN_BEHAVIOUR || index >= (uint32_t)SPAWN_BEHAVIOUR_COUNT) {
        return false;
    }
    npc_spawner_set_behaviour(index);
    return true;
}

bool overlay_spawn_bind(uint32_t slot, int32_t virtual_key)
{
    uint32_t     index;
    spawn_what_t what = what_is(slot, &index);

    if (what == SPAWN_PLACE_KEY) {
        return spawn_keys_bind(SPAWN_KEY_PLACE, virtual_key);
    }
    if (what == SPAWN_FACE_KEY) {
        return spawn_keys_bind(SPAWN_KEY_FACE, virtual_key);
    }
    return false;
}

bool overlay_spawn_toggle(uint32_t slot)
{
    uint32_t index;

    switch (what_is(slot, &index)) {
    case SPAWN_PLACE:
        /* Asked here, taken by the frame, which hides the panel and frees the pointer. */
        spawn_place_ask(spawn_place_state(), true);
        return true;
    case SPAWN_KIND:
        list_open = !list_open;
        if (list_open) {
            npc_spawner_refresh();   /* counted again, in case the level changed in place */
        }
        return true;
    case SPAWN_REMOVE:
        /* In a session a wish for this player's own copies, under the group's own hold: a
         * click that got past the lock on an older picture asks nothing. */
        if (session_lock_holds_the_spawner()) {
            return false;
        }
        if (npc_spawn_link_active()) {
            return npc_spawn_link_remove(false);
        }
        return npc_spawner_remove_all() != 0u;
    case SPAWN_REMOVE_ALL:
        return npc_spawn_link_remove(true);
    case SPAWN_SUMMARY:
        fold_open = !fold_open;
        return true;
    case SPAWN_ENTRY:
        /* Picking closes the list, so the choice reads back on the row above at once. */
        npc_spawner_choose((int32_t)index);
        list_open = false;
        return true;
    default:
        return false;   /* the notes, the headings, the lines and the keys, which the model binds */
    }
}
