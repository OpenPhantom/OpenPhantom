/* mp_menu_screens_join.c: BEITRETEN, the server list on the presets overlay.
 *
 * One screen for both networks, opened for the one the hub names. On the LAN the list is what the
 * LAN announce and the favourites say, and the field takes an address. In public the list is the
 * relay's, the field takes a session's code, and the two buttons that remember and forget an
 * address are hidden: a code is good for one session and is not worth keeping.
 */
#include "mp_menu_screens_int.h"

#include "mp_bridge_statement.h"

#include "common/text.h"

/* How long a sentence an action put on the band stands before the list's state takes it back. */
#define JOIN_HINT_HOLD_MS 4000u

/* The code field takes the eight symbols, the hyphen and a couple of blanks a player types. */
#define JOIN_CODE_CHARS 11u

/* ==============================================================================================
 * BEITRETEN: the server list, three actions, a typed address or code.
 * ============================================================================================ */

static void build_join(void)
{
    mp_screen_t *s = &mps.join;

    if (s->built) {
        return;
    }
    mp_screen_init(s);
    mp_screens_put_frame(s, MP_BMP_PRESETS, mp_text(MP_TEXT_BACK), SW_ACTION_CANCEL);
    (void)mp_screen_put_text(s, ID_TITLE, SW_ACTION_STATIC, mp_text(MP_TEXT_JOIN_TITLE),
                             MP_FONT_INDUST,
                             MP_ALIGN_CENTRE, PRE_TITLE_X, PRE_TITLE_Y, PRE_TITLE_W, PRE_TITLE_H);
    mps.join_list = mp_screen_put_list(s, ID_JOIN_LIST, true, PRE_LIST_X, PRE_LIST_Y, PRE_LIST_W,
                                      PRE_LIST_H);
    (void)mp_screen_put_text(s, ID_JOIN_GO, SW_ACTION_SELECT, mp_text(MP_TEXT_JOIN_GO),
                             MP_FONT_SYSFONT,
                             MP_ALIGN_CENTRE, PRE_SLOT_X, PRE_SLOT_Y, PRE_SLOT_W, PRE_SLOT_H);
    (void)mp_screen_put_text(s, ID_JOIN_KEEP, SW_ACTION_SELECT, mp_text(MP_TEXT_JOIN_KEEP),
                             MP_FONT_SYSFONT,
                             MP_ALIGN_CENTRE, PRE_SLOT_X, PRE_SLOT_Y + PRE_SLOT_PITCH, PRE_SLOT_W,
                             PRE_SLOT_H);
    (void)mp_screen_put_text(s, ID_JOIN_FORGET, SW_ACTION_SELECT, mp_text(MP_TEXT_JOIN_FORGET),
                             MP_FONT_SYSFONT,
                             MP_ALIGN_CENTRE, PRE_SLOT_X, PRE_SLOT_Y + 2 * PRE_SLOT_PITCH,
                             PRE_SLOT_W, PRE_SLOT_H);
    (void)mp_screen_put_text(s, ID_JOIN_HINT, SW_ACTION_STATIC, mps.join_hint, MP_FONT_SYSFONT,
                             MP_ALIGN_CENTRE, PRE_BAND_X, PRE_BAND_Y, PRE_BAND_W, PRE_BAND_H);
    (void)mp_screen_put_edit(s, ID_JOIN_EDIT, &mps.join_address, PRE_EDIT_X, PRE_EDIT_Y,
                             PRE_EDIT_W, PRE_EDIT_H);
}

/* This side's content fingerprint as a host announces its own: the fingerprint of the statement a
 * join request carries. A host announces it from the moment its transport is armed, so a browser
 * that judged the rows against 0 called every host on the LAN one it cannot join. The statement is
 * made on this first question if nothing has made it yet, so the browser has it in the menu before
 * any transport stands; 0 only before the shot module's init has run. */
static uint32_t own_fingerprint(void)
{
    return mp_bridge_statement_fingerprint();
}

static void join_hint(const char *text)
{
    FIT_SYSFONT(text, PRE_BAND_W, mps.join_hint);
    mps.join_hint_at = mp_wallclock_ms();
    mps.join_hint_held = true;
}

