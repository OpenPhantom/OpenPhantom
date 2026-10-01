/* spawn_keys.c: the placement mode's two keys, which ones may be bound, and the file they live in.
 *
 * The panel's own key opens the panel, so it may not also switch the mode inside it; which key
 * that is belongs to overlay_input.c, and is F6 here, as it is in a game with no OpenKey set. The
 * free camera's key is K here.
 */
#include "unittest.h"

#include "cheats_openphantom.h"
#include "overlay_input.h"
#include "spawn_keys.h"

#define VK_F6_KEY 0x75
#define VK_F9_KEY 0x78
#define VK_P_KEY  0x50
#define VK_K_KEY  0x4B

bool overlay_input_opens_on(int32_t virtual_key)
{
    return virtual_key == VK_F6_KEY;
}

int32_t cheats_openphantom_freecam_hotkey(void)
{
    return VK_K_KEY;
}

int main(void)
{
    static const int32_t REFUSED[] = { 0x1B, 0x0D, 0x25, 0x26, 0x27, 0x28, 0x12, 0x73,
                                       0x10, 0x11, 0xA0, 0xA1, 0xA2, 0xA3, VK_F6_KEY };
    bool                 all = true;
    uint32_t             i;

    ut_section("which keys are refused");
    for (i = 0; i < sizeof REFUSED / sizeof REFUSED[0]; ++i) {
        if (!spawn_keys_refused(SPAWN_KEY_PLACE, REFUSED[i]) ||
            !spawn_keys_refused(SPAWN_KEY_FACE, REFUSED[i])) {
            all = false;
        }
    }
    ut_check(all, "Escape, Return, the arrows, Alt, F4, Shift, Ctrl and the panel's own key are "
                  "refused for both");
    ut_check(spawn_keys_refused(SPAWN_KEY_PLACE, VK_K_KEY) &&
                 spawn_keys_refused(SPAWN_KEY_FACE, VK_K_KEY),
             "so is the free camera's key, which the camera reads whatever has the keys");
    ut_check(!spawn_keys_refused(SPAWN_KEY_PLACE, VK_F9_KEY), "F9 is free");

    ut_section("binding");
    ut_check(spawn_keys_bind(SPAWN_KEY_PLACE, VK_F9_KEY) &&
                 spawn_keys_get(SPAWN_KEY_PLACE) == VK_F9_KEY,
             "F9 binds the mode's key");
    ut_check(spawn_keys_is(SPAWN_KEY_PLACE, VK_F9_KEY) && !spawn_keys_is(SPAWN_KEY_FACE, VK_F9_KEY),
             "and is that key and not the other");
    ut_check(!spawn_keys_bind(SPAWN_KEY_FACE, VK_F9_KEY) && spawn_keys_get(SPAWN_KEY_FACE) == 0,
             "the other key may not take it too, and stays unbound");
    ut_check(!spawn_keys_bind(SPAWN_KEY_FACE, 0x1B), "Escape is refused as a binding");
    ut_check(spawn_keys_bind(SPAWN_KEY_FACE, VK_P_KEY), "P binds the face key");

    ut_section("the file");
    spawn_keys_load();
    ut_check(spawn_keys_get(SPAWN_KEY_PLACE) == VK_F9_KEY &&
                 spawn_keys_get(SPAWN_KEY_FACE) == VK_P_KEY,
             "both come back from the settings file as they were bound");
    ut_check(spawn_keys_bind(SPAWN_KEY_PLACE, 0) && spawn_keys_get(SPAWN_KEY_PLACE) == 0 &&
                 !spawn_keys_is(SPAWN_KEY_PLACE, 0),
             "0 unbinds, and an unbound key is never the key pressed");
    spawn_keys_load();
    ut_check(spawn_keys_get(SPAWN_KEY_PLACE) == 0, "and the file keeps it unbound");
    ut_check(spawn_keys_bind(SPAWN_KEY_FACE, 0), "the face key is let go for the next run");

    return ut_summary("spawn keys");
}
