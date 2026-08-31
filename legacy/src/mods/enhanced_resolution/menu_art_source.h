/* menu_art_source.h: mount a folder of upscaled menu artwork ahead of the game's own archives.
 *
 * ==============================================================================================
 * What this is for
 *
 * menu_scale.c makes the menu canvas bigger. That is only half of a widescreen menu: the engine's
 * blitter copies one source pixel to one destination pixel, so the artwork has to be bigger too,
 * and the artwork is the player's own, converted by tools\convert_menu.ps1 against their own
 * installation. This is what makes the engine read the converted files instead of the originals.
 *
 * The engine mounts a chain of resource sources and asks each in turn, and it already promotes the
 * install root to the head of that chain, which is why a loose file beside WMAIN.EXE beats big.lab.
 * That was how this worked while it was being proven, and it is a poor thing to ask of somebody's
 * game folder: about seventy loose BMPs with names like `stars.BMP` sitting next to the executable,
 * with no way to tell them from anything else and no clean way to undo it.
 *
 * So instead the converter writes one folder and this mounts it, using the engine's own
 * res_addSource and res_promoteSource. One directory, deletable in one action, and the ini says
 * which one it is. Nothing about the game's own archives is touched or even opened.
 *
 * ==============================================================================================
 * Why it mounts at swmenu_startup
 *
 * The mount has to happen after the resource system exists and before any menu bitmap is looked
 * up. swmenu_startup is exactly between the two: it runs once, guarded by its own flag, it loads
 * the menu string table (so resources are demonstrably working by then), and no screen has been
 * built yet, so no bitmap has been resolved. Mounting at DLL load instead would be too early, and
 * mounting at the first swmenu_open would be too late, because a screen's bitmaps are loaded by
 * swmenu_build before it is ever opened.
 */
#ifndef MENU_ART_SOURCE_H
#define MENU_ART_SOURCE_H

#include <stdbool.h>

/* The folder the converter writes and this mounts, relative to the game directory. Matches
 * convert_menu.ps1's own default, and movies_hd's naming. */
#define MENU_ART_DEFAULT_DIRECTORY "menu_hd"

/* Arms the mount. `directory` is the MenuArtDirectory ini value; an empty string or NULL means the
 * default above. Passing false for `enabled` declines, which is what a reader gets by setting
 * MenuArtDirectory= to nothing.
 *
 * The folder is NOT mounted here: this only hooks swmenu_startup, and the mount happens the first
 * time the engine brings its menu system up. Returns true when the hook is live.
 *
 * A missing folder is not an error and is not warned about. Running without converted artwork is
 * the normal state of a fresh install, and menu_scale independently declines to scale when there is
 * none, so the two agree without having to talk to each other. */
bool menu_art_source_install(bool enabled, const char *directory);

/* The directory in force, always non-empty, with no trailing separator. Callers that need to read a
 * converted file directly from disk rather than through the engine use this, so there is one answer
 * to "where is the converted artwork" rather than two that can drift. */
const char *menu_art_source_directory(void);

#endif /* MENU_ART_SOURCE_H */
