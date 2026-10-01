/* mp_menu_screens_host.c: HOSTEN, the game, the name, the password, the port and the seats, on
 * the dialog overlay.
 *
 * The game is not on this screen any more. There were two lamps for it, co-op and a deathmatch,
 * and the deathmatch is not offered: it is still built and still travels, and nothing here can
 * choose it. host_mode keeps its name and its three readers, so the day it comes back this file
 * is where the lamps go and nothing else moves. The seat slider still takes its range from the
 * game's own ceiling, which is four today, the bodies one machine can show.
 *
 * The red button goes back. It said STARTEN and started nothing, and there was no other way out
 * than the escape key. Going on to the lobby is now a row that says so, and the red button is
 * what it is on every other screen in the game.
 */
#include "mp_menu_screens_int.h"

#include "mp_settings.h"

#include "common/text.h"

/* Which row of the value list is which. Activating the row opens the entry screen for it, which
 * is what the row's own text promises; the separate "change the selection" entry it used to need
 * is gone. */
#define HOST_ROW_SESSION  0
#define HOST_ROW_PASSWORD 1
#define HOST_ROW_PORT     2
#define HOST_ROWS         3u

/* A public host has no port to pick: the relay is its port. */
static size_t host_rows(void)
{
    return mp_settings_is_public(mps.settings) ? HOST_ROW_PORT : HOST_ROWS;
}

/* The game this screen hosts. Its three readers, the seats, the frame and GO, are unchanged and
 * ask a function rather than a lamp, which is what makes the lamps removable at all. */
static mp_settings_mode_t host_mode(void)
{
    return MP_SETTINGS_MODE_COOP;
}

static void host_refresh_rows(void)
{
    char        rows[HOST_ROWS][CAPTION_MAX];
    const char *pointers[HOST_ROWS];
    size_t      i;

    text_format(rows[HOST_ROW_SESSION], sizeof rows[0], mp_text(MP_TEXT_HOST_ROW_SESSION),
                mps.settings->session_name);
    text_format(rows[HOST_ROW_PASSWORD], sizeof rows[0], mp_text(MP_TEXT_HOST_ROW_PASSWORD),
                mp_text(mps.settings->password[0] != '\0' ? MP_TEXT_HOST_PASSWORD_SET
                                                           : MP_TEXT_HOST_PASSWORD_NONE));
    text_format(rows[HOST_ROW_PORT], sizeof rows[0], mp_text(MP_TEXT_HOST_ROW_PORT),
                (unsigned)mps.settings->port);
    for (i = 0; i < HOST_ROWS; ++i) {
        pointers[i] = rows[i];
    }
    mp_screens_set_rows(mps.host_list, DLG_LIST_W, pointers, host_rows());
}

/* The one switch. There were four: a pickup switch that promised something no game code read,
 * and the two that chose the game. Both times the lamps below moved up, because a column with a
 * gap in it reads as a lamp that failed to draw.
 *
 * The black window the lamps sit in is x 87 to 227 by y 151 to 302 on the overlay, and the
 * shipped screen puts its three checkboxes in it at a pitch of 50, of which the engine keeps only
 * the 34 by 34 of the bitmap. A fourth at that pitch would start at 305, past the window's last
 * row; a pitch of 39 from y 151 seats four exactly (151 to 185, 190 to 224, 229 to 263, 268 to
 * 302), and the one lamp left sits at the first of those rows, which is where the shipped screen
 * puts its own. */
