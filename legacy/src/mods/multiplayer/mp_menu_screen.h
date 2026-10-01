/* mp_menu_screen.h: a screen of the engine's own menu toolkit, owned by this feature.
 *
 * Everything a multiplayer screen shows is one of the toolkit's own widget types, a picture, a
 * text, an edit field, a list box, a checkbox lamp, a slider, placed where the SHIPPED overlay
 * bitmap of a shipped screen paints a place for it. This file is the binding between
 * such a screen and the toolkit: it holds the widget array, builds the screen once, opens it,
 * drives the same loop every shipped screen drives (pump a frame, read the focus, take the
 * navigation code, act), and closes it. The screens themselves are mp_menu_screens.c.
 *
 * Four rules, each paid for once, by the crash of the first run in the game, by the toolkit's
 * list box protocol and by the review of the screens against the shipped ones:
 *
 *   A toolkit call with a widget id means the screen on show. `swmenu_setEditText`,
 *   `swmenu_getWidgetState` and their kind look the id up in whatever screen is current and do not
 *   check what they found. This file no longer calls them: an edit's text goes into its own
 *   buffer and a widget's state is read out of its own array, so the rule guards the next caller.
 *
 *   A list's rows are ours, its count is the engine's. A list box carries a descriptor of seven
 *   integers and an array of {id, text} pairs; the engine recounts the pairs on every draw and
 *   clamps its selection to them, so the rows may change while the screen is open. On close the
 *   engine FREES the pairs of every list whose `start` is negative with its own allocator, so this
 *   file unhooks its arrays before it closes.
 *
 *   The list moves itself. Arrow keys reach the focused widget as key codes before they become
 *   navigation codes; a list box moves its row on up and down. The loop therefore steps the focus
 *   on up and down only when the focus is NOT on a list, which is what the shipped load screen
 *   does. Left and right a list ignores, and they step the focus off it.
 *
 *   A checkbox label is a string table id, not a literal: its draw fetches the text through the
 *   game's localisation table. So a lamp is placed with a blank id and its caption is a TEXT widget
 *   of ours beside it, at the offset the engine would have used.
 */
#ifndef MULTIPLAYER_MP_MENU_SCREEN_H
#define MULTIPLAYER_MP_MENU_SCREEN_H

#include "common/engine_types.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The bitmaps a screen of ours may place, all shipped in big.lab, loaded by the engine by name
 * when first drawn. The order is the index a PIC or a lamp or a slider names. */
typedef enum mp_screen_bitmap {
    MP_BMP_SPLASHOL = 0,   /* the plate behind every options screen */
    MP_BMP_SOPTS,          /* the options hub overlay: four slots, a readout, the red button */
    MP_BMP_PRESETS,        /* the controller presets overlay: list, three slots, band, edit bar */
    MP_BMP_DIALOG,         /* the general options overlay: lamps window, list, readout, slider */
    MP_BMP_SLOAD,          /* the load game overlay: list, thumbnail frame, readout, two slots */
    MP_BMP_CHKBXOFF,       /* the lamp, off; the on lamp is the next index (the engine adds one) */
    MP_BMP_CHKBXON,
    MP_BMP_SLGAUGE,        /* the slider's gauge */
    MP_BMP_SLSLIDE,        /* and its knob */
    MP_BMP_SAVEUP1,        /* the list's scroll buttons, in the order the list draws them
                            * from its scroll base: up plain, up lit, down plain, down lit */
    MP_BMP_SAVEUP2,
    MP_BMP_SAVEDWN1,
    MP_BMP_SAVEDWN2,
    MP_BMP_OBIBIO,         /* the player portrait, 90x90, for the thumbnail frame */
    MP_BMP_COUNT
} mp_screen_bitmap_t;

/* The three fonts every frontend screen shares, by their index in the shared font table. */
#define MP_FONT_INDUST  0
#define MP_FONT_SYSFONT 1
#define MP_FONT_COURIER 2

