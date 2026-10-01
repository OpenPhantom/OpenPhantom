/* mp_menu_screen.c: a screen of the engine's own menu toolkit, owned by this feature. See the
 * header for the four rules this file pays for.
 *
 * SIZE NOTE: over 600 lines, and it is one responsibility: the binding between a widget array and
 * the toolkit, which is six widget kinds to place, one loop to run, and the accessors a screen uses
 * while it is open. The screens themselves are mp_menu_screens*.c and the byte patterns are
 * mp_menu_sites.c. */
#include "mp_menu_screen.h"

#include "mp_cells.h"
#include "mp_pause.h"
#include "mp_signatures.h"

#include "common/logging.h"
#include "common/memory.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The toolkit, as this file calls it. */
typedef int32_t(__cdecl *swmenu_build_fn)(void *menu, const int32_t *bitmap_names, int32_t unused,
                                          const int32_t *font_names, sw_widget_t *widgets,
                                          int32_t sel_init);   /* zero or more: mouse on */
typedef int32_t(__cdecl *swmenu_open_fn)(void *menu);
typedef void(__cdecl *swmenu_close_fn)(void);
typedef void(__cdecl *swmenu_pump_fn)(void);
typedef int32_t(__cdecl *swmenu_nav_fn)(void);
typedef int32_t(__cdecl *swmenu_focus_fn)(void);
typedef void(__cdecl *swwidget_focus_by_id_fn)(int32_t widget_id);

#define SWNAV_LEFT   1
#define SWNAV_RIGHT  2
#define SWNAV_UP     3
#define SWNAV_DOWN   4
#define SWNAV_ACCEPT 5
#define SWNAV_CANCEL 6
#define SWNAV_CLICK  7
#define SWNAV_TAB    8

/* swmenu_open answers 1 when the screen asked for is already the top of its stack, and then
 * opens nothing: 0045D9F8 compares the current menu at 0086D370 with its argument and moves 1
 * into eax. Being BUILT is a different test, the magic 0x849EA that swmenu_build checks. */
#define SWMENU_ALREADY_ON_TOP 1

/* A string table id whose text is empty: the label a lamp is given so that our own caption beside
 * it is the only one drawn. A checkbox label is a string table id and not a literal, the draw
 * fetches the string by `start` and never reads the data pointer, and id 23 of the shipped table
 * is "". The blank is per language: ordinal 23 of THIS string file. A localisation whose line 23
 * has text would draw it beside the lamp, and a runtime check would need a site on the string
 * getter. */
#define BLANK_STRING_ID 23

/* Where the engine draws a checkbox's label: right of the lamp bitmap plus four pixels, in a rect
 * two hundred wide, left aligned and vertically centred in the widget's rect, which the draw has
 * overwritten with the bitmap's own 34x34 by then (swchkbox draw, 0x45C570). So the caption is
 * centred on those 34 rows, whatever height the caller named. */
#define LAMP_BITMAP_WIDTH  34
#define LAMP_BITMAP_HEIGHT 34
#define LAMP_LABEL_GAP     4
#define LAMP_LABEL_WIDTH   200

/* The bitmap table, {index, name} pairs terminated by -1, in the order of mp_screen_bitmap_t. The
 * engine loads each by name from big.lab the first time a widget draws it. Both name tables the
 * builder takes are {i32, char*} pairs counted with stride 8 until the first i32 is -1, and the
 * i32 is never read. A name is 8.3 with its extension: the loader strips any path, validates
 * eight dot three, looks the name up without regard to case (the image says sload.bmp, the LAB
 * stores sload.BMP), and returns 0 for a name it cannot load, after which the picture draw
 * dereferences 0 + 0x78 with no NULL check in retail. Every name below exists once in that LAB. */
static const int32_t BITMAPS[] = {
    0,  (int32_t)(uintptr_t)"splashol.bmp",
    1,  (int32_t)(uintptr_t)"sopts.bmp",
    2,  (int32_t)(uintptr_t)"presets.bmp",
    3,  (int32_t)(uintptr_t)"dialog.bmp",
    4,  (int32_t)(uintptr_t)"sload.bmp",
    5,  (int32_t)(uintptr_t)"chkbxoff.bmp",
    6,  (int32_t)(uintptr_t)"chkbxon.bmp",
    7,  (int32_t)(uintptr_t)"slgauge.bmp",
    8,  (int32_t)(uintptr_t)"slslide.bmp",
    9,  (int32_t)(uintptr_t)"saveup1.bmp",
    10, (int32_t)(uintptr_t)"saveup2.bmp",
    11, (int32_t)(uintptr_t)"savedwn1.bmp",
    12, (int32_t)(uintptr_t)"savedwn2.bmp",
    13, (int32_t)(uintptr_t)"obibio.bmp",
    -1, 0
};