static void build_host(void)
{
    mp_screen_t *s = &mps.host;

    if (s->built) {
        return;
    }
    mp_screen_init(s);
    mp_screens_put_frame(s, MP_BMP_DIALOG, mp_text(MP_TEXT_BACK), SW_ACTION_CANCEL);
    (void)mp_screen_put_text(s, ID_TITLE, SW_ACTION_STATIC, mp_text(MP_TEXT_HOST_TITLE),
                             MP_FONT_INDUST, MP_ALIGN_CENTRE, DLG_TITLE_X, DLG_TITLE_Y,
                             DLG_TITLE_W, DLG_TITLE_H);
    (void)mp_screen_put_lamp(s, ID_HOST_ANNOUNCE, mp_text(MP_TEXT_HOST_ANNOUNCE), true, DLG_LAMP_X,
                             DLG_LAMP_Y, DLG_LAMP_LABEL_W, LAMP_H);
    mps.host_list = mp_screen_put_list(s, ID_HOST_LIST, true, DLG_LIST_X, DLG_LIST_Y, DLG_LIST_W,
                                      DLG_LIST_H);
    (void)mp_screen_put_text(s, ID_HOST_GO, SW_ACTION_SELECT, mp_text(MP_TEXT_HOST_GO),
                             MP_FONT_SYSFONT,
                             MP_ALIGN_CENTRE, DLG_SLOT_X, DLG_SLOT_Y, DLG_SLOT_W, DLG_SLOT_H);
    (void)mp_screen_put_slider(s, ID_HOST_SLIDER,
                               (int32_t)mp_settings_slot_notches(MP_SETTINGS_MODE_COOP), 0,
                               DLG_SLIDER_X, DLG_SLIDER_Y, DLG_SLIDER_W, DLG_SLIDER_H);
    (void)mp_screen_put_text(s, ID_HOST_SLOTS, SW_ACTION_STATIC, mps.host_slots, MP_FONT_SYSFONT,
                             MP_ALIGN_CENTRE, DLG_LABEL_X, DLG_LABEL_Y, DLG_LABEL_W, DLG_LABEL_H);
}

/* The seat slider follows the mode: the range is set from the mode's own ceiling and the knob is
 * pulled inside it, so a session cannot be left holding a count only the other game allowed.
 * Before this both the notch count and the clamp were blind to the mode and sized against
 * sixteen, and the two settings functions that know the ceiling per mode had no callers at all,
 * so a co-op session of sixteen was settable on this screen and reachable from the ini. */
static void host_apply_mode(mp_settings_mode_t mode)
{
    mp_screen_set_slider_notches(&mps.host, ID_HOST_SLIDER,
                                 (int32_t)mp_settings_slot_notches(mode));
}

static uint8_t host_slots(void)
{
    int32_t notch = mp_screen_get_state(&mps.host, ID_HOST_SLIDER);

    return mp_settings_clamp_slots_for((int32_t)MP_SETTINGS_SLOTS_MIN + (notch < 0 ? 0 : notch),
                                       host_mode());
}

static void host_frame(void *ctx)
{
    char text[CAPTION_MAX];

    (void)ctx;
    /* The ceiling stands beside the value, because a slider that stops short of what a player
     * expected reads as a broken slider unless it says where the end is. */
    text_format(text, sizeof text, mp_text(MP_TEXT_HOST_SLOTS), (unsigned)host_slots(),
                (unsigned)mp_settings_slots_max(host_mode()));
    FIT_SYSFONT(text, DLG_LABEL_W, mps.host_slots);
}

