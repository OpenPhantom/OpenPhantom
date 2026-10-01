/* mod_identity.c: which DLL in the mods folder is one of this release's own mods.
 *
 * The classification is pure and gets every class from strings. The reader is proven twice: on
 * images this test builds byte by byte, the PE headers, a resource tree and a version block laid
 * out the way the resource compiler writes them, so every class and every malformed shape has a
 * case of its own; and on a DLL of the operating system as it is loaded in this process. Then the
 * one rule for [multiplayer] AllowMods.
 */
#include "unittest.h"

#include "common/mod_identity.h"

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* ===================================== The pure half ====================================== */

static void check_the_classes(void)
{
    ut_section("the classification, from strings alone");

    ut_check(mod_identity_classify("OpenPhantom", "view_distance_fix", "view_distance_fix",
                                   "0.4.4", "0.4.4") == MOD_IDENTITY_OWN,
             "this project's company, the file's own name and this release: own");
    ut_check(mod_identity_classify("OpenPhantom", "View_Distance_Fix", "view_distance_FIX",
                                   "0.4.4", "0.4.4") == MOD_IDENTITY_OWN,
             "the name is compared without regard to case");
    ut_check(mod_identity_classify("Somebody Else", "view_distance_fix", "view_distance_fix",
                                   "0.4.4", "0.4.4") == MOD_IDENTITY_FOREIGN,
             "another company is foreign whatever else agrees");
    ut_check(mod_identity_classify("openphantom", "a", "a", "0.4.4", "0.4.4") ==
                 MOD_IDENTITY_FOREIGN,
             "the company is this project's spelling exactly");
    ut_check(mod_identity_classify(NULL, "a", "a", "0.4.4", "0.4.4") == MOD_IDENTITY_FOREIGN &&
                 mod_identity_classify("", "a", "a", "0.4.4", "0.4.4") == MOD_IDENTITY_FOREIGN,
             "no company at all is foreign");
    ut_check(mod_identity_classify("OpenPhantom", "variable_fov", "view_distance_fix", "0.4.4",
                                   "0.4.4") == MOD_IDENTITY_MISNAMED,
             "a mod under another mod's file name is misnamed");
    ut_check(mod_identity_classify("OpenPhantom", "", "a", "0.4.4", "0.4.4") ==
                     MOD_IDENTITY_MISNAMED &&
                 mod_identity_classify("OpenPhantom", NULL, "a", "0.4.4", "0.4.4") ==
                     MOD_IDENTITY_MISNAMED &&
                 mod_identity_classify("OpenPhantom", "a", NULL, "0.4.4", "0.4.4") ==
                     MOD_IDENTITY_MISNAMED,
             "an empty name on either side never matches");
    ut_check(mod_identity_classify("OpenPhantom", "a", "a", "0.4.3", "0.4.4") ==
                 MOD_IDENTITY_OTHER_RELEASE,
             "another release number is another release");
    ut_check(mod_identity_classify("OpenPhantom", "a", "a", "", "0.4.4") ==
                 MOD_IDENTITY_OTHER_RELEASE,
             "a mod without a number is not this release");
    ut_check(mod_identity_classify("OpenPhantom", "a", "a", "0.4.4", "") ==
                     MOD_IDENTITY_OTHER_RELEASE &&
                 mod_identity_classify("OpenPhantom", "a", "a", "0.4.4", NULL) ==
                     MOD_IDENTITY_OTHER_RELEASE,
             "asked without a number of its own, nothing is ever own");
}

/* ===================================== A built image ====================================== */

/* The image is laid out in memory the way the loader maps one: headers at the start, the
 * resource tree at 0x200 and the version block at 0x300, every offset a relative address. */
#define IMAGE_BYTES      0x800u
#define NT_AT            0x80u
#define OPTIONAL_AT      (NT_AT + 4u + 20u)
#define RESOURCE_AT      0x200u
#define DATA_ENTRY_AT    0x248u
#define BLOCK_AT         0x300u
#define TEST_TIME_STAMP  0x6ABB2222u

/* The optional header's own offsets, written out here rather than taken from the system headers
 * the module uses, so the two are a check on each other. */