typedef struct toolkit {
    bool                    resolved;
    bool                    ready;
    swmenu_build_fn         build;
    swmenu_open_fn          open;
    swmenu_close_fn         close;
    swmenu_pump_fn          pump;
    swmenu_nav_fn           nav;
    swmenu_focus_fn         focus_id;
    swmenu_focus_fn         last_focus_id;
    swwidget_focus_by_id_fn focus_by_id;
    const int32_t          *fonts;
    uint32_t                opened;
} toolkit_t;

static toolkit_t kit;

static void *resolved(mp_site_t site, const char *what, bool *ok)
{
    uintptr_t address = mp_signatures_address(site);

    if (address == 0u) {
        log_warning("a multiplayer screen cannot open: %s did not resolve", what);
        *ok = false;
        return NULL;
    }
    return (void *)address;
}

static void resolve_once(void)
{
    bool ok = true;

    if (kit.resolved) {
        return;
    }
    kit.resolved = true;
    kit.build         = (swmenu_build_fn)resolved(MP_SITE_SWMENU_BUILD, "the screen builder", &ok);
    kit.open          = (swmenu_open_fn)resolved(MP_SITE_SWMENU_OPEN, "the opener", &ok);
    kit.close         = (swmenu_close_fn)resolved(MP_SITE_SWMENU_CLOSE, "the closer", &ok);
    kit.pump          = (swmenu_pump_fn)resolved(MP_SITE_SWMENU_PUMP_FRAME, "the frame pump", &ok);
    kit.nav           = (swmenu_nav_fn)resolved(MP_SITE_SWMENU_TAKE_NAV, "the navigation", &ok);
    kit.focus_id      = (swmenu_focus_fn)resolved(MP_SITE_SWMENU_FOCUS_ID, "the focus", &ok);
    kit.last_focus_id = (swmenu_focus_fn)resolved(MP_SITE_SWMENU_LAST_FOCUS, "the last focus", &ok);
    kit.focus_by_id   = (swwidget_focus_by_id_fn)resolved(MP_SITE_SWWIDGET_FOCUS_BY_ID,
                                                          "the focus setter", &ok);
    kit.fonts = (const int32_t *)mp_cells_address(MP_CELL_MENU_FONTS);
    if (kit.fonts == NULL) {
        log_warning("a multiplayer screen cannot open: the shared font table did not resolve");
        ok = false;
    }
    kit.ready = ok;
}

bool mp_screen_toolkit_ready(void)
{
    resolve_once();
    return kit.ready;
}

/* ==============================================================================================
 * Building.
 * ============================================================================================ */

void mp_screen_init(mp_screen_t *screen)
{
    if (screen == NULL) {
        return;
    }
    memset(screen, 0, sizeof *screen);
}

static sw_widget_t *take(mp_screen_t *screen, int32_t *index)
{
    if (screen->built || screen->count >= MP_SCREEN_WIDGETS_MAX) {
        screen->overflowed = true;
        *index = -1;
        return NULL;
    }
    *index = (int32_t)screen->count;
    return &screen->widgets[screen->count++];
}

static void rect(sw_widget_t *w, int32_t x, int32_t y, int32_t width, int32_t height)
{
    w->rect.x      = x;
    w->rect.y      = y;
    w->rect.width  = width;
    w->rect.height = height;
}

int32_t mp_screen_put_pic(mp_screen_t *screen, int32_t id, mp_screen_bitmap_t bitmap, int32_t x,
                          int32_t y, int32_t w, int32_t h, bool keyed)
{
    int32_t      index;
    sw_widget_t *widget;

    if (screen == NULL || (widget = take(screen, &index)) == NULL) {
        return -1;
    }
    memset(widget, 0, sizeof *widget);
    widget->type       = SW_TYPE_PIC;
    widget->id         = id;
    widget->action     = SW_ACTION_STATIC;
    widget->visible    = 1;
    widget->start      = (int32_t)bitmap;   /* the base; the frame (state) is added, and is 0 */
    widget->font_index = keyed ? 1 : 0;     /* the BLIT MODE: 1 keys black out */
    widget->parameter  = -1;                /* nothing to mirror, so the link stays ours */
    rect(widget, x, y, w, h);
    return index;
}

