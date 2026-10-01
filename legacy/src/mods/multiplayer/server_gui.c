/* server_gui.c: the window an operator sets the server up in and watches it from.
 *
 * Plain Win32 and nothing else. No resource script, no manifest, no dialog template: every
 * control is created in code, which keeps the whole window in one file a reader can follow and
 * keeps the server a single executable with no companion files to lose. The layout is a column
 * of labelled fields on the left and the log on the right, laid out from constants rather than
 * from a designer, so a change is arithmetic rather than a binary blob.
 *
 * The window never touches the server. It hands a copy of the settings to server_run and reads a
 * block of numbers back; the server has its own thread and its own pace. That is what makes it
 * safe to drag this window, open a menu or hold a scrollbar while a match is running, all of
 * which stop a message queue and none of which may stop a server.
 *
 * The log arrives on a third thread, which is neither of those two: the log's writer thread hands
 * every formatted line to `take_line` below. That runs OUTSIDE this window's thread, so it must
 * not touch a control. It appends into a small buffer under a lock, and a timer on the window's
 * own thread moves whatever has gathered into the edit control in one go. Batching is not only
 * for safety: appending a line at a time to an edit control at five hundred lines a second is
 * how you make a window that cannot be closed.
 *
 * SIZE NOTE: a little under 600 lines. It is one window, and the excess over the
 * preferred band is the control table and the layout arithmetic, both of which are a list rather
 * than logic. The next seam is the settings pane: the twelve controls between START and the log,
 * their reading and their writing, about 200 lines that touch nothing else in the file.
 */
#include "server_config.h"
#include "server_run.h"

#include "mp_score.h"
#include "mp_server_log.h"

#include "common/text.h"

#include <windows.h>
#include <commctrl.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ==============================================================================================
 * The controls.
 * ============================================================================================ */

#define ID_PORT        1001
#define ID_NAME        1002
#define ID_PASSWORD    1003
#define ID_SLOTS       1004
#define ID_LEVEL       1005
#define ID_SCORE_LIMIT 1006
#define ID_TIME_LIMIT  1007
#define ID_TEAMS       1008
#define ID_FRIENDLY    1009
#define ID_RESPAWN     1010
#define ID_START       1011
#define ID_STOP        1012
#define ID_SAVE        1013
#define ID_LOG         1014
#define ID_STATUS      1015
#define ID_CAT_SESSION 1020
#define ID_CAT_MATCH   1021
#define ID_CAT_PICKUP  1022
#define ID_CAT_PING    1023
#define ID_CAT_POS     1024

#define LEFT_W    260
#define ROW_H      26
#define LABEL_W   110
#define FIELD_W   140
#define MARGIN     12
#define WINDOW_W  980
#define WINDOW_H  620
#define TIMER_ID     1
#define TIMER_MS   100

/* How much log the window keeps. The file keeps everything; this is what a person can scroll. */
#define LOG_VIEW_CHARS 240000u

/* What has arrived from the log's writer thread and not yet been put in the control. Sized so
 * that a burst between two timer ticks fits: five hundred lines a second, a tenth of a second, a
 * hundred and sixty characters a line. */
#define PENDING_CHARS 65536u

typedef struct gui {
    HWND window;
    HWND log;
    HWND status;
    HFONT font;

    server_config_t config;

    CRITICAL_SECTION pending_lock;
    char             pending[PENDING_CHARS];
    size_t           pending_used;
    bool             pending_overflowed;
} gui_t;

static gui_t g;

/* ==============================================================================================
 * The log, arriving from a thread that is not this one.
 * ============================================================================================ */

/* Called by the log's writer thread. It may not touch a control and it may not block for long:
 * everything it does is under one short lock and into plain memory. */
