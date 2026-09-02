/* dev_menu_size_row.c: see dev_menu_size_row.h.
 *
 * The clamp, the parser and the formatter are the same three functions view_range_row.c has, for
 * the same reasons, and the parser in particular is hand written rather than handed to strtof for
 * the reason recorded there: it runs on text somebody is still typing, and strtof asks the locale
 * whether a full stop is a decimal point.
 */
#include "dev_menu_size_row.h"

#include "overlay_draw.h"


#include "common/ini.h"

#include <stdio.h>

/* THE CEILING IS THE SCREEN, NOT A CONSTANT, and that is a repair rather than a refinement.
 *
 * DevMenuSize ends up as a fixed number of PIXELS: overlay_draw.c passes
 * glyph_scale(scale * 640/screen_w, scale * 480/screen_h), and the engine's own renderer multiplies
 * text by (screen_w/640, screen_h/480), so the two cancel and the panel is the same physical size
 * at 1080 as at 4K, times this scale. Four was chosen as a ceiling for a high density laptop where
 * a fixed pixel panel comes out tiny. On a 1280x800 Steam Deck the same four makes the panel larger
 * than the screen, and the row that would set it back is off the edge, so the setting cannot be
 * undone from inside the game. Field confirmed.
 *
 * So the ceiling scales with the display: the panel may grow until it takes the same share of the
 * screen that an authored panel takes of the 480 lines it was drawn for. 800 lines allows 1.66,
 * 1080 allows 2.25, 1440 allows 3.0, and anything from 1920 lines up keeps the full 4.0. Never
 * below 1.0, because the authored size has to remain reachable on any screen the game will run at.
 *
 * A screen the drawing layer cannot answer for yet, which is every call before the display mode is
 * set, keeps the static ceiling; the next call once a mode exists brings it back down. */
static float screen_ceiling(void)
{
    float width = 0.0f;
    float height = 0.0f;
    float ceiling;

    if (!overlay_draw_screen(&width, &height) || !(height > 0.0f)) {
        return DEV_MENU_SIZE_MAX;
    }

    ceiling = height / OVERLAY_AUTHORED_HEIGHT;
    if (ceiling < 1.0f) {
        ceiling = 1.0f;
    }
    if (ceiling > DEV_MENU_SIZE_MAX) {
        ceiling = DEV_MENU_SIZE_MAX;
    }
    return ceiling;
}

/* Zero is "decide for me". It is the same spelling MenuScale and FogScale already use for the same
 * idea, and it is the shipped default: the panel is a fixed number of pixels, so a number that
 * suits a 4K monitor is unreadable on a handheld and one that suits the handheld swamps the
 * monitor. Only the resolution is knowable from in here, so that is what it follows. */
static bool is_automatic(float scale)
{
    return !(scale > 0.005f);
}

/* What automatic actually picks, and it is NOT the ceiling above.
 *
 * The first version of this used screen_ceiling() and it was far too big: that number is "the
 * largest that still fits", which at 4K is four times the authored size and fills the screen. What
 * automatic wants is "the size this already reads at", held steady as the resolution changes.
 *
 * The panel is a fixed number of pixels, so on one physical monitor doubling the resolution halves
 * how big it looks. 1080 is the reference because the authored size is comfortable there on an
 * ordinary desktop monitor, which is the case DevMenuSize was originally added to rescue people
 * from. So 1440 gets 1.33, 2160 gets 2.0, and nothing below 1080 shrinks: a smaller screen is more
 * often a smaller PANEL physically, as on a handheld, and making it smaller still would be the
 * wrong direction.
 *
 * Resolution cannot tell 4K on a 27 inch monitor from 4K on a 13 inch laptop, so this is a sensible
 * middle and not a right answer. That is what the explicit setting is still for. */
#define DEV_MENU_SIZE_REFERENCE_HEIGHT 1080.0f

static float automatic_scale(void)
{
    float width = 0.0f;
    float height = 0.0f;
    float scale;
    float ceiling;

    if (!overlay_draw_screen(&width, &height) || !(height > 0.0f)) {
        return 1.0f;
    }

    scale = height / DEV_MENU_SIZE_REFERENCE_HEIGHT;
    if (scale < 1.0f) {
        scale = 1.0f;
    }

    ceiling = screen_ceiling();
    if (scale > ceiling) {
        scale = ceiling;      /* it must still fit, which is the ceiling's whole job */
    }
    return scale;
}

float dev_menu_size_row_clamp(float scale)
{
    const float ceiling = screen_ceiling();

    if (is_automatic(scale)) {
        return DEV_MENU_SIZE_AUTOMATIC;      /* kept as the request, resolved when it is drawn */
    }

    /* Written as a positive test so a NaN falls through to the minimum rather than past both
       comparisons and out into the file. */
    if (!(scale > DEV_MENU_SIZE_MIN)) {
        return DEV_MENU_SIZE_MIN;
    }
    if (scale > ceiling) {
        return ceiling;
    }
    return scale;
}

