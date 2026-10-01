/* mp_settings_ini.h: the intention on disk, and nothing else.
 *
 * The seam mp_menu.c named for itself: the two functions that turn the value into ini keys and
 * back are the only ones that know the key names. They read and write the feature's own section
 * of engine_fixes.ini, never the game's obi.ini, which is a relative path and a flat section the
 * game owns.
 *
 * Every key has the same default in mp_settings_default, so a missing key changes nothing and a
 * player may delete any of them. A list slot past the end is removed rather than blanked, so a
 * shortened list leaves no hole for the next load to read.
 */
#ifndef MULTIPLAYER_MP_SETTINGS_INI_H
#define MULTIPLAYER_MP_SETTINGS_INI_H

#include "mp_settings.h"

#include <stdbool.h>

/* Defaults first, then whatever the ini holds on top. */
void mp_settings_ini_load(mp_settings_t *settings);

/* Every key, whether or not it changed. */
void mp_settings_ini_save(const mp_settings_t *settings);

/* The player's name alone. The name is the one thing a player types that is not part of an
 * intention to host or join, so it is kept the moment it is typed rather than only when a session
 * is started; backing out of the menu must not throw it away. An empty name writes nothing. */
void mp_settings_ini_save_name(const char *name);

#endif /* MULTIPLAYER_MP_SETTINGS_INI_H */
