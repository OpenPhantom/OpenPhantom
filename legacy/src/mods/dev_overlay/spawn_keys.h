/* spawn_keys.h: the two keys of the entity spawner's placement mode, bound in the panel.
 *
 * One turns the mode on and off, from the panel shut as well, which opens it straight into the
 * mode; the other turns the entity back to face the player. Neither has a default: a key the
 * player did not choose is a key that does something they did not expect. Everything else the
 * mode does is on the mouse, and Escape leaves it, as it leaves everything in the panel.
 *
 * Stored in the settings file the way OpenKey is, [dev_overlay] SpawnPlaceKey and SpawnFaceKey: the
 * panel writes the key's code, and a person can type a name there instead, "F9".
 *
 * Internal to dev_overlay.
 */
#ifndef DEV_OVERLAY_SPAWN_KEYS_H
#define DEV_OVERLAY_SPAWN_KEYS_H

#include <stdbool.h>
#include <stdint.h>

typedef enum spawn_key {
    SPAWN_KEY_PLACE = 0,   /* the mode on and off */
    SPAWN_KEY_FACE,        /* turn back to face the player */
    SPAWN_KEY_COUNT
} spawn_key_t;

/* Reads both from the settings file. A name that is not a key is reported and left unbound. */
void spawn_keys_load(void);

/* The virtual key bound, 0 for none. */
int32_t spawn_keys_get(spawn_key_t which);

/* Whether `virtual_key` is the key bound for `which`. Never true for an unbound key. */
bool spawn_keys_is(spawn_key_t which, int32_t virtual_key);

/* Binds a key and writes it to the file. Refused, and the binding kept, for a key that would lock
 * the player out of the panel or collide with it: Escape, Return, the arrows, Alt, F4, the key
 * that opens the panel, and the other placement key. 0 unbinds. */
bool spawn_keys_bind(spawn_key_t which, int32_t virtual_key);

/* Whether a key is refused for binding, for the test. */
bool spawn_keys_refused(spawn_key_t which, int32_t virtual_key);

#endif /* DEV_OVERLAY_SPAWN_KEYS_H */
