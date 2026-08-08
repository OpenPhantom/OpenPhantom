/* menu_patcher.c: the shared widget-table patcher, exercised without the game.
 *
 * The source tables below are STATIC, which is what makes this testable at all:
 * menu_patcher_begin() insists that the source table lies inside the host image, and a static
 * array in this executable does. A heap table would be refused, correctly, because in the game
 * a widget table that is not in the image is not the engine's table.
 */
#include "unittest.h"

#include "common/engine_types.h"
#include "common/host_image.h"
#include "common/menu_patcher.h"

#include <string.h>

#define TARGET_CAPACITY 16

/* A miniature but shape-correct source table: two widgets and the terminator. */
static sw_widget_t good_source[3] = {
    { SW_TYPE_SLIDER, 0x5E, SW_ACTION_SELECT, 1, 11, 5, 3, 2, { 325, 250, 250, 50 }, NULL, NULL },
    { SW_TYPE_TEXT,   0x60, SW_ACTION_SELECT, 1,  0, 0, 1, -1, { 400, 400, 100, 30 }, NULL, NULL },
    { SW_TYPE_TERMINATOR, 0, SW_ACTION_STATIC, 0, 0, 0, 0, 0, { 0, 0, 0, 0 }, NULL, NULL }
};

/* Same, but with an implausible widget type in the middle. */
static sw_widget_t bad_type_source[3] = {
    { SW_TYPE_SLIDER, 0x5E, SW_ACTION_SELECT, 1, 11, 5, 3, 2, { 0, 0, 0, 0 }, NULL, NULL },
    { 99,             0x61, SW_ACTION_SELECT, 1,  0, 0, 0, -1, { 0, 0, 0, 0 }, NULL, NULL },
    { SW_TYPE_TERMINATOR, 0, SW_ACTION_STATIC, 0, 0, 0, 0, 0, { 0, 0, 0, 0 }, NULL, NULL }
};

/* No terminator anywhere in reach. */
static sw_widget_t unterminated_source[8] = {
    { SW_TYPE_PIC, 1, SW_ACTION_STATIC, 1, 0, 0, 0, -1, { 0, 0, 0, 0 }, NULL, NULL },
    { SW_TYPE_PIC, 2, SW_ACTION_STATIC, 1, 0, 0, 0, -1, { 0, 0, 0, 0 }, NULL, NULL },
    { SW_TYPE_PIC, 3, SW_ACTION_STATIC, 1, 0, 0, 0, -1, { 0, 0, 0, 0 }, NULL, NULL },
    { SW_TYPE_PIC, 4, SW_ACTION_STATIC, 1, 0, 0, 0, -1, { 0, 0, 0, 0 }, NULL, NULL },
    { SW_TYPE_PIC, 5, SW_ACTION_STATIC, 1, 0, 0, 0, -1, { 0, 0, 0, 0 }, NULL, NULL },
    { SW_TYPE_PIC, 6, SW_ACTION_STATIC, 1, 0, 0, 0, -1, { 0, 0, 0, 0 }, NULL, NULL },
    { SW_TYPE_PIC, 7, SW_ACTION_STATIC, 1, 0, 0, 0, -1, { 0, 0, 0, 0 }, NULL, NULL },
    { SW_TYPE_PIC, 8, SW_ACTION_STATIC, 1, 0, 0, 0, -1, { 0, 0, 0, 0 }, NULL, NULL }
};

/* The controls screen exactly as it ships, read out of the retail image: eight authored widgets
 * and the terminator, with their real ids and rects. It is here rather than paraphrased because
 * the two check boxes enhanced_input appends are placed against these rects and must not shadow these
 * ids, and both of those are claims about THIS table.
 *
 * Widget ids in use: 50, 0, 1, 2, 3, 4, 5, 6. Neither 0x70 nor 0x71 is among them.
 */
