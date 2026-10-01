/* mp_menu_screens_int.h: what the screen files share. Not a public header.
 *
 * The rectangles, the ids, the state and the four helpers every screen uses. mp_menu_screens.c
 * holds the state, the helpers, the entry screen, the hub and the entry points; the join, host
 * and player screens are one file each (mp_menu_screens_join.c, _host.c, _players.c).
 */
#ifndef MULTIPLAYER_MP_MENU_SCREENS_INT_H
#define MULTIPLAYER_MP_MENU_SCREENS_INT_H


#include "mp_announce.h"
#include "mp_bridge.h"
#include "mp_bridge_lobby.h"
#include "mp_bridge_public.h"
#include "mp_bridge_roster.h"
#include "mp_browser.h"
#include "mp_discovery.h"
#include "mp_lobby.h"
#include "mp_menu_metrics.h"
#include "mp_menu_screen.h"
#include "mp_mod_manifest_rule.h"
#include "mp_relay_list.h"
#include "mp_roster.h"
#include "mp_settings.h"
#include "mp_text.h"
#include "mp_wallclock.h"
#include "mp_wire.h"

#include "common/engine_types.h"
#include "common/logging.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ==============================================================================================
 * The rectangles, by overlay. Names are the census's; numbers are the census's.
 * ============================================================================================ */

/* Every screen. */
#define PLATE_X 0
#define PLATE_Y 0
#define PLATE_W 640
#define PLATE_H 480
#define RED_X 484   /* the red button, bottom right, indust */
#define RED_Y 400
#define RED_W 106
#define RED_H 67

/* sopts.bmp: the options hub. */
#define HUB_TITLE_X 382
#define HUB_TITLE_Y 28
#define HUB_TITLE_W 248
#define HUB_TITLE_H 51
#define HUB_ENTRY_X 0
#define HUB_ENTRY_Y 150
#define HUB_ENTRY_PITCH 50
#define HUB_ENTRY_W 300
#define HUB_ENTRY_H 50
#define HUB_OK_X 494
#define HUB_OK_Y 407
#define HUB_OK_W 90
#define HUB_OK_H 51

/* presets.bmp: the controller presets, a list with three slots, a band and an edit bar. */
#define PRE_TITLE_X 361
#define PRE_TITLE_Y 22
#define PRE_TITLE_W 236
#define PRE_TITLE_H 67
#define PRE_LIST_X 50
#define PRE_LIST_Y 50
#define PRE_LIST_W 300
#define PRE_LIST_H 218
#define PRE_SLOT_X 393
#define PRE_SLOT_Y 130
#define PRE_SLOT_PITCH 50
#define PRE_SLOT_W 190
#define PRE_SLOT_H 41
#define PRE_BAND_X 120
#define PRE_BAND_Y 290
#define PRE_BAND_W 400
#define PRE_BAND_H 50
#define PRE_EDIT_X 120
#define PRE_EDIT_Y 350
#define PRE_EDIT_W 400
#define PRE_EDIT_H 28

/* The lobby needs a FOURTH control row, and presets paints only three slots.
 *
 * What it does paint under them is one dark window running the whole width of the screen, and the
 * three slots and the instruction band both sit inside painted places within it, not on the metal.
 * Measured on the overlay: the lower window is x 48..585 by y 290..341, and the edit bar under it
 * is x 120..519 by y 350..378. So the fourth row takes the right hand end of the lower window,
 * directly under the third slot and in the same column; the level being played takes the left hand
 * end of it; and the status line moves down onto the edit bar, which the lobby has no field to
 * type in. Nothing here lies outside a painted rectangle and nothing overlaps: the level line ends
 * at 376 and the fourth row starts at 393. */
#define LOB_ROW4_Y   294
#define LOB_LEVEL_X  56
#define LOB_LEVEL_Y  294
#define LOB_LEVEL_W  320
#define LOB_LEVEL_H  41
#define LOB_BAND_X   120
#define LOB_BAND_Y   350
#define LOB_BAND_W   400
#define LOB_BAND_H   28

