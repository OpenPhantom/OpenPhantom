/* overlay_key_name.h: a virtual key code as a player would read it.
 *
 * Shared because two rows in two different groups bind a key: the free camera's teleport key among
 * the cheats, and the key that opens the panel among the utilities. One copy of the naming keeps
 * the two chips reading the same, which matters more than it sounds: a player comparing them is
 * comparing two bindings of the same kind.
 */
#ifndef DEV_OVERLAY_OVERLAY_KEY_NAME_H
#define DEV_OVERLAY_OVERLAY_KEY_NAME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Writes a short name for `vk` into `out`, always terminated. Letters and digits are themselves,
 * function keys read as F1 to F12, a handful of others get a word, and anything else falls back to
 * its hex code, which is at least something a player can put in the ini. */
void overlay_key_name(int32_t vk, char *out, size_t out_size);

/* And the other direction, for OpenKey in the settings file.
 *
 * A person locked out of the panel cannot use the row inside it that binds a key, which is the one
 * situation where this setting matters most, so the file has to be typeable: "F8", "numpad +",
 * "backtick", "A". Case, spaces and underscores are ignored.
 *
 * A bare number is still a virtual key code, which is what this setting has always been and what
 * every file written before this understood. That is also the way to reach a key with no name in
 * the table, including the number row: 53 is the 5 key.
 *
 * False when the text is not a key, which the caller reports rather than guessing at. */
bool overlay_key_from_name(const char *text, int32_t *out);

#endif /* DEV_OVERLAY_OVERLAY_KEY_NAME_H */
