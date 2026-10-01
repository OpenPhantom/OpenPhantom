/* mp_menu_screens_choice.c: what a host chooses in the lobby, a map, a savegame or a hero, and
 * the note that tells everybody.
 *
 * Split from mp_menu_screens_lobby.c when the one exit pushed that file past the hard limit, along
 * the seam its own size note had measured: these read four fields of the screen state and write
 * six, and touch nothing else of the lobby.
 */
#include "mp_menu_screens_int.h"

#include "mp_bridge_lobby.h"
#include "mp_bridge_savefile.h"
#include "mp_levels.h"
#include "mp_lobby.h"
#include "mp_rules.h"
#include "mp_saves.h"
#include "mp_settings.h"

#include "common/text.h"

/* ==============================================================================================
 * What the host chooses.
 * ============================================================================================ */

/* Tells everybody what will be played. Called whenever the host picks, and once when the lobby
 * opens so that a client arriving late has something to read. */
void mp_screens_publish_choice(void)
{
    mp_lobby_setup_t setup;

    /* A lobby whose transport would not arm tells nobody anything. Its note would have gone
     * onto a transport armed the other way round, as a client's own setup. */
    if (mps.lobby_arm_refused) {
        return;
    }
    memset(&setup, 0, sizeof setup);
    setup.mode        = (uint8_t)mps.settings->mode;
    setup.level_index = mps.lobby_level_index;
    setup.flags       = mps.lobby_from_save ? (uint8_t)MP_LOBBY_F_FROM_SAVE : 0u;
    /* The file travels with the choice. A client cannot restore a savegame it does not have, and
     * until this line every client of a host that restored one loaded the same level fresh and
     * stood in a world the file did not describe. The note names the file by its digest and its
     * size; the clients ask for it and the bridge hands it over (mp_bridge_savefile). A file the
     * host cannot read is offered as nothing, and the clients then do what they always did. */
    if (mps.lobby_from_save) {
        (void)mp_bridge_savefile_offer(mps.lobby_save, &setup.save_id, &setup.save_bytes);
    } else {
        mp_bridge_savefile_withdraw();
    }
    /* The rule set travels with the choice, and without this line it travelled as nine zeroes:
     * no points to win, no time limit, no teams, no penalties and an instant respawn. That
     * encodes cleanly and is refused by nothing, so the session simply played by rules nobody
     * chose. It is clamped rather than trusted, because the ini is a file a player can edit and
     * a rule set out of range is refused by the note's own encoder, which would leave the lobby
     * unable to say what it was playing at all. */
    setup.rules = mps.settings->rules;
    (void)mp_rules_clamp(&setup.rules);
    mp_lobby_clean_field(mps.lobby_level, setup.level, sizeof setup.level);
    mp_lobby_clean_field(mps.lobby_title, setup.title, sizeof setup.title);
    if (setup.level[0] == '\0') {
        return;   /* nothing chosen yet, and a setup with no level is refused by its own encoder */
    }
    mp_bridge_lobby_set_setup(&setup);
}

static void take_level(const mp_level_t *level)
{
    memcpy(mps.lobby_level, level->path, sizeof mps.lobby_level);
    memcpy(mps.lobby_title, level->title, sizeof mps.lobby_title);
    mps.lobby_level_index = level->index;
    mps.lobby_from_save   = false;
    mps.lobby_save_wait_ms = 0u;
    mps.lobby_save[0]     = '\0';
}

void mp_screens_choose_default(void)
{
    const mp_level_t *level;

    if (mps.lobby_level[0] != '\0') {
        return;
    }
    /* Something has to be chosen before a client can be told anything, so the lobby opens on the
     * first level the catalogue holds and the host changes it if it likes. */
    (void)mp_levels_scan();
    level = mp_levels_count() != 0u ? mp_levels_at(0) : NULL;
    if (level != NULL) {
        take_level(level);
    }
}

/* A co-op host chooses a map as well as a savegame. It used to have only the savegame list, whose
 * first row started a new game at level index 0, so every other campaign level was unreachable. */