/* The numbers every row shows after its name: who is in, a password, a build that differs. */
static void row_rest(const mp_announce_t *announce, const char *source, char *out, size_t size)
{
    text_format(out, size, " %u/%u %s%s%s", (unsigned)announce->players, (unsigned)announce->slots,
                source, (announce->flags & MP_ANNOUNCE_F_PASSWORD) != 0u ? "*" : "",
                announce->wire != (uint8_t)MP_WIRE_VERSION ? "!" : "");
}

/* One row: the name or the endpoint in the first two thirds, then who is in and where it came
 * from. A host on another wire version gets a mark, because the handshake will refuse it and the
 * player should see why before trying. */
static void join_row_text(const mp_browser_row_t *row, char *out, size_t out_size)
{
    char caption[MP_ANNOUNCE_NAME_MAX + MP_SETTINGS_ENDPOINT_MAX];
    char name[32];
    char rest[32];
    bool live = mp_browser_row_live(&mps.browser, row);
    const char *source = mp_text(row->favourite ? MP_TEXT_SOURCE_FAVOURITE
                                                : (row->typed ? MP_TEXT_SOURCE_TYPED
                                                              : MP_TEXT_SOURCE_LAN));

    (void)mp_browser_row_caption(&mps.browser, row, caption);
    mp_screens_fit(MP_MENU_ADVANCE_COURIER, caption, 170, name, sizeof name);
    if (row->heard && live) {
        row_rest(&row->announce, source, rest, sizeof rest);
    } else {
        text_format(rest, sizeof rest, " - %s", source);
    }
    text_format(out, out_size, "%s%s", name, rest);
}

static void join_refresh_rows(void)
{
    char        text[MP_BROWSER_ROWS_MAX][MP_SCREEN_ROW_TEXT_MAX];
    const char *rows[MP_BROWSER_ROWS_MAX];
    size_t      position;

    mps.join_rows = 0;
    for (position = 0; position < MP_BROWSER_ROWS_MAX; ++position) {
        size_t index = mp_browser_ordered(&mps.browser, position);

        if (index >= MP_BROWSER_ROWS_MAX) {
            break;
        }
        join_row_text(&mps.browser.row[index], text[position], sizeof text[position]);
        rows[position] = text[position];
        mps.join_order[position] = index;
        ++mps.join_rows;
    }
    mp_screens_set_rows(mps.join_list, PRE_LIST_W, rows, mps.join_rows);
}

/* The relay's list as rows, in the relay's order: the oldest listed session first. */
static void join_refresh_public_rows(void)
{
    char        text[MP_SCREEN_LIST_ROWS_MAX][MP_SCREEN_ROW_TEXT_MAX];
    const char *rows[MP_SCREEN_LIST_ROWS_MAX];
    size_t      count = mp_relay_list_count(&mps.relay_list);
    size_t      i;

    if (count > MP_SCREEN_LIST_ROWS_MAX) {
        count = MP_SCREEN_LIST_ROWS_MAX;
    }
    for (i = 0; i < count; ++i) {
        const mp_relay_list_row_t *row = mp_relay_list_row(&mps.relay_list, i);
        char                       name[32];
        char                       rest[32];

        mp_screens_fit(MP_MENU_ADVANCE_COURIER, row->announce.name, 170, name, sizeof name);
        row_rest(&row->announce, mp_text(MP_TEXT_SOURCE_PUBLIC), rest, sizeof rest);
        text_format(text[i], sizeof text[i], "%s%s", name, rest);
        rows[i] = text[i];
    }
    mps.join_rows = count;
    mp_screens_set_rows(mps.join_list, PRE_LIST_W, rows, count);
}

