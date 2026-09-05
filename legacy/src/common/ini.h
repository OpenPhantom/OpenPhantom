/* ini.h: generic access to the shared configuration file.
 *
 * One file, <game>\engine_fixes.ini, with one section per DLL. A feature passes its own section
 * name on every call and therefore cannot read or overwrite another feature's key by accident.
 *
 * This module knows nothing about what any key means. Range checks, NaN handling and semantic
 * validation belong to the feature that owns the value.
 */
#ifndef COMMON_INI_H
#define COMMON_INI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Full path of the configuration file, next to the host executable. Never NULL. */
const char *ini_path(void);

bool    ini_read_bool  (const char *section, const char *key, bool default_value);
int32_t ini_read_int   (const char *section, const char *key, int32_t default_value);
float   ini_read_float (const char *section, const char *key, float default_value);

/* Always null-terminates `buffer`. Returns false when the key was absent, in which case
 * `default_value` has been copied instead. */
bool ini_read_string(const char *section, const char *key, const char *default_value,
                     char *buffer, size_t buffer_size);

/* `decimal_places` is clamped to 0..6. Returns false and leaves the file alone on failure,
 * a caller that logs "saved" without checking this is lying to the user. */
bool ini_write_float(const char *section, const char *key, float value, int decimal_places);
bool ini_write_int  (const char *section, const char *key, int32_t value);

/* A number that changes whenever the file has been written, and does not change while it has not.
 *
 * WHY THIS EXISTS. Reading a key means parsing the whole file, and this project's ini is around
 * ninety kilobytes. A feature that wants to notice an edit within a frame rather than within a
 * second cannot afford to do that every frame, and until now the only way to notice one at all was
 * to read the key and compare. This asks the file system for the last write time instead, which
 * costs one attribute query and no parse, so a poll can run every frame and only read when there
 * is something new to read.
 *
 * The value is opaque: compare it with the last one you saw and do not interpret it. It is zero
 * when the file cannot be examined at all, which is the same answer as "it has not changed", and
 * that is deliberate: a file that has gone missing should leave a feature on the last settings it
 * successfully read rather than reverting it to defaults.
 *
 * It says the file changed, not which key did. The caller still reads and compares its own key.
 */
uint64_t ini_generation(void);

#endif /* COMMON_INI_H */
