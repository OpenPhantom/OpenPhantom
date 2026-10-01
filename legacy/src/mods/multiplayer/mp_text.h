/* mp_text.h: every text the multiplayer draws for a player, in the five languages the game
 * shipped in.
 *
 * Layer 1, no engine. Only what this feature draws itself is here: its menu, its lobby, its player
 * list, its scoreboard and the notices it lays over a level. What the game draws is the game's own,
 * in the language of whichever release is installed, and nothing here touches it.
 *
 * One row per id and one column per language, in the order of common/language.h. A text that
 * carries a number or a name is a format, whole, so a translation orders its words in one string;
 * every column of a row has the same conversions in the same order, which the unit test holds each
 * row to. A row with a font and a width is drawn where nothing cuts it, or where it is cut at that
 * width, and the same test measures every column against it.
 *
 * The table is two files, split by id: every id before MP_TEXT_SECOND_TABLE_FIRST is a row of
 * mp_text.c, every id from it on a row of mp_text_second.c, and mp_text_row asks the one the id
 * belongs to. A new text goes at the end of the enum, so it lands in the second file.
 */
#ifndef MULTIPLAYER_MP_TEXT_H
#define MULTIPLAYER_MP_TEXT_H

#include "common/language.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum mp_text_id {
    /* Buttons every screen shares. */
    MP_TEXT_BACK = 0,
    MP_TEXT_APPLY,
    MP_TEXT_CLEAR,
    MP_TEXT_CANCEL,

    /* The entry screen: the three rows of help under the edit bar. */
    MP_TEXT_ENTRY_HELP_CHARS,
    MP_TEXT_ENTRY_HELP_TRIM,
    MP_TEXT_ENTRY_HELP_KEYS,

    /* The hub. */
    MP_TEXT_HUB_TITLE,
    MP_TEXT_HUB_HOST,
    MP_TEXT_HUB_JOIN,
    MP_TEXT_HUB_NAME,
    MP_TEXT_HUB_NET_LAN,
    MP_TEXT_HUB_NET_PUBLIC,
    MP_TEXT_NAME_TITLE,
    MP_TEXT_NAME_HINT,

    /* The player table, headings and cells, each cut to its column in courier. */
    MP_TEXT_COL_NAME,
    MP_TEXT_COL_HERO,
    MP_TEXT_COL_TEAM,
    MP_TEXT_COL_READY,
    MP_TEXT_COL_PING,
    MP_TEXT_CELL_HOST,
    MP_TEXT_CELL_YES,
    MP_TEXT_CELL_NO,
    MP_TEXT_CELL_TEAM,

    /* Joining: the screen, the password question and the band. */
    MP_TEXT_JOIN_TITLE,
    MP_TEXT_JOIN_GO,
    MP_TEXT_JOIN_KEEP,
    MP_TEXT_JOIN_FORGET,
    MP_TEXT_JOIN_PASSWORD_FOR,
    MP_TEXT_JOIN_PASSWORD_WANTED,
    MP_TEXT_PASSWORD_TITLE,
    MP_TEXT_JOIN_PICK_HINT,
    MP_TEXT_JOIN_NOTHING_TO_KEEP,
    MP_TEXT_JOIN_KEPT,
    MP_TEXT_JOIN_FORGET_ONLY_FAV,
    MP_TEXT_JOIN_FORGOTTEN,
    MP_TEXT_JOIN_CODE_HINT,
    MP_TEXT_JOIN_CODE_BAD,
    MP_TEXT_JOIN_PUBLIC_ASKING,
    MP_TEXT_JOIN_PUBLIC_SILENT,
    MP_TEXT_JOIN_PUBLIC_COUNT,
    MP_TEXT_SOURCE_FAVOURITE,
    MP_TEXT_SOURCE_TYPED,
    MP_TEXT_SOURCE_LAN,
    MP_TEXT_SOURCE_PUBLIC,

    /* Hosting: the three rows, the switches and the three questions. */
    MP_TEXT_HOST_ROW_SESSION,
    MP_TEXT_HOST_ROW_PASSWORD,
    MP_TEXT_HOST_PASSWORD_SET,
    MP_TEXT_HOST_PASSWORD_NONE,
    MP_TEXT_HOST_ROW_PORT,
    MP_TEXT_HOST_TITLE,
    MP_TEXT_HOST_COOP,
    MP_TEXT_HOST_TDM,
    MP_TEXT_HOST_ANNOUNCE,
    MP_TEXT_HOST_LIST_PUBLIC,
    MP_TEXT_HOST_GO,
    MP_TEXT_HOST_SLOTS,
    MP_TEXT_HOST_PASSWORD_HINT,
    MP_TEXT_HOST_PORT_HINT,
    MP_TEXT_PORT_TITLE,
    MP_TEXT_HOST_PORT_BAD,
    MP_TEXT_SESSION_TITLE,
    MP_TEXT_SESSION_HINT,

    /* The players screen, opened during a game. */
    MP_TEXT_PLAYERS_TITLE,
    MP_TEXT_TEAM_1,
    MP_TEXT_TEAM_2,
    MP_TEXT_NO_TEAM,
    MP_TEXT_PLAYERS_IN_GAME,
    MP_TEXT_READY_WORD,
    MP_TEXT_WAITING_WORD,
    MP_TEXT_PLAYERS_FOOT,

    /* The lobby: why a join was refused, the rows, the band and the level line. */
    MP_TEXT_DENY_FULL,
    MP_TEXT_DENY_PROTOCOL,
    MP_TEXT_DENY_CONTENT,
    MP_TEXT_DENY_MODE,
    MP_TEXT_DENY_PASSWORD,
    MP_TEXT_LOBBY_TITLE,
    MP_TEXT_ROW_PICK_MAP,
    MP_TEXT_ROW_PICK_SAVE,
    MP_TEXT_ROW_FRIENDLY_FIRE,
    MP_TEXT_ROW_HERO,
    MP_TEXT_ROW_LEAVE_TEAM,
    MP_TEXT_ROW_TO_TEAM,
    MP_TEXT_ROW_NOT_READY,
    MP_TEXT_ROW_READY,
    MP_TEXT_ROW_START,
    MP_TEXT_LOBBY_NOBODY_YET,
    MP_TEXT_LOBBY_WAIT_HOST,
    MP_TEXT_CONTENT_MISMATCH,
    MP_TEXT_JOIN_HOST_LEFT,
    MP_TEXT_JOIN_GAVE_UP,
    MP_TEXT_JOIN_CONNECTED,
    MP_TEXT_JOIN_CONNECTING,
    MP_TEXT_BAND_NOT_READY,
    MP_TEXT_BAND_ALL_READY,
    MP_TEXT_RULES_COOP,
    MP_TEXT_RULES_TDM,
    MP_TEXT_RULES_ON,
    MP_TEXT_RULES_OFF,
    MP_TEXT_RULES_POINTS,
    MP_TEXT_RULES_NO_POINTS,
    MP_TEXT_RULES_MINUTES,
    MP_TEXT_RULES_NO_TIME,
    MP_TEXT_RULES_TEAMS,
    MP_TEXT_RULES_FREE,
    MP_TEXT_LEVEL_FROM_SAVE,
    MP_TEXT_LEVEL_NONE_HOST,
    MP_TEXT_LEVEL_NONE_CLIENT,
    MP_TEXT_MAP_OWN,
    MP_TEXT_MAPS_NONE,
    MP_TEXT_MAP_TITLE,
    MP_TEXT_MAP_HINT,
    MP_TEXT_SAVE_NEW_GAME,
    MP_TEXT_SAVE_TITLE,
    MP_TEXT_SAVE_HINT,
    MP_TEXT_SAVE_LEVEL_MISSING,
    MP_TEXT_HERO_TITLE,
    MP_TEXT_HERO_HINT,
    MP_TEXT_START_MAP_FIRST,
    MP_TEXT_START_LEVEL_FAILED,
    MP_TEXT_SAVE_STALLED,
    MP_TEXT_SAVE_COMING,
    MP_TEXT_HOST_MAP_MISSING,
    MP_TEXT_ARM_NO_CONNECTION,
    MP_TEXT_ARM_ROLE_FROM_INI,
    MP_TEXT_ARM_ALREADY_HOST,
    MP_TEXT_ARM_ALREADY_CLIENT,
    MP_TEXT_LOBBY_CODE,
    MP_TEXT_LOBBY_CODE_WAIT,
    MP_TEXT_RELAY_CONNECTING,
    MP_TEXT_RELAY_RETRYING,
    MP_TEXT_RELAY_FAILED,
    MP_TEXT_RELAY_NO_SESSION,
    MP_TEXT_RELAY_FULL,
    MP_TEXT_RELAY_CLOSED,

    /* The scoreboard panel and the notices over a level; the panel grows to its text. */
    MP_TEXT_HUD_HOLD_HINT,
    MP_TEXT_HUD_HOST_SILENT,
    MP_TEXT_BOARD_COL_NAME,
    MP_TEXT_BOARD_COL_POINTS,
    MP_TEXT_BOARD_COL_DEATHS,
    MP_TEXT_BOARD_COL_TEAM,
    MP_TEXT_BOARD_STAND_IN,
    MP_TEXT_BOARD_TITLE,
    MP_TEXT_BOARD_HEAD_OPEN,
    MP_TEXT_BOARD_HEAD_TARGET,
    MP_TEXT_BOARD_RUNNING,
    MP_TEXT_BOARD_WON_TEAM,
    MP_TEXT_BOARD_WON_NAME,
    MP_TEXT_BOARD_WON_SLOT,
    MP_TEXT_BOARD_DRAW,

    /* How a session ended, on the notice band. */
    MP_TEXT_OVER_HOST_ENDED,
    MP_TEXT_OVER_HOST_LOST,
    MP_TEXT_OVER_ALL_LEFT,
    MP_TEXT_OVER_BEHIND,

    /* The screen that tells a player at the title why the session ended. */
    MP_TEXT_NOTICE_TITLE,
    MP_TEXT_NOTICE_ALONE,
    MP_TEXT_NOTICE_OK,

    /* The line on the title screen, and a savegame with no name of its own. */
    MP_TEXT_HUD_HOST_LOADS,
    MP_TEXT_HUD_WIPE_WAIT,
    MP_TEXT_HUD_HOST_STILL,
    MP_TEXT_HUD_PLAYER_BEHIND,
    MP_TEXT_MENU_TITLE_LINE,
    MP_TEXT_SAVE_FALLBACK_NAME,

    /* Every id from here to the count is a row of the second table, mp_text_second.c. The marker
     * is a second name for the first id after it and not an id of its own, so no id moved when it
     * was put in. */
    MP_TEXT_SECOND_TABLE_FIRST,

    /* What the window's caption says this machine is, during a session. */
    MP_TEXT_CAPTION_HOST = MP_TEXT_SECOND_TABLE_FIRST,
    MP_TEXT_CAPTION_CLIENT,

    /* The chat's input row begins with it. */
    MP_TEXT_CHAT_SAY,

    /* The lobby's band while a client's lobby holds the start of a session that already runs:
     * what to do before it is taken, and that it is about to be. */
    MP_TEXT_BAND_RUNNING_PICK,
    MP_TEXT_BAND_RUNNING_READY,

    /* A join refused for a required mod or the game data: the band says what differs, and two
     * rows of the player list say the two sides' values. The word for a mod refusal that carries
     * no detail, and the rows' word for a side that lacks the mod. */
    MP_TEXT_DENY_MODS,
    MP_TEXT_REFUSED_OTHER,
    MP_TEXT_REFUSED_MISSING_AT_HOST,
    MP_TEXT_REFUSED_MISSING_HERE,
    MP_TEXT_REFUSED_HOST_ROW,
    MP_TEXT_REFUSED_HERE_ROW,
    MP_TEXT_REFUSED_MISSING,

    /* A join refused for a DLL out of the mods folder that is not of this release and that the
     * host's [multiplayer] AllowMods does not name: the word for a refusal that names no DLL, the
     * band's two sentences with a DLL's name in them and the host's value on its row. Then the
     * band of a machine that cannot host for a DLL of its own. */
    MP_TEXT_DENY_FOREIGN_DLL,
    MP_TEXT_REFUSED_NOT_ALLOWED,
    MP_TEXT_REFUSED_TOO_MANY,
    MP_TEXT_REFUSED_NOT_ALLOWED_WORD,
    MP_TEXT_ARM_FOREIGN_DLL,

    MP_TEXT_COUNT
} mp_text_id_t;