static sw_widget_t controls_source[9] = {
    { SW_TYPE_PIC,  50, SW_ACTION_STATIC, 1, 0, 11, 0, -1, {   0,   0, 640, 480 }, NULL, NULL },
    { SW_TYPE_PIC,   0, SW_ACTION_STATIC, 1, 0,  7, 1, -1, {   0,   0, 640, 480 }, NULL, NULL },
    { SW_TYPE_TEXT,  1, SW_ACTION_STATIC, 1, 0,  9, 0, -1, { 382,  19, 235,  67 }, NULL, NULL },
    { SW_TYPE_TEXT,  2, SW_ACTION_SELECT, 1, 0, 20, 1, -1, {   7, 175, 199,  53 }, NULL, NULL },
    { SW_TYPE_TEXT,  3, SW_ACTION_SELECT, 1, 0, 21, 1, -1, {   7, 277, 199,  53 }, NULL, NULL },
    { SW_TYPE_TEXT,  4, SW_ACTION_CANCEL, 1, 0,  4, 0, -1, { 484, 400, 106,  67 }, NULL, NULL },
    { SW_TYPE_PIC,   5, SW_ACTION_STATIC, 0, 0, 17, 1, -1, { 170, 140, 300, 200 }, NULL, NULL },
    { SW_TYPE_TEXT,  6, SW_ACTION_SELECT, 0, 0, 22, 0, -1, { 170, 140, 300, 200 }, NULL, NULL },
    { SW_TYPE_TERMINATOR, 0, SW_ACTION_STATIC, 0, 0, 0, 0, 0, { 0, 0, 0, 0 }, NULL, NULL }
};

/* A screen's bitmap-name table, laid out as the engine lays it out: 8-byte records ended by a
 * first dword of -1. Eighteen names, which is what the shared options table [0x4AEE10] carries, so
 * the valid index range here is 0..17 exactly as it is in the game. Static, so the patcher can
 * read it, and the content of each record does not matter, only where the terminator is. */
#define TEST_BITMAP_NAME_COUNT 18
static int32_t bitmap_names[(TEST_BITMAP_NAME_COUNT + 1) * 2] = {
    1, 0,  1, 0,  1, 0,  1, 0,  1, 0,  1, 0,
    1, 0,  1, 0,  1, 0,  1, 0,  1, 0,  1, 0,
    1, 0,  1, 0,  1, 0,  1, 0,  1, 0,  1, 0,
    -1, 0
};

/* The `push imm32` operand the commit repoints. Static, so it is writable image memory. */
static uint32_t table_pointer_cell;

static char caption[32] = "FIELD OF VIEW";

static void test_begin_copies_and_counts(void)
{
    menu_patch_context_t context;
    sw_widget_t          target[TARGET_CAPACITY];

    ut_check(menu_patcher_begin(&context, (uintptr_t)good_source, (uintptr_t)&table_pointer_cell,
                             (uintptr_t)bitmap_names, target, TARGET_CAPACITY),
          "begin accepts a well-formed table");
    ut_check(context.original_count == 2, "begin counts the authored widgets");
    ut_check(context.current_count == 2, "begin starts with nothing appended");
    ut_check(target[0].id == 0x5E && target[0].type == SW_TYPE_SLIDER,
          "begin copies the first widget verbatim");
    ut_check(target[1].id == 0x60 && target[1].type == SW_TYPE_TEXT,
          "begin copies the second widget verbatim");
    ut_check(!context.committed, "begin does not commit");
}

static void test_begin_refusals(void)
{
    menu_patch_context_t context;
    sw_widget_t          target[TARGET_CAPACITY];

    ut_check(!menu_patcher_begin(&context, (uintptr_t)bad_type_source,
                              (uintptr_t)&table_pointer_cell,
                              (uintptr_t)bitmap_names, target, TARGET_CAPACITY),
          "begin refuses an implausible widget type");

    ut_check(!menu_patcher_begin(&context, (uintptr_t)unterminated_source,
                              (uintptr_t)&table_pointer_cell,
                              (uintptr_t)bitmap_names, target, 4),
          "begin refuses a table with no terminator in reach");

    ut_check(!menu_patcher_begin(&context, 0, (uintptr_t)&table_pointer_cell,
                              (uintptr_t)bitmap_names, target, TARGET_CAPACITY),
          "begin refuses a null source table");

    /* A heap or stack table is outside the image and must be refused. */
    {
        sw_widget_t stack_source[1];
        memset(stack_source, 0, sizeof(stack_source));
        stack_source[0].type = SW_TYPE_TERMINATOR;
        ut_check(!menu_patcher_begin(&context, (uintptr_t)stack_source,
                                  (uintptr_t)&table_pointer_cell,
                              (uintptr_t)bitmap_names, target, TARGET_CAPACITY),
              "begin refuses a source table outside the image");
    }

    ut_check(!menu_patcher_begin(&context, (uintptr_t)good_source, (uintptr_t)&table_pointer_cell,
                              (uintptr_t)bitmap_names, target, 2),
          "begin refuses a target with no room to append");
}

