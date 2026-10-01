/* mp_menu.c: one line on the title screen, and the multiplayer screens behind it.
 *
 * The transport is put up when the lobby screen opens, and a client connects from there; this
 * file only hands the screens' choice to the arming. What it does not do is touch the title
 * screen's own input: the line it appends is STATIC, which the engine skips for focus and for
 * hit-testing, because a selectable widget there walks the title loop off the end of its video
 * array and into the saved frame pointer.
 *
 * The line is therefore reached by a key, and the key is written on the line. The key is read in
 * the hull on the navigation code below, and the title screen being the screen on show is a
 * pointer compare against two cells.
 *
 * The same key over a pause screen, while a session is joined, opens the player list: the hull
 * that reads the key stands on the navigation code every screen of the engine reads, so it sees
 * the pause screens as well as the title.
 *
 * The screens are mp_menu_screens.c on the toolkit binding of mp_menu_screen.c, and the ini keys
 * they stand for are read and written in mp_settings_ini.c. This file is the title line, the key,
 * the hull and the hand-over.
 */
#include "mp_menu.h"

#include "mp_bridge.h"
#include "mp_bridge_lobby.h"
#include "mp_cells.h"
#include "mp_menu_screen.h"
#include "mp_menu_screens.h"
#include "mp_menu_sites.h"
#include "mp_pause.h"
#include "mp_settings_ini.h"
#include "mp_start.h"
#include "mp_text.h"
#include "mp_signatures.h"
#include "multiplayer.h"

#include "common/engine_types.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/detour.h"
#include "common/menu_patcher.h"
#include "common/text.h"

#include <windows.h>

#include <stdio.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef int32_t(__cdecl *swmenu_nav_fn)(void);

/* The key that opens the screen. It is printed on the line it opens, because a static widget is
 * invisible to the engine's hit test and there is nothing else to discover it by. */
#define MP_MENU_KEY 'M'

/* The title screen uses ids 0 to 9, so the line appended there starts above them. */
#define ID_TITLE_LINE 10

#define TEXT_H 24

/* The engine's text draw takes its alignment out of the link field. */
#define ALIGN_LEFT ((void *)1)

typedef struct mp_menu_state {
    bool installed;
    bool line_appended;

    uintptr_t current_menu;   /* holds the screen on show, or nothing */
    uintptr_t title_screen;   /* the title screen's own slot */

    mp_settings_t settings;

    /* The title screen's array, copied and appended to. One spare for the line and one for the
     * terminator on top of whatever the screen already had. */
    sw_widget_t title_widgets[24];
    char        title_caption[40];

    mp_menu_applied_fn applied_client;
    mp_menu_arm_fn     arm_client;
    mp_menu_role_fn    armed_role;
    mp_menu_exit_fn    exit_client;
    mp_menu_title_fn   title_client;
    detour_t nav_hull;
    bool     in_screen;       /* our own loop is running, so the hull stands aside */
    bool     key_down;        /* the edge detector for the key that opens the screen */
    uint32_t opened;
    uint32_t applied;
    uint32_t players_opened;
    uint32_t notices;         /* sentences a player was stopped for at the title */
    uint32_t keys_left_to_an_edit;   /* key presses that went into a field being typed into */
} mp_menu_state_t;

static mp_menu_state_t menu;

/* A joining side's own `mode` is a hosting preference out of the ini and says nothing about the
 * session being joined, so the report asks for the mode the session will actually be played in. */
static const char *mode_word(const mp_settings_t *settings)
{
    return mp_settings_effective_mode(settings) == MP_SETTINGS_MODE_TDM ? "team deathmatch"
                                                                       : "co-op";
}

static const char *role_word(const mp_settings_t *settings)
{
    return settings->role == MP_SETTINGS_ROLE_HOST ? "hosting"
           : (settings->role == MP_SETTINGS_ROLE_JOIN ? "joining" : "off");
}

/* ==============================================================================================
 * The screens, and what comes out of them.
 * ============================================================================================ */

static void run_screens(void)
{
    ++menu.opened;
    if (!mp_menu_screens_run(&menu.settings)) {
        return;   /* backed out: the settings are what they were */
    }
    ++menu.applied;
    mp_settings_ini_save(&menu.settings);
    if (menu.applied_client != NULL) {
        menu.applied_client(&menu.settings);
    }
    log_info("the multiplayer menu: %s, %s port %u, %u server(s) remembered, session '%s', "
             "%s, %u slot(s)", role_word(&menu.settings), menu.settings.address,
             (unsigned)menu.settings.port, (unsigned)menu.settings.servers,
             menu.settings.session_name,
             mode_word(&menu.settings),
             (unsigned)menu.settings.slots);
}

