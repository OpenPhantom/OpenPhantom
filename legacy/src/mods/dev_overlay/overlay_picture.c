/* overlay_picture.c: see overlay_picture.h. */
#include "overlay_picture.h"

#include "auto_range_row.h"
#include "fov_row.h"
#include "overlay_host_value.h"
#include "overlay_kit.h"
#include "overlay_reason.h"
#include "strict_range_row.h"
#include "subtitle_size_row.h"
#include "view_range_live_row.h"
#include "view_range_row.h"

#include "common/text.h"

/* The slot enum and its assert are in overlay_picture.h, because session_lock.c addresses four
 * of these slots and was doing it with bare numbers. The table below is the order those names
 * stand for, and a NUMBER entry in it is TWO slots. */

/* The two numbers that cannot fail to be read, handed to the table in the shape the field of view
 * already has: the one that can. */
static bool draw_distance_now(float *out)
{
    *out = view_range_row_get();
    return true;
}

static bool subtitle_size_now(float *out)
{
    *out = subtitle_size_row_get();
    return true;
}

/* The field of view's ends come out of the settings file rather than out of the table. */
static void fov_ends(float *low, float *high)
{
    *low = fov_row_min();
    *high = fov_row_max();
}

static void fov_label(char *out, size_t size)
{
    text_format(out, size, "Field of view (%.0f to %.0f)",
                (double)fov_row_min(), (double)fov_row_max());
}

/* What Default puts back on each of the three numbers here, and each is a different claim.
 *
 * The draw distance has no shipped default: view_range_row_get() falls back to VIEW_RANGE_MIN
 * when the key is absent or unreadable, so 1.0 is what an untouched installation reads and what
 * Default has to return it to. It is that fallback and not a value anybody chose, and it is also
 * the low end of the track, which is a coincidence of this one row.
 *
 * The subtitle size has a real one, SUBTITLE_SIZE_DEFAULT: the size the text has at 640x480,
 * which is the point of that row. It sits in the middle of the band.
 *
 * The field of view has neither. What it shows is a base plus an offset, the offset is what this
 * row writes, and the value with nothing written is the base itself. So Default here is "no
 * offset", which is whatever base variable_fov published for this canvas, and the row has no
 * Default at all while it has published none. FOV_ROW_MIN_DEFAULT is the track's low end. */
static bool draw_distance_standard(float *out)
{
    *out = VIEW_RANGE_MIN;
    return true;
}

static bool subtitle_size_standard(float *out)
{
    *out = SUBTITLE_SIZE_DEFAULT;
    return true;
}

static bool fov_standard(float *out)
{
    return fov_row_base(out);
}

/* What the game is running, which is not always the number above it: the frame governor lowers
 * that when a scene costs too much, and the cell watchdog lowers it when the draw table or the
 * vertex cache is near overflowing. On Coruscant the watchdog can pin it at 1.00 for a whole
 * level, and the row above then shows a number nothing is using. */
static void live_range_label(char *out, size_t size)
{
    char text[16];

    if (view_range_live_row_get(text, sizeof text)) {
        text_format(out, size, "  in force: %s", text);
    } else {
        text_format(out, size, "  in force: not reported");
    }
}

/* The governor is greyed while strict mode is on, because the two contradict each other and strict
 * wins: it declines the governor outright, so a switch still reading ON would be describing
 * something that is not happening.
 *
 * Its key is deliberately NOT written when that happens. A reader who had the governor on, turns
 * strict on to look at something and turns it off again gets the governor back, rather than
 * finding a setting they never changed has been changed for them. So the row reports the state the
 * game is actually in, and the file keeps the state the reader asked for. */
static bool auto_range_offered(void)
{
    return !strict_range_row_get();
}

static uint32_t auto_range_reason(void)
{
    return (uint32_t)OVERLAY_REASON_NEEDS_ROW;   /* the strict row below it */
}

static bool auto_range_is_on(void)
{
    return auto_range_offered() && auto_range_row_get();
}

/* On a client of a running session the host's draw distance is the target here, and the row the
 * session has taken says so in its chip. The two switches under it have no host value: whether the
 * draw distance follows the frame rate, and whether it is kept at a cost, are this machine's own,
 * so they read `session` as before. */
static bool draw_distance_host_word(char *out, size_t size, bool *on)
{
    float value;

    (void)on;   /* a number has no switch to report */
    return overlay_host_value(HOST_SETTING_VIEW_RANGE_SCALE, &value) &&
           overlay_host_number_word(value, view_range_row_format, out, size);
}

