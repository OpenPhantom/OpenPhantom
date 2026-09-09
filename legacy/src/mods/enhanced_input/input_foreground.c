#include "input_foreground.h"

#include <windows.h>

/* How stale the cached answer may be. Eight milliseconds is under half a frame at 60 fps, so the
 * foreground change is acted on within the same frame a player could notice it in, while a 1000 Hz
 * mouse costs at most eight window calls a second rather than two thousand. */
#define FOREGROUND_POLL_MS 8u

static DWORD foreground_last_poll_ms;
static bool  foreground_ours;
static bool  foreground_polled;

static bool poll_foreground(void)
{
    HWND  front = GetForegroundWindow();
    DWORD owner = 0;

    /* No foreground window at all is somebody else's business, not ours: it happens while the
     * shell is switching and during a secure desktop, and answering true there would let the view
     * turn during exactly the moments the player cannot see it. */
    if (front == NULL) {
        return false;
    }
    GetWindowThreadProcessId(front, &owner);
    return owner == GetCurrentProcessId();
}

bool input_foreground_is_ours(void)
{
    DWORD now = GetTickCount();

    /* Unsigned subtraction, so the 49.7 day wrap of GetTickCount needs no special case: the
     * difference is still the elapsed interval across it. */
    if (!foreground_polled || (now - foreground_last_poll_ms) >= FOREGROUND_POLL_MS) {
        foreground_last_poll_ms = now;
        foreground_ours         = poll_foreground();
        foreground_polled       = true;
    }
    return foreground_ours;
}