static void join_frame_lan(void)
{
    uint8_t  datagram[64];
    char     from[MP_DISCOVERY_ADDRESS_MAX];
    char     typed[MP_SETTINGS_ENDPOINT_MAX];
    char     address[MP_SETTINGS_ADDRESS_MAX];
    uint16_t port;
    size_t   bytes;

    if (!mps.listener.up && !mps.listener_failed_logged) {
        if (!mp_discovery_open_listener(&mps.listener, (uint16_t)MP_ANNOUNCE_PORT)) {
            mps.listener_failed_logged = true;
            log_warning("the server list cannot hear the LAN: the announce port %u would not "
                        "open (error %d); favourites and a typed address still work",
                        (unsigned)MP_ANNOUNCE_PORT, mp_discovery_last_error(&mps.listener));
        }
    }
    /* Asked every frame and not only when the list is set up, so the rows are judged again the
     * moment the fingerprint becomes known; the browser does nothing while it has not changed. */
    mp_browser_set_fingerprint(&mps.browser, own_fingerprint());
    while ((bytes = mp_discovery_receive(&mps.listener, datagram, sizeof datagram, from)) != 0u) {
        (void)mp_browser_heard(&mps.browser, from, datagram, bytes);
    }
    mp_browser_tick(&mps.browser, mp_wallclock_ms());

    /* What is in the address field is a row of its own while it parses. */
    mp_screen_get_edit(&mps.join, ID_JOIN_EDIT, typed, sizeof typed);
    port = mps.settings->port;
    if (typed[0] != '\0' && mp_settings_parse_endpoint(typed, address, sizeof address, &port)) {
        mp_browser_set_typed(&mps.browser, address, port);
    } else {
        mp_browser_set_typed(&mps.browser, "", 0u);
    }
    join_refresh_rows();
}

/* The relay asked, the rows redrawn, and the band says where the list stands unless an action
 * has just put a sentence there. */
/* A row the player marked: its code goes into the field, which is what Beitreten joins. */
static void join_take_row(int32_t state)
{
    char text[MP_RELAY_CODE_TEXT_BYTES];

    if (state >= 0 && (size_t)state < mps.join_rows) {
        mp_relay_code_text(mp_relay_list_row(&mps.relay_list, (size_t)state)->code, text);
        mp_screen_set_edit(&mps.join, ID_JOIN_EDIT, text);
    }
}

static void join_frame_public(void)
{
    char    text[CAPTION_MAX];
    int32_t state  = mp_screen_get_state(&mps.join, ID_JOIN_LIST);
    bool    marked = state >= 0 && (size_t)state < mps.join_rows;
    uint8_t code[MP_RELAY_CODE_BYTES];
    size_t  i;

    /* A mark that moved since the last frame was moved by the player, with the arrow keys or the
     * mouse; one the list set on its own when its first rows came is not. */
    if (marked && state != mps.join_mark_seen && mps.join_mark_seen >= 0) {
        join_take_row(state);
    }
    if (marked) {
        memcpy(code, mp_relay_list_row(&mps.relay_list, (size_t)state)->code, sizeof code);
    }
    mp_relay_list_pump(&mps.relay_list);
    join_refresh_public_rows();
    /* The rows are replaced every round, and a session above the marked one may have gone: the
     * mark follows the session, not the row it stood on. */
    for (i = 0; marked && i < mps.join_rows; ++i) {
        if (memcmp(mp_relay_list_row(&mps.relay_list, i)->code, code, sizeof code) == 0) {
            if ((int32_t)i != state) {
                mp_screen_set_state(&mps.join, ID_JOIN_LIST, (int32_t)i);
            }
            break;
        }
    }
    mps.join_mark_seen = mps.join_rows != 0u ? mp_screen_get_state(&mps.join, ID_JOIN_LIST) : -1;
    if (mps.join_hint_held && mp_wallclock_ms() - mps.join_hint_at < JOIN_HINT_HOLD_MS) {
        return;
    }
    mps.join_hint_held = false;
    switch (mp_relay_list_state(&mps.relay_list)) {
    case MP_RELAY_LISTING_ANSWERING:
        text_format(text, sizeof text, mp_text(MP_TEXT_JOIN_PUBLIC_COUNT),
                    (unsigned)mp_relay_list_count(&mps.relay_list));
        FIT_SYSFONT(text, PRE_BAND_W, mps.join_hint);
        return;
    case MP_RELAY_LISTING_SILENT:
        FIT_SYSFONT(mp_text(MP_TEXT_JOIN_PUBLIC_SILENT), PRE_BAND_W, mps.join_hint);
        return;
    case MP_RELAY_LISTING_ASKING:
    default:
        FIT_SYSFONT(mp_text(MP_TEXT_JOIN_PUBLIC_ASKING), PRE_BAND_W, mps.join_hint);
        return;
    }
}

