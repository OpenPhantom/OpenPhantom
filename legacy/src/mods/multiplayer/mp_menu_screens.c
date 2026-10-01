/* mp_menu_screens.c: the five multiplayer screens on the shipped overlays. See the header.
 *
 * Every rectangle here is one the census of the shipped screens lists for the overlay reused,
 * and every font the one that screen uses there. What is drawn in a rectangle is cut to it by
 * the font's own advance table (mp_menu_metrics.h) before the engine sees it, so no string
 * leaves its column whatever a player types or a host calls itself.
 *
 * This file is the shared state, the helpers, the entry screen (which three screens open), the
 * hub and the entry points; the join, host and player screens are a file each.
 */
#include "mp_menu_screens.h"

#include "mp_menu_screens_int.h"
#include "mp_settings_ini.h"

#include "common/text.h"

screens_state_t mps;

/* ==============================================================================================
 * Text that stays in its column.
 * ============================================================================================ */

/* Copies `text` into `out` up to the first character that would take it past `width` pixels in
 * the font whose advance table is given; a character the font has no glyph for becomes '?'.
 * Courier is proportional here, 7 pixels for an x and 13 for a W, so a character count is not a
 * width, and the engine's text draw does not clip. */
void mp_screens_fit(const uint8_t *advance, const char *text, int32_t width, char *out,
                size_t out_size)
{
    int32_t used = 0;
    size_t  at = 0;

    if (out == NULL || out_size == 0u) {
        return;
    }
    for (; text != NULL && text[at] != '\0' && at + 1u < out_size; ++at) {
        unsigned char c = (unsigned char)text[at];
        int32_t       w;

        if (c < 0x20u || c > 0x7Eu) {
            c = '?';
        }
        w = (int32_t)advance[c];
        if (used + w > width) {
            break;
        }
        used += w;
        out[at] = (char)c;
    }
    out[at] = '\0';
}

/* A row of a list, in courier, in the list's own column. */
void mp_screens_set_rows(mp_screen_list_t *list, int32_t list_width, const char *const *rows,
                     size_t count)
{
    char        fitted[MP_SCREEN_LIST_ROWS_MAX][MP_SCREEN_ROW_TEXT_MAX];
    const char *pointers[MP_SCREEN_LIST_ROWS_MAX];
    size_t      i;

    if (count > MP_SCREEN_LIST_ROWS_MAX) {
        count = MP_SCREEN_LIST_ROWS_MAX;
    }
    for (i = 0; i < count; ++i) {
        mp_screens_fit(MP_MENU_ADVANCE_COURIER, rows[i], list_width - LIST_INSET, fitted[i],
            sizeof fitted[i]);
        pointers[i] = fitted[i];
    }
    mp_screen_list_set_rows(list, pointers, count);
}

/* ==============================================================================================
 * The player table: one heading and one row shape, written once.
 * ============================================================================================ */

/* The four heroes the game ships, in the order its own hero swap takes them. The short forms are
 * for a table column; the long ones for a question that has room for them. */
static const char *const HERO_NAME[] = { "Obi-Wan Kenobi", "Qui-Gon Jinn", "Panaka", "Amidala" };
static const char *const HERO_SHORT[] = { "Obi", "Qui", "Pan", "Ami" };

/* TWO LAYOUTS, because one of the five columns only exists in one of the two games.
 *
 * Both fill the same 288 pixels: a 300 pixel list box less six at either end, where the engine
 * insets the left six (its row draw starts at x+6 and keeps the box's full width) and the right
 * six are our margin. The one without teams gives that column's width to the name and to the
 * ready state, which are the two that were being cut.
 *
 * One table for the heading and the rows, because there used to be two. The heading was a
 * literal naming the name, the team, the status and the ping in that order, and the rows were
 * built elsewhere with the fields in another order and no ping cell for the host, so a player
 * read the round trip under the word for the status and the host's line ended one field short.
 * The headings measure 37, 35, 37, 46 and 30 pixels in courier, against columns of 100, 40, 38,
 * 50 and 44, so every one fits its column. */
