#include "menu_patcher.h"

#include "engine_types.h"
#include "logging.h"
#include "memory.h"
#include "patch.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Room for the terminator plus at least one appended widget, or beginning is pointless. */
#define MENU_PATCHER_MIN_HEADROOM 2u

/* A screen's bitmap-name table is an array of 8-byte records terminated by a first dword of -1.
 * The stride and the terminator are read out of the loop swmenu_build itself uses to count them:
 *
 *   8B 45 0C          mov eax,[pBmpNames]
 *   83 3C D0 FF       cmp dword [eax+edx*8], -1        <- stride 8, terminator -1
 *   74 02 / EB E9     until it hits one
 *
 * The count matters because the engine's own lookup checks only that an index is not negative,
 * `cmp [index],0 / jge` and nothing else, so one index too far is an unchecked read that ends in
 * a file loader. This is the same class of defect as the unbounded string-table index, which is
 * already guarded a layer up. */
#define BITMAP_NAME_STRIDE 8u
#define MAX_BITMAP_NAMES   256u

static bool widget_type_is_plausible(int32_t type)
{
    return type >= SW_TYPE_PIC && type <= SW_TYPE_SCROLLPIC;
}

/* Returns the number of names before the terminator, or 0 when the table cannot be trusted. Zero
 * is also what a caller passes to say "not known", and both mean the same thing downstream: the
 * bound is unavailable and every index is refused rather than guessed at. */
static size_t count_bitmap_names(uintptr_t table_address)
{
    size_t index;

    if (table_address == 0) {
        return 0;
    }

    for (index = 0; index < MAX_BITMAP_NAMES; ++index) {
        uintptr_t record = table_address + (uintptr_t)index * BITMAP_NAME_STRIDE;
        int32_t   first;

        if (!memory_is_readable_range(record, BITMAP_NAME_STRIDE)) {
            log_error("menu: the bitmap-name table at %08X is not readable at entry %u",
                      (unsigned)table_address, (unsigned)index);
            return 0;
        }
        first = *(const int32_t *)record;
        if (first == -1) {
            return index;
        }
    }

    log_error("menu: the bitmap-name table at %08X has no terminator within %u entries",
              (unsigned)table_address, (unsigned)MAX_BITMAP_NAMES);
    return 0;
}

/* Every bitmap index an append writes goes through here first. `highest` is the largest index the
 * widget can ever ASK for, which is not always the one supplied: a check box draws
 * `parameter + state` and its state reaches 1, so the frame after the one named has to exist too. */
static bool bitmap_index_is_usable(const menu_patch_context_t *context, int32_t index,
                                   int32_t highest, const char *what)
{
    if (index < 0) {
        log_error("menu: %s bitmap index %d is negative, the engine draws nothing for it",
                  what, (int)index);
        return false;
    }
    if (context->bitmap_name_count == 0) {
        log_error("menu: the screen's bitmap-name table was not supplied or could not be counted, "
                  "so %s bitmap index %d cannot be bounds-checked. The engine's own lookup only "
                  "rejects a negative index, so an index past the table is an unchecked read, the "
                  "widget is refused rather than guessed at.", what, (int)index);
        return false;
    }
    if ((size_t)highest >= context->bitmap_name_count) {
        log_error("menu: %s needs bitmap index %d but this screen's table holds only %u names - "
                  "refused, because the engine would read past it without noticing",
                  what, (int)highest, (unsigned)context->bitmap_name_count);
        return false;
    }
    return true;
}

static bool copy_source_table(menu_patch_context_t *context)
{
    const sw_widget_t *source = (const sw_widget_t *)context->source_table_address;
    size_t             limit  = context->capacity - MENU_PATCHER_MIN_HEADROOM;
    size_t             index;

    for (index = 0; index < limit; ++index) {
        if (!memory_is_readable_range((uintptr_t)&source[index], sizeof(sw_widget_t))) {
            log_error("menu: widget %u at %08X is not readable, refused",
                      (unsigned)index, (unsigned)(uintptr_t)&source[index]);
            return false;
        }
        if (source[index].type == SW_TYPE_TERMINATOR) {
            context->original_count = index;
            context->current_count  = index;
            return true;
        }
        if (!widget_type_is_plausible(source[index].type)) {
            log_error("menu: widget %u has type %d, this is not a swift widget table, refused",
                      (unsigned)index, (int)source[index].type);
            return false;
        }
        context->widgets[index] = source[index];
    }

    log_error("menu: no terminator within %u entries, refused", (unsigned)limit);
    return false;
}

