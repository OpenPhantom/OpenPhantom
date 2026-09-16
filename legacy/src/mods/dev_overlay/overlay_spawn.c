/* overlay_spawn.c: see overlay_spawn.h. */
#include "overlay_spawn.h"

#include "overlay_row_fill.h"
#include "spawn_scripts.h"

#include "common/text.h"

static bool list_open;
static bool behaviour_open;
static bool fold_open;

/* The fold's own text, one row per line, each kept to what fits the panel. */
static const char *const SPAWN_INFO_LINES[OVERLAY_SPAWN_LINE_COUNT] = {
    "The NPC appears when the menu closes,",
    "  three steps ahead of you, facing you",
    "Any actor in the game, the level's own first,",
    "  to stand, follow, attack or help you",
    "Any of them can be struck down and fades",
    "Spawned NPCs are gone with the level;",
    "  a save brings each back as one more",
    "Up to 16 alive at once; each new one",
    "  takes the next free spot around you"
};

/* What a drawn slot is. The list's entries follow the row that opens them, so every row below
 * that one moves down by the list's length while it is open, and the fold's lines follow the
 * summary the same way. The slot numbers in the header are the positions with both shut. */
typedef enum spawn_what {
    SPAWN_RUN, SPAWN_ALIVE, SPAWN_KIND, SPAWN_ENTRY, SPAWN_BEHAVIOUR, SPAWN_BEHAVIOUR_ENTRY,
    SPAWN_REMOVE, SPAWN_SUMMARY, SPAWN_LINE, SPAWN_NOTHING
} spawn_what_t;

static spawn_what_t what_is(uint32_t slot, uint32_t *index)
{
    uint32_t entries    = list_open ? npc_spawner_kind_count() : 0u;
    uint32_t behaviours = behaviour_open ? (uint32_t)SPAWN_BEHAVIOUR_COUNT : 0u;
    uint32_t lines      = fold_open ? OVERLAY_SPAWN_LINE_COUNT : 0u;

    *index = 0;
    if (slot == OVERLAY_SPAWN_RUN_SLOT) {
        return SPAWN_RUN;
    }
    if (slot == OVERLAY_SPAWN_ALIVE_SLOT) {
        return SPAWN_ALIVE;
    }
    if (slot == OVERLAY_SPAWN_KIND_SLOT) {
        return SPAWN_KIND;
    }
    slot -= OVERLAY_SPAWN_KIND_SLOT + 1u;
    if (slot < entries) {
        *index = slot;
        return SPAWN_ENTRY;
    }
    slot -= entries;
    if (slot == 0) {
        return SPAWN_BEHAVIOUR;
    }
    slot -= 1u;
    if (slot < behaviours) {
        *index = slot;
        return SPAWN_BEHAVIOUR_ENTRY;
    }
    slot -= behaviours;
    switch (slot) {
    case 0:  return SPAWN_REMOVE;
    case 1:  return SPAWN_SUMMARY;
    default: break;
    }
    slot -= 2u;
    if (slot < lines) {
        *index = slot;
        return SPAWN_LINE;
    }
    return SPAWN_NOTHING;
}

uint32_t overlay_spawn_row_count(void)
{
    return OVERLAY_SPAWN_FIXED_ROWS + (list_open ? npc_spawner_kind_count() : 0u) +
           (behaviour_open ? (uint32_t)SPAWN_BEHAVIOUR_COUNT : 0u) +
           (fold_open ? OVERLAY_SPAWN_LINE_COUNT : 0u);
}

void overlay_spawn_reset(void)
{
    list_open      = false;
    behaviour_open = false;
    fold_open      = false;
}

/* The chip on the kind row: the chosen actor file's stem, or None. */
static void chosen_name(char *out, uint32_t out_size)
{
    npc_spawner_kind_t kind;
    int32_t            chosen = npc_spawner_chosen();

    if (chosen < 0 || !npc_spawner_kind((uint32_t)chosen, &kind)) {
        text_format(out, out_size, "None");
        return;
    }
    text_format(out, out_size, "%s", kind.name);
}