static void join_frame(void *ctx)
{
    (void)ctx;
    if (mps.join_public) {
        join_frame_public();
    } else {
        join_frame_lan();
    }
}

/* The row under the selection, or NULL when the list is empty or nothing is selected. */
static const mp_browser_row_t *join_selected(void)
{
    int32_t state = mp_screen_get_state(&mps.join, ID_JOIN_LIST);

    if (state < 0 || (size_t)state >= mps.join_rows) {
        return NULL;
    }
    return &mps.browser.row[mps.join_order[(size_t)state]];
}

/* The endpoint a Beitreten or a Merken means: the selected row, else the typed address. */
static bool join_target(char *address, size_t address_size, uint16_t *port,
                        const mp_browser_row_t **row, bool prefer_typed)
{
    const mp_browser_row_t *selected = join_selected();
    char                    typed[MP_SETTINGS_ENDPOINT_MAX];

    *row = NULL;
    /* Enter in the address field means the address in the field. The list always has a
     * marked row once it has rows, and a live LAN row sorts above a typed one, so without
     * this a player who typed an address while a LAN host was announcing joined the LAN
     * host instead, and could not see why. */
    if (prefer_typed) {
        mp_screen_get_edit(&mps.join, ID_JOIN_EDIT, typed, sizeof typed);
        *port = mps.settings->port;
        if (typed[0] != '\0' &&
            mp_settings_parse_endpoint(typed, address, address_size, port)) {
            return true;
        }
    }
    if (selected != NULL) {
        strncpy(address, selected->address, address_size - 1u);
        address[address_size - 1u] = '\0';
        *port = selected->port;
        *row  = selected;
        return true;
    }
    mp_screen_get_edit(&mps.join, ID_JOIN_EDIT, typed, sizeof typed);
    *port = mps.settings->port;
    return typed[0] != '\0' && mp_settings_parse_endpoint(typed, address, address_size, port);
}

/* Asks for the password, or does not.
 *
 * A session that was HEARD says in its announce whether it wants one, and one that does is asked
 * about here, before the join. What was NOT heard is not asked about at all: a typed address and
 * a session code say nothing, and the question used to be put to them in advance, in case, so
 * every player joining a public session by its code was asked for a password almost no host has.
 *
 * Such a join goes out with whatever is held. A host that wants none ignores it (mp_session); a
 * host that wants one refuses the join and names the reason, and the LOBBY asks then
 * (mp_menu_screens_lobby), which is the first moment anything on this side knows.
 *
 * False when the player backed out of the question, which leaves them on the list. */
static bool join_password(const mp_announce_t *heard)
{
    bool wants = heard != NULL && (heard->flags & MP_ANNOUNCE_F_PASSWORD) != 0u;
    char hint[CAPTION_MAX];
    char typed[MP_SETTINGS_PASSWORD_MAX];

    switch (mp_lobby_password_question(heard != NULL, wants)) {
    case MP_LOBBY_PASSWORD_NONE:
        mps.settings->join_password[0] = '\0';   /* it said it wants none, so none is offered */
        return true;
    case MP_LOBBY_PASSWORD_WAIT:
        return true;
    case MP_LOBBY_PASSWORD_ASK:
    default:
        break;
    }
    text_format(hint, sizeof hint, mp_text(MP_TEXT_JOIN_PASSWORD_FOR), heard->name);
    if (!mp_screens_run_entry(mp_text(MP_TEXT_PASSWORD_TITLE), hint, mps.settings->join_password,
                   MP_SETTINGS_PASSWORD_MAX - 1u, typed, sizeof typed)) {
        return false;
    }
    memcpy(mps.settings->join_password, typed, sizeof typed);
    return true;
}

/* The game a heard session plays, 0 for one that was not heard. The bit is still read, because
 * the announcement still carries it; a game this build does not offer is not taken from it. The
 * handshake then names this side's own game, the host compares the two and refuses with a sentence
 * the player can read, which is a better answer than joining a round with no menu for it. */
