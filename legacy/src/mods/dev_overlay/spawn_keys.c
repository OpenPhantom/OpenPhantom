/* spawn_keys.c: see spawn_keys.h. */
#include "spawn_keys.h"

#include "cheats_openphantom.h"
#include "overlay_input.h"
#include "overlay_key_name.h"
#include "overlay_notice.h"

#include "common/ini.h"
#include "common/logging.h"

#define DEV_OVERLAY_SECTION "dev_overlay"

static const char *const KEY_NAME[SPAWN_KEY_COUNT] = { "SpawnPlaceKey", "SpawnFaceKey" };

static int32_t bound[SPAWN_KEY_COUNT];

void spawn_keys_load(void)
{
    uint32_t i;

    for (i = 0; i < SPAWN_KEY_COUNT; ++i) {
        char    text[32];
        int32_t key = 0;

        bound[i] = 0;
        if (!ini_read_string(DEV_OVERLAY_SECTION, KEY_NAME[i], "", text, sizeof text) ||
            text[0] == '\0') {
            continue;
        }
        if (!overlay_key_from_name(text, &key) || spawn_keys_refused((spawn_key_t)i, key)) {
            log_warning("npc spawner: %s=%s is not a key the placement mode can take, so it is "
                        "left unbound", KEY_NAME[i], text);
            continue;
        }
        bound[i] = key;
    }
}

int32_t spawn_keys_get(spawn_key_t which)
{
    return which < SPAWN_KEY_COUNT ? bound[which] : 0;
}

bool spawn_keys_is(spawn_key_t which, int32_t virtual_key)
{
    return which < SPAWN_KEY_COUNT && bound[which] != 0 && bound[which] == virtual_key;
}

/* The same keys the open key row refuses, for the same reasons: Escape is the way out of the panel
 * and of the mode, Return and the arrows drive the panel, and Alt with F4 closes the game. Shift
 * and Ctrl, which the mode reads held with the wheel for the fine and the square turn. Then the
 * keys that open the panel, since one key cannot both open it and switch the mode inside it, the
 * free camera's key, which the camera reads whatever else has the keys, and the other placement
 * key, since one key cannot mean two things. */
bool spawn_keys_refused(spawn_key_t which, int32_t virtual_key)
{
    uint32_t other;

    switch (virtual_key) {
    case 0x1B:      /* VK_ESCAPE */
    case 0x0D:      /* VK_RETURN */
    case 0x25:      /* VK_LEFT */
    case 0x26:      /* VK_UP */
    case 0x27:      /* VK_RIGHT */
    case 0x28:      /* VK_DOWN */
    case 0x12:      /* VK_MENU, either Alt */
    case 0x73:      /* VK_F4 */
    case 0x10:      /* VK_SHIFT */
    case 0x11:      /* VK_CONTROL */
    case 0xA0:      /* VK_LSHIFT */
    case 0xA1:      /* VK_RSHIFT */
    case 0xA2:      /* VK_LCONTROL */
    case 0xA3:      /* VK_RCONTROL */
        return true;
    default:
        break;
    }
    if (which >= SPAWN_KEY_COUNT || overlay_input_opens_on(virtual_key) ||
        (virtual_key != 0 && cheats_openphantom_freecam_hotkey() == virtual_key)) {
        return true;
    }
    other = (which == SPAWN_KEY_PLACE) ? (uint32_t)SPAWN_KEY_FACE : (uint32_t)SPAWN_KEY_PLACE;
    return virtual_key != 0 && bound[other] == virtual_key;
}

bool spawn_keys_bind(spawn_key_t which, int32_t virtual_key)
{
    char name[32];

    if (which >= SPAWN_KEY_COUNT || (virtual_key != 0 && spawn_keys_refused(which, virtual_key))) {
        overlay_notice_say("Refused: that key is taken by the panel or by a mode");
        return false;
    }
    /* The running mode first, so a key works at once even if the file could not be written, and
     * written as the code, as OpenKey is: the reader takes a code or a name, and a code is the
     * one spelling every key has. */
    bound[which] = virtual_key;
    overlay_key_name(virtual_key, name, sizeof name);
    log_info("npc spawner: %s is now %s", KEY_NAME[which], virtual_key != 0 ? name : "unbound");
    if (!ini_write_int(DEV_OVERLAY_SECTION, KEY_NAME[which], virtual_key)) {
        overlay_notice_say("That key works now, but could not be saved to the file");
        return false;
    }
    return true;
}