void overlay_spawn_row(uint32_t slot, overlay_row_t *out)
{
    uint32_t index;

    if (out == NULL) {
        return;
    }
    overlay_row_defaults(out);
    switch (what_is(slot, &index)) {
    case SPAWN_RUN:
        /* A plain RUN, so the press reads as a press; the count lives on the note below. */
        out->kind = OVERLAY_ROW_ACTION;
        overlay_row_label(out->label, "Spawn NPC (close menu to take effect)");
        out->available = npc_spawner_is_available() && npc_spawner_chosen() >= 0 &&
                         npc_spawner_alive() < NPC_SPAWNER_ALIVE_MAX;
        return;

    case SPAWN_ALIVE: {
        /* The spawns alive against the cap, so a spawn row that has just gone grey says why. */
        char line[OVERLAY_LABEL_MAX];

        out->kind = OVERLAY_ROW_INFO;
        text_format(line, sizeof line, "    %u of %u spawned NPCs alive", npc_spawner_alive(),
                    NPC_SPAWNER_ALIVE_MAX);
        overlay_row_label(out->label, line);
        out->available = false;   /* a note, never clicked */
        return;
    }

    case SPAWN_KIND:
        /* Opens the list; the chip shows the choice, as the level group's start row does. */
        out->kind = OVERLAY_ROW_ACTION;
        overlay_row_label(out->label, list_open ? "NPC to spawn (pick one)" : "NPC to spawn");
        out->available = npc_spawner_is_available() && npc_spawner_kind_count() > 0u;
        chosen_name(out->value, sizeof out->value);
        return;

    case SPAWN_BEHAVIOUR:
        /* Opens the scripts of this project's own; the chip names the one in force. */
        out->kind = OVERLAY_ROW_ACTION;
        overlay_row_label(out->label, behaviour_open ? "Spawned NPCs (pick one)"
                                                     : "Spawned NPCs");
        out->available = npc_spawner_is_available();
        text_format(out->value, sizeof out->value, "%s",
                    spawn_behaviour_name((spawn_behaviour_t)npc_spawner_behaviour()));
        return;

    case SPAWN_BEHAVIOUR_ENTRY: {
        static const char *const WHAT[SPAWN_BEHAVIOUR_COUNT] = {
            "stand, and turn to face you",
            "follow you, stopping close by",
            "swing at you, or shoot from afar",
            "follow you and fight your enemies"
        };
        char line[OVERLAY_LABEL_MAX];

        out->kind = OVERLAY_ROW_CHEAT;
        out->available = npc_spawner_is_available();
        out->on = (npc_spawner_behaviour() == index);
        text_format(line, sizeof line, "    %-7s %s",
                    spawn_behaviour_name((spawn_behaviour_t)index), WHAT[index]);
        overlay_row_label(out->label, line);
        return;
    }

    case SPAWN_REMOVE:
        out->kind = OVERLAY_ROW_ACTION;
        overlay_row_label(out->label, "Remove spawned NPCs");
        out->available = npc_spawner_is_available() && npc_spawner_alive() > 0u;
        return;

    case SPAWN_SUMMARY:
        /* A fold on one row, the free camera's shape: the marker is a character in the label. */
        out->kind = OVERLAY_ROW_INFO;
        overlay_row_label(out->label, fold_open ? "- About spawned NPCs" : "+ About spawned NPCs");
        out->expanded = fold_open;
        return;

    case SPAWN_LINE: {
        char line[OVERLAY_LABEL_MAX];

        out->kind = OVERLAY_ROW_INFO;
        text_format(line, sizeof line, "    %s", SPAWN_INFO_LINES[index]);
        overlay_row_label(out->label, line);
        return;    /* a note, never clicked */
    }

    case SPAWN_ENTRY: {
        npc_spawner_kind_t kind;
        char               line[OVERLAY_LABEL_MAX];

        out->kind = OVERLAY_ROW_CHEAT;
        if (!npc_spawner_kind(index, &kind)) {
            overlay_row_label(out->label, "");
            out->available = false;
            return;
        }
        out->available = npc_spawner_is_available();
        out->on = (npc_spawner_chosen() == (int32_t)index);
        /* The stem, then how many the level placed of it, which says at a glance whether this
         * is the level's crowd or its one boss; a file from the archive says so instead. */
        if (kind.foreign) {
            text_format(line, sizeof line, "    %-15s from the archive", kind.name);
        } else {
            text_format(line, sizeof line, "    %-15s %u placed", kind.name, kind.placements);
        }
        overlay_row_label(out->label, line);
        return;
    }

    case SPAWN_NOTHING:
    default:
        overlay_row_label(out->label, "");    /* past the end; a blank shows the caller's bug */
        out->available = false;
        return;
    }
}

bool overlay_spawn_toggle(uint32_t slot)
{
    uint32_t index;

    switch (what_is(slot, &index)) {
    case SPAWN_RUN:
        return npc_spawner_spawn();
    case SPAWN_ALIVE:
        return false;
    case SPAWN_KIND:
        list_open      = !list_open;
        behaviour_open = false;   /* one list at a time */
        if (list_open) {
            npc_spawner_refresh();   /* counted again, in case the level changed in place */
        }
        return true;
    case SPAWN_BEHAVIOUR:
        behaviour_open = !behaviour_open;
        list_open      = false;
        return true;
    case SPAWN_BEHAVIOUR_ENTRY:
        /* Picking closes the list, so the chip reads back the choice at once. */
        npc_spawner_set_behaviour(index);
        behaviour_open = false;
        return true;
    case SPAWN_REMOVE:
        return npc_spawner_remove_all() != 0u;
    case SPAWN_SUMMARY:
        fold_open = !fold_open;
        return true;
    case SPAWN_ENTRY:
        /* Picking closes the list, so the choice reads back on the row above at once. */
        npc_spawner_choose((int32_t)index);
        list_open = false;
        return true;
    case SPAWN_LINE:
    case SPAWN_NOTHING:
    default:
        return false;
    }
}
