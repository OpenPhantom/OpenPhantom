/* overlay_stubs.c: the drawing and input halves of the overlay, stubbed for the two model tests.
 *
 * Both overlay_model.c and overlay_groups.c link the real model and the real row sources, and
 * those reach across to the other half of the overlay in three places; the stubs below answer
 * for them, so the two programs share one copy instead of each carrying its own. */
#include "overlay_draw.h"
#include "overlay_input.h"

#include <stdbool.h>
#include <stdint.h>

/* The drawing and input halves, stubbed.
 *
 * The sources under test reach across to the other half of the overlay in three places: the size
 * row asks the renderer how big the screen is, the level skip and the free camera both ask the
 * input half to close the panel, and the open-key row hands it a new key. Linking the real
 * overlay_draw.c and overlay_input.c to satisfy those would drag Direct3D and a window procedure
 * into a process that has neither, which is a much larger dependency than the model test wants
 * for two calls it does not exercise.
 *
 * The screen stub answers "no screen", which is the honest answer here and the one the size row is
 * already written to survive: no device means no measurement, so automatic sizing falls back to its
 * own default rather than reading a number out of an uninitialised renderer. */
bool overlay_draw_screen(float *out_width, float *out_height)
{
    (void)out_width;
    (void)out_height;
    return false;
}

void overlay_input_close(void)
{
}

/* Reached by the row that binds the key opening the panel. Stubbed rather than linked in, for the
 * same reason the two above are: overlay_input.c installs a window-procedure detour, and a test
 * process has no window. The row's own decisions, which key it refuses and what the chip reads,
 * are the half worth pinning down here and they do not depend on this doing anything. */
void overlay_input_set_key(int32_t virtual_key)
{
    (void)virtual_key;
}
