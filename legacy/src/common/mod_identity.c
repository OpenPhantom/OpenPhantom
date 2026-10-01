/* mod_identity.c: which DLL in the mods folder is one of this release's own. See the header.
 *
 * The version block is walked by hand rather than through version.dll, which is not a known DLL:
 * a module that imported it would have it looked for in the game folder first, where an ASI loader
 * ships a DLL of that very name.
 */
#include "mod_identity.h"

#include "memory.h"

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The resource type a version block is filed under, RT_VERSION. The header spells it as a pointer
 * made from the number, and the walk below compares numbers. */
#define VERSION_RESOURCE_TYPE 16u

/* The one string table every mod of this project carries: language 0x0409, code page 1200. The
 * version resource template writes exactly this block, and a DLL whose strings stand under another
 * one was not built from it, which is all "foreign" means. */
#define OWN_STRING_TABLE "040904B0"

/* A resource directory is sixteen bytes and then its entries, eight bytes each: the name or number
 * of the entry, and where it leads. The high bit of the second word says the entry leads to another
 * directory rather than to a data entry. */
#define RESOURCE_DIRECTORY_BYTES   16u
#define RESOURCE_ENTRY_BYTES       8u
#define RESOURCE_DATA_ENTRY_BYTES  16u
#define RESOURCE_NAMED_COUNT_AT    12u
#define RESOURCE_ID_COUNT_AT       14u
#define RESOURCE_NAME_IS_STRING    0x80000000u
#define RESOURCE_LEADS_TO_DIRECTORY 0x80000000u

/* The three levels a resource sits at: its type, its name, its language. */
#define RESOURCE_LEVELS 3u

/* Every node of a version block starts with its length, the length of its value and its type,
 * two bytes each, and then its key as NUL terminated UTF-16. A type of 1 says the value is text
 * and its length counts characters; 0 says binary and it counts bytes. */
#define NODE_HEADER_BYTES 6u
#define NODE_TYPE_TEXT    1u

/* A handle from LoadLibraryEx for a data or image-resource mapping carries a mark in its two
 * lowest bits, and the image begins at the handle with those bits cleared; a caller may hand such
 * a handle in. */
#define HANDLE_MARK_BITS ((uintptr_t)3u)

/* The DLL suffix a name in the mods folder carries, which a name in [multiplayer] AllowMods may
 * leave out. */
#define DLL_SUFFIX        ".dll"
#define DLL_SUFFIX_LENGTH 4u

/* How far into the file the NT headers may start. The linker puts them right behind the DOS stub,
 * a few hundred bytes in; an offset past 64 KB is a file that only looks like an image, and it is
 * refused before its arithmetic can wrap. */
#define NT_HEADERS_REACH_MAX 0x10000u

/* What the reader sees: the image as mapped, and how far it is known to reach. Before
 * the headers are read that is only as far as they go; after, it is SizeOfImage. Every read goes
 * through `view_span`, so nothing outside the image and nothing uncommitted is ever touched. */
typedef struct image_view {
    const uint8_t *base;
    size_t         size;
} image_view_t;

/* One node of the version block, as offsets into the block. */
typedef struct version_node {
    size_t end;          /* one past the node, bounded by its parent */
    size_t key;          /* the first byte of the key */
    size_t value;        /* the first byte of the value */
    size_t value_end;    /* one past the value, bounded by the node */
    size_t children;     /* the first child, aligned */
} version_node_t;

/* The three strings the classification reads, as they stand in the block. */
typedef struct version_strings {
    char company[MOD_IDENTITY_NAME_MAX];
    char internal_name[MOD_IDENTITY_NAME_MAX];
    char file_version[MOD_IDENTITY_VERSION_MAX];
} version_strings_t;

/* ===================================== The pure half ====================================== */

static char ascii_lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

/* ASCII only and on purpose: the names compared here are file names this project gave, and a
 * comparison that followed the process's locale could answer differently on two machines. */