/* dialog.bmp: the general options, a title, a black window of lamps, a list, a slot, a slider. */
#define DLG_TITLE_X 50
#define DLG_TITLE_Y 25
#define DLG_TITLE_W 200
#define DLG_TITLE_H 50
/* Three lamps: which game, which game again as its opposite, and whether the session is announced
 * on the LAN. There were four until the pickup switch went, which is why the pitch is 39 and not
 * the shipped screen's 50: four bitmaps of 34 do not fit the black window at 50 and do fit at 39.
 * The pitch was left where it was when the fourth lamp went, because moving it would move two
 * lamps that are correctly placed to close a gap under the last one. The window is x 87..227 by
 * y 151..302 on the overlay. The lamp sits at the row's own top rather than centred in it, which
 * is where the shipped screen puts its own. */
#define DLG_LAMP_X 50
#define DLG_LAMP_Y 151
#define DLG_LAMP_PITCH 39
#define DLG_LAMP_LABEL_W 138   /* the black window's right edge, less the lamp and its gap */
#define DLG_LIST_X 300
#define DLG_LIST_Y 50
#define DLG_LIST_W 300
#define DLG_LIST_H 100
#define DLG_SLOT_X 300
#define DLG_SLOT_Y 150
#define DLG_SLOT_W 300
#define DLG_SLOT_H 50
#define DLG_SLIDER_X 325
#define DLG_SLIDER_Y 250
#define DLG_SLIDER_W 250
#define DLG_SLIDER_H 50
#define DLG_LABEL_X 325
#define DLG_LABEL_Y 300
#define DLG_LABEL_W 250
#define DLG_LABEL_H 50

/* sload.bmp: the load game, a tall list, a thumbnail frame, two readouts, two slots. */
#define LOAD_LIST_X 51
#define LOAD_LIST_Y 43
#define LOAD_LIST_W 300
#define LOAD_LIST_H 296
#define LOAD_FRAME_X 416
#define LOAD_FRAME_Y 59
#define LOAD_FRAME_W 160
#define LOAD_FRAME_H 120
#define LOAD_PORTRAIT_W 90
#define LOAD_PORTRAIT_H 90
#define LOAD_READ1_X 375
#define LOAD_READ1_Y 199
#define LOAD_READ2_Y 240
#define LOAD_READ_W 244
#define LOAD_READ_H 40
#define LOAD_SLOT_X 400
#define LOAD_SLOT1_Y 290
#define LOAD_SLOT2_Y 340
#define LOAD_SLOT_W 150
#define LOAD_SLOT_H 50
#define LOAD_FOOT_X 100
#define LOAD_FOOT_Y 350
#define LOAD_FOOT_W 200
#define LOAD_FOOT_H 50

/* The list box draws its rows from x+6 at the box's full width; the engine insets the left only.
 * Twelve comes off here, six at either end, as a margin of ours. */
#define LIST_INSET 12

/* The lamp bitmap is 34 high, and the lamp sits at the top of its row: the draw's centring takes
 * the bitmap height from a height it has just overwritten with that same height. */
#define LAMP_H 34

/* ==============================================================================================
 * Ids. Each screen numbers its own; 1..9 are the same on every screen.
 * ============================================================================================ */

#define ID_PLATE   1
#define ID_OVERLAY 2
#define ID_TITLE   3
#define ID_PICTURE 4
#define ID_BACK    9

#define ID_HUB_HOST 10
#define ID_HUB_JOIN 11
#define ID_HUB_NAME 12
#define ID_HUB_NET  13

#define ID_JOIN_LIST   20
#define ID_JOIN_GO     21
#define ID_JOIN_KEEP   22
#define ID_JOIN_FORGET 23
#define ID_JOIN_HINT   24
#define ID_JOIN_EDIT   25

#define ID_HOST_COOP     31
#define ID_HOST_TDM      32
#define ID_HOST_LIST     34
#define ID_HOST_GO       35
#define ID_HOST_SLIDER   36
#define ID_HOST_SLOTS    37
#define ID_HOST_ANNOUNCE 38

