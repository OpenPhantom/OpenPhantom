#include "import_patch.h"

#include "patch.h"

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
            if ((FARPROC)(uintptr_t)entry->u1.Function != real) {
                continue;
            }
            /* Through the patch layer like every other write into the image: the slot is
             * required to hold the import it was matched on, the protection is put back, and the
             * new pointer is read back before this claims to have placed it. */
            if (patch_repoint_operand((uintptr_t)&entry->u1.Function,
                                      (uint32_t)(uintptr_t)real,
                                      (uint32_t)(uintptr_t)replacement) != PATCH_RESULT_OK) {
                return false;
            }
            if (out_original != NULL) {
                *out_original = (void *)(uintptr_t)real;
            }
            return true;
        }
    }
    return false;
}
