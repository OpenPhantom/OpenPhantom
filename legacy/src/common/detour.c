#include "detour.h"

#include "logging.h"
#include "memory.h"
#include "patch.h"

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define JMP_REL32_OPCODE 0xE9u
#define JMP_REL32_LENGTH 5u
#define NOP_OPCODE       0x90u

#define DETOUR_PROLOGUE_MIN 5u
#define DETOUR_PROLOGUE_MAX 16u
#define TRAMPOLINE_BYTES    64u

static uint32_t relative_displacement(uintptr_t from_end, uintptr_t to)
{
    return (uint32_t)(to - from_end);
}

/* The target already carries our (or somebody's) `jmp rel32`: link in front of it. */
static bool chain_onto_existing_hook(detour_t *detour, uintptr_t target, const void *hook)
{
    uint32_t  displacement;
    uintptr_t previous_hook;
    uint8_t   patch_bytes[JMP_REL32_LENGTH];

    if (!memory_read_u32(target + 1, &displacement)) {
        log_error("detour: %08X starts with E9 but its displacement is unreadable, refused",
                  (unsigned)target);
        return false;
    }

    previous_hook = target + JMP_REL32_LENGTH + displacement;

    /* A branch target that is not executable memory means we misread the branch. Refusing is the
     * only safe answer: keeping it as `original` would send the engine to an address we cannot
     * account for, on the first call rather than at install time. The target normally lives in
     * another mod's DLL, so this is an executability test and not an image test. */
    if (!memory_is_executable_range(previous_hook, 1)) {
        log_error("detour: %08X jumps to %08X, which is not executable, refused",
                  (unsigned)target, (unsigned)previous_hook);
        return false;
    }

    patch_bytes[0] = JMP_REL32_OPCODE;
    displacement = relative_displacement(target + JMP_REL32_LENGTH, (uintptr_t)hook);
    memcpy(patch_bytes + 1, &displacement, sizeof(displacement));

    if (patch_write_bytes(target, patch_bytes, sizeof(patch_bytes)) != PATCH_RESULT_OK) {
        log_error("detour: could not rewrite the branch at %08X", (unsigned)target);
        return false;
    }

    detour->target        = target;
    detour->original      = (void *)previous_hook;
    detour->prologue_size = 0;
    detour->installed     = true;
    detour->chained       = true;

    log_info("detour: chained in front of an existing hook at %08X (previous hook %08X)",
             (unsigned)target, (unsigned)previous_hook);
    return true;
}

static uint8_t *build_trampoline(uintptr_t target, size_t prologue_size)
{
    uint8_t *trampoline;
    uint32_t displacement;

    trampoline = (uint8_t *)VirtualAlloc(NULL, TRAMPOLINE_BYTES, MEM_COMMIT | MEM_RESERVE,
                                         PAGE_EXECUTE_READWRITE);
    if (trampoline == NULL) {
        return NULL;
    }

    memcpy(trampoline, (const void *)target, prologue_size);
    trampoline[prologue_size] = JMP_REL32_OPCODE;

    displacement = relative_displacement((uintptr_t)trampoline + prologue_size + JMP_REL32_LENGTH,
                                         target + prologue_size);
    memcpy(trampoline + prologue_size + 1, &displacement, sizeof(displacement));

    FlushInstructionCache(GetCurrentProcess(), trampoline, TRAMPOLINE_BYTES);
    return trampoline;
}

bool detour_install(detour_t *detour, uintptr_t target, const void *hook, size_t prologue_size)
{
    uint8_t  patch_bytes[DETOUR_PROLOGUE_MAX];
    uint8_t *trampoline;
    uint8_t  first_byte;
    uint32_t displacement;
    size_t   index;

    if (detour == NULL || target == 0 || hook == NULL) {
        return false;
    }
    if (detour->installed) {
        log_warning("detour: %08X is already installed by this feature, ignored",
                    (unsigned)detour->target);
        return true;                               /* idempotent, not an error */
    }
    if (prologue_size < DETOUR_PROLOGUE_MIN || prologue_size > DETOUR_PROLOGUE_MAX) {
        log_error("detour: prologue size %u at %08X is out of range",
                  (unsigned)prologue_size, (unsigned)target);
        return false;
    }
    if (!memory_is_readable_range(target, prologue_size)) {
        log_error("detour: the prologue at %08X is not readable, refused", (unsigned)target);
        return false;
    }

    if (!memory_read_u8(target, &first_byte)) {
        return false;
    }
    if (first_byte == JMP_REL32_OPCODE) {
        return chain_onto_existing_hook(detour, target, hook);
    }

    trampoline = build_trampoline(target, prologue_size);
    if (trampoline == NULL) {
        log_error("detour: no memory for the trampoline of %08X", (unsigned)target);
        return false;
    }

    patch_bytes[0] = JMP_REL32_OPCODE;
    displacement = relative_displacement(target + JMP_REL32_LENGTH, (uintptr_t)hook);
    memcpy(patch_bytes + 1, &displacement, sizeof(displacement));
    for (index = JMP_REL32_LENGTH; index < prologue_size; ++index) {
        patch_bytes[index] = NOP_OPCODE;           /* pad to the instruction boundary */
    }

    if (patch_write_bytes(target, patch_bytes, prologue_size) != PATCH_RESULT_OK) {
        VirtualFree(trampoline, 0, MEM_RELEASE);
        log_error("detour: could not write the branch at %08X", (unsigned)target);
        return false;
    }

    detour->target        = target;
    detour->original      = trampoline;
    detour->prologue_size = prologue_size;
    detour->installed     = true;
    detour->chained       = false;
    return true;
}