/* ==============================================================================================
 * The line on the title screen.
 * ============================================================================================ */

static bool append_title_line(void)
{
    menu_patch_context_t context;
    uintptr_t            widgets = mp_cells_address(MP_CELL_TITLE_WIDGETS);
    uintptr_t            bitmaps = mp_cells_address(MP_CELL_TITLE_BITMAPS);
    uintptr_t            site    = mp_signatures_address(MP_SITE_TITLE_MAIN_MENU);
    size_t               index;

    if (widgets == 0u || bitmaps == 0u || site == 0u) {
        return false;
    }
    text_format(menu.title_caption, sizeof menu.title_caption, "%s",
                mp_text(MP_TEXT_MENU_TITLE_LINE));

    /* The operand of the push that hands the array to the builder, which is what gets repointed:
     * the builder at 0045E7A3 adopts the array it is handed, once, guarded by the magic 0x849EA,
     * and allocates nothing per widget, and this screen hands its array over as a single
     * `push imm32` at 00440340, whose operand at 00440341 is 0x2C into the site. The copy is
     * static storage in this DLL and
     * survives every open and close, because the screen release frees only the 3D widgets and
     * the list boxes with a negative start. */
    if (!menu_patcher_begin(&context, widgets, site + 0x2Cu, bitmaps, menu.title_widgets,
                            sizeof menu.title_widgets / sizeof menu.title_widgets[0])) {
        return false;
    }
    if (menu_patcher_has_widget_id(&context, ID_TITLE_LINE)) {
        return false;
    }
    /* Where the line sits. The right hand column is full, four video tiles of 120 pixels from
     * y = 10 end at y = 470 and the frames around them are painted into the plate, so a fifth
     * row cannot be added without repainting it. The left column below the logo is free, and a
     * brightness census of the plate picked the row: the band at y = 380, 380 wide and 30 high,
     * is the darkest continuous area (mean 35.4, 95th percentile 57, maximum 70), against 65.6,
     * 203 and 253 at y = 150, where the lightsabres are, and 41.3, 158 and 202 at y = 442. */
    if (!menu_patcher_append_label(&context, ID_TITLE_LINE, 10, 380, 380, TEXT_H, 0,
                                   menu.title_caption, &index)) {
        return false;
    }
    /* STATIC, and this is the load bearing part. The title screen's own loop indexes its four
     * background videos with `focusId - 1` and tests only for -1, so a widget it can focus reaches
     * past the end of that array and the call that follows writes through what it finds:
     *
     *     004405BE  cmp [ebp-0x9C],-1            the only guard
     *     004405CD  sub edx,1                    current = focusId - 1
     *     004405DE  mov ecx,[ebp+eax*4-0x24]     aVideo[current], and aVideo has FOUR entries
     *     004405E3  call 0x4981C4                the video's done setter: mov [ecx+0x14],edx
     *
     * The authored ids run 0 to 9, so this widget takes 10 and `current` is 9, which is [ebp+0],
     * the saved frame pointer; id 11 would be the return address. The focus walk is generic and
     * takes every visible widget whose action is not STATIC, by arrow key, tab and mouse, so a
     * selectable line here is a stack corruption waiting for the player to press down twice.
     * STATIC is skipped by the focus walk and by the hit test, so the engine never learns this
     * widget is there, and the key below is how it is reached instead. */
    menu.title_widgets[index].action = SW_ACTION_STATIC;
    menu.title_widgets[index].link   = ALIGN_LEFT;

    return menu_patcher_commit(&context);
}

/* Which screen is on show: the title, some other screen of the engine's, or none. */
typedef enum shown {
    SHOWN_NONE,
    SHOWN_TITLE,
    SHOWN_OTHER
} shown_t;

static shown_t screen_shown(void)
{
    uintptr_t current = 0;

    if (menu.current_menu == 0u || menu.title_screen == 0u) {
        return SHOWN_NONE;
    }
    if (!memory_try_read(menu.current_menu, &current, sizeof current)) {
        return SHOWN_NONE;
    }
    if (current == 0u) {
        return SHOWN_NONE;
    }
    return current == menu.title_screen ? SHOWN_TITLE : SHOWN_OTHER;
}

/* The key, as an edge, and only while this window is the one being typed into: two instances of
 * the game share a keyboard and an asynchronous key state does not know which was meant. */
