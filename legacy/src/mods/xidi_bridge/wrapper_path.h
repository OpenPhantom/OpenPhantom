/* wrapper_path.h: which file the wrapper is, and whether the library already came from our folder.
 *
 * The two questions this feature asks about paths, kept apart from the import table work because
 * they are the only part of it a test can observe without a process to patch. Both are pure string
 * work: nothing here touches the file system.
 */
#ifndef XIDI_BRIDGE_WRAPPER_PATH_H
#define XIDI_BRIDGE_WRAPPER_PATH_H

#include <stdbool.h>
#include <stddef.h>

/* True when `path` names a file inside `directory`.
 *
 * The match has to end on a separator, otherwise a sibling folder whose name merely begins with
 * the same characters counts as inside it: with a plain prefix test, "C:\Game" contains
 * "C:\Gamedata\winmm.dll". A trailing separator on `directory` is optional and means the same
 * thing either way. Windows paths, so the comparison ignores case and treats / and \ alike. */
bool wrapper_path_inside(const char *directory, const char *path);

/* Joins `directory` and `file_name` into `buffer`, inserting a separator when `directory` lacks
 * one. Returns false and writes an empty string when an argument is missing or the result does not
 * fit, so a caller that ignores the return value gets an empty path rather than a truncated one
 * that names some other file. */
bool wrapper_path_join(const char *directory, const char *file_name,
                       char *buffer, size_t buffer_size);

#endif /* XIDI_BRIDGE_WRAPPER_PATH_H */
