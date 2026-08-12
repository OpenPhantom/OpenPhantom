/* memory.h: range checks and page protection, so no feature has to call VirtualProtect itself.
 *
 * The rule these functions exist to enforce: validate the COMPLETE range you are about to touch.
 * Checking only the first address is not enough when reading a structure or an array, an engine
 * table can start in committed memory and end past the last committed page, and the read that
 * finds out is the one that kills the process.
 */
#ifndef COMMON_MEMORY_H
#define COMMON_MEMORY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* True when [address, address+size) is committed and not PAGE_NOACCESS / PAGE_GUARD.
 * Spans several regions correctly; it walks them. */
bool memory_is_readable_range(uintptr_t address, size_t size);

/* True when [address, address+size) lies inside the host executable's image. */
bool memory_is_inside_image(uintptr_t address, size_t size);

/* True when the range is readable AND every page of it is executable. This is the check a branch
 * target needs, and it is deliberately not `memory_is_inside_image`: a chained detour's previous
 * hook is a function in another mod's DLL, which is outside the host image by construction, so an
 * image test would refuse exactly the case chaining exists for. */
bool memory_is_executable_range(uintptr_t address, size_t size);

/* Makes a range writable and executable and leaves it that way. Only for the handful of sites
 * that are rewritten repeatedly at run time (the per-frame camera immediates). Everything else
 * must go through patch_write_*, which restores the original protection. */
bool memory_make_writable(uintptr_t address, size_t size);

/* Reads that refuse rather than fault. */
bool memory_read(uintptr_t address, void *destination, size_t size);
bool memory_read_u8 (uintptr_t address, uint8_t  *out);
bool memory_read_u32(uintptr_t address, uint32_t *out);

/* The same guarantee for code that runs once per drawn object rather than once at install.
 *
 * Everything above asks the operating system whether the range is readable, and that is a system
 * call on every call. At install time nobody can measure it. On the drawing path it is the wrong
 * shape entirely: a hook that runs for every emitter, every decal fan and every model in view pays
 * a syscall per read, and the cost is fixed per frame, so it stays invisible while frames are long
 * and becomes a material share of the frame as soon as they are short.
 *
 * These do the read directly and catch the fault instead of asking permission first. A bad pointer
 * costs one exception, which is expensive but only happens when the alternative would have been a
 * refusal anyway; a good pointer costs the read and nothing else. Use them where the pointer comes
 * from the engine at run time and the code runs per object. Keep the asking form for install-time
 * validation, where being told WHY a range is unusable is worth more than the nanoseconds. */
bool memory_try_read(uintptr_t address, void *destination, size_t size);
bool memory_try_readable(uintptr_t address, size_t size);

#endif /* COMMON_MEMORY_H */