static bool open_requested(void)
{
    bool down;

    if (GetForegroundWindow() != GetActiveWindow()) {
        menu.key_down = false;
        return false;
    }
    down = (GetAsyncKeyState(MP_MENU_KEY) & 0x8000) != 0;
    if (down && !menu.key_down) {
        menu.key_down = true;
        return true;
    }
    menu.key_down = down;
    return false;
}

/* The hull sits on the navigation code because of WHERE that is called from, not what it returns.
 *
 * A screen of the engine's own is a loop: pump a frame, read the focus, read the navigation code,
 * act on it. Opening a child screen happens in the acting, after the pump has fully returned, and
 * all seventeen shipped screens do it there. The frame hook this used to run from is INSIDE the
 * pump, so it opened a screen and ran a whole nested loop in the middle of the parent's frame.
 *
 * Reading the navigation code is the first thing after the pump this feature can reach, and it is
 * exactly where every screen's own loop stands: the title's, and the pause screens' in a level.
 * The hull stands aside while our screen runs, so our loop reaches the engine through the
 * trampoline and never re-enters. The site is 0045EA62 and its prologue `push ebp; mov ebp,esp;
 * sub esp,8`, six bytes, the first instruction boundary past the five a branch needs.
 *
 * The first build crashed the moment the key was pressed, and the crash report named both faults:
 * an access violation at 0045EB10 writing to 00000268, with 0045F661 (inside the frame pump) and
 * 004405B3 (inside the title screen's loop) on the stack. The address was the edit field setter
 * at 0045EADC, nine instructions that check nothing: it finds the widget in whatever screen is
 * CURRENT, reads the widget's data pointer with no test, and `rep movsd` into it; called before
 * the open, the screen on show was still the title and the copy went wherever the search
 * stopped, so the fields are written after the open now. The two return addresses were the
 * second fault, the screen opened from inside the parent's frame pump, which is what moved the
 * hull here. */
/* The menu's edit widget pointer: the field that typed characters go to. */
#define MENU_EDIT_WIDGET_OFFSET 0x50u

/* Whether the screen on show has a field that typed characters go to. The hotkey is read raw off
 * the keyboard, and the pause family has a screen with an edit widget, the savegame's name, so
 * an M typed into it opened the player list in the middle of the word. The engine hands every
 * character to the menu's edit pointer and not to the focused widget, and moving the focus to
 * nothing leaves that pointer standing: with the mouse resting on empty space the focus is
 * none and the name still takes the keys. So this asks the pointer, not the focus.
 *
 *     00460DFC  mov ecx,[0086D370]        the current menu
 *     00460E02  mov edx,[ecx+0x50]        its edit pointer, pushed as the target of
 *     00460E06  call 00462773             the character message (5) to the widget layer
 *     00462AC7  je 00462AED               setting the focus to nothing jumps over the two
 *                                         stores that would clear or move the edit pointer */
static bool an_edit_takes_keys(void)
{
    uintptr_t menu_ptr = 0;
    uintptr_t edit     = 0;

    if (menu.current_menu == 0u ||
        !memory_try_read(menu.current_menu, &menu_ptr, sizeof menu_ptr) || menu_ptr == 0u ||
        !memory_try_read(menu_ptr + MENU_EDIT_WIDGET_OFFSET, &edit, sizeof edit)) {
        return false;
    }
    return edit != 0u;
}

/* engine: i32 swmenu_takeNavCode(void) */
static int32_t __cdecl hook_take_nav(void)
{
    swmenu_nav_fn original = (swmenu_nav_fn)menu.nav_hull.original;
    int32_t       code     = original();
    int32_t       forced;
    shown_t       shown;

    /* First, before every way this hull stands aside: a session's pause menu that closes itself
     * has to reach whatever screen is on top, the player list of this file included. */
    forced = mp_pause_nav(code);
    if (forced != code) {
        return forced;
    }
    if (menu.in_screen) {
        return code;
    }
    /* A lobby that said go left a start standing. Driving it is two frames of focus and a code,
     * and it happens here because here is where the title screen's own loop asks. */
    if (mp_start_pending()) {
        return mp_start_drive(code);
    }
    if (!menu.line_appended) {
        return code;
    }
    shown = screen_shown();
    if (shown == SHOWN_NONE) {
        return code;
    }
    /* The title is where a session with nothing left to be ends, and where a player sent back from
     * one is told why, before the key and before anything else on the screen. */
    if (shown == SHOWN_TITLE && menu.title_client != NULL) {
        const char *notice = menu.title_client();

        if (notice != NULL) {
            ++menu.notices;
            menu.in_screen = true;
            mp_menu_screens_run_notice(notice);
            menu.in_screen = false;
            return 0;
        }
    }
    /* Asked of the sessions: the bridge's joined flag moves only in a substep, and after a
     * session was left it stood at yes on the title screen, over the options screen too. */
    if (shown == SHOWN_OTHER && !mp_bridge_lobby_connected()) {
        return code;   /* a pause screen with nobody on the other end has no list to show */
    }
    if (shown == SHOWN_OTHER && an_edit_takes_keys()) {
        /* The M belongs to the name being typed, not to this feature. The press is still read,
         * so one held in the field does not open the list the moment the field lets go. */
        if (open_requested()) {
            ++menu.keys_left_to_an_edit;
        }
        return code;
    }
    if (!open_requested()) {
        return code;
    }
    menu.in_screen = true;
    if (shown == SHOWN_TITLE) {
        run_screens();
    } else {
        ++menu.players_opened;
        mp_menu_screens_run_players();
    }
    menu.in_screen = false;

    /* The code that was waiting belonged to the screen the player has just left. Spending it here
     * keeps that screen from acting on it in the frame after the return. */
    return 0;
}