#define ID_ENTRY_HELP   40
#define ID_ENTRY_APPLY  41
#define ID_ENTRY_CLEAR  42
#define ID_ENTRY_CANCEL 43
#define ID_ENTRY_HINT   44
#define ID_ENTRY_EDIT   45

#define ID_PLAYERS_LIST  51
#define ID_PLAYERS_READ1 52
#define ID_PLAYERS_READ2 53
#define ID_PLAYERS_TEAM  54
#define ID_PLAYERS_READY 55
#define ID_PLAYERS_FOOT  56

/* ==============================================================================================
 * State.
 * ============================================================================================ */

#define CAPTION_MAX 64u

typedef struct screens_state {
    mp_settings_t *settings;   /* the run's, while a run is on */
    mp_settings_t  backup;     /* what was handed in, put back on a cancel */
    bool           applied;

    mp_screen_t hub;
    char        hub_net[CAPTION_MAX];   /* the network switch says which one it is on */
    mp_screen_t join;
    mp_screen_t host;
    mp_screen_t entry;
    mp_screen_t players;

    /* BEITRETEN. */
    mp_browser_t      browser;
    mp_discovery_t    listener;
    bool              listener_failed_logged;
    mp_screen_list_t *join_list;
    mp_screen_edit_t  join_address;
    char              join_hint[CAPTION_MAX];
    size_t            join_order[MP_BROWSER_ROWS_MAX];   /* each screen row's browser index */
    size_t            join_rows;
    bool              join_public;      /* opened for the public network: the relay's list */
    mp_relay_list_t   relay_list;
    uint32_t          join_hint_at;     /* when an action last put a sentence on the band */
    int32_t           join_mark_seen;   /* the list's mark as the last frame left it */
    bool              join_hint_held;   /* and that sentence still stands */

    /* HOSTEN. */
    mp_screen_list_t *host_list;
    char              host_slots[CAPTION_MAX];

    /* EINGABE. */
    mp_screen_list_t *entry_help;
    mp_screen_edit_t  entry_edit;
    char              entry_title[CAPTION_MAX];
    char              entry_hint[CAPTION_MAX];
    bool              entry_applied;

    /* SPIELER. */
    mp_screen_list_t *players_list;
    char              players_read[CAPTION_MAX];
    char              players_foot[CAPTION_MAX];
    char              players_ready[CAPTION_MAX];
    char              players_team[CAPTION_MAX];

    /* LOBBY. The six fields under the comment are the CHOICE, and they are wiped when the screen
     * opens: a lobby left behind and opened again in the other game used to start the last one's
     * savegame while saying it was going to load a map. */
    mp_screen_t       lobby;
    mp_screen_list_t *lobby_list;
    bool              lobby_is_host;
    bool              lobby_started;    /* a level is being entered; the session lives on */
    bool              lobby_arm_refused;   /* the transport would not arm; the band says why */
    mp_lobby_over_t   lobby_gone;          /* CLIENT: the host ended the lobby, or was lost */
    uint32_t          lobby_denials_answered; /* CLIENT: how many refusals the password question
                                             * has been put for, so each one is put once */

    bool              lobby_from_save;
    uint8_t           lobby_hero;
    uint8_t           lobby_level_index;
    char              lobby_level[MP_LOBBY_LEVEL_MAX];
    char              lobby_title[MP_LOBBY_TITLE_MAX];
    char              lobby_save[64];
    uint32_t          lobby_save_wait_ms;   /* CLIENT: when a start was first held for the host's
                                             * savegame, 0 while none is */
    bool              lobby_save_slow_logged; /* CLIENT: the wait has been called slow once. It is
                                             * a latch rather than a counter because the wait no
                                             * longer ends in anything, so without it the line
                                             * would be written on every frame for as long as the
                                             * player left the lobby open */

    /* What the four control rows currently are and what they say. Which of them exist is
     * mp_settings_lobby_rows; the rest of this file only draws the answer. */
    mp_settings_lobby_row_t lobby_row[MP_SETTINGS_LOBBY_ROWS_MAX];
    size_t                  lobby_rows;
    char                    lobby_row_text[MP_SETTINGS_LOBBY_ROWS_MAX][CAPTION_MAX];

    char              lobby_band[CAPTION_MAX];
    char              lobby_choice[CAPTION_MAX];
    char              lobby_error[CAPTION_MAX];

    /* The picker, which every list-shaped question borrows. */
    mp_screen_t       pick;
    mp_screen_list_t *pick_list;
    char              pick_title[CAPTION_MAX];
    char              pick_hint[CAPTION_MAX];
    int32_t           pick_result;

    /* The notice, the one sentence a player is owed before anything else. */
    mp_screen_t       notice;
    mp_screen_list_t *notice_list;

    uint32_t opened;
} screens_state_t;

