#include "import_patch.h"

#include <stdint.h>
#include <string.h>

bool import_patch_replace(const char *module_name, const char *imported_dll,
                          const char *function_name, void *replacement, void **out_original)
{
    HMODULE                  target;
    HMODULE                  owner;
    FARPROC                  real;
    uint8_t                 *base;
    IMAGE_DOS_HEADER        *dos;
    IMAGE_NT_HEADERS        *nt;
    IMAGE_DATA_DIRECTORY     directory;
    IMAGE_IMPORT_DESCRIPTOR *descriptor;

    if (imported_dll == NULL || function_name == NULL || replacement == NULL) {
        return false;
    }

    target = GetModuleHandleA(module_name);      /* NULL asks for the running executable */
    owner  = GetModuleHandleA(imported_dll);
    if (target == NULL || owner == NULL) {
        return false;
    }
    real = GetProcAddress(owner, function_name);
    if (real == NULL) {
        return false;
    }

    base = (uint8_t *)target;
    dos  = (IMAGE_DOS_HEADER *)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        return false;
    }
    nt = (IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) {
        return false;
    }

    directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (directory.VirtualAddress == 0 || directory.Size == 0) {
        return false;
    }

    descriptor = (IMAGE_IMPORT_DESCRIPTOR *)(base + directory.VirtualAddress);
    for (; descriptor->Name != 0; descriptor++) {
        IMAGE_THUNK_DATA *entry;

        if (_stricmp((const char *)(base + descriptor->Name), imported_dll) != 0) {
            continue;
        }

        entry = (IMAGE_THUNK_DATA *)(base + descriptor->FirstThunk);
        for (; entry->u1.Function != 0; entry++) {
            DWORD previous;

            if ((FARPROC)(uintptr_t)entry->u1.Function != real) {
                continue;
            }
            if (!VirtualProtect(&entry->u1.Function, sizeof entry->u1.Function,
                                PAGE_READWRITE, &previous)) {
                return false;
            }
            if (out_original != NULL) {
                *out_original = (void *)(uintptr_t)entry->u1.Function;
            }
            entry->u1.Function = (DWORD)(uintptr_t)replacement;
            (void)VirtualProtect(&entry->u1.Function, sizeof entry->u1.Function,
                                 previous, &previous);
            return true;
        }
    }
    return false;
}