/* Text alignment, the toolkit's own numbers. */
#define MP_ALIGN_CENTRE       0
#define MP_ALIGN_LEFT         1
#define MP_ALIGN_RIGHT        2
#define MP_ALIGN_LEFT_VCENTRE 3

/* How many widgets a screen may hold, and how many rows a list may show. */
#define MP_SCREEN_WIDGETS_MAX  48u
#define MP_SCREEN_LISTS_MAX    6u
#define MP_SCREEN_LIST_ROWS_MAX 32u
#define MP_SCREEN_ROW_TEXT_MAX 48u

/* Four guard bytes in front of every edit buffer: the engine's edit widget answers a delete at
 * caret 0 by writing a zero one byte BEFORE its buffer and leaving the caret at -1. The widget's
 * `start` is the buffer size, `state` the caret and `pData` the buffer; typing inserts at the
 * caret clamped to `start - 2` and writes the terminator one past it, so the text grows to
 * `start - 1` characters. The draw copies the buffer into a 204 byte stack array without a
 * bound, which is why the text stays well under 200. */
typedef struct mp_screen_edit {
    char guard[4];
    char text[64];
} mp_screen_edit_t;

typedef struct mp_screen_list {
    int32_t  descriptor[7];   /* count, first, row height, visible rows, drawn, scroll base,
                               * pressed */
    int32_t  items[(MP_SCREEN_LIST_ROWS_MAX + 1u) * 2u];   /* {id, char*} pairs and a terminator */
    char     text[MP_SCREEN_LIST_ROWS_MAX][MP_SCREEN_ROW_TEXT_MAX];
    size_t   rows;
    size_t   widget;          /* index into the screen's widget array */
} mp_screen_list_t;

typedef struct mp_screen {
    uint8_t     record[0x54];   /* the toolkit's own screen record; zeroed once, then its own */
    sw_widget_t widgets[MP_SCREEN_WIDGETS_MAX + 1u];
    size_t      count;
    bool        overflowed;
    bool        built;
    bool        open;
    mp_screen_list_t lists[MP_SCREEN_LISTS_MAX];
    size_t           list_count;
    bool             close_requested;   /* a frame callback asked to leave */
} mp_screen_t;

/* What the loop asks of a screen: what to do when the focused widget is activated (false closes
 * the screen), and what to do once per frame while it is open (a browser polls its socket here).
 * `ctx` is whatever the screen handed in. */
typedef bool (*mp_screen_activate_fn)(int32_t id, void *ctx);
typedef void (*mp_screen_frame_fn)(void *ctx);

/* ---------------------------------------------------------------------------------------------
 * Building. All of these are called before mp_screen_build, in array order, which is draw order:
 * the plate first, the overlay second, the widgets on top. Each answers the widget's index, or
 * -1 when the array is full, in which case the build refuses and nothing opens.
 * ------------------------------------------------------------------------------------------- */

void mp_screen_init(mp_screen_t *screen);

/* A picture at a rect. `keyed` draws black as transparent, which is how every overlay is drawn
 * over its plate. */
int32_t mp_screen_put_pic(mp_screen_t *screen, int32_t id, mp_screen_bitmap_t bitmap, int32_t x,
                          int32_t y, int32_t w, int32_t h, bool keyed);

/* A text with a literal caption. `action` is SW_ACTION_STATIC, _SELECT, _DEFAULT or _CANCEL. The
 * caption is not copied: it must outlive the screen. */
int32_t mp_screen_put_text(mp_screen_t *screen, int32_t id, int32_t action, const char *caption,
                           int32_t font, int32_t align, int32_t x, int32_t y, int32_t w, int32_t h);

/* An edit field over the caller's buffer, in courier, as the pause screen's savegame name is. */
int32_t mp_screen_put_edit(mp_screen_t *screen, int32_t id, mp_screen_edit_t *buffer, int32_t x,
                           int32_t y, int32_t w, int32_t h);