extern screens_state_t mps;


/* Copies `text` into `out` up to the first character that would take it past `width` pixels in
 * the font whose advance table is given; a character the font has no glyph for becomes '?'. */
void mp_screens_fit(const uint8_t *advance, const char *text, int32_t width, char *out,
                    size_t out_size);

#define FIT_INDUST(text, width, out)                                                              \
    do {                                                                                          \
        _Static_assert(sizeof(out) > sizeof(char *),                                               \
                       "FIT_INDUST needs the array, not a pointer to it");                        \
        mp_screens_fit(MP_MENU_ADVANCE_INDUST, (text), (width), (out), sizeof(out));               \
    } while (0)
/* All THREE take the ARRAY and measure it themselves, and all three REFUSE A POINTER at compile
 * time. The third was left without a guard when the other two got theirs, and the change that
 * added them claimed a clean build proved there was no other such caller: it proved it for two
 * of the three.
 *
 * The refusal is not decoration. `sizeof out` on a `char *` parameter is four, and
 * mp_screens_fit stops on the buffer as well as on the width, so every caption written through a
 * pointer came out three characters long: "Karte waehlen" drew as "Kar", "SPIEL STARTEN" as
 * "SPI", and the lobby shipped a column of stubs that read like a broken font rather than like a
 * buffer size. Nothing warned, because passing an array where a pointer is wanted is exactly what
 * C is built to allow.
 *
 * A caller that genuinely holds a pointer calls mp_screens_fit and passes the size it was given,
 * which is what the size is FOR. */
#define FIT_SYSFONT(text, width, out)                                                             \
    do {                                                                                          \
        _Static_assert(sizeof(out) > sizeof(char *),                                              \
                       "FIT_SYSFONT needs the array, not a pointer to it");                       \
        mp_screens_fit(MP_MENU_ADVANCE_SYSFONT, (text), (width), (out), sizeof(out));             \
    } while (0)
#define FIT_COURIER(text, width, out)                                                             \
    do {                                                                                          \
        _Static_assert(sizeof(out) > sizeof(char *),                                              \
                       "FIT_COURIER needs the array, not a pointer to it");                       \
        mp_screens_fit(MP_MENU_ADVANCE_COURIER, (text), (width), (out), sizeof(out));             \
    } while (0)

/* A list's rows, in courier, each cut to the list's own column. */
void mp_screens_set_rows(mp_screen_list_t *list, int32_t list_width, const char *const *rows,
                         size_t count);

/* ==============================================================================================
 * The player table, written in one place because it is read as two.
 *
 * A heading that promises four columns over rows that carry three in another order is not a
 * cosmetic fault: it makes the player read the wrong number as the ping. So the heading and the
 * rows are laid out by the same call against the same column table, and a field that would run
 * into the next column is cut rather than allowed to push it.
 *
 * courier is proportional, so the columns are held by padding with spaces to a PIXEL offset, four
 * pixels at a time (a courier space), and a column can therefore stand up to three pixels early.
 * ============================================================================================ */

