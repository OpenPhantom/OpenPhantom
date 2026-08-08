/* menu_patcher.h: append widgets to one of the engine's own menu screens, safely and generically.
 *
 * ==============================================================================================
 * Why appending a widget is a data change, not a code change
 *
 * A swift screen hands its widget array to swmenu_build 0x45e7a3 as a single `push imm32`.
 * build walks the array once to the `type == -1` terminator, counts, records the default and
 * cancel widgets, stamps SW_ACTION_STATIC onto the terminator slot and stores the pointer in
 * pMenu->pWidgets. It allocates NOTHING per widget, and a 0x849ea magic guard makes it
 * idempotent, so the array adopted on the first call is the array the screen keeps for the rest
 * of the process.
 *
 * That is why one repointed immediate is the whole patch: hand the screen a COPY of its own array
 * with entries appended, and it has more widgets, natively, drawn by its own toolkit.
 *
 * swmenu_releaseScreen 0x45de4c frees exactly two widget classes on close, SW_3D and
 * SW_LISTBOX-with-start<0. Sliders and text labels own no heap and are never touched, so a static
 * array survives every open/close cycle.
 *
 * ==============================================================================================
 * What this module knows and what it refuses to know
 *
 * It understands the technical widget format: the stride, the terminator, the fields a slider or
 * a label needs, and duplicate ids. It knows nothing about what any widget MEANS. Positions,
 * notch counts, captions, ids and the value behind them belong to the feature.
 *
 * ==============================================================================================
 * The rectangle is not a crop, and for most widget types it is not even an input
 *
 * This is the fact that invalidated three attempts at laying widgets out, so it is stated before
 * anything else a caller might believe. The blit the whole toolkit ends in takes five arguments,
 * canvas, bitmap, x, y, font, and NO width and NO height. It clips against a hard-coded 640x480
 * and nothing else.
 *
 * There is therefore no crop and no scale anywhere in this toolkit. A rectangle cannot turn a
 * full-screen bitmap into a partial overlay: asking for a 286x232 window onto a 640x480 plate draws
 * the whole plate, positioned at the rectangle's corner, with whatever falls off the canvas simply
 * gone. If the interesting part of that bitmap is on the right, it is the part that is lost.
 *
 * Worse, `rect.width` and `rect.height` are OUTPUTS for three of the four types a caller here can
 * append. Each writes them back from its own artwork:
 *
 *   SW_PIC       every draw:  rect.w/h := the bitmap's own size
 *   SW_CHECKBOX  every draw:  rect.w/h := chkbxoff.bmp, 34x34
 *   SW_SLIDER    every RESET: rect.w/h := slgauge.bmp, 250x50, and RESET is broadcast to every
 *                             widget each time a screen is opened
 *
 * `rect.x` and `rect.y` are never rewritten for any type, so POSITION is the only part of a
 * rectangle a caller really controls. The width and height passed to the append functions below
 * are kept in the signatures because a caller still has to declare what it INTENDED, but for
 * those three types the engine discards them, and a compile-time check written against them is
 * checking a number that never reaches the screen.
 *
 * What a check box's rectangle does buy: its clickable area is the 34x34 box alone, the caption
 * is NOT clickable, and its caption is drawn at rect.x + 34 + 4, always exactly 200 pixels wide,
 * taking only its HEIGHT from the rectangle supplied.
 *
 * ==============================================================================================
 * ONE TRAP, and it is why a label must carry parameter = -1
 *
 * swmenu_build walks the array once and, for every widget of type 0 or 1 (SW_PIC and SW_TEXT)
 * whose `parameter` is >= 0, replaces `link` with the result of a lookup and then clears
 * `parameter` to 0. The lookup resolves the parameter against `g_curMenu`. the screen that is
 * Open at build time, which is the PARENT screen, not the one being built. So the widget is linked
 * to something on a different screen entirely, chosen by whichever screen the player happened to
 * arrive from.
 *
 * What that then does depends on the type, and both are bad:
 *
 *   SW_TEXT  `link` IS the alignment. A pointer where an alignment belongs falls into the text
 *            draw's `default:` case with no positioning at all.
 *   SW_PIC   a non-NULL `link` makes the picture copy that other widget's `state` into its own
 *            EVERY FRAME, and a picture's state is its bitmap index, so the plate would change
 *            which bitmap it draws according to a check box on another screen.
 *
 * All 216 authored SW_TYPE_TEXT widgets in the retail data carry -1. The append functions below
 * write -1 themselves for both types and do not offer the choice.
 */
#ifndef COMMON_MENU_PATCHER_H
#define COMMON_MENU_PATCHER_H