bool menu_patcher_begin(menu_patch_context_t *context,
                        uintptr_t             source_table_address,
                        uintptr_t             table_pointer_address,
                        uintptr_t             bitmap_name_table_address,
                        sw_widget_t          *target_buffer,
                        size_t                target_capacity)
{
    if (context == NULL || target_buffer == NULL) {
        return false;
    }
    if (target_capacity < MENU_PATCHER_MIN_HEADROOM + 1u) {
        log_error("menu: the target buffer holds %u entries, which leaves no room to append",
                  (unsigned)target_capacity);
        return false;
    }
    if (source_table_address == 0 || !memory_is_inside_image(source_table_address,
                                                            sizeof(sw_widget_t))) {
        log_error("menu: the widget table pointer %08X is outside the image, refused",
                  (unsigned)source_table_address);
        return false;
    }

    memset(context, 0, sizeof(*context));
    context->widgets               = target_buffer;
    context->capacity              = target_capacity;
    context->source_table_address  = source_table_address;
    context->table_pointer_address = table_pointer_address;
    context->bitmap_name_count     = count_bitmap_names(bitmap_name_table_address);

    memset(target_buffer, 0, target_capacity * sizeof(sw_widget_t));

    if (context->bitmap_name_count == 0) {
        log_warning("menu: this screen's bitmap-name table is unknown, so no widget that names a "
                    "bitmap can be appended to it. Sliders, check boxes and plates all do; labels "
                    "do not and are unaffected.");
    } else {
        log_info("menu: the screen's bitmap-name table at %08X holds %u names, so a bitmap index "
                 "is bounded by %u",
                 (unsigned)bitmap_name_table_address, (unsigned)context->bitmap_name_count,
                 (unsigned)(context->bitmap_name_count - 1u));
    }

    return copy_source_table(context);
}

bool menu_patcher_has_widget_id(const menu_patch_context_t *context, int32_t widget_id)
{
    size_t index;

    if (context == NULL) {
        return false;
    }
    for (index = 0; index < context->current_count; ++index) {
        if (context->widgets[index].id == widget_id) {
            return true;
        }
    }
    return false;
}

/* Shared front half of both append functions: capacity, duplicate id, and the zeroed slot. */
static sw_widget_t *reserve_slot(menu_patch_context_t *context, int32_t widget_id,
                                 size_t *out_widget_index)
{
    sw_widget_t *slot;

    if (context == NULL || context->committed) {
        return NULL;
    }
    if (context->current_count + MENU_PATCHER_MIN_HEADROOM > context->capacity) {
        log_error("menu: no room for widget id %d (%u of %u entries used)",
                  (int)widget_id, (unsigned)context->current_count, (unsigned)context->capacity);
        return NULL;
    }
    if (menu_patcher_has_widget_id(context, widget_id)) {
        log_error("menu: widget id %d is already taken by this screen, refused", (int)widget_id);
        return NULL;
    }

    slot = &context->widgets[context->current_count];
    memset(slot, 0, sizeof(*slot));
    slot->id = widget_id;

    if (out_widget_index != NULL) {
        *out_widget_index = context->current_count;
    }
    ++context->current_count;

    return slot;
}

