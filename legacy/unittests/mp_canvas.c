/* The list a canvas widget draws, driven over its edges with no game and no pixel.
 *
 * Every rule here is one a player feels rather than reads. A selection that jumps out of the window
 * when the arrow key is pressed once too often, a wheel that drags the selection along with the
 * page, a list that shrinks and leaves the keyboard standing on a row that is gone, a hover that
 * lands on the header or on the blank space under the last row: each is a field report waiting to
 * be written. The numbers are pinned here instead.
 */
#include "unittest.h"

#include "mp_canvas.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* A list of 30-pixel rows under a 24-pixel header in a 174-pixel window: exactly five fit. */
static void five_rows(mp_canvas_list_t *list, uint32_t rows)
{
    mp_canvas_list_init(list, 30u, 24u, 174u);
    mp_canvas_list_set_rows(list, rows);
}

static void check_geometry(void)
{
    mp_canvas_list_t list;
    uint32_t         row = 99u;
    int32_t          y = -1;

    ut_section("rows fit the window the way the pixels say they do");
    five_rows(&list, 12u);
    ut_check(mp_canvas_list_visible(&list) == 5u, "five thirty-pixel rows fit under the header");
    ut_check(mp_canvas_list_scrolls(&list), "and twelve rows is more than five, so it scrolls");

    ut_check(!mp_canvas_list_row_at(&list, 10, &row), "the header is not a row");
    ut_check(mp_canvas_list_row_at(&list, 24, &row) && row == 0u,
             "the first pixel under it is row 0");
    ut_check(mp_canvas_list_row_at(&list, 53, &row) && row == 0u,
             "and so is the last pixel of row 0");
    ut_check(mp_canvas_list_row_at(&list, 54, &row) && row == 1u, "row 1 starts on the next pixel");
    ut_check(mp_canvas_list_row_at(&list, 173, &row) && row == 4u,
             "the last visible pixel is row 4");
    ut_check(!mp_canvas_list_row_at(&list, 174, &row), "and the window ends there");

    ut_check(mp_canvas_list_row_top(&list, 2u, &y) && y == 84,
             "row 2 is drawn at header plus two rows");
    ut_check(!mp_canvas_list_row_top(&list, 7u, &y),
             "a row scrolled out of the window has no place");

    five_rows(&list, 3u);
    ut_check(!mp_canvas_list_row_at(&list, 120, &row),
             "with three rows the blank space under the third is not a row either");
    ut_check(!mp_canvas_list_scrolls(&list), "and three rows do not scroll");
}

static void check_a_short_window_is_not_a_division_by_zero(void)
{
    mp_canvas_list_t list;

    ut_section("a window shorter than one row still answers");
    mp_canvas_list_init(&list, 30u, 24u, 20u);
    mp_canvas_list_set_rows(&list, 4u);
    ut_check(mp_canvas_list_visible(&list) == 1u, "one row is the least a window ever shows");
    mp_canvas_list_move(&list, 1);
    mp_canvas_list_move(&list, 1);
    ut_check(list.selected == 1 && list.first == 1u,
             "and moving through it scrolls one row at a time");
}

static void check_the_selection_drags_the_window(void)
{
    mp_canvas_list_t list;

    ut_section("the selection follows the row and the window follows the selection");
    five_rows(&list, 12u);
    ut_check(list.selected == MP_CANVAS_NONE, "nothing is selected to begin with");

    mp_canvas_list_move(&list, 1);
    ut_check(list.selected == 0 && list.first == 0u,
             "the first key selects the first visible row rather than moving from nowhere");

    mp_canvas_list_move(&list, 4);
    ut_check(list.selected == 4 && list.first == 0u,
             "four down is the last visible row, no scroll");
    mp_canvas_list_move(&list, 1);
    ut_check(list.selected == 5 && list.first == 1u,
             "one more scrolls by exactly one: the selection stays on the bottom line");
    mp_canvas_list_move(&list, 100);
    ut_check(list.selected == 11 && list.first == 7u,
             "a big move clamps at the last row and the window shows the last five");
    mp_canvas_list_move(&list, -100);
    ut_check(list.selected == 0 && list.first == 0u, "and back to the top clamps and scrolls up");
}

static void check_the_window_moves_without_the_selection(void)
{
    mp_canvas_list_t list;
    int32_t          y;

    ut_section("scrolling the window leaves the selection where it is");
    five_rows(&list, 12u);
    mp_canvas_list_select(&list, 2);
    mp_canvas_list_scroll(&list, 5);
    ut_check(list.first == 5u && list.selected == 2,
             "a wheel moves the page, not the selection, even out of sight");
    ut_check(!mp_canvas_list_row_top(&list, 2u, &y), "so the selected row has no place to draw");
    mp_canvas_list_scroll(&list, 100);
    ut_check(list.first == 7u, "the window clamps so the last row stays at the bottom");
    mp_canvas_list_scroll(&list, -100);
    ut_check(list.first == 0u, "and at the top");

    mp_canvas_list_move(&list, 1);
    ut_check(list.selected == 3 && list.first == 0u,
             "the next key moves from the selection, wherever the window was");
}

static void check_select_scrolls_the_least(void)
{
    mp_canvas_list_t list;

    ut_section("selecting an unseen row scrolls by the least that shows it");
    five_rows(&list, 12u);
    mp_canvas_list_select(&list, 9);
    ut_check(list.first == 5u, "row 9 lands on the bottom line of the window");
    mp_canvas_list_select(&list, 1);
    ut_check(list.first == 1u, "row 1 lands on the top line");
    mp_canvas_list_select(&list, 3);
    ut_check(list.first == 1u, "a row already in the window moves nothing");
    mp_canvas_list_select(&list, 50);
    ut_check(list.selected == MP_CANVAS_NONE, "a row past the end selects nothing");
}