/* The ready column is five pixels wider than it was, taken from the name, so that the widest
 * of the five languages' words for it, PRONTO, stands whole over its column. */
const int32_t MP_PLAYER_COLUMN_X[PLAYER_COLUMNS] = { 0, 99, 143, 185, 244 };
static const int32_t PLAYER_COLUMN_W[PLAYER_COLUMNS] = { 95, 40, 38, 55, 44 };
static const mp_text_id_t PLAYER_HEADING[PLAYER_COLUMNS] = {
    MP_TEXT_COL_NAME, MP_TEXT_COL_HERO, MP_TEXT_COL_TEAM, MP_TEXT_COL_READY, MP_TEXT_COL_PING
};

#define PLAYER_COLUMNS_NO_TEAM 4u
static const int32_t NO_TEAM_COLUMN_X[PLAYER_COLUMNS_NO_TEAM] = { 0, 120, 168, 232 };
static const int32_t NO_TEAM_COLUMN_W[PLAYER_COLUMNS_NO_TEAM] = { 116, 44, 60, 56 };
static const mp_text_id_t NO_TEAM_HEADING[PLAYER_COLUMNS_NO_TEAM] = {
    MP_TEXT_COL_NAME, MP_TEXT_COL_HERO, MP_TEXT_COL_READY, MP_TEXT_COL_PING
};

const char *mp_screens_hero_name(uint8_t hero)
{
    return HERO_NAME[hero & 3u];
}

const char *mp_screens_hero_short(uint8_t hero)
{
    return HERO_SHORT[hero & 3u];
}

static int32_t advance_width(const uint8_t *advance, const char *text)
{
    int32_t width = 0;
    size_t  at;

    for (at = 0; text != NULL && text[at] != '\0'; ++at) {
        unsigned char c = (unsigned char)text[at];

        width += (int32_t)advance[(c < 0x20u || c > 0x7Eu) ? (unsigned char)'?' : c];
    }
    return width;
}

/* Pads with courier spaces up to each column's pixel offset, then writes the field cut to its
 * width. A space is four pixels, so a column can begin up to three pixels early; nothing can
 * begin late, which is the property that keeps a heading over its values. */
static void put_columns(const char *const *fields, bool with_team, char *out, size_t out_size)
{
    const int32_t *offsets = with_team ? MP_PLAYER_COLUMN_X : NO_TEAM_COLUMN_X;
    const int32_t *widths  = with_team ? PLAYER_COLUMN_W : NO_TEAM_COLUMN_W;
    size_t         count   = with_team ? PLAYER_COLUMNS : PLAYER_COLUMNS_NO_TEAM;
    size_t at = 0;
    int32_t used = 0;
    size_t  column;

    if (out == NULL || out_size == 0u) {
        return;
    }
    for (column = 0; column < count; ++column) {
        char   cut[MP_SCREEN_ROW_TEXT_MAX];
        size_t length;

        while (used < offsets[column] && at + 1u < out_size) {
            out[at++] = ' ';
            used += (int32_t)MP_MENU_ADVANCE_COURIER[(unsigned char)' '];
        }
        mp_screens_fit(MP_MENU_ADVANCE_COURIER, fields[column], widths[column], cut,
                       sizeof cut);
        length = strlen(cut);
        if (at + length + 1u > out_size) {
            length = out_size - at - 1u;
        }
        memcpy(out + at, cut, length);
        at += length;
        used += advance_width(MP_MENU_ADVANCE_COURIER, cut);
    }
    out[at] = '\0';
}

void mp_screens_player_header(bool with_team, char *out, size_t out_size)
{
    const mp_text_id_t *ids   = with_team ? PLAYER_HEADING : NO_TEAM_HEADING;
    size_t              count = with_team ? PLAYER_COLUMNS : PLAYER_COLUMNS_NO_TEAM;
    const char         *fields[PLAYER_COLUMNS];
    size_t              column;

    for (column = 0; column < count; ++column) {
        fields[column] = mp_text(ids[column]);
    }
    put_columns(fields, with_team, out, out_size);
}

