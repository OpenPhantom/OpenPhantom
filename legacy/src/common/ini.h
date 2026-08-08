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

#endif /* COMMON_INI_H */