bool menu_patcher_append_slider(menu_patch_context_t *context,
                                int32_t widget_id, int32_t notch_count,
                                int32_t x, int32_t y, int32_t width, int32_t height,
                                int32_t knob_bitmap, int32_t gauge_bitmap,
                                size_t *out_widget_index)
{
    sw_widget_t *slot;

    if (notch_count < 2) {
        log_error("menu: a slider needs at least 2 notches (swslider divides by start - 1)");
        return false;
    }
    if (context == NULL ||
        !bitmap_index_is_usable(context, knob_bitmap, knob_bitmap, "a slider's knob") ||
        !bitmap_index_is_usable(context, gauge_bitmap, gauge_bitmap, "a slider's gauge")) {
        return false;
    }

    slot = reserve_slot(context, widget_id, out_widget_index);
    if (slot == NULL) {
        return false;
    }

    slot->type       = SW_TYPE_SLIDER;
    /* NOT decoration and NOT merely "select": the hit test and the focus walk both refuse any
     * widget whose action IS SW_ACTION_STATIC (-29), and both additionally require visible == 1
     * compared against that literal. Any non-static action would do; SELECT is the authored one. */
    slot->action     = SW_ACTION_SELECT;
    slot->visible    = 1;                  /* 1, not "non-zero": see above */
    slot->start      = notch_count;
    slot->state      = 0;
    slot->font_index = knob_bitmap;        /* SLIDER: a bitmap index, not a font */
    slot->parameter  = gauge_bitmap;
    slot->rect.x     = x;
    slot->rect.y     = y;
    /* DECLARED INTENT ONLY. Every screen open broadcasts a RESET, and the slider answers it by
     * overwriting both of these from the gauge bitmap, 250x50 in the retail data. What the
     * layout really controls is x and y. */
    slot->rect.width  = width;
    slot->rect.height = height;

    return true;
}

bool menu_patcher_append_checkbox(menu_patch_context_t *context,
                                  int32_t widget_id, int32_t label_string_id,
                                  int32_t x, int32_t y, int32_t width, int32_t height,
                                  int32_t font_index, int32_t unchecked_bitmap,
                                  int32_t initial_state,
                                  size_t *out_widget_index)
{
    sw_widget_t *slot;

    if (label_string_id < 0) {
        log_error("menu: a check box needs a real string id");
        return false;
    }
    if (initial_state != 0 && initial_state != 1) {
        return false;
    }
    /* BOTH FRAMES, not just the one named. The box draws `parameter + state` and the state reaches
     * 1 the first time it is ticked, so a table that ends on the unchecked frame would read past
     * itself the moment the player clicked. */
    if (context == NULL ||
        !bitmap_index_is_usable(context, unchecked_bitmap, unchecked_bitmap + 1,
                                "a check box's two frames")) {
        return false;
    }

    slot = reserve_slot(context, widget_id, out_widget_index);
    if (slot == NULL) {
        return false;
    }

    slot->type       = SW_TYPE_CHECKBOX;
    /* See the slider: the predicate is `action != SW_ACTION_STATIC` plus `visible == 1`. */
    slot->action     = SW_ACTION_SELECT;
    slot->visible    = 1;
    slot->start      = label_string_id;
    slot->state      = initial_state;      /* CHECKBOX: the state IS the bitmap offset */
    slot->font_index = font_index;
    slot->parameter  = unchecked_bitmap;   /* drawn as parameter + state */
    slot->rect.x     = x;
    slot->rect.y     = y;
    /* Of these two, only `height` IS READ, and only by the caption box beside the tick. The box
     * itself is 34x34 from its own bitmap and rewrites `width` from it on every draw, and the
     * caption's own width is forced to 200. So the clickable area is the 34x34 box alone: the
     * words next to it are not a hit target however wide this says they are. */
    slot->rect.width  = width;
    slot->rect.height = height;

    return true;
}

