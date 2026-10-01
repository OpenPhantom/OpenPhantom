/* mp_mod_folder.h: which modules of this process came out of the mods folder.
 *
 * Layer 3. The folder is the loader's: the host's directory and [loader] ModDirectory, the way the
 * loader composes them, written plainly so that "mods\", "mods/" and ".\mods" name one folder. The
 * truth is what runs: the process's module list, with the path each module was loaded from, which
 * stays the same when its file is renamed afterwards. Of that list a module whose file lies
 * directly in the folder is handed over, whatever its name ends in, because the loader loads from
 * there and nothing else does. A module from a folder below it is a mod's own file, the movie
 * player's runtime among them, and is only counted; one from anywhere else, a wrapper beside the
 * executable or what the system and its drivers place in every process, is not looked at.
 *
 * The list is asked for in the main thread, from the menu or a substep, never under the loader
 * lock. When it cannot be read the folder's DLLs are asked of the system by name instead, and a DLL
 * renamed after it was loaded is then not seen; the walk says which way it went.
 */
#ifndef MULTIPLAYER_MP_MOD_FOLDER_H
#define MULTIPLAYER_MP_MOD_FOLDER_H

#include <stdbool.h>
#include <stddef.h>

/* The longest path the walk holds, MAX_PATH, named here so this header stays free of windows.h. */
#define MP_MOD_FOLDER_PATH_MAX 260u

typedef enum mp_mod_place {
    MP_MOD_PLACE_ELSEWHERE = 0,   /* outside the folder */
    MP_MOD_PLACE_DIRECTLY,        /* a file directly in the folder, where the loader loads from */
    MP_MOD_PLACE_BELOW            /* in a folder below it: a mod's own files */
} mp_mod_place_t;

/* Pure. Where the file at `path` lies against `folder`. Both are compared as the same path written
 * plainly: '/' as '\', no doubled separator, no "." or ".." step, no separator at the end, and
 * without regard to case. The folder itself lies elsewhere. */
mp_mod_place_t mp_mod_folder_place(const char *path, const char *folder);

/* What a walk found besides the modules it handed over. */
typedef struct mp_mod_folder_walk {
    char          folder[MP_MOD_FOLDER_PATH_MAX];   /* the folder, written plainly */
    bool          list_asked;     /* the module list was asked for */
    bool          from_list;      /* and it answered */
    unsigned long list_error;     /* why it did not */
    bool          folder_found;   /* the folder holds a DLL */
    unsigned      below;          /* modules from folders below it */
    unsigned      not_loaded;     /* DLLs in the folder that no module came from */
} mp_mod_folder_walk_t;

/* One module whose file lies directly in the folder: its handle, an HMODULE passed as a pointer,
 * and its file name as it was loaded. */
typedef void (*mp_mod_folder_each_fn)(void *module, const char *name);

/* Hands `each` every module of this process whose file lies directly in the mods folder: from the
 * module list, or from the folder by name when `module_list` is false or the list cannot be read.
 * Counts the DLLs of the folder that were not loaded either way, and fills `out`. */
void mp_mod_folder_walk(bool module_list, mp_mod_folder_each_fn each, mp_mod_folder_walk_t *out);

#endif /* MULTIPLAYER_MP_MOD_FOLDER_H */