/* ==============================================================================================
 * Installation.
 * ============================================================================================ */

bool mp_menu_install(void)
{
    mp_settings_ini_load(&menu.settings);

    menu.current_menu = mp_cells_address(MP_CELL_CURRENT_MENU);
    menu.title_screen = mp_cells_address(MP_CELL_TITLE_SCREEN);
    if (menu.current_menu == 0u || menu.title_screen == 0u) {
        log_warning("the multiplayer menu cannot open: a front end cell did not resolve");
        return false;
    }
    if (!mp_screen_toolkit_ready()) {
        return false;   /* said by the toolkit binding, site by site */
    }

    menu.installed     = true;
    menu.line_appended = append_title_line();
    if (menu.line_appended &&
        !detour_install(&menu.nav_hull, mp_signatures_address(MP_SITE_SWMENU_TAKE_NAV),
                        (const void *)&hook_take_nav,
                        mp_signatures_prologue(MP_SITE_SWMENU_TAKE_NAV))) {
        log_warning("the multiplayer menu is unreachable: the navigation code would not take a "
                    "hull, so the line on the title screen answers nothing");
        menu.line_appended = false;
    }
    if (!menu.line_appended) {
        log_warning("the multiplayer menu is unreachable: the title screen would not take the "
                    "line, so the settings are whatever the ini says");
    } else {
        log_info("the multiplayer menu is on the title screen: %s, %s port %u; the same key over "
                 "a pause screen opens the player list while a session is joined",
                 role_word(&menu.settings), menu.settings.address, (unsigned)menu.settings.port);
    }
    return true;
}

void mp_menu_set_applied_client(mp_menu_applied_fn client)
{
    menu.applied_client = client;
}

void mp_menu_set_arm_client(mp_menu_arm_fn client, mp_menu_role_fn armed_role)
{
    menu.arm_client = client;
    menu.armed_role = armed_role;
}

void mp_menu_set_session_clients(mp_menu_exit_fn exit_client, mp_menu_title_fn title_client)
{
    menu.exit_client  = exit_client;
    menu.title_client = title_client;
}

void mp_menu_exit(mp_exit_why_t why)
{
    if (menu.exit_client == NULL) {
        log_warning("the screens asked to leave the session and nothing is wired to take it down, "
                    "so it stays up");
        return;
    }
    menu.exit_client(why);
}

uint32_t mp_menu_armed_role(void)
{
    return menu.armed_role != NULL ? menu.armed_role() : 0u;
}

bool mp_menu_arm(const mp_settings_t *settings)
{
    return menu.arm_client != NULL && settings != NULL && menu.arm_client(settings);
}

const mp_settings_t *mp_menu_settings(void)
{
    return &menu.settings;
}

void mp_menu_report(void)
{
    if (!menu.installed) {
        return;
    }
    log_info("  the menu: %s, %u time(s) opened, %u applied, %u player list(s), %u notice(s) "
             "at the title, %u key(s) left "
             "to a field being typed into, %u screen(s) in all, %u announce(s) heard; %s, %s "
             "port %u, %u remembered",
             menu.line_appended ? "on the title screen" : "unreachable", (unsigned)menu.opened,
             (unsigned)menu.applied, (unsigned)menu.players_opened, (unsigned)menu.notices,
             (unsigned)menu.keys_left_to_an_edit,
             (unsigned)mp_menu_screens_opened(), (unsigned)mp_menu_screens_announces_heard(),
             role_word(&menu.settings), menu.settings.address, (unsigned)menu.settings.port,
             (unsigned)menu.settings.servers);
}