#define PE32_SIZE_OF_IMAGE       56u
#define PE32_DIRECTORY_COUNT     92u
#define PE32_DIRECTORIES         96u
#define PE32PLUS_DIRECTORY_COUNT 108u
#define PE32PLUS_DIRECTORIES     112u

static union {
    uint32_t align;
    uint8_t  bytes[IMAGE_BYTES];
} image;

static void put16(size_t at, uint16_t value)
{
    image.bytes[at]      = (uint8_t)value;
    image.bytes[at + 1u] = (uint8_t)(value >> 8);
}

static void put32(size_t at, uint32_t value)
{
    put16(at, (uint16_t)value);
    put16(at + 2u, (uint16_t)(value >> 16));
}

/* A resource directory with one numbered entry. */
static void put_directory(size_t at, uint32_t name, uint32_t leads_to)
{
    put16(at + 14u, 1u);          /* no named entries, one numbered one */
    put32(at + 16u, name);
    put32(at + 20u, leads_to);
}

typedef struct block_writer {
    size_t at;
} block_writer_t;

static void pad(block_writer_t *w)
{
    while (((w->at - BLOCK_AT) & 3u) != 0u) {
        image.bytes[w->at++] = 0u;
    }
}

static void put_wide(block_writer_t *w, const char *text)
{
    size_t i;

    for (i = 0; text[i] != '\0'; ++i) {
        put16(w->at, (uint16_t)(unsigned char)text[i]);
        w->at += 2u;
    }
    put16(w->at, 0u);
    w->at += 2u;
}

/* A node's header and key; its length is written when it ends. */
static size_t begin_node(block_writer_t *w, const char *key, uint16_t value_length, uint16_t type)
{
    size_t start = w->at;

    put16(w->at + 2u, value_length);
    put16(w->at + 4u, type);
    w->at += 6u;
    put_wide(w, key);
    pad(w);
    return start;
}

static void end_node(block_writer_t *w, size_t start)
{
    put16(start, (uint16_t)(w->at - start));
    pad(w);
}

static void put_string(block_writer_t *w, const char *key, const char *value)
{
    size_t start = begin_node(w, key, (uint16_t)(strlen(value) + 1u), 1u);

    put_wide(w, value);
    end_node(w, start);
}

typedef struct block_spec {
    const char *table;
    const char *company;
    const char *internal_name;
    const char *file_version;
} block_spec_t;

/* The version block as the resource compiler writes it: the fixed information, then the string
 * table, then the translation. Returns its size. */
static size_t put_block(const block_spec_t *spec)
{
    block_writer_t w = { BLOCK_AT };
    size_t         root;
    size_t         strings;
    size_t         table;
    size_t         vars;
    size_t         translation;

    root = begin_node(&w, "VS_VERSION_INFO", 52u, 0u);
    put32(w.at, 0xFEEF04BDu);
    w.at += 52u;
    pad(&w);
    strings = begin_node(&w, "StringFileInfo", 0u, 1u);
    table   = begin_node(&w, spec->table, 0u, 1u);
    if (spec->company != NULL) {
        put_string(&w, "CompanyName", spec->company);
    }
    put_string(&w, "FileDescription", "a module");
    if (spec->file_version != NULL) {
        put_string(&w, "FileVersion", spec->file_version);
    }
    if (spec->internal_name != NULL) {
        put_string(&w, "InternalName", spec->internal_name);
    }
    end_node(&w, table);
    end_node(&w, strings);
    vars        = begin_node(&w, "VarFileInfo", 0u, 1u);
    translation = begin_node(&w, "Translation", 4u, 0u);
    put32(w.at, 0x04B00409u);
    w.at += 4u;
    end_node(&w, translation);
    end_node(&w, vars);
    end_node(&w, root);
    return w.at - BLOCK_AT;
}

/* A whole image: headers, the resource tree and the block. `wide` builds the 64-bit shape of the
 * optional header. `spec` NULL leaves the resource directory out altogether. */