void mp_screens_pick_map(void)
{
    char        rows[MP_SCREEN_LIST_ROWS_MAX][MP_SCREEN_ROW_TEXT_MAX];
    const char *pointers[MP_SCREEN_LIST_ROWS_MAX];
    size_t      count = mp_levels_scan();
    size_t      i;
    int32_t     chosen;

    if (count > MP_SCREEN_LIST_ROWS_MAX) {
        count = MP_SCREEN_LIST_ROWS_MAX;
    }
    for (i = 0; i < count; ++i) {
        const mp_level_t *level = mp_levels_at(i);

        text_format(rows[i], sizeof rows[i], "%s%s", level->title,
                    level->index == (uint8_t)MP_LOBBY_LEVEL_CUSTOM ? mp_text(MP_TEXT_MAP_OWN) : "");
        pointers[i] = rows[i];
    }
    if (count == 0u) {
        FIT_SYSFONT(mp_text(MP_TEXT_MAPS_NONE), LOB_BAND_W, mps.lobby_error);
        return;
    }
    chosen = mp_screens_pick(mp_text(MP_TEXT_MAP_TITLE), mp_text(MP_TEXT_MAP_HINT), pointers,
                             count, 0);
    if (chosen < 0 || (size_t)chosen >= count) {
        return;
    }
    take_level(mp_levels_at((size_t)chosen));
    mp_screens_publish_choice();
}

void mp_screens_pick_save(void)
{
    char        rows[MP_SCREEN_LIST_ROWS_MAX][MP_SCREEN_ROW_TEXT_MAX];
    const char *pointers[MP_SCREEN_LIST_ROWS_MAX];
    size_t      count = mp_saves_scan();
    size_t      i;
    int32_t     chosen;

    if (count > MP_SCREEN_LIST_ROWS_MAX - 1u) {
        count = MP_SCREEN_LIST_ROWS_MAX - 1u;
    }
    /* A new game is the first row rather than a button of its own: it is the same question. */
    pointers[0] = mp_text(MP_TEXT_SAVE_NEW_GAME);
    for (i = 0; i < count; ++i) {
        const mp_save_t *save = mp_saves_at(i);

        text_format(rows[i], sizeof rows[i], "%s", save->name);
        pointers[i + 1u] = rows[i];
    }
    chosen = mp_screens_pick(mp_text(MP_TEXT_SAVE_TITLE), mp_text(MP_TEXT_SAVE_HINT), pointers,
                             count + 1u, 0);
    if (chosen < 0) {
        return;
    }
    if (chosen == 0) {
        const mp_level_t *level = mp_levels_shipped(0);   /* the campaign starts at its first */

        if (level == NULL) {
            (void)mp_levels_scan();
            level = mp_levels_shipped(0);
        }
        if (level == NULL) {
            return;
        }
        take_level(level);
        mps.lobby_level_index = 0u;
    } else {
        const mp_save_t  *save  = mp_saves_at((size_t)chosen - 1u);
        const mp_level_t *level = mp_levels_shipped(save->level_index);

        if (level == NULL) {
            FIT_SYSFONT(mp_text(MP_TEXT_SAVE_LEVEL_MISSING), LOB_BAND_W, mps.lobby_error);
            return;
        }
        /* The host restores the save, and every client restores the same file, sent to it before
         * the start; the level it sits in is the field the save's own header carries. */
        memcpy(mps.lobby_level, level->path, sizeof mps.lobby_level);
        mp_screens_fit(MP_MENU_ADVANCE_COURIER, save->name, 1000, mps.lobby_title,
                       sizeof mps.lobby_title);
        mps.lobby_level_index = save->level_index;
        mps.lobby_from_save   = true;
        memcpy(mps.lobby_save, save->file, sizeof mps.lobby_save);
    }
    mp_screens_publish_choice();
}

void mp_screens_pick_hero(void)
{
    const char *rows[MP_LOBBY_HERO_MAX + 1u];
    size_t      i;
    int32_t     chosen;

    for (i = 0; i <= MP_LOBBY_HERO_MAX; ++i) {
        rows[i] = mp_screens_hero_name((uint8_t)i);
    }
    chosen = mp_screens_pick(mp_text(MP_TEXT_HERO_TITLE), mp_text(MP_TEXT_HERO_HINT), rows,
                             MP_LOBBY_HERO_MAX + 1u, (int32_t)mps.lobby_hero);
    if (chosen < 0 || chosen > (int32_t)MP_LOBBY_HERO_MAX) {
        return;
    }
    mps.lobby_hero = (uint8_t)chosen;
    mp_bridge_set_lobby_hero(mps.lobby_hero);
}