bool menu_patcher_append_label(menu_patch_context_t *context,
                               int32_t widget_id,
                               int32_t x, int32_t y, int32_t width, int32_t height,
                               int32_t font_index, char *label,
                               size_t *out_widget_index)
{
    sw_widget_t *slot;

    if (label == NULL) {
        return false;
    }

    slot = reserve_slot(context, widget_id, out_widget_index);
    if (slot == NULL) {
        return false;
    }

    slot->type       = SW_TYPE_TEXT;
    slot->action     = SW_ACTION_STATIC;   /* a caption: never focused, never hit-tested */
    slot->visible    = 1;                  /* the draw loop tests == 1; 2 would be invisible */
    /* A label's string id is `start + state`, the same sum a picture's bitmap index is. Both are
     * zero here because `data` below overrides the string table outright, so neither is ever used,
     * but leaving `start` unwritten would make that dependence invisible. */
    slot->start      = 0;
    slot->state      = 0;                  /* must be >= 0 or swtext_drawWidget returns early */
    slot->font_index = font_index;
    slot->parameter  = -1;                 /* >= 0 would make swmenu_build eat the alignment */
    slot->rect.x     = x;
    slot->rect.y     = y;
    slot->rect.width  = width;
    slot->rect.height = height;
    slot->link       = NULL;               /* SW_TYPE_TEXT: link is the ALIGNMENT, 0 = centred */
    slot->data       = label;              /* overrides the localised string table */

    return true;
}

bool menu_patcher_append_pic(menu_patch_context_t *context,
                             int32_t widget_id, int32_t bitmap_index,
                             int32_t x, int32_t y, int32_t width, int32_t height,
                             size_t *out_widget_index)
{
    sw_widget_t *slot;

    if (context == NULL ||
        !bitmap_index_is_usable(context, bitmap_index, bitmap_index, "a picture's")) {
        return false;
    }

    slot = reserve_slot(context, widget_id, out_widget_index);
    if (slot == NULL) {
        return false;
    }

    slot->type       = SW_TYPE_PIC;
    slot->action     = SW_ACTION_STATIC;   /* a backdrop: never focused, never hit-tested */
    slot->visible    = 1;
    /* The bitmap index a picture draws is `start + state`, not `state` alone. Writing 0 into
     * `start` is what makes putting the index in `state` correct, so the two lines below are one
     * decision and must not be separated. Read out of all three authored pictures on the shipped
     * controls screen, whose `start` is 0 and whose `state` fields are 11, 7 and 17,
     * splashol.bmp, controls.bmp and popup.bmp, in the order that table lists them. */
    slot->start      = 0;
    slot->state      = bitmap_index;
    slot->font_index = 1;                  /* both authored plates on that screen carry 1 */
    /* -1 keeps swmenu_build from resolving this into `link`, and for a picture a non-NULL link is
     * worse than a wrong alignment: the picture then copies the linked widget's state into its own
     * every frame, which for a picture IS its bitmap index. */
    slot->parameter  = -1;
    slot->rect.x     = x;
    slot->rect.y     = y;
    /* Overwritten from the bitmap's own size on the first draw. The bitmap is drawn WHOLE at (x,y):
     * there is no scale and no crop anywhere in this toolkit. */
    slot->rect.width  = width;
    slot->rect.height = height;
    slot->link       = NULL;
    slot->data       = NULL;

    return true;
}

bool menu_patcher_commit(menu_patch_context_t *context)
{
    sw_widget_t *terminator;

    if (context == NULL || context->committed) {
        return false;
    }
    if (context->current_count == context->original_count) {
        log_warning("menu: nothing was appended, the screen is left exactly as it shipped");
        return false;
    }
    if (context->current_count >= context->capacity) {
        return false;
    }

    terminator = &context->widgets[context->current_count];
    memset(terminator, 0, sizeof(*terminator));
    terminator->type   = SW_TYPE_TERMINATOR;
    terminator->action = SW_ACTION_STATIC;  /* build writes this itself; pre-set for clarity */

    if (patch_write_pointer32(context->table_pointer_address, context->widgets)
        != PATCH_RESULT_OK) {
        log_error("menu: could not repoint the widget table at %08X - the screen keeps its own "
                  "%u widgets and nothing we built is reachable",
                  (unsigned)context->table_pointer_address, (unsigned)context->original_count);
        return false;
    }

    context->committed = true;
    log_info("menu: widget table [%08X] (%u authored) copied to %08X and extended to %u entries",
             (unsigned)context->source_table_address, (unsigned)context->original_count,
             (unsigned)(uintptr_t)context->widgets, (unsigned)context->current_count);
    return true;
}