void mp_screens_player_row(const mp_roster_entry_t *entry, bool with_team, char *out,
                           size_t out_size)
{
    const char *fields[PLAYER_COLUMNS];
    char        ping[16];
    char        team[8];
    size_t      at = 0;

    if (entry == NULL) {
        return;
    }
    text_format(team, sizeof team, mp_text(MP_TEXT_CELL_TEAM), (unsigned)entry->team);
    /* The authority measures no round trip to itself, so its cell says what it is rather than
     * showing a zero the player would read as a perfect connection. */
    if (entry->slot == 0u) {
        text_format(ping, sizeof ping, "%s", mp_text(MP_TEXT_CELL_HOST));
    } else {
        text_format(ping, sizeof ping, "%ums", (unsigned)entry->rtt_ms);
    }

    /* Filled in the order the heading names them, and the team is simply skipped when there is
     * none, so the two can never come to different column counts. */
    fields[at++] = entry->name;
    fields[at++] = mp_screens_hero_short(entry->hero);
    if (with_team) {
        fields[at++] = (entry->team == 1u || entry->team == 2u) ? team : "--";
    }
    fields[at++] = mp_text(entry->ready != 0u ? MP_TEXT_CELL_YES : MP_TEXT_CELL_NO);
    fields[at++] = ping;
    put_columns(fields, with_team, out, out_size);
}

/* The plate, the overlay and the red button, the same on every screen. */
void mp_screens_put_frame(mp_screen_t *screen, mp_screen_bitmap_t overlay, const char *red_caption,
                      int32_t red_action)
{
    (void)mp_screen_put_pic(screen, ID_PLATE, MP_BMP_SPLASHOL, PLATE_X, PLATE_Y, PLATE_W, PLATE_H,
                            false);
    (void)mp_screen_put_pic(screen, ID_OVERLAY, overlay, PLATE_X, PLATE_Y, PLATE_W, PLATE_H, true);
    if (red_caption != NULL) {
        (void)mp_screen_put_text(screen, ID_BACK, red_action, red_caption, MP_FONT_INDUST,
                                 MP_ALIGN_CENTRE, RED_X, RED_Y, RED_W, RED_H);
    }
}

/* ==============================================================================================
 * EINGABE: one value typed on the edit bar. Used by three screens, so it comes first.
 * ============================================================================================ */

static const mp_text_id_t ENTRY_HELP_ROWS[] = {
    MP_TEXT_ENTRY_HELP_CHARS,
    MP_TEXT_ENTRY_HELP_TRIM,
    MP_TEXT_ENTRY_HELP_KEYS,
};