static void build_image(const block_spec_t *spec, bool wide)
{
    size_t optional_count = wide ? PE32PLUS_DIRECTORY_COUNT : PE32_DIRECTORY_COUNT;
    size_t directories    = wide ? PE32PLUS_DIRECTORIES : PE32_DIRECTORIES;
    size_t block_size;

    memset(image.bytes, 0, sizeof image.bytes);
    put16(0u, 0x5A4Du);                         /* MZ */
    put32(0x3Cu, NT_AT);
    put32(NT_AT, 0x00004550u);                  /* PE\0\0 */
    put16(NT_AT + 4u, wide ? 0x8664u : 0x014Cu);
    put32(NT_AT + 8u, TEST_TIME_STAMP);
    put16(NT_AT + 20u, wide ? 0xF0u : 0xE0u);
    put16(OPTIONAL_AT, wide ? 0x020Bu : 0x010Bu);
    put32(OPTIONAL_AT + PE32_SIZE_OF_IMAGE, IMAGE_BYTES);
    put32(OPTIONAL_AT + optional_count, 16u);
    if (spec == NULL) {
        return;
    }
    put_directory(RESOURCE_AT, 16u, 0x80000000u | 0x18u);
    put_directory(RESOURCE_AT + 0x18u, 1u, 0x80000000u | 0x30u);
    put_directory(RESOURCE_AT + 0x30u, 0x409u, DATA_ENTRY_AT - RESOURCE_AT);
    block_size = put_block(spec);
    put32(DATA_ENTRY_AT, BLOCK_AT);
    put32(DATA_ENTRY_AT + 4u, (uint32_t)block_size);
    put32(OPTIONAL_AT + directories + 2u * 8u, RESOURCE_AT);
    put32(OPTIONAL_AT + directories + 2u * 8u + 4u, 0x60u);
}

static mod_identity_kind_t kind_of_image(const char *stem, const char *own)
{
    mod_identity_t out;

    if (!mod_identity_of_module(image.bytes, stem, own, &out)) {
        return MOD_IDENTITY_UNREADABLE;
    }
    return out.kind;
}

static const block_spec_t OWN_MOD = { "040904B0", "OpenPhantom", "view_distance_fix", "0.4.4" };

static void check_a_built_image(void)
{
    mod_identity_t out;
    block_spec_t   spec;

    ut_section("an image with a version block, every class");

    build_image(&OWN_MOD, false);
    ut_check(mod_identity_of_module(image.bytes, "view_distance_fix", "0.4.4", &out) &&
                 out.kind == MOD_IDENTITY_OWN,
             "this project's block, the file's own name and this release read as own");
    ut_checkf(out.time_stamp == TEST_TIME_STAMP && out.image_size == IMAGE_BYTES,
              "and the linker's stamp and the image size come out of the headers (%08X, %08X)",
              (unsigned)out.time_stamp, (unsigned)out.image_size);
    ut_check(strcmp(out.internal_name, "view_distance_fix") == 0 &&
                 strcmp(out.file_version, "0.4.4") == 0,
             "the internal name and the version are handed back as the block spells them");
    ut_check(kind_of_image("VIEW_DISTANCE_FIX", "0.4.4") == MOD_IDENTITY_OWN,
             "a file name in other case is the same mod");
    ut_check(kind_of_image("variable_fov", "0.4.4") == MOD_IDENTITY_MISNAMED,
             "the same block under another file name is misnamed");
    ut_check(kind_of_image("view_distance_fix", "0.4.5") == MOD_IDENTITY_OTHER_RELEASE,
             "against another release number it is another release");
    ut_check(kind_of_image("view_distance_fix", "") == MOD_IDENTITY_OTHER_RELEASE,
             "and against no number at all it is never own");

    build_image(&OWN_MOD, true);
    ut_check(kind_of_image("view_distance_fix", "0.4.4") == MOD_IDENTITY_OWN,
             "the 64-bit shape of the optional header is read the same way");

    spec = OWN_MOD;
    spec.company = "Somebody Else";
    build_image(&spec, false);
    ut_check(kind_of_image("view_distance_fix", "0.4.4") == MOD_IDENTITY_FOREIGN,
             "another company's block is foreign");

    spec = OWN_MOD;
    spec.table = "040704B0";
    build_image(&spec, false);
    ut_check(kind_of_image("view_distance_fix", "0.4.4") == MOD_IDENTITY_FOREIGN,
             "strings under another language's table were not written by this project");

    spec = OWN_MOD;
    spec.table = "040904b0";
    build_image(&spec, false);
    ut_check(kind_of_image("view_distance_fix", "0.4.4") == MOD_IDENTITY_OWN,
             "the table's name is hexadecimal and read in either case");

    spec = OWN_MOD;
    spec.internal_name = "view_distance_fi\xE9";
    build_image(&spec, false);
    ut_check(kind_of_image("view_distance_fi?", "0.4.4") == MOD_IDENTITY_OWN &&
                 kind_of_image("view_distance_fix", "0.4.4") == MOD_IDENTITY_MISNAMED,
             "a character outside ASCII reads as a question mark and matches nothing written");

    spec = OWN_MOD;
    spec.company = NULL;
    build_image(&spec, false);
    ut_check(kind_of_image("view_distance_fix", "0.4.4") == MOD_IDENTITY_FOREIGN,
             "a block without a company is foreign");

    spec = OWN_MOD;
    spec.file_version = NULL;
    build_image(&spec, false);
    ut_check(kind_of_image("view_distance_fix", "0.4.4") == MOD_IDENTITY_OTHER_RELEASE,
             "a block without a version is not this release");
}

