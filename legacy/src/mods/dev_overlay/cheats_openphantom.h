/* cheats_openphantom.h: the three codes this project adds - unlimited ammunition, unlimited
 * health, and no fog.
 *
 * The first two work the same way and it is the smallest way there is. The engine spends
 * ammunition and applies damage through one short function each, and while a cheat is on its
 * detour returns without calling the original. Nothing is written into the player's record, no
 * counter is topped up and no timer is held, so switching a cheat off leaves the game in a state
 * it could have reached by itself.
 *
 * That matters more than it sounds. Refilling ammunition every frame would fight the pickup code,
 * change what the HUD flashes and survive a save; declining to subtract does none of those.
 *
 * No fog is a different shape, because there is nothing to decline: fog is not spent, it is a bit
 * in the loaded level's own record, read fresh every frame by the renderer rather than cached
 * anywhere this project could detour instead. See cheats_no_fog.c for why it is a per-frame force
 * rather than a single write, and why turning it back off does not try to restore what a level
 * authored.
 */
#ifndef CHEATS_OPENPHANTOM_H
#define CHEATS_OPENPHANTOM_H

#include <stdbool.h>
#include <stdint.h>

typedef enum cheats_own_id {
    CHEATS_OWN_UNLIMITED_AMMO = 0,
    CHEATS_OWN_UNLIMITED_HEALTH,
    CHEATS_OWN_NO_FOG,
    CHEATS_OWN_COUNT
} cheats_own_id_t;

/* Places both detours. Answers false if neither could be placed; a single one that resolved is
 * kept and reported, because half of this feature is still worth having and the log says which
 * half. Idempotent. */
bool cheats_openphantom_install(void);

/* The name shown in the panel, and NULL for an id out of range. */
const char *cheats_openphantom_name(cheats_own_id_t id);

/* Whether the site this cheat needs actually resolved. A cheat that never armed is shown but
 * cannot be switched, which is honest: the alternative is a row that ticks and does nothing. */
bool cheats_openphantom_is_available(cheats_own_id_t id);

bool cheats_openphantom_is_on(cheats_own_id_t id);

/* Flips one and answers the new state. A cheat whose site did not resolve stays off. */
bool cheats_openphantom_toggle(cheats_own_id_t id);

#endif /* CHEATS_OPENPHANTOM_H */