static const overlay_kit_entry_t ROWS[] = {
    /* The accepted range is in the label rather than left for a player to discover by having a
     * number refused. Both ends are compile-time constants here, unlike the field of view, so
     * there is no divide by zero to guard and no way for a player to set them equal.
     *
     * The step is a hundredth, the precision the formatter shows; without it a drag writes more
     * decimals than the text beside it displays and the two disagree about what was set. A
     * fiftieth was tried and is wrong, because the grid has to contain both ends of every row that
     * uses it: the fog thickness, which uses the same one from its own group, starts at 0.25, so
     * dragging fully left rounded up to 0.26 and the documented minimum could not be reached at
     * all. Caught in a log, not in a test. */
    { .type       = OVERLAY_KIT_NUMBER,
      .label      = "Draw distance (1.0 to 2.5)",
      .number     = draw_distance_now,
      .set_number = view_range_row_set,
      .format     = view_range_row_format,
      .parse      = view_range_row_parse,
      .minimum    = VIEW_RANGE_MIN,
      .maximum    = VIEW_RANGE_MAX,
      .step       = 0.01f,
      .coarse     = 0.10f,
      .standard   = draw_distance_standard,
      .host_word  = draw_distance_host_word },

    /* A note rather than a control, so it cannot be clicked into and cannot be mistaken for
     * something to set. */
    { .type       = OVERLAY_KIT_NOTE,
      .label_now  = live_range_label },

    { .type       = OVERLAY_KIT_TOGGLE,
      .label      = "Draw distance follows the frame rate",
      .get_on     = auto_range_is_on,
      .set_on     = auto_range_row_set,
      .offered    = auto_range_offered,
      .reason     = auto_range_reason },

    /* Named for the trade rather than for the machinery, like the row above it. The frame rate is
     * the cost a reader will actually meet, because the governor is the term that acts in ordinary
     * play; the watchdog only acts above 1.00x, and what it costs when declined is in the ini and
     * in strict_range_row.h rather than in 47 characters. */
    { .type       = OVERLAY_KIT_TOGGLE,
      .label      = "Keep the draw distance (costs frame rate)",
      .get_on     = strict_range_row_get,
      .set_on     = strict_range_row_set },

    /* The one row here that can be unavailable. Every other row edits a settings file and works
     * with the DLL that reads it gone; this one needs a width in degrees that only variable_fov
     * can publish, and inventing one would be wrong on some canvas.
     *
     * Whole degrees, because the row shows whole degrees: a drag that set 96.4 would display 96
     * and write 96.4 back into the file, and the two would disagree for anyone reading it. A
     * degree is also below what the eye picks out mid-drag. The full rate is for this track alone:
     * its whole effect is the picture zooming under the hand, and at a few writes a second that
     * zoom is a series of steps. */
    { .type       = OVERLAY_KIT_NUMBER,
      .label_now  = fov_label,
      .number     = fov_row_get,
      .set_number = fov_row_set,
      .format     = fov_row_format,
      .parse      = fov_row_parse,
      .ends       = fov_ends,
      .minimum    = FOV_ROW_MIN_DEFAULT,
      .maximum    = FOV_ROW_MAX_DEFAULT,
      .step       = 1.0f,
      .coarse     = 5.0f,
      .standard   = fov_standard,
      .full_rate  = true },

    /* Named for what it changes rather than for the key it writes, with the band in the label so
     * it need not be found by having a value refused. No availability test: both ends are fixed,
     * so unlike the field of view nothing has to be published by another DLL first. With
     * enhanced_resolution absent the drag writes a key nothing reads, which is how every
     * cross-DLL row here already behaves. */
    { .type       = OVERLAY_KIT_NUMBER,
      .label      = "Subtitle size (0.50 to 3.0)",
      .number     = subtitle_size_now,
      .set_number = subtitle_size_row_set,
      .format     = subtitle_size_row_format,
      .parse      = subtitle_size_row_parse,
      .minimum    = SUBTITLE_SIZE_MIN,
      .maximum    = SUBTITLE_SIZE_MAX,
      .step       = 0.01f,
      .coarse     = 0.10f,
      .standard   = subtitle_size_standard }
};

#define ROW_ENTRIES ((uint32_t)(sizeof ROWS / sizeof ROWS[0]))

uint32_t overlay_picture_row_count(void)
{
    return overlay_kit_count(ROWS, ROW_ENTRIES);
}

void overlay_picture_row(uint32_t slot, const char *editing_text, overlay_row_t *out)
{
    overlay_kit_fill(ROWS, ROW_ENTRIES, slot, editing_text, out);
}

bool overlay_picture_toggle(uint32_t slot)
{
    return overlay_kit_activate(ROWS, ROW_ENTRIES, slot);
}

bool overlay_picture_commit(uint32_t slot, const char *text)
{
    return overlay_kit_commit(ROWS, ROW_ENTRIES, slot, text);
}

bool overlay_picture_slider_wants_full_rate(uint32_t slot)
{
    return overlay_kit_full_rate(ROWS, ROW_ENTRIES, slot);
}

bool overlay_picture_slider_set(uint32_t slot, float fraction)
{
    return overlay_kit_slider(ROWS, ROW_ENTRIES, slot, fraction);
}

bool overlay_picture_slider_value(uint32_t slot, float fraction, char *out, size_t size)
{
    return overlay_kit_value_at(ROWS, ROW_ENTRIES, slot, fraction, out, size);
}

/* The numbers behind that track, straight off the table entry; see overlay_kit.h. */
bool overlay_picture_slider_limits(uint32_t slot, overlay_number_t *out)
{
    return overlay_kit_limits(ROWS, ROW_ENTRIES, slot, out);
}
