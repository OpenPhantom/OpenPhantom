/* import_patch.h: replace one entry in another module's import address table.
 *
 * Everything else in this tree patches the GAME. This patches a library the game loads: it changes
 * which function one named module calls for one named import, while the rest of the process is
 * affected. Our own calls to the same function are untouched, because they resolve through our own
 * import table.
 *
 * The entry is found by ADDRESS rather than by name. A module's import names live in
 * OriginalFirstThunk, which the loader is free to leave at zero for a bound import, in which case
 * there are no names left to match against. The addresses in FirstThunk are always present and
 * always correct, so resolving the function once with GetProcAddress and looking for that value is
 * the form that works in both cases.
 */
#ifndef IMPORT_PATCH_H
#define IMPORT_PATCH_H

#include <stdbool.h>

#include <windows.h>

/* Replaces `imported_dll`'s `function_name` entry in `module_name`'s import table with
 * `replacement`, and hands back what was there so the caller can chain to it. A NULL
 * module name means the running executable, which is how the game's own imports are reached.
 *
 * Returns false, having changed nothing, when any of the three names does not resolve or the entry
 * is not in that module's table. That is a normal outcome rather than an error: the module may not
 * be loaded, or a different build of it may not import that function at all, and the caller is
 * expected to log and carry on without the feature. */
/* The slot has to hold the address the exporting module answers for `function_name` today. A slot
 * something else has already redirected, a wrapper hooking the same import, reads as absent and
 * this answers false, so the feature declines rather than chaining onto a stranger's hook. */
bool import_patch_replace(const char *module_name, const char *imported_dll,
                          const char *function_name, void *replacement, void **out_original);

#endif /* IMPORT_PATCH_H */
