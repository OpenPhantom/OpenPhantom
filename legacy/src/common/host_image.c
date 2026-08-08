#include "host_image.h"

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct host_image_state {
    bool      resolved;
    uintptr_t base;
    uintptr_t end;
    uintptr_t text;
    size_t    text_size;
    char      directory[MAX_PATH];
} host_image_state_t;

static host_image_state_t host_state;

static void resolve_directory(void)
{
    char  path[MAX_PATH];
    char *last_separator;

    host_state.directory[0] = '\0';

    if (GetModuleFileNameA(NULL, path, MAX_PATH) == 0) {
        return;
    }
    path[MAX_PATH - 1] = '\0';

    last_separator = strrchr(path, '\\');
    if (last_separator == NULL) {
        return;
    }
    *(last_separator + 1) = '\0';

    strncpy(host_state.directory, path, sizeof(host_state.directory) - 1);
    host_state.directory[sizeof(host_state.directory) - 1] = '\0';
}

static bool resolve_sections(HMODULE module)
{
    IMAGE_DOS_HEADER     *dos_header;
    IMAGE_NT_HEADERS     *nt_headers;
    IMAGE_SECTION_HEADER *section;
    unsigned int          index;

    dos_header = (IMAGE_DOS_HEADER *)module;
    if (dos_header->e_magic != IMAGE_DOS_SIGNATURE) {
        return false;
    }

    nt_headers = (IMAGE_NT_HEADERS *)((uint8_t *)module + dos_header->e_lfanew);
    if (nt_headers->Signature != IMAGE_NT_SIGNATURE) {
        return false;
    }

    /* Refusing anything but i386 is deliberate: every offset in this project is 32-bit. */
    if (nt_headers->FileHeader.Machine != IMAGE_FILE_MACHINE_I386) {
        return false;
    }

    host_state.base = (uintptr_t)module;
    host_state.end  = host_state.base + nt_headers->OptionalHeader.SizeOfImage;

    section = IMAGE_FIRST_SECTION(nt_headers);
    for (index = 0; index < nt_headers->FileHeader.NumberOfSections; ++index, ++section) {
        if ((section->Characteristics & IMAGE_SCN_CNT_CODE) == 0) {
            continue;
        }
        host_state.text = host_state.base + section->VirtualAddress;
        host_state.text_size = (section->Misc.VirtualSize != 0)
                             ? section->Misc.VirtualSize
                             : section->SizeOfRawData;
        return true;
    }

    return false;
}

bool host_image_resolve(void)
{
    HMODULE module;

    if (host_state.resolved) {
        return true;
    }

    resolve_directory();

    module = GetModuleHandleA(NULL);
    if (module == NULL) {
        return false;
    }
    if (!resolve_sections(module)) {
        return false;
    }

    host_state.resolved = true;
    return true;
}

uintptr_t host_image_base(void)      { return host_state.base; }
uintptr_t host_image_end(void)       { return host_state.end; }
uintptr_t host_image_text(void)      { return host_state.text; }
size_t    host_image_text_size(void) { return host_state.text_size; }

const char *host_directory(void)
{
    if (host_state.directory[0] == '\0') {
        resolve_directory();
    }
    return host_state.directory;
}