static void build_entry(void)
{
    mp_screen_t *s = &mps.entry;
    const char  *help[sizeof ENTRY_HELP_ROWS / sizeof ENTRY_HELP_ROWS[0]];
    size_t       row;

    if (s->built) {
        return;
    }
    for (row = 0; row < sizeof help / sizeof help[0]; ++row) {
        help[row] = mp_text(ENTRY_HELP_ROWS[row]);
    }
    mp_screen_init(s);
    mp_screens_put_frame(s, MP_BMP_PRESETS, mp_text(MP_TEXT_BACK), SW_ACTION_CANCEL);
    (void)mp_screen_put_text(s, ID_TITLE, SW_ACTION_STATIC, mps.entry_title, MP_FONT_INDUST,
                             MP_ALIGN_CENTRE, PRE_TITLE_X, PRE_TITLE_Y, PRE_TITLE_W, PRE_TITLE_H);
    mps.entry_help = mp_screen_put_list(s, ID_ENTRY_HELP, false, PRE_LIST_X, PRE_LIST_Y,
                                       PRE_LIST_W, PRE_LIST_H);
    mp_screens_set_rows(mps.entry_help, PRE_LIST_W, help, sizeof help / sizeof help[0]);
    (void)mp_screen_put_text(s, ID_ENTRY_APPLY, SW_ACTION_SELECT, mp_text(MP_TEXT_APPLY),
                             MP_FONT_SYSFONT, MP_ALIGN_CENTRE, PRE_SLOT_X, PRE_SLOT_Y,
                             PRE_SLOT_W, PRE_SLOT_H);
    (void)mp_screen_put_text(s, ID_ENTRY_CLEAR, SW_ACTION_SELECT, mp_text(MP_TEXT_CLEAR),
                             MP_FONT_SYSFONT,
                             MP_ALIGN_CENTRE, PRE_SLOT_X, PRE_SLOT_Y + PRE_SLOT_PITCH, PRE_SLOT_W,
                             PRE_SLOT_H);
    (void)mp_screen_put_text(s, ID_ENTRY_CANCEL, SW_ACTION_CANCEL, mp_text(MP_TEXT_CANCEL),
                             MP_FONT_SYSFONT,
                             MP_ALIGN_CENTRE, PRE_SLOT_X, PRE_SLOT_Y + 2 * PRE_SLOT_PITCH,
                             PRE_SLOT_W, PRE_SLOT_H);
    (void)mp_screen_put_text(s, ID_ENTRY_HINT, SW_ACTION_STATIC, mps.entry_hint, MP_FONT_SYSFONT,
                             MP_ALIGN_CENTRE, PRE_BAND_X, PRE_BAND_Y, PRE_BAND_W, PRE_BAND_H);
    (void)mp_screen_put_edit(s, ID_ENTRY_EDIT, &mps.entry_edit, PRE_EDIT_X, PRE_EDIT_Y, PRE_EDIT_W,
                             PRE_EDIT_H);
}

static bool entry_activate(int32_t id, void *ctx)
{
    (void)ctx;
    switch (id) {
    case ID_ENTRY_APPLY:
    case ID_ENTRY_EDIT:   /* Enter in the field */
        mps.entry_applied = true;
        return false;
    case ID_ENTRY_CLEAR:
        mp_screen_set_edit(&mps.entry, ID_ENTRY_EDIT, "");
        return true;
    case ID_ENTRY_CANCEL:
    case ID_BACK:
        return false;
    default:
        return true;
    }
}

/* Runs the entry screen for one value. `max_chars` is what the field takes; `out` receives the
 * typed text, cleaned to printable ASCII with the blanks trimmed, only on an apply. */
bool mp_screens_run_entry(const char *title, const char *hint, const char *initial,
                          size_t max_chars, char *out, size_t out_size)
{
    build_entry();
    FIT_INDUST(title, PRE_TITLE_W, mps.entry_title);
    FIT_SYSFONT(hint, PRE_BAND_W, mps.entry_hint);
    if (max_chars + 2u > sizeof mps.entry_edit.text) {
        max_chars = sizeof mps.entry_edit.text - 2u;
    }
    mp_screen_set_edit_limit(&mps.entry, ID_ENTRY_EDIT, max_chars);
    mp_screen_set_edit(&mps.entry, ID_ENTRY_EDIT, initial != NULL ? initial : "");
    mps.entry_applied = false;
    ++mps.opened;
    (void)mp_screen_run(&mps.entry, ID_ENTRY_EDIT, &entry_activate, NULL, NULL);
    if (!mps.entry_applied) {
        return false;
    }
    mp_settings_clean_text(mps.entry_edit.text, out, out_size);
    return true;
}

/* ==============================================================================================
 * PICKER: one question shaped like a list, borrowed by every choice that is one.
 * ============================================================================================ */

#define ID_PICK_LIST 70
#define ID_PICK_OK   71
#define ID_PICK_HINT 72