static void take_line(const char *line, void *context)
{
    size_t length;

    (void)context;
    if (line == NULL) {
        return;
    }
    length = strlen(line);

    EnterCriticalSection(&g.pending_lock);
    if (g.pending_used + length + 3u >= PENDING_CHARS) {
        /* The window is behind. Dropping here rather than growing is deliberate: what is dropped
         * is only the SCREEN copy, and the file has every line. */
        g.pending_overflowed = true;
    } else {
        memcpy(g.pending + g.pending_used, line, length);
        g.pending_used += length;
        g.pending[g.pending_used++] = '\r';
        g.pending[g.pending_used++] = '\n';
        g.pending[g.pending_used]   = '\0';
    }
    LeaveCriticalSection(&g.pending_lock);
}

/* On the window's own thread, once a tenth of a second: everything that gathered, in one append.
 * The selection is put at the end first, so the append scrolls rather than replacing. */
static void flush_pending(void)
{
    char   batch[PENDING_CHARS];
    bool   overflowed;
    size_t used;
    int    length;

    EnterCriticalSection(&g.pending_lock);
    used       = g.pending_used;
    overflowed = g.pending_overflowed;
    if (used != 0u) {
        memcpy(batch, g.pending, used);
    }
    batch[used]          = '\0';
    g.pending_used       = 0;
    g.pending_overflowed = false;
    LeaveCriticalSection(&g.pending_lock);

    if (used == 0u && !overflowed) {
        return;
    }
    /* Keep the control from growing without bound. It is trimmed from the front, in one go, when
     * it gets long: a person scrolls back a few hundred lines, not a few hundred thousand. */
    length = GetWindowTextLengthA(g.log);
    if ((size_t)length + used >= LOG_VIEW_CHARS) {
        SendMessageA(g.log, EM_SETSEL, 0, (LPARAM)(length / 2));
        SendMessageA(g.log, EM_REPLACESEL, FALSE, (LPARAM)"");
        length = GetWindowTextLengthA(g.log);
    }
    SendMessageA(g.log, EM_SETSEL, (WPARAM)length, (LPARAM)length);
    if (used != 0u) {
        SendMessageA(g.log, EM_REPLACESEL, FALSE, (LPARAM)batch);
    }
    if (overflowed) {
        SendMessageA(g.log, EM_REPLACESEL, FALSE,
                     (LPARAM)"  [view] lines dropped from the window; the file has them\r\n");
    }
    SendMessageA(g.log, EM_SCROLLCARET, 0, 0);
}

/* ==============================================================================================
 * Building the window.
 * ============================================================================================ */

static HWND put(const char *class_name, const char *text, DWORD style, int x, int y, int w, int h,
                int id)
{
    HWND control = CreateWindowExA(0, class_name, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h,
                                   g.window, (HMENU)(INT_PTR)id, GetModuleHandleA(NULL), NULL);

    if (control != NULL && g.font != NULL) {
        SendMessageA(control, WM_SETFONT, (WPARAM)g.font, TRUE);
    }
    return control;
}

static void put_label(const char *text, int y)
{
    (void)put("STATIC", text, SS_LEFT, MARGIN, y + 4, LABEL_W, ROW_H - 6, -1);
}

static HWND put_edit(const char *text, int y, int id, DWORD extra)
{
    return put("EDIT", text, WS_BORDER | ES_AUTOHSCROLL | extra, MARGIN + LABEL_W, y, FIELD_W,
               ROW_H - 4, id);
}