static uint8_t heard_mode(const mp_announce_t *heard)
{
    uint8_t mode;

    if (heard == NULL) {
        return 0u;
    }
    mode = (heard->flags & MP_ANNOUNCE_F_TDM) != 0u ? (uint8_t)MP_SETTINGS_MODE_TDM
                                                     : (uint8_t)MP_SETTINGS_MODE_COOP;
    return mp_settings_mode_offered((int32_t)mode) ? mode : (uint8_t)MP_SETTINGS_MODE_COOP;
}

static bool join_go_lan(bool prefer_typed)
{
    char                    address[MP_SETTINGS_ADDRESS_MAX];
    uint16_t                port;
    const mp_browser_row_t *row;
    const mp_announce_t    *heard;

    if (!join_target(address, sizeof address, &port, &row, prefer_typed)) {
        join_hint(mp_text(MP_TEXT_JOIN_PICK_HINT));
        return true;
    }
    heard = row != NULL && row->heard ? &row->announce : NULL;
    mps.settings->join_mode = heard_mode(heard);
    if (!join_password(heard)) {
        return true;
    }
    strncpy(mps.settings->address, address, sizeof mps.settings->address - 1u);
    mps.settings->address[sizeof mps.settings->address - 1u] = '\0';
    mps.settings->port = port;
    mps.settings->role = MP_SETTINGS_ROLE_JOIN;
    /* Into the room, where the player finds out whether the join worked and why not. Backing
     * out of it comes back to this list, one step, rather than to the hub. */
    mps.applied = mp_screens_run_lobby(false);
    return !mps.applied;
}

/* The session a public Beitreten means is the code in the field: the player sees it, typed it or
 * put it there by marking a row. Only an empty field falls back to the marked row. A code that is
 * listed borrows its row's announce, for the password question and the game. */
static bool join_go_public(void)
{
    char          typed[MP_SETTINGS_ENDPOINT_MAX];
    uint8_t       code[MP_RELAY_CODE_BYTES];
    mp_announce_t announce;
    bool          heard = false;
    int32_t       state = mp_screen_get_state(&mps.join, ID_JOIN_LIST);
    size_t        i;

    mp_screen_get_edit(&mps.join, ID_JOIN_EDIT, typed, sizeof typed);
    if (typed[0] != '\0') {
        if (!mp_relay_code_parse(typed, code)) {
            join_hint(mp_text(MP_TEXT_JOIN_CODE_BAD));
            return true;
        }
    } else if (state >= 0 && (size_t)state < mps.join_rows) {
        memcpy(code, mp_relay_list_row(&mps.relay_list, (size_t)state)->code, sizeof code);
    } else {
        join_hint(mp_text(MP_TEXT_JOIN_CODE_HINT));
        return true;
    }
    for (i = 0; i < mps.join_rows && !heard; ++i) {
        const mp_relay_list_row_t *row = mp_relay_list_row(&mps.relay_list, i);

        if (memcmp(row->code, code, sizeof code) == 0) {
            announce = row->announce;   /* a copy: the list moves on while the password is asked */
            heard    = true;
        }
    }
    mps.settings->join_mode = heard_mode(heard ? &announce : NULL);
    if (!join_password(heard ? &announce : NULL)) {
        return true;
    }
    mp_relay_code_text(code, mps.settings->join_code);
    mp_screen_set_edit(&mps.join, ID_JOIN_EDIT, mps.settings->join_code);
    mps.settings->role = MP_SETTINGS_ROLE_JOIN;
    mps.applied = mp_screens_run_lobby(false);
    return !mps.applied;
}