/* A lamp with a caption of ours beside it. The lamp's own label is blank; the caption is a TEXT at
 * the offset the engine would have drawn its label, in courier, so the two are indistinguishable
 * from a shipped checkbox. The checkbox draw fetches its label through the string table by the
 * widget's `start` and never reads `pData`, so the lamp is placed with the id of an empty line,
 * ordinal 23 of the installed table; a localisation whose line 23 has text would draw it beside
 * the lamp. The draw puts the label at x + 34 + 4, 200 wide, aligned 3, and overwrites the
 * widget's own width and height with the bitmap's 34 by 34, so the hit box is the box alone
 * whatever height the caller named. The engine flips the lamp itself, in the checkbox's activate
 * at 0x45C6BD, on Return over the focus (navigation 5) and on a mouse release over it
 * (navigation 7), before the loop ever sees the code; a loop that flipped again would undo it. */
int32_t mp_screen_put_lamp(mp_screen_t *screen, int32_t id, const char *caption, bool on,
                           int32_t x, int32_t y, int32_t w, int32_t h);

/* The id of that caption. A lamp that is hidden has to take its caption with it, and the caption
 * is a widget of its own; ids at and above this base belong to captions and to nothing else. */
#define MP_SCREEN_LAMP_CAPTION_BASE 1000
#define MP_SCREEN_LAMP_CAPTION_ID(lamp_id) (MP_SCREEN_LAMP_CAPTION_BASE + (lamp_id))

/* A slider with `notches` positions, standing at `value` (0-based). */
int32_t mp_screen_put_slider(mp_screen_t *screen, int32_t id, int32_t notches, int32_t value,
                             int32_t x, int32_t y, int32_t w, int32_t h);

/* A list box in courier. `selectable` false makes it STATIC: a second column that follows the
 * first, the way the shipped key list does. Answers the list's slot, or NULL.
 *
 * The engine's init at open recounts the pairs, sets the row height to the larger of the font
 * height and 16, sets the visible rows to (rect.h - 3) / rowHeight and REWRITES rect.h to fit;
 * every draw recounts and clamps the selection. The top index is never written by the toolkit,
 * so it is zeroed here before every open. A list that can be longer than its view needs at least
 * three visible rows, or the engine's view walk never terminates. On close the engine frees the
 * text of every LISTBOX whose `start` is negative and whose `pData` is set, through WMAIN's own
 * CRT free, so the arrays are unhooked after the last pump and hooked again before the next
 * open; never a shorter dummy array, because the count the free uses is the last recount. */
mp_screen_list_t *mp_screen_put_list(mp_screen_t *screen, int32_t id, bool selectable, int32_t x,
                                     int32_t y, int32_t w, int32_t h);

/* Replaces a list's rows. Safe while the screen is open: the engine recounts on every draw. */
void mp_screen_list_set_rows(mp_screen_list_t *list, const char *const *rows, size_t count);

/* Terminates the array and builds the screen with this feature's bitmap table and the shared
 * fonts. False, with a log line, when the array overflowed or the toolkit did not resolve.
 *
 * Both name tables the build takes are {i32, char*} pairs walked with a stride of 8 until the
 * first i32 is -1; the i32 is never read. The bitmap handle array is sized at the first build
 * and the record carries a magic, so a record is built ONCE and reopened as often as anybody
 * likes; the engine also registers it in a list it frees itself at shutdown, which is why every
 * record here is static. Every TEXT and PIC is built with `param` -1: for those two types a
 * `param` of 0 or more is looked up as a widget id in the CURRENT screen, not the one being
 * built, and written into the widget's link, which for a TEXT is its alignment. Bitmap names
 * are eight dot three with the extension, looked up without regard to case (the exe says
 * sload.bmp, the LAB stores sload.BMP); a name that does not load answers 0 and the PIC draw
 * then reads 0 + 0x78 with no check in retail, so every name in the table has to exist. */
bool mp_screen_build(mp_screen_t *screen);