int32_t mp_screen_put_text(mp_screen_t *screen, int32_t id, int32_t action, const char *caption,
                           int32_t font, int32_t align, int32_t x, int32_t y, int32_t w, int32_t h)
{
    int32_t      index;
    sw_widget_t *widget;

    if (screen == NULL || (widget = take(screen, &index)) == NULL) {
        return -1;
    }
    memset(widget, 0, sizeof *widget);
    widget->type       = SW_TYPE_TEXT;
    widget->id         = id;
    widget->action     = action;
    widget->visible    = 1;
    widget->font_index = font;
    widget->parameter  = -1;   /* a text whose parameter is not -1 has its alignment overwritten */
    widget->link       = (void *)(uintptr_t)align;
    widget->data       = (void *)(uintptr_t)caption;
    rect(widget, x, y, w, h);
    return index;
}

/* `start` is the buffer size, `state` the caret and the data pointer the buffer. Typing inserts
 * at the caret clamped to `start - 2`. A delete at caret 0 writes a terminator at buf[-1] and
 * leaves the caret at -1, the guard being `>= 0` where it should be `> 0`, which is why every
 * buffer here has four guard bytes in front of it in the same structure. The draw copies the
 * buffer into a 204 byte stack array without a bound, so buffers stay under 200. */
int32_t mp_screen_put_edit(mp_screen_t *screen, int32_t id, mp_screen_edit_t *buffer, int32_t x,
                           int32_t y, int32_t w, int32_t h)
{
    int32_t      index;
    sw_widget_t *widget;

    if (screen == NULL || buffer == NULL || (widget = take(screen, &index)) == NULL) {
        return -1;
    }
    memset(widget, 0, sizeof *widget);
    widget->type       = SW_TYPE_EDIT;
    widget->id         = id;
    widget->action     = SW_ACTION_SELECT;
    widget->visible    = 1;
    widget->start      = (int32_t)sizeof buffer->text;   /* the buffer size */
    widget->font_index = MP_FONT_COURIER;
    widget->parameter  = -1;
    widget->data       = buffer->text;
    rect(widget, x, y, w, h);
    return index;
}

/* The engine flips the lamp before the loop sees the code: the toggle is the checkbox's own
 * activate at 0045C6BD, reached through the activate dispatcher on a Return key (then navigation
 * code 5) and on a mouse button release over the focus (then code 7). A loop that flipped it
 * again would undo it; a screen that wants two lamps exclusive sets both states explicitly. */
int32_t mp_screen_put_lamp(mp_screen_t *screen, int32_t id, const char *caption, bool on,
                           int32_t x, int32_t y, int32_t w, int32_t h)
{
    int32_t      index;
    sw_widget_t *widget;

    if (screen == NULL || (widget = take(screen, &index)) == NULL) {
        return -1;
    }
    memset(widget, 0, sizeof *widget);
    widget->type       = SW_TYPE_CHECKBOX;
    widget->id         = id;
    widget->action     = SW_ACTION_SELECT;
    widget->visible    = 1;
    widget->start      = BLANK_STRING_ID;      /* its own label draws nothing */
    widget->state      = on ? 1 : 0;
    widget->font_index = MP_FONT_COURIER;
    widget->parameter  = (int32_t)MP_BMP_CHKBXOFF;   /* off, and on is the next index */
    rect(widget, x, y, LAMP_BITMAP_WIDTH, LAMP_BITMAP_HEIGHT);

    /* The caption, where the engine would have drawn the label. STATIC, so the lamp keeps the
     * focus and the hit-test. `w` is the caption's column, not the lamp's: the engine overwrites
     * the lamp's rect with the bitmap's size, so the width a caller gives is only ever of use
     * here. `h` is the bitmap's and cannot be anything else. */
    (void)h;
    if (mp_screen_put_text(screen, MP_SCREEN_LAMP_CAPTION_ID(id), SW_ACTION_STATIC, caption,
                           MP_FONT_COURIER,
                           MP_ALIGN_LEFT_VCENTRE, x + LAMP_BITMAP_WIDTH + LAMP_LABEL_GAP, y,
                           w > 0 ? w : LAMP_LABEL_WIDTH, LAMP_BITMAP_HEIGHT) < 0) {
        return -1;
    }
    return index;
}

