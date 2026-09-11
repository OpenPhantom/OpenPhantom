/* overlay_framerate.c: see overlay_framerate.h. */
#include "overlay_framerate.h"

#include "common/ini.h"

#include <windows.h>

#include <stdio.h>
#include <stdlib.h>

#define FRAMERATE_SECTION "framerate_fix"

/* The largest limit the setting accepts, and the same bound framerate_fix clamps to. */
#define LIMIT_MAXIMUM 1000

enum {
    FRAMERATE_ROW_MATCH = 0,
    FRAMERATE_ROW_DIVISOR,
    FRAMERATE_ROW_LIMIT,
    FRAMERATE_ROW_NOTE,
    FRAMERATE_ROW_NOTE_MORE,
    FRAMERATE_ROW_NOTE_LAST
};

/* VREFRESH, from wingdi.h, and the only thing wanted out of it. A driver answering 0 or 1 is
 * reporting a hardware default rather than a rate, so neither is shown as one. */
#define DEVICE_CAP_VREFRESH 116

static int display_refresh_hz(void)
{
    HDC dc;
    int hz;

    dc = GetDC(NULL);
    if (dc == NULL) {
        return 0;
    }
    hz = GetDeviceCaps(dc, DEVICE_CAP_VREFRESH);
    (void)ReleaseDC(NULL, dc);
    return (hz >= 20 && hz <= LIMIT_MAXIMUM) ? hz : 0;
}

static bool match_enabled(void)
{
    return ini_read_bool(FRAMERATE_SECTION, "MatchDisplayRefresh", true);
}

static int configured_limit(void)
{
    return ini_read_int(FRAMERATE_SECTION, "TargetFps", 0);
}

/* The same bounds framerate_fix keeps: four fractions at most, and none under 30 a second. */
#define DIVISOR_MAXIMUM 4
#define DIVISOR_FLOOR_FPS 30

static int configured_divisor(void)
{
    int divisor = ini_read_int(FRAMERATE_SECTION, "RefreshDivisor", 0);

    return (divisor < 0) ? 0 : (divisor > DIVISOR_MAXIMUM) ? DIVISOR_MAXIMUM : divisor;
}

/* The fraction row's chip: "auto", or the pinned fraction with the rate it makes on this screen.
 * A fraction the screen cannot go down to (a quarter of 60 is 15) is named as refused, the same
 * way framerate_fix refuses it, so the chip never promises a rate that will not be applied. */
static void divisor_text(char *out, size_t size)
{
    int divisor = configured_divisor();
    int refresh = display_refresh_hz();

    if (divisor == 0) {
        (void)_snprintf(out, size - 1u, "auto");
    } else if (refresh > 0 && refresh / divisor < DIVISOR_FLOOR_FPS) {
        (void)_snprintf(out, size - 1u, "1/%d too low", divisor);
    } else if (refresh > 0) {
        (void)_snprintf(out, size - 1u, "1/%d = %d", divisor, (refresh + divisor / 2) / divisor);
    } else {
        (void)_snprintf(out, size - 1u, "1/%d", divisor);
    }
    out[size - 1u] = '\0';
}

/* The limit as the row shows it. Private: nothing outside this file has a reason to format it. */
static void value_text(uint32_t slot, char *out, size_t size)
{
    int limit;

    if (out == NULL || size == 0u) {
        return;
    }
    out[0] = '\0';
    if (slot != (uint32_t)FRAMERATE_ROW_LIMIT) {
        return;
    }

    limit = configured_limit();
    if (match_enabled()) {
        /* The setting still holds this number and framerate_fix still keeps it as the fallback,
         * but nothing is using it while the row above is on, and showing it invites somebody to
         * change it and watch nothing happen. */
        (void)_snprintf(out, size - 1u, "n/a");
    } else if (limit <= 0) {
        (void)_snprintf(out, size - 1u, "none");
    } else {
        (void)_snprintf(out, size - 1u, "%d", limit);
    }
    out[size - 1u] = '\0';
}

