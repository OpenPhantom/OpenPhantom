/* patch.h: every write into engine code or engine data goes through here.
 *
 * The three habits this module makes cheap, because they were expensive to learn:
 *
 *   1. VALIDATE BEFORE WRITING. patch_repoint_operand() reads the current value and refuses when
 *      it is not what the caller expected. That single check is also what makes a patch
 *      idempotent: a second run finds the new value, not the expected old one, and declines.
 *
 *   2. write the whole WORD, not a BYTE of it. A review once proposed poking one byte of the
 *      `cmp eax,0x2000` immediate at 0x4064B9 from 0x20 to 0x1F. The low byte of that
 *      little-endian immediate is 0x00; the poke would have produced `cmp eax,0x201F`, the
 *      limit RAISED and the overflow guaranteed.
 *
 *   3. read the target of a CALL, do not assume it. patch_read_call_target() checks the E8 opcode
 *      before it believes the displacement.
 */
#ifndef COMMON_PATCH_H
#define COMMON_PATCH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum patch_result {
    PATCH_RESULT_OK,
    PATCH_RESULT_INVALID_ARGUMENT,
    PATCH_RESULT_UNSUPPORTED_BUILD,
    PATCH_RESULT_UNEXPECTED_BYTES,
    PATCH_RESULT_PROTECTION_FAILED,
    PATCH_RESULT_WRITE_FAILED
} patch_result_t;

const char *patch_result_text(patch_result_t result);

/* True when the bytes at `address` equal `expected_bytes`. Refuses (false) when the range is not
 * readable rather than faulting. */
bool patch_validate_bytes(uintptr_t address, const uint8_t *expected_bytes, size_t size);

patch_result_t patch_write_bytes(uintptr_t address, const void *data, size_t size);
patch_result_t patch_write_u8 (uintptr_t address, uint8_t  value);
patch_result_t patch_write_u32(uintptr_t address, uint32_t value);
patch_result_t patch_write_f32(uintptr_t address, float    value);
patch_result_t patch_write_pointer32(uintptr_t address, const void *pointer);

/* Rewrites a 32-bit absolute memory operand, the address field of an instruction, not the
 * instruction. Fails with PATCH_RESULT_UNEXPECTED_BYTES when the operand does not currently hold
 * `expected_old`, and logs what it found. */
patch_result_t patch_repoint_operand(uintptr_t operand_address, uint32_t expected_old,
                                     uint32_t new_value);

/* `call_address` must point at the 0xE8 opcode. Returns false when it does not, or when the
 * computed target leaves the host image. */
bool patch_read_call_target(uintptr_t call_address, uintptr_t *out_target);

/* Redirects an existing `call rel32` to `new_target`, keeping the E8. The opcode is verified
 * first. This is how a single call site is diverted without touching the callee, which matters
 * whenever the callee has other callers that must stay untouched. */
patch_result_t patch_redirect_call(uintptr_t call_address, const void *new_target);

#endif /* COMMON_PATCH_H */