int32_t mp_screen_put_slider(mp_screen_t *screen, int32_t id, int32_t notches, int32_t value,
                             int32_t x, int32_t y, int32_t w, int32_t h)
{
    int32_t      index;
    sw_widget_t *widget;

    if (screen == NULL || notches < 2 || (widget = take(screen, &index)) == NULL) {
        return -1;
    }
    memset(widget, 0, sizeof *widget);
    widget->type       = SW_TYPE_SLIDER;
    widget->id         = id;
    widget->action     = SW_ACTION_SELECT;
    widget->visible    = 1;
    widget->start      = notches;
    widget->state      = value < 0 ? 0 : (value >= notches ? notches - 1 : value);
    widget->font_index = (int32_t)MP_BMP_SLSLIDE;   /* the knob */
    widget->parameter  = (int32_t)MP_BMP_SLGAUGE;   /* the gauge */
    rect(widget, x, y, w, h);
    return index;
}

/* The rows are ours, the count is the engine's. The link is a descriptor of seven words: count,
 * top index, row height, visible rows, drawn, arrow mode, scroll direction. The init message at
 * open recounts the pairs, sets the row height to the larger of the font height and 16, sets the
 * visible rows to (height - 3) / row height and REWRITES the widget's height; every draw recounts
 * and clamps the selection. The visible rows must be at least three when the list can be longer
 * than the view, or the view walk never terminates. */
mp_screen_list_t *mp_screen_put_list(mp_screen_t *screen, int32_t id, bool selectable, int32_t x,
                                     int32_t y, int32_t w, int32_t h)
{
    int32_t           index;
    sw_widget_t      *widget;
    mp_screen_list_t *list;

    if (screen == NULL || screen->list_count >= MP_SCREEN_LISTS_MAX ||
        (widget = take(screen, &index)) == NULL) {
        return NULL;
    }
    list = &screen->lists[screen->list_count++];
    memset(list, 0, sizeof *list);
    list->widget   = (size_t)index;
    list->items[0] = -1;   /* no rows yet: the terminator */

    memset(widget, 0, sizeof *widget);
    widget->type       = SW_TYPE_LISTBOX;
    widget->id         = id;
    widget->action     = selectable ? SW_ACTION_SELECT : SW_ACTION_STATIC;
    widget->visible    = 1;
    widget->start      = -1;                       /* the rows are {id, text} pairs of ours */
    widget->state      = selectable ? 0 : -1;      /* the selected row */
    widget->font_index = MP_FONT_COURIER;
    /* Zero: the list's draw tests this field against zero (0045CCD7 cmp [w+0x1C],0) and
     * draws a dark blue frame when it is set, which the shipped load screen's list does not
     * carry. The arrows are the descriptor's scroll base, descriptor[5], which every open sets
     * to -1 (0045CA64), the words UP and DOWN; mp_screen_run sets it again after the open. */
    widget->parameter  = 0;
    widget->link       = list->descriptor;
    widget->data       = list->items;
    rect(widget, x, y, w, h);
    return list;
}

void mp_screen_list_set_rows(mp_screen_list_t *list, const char *const *rows, size_t count)
{
    size_t i;

    if (list == NULL) {
        return;
    }
    if (count > MP_SCREEN_LIST_ROWS_MAX) {
        count = MP_SCREEN_LIST_ROWS_MAX;
    }
    for (i = 0; i < count; ++i) {
        const char *row = (rows != NULL && rows[i] != NULL) ? rows[i] : "";
        size_t      length = strlen(row);

        if (length >= MP_SCREEN_ROW_TEXT_MAX) {
            length = MP_SCREEN_ROW_TEXT_MAX - 1u;
        }
        memcpy(list->text[i], row, length);
        list->text[i][length] = '\0';
        list->items[i * 2u]      = (int32_t)i;
        list->items[i * 2u + 1u] = (int32_t)(uintptr_t)list->text[i];
    }
    list->items[count * 2u]      = -1;   /* the terminator the engine counts up to */
    list->items[count * 2u + 1u] = 0;
    list->rows = count;
}