static bool same_ignoring_case(const char *a, const char *b)
{
    size_t i;

    for (i = 0; a[i] != '\0' || b[i] != '\0'; ++i) {
        if (ascii_lower(a[i]) != ascii_lower(b[i])) {
            return false;
        }
    }
    return true;
}

static bool is_empty(const char *text)
{
    return text == NULL || text[0] == '\0';
}

mod_identity_kind_t mod_identity_classify(const char *company, const char *internal_name,
                                          const char *file_stem, const char *file_version,
                                          const char *own_version)
{
    if (is_empty(company) || strcmp(company, MOD_IDENTITY_COMPANY) != 0) {
        return MOD_IDENTITY_FOREIGN;
    }
    if (is_empty(internal_name) || is_empty(file_stem) ||
        !same_ignoring_case(internal_name, file_stem)) {
        return MOD_IDENTITY_MISNAMED;
    }
    /* No number to hold it against is not a match. A caller without one learns that from the
     * answer rather than from a mod that was waved through. */
    if (is_empty(own_version) || is_empty(file_version) ||
        strcmp(file_version, own_version) != 0) {
        return MOD_IDENTITY_OTHER_RELEASE;
    }
    return MOD_IDENTITY_OWN;
}

static uint16_t get_u16(const uint8_t *at)
{
    return (uint16_t)(at[0] | (at[1] << 8));
}

static uint32_t get_u32(const uint8_t *at)
{
    return (uint32_t)at[0] | ((uint32_t)at[1] << 8) | ((uint32_t)at[2] << 16) |
           ((uint32_t)at[3] << 24);
}

/* Four byte alignment counted from the start of the block, which is how the resource compiler
 * pads between a key, its value and the children. The block itself starts aligned. */
static size_t align4(size_t offset)
{
    return (offset + 3u) & ~(size_t)3u;
}

static bool read_node(const uint8_t *block, size_t start, size_t limit, version_node_t *out)
{
    size_t length;
    size_t value_length;
    size_t at;

    if (start > limit || limit - start < NODE_HEADER_BYTES) {
        return false;
    }
    length = get_u16(block + start);
    if (length < NODE_HEADER_BYTES || length > limit - start) {
        return false;
    }
    out->end     = start + length;
    value_length = get_u16(block + start + 2u);
    if (get_u16(block + start + 4u) == NODE_TYPE_TEXT) {
        value_length *= 2u;
    }
    out->key = start + NODE_HEADER_BYTES;
    for (at = out->key; at + 2u <= out->end && get_u16(block + at) != 0u; at += 2u) {
    }
    if (at + 2u > out->end) {
        return false;   /* a key with no terminator inside its own node */
    }
    out->value = align4(at + 2u);
    if (out->value > out->end) {
        out->value = out->end;
    }
    /* Clamped rather than refused: some writers count a text value in bytes rather than in
     * characters, and the text reader stops at its terminator either way. */
    out->value_end = (value_length > out->end - out->value) ? out->end : out->value + value_length;
    out->children  = align4(out->value_end);
    if (out->children > out->end) {
        out->children = out->end;
    }
    return true;
}

static bool key_is(const uint8_t *block, const version_node_t *node, const char *name)
{
    size_t at = node->key;
    size_t i;

    for (i = 0; name[i] != '\0'; ++i, at += 2u) {
        uint16_t c = get_u16(block + at);

        if (c > 0x7Fu || ascii_lower((char)c) != ascii_lower(name[i])) {
            return false;
        }
    }
    return get_u16(block + at) == 0u;
}

static bool find_child(const uint8_t *block, const version_node_t *parent, const char *name,
                       version_node_t *out)
{
    size_t at = parent->children;

    while (at < parent->end && parent->end - at >= NODE_HEADER_BYTES) {
        if (!read_node(block, at, parent->end, out)) {
            return false;
        }
        if (key_is(block, out, name)) {
            return true;
        }
        at = align4(out->end);
    }
    return false;
}

/* A text value to ASCII, up to its terminator. Anything outside printable ASCII becomes a
 * question mark, which can never match a name or a number this project writes. */