static void test_append_and_commit(void)
{
    menu_patch_context_t context;
    sw_widget_t          target[TARGET_CAPACITY];
    size_t               slider_index = 0;
    size_t               label_index = 0;

    if (!menu_patcher_begin(&context, (uintptr_t)good_source, (uintptr_t)&table_pointer_cell,
                            (uintptr_t)bitmap_names, target, TARGET_CAPACITY)) {
        ut_check(0, "begin succeeded (prerequisite for the append tests)");
        return;
    }

    ut_check(menu_patcher_has_widget_id(&context, 0x5E), "an authored id is found");
    ut_check(!menu_patcher_has_widget_id(&context, 0x70), "an unused id is not found");

    ut_check(!menu_patcher_append_slider(&context, 0x70, 1, 25, 250, 250, 50, 3, 2, &slider_index),
          "a slider with one notch is refused (swslider divides by start - 1)");

    ut_check(menu_patcher_append_slider(&context, 0x70, 21, 25, 250, 250, 50, 3, 2, &slider_index),
          "a well-formed slider is appended");
    ut_check(slider_index == 2, "the slider lands behind the authored widgets");
    ut_check(target[2].type == SW_TYPE_SLIDER, "the appended slider has the slider type");
    ut_check(target[2].start == 21, "the notch count lands in `start`");
    ut_check(target[2].font_index == 3, "the knob bitmap lands in `font_index`");
    ut_check(target[2].parameter == 2, "the gauge bitmap lands in `parameter`");
    ut_check(target[2].action == SW_ACTION_SELECT, "the slider is selectable");
    ut_check(target[2].visible == 1, "the slider is visible");

    ut_check(!menu_patcher_append_slider(&context, 0x70, 21, 0, 0, 0, 0, 3, 2, NULL),
          "a duplicate widget id is refused");
    ut_check(!menu_patcher_append_slider(&context, 0x5E, 21, 0, 0, 0, 0, 3, 2, NULL),
          "shadowing an authored id is refused");

    ut_check(menu_patcher_append_label(&context, 0x71, 25, 285, 250, 50, 1, caption, &label_index),
          "a label is appended");
    ut_check(target[3].type == SW_TYPE_TEXT, "the label has the text type");
    ut_check(target[3].parameter == -1,
          "the label carries parameter -1, or swmenu_build would eat its alignment");
    ut_check(target[3].action == SW_ACTION_STATIC, "the label is static");
    ut_check(target[3].state == 0, "the label state is not negative");
    ut_check(target[3].data == caption, "the label points at the caller's buffer");

    ut_check(!menu_patcher_append_label(&context, 0x72, 0, 0, 0, 0, 1, NULL, NULL),
          "a label with no text is refused");

    table_pointer_cell = 0;
    ut_check(menu_patcher_commit(&context), "commit succeeds");
    ut_check(context.committed, "commit marks the context");
    ut_check(target[4].type == SW_TYPE_TERMINATOR, "commit writes the terminator behind the last "
                                                "widget");
    ut_check(table_pointer_cell == (uint32_t)(uintptr_t)target,
          "commit repoints the table pointer at our buffer");

    ut_check(!menu_patcher_commit(&context), "a second commit is refused");
    ut_check(!menu_patcher_append_slider(&context, 0x73, 5, 0, 0, 0, 0, 3, 2, NULL),
          "appending after the commit is refused");
}

/* The whole group enhanced_input appends to the controls screen: two check boxes, a slider and its
 * caption, from ONE patcher and ONE copy. What this really covers is that each widget's value is
 * read back through its OWN recorded index, a shared or stale one would read the wrong setting
 * back with no symptom until it was clicked, and that the layout invariants hold against the
 * shipped table rather than only in prose.
 *
 * There is no plate, and that is the point of this version. The previous one appended one and then
 * checked that every widget of the group sat inside the plate's RECTANGLE. That check passed while
 * the screen was wrong, because a picture's rectangle is not where its bitmap goes: the blit takes
 * x and y and no size at all, so the plate drew its full 640x480 from the rectangle's corner and
 * the rectangle bounded nothing. The layout checks below are therefore written against the DRAWN
 * footprints, the gauge bitmap's 250x50, the check box's 34x34 plus a caption forced to 200
 * wide, and against the region of the background art that is actually empty. */
