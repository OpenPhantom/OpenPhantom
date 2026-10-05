/* mp_mouse_latch.c: the mouse buttons the engine leaves down as a level begins.
 *
 * The release runs on a table of this test's own, laid out as the engine's: one cell a control,
 * the keys first, the joystick's buttons from 0x100 and the four mouse buttons last. What is held
 * is that only those four are touched, and only the ones that read down. The engine side has no
 * game to bind to here, which is the other thing held: it refuses and then does nothing.
 */
#include "unittest.h"

#include "mp_mouse_latch.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The jump key and the jump button of the joystick, as the engine's defaults bind them: both are
 * controls a release of the mouse buttons has no business with. */
#define KEY_X           0x2Du
#define JOYSTICK_BUTTON 0x101u

static uint32_t s_table[MP_MOUSE_LATCH_CONTROLS + 4u];   /* and four cells behind the table */

static void lay_the_table(void)
{
    size_t i;

    memset(s_table, 0, sizeof s_table);
    for (i = MP_MOUSE_LATCH_CONTROLS; i < MP_MOUSE_LATCH_CONTROLS + 4u; ++i) {
        s_table[i] = 0xA5A5A5A5u;
    }
}

static bool the_cells_behind_are_whole(void)
{
    size_t i;

    for (i = MP_MOUSE_LATCH_CONTROLS; i < MP_MOUSE_LATCH_CONTROLS + 4u; ++i) {
        if (s_table[i] != 0xA5A5A5A5u) {
            return false;
        }
    }
    return true;
}

static void check_the_table(void)
{
    ut_section("the table as the engine lays it out");
    ut_check(MP_MOUSE_LATCH_FIRST == 0x124u && MP_MOUSE_LATCH_BUTTONS == 4u &&
                 MP_MOUSE_LATCH_CONTROLS == 0x128u,
             "the four mouse buttons are controls 0x124 to 0x127, the last of 296");
    ut_check(MP_MOUSE_LATCH_JUMP == 0x125u,
             "and the second of them is the one the engine binds to jump and its flush writes");
}

static void check_the_release(void)
{
    bool     read = false;
    uint32_t were_down;

    ut_section("the flush left jump down: the cell is released, and nothing else is touched");
    lay_the_table();
    s_table[KEY_X]               = 1u;
    s_table[JOYSTICK_BUTTON]     = 1u;
    s_table[MP_MOUSE_LATCH_JUMP] = 1u;
    were_down = mp_mouse_latch_release((uintptr_t)s_table, &read);
    ut_check(read && were_down == 0x2u, "the second button is named as the one that read down");
    ut_check(s_table[MP_MOUSE_LATCH_JUMP] == 0u, "its cell reads up afterwards");
    ut_check(s_table[KEY_X] == 1u && s_table[JOYSTICK_BUTTON] == 1u,
             "the jump key and the joystick's jump button keep what they held: their devices "
             "are polled as states and the keyboard has a flush of its own");
    ut_check(the_cells_behind_are_whole(), "and nothing behind the table is written");

    ut_section("asked again there is nothing to release");
    were_down = mp_mouse_latch_release((uintptr_t)s_table, &read);
    ut_check(read && were_down == 0u, "no button reads down, and the cells did read");

    ut_section("every button that reads down is released, whatever value stands for down");
    lay_the_table();
    s_table[MP_MOUSE_LATCH_FIRST]      = 1u;
    s_table[MP_MOUSE_LATCH_FIRST + 2u] = 0x00FF0000u;
    s_table[MP_MOUSE_LATCH_FIRST + 3u] = 0xFFFFFFFFu;
    were_down = mp_mouse_latch_release((uintptr_t)s_table, &read);
    ut_check(read && were_down == 0xDu, "the first, the third and the fourth are named");
    ut_check(s_table[MP_MOUSE_LATCH_FIRST] == 0u && s_table[MP_MOUSE_LATCH_FIRST + 1u] == 0u &&
                 s_table[MP_MOUSE_LATCH_FIRST + 2u] == 0u &&
                 s_table[MP_MOUSE_LATCH_FIRST + 3u] == 0u && the_cells_behind_are_whole(),
             "all four read up afterwards, and the table's end is where it was");

    ut_section("no table is no release");
    read = true;
    ut_check(mp_mouse_latch_release(0u, &read) == 0u && !read,
             "a table that did not resolve releases nothing and says the cells did not read");
    ut_check(mp_mouse_latch_release(0u, NULL) == 0u, "with nowhere to say it as well");
}

static void check_who_pressed(void)
{
    ut_section("a button down in the table that nobody presses is the flush's, or a lost release");
    ut_check(mp_mouse_latch_not_pressed(0x2u, 0x0u) == 0x2u,
             "jump down in the table with no button pressed: nobody's");
    ut_check(mp_mouse_latch_not_pressed(0x2u, 0x2u) == 0x0u,
             "jump down in the table with the second button held: the player's own");
    ut_check(mp_mouse_latch_not_pressed(0xFu, 0x5u) == 0xAu,
             "of four down with the first and third held, the second and fourth are nobody's");
    ut_check(mp_mouse_latch_not_pressed(0x0u, 0xFu) == 0x0u &&
                 mp_mouse_latch_not_pressed(0xFFFFFFF0u, 0x0u) == 0x0u,
             "nothing released is nobody's, and bits past the four buttons are no buttons");
}

static void check_without_a_game(void)
{
    ut_section("with no game in the process the engine side refuses and then does nothing");
    ut_check(!mp_mouse_latch_install(), "the table does not resolve");
    ut_check(!mp_mouse_latch_install(), "and asked again the answer is the same");
    lay_the_table();
    s_table[MP_MOUSE_LATCH_JUMP] = 1u;
    mp_mouse_latch_level_begins();
    mp_mouse_latch_report();
    ut_check(s_table[MP_MOUSE_LATCH_JUMP] == 1u,
             "a level's beginning writes nowhere, least of all into a table it was never given");
}

int main(void)
{
    check_the_table();
    check_the_release();
    check_who_pressed();
    check_without_a_game();

    return ut_summary("mp_mouse_latch");
}