bool mp_screen_build(mp_screen_t *screen)
{
    sw_widget_t *terminator;

    if (screen == NULL) {
        return false;
    }
    if (screen->built) {
        return true;
    }
    if (!mp_screen_toolkit_ready()) {
        return false;
    }
    if (screen->overflowed || screen->count >= MP_SCREEN_WIDGETS_MAX + 1u) {
        log_error("a multiplayer screen was not built: its widget array holds %u and the layout "
                  "wants more, so nothing is opened rather than written past the end",
                  (unsigned)MP_SCREEN_WIDGETS_MAX);
        return false;
    }
    terminator = &screen->widgets[screen->count];
    memset(terminator, 0, sizeof *terminator);
    terminator->type = SW_TYPE_TERMINATOR;

    /* Built ONCE and reopened as often as anybody likes: the bitmap handle array is sized at the
     * first build and the record carries a magic, and the record is registered in a list the
     * engine walks with its own free at shutdown, which is why every record here is static. For
     * a TEXT or PIC with a parameter at or above zero the build looks the parameter up as a
     * widget id in the CURRENT screen, not the one being built, and writes the answer into the
     * link, which for a TEXT is its alignment; a zeroed TEXT built while the title is current
     * would be aligned by the title's widget 0, which is why every one here carries -1. */
    (void)kit.build(screen->record, BITMAPS, 0, kit.fonts, screen->widgets, 0);
    screen->built = true;
    return true;
}

/* ==============================================================================================
 * Running.
 * ============================================================================================ */

static sw_widget_t *find(mp_screen_t *screen, int32_t id)
{
    size_t i;

    for (i = 0; i < screen->count; ++i) {
        if (screen->widgets[i].id == id) {
            return &screen->widgets[i];
        }
    }
    return NULL;
}

/* Whether the focused widget has already spent this arrow key on itself: a list box moves its
 * row on up and down, a slider moves its knob on left and right. The window procedure hands the
 * key to the widget first and leaves the navigation code as well, so a loop that also stepped
 * the focus would jump off the list on its first press. Left and right a list ignores, so those
 * step the focus off it, which is how the shipped load screen leaves its list. The list's key
 * handler subtracts 0x21 and jumps through a table at 0045CC5D: Page Up and Page Down page, Up
 * and Down step, and End, Home, Left and Right all land on the plain return at 0045CC53. */
static bool moves_itself(const mp_screen_t *screen, int32_t id, int32_t code)
{
    size_t i;

    for (i = 0; i < screen->count; ++i) {
        if (screen->widgets[i].id == id) {
            if (screen->widgets[i].type == SW_TYPE_LISTBOX) {
                return code == SWNAV_UP || code == SWNAV_DOWN;
            }
            if (screen->widgets[i].type == SW_TYPE_SLIDER) {
                return code == SWNAV_LEFT || code == SWNAV_RIGHT;
            }
            return false;
        }
    }
    return false;
}

/* The focus, stepped over our own array rather than through the engine's stepper: the two engine
 * functions that do it differ only in a call displacement, which a mask cannot tell apart. */
static void step_focus(mp_screen_t *screen, int32_t from, int direction)
{
    size_t start = 0;
    size_t index;
    size_t count = screen->count;

    for (index = 0; index < count; ++index) {
        if (screen->widgets[index].id == from) {
            start = index;
            break;
        }
    }
    for (index = 1; index <= count; ++index) {
        size_t at = direction > 0 ? (start + index) % count
                                  : (start + count - (index % count)) % count;

        if (screen->widgets[at].visible != 0 && screen->widgets[at].action != SW_ACTION_STATIC) {
            kit.focus_by_id(screen->widgets[at].id);
            return;
        }
    }
}

/* The engine frees every list's rows on close with its own allocator: the close releases the
 * screen, and for every list box with a negative start and a non null data pointer the release
 * frees each text with the host's own CRT free. Unhooking them after the last pump and before
 * the close is what keeps ours; they are hooked again before the next open, which is when the
 * engine counts them, and never with a shorter dummy array, because the count is the last
 * recount. */
static void hook_lists(mp_screen_t *screen, bool hooked)
{
    size_t i;

    for (i = 0; i < screen->list_count; ++i) {
        screen->widgets[screen->lists[i].widget].data = hooked ? screen->lists[i].items : NULL;
        if (hooked) {
            /* The top row. The toolkit moves it as the list scrolls (the recount writes it at
             * 0045D33D, the page at 0045D170), but its reset leaves it where the last visit put
             * it, so it is put back here. */
            screen->lists[i].descriptor[1] = 0;
        }
    }
}