#define CONTROLS_FREE_X       281
#define CONTROLS_FREE_Y        86
#define CONTROLS_FREE_RIGHT   639
#define CONTROLS_FREE_BOTTOM  398
#define GAUGE_W               250
#define GAUGE_H                50
#define BOX_SIZE               34
#define BOX_LABEL_GAP           4
#define BOX_LABEL_W           200

static void test_append_controls_group(void)
{
    menu_patch_context_t context;
    sw_widget_t          target[TARGET_CAPACITY];
    size_t               strafe_index = 0;
    size_t               free_look_index = 0;
    size_t               slider_index = 0;
    size_t               label_index = 0;
    /* A label's text is read by the engine every frame through widget->data and never copied, so
     * the buffer has to outlive the append. */
    static char          slider_caption[] = "MOUSE SPEED 30";

    if (!menu_patcher_begin(&context, (uintptr_t)controls_source,
                            (uintptr_t)&table_pointer_cell, (uintptr_t)bitmap_names,
                            target, TARGET_CAPACITY)) {
        ut_check(0, "begin accepts the shipped controls table (prerequisite)");
        return;
    }
    ut_check(context.original_count == 8, "the shipped controls screen has eight authored widgets");
    ut_check(context.bitmap_name_count == TEST_BITMAP_NAME_COUNT,
          "begin counts the screen's bitmap names to its terminator");

    ut_check(!menu_patcher_has_widget_id(&context, 0x70),
          "widget id 0x70 is free on the shipped controls screen");
    ut_check(!menu_patcher_has_widget_id(&context, 0x71),
          "widget id 0x71 is free on the shipped controls screen");

    ut_check(menu_patcher_append_checkbox(&context, 0x70, 0x7655, 330, 192, 255, 50, 2, 4, 0,
                                       &strafe_index),
          "the first check box is appended");
    ut_check(menu_patcher_append_checkbox(&context, 0x71, 0x7656, 330, 246, 255, 50, 2, 4, 1,
                                       &free_look_index),
          "the second check box is appended behind it");
    ut_check(strafe_index == 8 && free_look_index == 9,
          "each box gets its own index, in append order, behind the authored widgets");

    ut_check(target[8].type == SW_TYPE_CHECKBOX && target[9].type == SW_TYPE_CHECKBOX,
          "both boxes carry the check-box type");
    ut_check(target[8].action == SW_ACTION_SELECT && target[9].action == SW_ACTION_SELECT,
          "both carry a non-static action, which the hit test requires");
    ut_check(target[8].visible == 1 && target[9].visible == 1,
          "both are visible == 1, which is what the draw loop and the hit test compare against");
    ut_check(target[8].start == 0x7655 && target[9].start == 0x7656,
          "the label string id lands in `start`, and the two ids are distinct");
    ut_check(target[8].state == 0 && target[9].state == 1,
          "the initial state is carried through, and the state is the bitmap offset");
    ut_check(target[8].font_index == 2 && target[9].font_index == 2,
          "both use font 2, as the authored check boxes do");
    ut_check(target[8].parameter == 4 && target[9].parameter == 4,
          "the unchecked bitmap index lands in `parameter`, and the checked frame is the next one");

    ut_check(!menu_patcher_append_checkbox(&context, 0x71, 0x7657, 0, 0, 0, 0, 2, 4, 0, NULL),
          "a second box reusing the first appended id is refused");
    ut_check(!menu_patcher_append_checkbox(&context, 4, 0x7657, 0, 0, 0, 0, 2, 4, 0, NULL),
          "a box shadowing the authored BACK id is refused");

    /* The slider and its caption, which the controls screen ships neither of. The two bitmap
     * indices are the ones the shared table [0x4AEE10] carries: 2 = slgauge.bmp, 3 = slslide.bmp. */
    ut_check(menu_patcher_append_slider(&context, 0x72, 100, 330, 96, 255, 50, 3, 2, &slider_index),
          "the mouse speed slider is appended");
    ut_check(menu_patcher_append_label(&context, 0x73, 330, 148, 250, 40, 1, slider_caption,
                                    &label_index),
          "its caption is appended behind it");
    ut_check(slider_index == 10 && label_index == 11,
          "each widget gets its own index, in append order, behind the authored ones");
    ut_check(target[10].type == SW_TYPE_SLIDER, "the slider carries the slider type");
    ut_check(target[11].type == SW_TYPE_TEXT, "and the caption is a text widget");
    ut_check(target[11].data == (void *)slider_caption,
          "the caption's text pointer is ours, and the engine reads it every frame");

    /* THE LAYOUT, checked against what the engine really draws rather than against the rectangles
     * it discards. These are the invariants input_menu.c asserts at compile time; repeating them
     * here catches a change made to that file's numbers without its assertions. */
    ut_check(target[10].rect.x >= CONTROLS_FREE_X &&
          target[10].rect.x + GAUGE_W - 1 <= CONTROLS_FREE_RIGHT,
          "the slider's 250-wide gauge stays inside the empty region");
    ut_check(target[8].rect.x + BOX_SIZE + BOX_LABEL_GAP + BOX_LABEL_W - 1 <= CONTROLS_FREE_RIGHT,
          "a check box plus its 200-wide forced caption stays inside the empty region");
    ut_check(target[10].rect.y >= CONTROLS_FREE_Y &&
          target[9].rect.y + BOX_SIZE - 1 <= CONTROLS_FREE_BOTTOM,
          "the group stays inside the empty region vertically");
    ut_check(target[11].rect.y >= target[10].rect.y + GAUGE_H,
          "the caption begins at or below where the drawn gauge ends");
    ut_check(target[9].rect.y >= target[8].rect.y + BOX_SIZE,
          "the two boxes do not overlap each other");
    ut_check(target[8].rect.x >= controls_source[3].rect.x + controls_source[3].rect.width,
          "the group is a column of its own, clear of the authored buttons");
    ut_check(target[9].rect.y + target[9].rect.height <= controls_source[5].rect.y,
          "and it ends above BACK's row, so the two cannot collide");

    table_pointer_cell = 0;
    ut_check(menu_patcher_commit(&context), "the four-widget screen commits");
    ut_check(target[12].type == SW_TYPE_TERMINATOR, "the terminator lands behind all four");
}

