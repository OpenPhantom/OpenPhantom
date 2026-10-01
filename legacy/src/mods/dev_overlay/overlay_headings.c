/* overlay_headings.c: see overlay_headings.h. */
#include "overlay_headings.h"

#include <stddef.h>
#include <stdint.h>

static const overlay_tab_t GROUP_TAB[OVERLAY_GROUP_COUNT] = {
    OVERLAY_TAB_ORIGINAL,      /* OVERLAY_GROUP_ORIGINAL_TOGGLES */
    OVERLAY_TAB_ORIGINAL,      /* OVERLAY_GROUP_ORIGINAL_ACTIONS */
    OVERLAY_TAB_OPENPHANTOM,   /* OVERLAY_GROUP_OPENPHANTOM      */
    OVERLAY_TAB_OPENPHANTOM,   /* OVERLAY_GROUP_OPENPHANTOM_LEVELS    */
    OVERLAY_TAB_OPENPHANTOM,   /* OVERLAY_GROUP_OPENPHANTOM_SPAWN     */
    OVERLAY_TAB_OPENPHANTOM,   /* OVERLAY_GROUP_OPENPHANTOM_FREECAM   */
    OVERLAY_TAB_OPENPHANTOM,   /* OVERLAY_GROUP_OPENPHANTOM_DISMEMBER */
    OVERLAY_TAB_OPENPHANTOM,   /* OVERLAY_GROUP_OPENPHANTOM_MODELSWAP */
    OVERLAY_TAB_OPENPHANTOM,   /* OVERLAY_GROUP_OPENPHANTOM_UTILITIES */
    OVERLAY_TAB_OPENPHANTOM,   /* OVERLAY_GROUP_OPENPHANTOM_MENU_EXTRAS */
    OVERLAY_TAB_OPENPHANTOM,   /* OVERLAY_GROUP_OPENPHANTOM_PICTURE   */
    OVERLAY_TAB_OPENPHANTOM,   /* OVERLAY_GROUP_OPENPHANTOM_FOG       */
    OVERLAY_TAB_OPENPHANTOM,   /* OVERLAY_GROUP_OPENPHANTOM_CONTROLS  */
    OVERLAY_TAB_OPENPHANTOM,   /* OVERLAY_GROUP_OPENPHANTOM_WINDOW    */
    OVERLAY_TAB_OPENPHANTOM,   /* OVERLAY_GROUP_OPENPHANTOM_FRAMERATE */
    OVERLAY_TAB_OPENPHANTOM    /* OVERLAY_GROUP_OPENPHANTOM_MULTIPLAYER */
};

/* The order the headings are drawn in, and it is this table rather than the enum.
 *
 * The three at the top are the three used while somebody is standing in a level, which is where
 * this panel is opened from; the level selection and everything that is set once sit below them.
 * The enum keeps the order it grew in, because a row id is derived from it and the ids are what
 * the typed value and the hotkey capture remember a row by.
 *
 * The multiplayer's heading stands directly under the controls. Its one row is the key that opens
 * the chat, and a player looking for a key looks beside the controls. The row was the third one
 * under Menus before it had a heading of its own, and a heading that says neither chat nor key,
 * folded and second from the bottom, is where it was not found. */
static const overlay_group_t DRAW_ORDER[] = {
    OVERLAY_GROUP_ORIGINAL_TOGGLES,
    OVERLAY_GROUP_ORIGINAL_ACTIONS,
    OVERLAY_GROUP_OPENPHANTOM,
    OVERLAY_GROUP_OPENPHANTOM_FREECAM,
    OVERLAY_GROUP_OPENPHANTOM_SPAWN,
    OVERLAY_GROUP_OPENPHANTOM_MODELSWAP,
    OVERLAY_GROUP_OPENPHANTOM_LEVELS,
    OVERLAY_GROUP_OPENPHANTOM_PICTURE,
    OVERLAY_GROUP_OPENPHANTOM_CONTROLS,
    OVERLAY_GROUP_OPENPHANTOM_MULTIPLAYER,
    OVERLAY_GROUP_OPENPHANTOM_WINDOW,
    OVERLAY_GROUP_OPENPHANTOM_FRAMERATE,
    OVERLAY_GROUP_OPENPHANTOM_UTILITIES,
    OVERLAY_GROUP_OPENPHANTOM_DISMEMBER
};

/* Which heading a source is drawn under. A source naming itself has a heading of its own; the two
 * that name another one have no heading and their rows follow that one's, inside its fold.
 *
 * The fog belongs under the engine's heading because one of its rows, "Fog follows the draw
 * distance", points at a number in that group; a reader following that sentence had to leave the
 * group to find what it meant. The game's own screens belong beside this panel's own rows because
 * both answer one question, which is where the settings of this patch turn up.
 *
 * Their rows are appended after the heading group's own rather than woven into them, because the
 * order inside a source is that source's slot numbering and the session lock's lists are written
 * against it. */