static void check_malformed_images(void)
{
    ut_section("images that are not what they claim");

    build_image(NULL, false);
    ut_check(kind_of_image("view_distance_fix", "0.4.4") == MOD_IDENTITY_FOREIGN,
             "an image without a resource directory is foreign, not unreadable");

    build_image(&OWN_MOD, false);
    put32(DATA_ENTRY_AT + 4u, 40u);
    ut_check(kind_of_image("view_distance_fix", "0.4.4") == MOD_IDENTITY_FOREIGN,
             "a block cut short before its strings holds none of them");

    build_image(&OWN_MOD, false);
    put32(DATA_ENTRY_AT, IMAGE_BYTES - 8u);
    ut_check(kind_of_image("view_distance_fix", "0.4.4") == MOD_IDENTITY_FOREIGN,
             "a block said to run past the image is not read");

    build_image(&OWN_MOD, false);
    put16(BLOCK_AT, 0xFFFFu);
    ut_check(kind_of_image("view_distance_fix", "0.4.4") == MOD_IDENTITY_FOREIGN,
             "a node longer than its block is refused");

    build_image(&OWN_MOD, false);
    put_directory(RESOURCE_AT, 6u, 0x80000000u | 0x18u);
    ut_check(kind_of_image("view_distance_fix", "0.4.4") == MOD_IDENTITY_FOREIGN,
             "resources of other types only are no version block");

    build_image(&OWN_MOD, false);
    put_directory(RESOURCE_AT + 0x18u, 1u, 0x30u);
    ut_check(kind_of_image("view_distance_fix", "0.4.4") == MOD_IDENTITY_FOREIGN,
             "a tree whose second level leads straight to data is not followed");

    build_image(&OWN_MOD, false);
    put16(0u, 0x0000u);
    ut_check(kind_of_image("view_distance_fix", "0.4.4") == MOD_IDENTITY_UNREADABLE,
             "no MZ at the start is unreadable");

    build_image(&OWN_MOD, false);
    put32(0x3Cu, 0x7FFFFFF0u);
    ut_check(kind_of_image("view_distance_fix", "0.4.4") == MOD_IDENTITY_UNREADABLE,
             "headers said to stand far outside the image are unreadable");

    build_image(&OWN_MOD, false);
    put32(NT_AT, 0x00004551u);
    ut_check(kind_of_image("view_distance_fix", "0.4.4") == MOD_IDENTITY_UNREADABLE,
             "a wrong PE signature is unreadable");

    build_image(&OWN_MOD, false);
    put16(OPTIONAL_AT, 0x0107u);
    ut_check(kind_of_image("view_distance_fix", "0.4.4") == MOD_IDENTITY_UNREADABLE,
             "an optional header of neither shape is unreadable");

    build_image(&OWN_MOD, false);
    put32(OPTIONAL_AT + PE32_SIZE_OF_IMAGE, 0x100u);
    ut_check(kind_of_image("view_distance_fix", "0.4.4") == MOD_IDENTITY_UNREADABLE,
             "an image that does not hold its own headers is unreadable");

    {
        mod_identity_t out;

        ut_check(!mod_identity_of_module(NULL, "a", "0.4.4", &out) &&
                     out.kind == MOD_IDENTITY_UNREADABLE && out.time_stamp == 0u,
                 "no module at all answers false, unreadable and empty");
    }
}