bool dev_menu_size_row_parse(const char *text, float *out)
{
    float  value = 0.0f;
    float  fraction = 0.1f;
    bool   seen_digit = false;
    bool   seen_point = false;
    size_t index = 0;

    if (text == NULL || out == NULL) {
        return false;
    }

    /* "auto", in any case, and whatever follows it. The chip displays the word together with the
       size it currently works out to, so this is what makes that string typeable back verbatim. */
    if ((text[0] == 'a' || text[0] == 'A') && (text[1] == 'u' || text[1] == 'U') &&
        (text[2] == 't' || text[2] == 'T') && (text[3] == 'o' || text[3] == 'O')) {
        *out = DEV_MENU_SIZE_AUTOMATIC;
        return true;
    }

    /* Hand parsed rather than handed to strtof, for one reason worth the lines: this runs on text
       the player is still typing, and strtof's locale decides whether '.' is a decimal point at
       all. A German Windows would parse "2.5" as 2 and leave ".5" as trailing rubbish, which is
       the kind of fault nobody finds until somebody else's machine. */
    for (index = 0; text[index] != '\0'; ++index) {
        char c = text[index];

        if (c >= '0' && c <= '9') {
            seen_digit = true;
            if (seen_point) {
                value += (float)(c - '0') * fraction;
                fraction *= 0.1f;
            } else {
                value = (value * 10.0f) + (float)(c - '0');
            }
            continue;
        }
        if (c == '.' && !seen_point) {
            seen_point = true;
            continue;
        }
        /* The trailing "x" the chip displays, so the value can be typed back exactly as read. */
        if ((c == 'x' || c == 'X') && text[index + 1] == '\0' && seen_digit) {
            break;
        }
        return false;
    }

    if (!seen_digit) {
        return false;
    }
    /* A typed zero asks for automatic, which is also how the ini spells it. */
    *out = (value < 0.005f) ? DEV_MENU_SIZE_AUTOMATIC : value;
    return true;
}

void dev_menu_size_row_format(float scale, char *out, size_t size)
{
    if (out == NULL || size == 0u) {
        return;
    }
    if (is_automatic(scale)) {
        /* The word AND the size it currently works out to. The number alone would look like a
           setting the player had chosen, and the word alone would not answer "how big is it". */
        (void)_snprintf(out, size - 1u, "auto %.2fx", (double)automatic_scale());
    } else {
        (void)_snprintf(out, size - 1u, "%.2fx", (double)scale);
    }
    out[size - 1u] = '\0';
}

#define DEV_MENU_SIZE_SECTION  "dev_overlay"
#define DEV_MENU_SIZE_KEY      "DevMenuSize"
#define DEV_MENU_SIZE_DECIMALS 2

float dev_menu_size_row_get(void)
{
    /* Read as text and parsed here rather than through a float reader, so the ini and the row agree
       about what counts as a number, decimal point included. */
    char  buffer[32];
    float value = DEV_MENU_SIZE_AUTOMATIC;

    if (!ini_read_string(DEV_MENU_SIZE_SECTION, DEV_MENU_SIZE_KEY, "", buffer, sizeof(buffer))) {
        return DEV_MENU_SIZE_AUTOMATIC;
    }
    if (!dev_menu_size_row_parse(buffer, &value)) {
        return DEV_MENU_SIZE_AUTOMATIC;   /* absent or unreadable: let the resolution decide */
    }
    return dev_menu_size_row_clamp(value);
}

/* The value the panel is being drawn at right now. Negative means it has not been read yet.
 *
 * This is the one owner of the number, and the drawing layer asks for it rather than being told.
 * That direction matters: it lets this file be linked into a test on its own, with no engine and no
 * renderer behind it, which is what makes the parser and the clamp testable at all. */
static float applied = DEV_MENU_SIZE_AUTOMATIC;
static bool  applied_read = false;      /* automatic IS zero, so zero cannot mean "not yet read" */

float dev_menu_size_row_current(void)
{
    if (!applied_read) {
        applied_read = true;
        applied = dev_menu_size_row_get();      /* first ask of the session, take it from the ini */
    }

    /* Resolved here and never stored: automatic has to answer the resolution of the frame being
       drawn, not whichever one happened to be set when the ini was read. */
    if (is_automatic(applied)) {
        return automatic_scale();
    }

    /* Re-clamped on every ask, not only when it is set. The first ask can happen before the display
       mode exists, and the player can change resolution from the video options at any time; either
       way the panel must not be left larger than the screen it is now being drawn on. */
    applied = dev_menu_size_row_clamp(applied);
    return applied;
}

bool dev_menu_size_row_set(float scale)
{
    const float wanted = dev_menu_size_row_clamp(scale);

    /* Applied before it is written, and applied whether or not the write succeeds. A read only ini
       is a reason to lose the setting next launch, not a reason to refuse to resize the panel the
       player is looking at right now. */
    applied = wanted;
    return ini_write_float(DEV_MENU_SIZE_SECTION, DEV_MENU_SIZE_KEY, wanted, DEV_MENU_SIZE_DECIMALS);
}