#define PLAYER_COLUMNS 5u

/* The column starts, in pixels from the left of the row, and the width each field is cut to. */
extern const int32_t MP_PLAYER_COLUMN_X[PLAYER_COLUMNS];

/* Writes the heading, or one player's line, into `out`. `hero` is a hero index; `ping_ms` is
 * ignored for the authority, whose line says so instead of showing a round trip of zero. */
/* `with_team` drops the team column and gives its width to the others. Co-operative play has
 * no teams at all, and a column of dashes in a game that does not have the concept is the
 * kind of noise a player reads as a setting they failed to find. Heading and rows take the
 * same flag from the same caller, so the two cannot disagree about how many columns there
 * are, which is the defect this pair was rebuilt to make impossible. */
void mp_screens_player_header(bool with_team, char *out, size_t out_size);
void mp_screens_player_row(const mp_roster_entry_t *entry, bool with_team, char *out,
                           size_t out_size);

/* A hero's name, and a short one for a table column. An index past the last hero answers the
 * first, because the engine does not clamp and a bad index there is a program end. */
const char *mp_screens_hero_name(uint8_t hero);
const char *mp_screens_hero_short(uint8_t hero);

/* The plate, the overlay and the red button, the same on every screen. */
void mp_screens_put_frame(mp_screen_t *screen, mp_screen_bitmap_t overlay, const char *red_caption,
                          int32_t red_action);

/* The entry screen for one value; see mp_menu_screens.c. */
bool mp_screens_run_entry(const char *title, const char *hint, const char *initial,
                          size_t max_chars, char *out, size_t out_size);

/* The screens the hub opens. True when the player applied a choice. */
bool mp_screens_run_join(void);
bool mp_screens_run_host(void);

/* The lobby: who is here, what will be played, and the moment everybody goes in. True when a
 * level is being entered, in which case the session stays up. */
bool mp_screens_run_lobby(bool as_host);

/* One question shaped like a list: a map, a savegame, anything. Answers the row the player chose,
 * or -1 for a cancel. The rows are not copied, so they must outlive the call. */
int32_t mp_screens_pick(const char *title, const char *hint, const char *const *rows, size_t count,
                        int32_t initial);

/* What a host chooses in the lobby (mp_menu_screens_choice.c): the note that tells everybody, the
 * first level when nothing is chosen yet, and the three pickers. */
void mp_screens_publish_choice(void);
void mp_screens_choose_default(void);
void mp_screens_pick_map(void);
void mp_screens_pick_save(void);
void mp_screens_pick_hero(void);

/* The lobby's two lines of text (mp_menu_screens_band.c), written every frame: the band, which
 * says what is going on, and the level line, which says what will be played. And how many players
 * have not said they are ready, with the first of their names in `out` when it is not NULL, which
 * the band shows and the host's start asks before it goes. */
void   mp_screens_refresh_band(void);
void   mp_screens_refresh_level(void);
size_t mp_screens_waiting_players(char *out, size_t out_size);

/* CLIENT: whether the lobby may take the start the host has given, which it may only once this
 * player has said ready, with the start in `out` (mp_menu_screens_lobby.c). The frame takes a start
 * on nothing else, and the band says the level is about to begin on this same answer. */
bool mp_screens_lobby_start_to_take(mp_lobby_setup_t *out);

/* CLIENT: what a refusal for the game data, a required mod or a DLL outside this release says
 * (mp_menu_screens_band.c): the band's sentence, which names the file, into `band`, and the two
 * sides' values into `host_row` and `here_row` for the player list, each empty where the refusal
 * has no value for that side, and the list then leaves that row out. Either half may be NULL.
 * False, with nothing written, for a refusal whose detail this build cannot read, and the band then
 * says the reason's word. */
bool mp_screens_refusal_lines(const mp_mod_refusal_t *refusal, char *band, size_t band_size,
                              char *host_row, char *here_row, size_t row_size);

#endif /* MULTIPLAYER_MP_MENU_SCREENS_INT_H */