void overlay_framerate_row(uint32_t slot, const char *editing_text, overlay_row_t *out)
{
    int refresh;

    if (out == NULL) {
        return;
    }
    /* The caller owns group and id; everything below is this group's own. Only the match row is a
     * toggle, so the rest are left off rather than each saying so. */
    out->available = true;
    out->on        = false;
    out->label[0]  = '\0';
    out->value[0]  = '\0';

    switch (slot) {
    case FRAMERATE_ROW_MATCH:
        out->kind = OVERLAY_ROW_CHEAT;
        out->on   = match_enabled();
        refresh   = display_refresh_hz();
        if (refresh > 0) {
            (void)_snprintf(out->label, sizeof out->label - 1u,
                            "Match the screen, %d a second (recommended)", refresh);
        } else {
            /* Named rather than hidden: the row still writes the setting, and framerate_fix falls
             * back to the typed limit and says so in the log. */
            (void)_snprintf(out->label, sizeof out->label - 1u,
                            "Match the screen (it will not report a rate)");
        }
        break;

    case FRAMERATE_ROW_DIVISOR:
        /* An action with a chip rather than a typed value, because the only sensible answers are
         * five and typing one of them is slower than pressing until it comes round. Greyed with
         * the limit row while the screen is not being matched: a fraction of a rate nobody is
         * following is not a setting. */
        out->kind      = OVERLAY_ROW_ACTION;
        out->available = match_enabled();
        (void)_snprintf(out->label, sizeof out->label - 1u,
                        "  Fraction of the screen's rate");
        divisor_text(out->value, sizeof out->value);
        break;

    case FRAMERATE_ROW_LIMIT:
        out->kind = OVERLAY_ROW_VALUE;
        /* Greyed rather than hidden while the screen decides the rate. A row that disappears
         * takes the answer to "where did I set that" with it; a greyed one says the setting is
         * still here and something above it is in charge. Unavailable also means the panel
         * refuses to start an edit on it, so there is no way to type into a number nothing
         * reads. */
        out->available = !match_enabled();
        (void)_snprintf(out->label, sizeof out->label - 1u, "Frame rate limit");
        if (editing_text != NULL) {
            /* The trailing underscore is the caret every other typed row in the panel shows. */
            (void)_snprintf(out->value, sizeof out->value - 1u, "%s_", editing_text);
        } else {
            value_text(slot, out->value, sizeof out->value);
        }
        break;

    /* Three rows for one sentence, because the panel is only about forty-five characters wide
     * and a longer label is cut off rather than wrapped. The continuations are indented past the
     * line they finish, the same shape the free camera's how-to-fly lines use.
     *
     * It names what goes wrong rather than only that something does. Somebody reading this row
     * has come here because the game looks bad while the numbers look fine, and the last line is
     * the half that tells them they are in the right place. */
    case FRAMERATE_ROW_NOTE:
        out->kind = OVERLAY_ROW_INFO;
        (void)_snprintf(out->label, sizeof out->label - 1u,
                        "Only the screen's Hz or an even fraction of");
        break;

    case FRAMERATE_ROW_NOTE_MORE:
        out->kind = OVERLAY_ROW_INFO;
        (void)_snprintf(out->label, sizeof out->label - 1u,
                        "  it is smooth; any other limit makes movement");
        break;

    case FRAMERATE_ROW_NOTE_LAST:
    default:
        out->kind = OVERLAY_ROW_INFO;
        (void)_snprintf(out->label, sizeof out->label - 1u,
                        "  choppy. Auto steps down when frames run long");
        break;
    }

    out->label[sizeof out->label - 1u] = '\0';
    out->value[sizeof out->value - 1u] = '\0';
}

bool overlay_framerate_toggle(uint32_t slot)
{
    if (slot == (uint32_t)FRAMERATE_ROW_DIVISOR) {
        if (!match_enabled()) {
            return false;
        }
        /* auto, 1, 2, 3, 4, auto: the whole ring is five presses, and it starts where it is. */
        return ini_write_int(FRAMERATE_SECTION, "RefreshDivisor",
                             (configured_divisor() + 1) % (DIVISOR_MAXIMUM + 1));
    }
    if (slot != (uint32_t)FRAMERATE_ROW_MATCH) {
        return false;
    }
    return ini_write_int(FRAMERATE_SECTION, "MatchDisplayRefresh", match_enabled() ? 0 : 1);
}

bool overlay_framerate_accept_value(uint32_t slot, const char *text)
{
    long value;
    char *end = NULL;

    if (slot != (uint32_t)FRAMERATE_ROW_LIMIT || text == NULL || match_enabled()) {
        return false;
    }

    /* Hand parsed rather than strtof, and refused rather than clamped: somebody typing 1440 for a
     * 144 Hz screen has made a mistake, and silently giving them 1000 hides it. */
    value = strtol(text, &end, 10);
    if (end == text || value < 0 || value > LIMIT_MAXIMUM) {
        return false;
    }
    while (end != NULL && *end != '\0') {
        if (*end != ' ' && *end != '\t') {
            return false;
        }
        ++end;
    }
    return ini_write_int(FRAMERATE_SECTION, "TargetFps", (int)value);
}