/* ---------------------------------------------------------------------------------------------
 * Running.
 * ------------------------------------------------------------------------------------------- */

/* Opens the screen, runs its loop until an activation answers false or the player cancels, and
 * closes it. Answers the id that closed it, or -1 for a cancel. `focus_id` is the widget the
 * focus starts on. Nests: a screen may call this for another from inside its activate, and from
 * inside its frame callback, which is how a lobby asks a refused player for the password.
 *
 * The loop is the one the seventeen shipped screens run: pump a frame, read the focus, take the
 * navigation code, act. A focus of -1 is the mouse off every widget, and the code is spent on
 * putting the focus back. ACCEPT and CLICK share every arm. The focus is stepped over our own
 * array rather than through the engine's, because its next and previous steppers differ only in
 * a call displacement a byte pattern cannot tell apart. Nesting is the engine's own: an open at
 * depth one or more releases the parent's bitmap handles and pushes the child, and the close
 * pops, after which the parent reloads its sheets on its next draw, a hitch and not a fault.
 * None of the in-level work of a depth zero open (the clock cell, the mode switch, the blur)
 * runs for a nested one. The frame end hook at 0x46C139 sits inside the toolkit's own frame
 * pump as well as inside the engine's frame, so the bridge's idle pump keeps running from this
 * loop; what a nested screen withholds is the module broadcasts 0x0D and 0x11. */
int32_t mp_screen_run(mp_screen_t *screen, int32_t focus_id, mp_screen_activate_fn activate,
                      mp_screen_frame_fn frame, void *ctx);

/* Only between open and close, i.e. from a callback of mp_screen_run. The state is read from our
 * own array and never through the toolkit's getter, which reads *(0 + 0x14) for an id it cannot
 * find in the current screen. */
int32_t mp_screen_get_state(const mp_screen_t *screen, int32_t id);    /* -1 for an unknown id */
void    mp_screen_set_state(mp_screen_t *screen, int32_t id, int32_t value);
void    mp_screen_get_edit(mp_screen_t *screen, int32_t id, char *out, size_t size);

/* Fills an edit field and puts the caret at its end. Open or closed: the buffer is ours and the
 * caret is the widget's state, which the open does not reset. */
void    mp_screen_set_edit(mp_screen_t *screen, int32_t id, const char *text);

/* How many characters the field takes. The engine lets a field grow to one less than its size,
 * so the size is set one above the count, and the count is held two short of the buffer so the
 * terminator and a spare byte always fit. */
void    mp_screen_set_edit_limit(mp_screen_t *screen, int32_t id, size_t max_chars);

/* How many positions a slider has, changed after the screen was built. The knob is pulled back
 * inside the new range, because a state past the last notch would draw the knob off its gauge and
 * would be read back as a value nothing allows. Fewer than two notches is refused. */
void    mp_screen_set_slider_notches(mp_screen_t *screen, int32_t id, int32_t notches);
void    mp_screen_set_caption(mp_screen_t *screen, int32_t id, const char *caption);

/* Hidden is hidden to the mouse as well. The engine's draw at 0x462E51 walks the array and draws
 * only widgets whose visible word is 1, and its hit test at 0x462903 skips invisible widgets and
 * STATIC ones, so a control the mode takes away cannot be drawn, clicked or focused, and the
 * loop's own focus stepper skips it for the same reason. */
void    mp_screen_set_visible(mp_screen_t *screen, int32_t id, bool visible);
void    mp_screen_focus(mp_screen_t *screen, int32_t id);

/* Asks the loop to close after this frame. The only way a FRAME callback can end a screen: an
 * activation can answer false, but a frame callback has no answer, and a lobby closes on a note
 * arriving rather than on a button. */
void    mp_screen_request_close(mp_screen_t *screen);

/* Whether the toolkit resolved. False turns every screen into a no-op with a log line. */
bool mp_screen_toolkit_ready(void);

#endif /* MULTIPLAYER_MP_MENU_SCREEN_H */