#include "engine_types.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct menu_patch_context {
    sw_widget_t *widgets;                /* the caller's buffer, which becomes the live array   */
    size_t       capacity;               /* entries in that buffer, terminator included         */
    size_t       original_count;         /* authored widgets copied in by menu_patcher_begin    */
    size_t       current_count;          /* authored + appended, terminator NOT included        */
    uintptr_t    source_table_address;
    uintptr_t    table_pointer_address;  /* the `push imm32` operand we will repoint            */

    /* How many bitmap names the screen's own table holds, counted to its -1 terminator, or 0 when
     * the caller did not supply the table. Every bitmap index appended below is checked against
     * it, see menu_patcher_begin. */
    size_t       bitmap_name_count;

    bool         committed;
} menu_patch_context_t;

/* Validates the source array and copies it into `target_buffer`. Fails, writing nothing, when
 * the table is unreadable, has an implausible widget type, has no terminator within its capacity,
 * or would not leave room for at least one append plus the terminator.
 *
 * `bitmap_name_table_address` is the screen's own bitmap-name table, the third `push imm32` the
 * caller already reads out of the screen's prologue to find the widget table. It is walked to its
 * `-1` terminator (stride 8) and the count is kept, because the engine does not bound-CHECK A
 * BITMAP INDEX: its lookup rejects a negative index and nothing else, so an index one past the end
 * of this table is an unchecked read whose result is handed to a file loader. Pass 0 to say "not
 * known", which disables the check and is reported once. */
bool menu_patcher_begin(menu_patch_context_t *context,
                        uintptr_t             source_table_address,
                        uintptr_t             table_pointer_address,
                        uintptr_t             bitmap_name_table_address,
                        sw_widget_t          *target_buffer,
                        size_t                target_capacity);

/* True when the screen already uses that id, the caller must pick another rather than shadow an
 * authored widget. */
bool menu_patcher_has_widget_id(const menu_patch_context_t *context, int32_t widget_id);

/* `notch_count` must be >= 2: swslider divides by start - 1.
 * `knob_bitmap` and `gauge_bitmap` are indices into the SCREEN's own bitmap-name table and are
 * bounds-checked against it. `width`/`height` are overwritten from slgauge.bmp on every screen
 * open, 250x50 in the retail data, so they declare intent and nothing more. */
bool menu_patcher_append_slider(menu_patch_context_t *context,
                                int32_t widget_id, int32_t notch_count,
                                int32_t x, int32_t y, int32_t width, int32_t height,
                                int32_t knob_bitmap, int32_t gauge_bitmap,
                                size_t *out_widget_index);

/* A check box is the only widget that can never close a screen: swchkbox_activate toggles `state`,
 * beeps and returns -1, and every screen loop runs `while (result < 0)`. A selectable text widget
 * returns its own id instead and would end the screen. That is the reason a feature toggle is a
 * check box and not a label.
 *
 * `unchecked_bitmap` is an index into the SCREEN's own bitmap-name table, and the checked frame is
 * the NEXT index, swchkbox draws `parameter + state`, so the two frames must be adjacent. BOTH
 * are bounds-checked, because the state can reach 1 and the pair is what the table has to hold.
 *
 * `width` is discarded: the box is 34x34 from its own bitmap and the caption beside it is forced to
 * 200 wide. `height` reaches the caption box and nothing else.
 *
 * `label_string_id` goes to swmenu_getString, which indexes the localised table WITHOUT a bounds
 * check; it reads a pointer at [table + id*8 + 4] and copies from it when it is non-NULL. Only
 * ids the game really has may be passed; an invented one is an unchecked read. A feature that wants
 * its own wording has to answer that id itself, in front of the table. */
bool menu_patcher_append_checkbox(menu_patch_context_t *context,
                                  int32_t widget_id, int32_t label_string_id,
                                  int32_t x, int32_t y, int32_t width, int32_t height,
                                  int32_t font_index, int32_t unchecked_bitmap,
                                  int32_t initial_state,
                                  size_t *out_widget_index);

/* A backdrop plate. `bitmap_index` indexes the SCREEN's own bitmap-name table and is bounds-checked
 * against it.
 *
 * The bitmap is drawn whole, at (x,y), with no scale and no crop, see the header comment. The
 * width and height given here are recorded as intent and are then overwritten by the engine from
 * the bitmap's own size on the first draw. A plate is only usable when its ARTWORK is already the
 * size and shape the layout wants.
 *
 * Append it before the widgets it sits behind. The engine draws a screen in table order, so a
 * plate appended after them covers them instead of backing them. */
bool menu_patcher_append_pic(menu_patch_context_t *context,
                             int32_t widget_id, int32_t bitmap_index,
                             int32_t x, int32_t y, int32_t width, int32_t height,
                             size_t *out_widget_index);

/* `label` must outlive the process: the engine reads it every frame through widget->data and
 * never copies it. */
bool menu_patcher_append_label(menu_patch_context_t *context,
                               int32_t widget_id,
                               int32_t x, int32_t y, int32_t width, int32_t height,
                               int32_t font_index, char *label,
                               size_t *out_widget_index);

/* Writes the terminator and repoints the screen's table pointer at our buffer. Until this
 * returns true the engine has not seen a single byte of our array. */
bool menu_patcher_commit(menu_patch_context_t *context);

#endif /* COMMON_MENU_PATCHER_H */
