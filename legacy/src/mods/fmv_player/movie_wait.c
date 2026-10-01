/* movie_wait.c: the black picture a held client waits behind. See the header. */
#include "movie_wait.h"

#include "vlc_playback.h"

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>

/* The line's height against the picture's, and the smallest it may get, so it reads the same on a
 * 640x480 window and on a 4K monitor. */
#define WAIT_TEXT_HEIGHT_DIVISOR 24
#define WAIT_TEXT_MIN_HEIGHT     16

/* A light grey rather than white, which glows on a picture that is otherwise black. */
#define WAIT_TEXT_COLOUR RGB(200, 200, 200)

/* Module state because one wait is on screen at a time, on the game's thread. */
static const char *waiting_text;

const char *movie_wait_text(void)
{
    return waiting_text;
}

void movie_wait_paint(HWND window, HDC dc, const char *text)
{
    RECT  client;
    HFONT font;
    HFONT previous = NULL;
    int   height;

    if (dc == NULL || !GetClientRect(window, &client)) {
        return;
    }
    FillRect(dc, &client, (HBRUSH)GetStockObject(BLACK_BRUSH));
    if (text == NULL) {
        return;
    }
    height = (client.bottom - client.top) / WAIT_TEXT_HEIGHT_DIVISOR;
    if (height < WAIT_TEXT_MIN_HEIGHT) {
        height = WAIT_TEXT_MIN_HEIGHT;
    }
    font = CreateFontA(-height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                       DEFAULT_PITCH | FF_SWISS, "Arial");
    if (font != NULL) {
        previous = (HFONT)SelectObject(dc, font);
    }
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, WAIT_TEXT_COLOUR);
    DrawTextA(dc, text, -1, &client, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    if (font != NULL) {
        SelectObject(dc, previous);
        DeleteObject(font);
    }
}

/* The overlay is this DLL's own window, so it is repainted through its window procedure and keeps
 * the line when something covers it and goes away. The game's own window is not, and gets the
 * line once, as the last resort surface setting gives libVLC that window and nothing else. */
static void show_the_line(HWND overlay_window, HWND render_window, const char *text)
{
    waiting_text = text;
    if (overlay_window != NULL) {
        InvalidateRect(overlay_window, NULL, TRUE);
        UpdateWindow(overlay_window);
        return;
    }
    if (render_window != NULL) {
        HDC dc = GetDC(render_window);

        if (dc != NULL) {
            movie_wait_paint(render_window, dc, text);
            ReleaseDC(render_window, dc);
        }
    }
}

void movie_wait_hold(HWND overlay_window, HWND render_window, HWND game_window,
                     movie_loop_t *loop)
{
    movie_verdict_t first;

    if (loop == NULL || loop->poll == NULL || render_window == NULL) {
        return;
    }
    vlc_playback_pump_timers(render_window, loop);   /* pumped before it is asked */
    first = loop->poll();
    if (first == MOVIE_VERDICT_HOST_DONE) {
        loop->end = MOVIE_END_HOST;   /* the two movies ended together, near enough */
        return;
    }
    if (first != MOVIE_VERDICT_GO_ON) {
        return;   /* the session let the player go as the movie ended: it simply ended */
    }
    show_the_line(overlay_window, render_window, loop->wait_text);
    if (loop->waits != NULL) {
        loop->waits();
    }
    vlc_playback_hold_blocking(render_window, game_window, loop);
    waiting_text = NULL;
}