static void copy_text(const uint8_t *block, const version_node_t *node, char *out, size_t capacity)
{
    size_t length = 0;
    size_t at;

    for (at = node->value; at + 2u <= node->value_end && length + 1u < capacity; at += 2u) {
        uint16_t c = get_u16(block + at);

        if (c == 0u) {
            break;
        }
        out[length++] = (c >= 0x20u && c < 0x7Fu) ? (char)c : '?';
    }
    out[length] = '\0';
}

/* The three strings out of a version block, each empty when the block does not hold it. A block
 * that does not parse holds none of them, which classifies as foreign. */
static void read_version_strings(const uint8_t *block, size_t size, version_strings_t *out)
{
    version_node_t root;
    version_node_t file_info;
    version_node_t table;
    version_node_t entry;

    memset(out, 0, sizeof *out);
    if (!read_node(block, 0u, size, &root) || !key_is(block, &root, "VS_VERSION_INFO") ||
        !find_child(block, &root, "StringFileInfo", &file_info) ||
        !find_child(block, &file_info, OWN_STRING_TABLE, &table)) {
        return;
    }
    if (find_child(block, &table, "CompanyName", &entry)) {
        copy_text(block, &entry, out->company, sizeof out->company);
    }
    if (find_child(block, &table, "InternalName", &entry)) {
        copy_text(block, &entry, out->internal_name, sizeof out->internal_name);
    }
    if (find_child(block, &table, "FileVersion", &entry)) {
        copy_text(block, &entry, out->file_version, sizeof out->file_version);
    }
}

/* ===================================== The image walk ===================================== */

static bool view_span(const image_view_t *view, size_t offset, size_t bytes)
{
    return bytes != 0u && offset <= view->size && bytes <= view->size - offset &&
           memory_is_readable_range((uintptr_t)(view->base + offset), bytes);
}

/* The headers: the time stamp, the size of the image and where its resources are. False when
 * they are not a PE image's, which is the one case a reader answers "unreadable". Both optional
 * header shapes are read, so a 64-bit DLL someone put into the folder is judged like any other
 * rather than refused as unreadable. */
static bool read_headers(image_view_t *view, mod_identity_t *out, uint32_t *resource_rva,
                         uint32_t *resource_size)
{
    size_t   nt;
    size_t   optional;
    size_t   directories;
    uint32_t directory_count;
    uint16_t magic;

    view->size = sizeof(IMAGE_DOS_HEADER);
    if (!view_span(view, 0u, sizeof(IMAGE_DOS_HEADER)) ||
        get_u16(view->base) != IMAGE_DOS_SIGNATURE) {
        return false;
    }
    nt = get_u32(view->base + offsetof(IMAGE_DOS_HEADER, e_lfanew));
    optional = nt + sizeof(uint32_t) + sizeof(IMAGE_FILE_HEADER);
    view->size = optional + sizeof(IMAGE_OPTIONAL_HEADER64);
    if (nt > NT_HEADERS_REACH_MAX || !view_span(view, nt, view->size - nt) ||
        get_u32(view->base + nt) != (uint32_t)IMAGE_NT_SIGNATURE) {
        return false;
    }
    magic = get_u16(view->base + optional);
    if (magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
        directory_count = get_u32(view->base + optional +
                                  offsetof(IMAGE_OPTIONAL_HEADER32, NumberOfRvaAndSizes));
        directories = optional + offsetof(IMAGE_OPTIONAL_HEADER32, DataDirectory);
    } else if (magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
        directory_count = get_u32(view->base + optional +
                                  offsetof(IMAGE_OPTIONAL_HEADER64, NumberOfRvaAndSizes));
        directories = optional + offsetof(IMAGE_OPTIONAL_HEADER64, DataDirectory);
    } else {
        return false;
    }
    /* SizeOfImage stands at the same place in both shapes. */
    out->time_stamp = get_u32(view->base + nt + sizeof(uint32_t) +
                              offsetof(IMAGE_FILE_HEADER, TimeDateStamp));
    out->image_size =
        get_u32(view->base + optional + offsetof(IMAGE_OPTIONAL_HEADER32, SizeOfImage));
    *resource_rva  = 0u;
    *resource_size = 0u;
    if (directory_count > (uint32_t)IMAGE_DIRECTORY_ENTRY_RESOURCE) {
        const uint8_t *entry = view->base + directories +
                               IMAGE_DIRECTORY_ENTRY_RESOURCE * sizeof(IMAGE_DATA_DIRECTORY);

        *resource_rva  = get_u32(entry);
        *resource_size = get_u32(entry + sizeof(uint32_t));
    }
    if (out->image_size < view->size) {
        return false;   /* an image that does not even hold its own headers */
    }
    view->size = out->image_size;
    return true;
}