static void build_pick(void)
{
    mp_screen_t *s = &mps.pick;

    if (s->built) {
        return;
    }
    mp_screen_init(s);
    mp_screens_put_frame(s, MP_BMP_PRESETS, mp_text(MP_TEXT_BACK), SW_ACTION_CANCEL);
    (void)mp_screen_put_text(s, ID_TITLE, SW_ACTION_STATIC, mps.pick_title, MP_FONT_INDUST,
                             MP_ALIGN_CENTRE, PRE_TITLE_X, PRE_TITLE_Y, PRE_TITLE_W, PRE_TITLE_H);
    mps.pick_list = mp_screen_put_list(s, ID_PICK_LIST, true, PRE_LIST_X, PRE_LIST_Y, PRE_LIST_W,
                                       PRE_LIST_H);
    (void)mp_screen_put_text(s, ID_PICK_OK, SW_ACTION_SELECT, mp_text(MP_TEXT_APPLY),
                             MP_FONT_SYSFONT, MP_ALIGN_CENTRE, PRE_SLOT_X, PRE_SLOT_Y,
                             PRE_SLOT_W, PRE_SLOT_H);
    (void)mp_screen_put_text(s, ID_PICK_HINT, SW_ACTION_STATIC, mps.pick_hint, MP_FONT_SYSFONT,
                             MP_ALIGN_CENTRE, PRE_BAND_X, PRE_BAND_Y, PRE_BAND_W, PRE_BAND_H);
}

static bool pick_activate(int32_t id, void *ctx)
{
    (void)ctx;
    if (id == ID_PICK_OK || id == ID_PICK_LIST) {
        int32_t row = mp_screen_get_state(&mps.pick, ID_PICK_LIST);

        if (row < 0) {
            return true;   /* a paged list deselects; the player picks again */
        }
        mps.pick_result = row;
        return false;
    }
    if (id == ID_BACK) {
        return false;
    }
    return true;
}

int32_t mp_screens_pick(const char *title, const char *hint, const char *const *rows, size_t count,
                        int32_t initial)
{
    if (rows == NULL || count == 0u) {
        return -1;
    }
    build_pick();
    FIT_INDUST(title, PRE_TITLE_W, mps.pick_title);
    FIT_SYSFONT(hint, PRE_BAND_W, mps.pick_hint);
    mp_screens_set_rows(mps.pick_list, PRE_LIST_W, rows, count);
    mp_screen_set_state(&mps.pick, ID_PICK_LIST,
                        initial >= 0 && (size_t)initial < count ? initial : 0);
    mps.pick_result = -1;
    ++mps.opened;
    (void)mp_screen_run(&mps.pick, ID_PICK_LIST, &pick_activate, NULL, NULL);
    return mps.pick_result;
}

/* ==============================================================================================
 * The hub.
 * ============================================================================================ */

/* ==============================================================================================
 * HINWEIS: why a session ended, and the button that closes it.
 * ============================================================================================ */

#define ID_NOTICE_LIST 73
#define ID_NOTICE_OK   74

/* The rows a notice can take: the reason in up to three, a blank one, and the line after it. */
#define NOTICE_ROWS 5u

static void build_notice(void)
{
    mp_screen_t *s = &mps.notice;

    if (s->built) {
        return;
    }
    mp_screen_init(s);
    mp_screens_put_frame(s, MP_BMP_PRESETS, mp_text(MP_TEXT_BACK), SW_ACTION_CANCEL);
    (void)mp_screen_put_text(s, ID_TITLE, SW_ACTION_STATIC, mp_text(MP_TEXT_NOTICE_TITLE),
                             MP_FONT_INDUST, MP_ALIGN_CENTRE, PRE_TITLE_X, PRE_TITLE_Y,
                             PRE_TITLE_W, PRE_TITLE_H);
    mps.notice_list = mp_screen_put_list(s, ID_NOTICE_LIST, false, PRE_LIST_X, PRE_LIST_Y,
                                         PRE_LIST_W, PRE_LIST_H);
    (void)mp_screen_put_text(s, ID_NOTICE_OK, SW_ACTION_SELECT, mp_text(MP_TEXT_NOTICE_OK),
                             MP_FONT_SYSFONT, MP_ALIGN_CENTRE, PRE_SLOT_X, PRE_SLOT_Y,
                             PRE_SLOT_W, PRE_SLOT_H);
}

static bool notice_activate(int32_t id, void *ctx)
{
    (void)ctx;
    return id != ID_NOTICE_OK && id != ID_BACK;
}

