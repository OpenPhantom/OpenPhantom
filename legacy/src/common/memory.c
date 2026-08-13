#include "memory.h"

#include "host_image.h"

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define UNREADABLE_PROTECTION (PAGE_NOACCESS | PAGE_GUARD)

/* The four protection constants that carry execute rights. PAGE_GUARD and PAGE_NOACCESS are
 * separate bits that can accompany them, so the executable set is matched after masking those off
 * rather than by a bit test. PAGE_EXECUTE_READ is 0x20, not a flag combination. */
#define EXECUTABLE_PROTECTION_MASK (PAGE_EXECUTE | PAGE_EXECUTE_READ \
                                    | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)

bool memory_is_readable_range(uintptr_t address, size_t size)
{
    MEMORY_BASIC_INFORMATION information;
    uintptr_t                 cursor;
    uintptr_t                 end;

    if (size == 0) {
        return false;
    }

    end = address + size;
    if (end < address) {
        return false;                              /* wrapped: the caller computed nonsense */
    }

    /* One region at a time. A single VirtualQuery only describes the region the START address
     * falls into, which is exactly the check that misses a table running off its last page. */
    for (cursor = address; cursor < end; cursor = (uintptr_t)information.BaseAddress
                                                 + information.RegionSize) {
        if (VirtualQuery((LPCVOID)cursor, &information, sizeof(information))
            != sizeof(information)) {
            return false;
        }
        if (information.State != MEM_COMMIT) {
            return false;
        }
        if ((information.Protect & UNREADABLE_PROTECTION) != 0) {
            return false;
        }
    }

    return true;
}

bool memory_is_executable_range(uintptr_t address, size_t size)
{
    MEMORY_BASIC_INFORMATION information;
    uintptr_t                 cursor;
    uintptr_t                 end;

    if (size == 0) {
        return false;
    }

    end = address + size;
    if (end < address) {
        return false;
    }

    /* Region by region, for the same reason the readable check walks them: a branch target may sit
     * one byte before the end of an executable region whose neighbour is data. */
    for (cursor = address; cursor < end; cursor = (uintptr_t)information.BaseAddress
                                                 + information.RegionSize) {
        DWORD protection;

        if (VirtualQuery((LPCVOID)cursor, &information, sizeof(information))
            != sizeof(information)) {
            return false;
        }
        if (information.State != MEM_COMMIT) {
            return false;
        }
        if ((information.Protect & UNREADABLE_PROTECTION) != 0) {
            return false;
        }

        protection = information.Protect & ~(DWORD)UNREADABLE_PROTECTION;
        if ((protection & EXECUTABLE_PROTECTION_MASK) == 0) {
            return false;
        }
    }

    return true;
}

bool memory_is_inside_image(uintptr_t address, size_t size)
{
    uintptr_t end;

    if (size == 0) {
        return false;
    }

    end = address + size;
    if (end < address) {
        return false;
    }

    return address >= host_image_base() && end <= host_image_end();
}

bool memory_make_writable(uintptr_t address, size_t size)
{
    DWORD previous_protection;

    if (size == 0) {
        return false;
    }

    return VirtualProtect((LPVOID)address, size, PAGE_EXECUTE_READWRITE, &previous_protection)
           != 0;
}

bool memory_read(uintptr_t address, void *destination, size_t size)
{
    if (destination == NULL || !memory_is_readable_range(address, size)) {
        return false;
    }
    memcpy(destination, (const void *)address, size);
    return true;
}

bool memory_read_u8(uintptr_t address, uint8_t *out)
{
    return memory_read(address, out, sizeof(*out));
}

bool memory_read_u32(uintptr_t address, uint32_t *out)
{
    return memory_read(address, out, sizeof(*out));
}

/* The structured exception handler is the whole point of these two, so they must not be folded back
 * into the functions above: a __try frame around a memcpy is a few instructions of setup on x86,
 * while VirtualQuery is a system call, and the difference only matters where these are used.
 *
 * EXCEPTION_EXECUTE_HANDLER rather than a filter that inspects the record, because the only reason
 * to be here is a read that would have been refused anyway. Nothing else in this process is allowed
 * to be swallowed: the range is one caller's read of one structure, so a fault inside it cannot be
 * anybody else's fault being hidden. */
bool memory_try_read(uintptr_t address, void *destination, size_t size)
{
    if (destination == NULL || size == 0) {
        return false;
    }
    if (address + size < address) {
        return false;                              /* wrapped: the caller computed nonsense */
    }

    __try {
        memcpy(destination, (const void *)address, size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

/* Touching the first and the last byte is the whole range for this purpose. A range that spans a
 * hole would have to be committed at both ends and not in the middle, which no allocator this
 * engine uses produces, and the caller is about to read the whole thing anyway, so a fault in the
 * middle lands in ITS handler rather than here. That is a deliberate difference from the asking
 * form above, which walks every region because it is used to validate patch sites where a hole is a
 * real possibility and a fault is not acceptable. */
bool memory_try_readable(uintptr_t address, size_t size)
{
    volatile uint8_t sink;

    if (size == 0 || address + size < address) {
        return false;
    }

    __try {
        sink = *(const volatile uint8_t *)address;
        sink = *(const volatile uint8_t *)(address + size - 1u);
        (void)sink;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