static void check_the_keys(void)
{
    mp_canvas_list_t list;

    ut_section("the keys a focused canvas is handed");
    five_rows(&list, 12u);
    ut_check(mp_canvas_list_key(&list, MP_CANVAS_VK_END) && list.selected == 11,
             "end goes to the last row");
    ut_check(mp_canvas_list_key(&list, MP_CANVAS_VK_HOME) && list.selected == 0,
             "home to the first");
    ut_check(mp_canvas_list_key(&list, MP_CANVAS_VK_NEXT) && list.selected == 5,
             "page down moves by the window's five rows");
    ut_check(mp_canvas_list_key(&list, MP_CANVAS_VK_PRIOR) && list.selected == 0,
             "and page up back");
    ut_check(mp_canvas_list_key(&list, MP_CANVAS_VK_DOWN) && list.selected == 1, "down by one");
    ut_check(mp_canvas_list_key(&list, MP_CANVAS_VK_UP) && list.selected == 0, "up by one");
    ut_check(!mp_canvas_list_key(&list, 0x41u),
             "a letter is not answered, so the caller can hand it on rather than beep");
    ut_check(!mp_canvas_list_key(&list, MP_CANVAS_VK_TAB),
             "and tab is not either: it is the engine's focus walk, not the list's");
}

static void check_hover(void)
{
    mp_canvas_list_t list;

    ut_section("the pointer hovers a row or nothing");
    five_rows(&list, 12u);
    mp_canvas_list_hover(&list, 10, 60, 500u);
    ut_check(list.hovered == 1, "inside the rectangle on row 1");
    mp_canvas_list_hover(&list, 600, 60, 500u);
    ut_check(list.hovered == MP_CANVAS_NONE, "past the right edge hovers nothing");
    mp_canvas_list_hover(&list, -1, 60, 500u);
    ut_check(list.hovered == MP_CANVAS_NONE, "and past the left edge");
    mp_canvas_list_hover(&list, 10, 5, 500u);
    ut_check(list.hovered == MP_CANVAS_NONE, "and on the header");
    mp_canvas_list_scroll(&list, 3);
    mp_canvas_list_hover(&list, 10, 60, 500u);
    ut_check(list.hovered == 4, "after a scroll the same pixel is a different row");
}

static void check_the_list_shrinks_under_the_keyboard(void)
{
    mp_canvas_list_t list;

    ut_section("a list that shrinks leaves the keyboard on a row that exists");
    five_rows(&list, 12u);
    mp_canvas_list_select(&list, 11);
    mp_canvas_list_hover(&list, 10, 60, 500u);
    mp_canvas_list_set_rows(&list, 4u);
    ut_check(list.selected == 3, "the selection lands on the new last row rather than on nothing");
    ut_check(list.first == 0u, "and the window is pulled back so the four rows are seen");
    ut_check(list.hovered == MP_CANVAS_NONE || list.hovered < 4,
             "and the hover does not name a row that is gone");
    mp_canvas_list_set_rows(&list, 0u);
    ut_check(list.selected == MP_CANVAS_NONE && list.first == 0u, "an empty list selects nothing");
    mp_canvas_list_move(&list, 1);
    ut_check(list.selected == MP_CANVAS_NONE, "and a key on an empty list is not a crash");
}

static void check_the_thumb(void)
{
    mp_canvas_list_t list;
    float            at = -1.0f;
    float            len = -1.0f;

    ut_section("the scrollbar's thumb is the window over the list");
    five_rows(&list, 3u);
    mp_canvas_list_thumb(&list, &at, &len);
    ut_check(at == 0.0f && len == 1.0f, "a list that fits has a thumb the whole track long");

    five_rows(&list, 20u);
    mp_canvas_list_thumb(&list, &at, &len);
    ut_check(len > 0.24f && len < 0.26f, "five of twenty is a quarter");
    ut_check(at == 0.0f, "at the top to begin with");
    mp_canvas_list_scroll(&list, 15);
    mp_canvas_list_thumb(&list, &at, &len);
    ut_check(at > 0.74f && at < 0.76f && at + len > 0.99f,
             "scrolled to the end the thumb touches the bottom of the track");
}

static void check_tabs(void)
{
    ut_section("a strip of equal tabs");
    ut_check(mp_canvas_tab_at(3u, 0, 600u) == 0u, "the first pixel is the first tab");
    ut_check(mp_canvas_tab_at(3u, 199, 600u) == 0u, "and the last pixel of its third");
    ut_check(mp_canvas_tab_at(3u, 200, 600u) == 1u, "the next pixel is the second tab");
    ut_check(mp_canvas_tab_at(3u, 599, 600u) == 2u, "the last pixel is the last tab");
    ut_check(mp_canvas_tab_at(3u, 600, 600u) == 3u, "past the strip is none");
    ut_check(mp_canvas_tab_at(3u, -1, 600u) == 3u, "and before it");
    ut_check(mp_canvas_tab_at(0u, 10, 600u) == 0u, "no tabs answers none, which is zero of zero");
}

int main(void)
{
    check_geometry();
    check_a_short_window_is_not_a_division_by_zero();
    check_the_selection_drags_the_window();
    check_the_window_moves_without_the_selection();
    check_select_scrolls_the_least();
    check_the_keys();
    check_hover();
    check_the_list_shrinks_under_the_keyboard();
    check_the_thumb();
    check_tabs();

    return ut_summary("the canvas list");
}
