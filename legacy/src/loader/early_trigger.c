#include "early_trigger.h"

#include "mod_loader.h"

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define JMP_REL32_OPCODE 0xE9u
#define JMP_REL32_LENGTH 5u

typedef struct early_trigger_state {
    uintptr_t entry_point;
    uint8_t   original_bytes[JMP_REL32_LENGTH];
    bool      armed;
    bool      fired;
} early_trigger_state_t;

static early_trigger_state_t trigger_state;

/* Deliberately not using common/patch.c: that logs, and nothing may log from DllMain before the
 * log file has been decided. This writes five bytes and restores the protection, nothing else. */
static bool write_raw(uintptr_t address, const void *data, size_t size)
{
    DWORD previous;
    DWORD unused;

    if (!VirtualProtect((LPVOID)address, size, PAGE_EXECUTE_READWRITE, &previous)) {
        return false;
    }
    memcpy((void *)address, data, size);
    VirtualProtect((LPVOID)address, size, previous, &unused);
    FlushInstructionCache(GetCurrentProcess(), (LPCVOID)address, size);
    return true;
}

/* Runs at the host's entry point, with the loader lock released and one thread in the process.
 * Puts the original bytes back first, so the entry point can be re-executed from its first byte
 * and no instruction boundary has to be known. */
static void __cdecl restore_and_load(void)
{
    if (trigger_state.fired) {
        return;
    }
    trigger_state.fired = true;

    write_raw(trigger_state.entry_point, trigger_state.original_bytes,
              sizeof(trigger_state.original_bytes));
    mod_loader_run_once();
}

static void __declspec(naked) entry_point_stub(void)
{
    __asm {
        pushad
        pushfd
        call restore_and_load
        popfd
        popad
        jmp  dword ptr [trigger_state.entry_point]
    }
}

static uintptr_t host_entry_point(void)
{
    HMODULE               module;
    IMAGE_DOS_HEADER     *dos_header;
    IMAGE_NT_HEADERS     *nt_headers;

    module = GetModuleHandleA(NULL);
    if (module == NULL) {
        return 0;
    }

    dos_header = (IMAGE_DOS_HEADER *)module;
    if (dos_header->e_magic != IMAGE_DOS_SIGNATURE) {
        return 0;
    }
    nt_headers = (IMAGE_NT_HEADERS *)((uint8_t *)module + dos_header->e_lfanew);
    if (nt_headers->Signature != IMAGE_NT_SIGNATURE) {
        return 0;
    }
    if (nt_headers->FileHeader.Machine != IMAGE_FILE_MACHINE_I386) {
        return 0;
    }
    if (nt_headers->OptionalHeader.AddressOfEntryPoint == 0) {
        return 0;
    }

    return (uintptr_t)module + nt_headers->OptionalHeader.AddressOfEntryPoint;
}

bool early_trigger_arm(void)
{
    uint8_t  branch[JMP_REL32_LENGTH];
    uint32_t displacement;

    if (trigger_state.armed) {
        return true;
    }

    trigger_state.entry_point = host_entry_point();
    if (trigger_state.entry_point == 0) {
        return false;
    }

    memcpy(trigger_state.original_bytes, (const void *)trigger_state.entry_point,
           sizeof(trigger_state.original_bytes));

    /* Somebody else has already redirected the entry point. Chaining onto that would mean
     * guessing what they intend to do with it; the DirectInputCreateA fallback is the honest
     * answer instead. */
    if (trigger_state.original_bytes[0] == JMP_REL32_OPCODE) {
        return false;
    }

    branch[0] = JMP_REL32_OPCODE;
    displacement = (uint32_t)((uintptr_t)entry_point_stub
                              - (trigger_state.entry_point + JMP_REL32_LENGTH));
    memcpy(branch + 1, &displacement, sizeof(displacement));

    if (!write_raw(trigger_state.entry_point, branch, sizeof(branch))) {
        return false;
    }

    trigger_state.armed = true;
    return true;
}