/* The bound the engine does not have. Its own lookup rejects a negative index and checks nothing
 * else, so an index past the screen's bitmap-name table is an unchecked read whose result reaches
 * a file loader. Every appender that names a bitmap has to refuse one. */
static void test_bitmap_index_bounds(void)
{
    menu_patch_context_t context;
    sw_widget_t          target[TARGET_CAPACITY];
    static char          text[] = "CAPTION";

    if (!menu_patcher_begin(&context, (uintptr_t)good_source, (uintptr_t)&table_pointer_cell,
                            (uintptr_t)bitmap_names, target, TARGET_CAPACITY)) {
        ut_check(0, "begin succeeded (prerequisite)");
        return;
    }
    ut_check(context.bitmap_name_count == TEST_BITMAP_NAME_COUNT,
          "the bitmap-name table is counted to its -1 terminator, stride 8");

    ut_check(menu_patcher_append_pic(&context, 0x90, TEST_BITMAP_NAME_COUNT - 1, 0, 0, 10, 10, NULL),
          "the last valid bitmap index is accepted");
    ut_check(!menu_patcher_append_pic(&context, 0x91, TEST_BITMAP_NAME_COUNT, 0, 0, 10, 10, NULL),
          "one index past the table is refused rather than read");
    ut_check(!menu_patcher_append_pic(&context, 0x92, -1, 0, 0, 10, 10, NULL),
          "a negative bitmap index is refused");

    ut_check(!menu_patcher_append_slider(&context, 0x93, 5, 0, 0, 0, 0,
                                      TEST_BITMAP_NAME_COUNT, 2, NULL),
          "a slider whose knob is past the table is refused");
    ut_check(!menu_patcher_append_slider(&context, 0x94, 5, 0, 0, 0, 0,
                                      3, TEST_BITMAP_NAME_COUNT, NULL),
          "a slider whose gauge is past the table is refused");

    /* A check box draws `parameter + state` and its state reaches 1, so the LAST index is still
     * one too far: the frame it would need when ticked does not exist. */
    ut_check(!menu_patcher_append_checkbox(&context, 0x95, 100, 0, 0, 0, 0, 2,
                                        TEST_BITMAP_NAME_COUNT - 1, 0, NULL),
          "a check box on the last index is refused, because its checked frame is the next one");
    ut_check(menu_patcher_append_checkbox(&context, 0x96, 100, 0, 0, 0, 0, 2,
                                       TEST_BITMAP_NAME_COUNT - 2, 0, NULL),
          "and one index earlier is accepted, because both its frames exist");

    /* A label names no bitmap and is unaffected by any of this. */
    ut_check(menu_patcher_append_label(&context, 0x97, 0, 0, 10, 10, 1, text, NULL),
          "a label is appended whatever the bitmap table says, because it names no bitmap");
}