/* The font a row's width is measured in, or none for a drawer that grows to its text. */
typedef enum mp_text_font {
    MP_TEXT_FONT_NONE = 0,
    MP_TEXT_FONT_INDUST,
    MP_TEXT_FONT_SYSFONT,
    MP_TEXT_FONT_COURIER
} mp_text_font_t;

typedef struct mp_text_row {
    const char *text[LANGUAGE_COUNT];   /* none empty; if one were, English would show */
    uint8_t     font;                   /* mp_text_font_t */
    uint16_t    width;                  /* pixels it has to fit whole, 0 for none */
} mp_text_row_t;

/* The language every later call answers in, from the ini's Language= (common/language.h), said
 * once in the log. Before it is called, English. */
void mp_text_choose(const char *tag);

void       mp_text_set_language(language_t language);
language_t mp_text_language(void);

/* A text in the chosen language, and in a given one. Never NULL: an id past the table is "". */
const char *mp_text(mp_text_id_t id);
const char *mp_text_in(mp_text_id_t id, language_t language);

/* The whole row, for the test that measures it; NULL past the table. */
const mp_text_row_t *mp_text_row(mp_text_id_t id);

/* A row of the second table, for mp_text_row and nothing else: every other caller asks mp_text_row,
 * which knows which table an id is in. NULL for an id outside the second table. */
const mp_text_row_t *mp_text_second_row(mp_text_id_t id);

#endif /* MULTIPLAYER_MP_TEXT_H */