/* The sentence cut at its spaces into rows as wide as the list's column in courier. A word wider
 * than the column is cut where the column ends, which no text of this feature's has. */
static size_t wrap_rows(const char *text, char rows[][MP_SCREEN_ROW_TEXT_MAX], size_t max_rows)
{
    const int32_t width = PRE_LIST_W - LIST_INSET;
    size_t        count = 0;
    size_t        at    = 0;

    while (text != NULL && count < max_rows) {
        size_t  length = 0;
        size_t  fits   = 0;   /* the length up to the last word that still fitted */
        int32_t used   = 0;

        while (text[at] == ' ') {
            ++at;
        }
        if (text[at] == '\0') {
            break;
        }
        while (text[at + length] != '\0' && length + 1u < MP_SCREEN_ROW_TEXT_MAX) {
            unsigned char c = (unsigned char)text[at + length];
            int32_t       w = (int32_t)MP_MENU_ADVANCE_COURIER[(c < 0x20u || c > 0x7Eu) ? '?' : c];

            if (used + w > width) {
                break;
            }
            used += w;
            ++length;
            if (text[at + length] == ' ' || text[at + length] == '\0') {
                fits = length;
            }
        }
        if (text[at + length] != '\0' && fits != 0u) {
            length = fits;
        }
        memcpy(rows[count], text + at, length);
        rows[count][length] = '\0';
        ++count;
        at += length;
    }
    return count;
}

void mp_menu_screens_run_notice(const char *text)
{
    char        rows[NOTICE_ROWS][MP_SCREEN_ROW_TEXT_MAX];
    const char *pointers[NOTICE_ROWS];
    size_t      count;
    size_t      i;

    if (text == NULL || !mp_screen_toolkit_ready()) {
        return;
    }
    build_notice();
    count = wrap_rows(text, rows, NOTICE_ROWS - 2u);
    rows[count++][0] = '\0';
    mp_screens_fit(MP_MENU_ADVANCE_COURIER, mp_text(MP_TEXT_NOTICE_ALONE),
                   PRE_LIST_W - LIST_INSET, rows[count], sizeof rows[count]);
    ++count;
    for (i = 0; i < count; ++i) {
        pointers[i] = rows[i];
    }
    mp_screens_set_rows(mps.notice_list, PRE_LIST_W, pointers, count);
    ++mps.opened;
    (void)mp_screen_run(&mps.notice, ID_NOTICE_OK, &notice_activate, NULL, NULL);
}

static void build_hub(void)
{
    mp_screen_t *s = &mps.hub;

    if (s->built) {
        return;
    }
    mp_screen_init(s);
    mp_screens_put_frame(s, MP_BMP_SOPTS, NULL, 0);
    (void)mp_screen_put_text(s, ID_TITLE, SW_ACTION_STATIC, mp_text(MP_TEXT_HUB_TITLE),
                             MP_FONT_INDUST, MP_ALIGN_CENTRE, HUB_TITLE_X, HUB_TITLE_Y,
                             HUB_TITLE_W, HUB_TITLE_H);
    /* The network first, because it decides what the two after it do. */
    (void)mp_screen_put_text(s, ID_HUB_NET, SW_ACTION_SELECT, mps.hub_net, MP_FONT_SYSFONT,
                             MP_ALIGN_CENTRE, HUB_ENTRY_X, HUB_ENTRY_Y, HUB_ENTRY_W, HUB_ENTRY_H);
    (void)mp_screen_put_text(s, ID_HUB_HOST, SW_ACTION_SELECT, mp_text(MP_TEXT_HUB_HOST),
                             MP_FONT_SYSFONT, MP_ALIGN_CENTRE, HUB_ENTRY_X,
                             HUB_ENTRY_Y + HUB_ENTRY_PITCH, HUB_ENTRY_W, HUB_ENTRY_H);
    (void)mp_screen_put_text(s, ID_HUB_JOIN, SW_ACTION_SELECT, mp_text(MP_TEXT_HUB_JOIN),
                             MP_FONT_SYSFONT, MP_ALIGN_CENTRE, HUB_ENTRY_X,
                             HUB_ENTRY_Y + 2 * HUB_ENTRY_PITCH, HUB_ENTRY_W, HUB_ENTRY_H);
    (void)mp_screen_put_text(s, ID_HUB_NAME, SW_ACTION_SELECT, mp_text(MP_TEXT_HUB_NAME),
                             MP_FONT_SYSFONT,
                             MP_ALIGN_CENTRE, HUB_ENTRY_X, HUB_ENTRY_Y + 3 * HUB_ENTRY_PITCH,
                             HUB_ENTRY_W, HUB_ENTRY_H);
    /* The hub's own red button is the OK plate of sopts, a little inside the red button rect. */
    (void)mp_screen_put_text(s, ID_BACK, SW_ACTION_CANCEL, mp_text(MP_TEXT_BACK), MP_FONT_INDUST,
                             MP_ALIGN_CENTRE, HUB_OK_X, HUB_OK_Y, HUB_OK_W, HUB_OK_H);
}