/* The loop, copied from the seventeen shipped screens. Nesting is the engine's own: an open at
 * depth one or more releases the parent's bitmap handles and pushes the child, and the close
 * pops, after which the parent reloads its sheets lazily on its next draw, a hitch and not a
 * fault; none of the in level work of a depth zero open (the clock cell, the mode switch, the
 * blur) runs for a nested one. Every id based toolkit call acts on the current screen only,
 * which is why nothing here calls one between open and close except the focus setter. The frame
 * hook keeps running, because the frame end at 0046C139 sits inside the frame pump as well as
 * inside the game's own frame, so the bridge's idle pump runs from this loop too; what a nested
 * screen withholds is the module broadcasts 0x0D and 0x11. */
int32_t mp_screen_run(mp_screen_t *screen, int32_t focus_id, mp_screen_activate_fn activate,
                      mp_screen_frame_fn frame, void *ctx)
{
    int32_t closed_by = -1;
    bool    running = true;
    size_t  i;

    if (screen == NULL || !mp_screen_build(screen)) {
        return -1;
    }
    if (screen->open) {
        log_warning("a multiplayer screen was asked to open while it was open");
        return -1;
    }
    screen->close_requested = false;
    hook_lists(screen, true);
    if (kit.open(screen->record) == SWMENU_ALREADY_ON_TOP) {
        log_warning("a multiplayer screen was already on show, so it was not opened again");
        hook_lists(screen, false);
        return -1;
    }
    screen->open = true;
    ++kit.opened;
    /* The arrows as bitmaps, set after the open because the open resets the scroll base to
     * the words UP and DOWN. The shipped load screen does the same after its own open, with
     * its saveup1 at index 3 of its own table (004408AB). */
    for (i = 0; i < screen->list_count; ++i) {
        screen->lists[i].descriptor[5] = (int32_t)MP_BMP_SAVEUP1;
    }
    if (focus_id >= 0 && find(screen, focus_id) != NULL) {
        kit.focus_by_id(focus_id);
    }

    while (running) {
        int32_t focus;
        int32_t code;

        /* Over a session's pause menu the list runs the world's frame, as the engine's own screens
         * choose it; everywhere else the menu's own pump, which runs no substep. */
        if (!mp_pause_pump_world()) {
            kit.pump();
        }
        if (frame != NULL) {
            frame(ctx);
            if (screen->close_requested) {
                break;
            }
        }
        focus = kit.focus_id();
        code  = kit.nav();

        if (code == 0) {
            continue;
        }
        if (focus == -1 && code != SWNAV_CANCEL) {
            /* The mouse wandered off every widget. The engine's screens spend an arrow or an
             * accept on putting the focus back rather than acting, and so does this one. A cancel
             * they act on whatever the focus, and so does this one: the load screen's cancel arm
             * sets its result without reading the focus. Otherwise the first ESC with the pointer
             * on empty space would do nothing. */
            kit.focus_by_id(kit.last_focus_id());
            continue;
        }
        switch (code) {
        case SWNAV_UP:
        case SWNAV_LEFT:
            if (!moves_itself(screen, focus, code)) {
                step_focus(screen, focus, -1);
            }
            break;
        case SWNAV_DOWN:
        case SWNAV_RIGHT:
            if (!moves_itself(screen, focus, code)) {
                step_focus(screen, focus, 1);
            }
            break;
        case SWNAV_TAB:
            step_focus(screen, focus, 1);
            break;
        case SWNAV_ACCEPT:
        case SWNAV_CLICK:
            if (activate != NULL && !activate(focus, ctx)) {
                closed_by = focus;
                running = false;
            }
            break;
        case SWNAV_CANCEL:
            closed_by = -1;
            running = false;
            break;
        default:
            break;
        }
    }

    hook_lists(screen, false);
    kit.close();
    screen->open = false;
    return closed_by;
}

/* Read from our own array, never through the toolkit's state getter, which reads *(0 + 0x14)
 * for an id it cannot find in the current screen. */
int32_t mp_screen_get_state(const mp_screen_t *screen, int32_t id)
{
    size_t i;

    if (screen == NULL) {
        return -1;
    }
    for (i = 0; i < screen->count; ++i) {
        if (screen->widgets[i].id == id) {
            return screen->widgets[i].state;
        }
    }
    return -1;
}