/* With no bitmap-name table the bound cannot be computed, and a widget that names a bitmap must be
 * refused rather than appended on trust. The label still goes in. */
static void test_bitmap_bound_unavailable(void)
{
    menu_patch_context_t context;
    sw_widget_t          target[TARGET_CAPACITY];
    static char          text[] = "CAPTION";

    if (!menu_patcher_begin(&context, (uintptr_t)good_source, (uintptr_t)&table_pointer_cell,
                            0, target, TARGET_CAPACITY)) {
        ut_check(0, "begin succeeds without a bitmap-name table (prerequisite)");
        return;
    }
    ut_check(context.bitmap_name_count == 0, "an absent bitmap table leaves the count at zero");
    ut_check(!menu_patcher_append_pic(&context, 0x90, 0, 0, 0, 10, 10, NULL),
          "with no bound available even index 0 is refused rather than trusted");
    ut_check(!menu_patcher_append_slider(&context, 0x91, 5, 0, 0, 0, 0, 3, 2, NULL),
          "and so is a slider");
    ut_check(menu_patcher_append_label(&context, 0x92, 0, 0, 10, 10, 1, text, NULL),
          "but a label, which names no bitmap, is still appended");
}

static void test_commit_without_append(void)
{
    menu_patch_context_t context;
    sw_widget_t          target[TARGET_CAPACITY];

    if (!menu_patcher_begin(&context, (uintptr_t)good_source, (uintptr_t)&table_pointer_cell,
                            (uintptr_t)bitmap_names, target, TARGET_CAPACITY)) {
        ut_check(0, "begin succeeded (prerequisite)");
        return;
    }
    ut_check(!menu_patcher_commit(&context),
          "committing without appending anything is refused, the screen is left as it shipped");
}

static void test_capacity_exhaustion(void)
{
    menu_patch_context_t context;
    sw_widget_t          target[5];               /* 2 authored + at most 2 appended + terminator */
    int                  appended = 0;

    if (!menu_patcher_begin(&context, (uintptr_t)good_source, (uintptr_t)&table_pointer_cell,
                            (uintptr_t)bitmap_names, target, 5)) {
        ut_check(0, "begin succeeded (prerequisite)");
        return;
    }

    appended += menu_patcher_append_slider(&context, 0x80, 5, 0, 0, 0, 0, 3, 2, NULL) ? 1 : 0;
    appended += menu_patcher_append_slider(&context, 0x81, 5, 0, 0, 0, 0, 3, 2, NULL) ? 1 : 0;
    ut_check(appended == 2, "the buffer takes exactly what fits");
    ut_check(!menu_patcher_append_slider(&context, 0x82, 5, 0, 0, 0, 0, 3, 2, NULL),
          "one widget too many is refused rather than written past the end");
}

int main(void)
{
    if (!host_image_resolve()) {
        ut_check(0, "host_image_resolve (the test executable is not a 32-bit PE?)");
        return ut_summary("menu_patcher");
    }

    test_begin_copies_and_counts();
    test_begin_refusals();
    test_append_and_commit();
    test_append_controls_group();
    test_bitmap_index_bounds();
    test_bitmap_bound_unavailable();
    test_commit_without_append();
    test_capacity_exhaustion();

    return ut_summary("menu_patcher");
}