/* The switch's words for the network the settings name. */
static void hub_net_caption(void)
{
    FIT_SYSFONT(mp_text(mp_settings_is_public(mps.settings) ? MP_TEXT_HUB_NET_PUBLIC
                                                            : MP_TEXT_HUB_NET_LAN),
                HUB_ENTRY_W, mps.hub_net);
}

static bool hub_activate(int32_t id, void *ctx)
{
    char name[MP_SETTINGS_NAME_MAX];

    (void)ctx;
    switch (id) {
    case ID_HUB_NET:
        /* The hub stays open: the switch is a choice about what the next two entries do. */
        mps.settings->net = mp_settings_is_public(mps.settings) ? MP_SETTINGS_NET_LAN
                                                                : MP_SETTINGS_NET_PUBLIC;
        hub_net_caption();
        log_info("the menu's network is now %s", mp_settings_is_public(mps.settings)
                                                     ? "PUBLIC: hosting and joining go through "
                                                       "the relay"
                                                     : "the LAN: direct UDP, never the relay");
        return true;
    case ID_HUB_HOST:
        return !mp_screens_run_host();
    case ID_HUB_JOIN:
        return !mp_screens_run_join();
    case ID_HUB_NAME:
        if (mp_screens_run_entry(mp_text(MP_TEXT_NAME_TITLE), mp_text(MP_TEXT_NAME_HINT),
                                 mps.settings->name, MP_SETTINGS_NAME_MAX - 1u, name,
                                 sizeof name) && name[0] != '\0') {
            mp_roster_name_clean(name, mps.settings->name);
            /* The name is the one thing typed here that is not part of an intention to host or to
             * join, and the field said "Uebernehmen". So it goes into the file at once, which does
             * not wait for a session to be started. */
            mp_settings_ini_save_name(mps.settings->name);
        }
        return true;
    case ID_BACK:
        return false;
    default:
        return true;
    }
}

/* ==============================================================================================
 * Entry points.
 * ============================================================================================ */

bool mp_menu_screens_run(mp_settings_t *settings)
{
    if (settings == NULL || !mp_screen_toolkit_ready()) {
        return false;
    }
    mps.settings = settings;
    mps.backup   = *settings;
    mps.applied  = false;
    hub_net_caption();
    build_hub();
    ++mps.opened;
    (void)mp_screen_run(&mps.hub, ID_HUB_HOST, &hub_activate, NULL, NULL);
    if (!mps.applied) {
        /* Backed out, and what was typed stays: a player who went back to change one thing does
         * not type the others again, and the file keeps them for the next launch. Only the side
         * is put back, because none was taken. It used to put everything back, so an address,
         * a session name and a server just remembered were gone with one press of the red
         * button. */
        settings->role = mps.backup.role;
        mp_settings_ini_save(settings);
    }
    mps.settings = NULL;
    return mps.applied;
}

uint32_t mp_menu_screens_opened(void)
{
    return mps.opened;
}

uint32_t mp_menu_screens_announces_heard(void)
{
    return mps.browser.heard;
}