static bool join_activate(int32_t id, void *ctx)
{
    char                    address[MP_SETTINGS_ADDRESS_MAX];
    uint16_t                port;
    const mp_browser_row_t *row;
    size_t                  i;

    (void)ctx;
    switch (id) {
    case ID_JOIN_GO:
        return mps.join_public ? join_go_public() : join_go_lan(false);
    case ID_JOIN_EDIT:   /* Enter in the field: what is in the field, not the marked row */
        return mps.join_public ? join_go_public() : join_go_lan(true);
    case ID_JOIN_LIST:   /* a row chosen: in public its code goes into the field */
        if (mps.join_public) {
            join_take_row(mp_screen_get_state(&mps.join, ID_JOIN_LIST));
        }
        return true;
    case ID_JOIN_KEEP:
        if (mps.join_public) {
            return true;
        }
        if (!join_target(address, sizeof address, &port, &row, false)) {
            join_hint(mp_text(MP_TEXT_JOIN_NOTHING_TO_KEEP));
            return true;
        }
        if (mp_settings_remember(mps.settings, address, port)) {
            mp_browser_set_favourites(&mps.browser, mps.settings);
            join_hint(mp_text(MP_TEXT_JOIN_KEPT));
        }
        return true;
    case ID_JOIN_FORGET:
        row = mps.join_public ? NULL : join_selected();
        if (row == NULL || !row->favourite) {
            join_hint(mp_text(MP_TEXT_JOIN_FORGET_ONLY_FAV));
            return true;
        }
        for (i = 0; i < mps.settings->servers; ++i) {
            if (mps.settings->server[i].port == row->port &&
                strncmp(mps.settings->server[i].address, row->address,
                        MP_SETTINGS_ADDRESS_MAX) == 0) {
                (void)mp_settings_forget(mps.settings, i);
                break;
            }
        }
        /* The browser keeps the row until it goes stale; rebuilt from the list it forgets it. */
        mp_browser_init(&mps.browser, (uint8_t)MP_WIRE_VERSION, own_fingerprint());
        mp_browser_set_favourites(&mps.browser, mps.settings);
        join_hint(mp_text(MP_TEXT_JOIN_FORGOTTEN));
        return true;
    case ID_BACK:
        return false;
    default:
        return true;   /* the list: its selection moved, nothing else */
    }
}

/* The field and the two LAN buttons for the network the screen opens on. */
static void join_open_for_the_network(void)
{
    char endpoint[MP_SETTINGS_ENDPOINT_MAX];

    mp_screen_set_visible(&mps.join, ID_JOIN_KEEP, !mps.join_public);
    mp_screen_set_visible(&mps.join, ID_JOIN_FORGET, !mps.join_public);
    if (mps.join_public) {
        if (!mp_relay_list_open(&mps.relay_list, mp_relay_services_real())) {
            log_warning("the public list is not asked for; a typed code still joins");
        }
        mp_screen_set_edit(&mps.join, ID_JOIN_EDIT, mps.settings->join_code);
        mps.join_mark_seen = -1;
        mp_screen_set_edit_limit(&mps.join, ID_JOIN_EDIT, JOIN_CODE_CHARS);
        join_hint(mp_text(MP_TEXT_JOIN_CODE_HINT));
        join_refresh_public_rows();
        return;
    }
    mp_browser_init(&mps.browser, (uint8_t)MP_WIRE_VERSION, own_fingerprint());
    mp_browser_set_favourites(&mps.browser, mps.settings);
    join_hint(mp_text(MP_TEXT_JOIN_PICK_HINT));
    /* Offered whenever there is one. It used to be offered only while the remembered role
     * still said join, which meant an ini carried from the other machine held the right
     * address and did not put it in the box. */
    if (mp_settings_format_endpoint(mps.settings->address, mps.settings->port, endpoint,
                                    sizeof endpoint)) {
        mp_screen_set_edit(&mps.join, ID_JOIN_EDIT, endpoint);   /* the last one joined */
    } else {
        mp_screen_set_edit(&mps.join, ID_JOIN_EDIT, "");
    }
    mp_screen_set_edit_limit(&mps.join, ID_JOIN_EDIT, MP_SETTINGS_ENDPOINT_MAX - 1u);
    join_refresh_rows();
}

bool mp_screens_run_join(void)
{
    build_join();
    mps.join_public = mp_settings_is_public(mps.settings);
    mps.join_rows = 0u;
    join_open_for_the_network();
    mps.listener_failed_logged = false;
    ++mps.opened;
    (void)mp_screen_run(&mps.join, ID_JOIN_LIST, &join_activate, &join_frame, NULL);
    if (mps.join_public) {
        mp_relay_list_close(&mps.relay_list);
    }
    mp_discovery_close(&mps.listener);   /* the port is shared, but not held while nobody looks */
    return mps.applied;
}
