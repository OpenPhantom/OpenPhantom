/* texture_size.h: whether a requested texture size may be used, and in the unit the engine wants.
 *
 * Split out of large_textures.c because it is the only part of this feature a test can observe
 * without the game. Everything else there is a signature scan and a write into a live process.
 * Every function here is a pure function of its arguments: the caller reads the ini, reports the
 * verdict to the log and does the patching, and this decides only what the verdict is.
 * See unittests/texture_size.c.
 *
 * None of this repairs anything, and it is careful not to overstate why. No page the game ships
 * exceeds either limit, so raising them changes nothing that is drawn; that is not the same as
 * writing nothing, because the shipped setting asks for 1024 and the clamp really is rewritten.
 * It simply has no artwork to act on. The validation exists for replacement artwork, which reaches surface creation
 * unchanged and is cropped without a word when it is too large for the clamp.
 */
#ifndef TEXTURE_SIZE_H
#define TEXTURE_SIZE_H

#include <stdint.h>

/* The range MaxTextureSize accepts, in pixels per axis. The floor is the engine's own ceiling, the
 * 0x100 in the immediate at 0x00488758, because going below it would crop artwork the game already
 * ships. The roof is what a Direct3D 9 class device is certain to manage. */
#define TEXTURE_SIZE_MIN_CEILING     0x100u
#define TEXTURE_SIZE_MAX_CEILING     0x2000u

/* The range MaxWorldPageSize accepts, in pixels per axis. The floor is the engine's own, the axis
 * that corresponds to the 0x10000 byte block pushed at 0x0041DAB6, because a smaller block would
 * be too small for the pages the game already ships and would corrupt the heap on the first level
 * load. The roof keeps the transient allocation at 16 MB. */
#define TEXTURE_SIZE_MIN_WORLD_PAGE  0x100u
#define TEXTURE_SIZE_MAX_WORLD_PAGE  0x1000u

/* What may be done with a requested size. The tests run in the order these are listed, so a value
 * outside the range is reported as out of range even when it is also not a power of two. */
typedef enum texture_size_verdict {
    TEXTURE_SIZE_APPLY = 0,        /* usable, and not what the engine's instruction already holds */
    TEXTURE_SIZE_UNCHANGED,        /* usable, and exactly what it already holds                   */
    TEXTURE_SIZE_BELOW_MINIMUM,
    TEXTURE_SIZE_ABOVE_MAXIMUM,
    TEXTURE_SIZE_NOT_POWER_OF_TWO
} texture_size_verdict_t;

/* `requested` is signed because it comes from an ini file and an ini file can say anything,
 * including a negative number. Comparing it signed is what stops -1 from arriving as four billion.
 *
 * `original` is the value the engine's own instruction holds. Asking for exactly that is
 * TEXTURE_SIZE_UNCHANGED rather than an error, because there is nothing to write. `low` and `high`
 * are inclusive and both have to fit in int32_t.
 *
 * A power of two is required rather than preferred, and it is required here because the device
 * never asks for it: D3DPTEXTURECAPS_POW2 is a capability this engine does not check. The accepted
 * size is handed straight to surface creation, and bapmat_buildPage at 0x0040EC33 stores
 * widthMinus1 and heightMinus1 and uses them as addressing masks, which is the mask form of a power
 * of two and of nothing else. */
texture_size_verdict_t texture_size_check(int32_t requested, uint32_t original,
                                          uint32_t low, uint32_t high);

/* A world page axis as the byte count the level loader's one allocation wants. A world page is one
 * palette index per pixel, so the block is axis times axis bytes, and 256 gives the 65536 the
 * engine ships with. Pass an axis that texture_size_check has already accepted: the roof of that
 * range squares to 16 MB, which is well inside 32 bits, and nothing above 65535 could be squared
 * in 32 bits at all. */
uint32_t texture_size_world_page_bytes(uint32_t axis);

/* The ceiling that really crops an upload, given the ceiling this run applied and the engine's own.
 * An applied ceiling of zero means the clamp was left alone, either because the player asked for
 * the value it already holds or because the value was refused, and then the engine's own ceiling
 * still governs. The two sites are independent, so a player can grow the world page block on its
 * own and get pages that load and are then cropped on upload. This is the number that says so. */
uint32_t texture_size_effective_ceiling(uint32_t applied_ceiling, uint32_t original_ceiling);

#endif /* TEXTURE_SIZE_H */
