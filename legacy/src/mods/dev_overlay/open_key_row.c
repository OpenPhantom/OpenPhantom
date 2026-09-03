/* open_key_row.c: see open_key_row.h. */
#include "open_key_row.h"

#include "overlay_input.h"

#include "common/ini.h"

#define DEV_OVERLAY_SECTION "dev_overlay"
#define OPEN_KEY_KEY        "OpenKey"

/* The keys this row will not bind, and every one of them is a way to lock a player out of the
 * panel or out of the game.
 *
 * Escape closes the panel, so binding it would make one key both the way in and the way out and
 * the panel would flicker rather than open. Return and the four arrows drive the panel's own rows;
 * a player who bound one of those could open the panel and then not move in it. Alt and F4 are
 * refused together because Alt+F4 closes the game and the panel does not read modifiers, so a
 * player pressing it to quit would get the panel over a closing window.
 *
 * Everything else is allowed, including keys the game itself uses. That is the player's business:
 * the panel is a developer tool, the game reads its own keys as DirectInput scancodes and never
 * sees ours, so a shared key does both things and the log says which key is bound. */
static bool key_is_refused(int32_t vk)
{
    switch (vk) {
    case 0x1B:      /* VK_ESCAPE, the way out of the panel */
    case 0x0D:      /* VK_RETURN, activates the selected row */
    case 0x25:      /* VK_LEFT */
    case 0x26:      /* VK_UP */
    case 0x27:      /* VK_RIGHT */
    case 0x28:      /* VK_DOWN */
    case 0x12:      /* VK_MENU, either Alt */
    case 0x73:      /* VK_F4, so Alt+F4 stays a way to quit */
        return true;
    default:
        return false;
    }
}

int32_t open_key_row_get(void)
{
    /* 0 rather than a key code is the default, and it is what an untouched installation reads. */
    return (int32_t)ini_read_int(DEV_OVERLAY_SECTION, OPEN_KEY_KEY, 0);
}

bool open_key_row_set(int32_t virtual_key)
{
    if (virtual_key != 0 && key_is_refused(virtual_key)) {
        return false;
    }
    /* The running panel first, so a player who binds a key can use it immediately even if the file
     * could not be written; the write is what makes it survive a restart, and its failure is worth
     * reporting but not worth undoing a binding that already works. */
    overlay_input_set_key(virtual_key);
    return ini_write_int(DEV_OVERLAY_SECTION, OPEN_KEY_KEY, (int)virtual_key);
}