static const overlay_group_t DRAWN_UNDER[OVERLAY_GROUP_COUNT] = {
    OVERLAY_GROUP_ORIGINAL_TOGGLES,
    OVERLAY_GROUP_ORIGINAL_ACTIONS,
    OVERLAY_GROUP_OPENPHANTOM,
    OVERLAY_GROUP_OPENPHANTOM_LEVELS,
    OVERLAY_GROUP_OPENPHANTOM_SPAWN,
    OVERLAY_GROUP_OPENPHANTOM_FREECAM,
    OVERLAY_GROUP_OPENPHANTOM_DISMEMBER,
    OVERLAY_GROUP_OPENPHANTOM_MODELSWAP,
    OVERLAY_GROUP_OPENPHANTOM_UTILITIES,
    OVERLAY_GROUP_OPENPHANTOM_UTILITIES,   /* the game's own screens, under this panel's rows */
    OVERLAY_GROUP_OPENPHANTOM_PICTURE,
    OVERLAY_GROUP_OPENPHANTOM_PICTURE,     /* the fog, under the engine's settings */
    OVERLAY_GROUP_OPENPHANTOM_CONTROLS,
    OVERLAY_GROUP_OPENPHANTOM_WINDOW,
    OVERLAY_GROUP_OPENPHANTOM_FRAMERATE,
    OVERLAY_GROUP_OPENPHANTOM_MULTIPLAYER
};

#define DRAW_ORDER_COUNT (sizeof DRAW_ORDER / sizeof DRAW_ORDER[0])

/* Every source is either a heading of its own or drawn under one, and every heading is drawn
 * once. The count is what the assert can reach: two sources are merged, so there are two fewer
 * headings than sources. */
_Static_assert(DRAW_ORDER_COUNT + 2u == (size_t)OVERLAY_GROUP_COUNT,
               "a group was added without being given a place in the drawn order");

/* The titles name what a row under them changes, not the DLL that reads it. Three of them used to
 * be the DLL: a player looking for the field of view found "Enhanced resolution", which holds no
 * resolution at all. The two sources with no title have no heading either; their rows are drawn
 * under the group named beside them in DRAWN_UNDER. */
static const char *const TITLE[OVERLAY_GROUP_COUNT] = {
    [OVERLAY_GROUP_ORIGINAL_TOGGLES] = "Original cheats",
    [OVERLAY_GROUP_ORIGINAL_ACTIONS] = "Original cheats (one-time effects)",
    /* Split by what a row does rather than by what reads it, following the retail half of this
     * panel, which already separates its own toggles from its one-time effects. The tab began as
     * five cheats with a settings row appended and settings kept arriving, until a reader had to
     * scroll past invincibility to reach the draw distance. */
    [OVERLAY_GROUP_OPENPHANTOM]            = "Cheats",
    [OVERLAY_GROUP_OPENPHANTOM_LEVELS]     = "Level selection",
    [OVERLAY_GROUP_OPENPHANTOM_SPAWN]      = "Entity spawner",
    [OVERLAY_GROUP_OPENPHANTOM_FREECAM]    = "Free camera",
    [OVERLAY_GROUP_OPENPHANTOM_DISMEMBER]  = "Dismemberment",
    /* What the player asks is what they look like, not that a model is being swapped. */
    [OVERLAY_GROUP_OPENPHANTOM_MODELSWAP]  = "Appearance",
    /* This panel's own rows and the game's own screens: the size of this panel, the key that
     * opens it, and where the settings of this patch appear. */
    [OVERLAY_GROUP_OPENPHANTOM_UTILITIES]  = "Menus",
    /* The draw distance and its two gates, the field of view, the subtitle size and the fog:
     * how the engine draws the world. Engine names that, and it is no DLL's name either, since
     * three DLLs read the rows under it. The group keeps its inner name, the picture, because
     * its slots and its row ids are written against that name. */
    [OVERLAY_GROUP_OPENPHANTOM_PICTURE]    = "Engine",
    [OVERLAY_GROUP_OPENPHANTOM_CONTROLS]   = "Controls",
    [OVERLAY_GROUP_OPENPHANTOM_WINDOW]     = "Window",
    [OVERLAY_GROUP_OPENPHANTOM_FRAMERATE]  = "Frame rate",
    /* The multiplayer's settings that belong to this machine alone, the chat's key today. */
    [OVERLAY_GROUP_OPENPHANTOM_MULTIPLAYER] = "Multiplayer"
};

overlay_tab_t overlay_headings_tab(overlay_group_t group)
{
    return ((uint32_t)group < (uint32_t)OVERLAY_GROUP_COUNT) ? GROUP_TAB[group]
                                                             : OVERLAY_TAB_COUNT;
}

uint32_t overlay_headings_count(void)
{
    return (uint32_t)DRAW_ORDER_COUNT;
}

overlay_group_t overlay_headings_at(uint32_t index)
{
    return (index < (uint32_t)DRAW_ORDER_COUNT) ? DRAW_ORDER[index] : OVERLAY_GROUP_COUNT;
}

overlay_group_t overlay_headings_drawn_under(overlay_group_t group)
{
    return ((uint32_t)group < (uint32_t)OVERLAY_GROUP_COUNT) ? DRAWN_UNDER[group] : group;
}

/* Two headings have one other source each today, and the loop costs a walk of the enum per
 * heading. */
uint32_t overlay_headings_bodies(overlay_group_t heading, overlay_group_t *out, uint32_t max)
{
    uint32_t count = 0;
    uint32_t i;

    if (out == NULL || max == 0u) {
        return 0;
    }
    out[count++] = heading;
    for (i = 0; i < (uint32_t)OVERLAY_GROUP_COUNT && count < max; ++i) {
        if ((overlay_group_t)i != heading && DRAWN_UNDER[i] == heading) {
            out[count++] = (overlay_group_t)i;
        }
    }
    return count;
}

const char *overlay_headings_title(overlay_group_t heading)
{
    return ((uint32_t)heading < (uint32_t)OVERLAY_GROUP_COUNT) ? TITLE[heading] : NULL;
}