/* ===================================== The operating system ================================ */

static void check_the_system_dll(void)
{
    mod_identity_t from_module;
    HMODULE        kernel = GetModuleHandleA("kernel32.dll");

    ut_section("the operating system's own DLL, as it is loaded in this process");

    ut_check(kernel != NULL && mod_identity_of_module(kernel, "kernel32", "0.4.4", &from_module) &&
                 from_module.kind == MOD_IDENTITY_FOREIGN,
             "the loaded kernel32 reads, and is foreign");
    ut_checkf(_stricmp(from_module.internal_name, "kernel32") == 0 &&
                  from_module.time_stamp != 0u && from_module.image_size != 0u,
              "its version block names it and its headers give a stamp and a size (%s, %08X, %08X)",
              from_module.internal_name, (unsigned)from_module.time_stamp,
              (unsigned)from_module.image_size);
}

static void check_the_own_version(void)
{
    char    version[MOD_IDENTITY_VERSION_MAX];
    HMODULE kernel = GetModuleHandleA("kernel32.dll");
    FARPROC inside = kernel != NULL ? GetProcAddress(kernel, "GetTickCount") : NULL;

    ut_section("the release number a module reads out of itself");

    memset(version, 'x', sizeof version);
    ut_check(!mod_identity_own_version((const void *)(uintptr_t)&check_the_own_version, version,
                                       sizeof version) && version[0] == '\0',
             "a test program carries no version resource, and answers false with nothing");
    ut_check(inside != NULL &&
                 mod_identity_own_version((const void *)(uintptr_t)inside, version,
                                          sizeof version) &&
                 version[0] >= '0' && version[0] <= '9',
             "a module that carries one hands back its file version");
    ut_checkf(strlen(version) < sizeof version, "cut to the field it was given (%s)", version);
    ut_check(!mod_identity_own_version(NULL, version, 0u), "no room is refused");
}

/* ===================================== The rule for AllowMods ============================= */

/* The cases the one AllowMods rule is held to, which the host asks before it hosts and its judge
 * asks of a join. */
static void check_the_allow_list(void)
{
    ut_section("[multiplayer] AllowMods as a player writes it, one rule for everybody");

    ut_check(mod_identity_name_listed("fps_counter.dll", "fps_counter.dll"),
             "the file name names the file");
    ut_check(mod_identity_name_listed("fps_counter", "fps_counter.dll"),
             "and so does its stem");
    ut_check(mod_identity_name_listed("Fps_Counter.DLL", "fps_counter.dll") &&
                 mod_identity_name_listed("fps_counter", "FPS_COUNTER.dll"),
             "in any case on either side");
    ut_check(mod_identity_name_listed("a.dll, fps_counter ,b", "fps_counter.dll"),
             "with blanks around the commas");
    ut_check(mod_identity_name_listed("\tfps_counter\t", "fps_counter.dll"),
             "and tabs around a single name");
    ut_check(mod_identity_name_listed("a.dll,b.dll,fps_counter.dll", "fps_counter.dll"),
             "at the end of a longer list");
    ut_check(!mod_identity_name_listed("fps_counte", "fps_counter.dll") &&
                 !mod_identity_name_listed("fps_counter.dl", "fps_counter.dll") &&
                 !mod_identity_name_listed("fps_counter.dll.bak", "fps_counter.dll"),
             "a part of the name is not the name");
    ut_check(!mod_identity_name_listed("xfps_counter", "fps_counter.dll"),
             "nor a longer one");
    ut_check(!mod_identity_name_listed("", "fps_counter.dll") &&
                 !mod_identity_name_listed(NULL, "fps_counter.dll") &&
                 !mod_identity_name_listed(" , ,", "fps_counter.dll"),
             "an empty list, or one of blanks and commas, names nothing");
    ut_check(!mod_identity_name_listed("fps_counter", "") &&
                 !mod_identity_name_listed("a", NULL),
             "and no file is named by anything");
    ut_check(!mod_identity_name_listed(".dll", "fps_counter.dll"),
             "the suffix alone is not a name");
}

int main(void)
{
    check_the_classes();
    check_a_built_image();
    check_malformed_images();
    check_the_system_dll();
    check_the_own_version();
    check_the_allow_list();

    return ut_summary("mod_identity");
}