void mp_screen_set_state(mp_screen_t *screen, int32_t id, int32_t value)
{
    sw_widget_t *widget = screen != NULL ? find(screen, id) : NULL;

    if (widget != NULL) {
        widget->state = value;
    }
}

void mp_screen_get_edit(mp_screen_t *screen, int32_t id, char *out, size_t size)
{
    sw_widget_t *widget;

    if (screen == NULL || out == NULL || size == 0u) {
        return;
    }
    out[0] = '\0';
    widget = find(screen, id);
    if (widget == NULL || widget->type != SW_TYPE_EDIT || widget->data == NULL) {
        return;
    }
    /* The engine types straight into the buffer the widget names, so it is read from there rather
     * than through a toolkit call that would look the id up in whatever screen is current. */
    strncpy(out, (const char *)widget->data, size - 1u);
    out[size - 1u] = '\0';
}

void mp_screen_set_edit(mp_screen_t *screen, int32_t id, const char *text)
{
    sw_widget_t *widget;
    size_t       room;
    size_t       length;

    if (screen == NULL || text == NULL) {
        return;
    }
    widget = find(screen, id);
    if (widget == NULL || widget->type != SW_TYPE_EDIT || widget->data == NULL) {
        return;
    }
    /* Into our own buffer rather than through the toolkit's writer, which looks the id up in
     * whatever screen is current and copies without a bound. The field grows to `start - 1`
     * characters, so that is where the text is cut: the edit's input at 0045C7B7 stores at the
     * caret, writes the terminator one past it, and clamps the caret to `start - 2`. */
    room   = widget->start > 1 ? (size_t)widget->start - 1u : 0u;
    length = strlen(text);
    if (length > room) {
        length = room;
    }
    memcpy(widget->data, text, length);
    ((char *)widget->data)[length] = '\0';
    widget->state = (int32_t)length;   /* the caret */
}

void mp_screen_set_edit_limit(mp_screen_t *screen, int32_t id, size_t max_chars)
{
    sw_widget_t *widget = screen != NULL ? find(screen, id) : NULL;

    if (widget == NULL || widget->type != SW_TYPE_EDIT) {
        return;
    }
    if (max_chars + 2u > sizeof ((mp_screen_edit_t *)0)->text) {
        max_chars = sizeof ((mp_screen_edit_t *)0)->text - 2u;
    }
    /* One above the count: the engine lets a field grow to `start - 1` characters. */
    widget->start = (int32_t)(max_chars + 1u);
}

void mp_screen_set_slider_notches(mp_screen_t *screen, int32_t id, int32_t notches)
{
    sw_widget_t *widget = screen != NULL ? find(screen, id) : NULL;

    if (widget == NULL || widget->type != SW_TYPE_SLIDER || notches < 2) {
        return;
    }
    /* The notch count and the knob position are both fields of our own widget record, which the
     * engine re-reads on every draw and every key, so a range that shrinks under an open screen
     * is safe as long as the knob is brought inside it in the same breath. */
    widget->start = notches;
    if (widget->state >= notches) {
        widget->state = notches - 1;
    }
    if (widget->state < 0) {
        widget->state = 0;
    }
}

void mp_screen_set_caption(mp_screen_t *screen, int32_t id, const char *caption)
{
    sw_widget_t *widget = screen != NULL ? find(screen, id) : NULL;

    if (widget != NULL && widget->type == SW_TYPE_TEXT) {
        widget->data = (void *)(uintptr_t)caption;
    }
}

/* Hidden is hidden to the mouse as well: the draw at 00462E51 walks the array and draws only
 * widgets with visible == 1, and the hit test at 00462903 skips invisible widgets and STATIC
 * ones, so a control the mode takes away cannot be drawn, clicked or focused, and the loop's own
 * focus stepper skips it for the same reason. A lamp's caption is a widget of its own and has to
 * be hidden with the lamp. */
void mp_screen_set_visible(mp_screen_t *screen, int32_t id, bool visible)
{
    sw_widget_t *widget = screen != NULL ? find(screen, id) : NULL;

    if (widget != NULL) {
        widget->visible = visible ? 1 : 0;
    }
}

void mp_screen_focus(mp_screen_t *screen, int32_t id)
{
    if (screen != NULL && screen->open && find(screen, id) != NULL) {
        kit.focus_by_id(id);
    }
}

void mp_screen_request_close(mp_screen_t *screen)
{
    if (screen != NULL) {
        screen->close_requested = true;
    }
}