/* One entry of a resource directory at `directory` (an offset from the resource root): the one
 * whose number is `wanted`, or the first one when `wanted` is 0. Its second word, or false. */
static bool directory_entry(const image_view_t *view, size_t root, size_t directory,
                            uint32_t wanted, uint32_t *leads_to)
{
    size_t   at = root + directory;
    uint32_t count;
    uint32_t i;

    if (!view_span(view, at, RESOURCE_DIRECTORY_BYTES)) {
        return false;
    }
    count = (uint32_t)get_u16(view->base + at + RESOURCE_NAMED_COUNT_AT) +
            get_u16(view->base + at + RESOURCE_ID_COUNT_AT);
    at += RESOURCE_DIRECTORY_BYTES;
    if (count == 0u || !view_span(view, at, (size_t)count * RESOURCE_ENTRY_BYTES)) {
        return false;
    }
    for (i = 0; i < count; ++i, at += RESOURCE_ENTRY_BYTES) {
        uint32_t name = get_u32(view->base + at);

        if (wanted == 0u || ((name & RESOURCE_NAME_IS_STRING) == 0u && name == wanted)) {
            *leads_to = get_u32(view->base + at + sizeof(uint32_t));
            return true;
        }
    }
    return false;
}

/* The version block of the image: its type, then the first name under it, then the first
 * language under that. A module carries one version resource, and which language the first one is
 * does not matter, because the string table inside it is what names the language. */
static bool find_version_block(const image_view_t *view, uint32_t resource_rva,
                               const uint8_t **block, size_t *size)
{
    size_t   directory = 0u;
    uint32_t leads_to = 0u;
    uint32_t level;
    size_t   entry;
    uint32_t data_rva;
    uint32_t data_size;

    if (resource_rva == 0u) {
        return false;
    }
    for (level = 0; level < RESOURCE_LEVELS; ++level) {
        if (!directory_entry(view, resource_rva, directory,
                             level == 0u ? VERSION_RESOURCE_TYPE : 0u, &leads_to)) {
            return false;
        }
        /* The first two levels lead to a directory, the last to the data entry. */
        if (((leads_to & RESOURCE_LEADS_TO_DIRECTORY) != 0u) != (level + 1u < RESOURCE_LEVELS)) {
            return false;
        }
        directory = leads_to & ~RESOURCE_LEADS_TO_DIRECTORY;
    }
    entry = (size_t)resource_rva + directory;
    if (!view_span(view, entry, RESOURCE_DATA_ENTRY_BYTES)) {
        return false;
    }
    data_rva  = get_u32(view->base + entry);
    data_size = get_u32(view->base + entry + sizeof(uint32_t));
    if (!view_span(view, data_rva, data_size)) {
        return false;
    }
    *block = view->base + data_rva;
    *size  = data_size;
    return true;
}

static void clear_identity(mod_identity_t *out)
{
    memset(out, 0, sizeof *out);
    out->kind = MOD_IDENTITY_UNREADABLE;
}

static void copy_bounded(char *out, size_t capacity, const char *text)
{
    size_t length = 0;

    while (text[length] != '\0' && length + 1u < capacity) {
        out[length] = text[length];
        ++length;
    }
    out[length] = '\0';
}

/* The image at `base` read and classified. False, with the kind left unreadable, when its headers
 * are not a PE image's; a readable image without a version block is foreign. */
