#include "patch.h"

#include "logging.h"
#include "memory.h"

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define CALL_REL32_OPCODE 0xE8u
#define CALL_REL32_LENGTH 5u

const char *patch_result_text(patch_result_t result)
{
    switch (result) {
    case PATCH_RESULT_OK:                return "ok";
    case PATCH_RESULT_INVALID_ARGUMENT:  return "invalid argument";
    case PATCH_RESULT_UNSUPPORTED_BUILD: return "unsupported build";
    case PATCH_RESULT_UNEXPECTED_BYTES:  return "unexpected bytes";
    case PATCH_RESULT_PROTECTION_FAILED: return "page protection could not be changed";
    case PATCH_RESULT_WRITE_FAILED:      return "write failed";
    default:                             return "?";
    }
}

bool patch_validate_bytes(uintptr_t address, const uint8_t *expected_bytes, size_t size)
{
    if (expected_bytes == NULL || size == 0) {
        return false;
    }
    if (!memory_is_readable_range(address, size)) {
        return false;
    }
    return memcmp((const void *)address, expected_bytes, size) == 0;
}

patch_result_t patch_write_bytes(uintptr_t address, const void *data, size_t size)
{
    /* A failed RESTORE is not a failed write, the bytes are in place and the caller's patch is
     * live, but it leaves the page writable and executable for the rest of the run, and a page
     * that silently stays RWX is exactly the kind of state nobody discovers until it matters.
     * Reported once per process: a flood would be a second defect, and the first occurrence
     * carries all the information the next one would. */
    static bool warned_about_restore;

    DWORD previous_protection;
    DWORD restored_protection;

    if (data == NULL || size == 0) {
        return PATCH_RESULT_INVALID_ARGUMENT;
    }

    if (!VirtualProtect((LPVOID)address, size, PAGE_EXECUTE_READWRITE, &previous_protection)) {
        return PATCH_RESULT_PROTECTION_FAILED;
    }

    memcpy((void *)address, data, size);

    if (!VirtualProtect((LPVOID)address, size, previous_protection, &restored_protection)
        && !warned_about_restore) {
        warned_about_restore = true;
        log_warning("the %u byte(s) at %08X were written, but the page protection could not be "
                    "restored to %08X (error %lu). It stays writable and executable. Further "
                    "occurrences are not reported.",
                    (unsigned)size, (unsigned)address, (unsigned)previous_protection,
                    (unsigned long)GetLastError());
    }
    FlushInstructionCache(GetCurrentProcess(), (LPCVOID)address, size);

    return PATCH_RESULT_OK;
}

patch_result_t patch_write_u8(uintptr_t address, uint8_t value)
{
    return patch_write_bytes(address, &value, sizeof(value));
}

patch_result_t patch_write_u32(uintptr_t address, uint32_t value)
{
    return patch_write_bytes(address, &value, sizeof(value));
}

patch_result_t patch_write_f32(uintptr_t address, float value)
{
    return patch_write_bytes(address, &value, sizeof(value));
}

patch_result_t patch_write_pointer32(uintptr_t address, const void *pointer)
{
    uint32_t value = (uint32_t)(uintptr_t)pointer;
    return patch_write_bytes(address, &value, sizeof(value));
}

patch_result_t patch_repoint_operand(uintptr_t operand_address, uint32_t expected_old,
                                     uint32_t new_value)
{
    uint32_t current;

    if (operand_address == 0) {
        return PATCH_RESULT_INVALID_ARGUMENT;
    }
    if (!memory_read_u32(operand_address, &current)) {
        log_warning("operand at %08X is not readable, refused", (unsigned)operand_address);
        return PATCH_RESULT_UNEXPECTED_BYTES;
    }
    if (current != expected_old) {
        log_warning("operand at %08X holds %08X, expected %08X - refused",
                    (unsigned)operand_address, (unsigned)current, (unsigned)expected_old);
        return PATCH_RESULT_UNEXPECTED_BYTES;
    }

    return patch_write_u32(operand_address, new_value);
}

bool patch_read_call_target(uintptr_t call_address, uintptr_t *out_target)
{
    uint8_t   opcode;
    uint32_t  displacement;
    uintptr_t target;

    if (out_target == NULL) {
        return false;
    }
    if (!memory_read_u8(call_address, &opcode) || opcode != CALL_REL32_OPCODE) {
        return false;
    }
    if (!memory_read_u32(call_address + 1, &displacement)) {
        return false;
    }

    target = call_address + CALL_REL32_LENGTH + displacement;
    if (!memory_is_inside_image(target, 1)) {
        return false;
    }

    *out_target = target;
    return true;
}

patch_result_t patch_redirect_call(uintptr_t call_address, const void *new_target)
{
    uint8_t  opcode;
    uint32_t displacement;

    if (new_target == NULL) {
        return PATCH_RESULT_INVALID_ARGUMENT;
    }
    if (!memory_read_u8(call_address, &opcode)) {
        return PATCH_RESULT_UNEXPECTED_BYTES;
    }
    if (opcode != CALL_REL32_OPCODE) {
        log_warning("no E8 at %08X (found %02X), call not redirected",
                    (unsigned)call_address, opcode);
        return PATCH_RESULT_UNEXPECTED_BYTES;
    }

    displacement = (uint32_t)((uintptr_t)new_target - (call_address + CALL_REL32_LENGTH));
    return patch_write_u32(call_address + 1, displacement);
}
