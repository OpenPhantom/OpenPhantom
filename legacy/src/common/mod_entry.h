/* mod_entry.h: the contract between the loader and a feature DLL.
 *
 * A feature DLL exports exactly one function:
 *
 *     void engine_fix_install(void);
 *
 * The loader calls it AFTER LoadLibrary has returned, which means it runs OUTSIDE the loader lock
 * with the main image fully mapped. That is the whole reason the loader exists as a separate
 * step: DllMain may not scan, may not read files and may not load anything, and a patch installer
 * wants to do all three.
 *
 * A DLL in the mods folder WITHOUT this export is still loaded; the loader logs that it has no
 * entry point and moves on. Arbitrary third-party DLLs therefore work too.
 */
#ifndef COMMON_MOD_ENTRY_H
#define COMMON_MOD_ENTRY_H

#define ENGINE_FIX_ENTRY_NAME "engine_fix_install"

#define ENGINE_FIX_ENTRY __declspec(dllexport) void __cdecl engine_fix_install(void)

#endif /* COMMON_MOD_ENTRY_H */
