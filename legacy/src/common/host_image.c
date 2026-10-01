#include "host_image.h"

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Five in every shipped build of this engine. The bound exists so that a file claiming an absurd
 * section count is refused rather than read into a fixed array. */
#define MAX_FILE_SECTIONS 32u

typedef struct host_file_state {
    bool                 probed;   /* the file has been looked at, successfully or not */
    bool                 usable;
    uint32_t             image_base;
    size_t               section_count;
    host_image_section_t sections[MAX_FILE_SECTIONS];
} host_file_state_t;

typedef struct host_image_state {
    bool      resolved;
    uintptr_t base;
    uintptr_t end;
    uintptr_t text;
    size_t    text_size;
    char      path[MAX_PATH];
    char      directory[MAX_PATH];
} host_image_state_t;

static host_image_state_t host_state;
static host_file_state_t  file_state;

static void resolve_directory(void)
{
    char *last_separator;

    host_state.path[0] = '\0';
    host_state.directory[0] = '\0';

    if (GetModuleFileNameA(NULL, host_state.path, MAX_PATH) == 0) {
        return;
    }
    host_state.path[MAX_PATH - 1] = '\0';

    strncpy(host_state.directory, host_state.path, sizeof(host_state.directory) - 1);
    host_state.directory[sizeof(host_state.directory) - 1] = '\0';

    last_separator = strrchr(host_state.directory, '\\');
    if (last_separator == NULL) {
        host_state.directory[0] = '\0';
        return;
    }
    *(last_separator + 1) = '\0';
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

/* ==============================================================================================
 * The file the process was loaded from
 * ============================================================================================ */

/* The handle is opened and closed around every read rather than held. This runs a handful of times
 * at installation, and a static library linked into every feature DLL and the loader would
 * otherwise hold one handle per DLL on the player's executable for the life of the process. */
static bool read_file_at(size_t offset, void *destination, size_t size)
{
    HANDLE  file;
    DWORD   moved;
    DWORD   got = 0;
    BOOL    ok;

    /* The seek takes a signed distance with no high word, so an offset that does not fit one is
     * refused here rather than turned into a negative position by the cast below. */
    if (host_state.path[0] == '\0' || destination == NULL || size == 0 || offset > 0x7FFFFFFFu) {
        return false;
    }

    /* The image is open in this very process, so anything less permissive than sharing all three
     * is refused by the file system rather than by us. */
    file = CreateFileA(host_state.path, GENERIC_READ,
                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }

    moved = SetFilePointer(file, (LONG)offset, NULL, FILE_BEGIN);
    if (moved == INVALID_SET_FILE_POINTER) {
        CloseHandle(file);
        return false;
    }

    ok = ReadFile(file, destination, (DWORD)size, &got, NULL);
    CloseHandle(file);

    return (ok != FALSE) && (got == (DWORD)size);
}

/* Parsed once and cached, the failure included: the probed flag is set before the first read, so
 * a file that cannot be parsed is not reparsed for every operand that asks. */
static bool load_file_layout(void)
{
    IMAGE_DOS_HEADER     dos_header;
    IMAGE_NT_HEADERS32   nt_headers;
    IMAGE_SECTION_HEADER section;
    size_t               table;
    unsigned int         index;

    if (file_state.probed) {
        return file_state.usable;
    }
    file_state.probed = true;

    if (!read_file_at(0, &dos_header, sizeof(dos_header)) ||
        dos_header.e_magic != IMAGE_DOS_SIGNATURE || dos_header.e_lfanew < 0) {
        return false;
    }
    if (!read_file_at((size_t)dos_header.e_lfanew, &nt_headers, sizeof(nt_headers)) ||
        nt_headers.Signature != IMAGE_NT_SIGNATURE ||
        nt_headers.FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
        nt_headers.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
        return false;
    }
    if (nt_headers.FileHeader.NumberOfSections == 0 ||
        nt_headers.FileHeader.NumberOfSections > MAX_FILE_SECTIONS) {
        return false;
    }

    /* The section table follows the optional header, whose size is declared rather than fixed. */
    table = (size_t)dos_header.e_lfanew + offsetof(IMAGE_NT_HEADERS32, OptionalHeader)
          + nt_headers.FileHeader.SizeOfOptionalHeader;

    for (index = 0; index < nt_headers.FileHeader.NumberOfSections; ++index) {
        if (!read_file_at(table + (size_t)index * sizeof(section), &section, sizeof(section))) {
            return false;
        }
        file_state.sections[index].virtual_address = section.VirtualAddress;
        file_state.sections[index].virtual_size    = section.Misc.VirtualSize;
        file_state.sections[index].raw_offset      = section.PointerToRawData;
        file_state.sections[index].raw_size        = section.SizeOfRawData;
    }

    file_state.section_count = nt_headers.FileHeader.NumberOfSections;
    file_state.image_base    = nt_headers.OptionalHeader.ImageBase;
    file_state.usable        = true;
    return true;
}

/* Containment is decided on the virtual size and the bound on the raw size, and the two differ in
 * both directions in the real files. The retail executable's code section is stored larger than
 * it is mapped, 0xA6A00 raw against 0xA6870 virtual, which is file alignment padding, and its
 * data section is mapped more than fifty times larger than it is stored, 0x416CF0 against
 * 0x13800, because nearly all of it is zero filled by the loader. Deciding containment on the raw
 * size would put addresses in the padding inside the code section; bounding the read by the
 * virtual size would read padding, or the next section's bytes, as if they were code. */
size_t host_image_file_offset(const host_image_section_t *sections, size_t count,
                              uint32_t rva, size_t size)
{
    size_t index;

    if (sections == NULL || size == 0) {
        return HOST_IMAGE_NO_FILE_OFFSET;
    }

    for (index = 0; index < count; ++index) {
        const host_image_section_t *section = &sections[index];
        uint32_t                    extent;
        uint32_t                    inside;

        extent = (section->virtual_size != 0) ? section->virtual_size : section->raw_size;
        if (extent == 0 || rva < section->virtual_address) {
            continue;
        }
        inside = rva - section->virtual_address;
        if (inside >= extent) {
            continue;
        }

        /* A section is mapped to its virtual size and stored to its raw size, and the difference
         * is zero filled at load time. An address in that difference exists in the process and
         * exists nowhere in the file, which is a refusal and not an offset of zero. */
        if (inside >= section->raw_size || size > (size_t)(section->raw_size - inside)) {
            return HOST_IMAGE_NO_FILE_OFFSET;
        }
        return (size_t)section->raw_offset + (size_t)inside;
    }

    return HOST_IMAGE_NO_FILE_OFFSET;
}

bool host_image_read_original(uintptr_t address, void *destination, size_t size)
{
    size_t offset;

    if (!host_state.resolved || destination == NULL || size == 0) {
        return false;
    }
    if (address < host_state.base || address >= host_state.end ||
        size > (size_t)(host_state.end - address)) {
        return false;
    }
    if (!load_file_layout()) {
        return false;
    }

    offset = host_image_file_offset(file_state.sections, file_state.section_count,
                                    (uint32_t)(address - host_state.base), size);
    if (offset == HOST_IMAGE_NO_FILE_OFFSET) {
        return false;
    }

    return read_file_at(offset, destination, size);
}

/* Answering zero when the file cannot be parsed would be a guess, so no caller may ask this
 * question first: every one of them reads bytes out of the file before it needs the delta, and
 * that read has already failed and been reported by the time this could answer wrongly. */
intptr_t host_image_relocation_delta(void)
{
    if (!host_state.resolved || !load_file_layout()) {
        return 0;
    }
    return (intptr_t)(host_state.base - (uintptr_t)file_state.image_base);
}

const char *host_directory(void)
{
    if (host_state.directory[0] == '\0') {
        resolve_directory();
    }
    return host_state.directory;
}
