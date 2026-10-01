/* spawn_marks.c: the box a copy's corners are drawn round, and a scene drawn with no screen.
 *
 * The box is the half of the marks that decides where they land; the drawing itself is the panel's
 * renderer, which a test process does not have. So the renderer's five calls are stood up below
 * and answer that there is no screen, and a scene handed over then draws nothing and says so by
 * not calling past the first question.
 */
#include "unittest.h"

#include "overlay_draw.h"
#include "spawn_marks.h"

#include <string.h>

static uint32_t drawn;

bool overlay_draw_screen(float *out_width, float *out_height)
{
    (void)out_width;
    (void)out_height;
    return false;
}

void overlay_draw_note(const char *text, float x, float y, uint32_t argb)
{
    (void)text;
    (void)x;
    (void)y;
    (void)argb;
    ++drawn;
}

float overlay_draw_note_width(const char *text)
{
    (void)text;
    ++drawn;
    return 0.0f;
}

float overlay_draw_note_height(void)
{
    ++drawn;
    return 0.0f;
}

void overlay_draw_pointer_at(float x, float y)
{
    (void)x;
    (void)y;
    ++drawn;
}

int main(void)
{
    spawn_mark_bracket_t box;
    spawn_marks_scene_t  scene;

    ut_section("the box");
    spawn_marks_box(100.0f, 200.0f, 100.0f, 100.0f, &box);
    ut_near(box.top, 100.0, 1e-4, "the head is the top");
    ut_near(box.bottom, 200.0, 1e-4, "the feet the bottom");
    ut_near(box.right - box.left, 50.0, 1e-4, "half as wide as it is tall");
    ut_near((box.left + box.right) * 0.5f, 100.0, 1e-4, "and centred on the body");
    spawn_marks_box(100.0f, 100.0f, 104.0f, 100.0f, &box);
    ut_check(box.bottom - box.top >= 6.0f && box.right - box.left >= 6.0f,
             "a copy far away is still a box one can see");
    ut_near((box.left + box.right) * 0.5f, 102.0, 1e-4,
            "a leaning body is centred between its feet and its head");
    spawn_marks_box(50.0f, 100.0f, 50.0f, 300.0f, &box);
    ut_check(box.top == 100.0f && box.bottom == 300.0f,
             "a head below the feet, seen from above, still makes a box the right way up");

    ut_section("no screen");
    memset(&scene, 0, sizeof scene);
    scene.brackets = 1u;
    spawn_marks_draw(&scene);
    spawn_marks_draw(NULL);
    ut_check(drawn == 0u, "with no display mode nothing is drawn and nothing is measured");

    return ut_summary("spawn marks");
}