static bool identity_of_image(const uint8_t *base, const char *file_stem, const char *own_version,
                              mod_identity_t *out)
{
    image_view_t      view = { base, 0u };
    version_strings_t strings;
    const uint8_t    *block = NULL;
    size_t            block_size = 0u;
    uint32_t          resource_rva = 0u;
    uint32_t          resource_size = 0u;

    clear_identity(out);
    if (base == NULL || !read_headers(&view, out, &resource_rva, &resource_size)) {
        out->time_stamp = 0u;
        out->image_size = 0u;
        return false;
    }
    memset(&strings, 0, sizeof strings);
    if (resource_size != 0u && find_version_block(&view, resource_rva, &block, &block_size)) {
        read_version_strings(block, block_size, &strings);
    }
    copy_bounded(out->internal_name, sizeof out->internal_name, strings.internal_name);
    copy_bounded(out->file_version, sizeof out->file_version, strings.file_version);
    out->kind = mod_identity_classify(strings.company, strings.internal_name, file_stem,
                                      strings.file_version, own_version);
    return true;
}

/* ===================================== The reader ========================================= */

bool mod_identity_of_module(const void *module, const char *file_stem, const char *own_version,
                            mod_identity_t *out)
{
    if (out == NULL) {
        return false;
    }
    return identity_of_image((const uint8_t *)((uintptr_t)module & ~HANDLE_MARK_BITS), file_stem,
                             own_version, out);
}

bool mod_identity_own_version(const void *address_inside, char *out, size_t capacity)
{
    HMODULE           module = NULL;
    image_view_t      view;
    mod_identity_t    headers;
    version_strings_t strings;
    const uint8_t    *block = NULL;
    size_t            block_size = 0u;
    uint32_t          resource_rva = 0u;
    uint32_t          resource_size = 0u;

    if (out == NULL || capacity == 0u) {
        return false;
    }
    out[0] = '\0';
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCSTR)address_inside, &module) ||
        module == NULL) {
        return false;
    }
    view.base = (const uint8_t *)module;
    view.size = 0u;
    if (!read_headers(&view, &headers, &resource_rva, &resource_size) || resource_size == 0u ||
        !find_version_block(&view, resource_rva, &block, &block_size)) {
        return false;
    }
    read_version_strings(block, block_size, &strings);
    copy_bounded(out, capacity, strings.file_version);
    return out[0] != '\0';
}

/* ===================================== The rule for AllowMods ============================= */

/* Whether the `length` characters at `run` are the `name_length` characters at `name`, ignoring
 * case. */
static bool same_run(const char *run, size_t length, const char *name, size_t name_length)
{
    size_t i;

    if (length != name_length) {
        return false;
    }
    for (i = 0; i < length; ++i) {
        if (ascii_lower(run[i]) != ascii_lower(name[i])) {
            return false;
        }
    }
    return true;
}

static bool is_blank(char c)
{
    return c == ' ' || c == '\t';
}

bool mod_identity_name_listed(const char *list, const char *file_name)
{
    size_t name_length;
    size_t stem_length;
    size_t at = 0;

    if (list == NULL || file_name == NULL || file_name[0] == '\0') {
        return false;
    }
    name_length = strlen(file_name);
    stem_length = name_length;
    if (name_length > DLL_SUFFIX_LENGTH &&
        same_run(file_name + name_length - DLL_SUFFIX_LENGTH, DLL_SUFFIX_LENGTH, DLL_SUFFIX,
                 DLL_SUFFIX_LENGTH)) {
        stem_length = name_length - DLL_SUFFIX_LENGTH;
    }
    while (list[at] != '\0') {
        size_t start;
        size_t end;

        while (is_blank(list[at])) {
            ++at;
        }
        start = at;
        while (list[at] != '\0' && list[at] != ',') {
            ++at;
        }
        end = at;
        while (end > start && is_blank(list[end - 1u])) {
            --end;
        }
        if (end > start && (same_run(list + start, end - start, file_name, name_length) ||
                            same_run(list + start, end - start, file_name, stem_length))) {
            return true;
        }
        if (list[at] == ',') {
            ++at;
        }
    }
    return false;
}