static void put_check(const char *text, int x, int y, int w, int id, bool on)
{
    HWND box = put("BUTTON", text, BS_AUTOCHECKBOX, x, y, w, ROW_H - 6, id);

    if (box != NULL) {
        SendMessageA(box, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
    }
}

static void set_int(int id, int32_t value)
{
    char text[16];

    text_format(text, sizeof text, "%d", (int)value);
    SetDlgItemTextA(g.window, id, text);
}

static int32_t get_int(int id, int32_t fallback)
{
    char text[16];

    if (GetDlgItemTextA(g.window, id, text, sizeof text) == 0) {
        return fallback;
    }
    return (int32_t)atoi(text);
}

static bool checked(int id)
{
    return SendDlgItemMessageA(g.window, id, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

static void build(void)
{
    int y = MARGIN;
    size_t i;
    HWND   levels;

    put_label("Port", y);
    (void)put_edit("", y, ID_PORT, ES_NUMBER);
    y += ROW_H;

    put_label("Server name", y);
    (void)put_edit("", y, ID_NAME, 0);
    y += ROW_H;

    put_label("Password", y);
    (void)put_edit("", y, ID_PASSWORD, 0);
    y += ROW_H;

    put_label("Players", y);
    (void)put_edit("", y, ID_SLOTS, ES_NUMBER);
    y += ROW_H;

    put_label("Level", y);
    levels = put("COMBOBOX", "", CBS_DROPDOWN | WS_VSCROLL, MARGIN + LABEL_W, y, FIELD_W, 240,
                 ID_LEVEL);
    for (i = 0; i < server_config_level_count(); ++i) {
        SendMessageA(levels, CB_ADDSTRING, 0, (LPARAM)server_config_level_path(i));
    }
    y += ROW_H + 4;

    put_label("Points to win", y);
    (void)put_edit("", y, ID_SCORE_LIMIT, ES_NUMBER);
    y += ROW_H;

    put_label("Time limit (s)", y);
    (void)put_edit("", y, ID_TIME_LIMIT, ES_NUMBER);
    y += ROW_H;

    put_label("Respawn (1/10 s)", y);
    (void)put_edit("", y, ID_RESPAWN, ES_NUMBER);
    y += ROW_H + 4;

    put_check("Teams", MARGIN, y, 110, ID_TEAMS, true);
    put_check("Friendly fire", MARGIN + 120, y, 130, ID_FRIENDLY, false);
    y += ROW_H + 8;

    (void)put("STATIC", "Log", SS_LEFT, MARGIN, y, LEFT_W, ROW_H - 8, -1);
    y += ROW_H - 6;
    put_check("Session", MARGIN, y, 110, ID_CAT_SESSION, true);
    put_check("Match", MARGIN + 120, y, 130, ID_CAT_MATCH, true);
    y += ROW_H - 4;
    put_check("Pickups", MARGIN, y, 110, ID_CAT_PICKUP, true);
    put_check("Ping", MARGIN + 120, y, 130, ID_CAT_PING, false);
    y += ROW_H - 4;
    put_check("Positions", MARGIN, y, 200, ID_CAT_POS, false);
    y += ROW_H + 8;

    (void)put("BUTTON", "START", BS_DEFPUSHBUTTON, MARGIN, y, 80, 28, ID_START);
    (void)put("BUTTON", "STOP", 0, MARGIN + 90, y, 80, 28, ID_STOP);
    (void)put("BUTTON", "Save", 0, MARGIN + 180, y, 80, 28, ID_SAVE);
    y += 40;

    g.status = put("STATIC", "not running", SS_LEFT, MARGIN, y, LEFT_W, WINDOW_H - y - 60,
                   ID_STATUS);

    g.log = put("EDIT", "", WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
                MARGIN + LEFT_W + MARGIN, MARGIN, WINDOW_W - LEFT_W - 3 * MARGIN - 16,
                WINDOW_H - 2 * MARGIN - 40, ID_LOG);
}

/* ==============================================================================================
 * The settings, in both directions.
 * ============================================================================================ */

static void config_to_screen(void)
{
    set_int(ID_PORT, (int32_t)g.config.port);
    SetDlgItemTextA(g.window, ID_NAME, g.config.name);
    SetDlgItemTextA(g.window, ID_PASSWORD, g.config.password);
    set_int(ID_SLOTS, (int32_t)g.config.slots);
    SetDlgItemTextA(g.window, ID_LEVEL, g.config.setup.level);
    set_int(ID_SCORE_LIMIT, (int32_t)g.config.setup.rules.score_limit);
    set_int(ID_TIME_LIMIT, (int32_t)g.config.setup.rules.time_limit_s);
    set_int(ID_RESPAWN, (int32_t)g.config.setup.rules.respawn_tenths);

    SendDlgItemMessageA(g.window, ID_TEAMS, BM_SETCHECK,
                        mp_rules_teams(&g.config.setup.rules) ? BST_CHECKED : BST_UNCHECKED, 0);
    SendDlgItemMessageA(g.window, ID_FRIENDLY, BM_SETCHECK,
                        mp_rules_friendly_fire(&g.config.setup.rules) ? BST_CHECKED
                                                                      : BST_UNCHECKED, 0);
    SendDlgItemMessageA(g.window, ID_CAT_SESSION, BM_SETCHECK,
                        (g.config.log_categories & MP_SERVER_LOG_SESSION) ? BST_CHECKED
                                                                          : BST_UNCHECKED, 0);
    SendDlgItemMessageA(g.window, ID_CAT_MATCH, BM_SETCHECK,
                        (g.config.log_categories & MP_SERVER_LOG_MATCH) ? BST_CHECKED
                                                                        : BST_UNCHECKED, 0);
    SendDlgItemMessageA(g.window, ID_CAT_PICKUP, BM_SETCHECK,
                        (g.config.log_categories & MP_SERVER_LOG_PICKUP) ? BST_CHECKED
                                                                         : BST_UNCHECKED, 0);
    SendDlgItemMessageA(g.window, ID_CAT_PING, BM_SETCHECK,
                        (g.config.log_categories & MP_SERVER_LOG_PING) ? BST_CHECKED
                                                                       : BST_UNCHECKED, 0);
    SendDlgItemMessageA(g.window, ID_CAT_POS, BM_SETCHECK,
                        (g.config.log_categories & MP_SERVER_LOG_POSITION) ? BST_CHECKED
                                                                           : BST_UNCHECKED, 0);
}

/* Reads the categories alone, which is the one setting that may change WHILE the server runs:
 * it belongs to the log rather than to the session, and turning positions off mid-match is the
 * whole reason a person has that box. */
static uint32_t categories_from_screen(void)
{
    uint32_t categories = 0;

    if (checked(ID_CAT_SESSION)) { categories |= (uint32_t)MP_SERVER_LOG_SESSION; }
    if (checked(ID_CAT_MATCH))   { categories |= (uint32_t)MP_SERVER_LOG_MATCH; }
    if (checked(ID_CAT_PICKUP))  { categories |= (uint32_t)MP_SERVER_LOG_PICKUP; }
    if (checked(ID_CAT_PING))    { categories |= (uint32_t)MP_SERVER_LOG_PING; }
    if (checked(ID_CAT_POS))     { categories |= (uint32_t)MP_SERVER_LOG_POSITION; }
    return categories;
}

static void screen_to_config(void)
{
    char text[SERVER_CONFIG_LOG_PATH_MAX];

    g.config.port  = (uint16_t)get_int(ID_PORT, (int32_t)g.config.port);
    g.config.slots = (uint8_t)get_int(ID_SLOTS, (int32_t)g.config.slots);

    if (GetDlgItemTextA(g.window, ID_NAME, text, (int)sizeof g.config.name) >= 0) {
        memset(g.config.name, 0, sizeof g.config.name);
        strncpy(g.config.name, text, sizeof g.config.name - 1u);
    }
    if (GetDlgItemTextA(g.window, ID_PASSWORD, text, (int)sizeof g.config.password) >= 0) {
        memset(g.config.password, 0, sizeof g.config.password);
        strncpy(g.config.password, text, sizeof g.config.password - 1u);
    }
    if (GetDlgItemTextA(g.window, ID_LEVEL, text, (int)sizeof g.config.setup.level) >= 0) {
        memset(g.config.setup.level, 0, sizeof g.config.setup.level);
        strncpy(g.config.setup.level, text, sizeof g.config.setup.level - 1u);
    }
    g.config.setup.rules.score_limit =
        (uint16_t)get_int(ID_SCORE_LIMIT, (int32_t)g.config.setup.rules.score_limit);
    g.config.setup.rules.time_limit_s =
        (uint16_t)get_int(ID_TIME_LIMIT, (int32_t)g.config.setup.rules.time_limit_s);
    g.config.setup.rules.respawn_tenths =
        (uint8_t)get_int(ID_RESPAWN, (int32_t)g.config.setup.rules.respawn_tenths);

    g.config.setup.rules.flags = 0u;
    if (checked(ID_TEAMS))    { g.config.setup.rules.flags |= (uint8_t)MP_RULES_F_TEAMS; }
    if (checked(ID_FRIENDLY)) { g.config.setup.rules.flags |= (uint8_t)MP_RULES_F_FRIENDLY_FIRE; }

    g.config.log_categories = categories_from_screen();
    (void)server_config_clamp(&g.config);
}

/* ==============================================================================================
 * What the screen says while it runs.
 * ============================================================================================ */

static const char *outcome_text(uint8_t outcome, uint8_t winner)
{
    static char text[64];

    switch (outcome) {
    case MP_SCORE_WON_PLAYER:
        text_format(text, sizeof text, "slot %u has won", (unsigned)winner);
        break;
    case MP_SCORE_WON_TEAM:
        text_format(text, sizeof text, "team %u has won", (unsigned)winner);
        break;
    case MP_SCORE_DRAW:
        text_format(text, sizeof text, "drawn");
        break;
    default:
        text_format(text, sizeof text, "running");
        break;
    }
    return text;
}

static void refresh_status(void)
{
    server_run_status_t         status;
    mp_server_log_counters_t    counters;
    char                        text[640];

    server_run_status(&status);
    mp_server_log_counters(&counters);

    if (!status.running) {
        SetWindowTextA(g.status, "not running");
        return;
    }
    text_format(text, sizeof text,
                "listening on %u\r\n\r\n"
                "players      %u\r\n"
                "joins        %u\r\n"
                "refused      %u\r\n"
                "dropped      %u\r\n\r\n"
                "round        %s\r\n"
                "generation   %u\r\n"
                "rounds       %u\r\n"
                "elapsed      %u s\r\n"
                "remaining    %u s\r\n\r\n"
                "worlds       %u\r\n"
                "relayed      %u (%u refused, %u client setup(s) and %u other note(s) "
                "dropped)\r\n"
                "setups       %u\r\n"
                "boards       %u\r\n"
                "deaths       %u\r\n"
                "npc copies   %u wish(es), not built here\r\n\r\n"
                "log written  %u\r\n"
                "log dropped  %u\r\n"
                "log filtered %u",
                (unsigned)status.port, (unsigned)status.players, (unsigned)status.joins,
                (unsigned)status.denied, (unsigned)status.drops,
                outcome_text(status.outcome, status.winner), (unsigned)status.generation,
                (unsigned)status.rounds_begun, (unsigned)status.round_elapsed_s,
                (unsigned)status.round_remaining_s, (unsigned)status.worlds_sent,
                (unsigned)status.relayed, (unsigned)status.relay_refused,
                (unsigned)status.setups_refused, (unsigned)status.notes_refused,
                (unsigned)status.setups_sent, (unsigned)status.boards_sent,
                (unsigned)status.deaths_seen, (unsigned)status.copy_wishes,
                (unsigned)counters.written,
                (unsigned)counters.dropped, (unsigned)counters.filtered);
    SetWindowTextA(g.status, text);
}

/* The settings that may only change between runs are greyed while one is on, so that a field
 * which cannot take effect cannot be typed into either. */
static void enable_settings(bool enabled)
{
    static const int FIELDS[] = { ID_PORT, ID_NAME, ID_PASSWORD, ID_SLOTS, ID_LEVEL,
                                  ID_SCORE_LIMIT, ID_TIME_LIMIT, ID_RESPAWN, ID_TEAMS,
                                  ID_FRIENDLY };
    size_t i;

    for (i = 0; i < sizeof FIELDS / sizeof FIELDS[0]; ++i) {
        EnableWindow(GetDlgItem(g.window, FIELDS[i]), enabled ? TRUE : FALSE);
    }
    EnableWindow(GetDlgItem(g.window, ID_START), enabled ? TRUE : FALSE);
    EnableWindow(GetDlgItem(g.window, ID_STOP), enabled ? FALSE : TRUE);
}

/* ==============================================================================================
 * The window's own messages.
 * ============================================================================================ */

static void on_start(void)
{
    screen_to_config();
    config_to_screen();   /* whatever the clamp moved is shown, rather than silently taken */
    mp_server_log_set_categories(g.config.log_categories);
    if (!server_run_start(&g.config)) {
        MessageBoxA(g.window, "The server did not start. The log says why; the usual reason is a "
                              "port another program already has.", "Obi dedicated server",
                    MB_OK | MB_ICONWARNING);
        return;
    }
    enable_settings(false);
}

static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message) {
    case WM_COMMAND:
        switch (LOWORD(wparam)) {
        case ID_START:
            on_start();
            return 0;
        case ID_STOP:
            server_run_stop();
            enable_settings(true);
            return 0;
        case ID_SAVE:
            screen_to_config();
            config_to_screen();
            if (!server_config_save(&g.config)) {
                MessageBoxA(window, "The settings could not be written.",
                            "Obi dedicated server", MB_OK | MB_ICONWARNING);
            }
            return 0;
        case ID_CAT_SESSION:
        case ID_CAT_MATCH:
        case ID_CAT_PICKUP:
        case ID_CAT_PING:
        case ID_CAT_POS:
            /* These take effect at once, running or not: they belong to the log rather than to
             * the session, and turning positions off in the middle of a match is the reason
             * somebody reaches for that box. */
            g.config.log_categories = categories_from_screen();
            mp_server_log_set_categories(g.config.log_categories);
            return 0;
        default:
            break;
        }
        break;
    case WM_TIMER:
        flush_pending();
        refresh_status();
        return 0;
    case WM_CLOSE:
        server_run_stop();
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcA(window, message, wparam, lparam);
}

int server_gui_run(void)
{
    WNDCLASSA wc;
    MSG       message;

    memset(&g, 0, sizeof g);
    InitializeCriticalSection(&g.pending_lock);
    server_config_load(&g.config);

    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc   = &window_proc;
    wc.hInstance     = GetModuleHandleA(NULL);
    wc.hCursor       = LoadCursorA(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = "ObiDedicatedServer";
    if (RegisterClassA(&wc) == 0) {
        return 1;
    }
    g.window = CreateWindowExA(0, wc.lpszClassName, "Obi dedicated server",
                               WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                               CW_USEDEFAULT, CW_USEDEFAULT, WINDOW_W, WINDOW_H, NULL, NULL,
                               wc.hInstance, NULL);
    if (g.window == NULL) {
        return 1;
    }
    g.font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    build();
    config_to_screen();
    enable_settings(true);

    /* The log is opened before the server, so the first line the server writes has somewhere to
     * go. The listener is this window; the file is whatever the settings named. */
    if (!mp_server_log_open(g.config.log_path, g.config.log_categories, g.config.log_ring)) {
        MessageBoxA(g.window, "The log file could not be opened, so nothing will be written to "
                              "disk. The window still shows everything.",
                    "Obi dedicated server", MB_OK | MB_ICONWARNING);
    }
    mp_server_log_set_line_listener(&take_line, NULL);

    ShowWindow(g.window, SW_SHOW);
    SetTimer(g.window, TIMER_ID, TIMER_MS, NULL);

    while (GetMessageA(&message, NULL, 0, 0) > 0) {
        if (!IsDialogMessageA(g.window, &message)) {
            TranslateMessage(&message);
            DispatchMessageA(&message);
        }
    }

    server_run_stop();
    mp_server_log_close();
    DeleteCriticalSection(&g.pending_lock);
    return 0;
}