/* Opens the entry screen the selected row names. */
static void host_edit_row(void)
{
    int32_t row = mp_screen_get_state(&mps.host, ID_HOST_LIST);

    if (row == HOST_ROW_PASSWORD) {
        char password[MP_SETTINGS_PASSWORD_MAX];

        if (mp_screens_run_entry(mp_text(MP_TEXT_PASSWORD_TITLE),
                                 mp_text(MP_TEXT_HOST_PASSWORD_HINT),
                      mps.settings->password, MP_SETTINGS_PASSWORD_MAX - 1u, password,
                      sizeof password)) {
            memcpy(mps.settings->password, password, sizeof password);
        }
    } else if (row == HOST_ROW_PORT) {
        char        typed[MP_SETTINGS_PORT_TEXT_MAX];
        char        current[MP_SETTINGS_PORT_TEXT_MAX];
        const char *hint = mp_text(MP_TEXT_HOST_PORT_HINT);
        uint16_t    port = mps.settings->port;

        text_format(current, sizeof current, "%u", (unsigned)port);
        /* A refused port says so on the band of the screen it was typed on and asks again. This
         * screen has no painted place for a sentence of its own, and putting one on the plate
         * would be text on the background photograph. */
        while (mp_screens_run_entry(mp_text(MP_TEXT_PORT_TITLE), hint, current,
                                    MP_SETTINGS_PORT_TEXT_MAX - 1u, typed, sizeof typed)) {
            if (mp_settings_parse_port(typed, &port)) {
                mps.settings->port = port;
                break;
            }
            memcpy(current, typed, sizeof current);
            hint = mp_text(MP_TEXT_HOST_PORT_BAD);
        }
    } else {
        char name[MP_SETTINGS_SESSION_NAME_MAX];

        if (mp_screens_run_entry(mp_text(MP_TEXT_SESSION_TITLE), mp_text(MP_TEXT_SESSION_HINT),
                      mps.settings->session_name, MP_SETTINGS_SESSION_NAME_MAX - 1u, name,
                      sizeof name) && name[0] != '\0') {
            memcpy(mps.settings->session_name, name, sizeof name);
        }
    }
    host_refresh_rows();
}

static bool host_activate(int32_t id, void *ctx)
{
    (void)ctx;
    switch (id) {
    case ID_HOST_LIST:   /* the row opens what it names */
        host_edit_row();
        return true;
    case ID_HOST_GO:
        mps.settings->mode  = host_mode();
        mps.settings->slots = host_slots();
        /* Read off the lamp. It used to be forced to true on the way out and written back to
         * the ini, so an Announce of 0 in the file did not survive a single start. */
        /* One lamp, two meanings: the LAN announce on the LAN, the relay's list in public. */
        if (mp_settings_is_public(mps.settings)) {
            mps.settings->list_public = mp_screen_get_state(&mps.host, ID_HOST_ANNOUNCE) == 1;
        } else {
            mps.settings->announce = mp_screen_get_state(&mps.host, ID_HOST_ANNOUNCE) == 1;
        }
        mps.settings->role     = MP_SETTINGS_ROLE_HOST;
        /* The choices are made; the room comes next. A lobby that ends in a level applies and
         * closes this screen with it. One the player backs out of comes back here, one step,
         * as every other back button in these screens does; it used to close this screen too
         * and drop the player in the hub. */
        mps.applied = mp_screens_run_lobby(true);
        return !mps.applied;
    case ID_BACK:
        return false;
    default:
        return true;
    }
}

bool mp_screens_run_host(void)
{
    build_host();
    host_apply_mode(mps.settings->mode);
    mp_screen_set_caption(&mps.host, MP_SCREEN_LAMP_CAPTION_ID(ID_HOST_ANNOUNCE),
                          mp_text(mp_settings_is_public(mps.settings) ? MP_TEXT_HOST_LIST_PUBLIC
                                                                      : MP_TEXT_HOST_ANNOUNCE));
    mp_screen_set_state(&mps.host, ID_HOST_ANNOUNCE,
                        (mp_settings_is_public(mps.settings) ? mps.settings->list_public
                                                             : mps.settings->announce)
                            ? 1
                            : 0);
    mp_screen_set_state(&mps.host, ID_HOST_SLIDER,
                        (int32_t)mp_settings_clamp_slots_for((int32_t)mps.settings->slots,
                                                             mps.settings->mode) -
                            (int32_t)MP_SETTINGS_SLOTS_MIN);
    mp_screen_set_state(&mps.host, ID_HOST_LIST, 0);
    host_refresh_rows();
    host_frame(NULL);
    ++mps.opened;
    /* The focus opens on the first control there is. It used to open on the co-op lamp, which
     * is no longer on the screen, and a focus on a control that is not there is no focus. */
    (void)mp_screen_run(&mps.host, ID_HOST_ANNOUNCE, &host_activate, &host_frame, NULL);
    return mps.applied;
}
