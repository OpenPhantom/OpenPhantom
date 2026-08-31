/* menu_art_source.c: the three sites and the mount. See menu_art_source.h for what and why.
 *
 * ==============================================================================================
 * The sites
 *
 * res_addSource, retail 0x004719E0. Matched on its prologue, which is unmistakable: it takes the
 * string's length with a REPNE SCASB and then tests the LAST character against 0x5C. That one
 * character is the whole of its behaviour, and it is the reason the path this passes must end in a
 * backslash: with one it mounts a DIRECTORY, without one it tries to open the path as an archive.
 * There is no extension test and no probing of the file's contents.
 *
 * res_promoteSource, retail 0x00471BBB. Unlinks a source from the chain and relinks it at the head,
 * writing the head cell at [0x006F83A4]. res_addSource already pushes to the front, so on a clean
 * mount this is redundant; the engine calls it anyway in res_cacheRemount, for the case where the
 * mount failed and left somebody else at the head. This does the same, for the same reason.
 *
 * swmenu_startup, retail 0x0045D77C, detoured on a 6 byte prologue. See the header for why this
 * particular moment.
 */
#include "menu_art_source.h"

#include "common/detour.h"
#include "common/logging.h"
#include "common/signature.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---------------------------------------------------------------------------------------------
 * res_addSource
 */
static const uint8_t SIG_RES_ADD_SOURCE[] = {
    0x55, 0x8B, 0xEC, 0x51, 0x57,              /* push ebp / mov ebp,esp / push ecx / push edi   */
    0x8B, 0x45, 0x08, 0x0F, 0xBE, 0x08,        /* movsx ecx, byte [path]                         */
    0x85, 0xC9, 0x74, 0x2D,                    /* the empty string arm                           */
    0x8B, 0x7D, 0x08, 0x83, 0xC9, 0xFF, 0x33, 0xC0, 0xF2, 0xAE,   /* repne scasb, i.e. strlen    */
    0xF7, 0xD1, 0x83, 0xC1, 0xFF,
    0x8B, 0x55, 0x08, 0x0F, 0xBE, 0x44, 0x0A, 0xFF,               /* the LAST character          */
    0x83, 0xF8, 0x5C                                              /* compared against '\\'       */
};

/* ---------------------------------------------------------------------------------------------
 * res_promoteSource. The pattern reaches as far as the head cell it writes, because the first
 * seventeen bytes on their own are an ordinary prologue that would match in several places.
 */
static const uint8_t SIG_RES_PROMOTE_SOURCE[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x08,
    0xC7, 0x45, 0xFC, 0x00, 0x00, 0x00, 0x00,
    0x8B, 0x45, 0x08, 0x50,
    0xE8, 0x00, 0x00, 0x00, 0x00,              /* call res_findSource, displacement masked       */
    0x83, 0xC4, 0x04, 0x89, 0x45, 0xF8,
    0x83, 0x7D, 0xF8, 0x00, 0x74, 0x78,
    0x8B, 0x4D, 0xF8, 0x83, 0x79, 0x04, 0x00, 0x74, 0x0D,
    0x8B, 0x55, 0xF8, 0x8B, 0x42, 0x04, 0x8B, 0x4D, 0xF8, 0x8B, 0x11, 0x89, 0x10,
    0x8B, 0x45, 0xF8, 0x83, 0x38, 0x00, 0x74, 0x0E,
    0x8B, 0x4D, 0xF8, 0x8B, 0x11, 0x8B, 0x45, 0xF8, 0x8B, 0x48, 0x04, 0x89, 0x4A, 0x04,
    0x8B, 0x15, 0xA4, 0x83, 0x6F, 0x00          /* mov edx,[g_resSourceHead]                     */
};
static const uint8_t MSK_RES_PROMOTE_SOURCE[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};

/* ---------------------------------------------------------------------------------------------
 * swmenu_startup
 */
static const uint8_t SIG_MENU_STARTUP[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x38,        /* push ebp / mov ebp,esp / sub esp,0x38          */
    0x83, 0x3D, 0x60, 0xD3, 0x86, 0x00, 0x01,  /* cmp [g_swMac.bStartup],1                       */
    0x75, 0x0A,                                /* jne past the early return                      */
    0xB8, 0x01, 0x00, 0x00, 0x00,              /* mov eax,1                                      */
    0xE9, 0x12, 0x02, 0x00, 0x00               /* jmp the tail                                   */
};
#define MENU_STARTUP_PROLOGUE 6u

enum {
    SITE_RES_ADD_SOURCE,
    SITE_RES_PROMOTE_SOURCE,
    SITE_MENU_STARTUP,
    SITE_COUNT
};

static signature_t sites[SITE_COUNT] = {
    SIGNATURE_ENTRY("res_addSource", SIG_RES_ADD_SOURCE),
    SIGNATURE_ENTRY_MASKED("res_promoteSource", SIG_RES_PROMOTE_SOURCE, MSK_RES_PROMOTE_SOURCE),
    SIGNATURE_ENTRY_DETOUR("swmenu_startup", SIG_MENU_STARTUP, MENU_STARTUP_PROLOGUE)
};

typedef void *(__cdecl *res_add_source_fn_t)(const char *path);
typedef int32_t(__cdecl *res_promote_source_fn_t)(const char *path);
typedef int32_t(__cdecl *menu_startup_fn_t)(void);

/* The folder name, and the same name again with the trailing backslash res_addSource needs to read
 * it as a directory rather than an archive. Both are held because the mount needs one and everybody
 * who wants to open a file underneath it needs the other. */
static char     art_directory[192] = MENU_ART_DEFAULT_DIRECTORY;
static char     art_mount_path[196];
static detour_t startup_detour;
static bool     mounted;

const char *menu_art_source_directory(void)
{
    return art_directory;
}

static void mount_once(void)
{
    res_add_source_fn_t     add;
    res_promote_source_fn_t promote;
    void                   *source;

    if (mounted) {
        return;
    }
    mounted = true;                       /* one attempt, whatever comes of it */

    add     = (res_add_source_fn_t)sites[SITE_RES_ADD_SOURCE].address;
    promote = (res_promote_source_fn_t)sites[SITE_RES_PROMOTE_SOURCE].address;

    source = add(art_mount_path);
    if (source == NULL) {
        /* Overwhelmingly this means the folder is not there, which is the normal state of an
         * install whose owner has not run the converter. menu_scale independently finds no
         * artwork and leaves the canvas alone, so the menus are the shipped ones and nothing is
         * wrong. Said at info, once, because it is the first thing to check when somebody reports
         * that they converted the art and nothing changed. */
        log_info("no menu artwork mounted from '%s'. That is the normal state without converted "
                 "artwork; run tools\\Convert Menu Art.bat to make some", art_mount_path);
        return;
    }

    /* Unconditional, exactly as res_cacheRemount does it: res_addSource already pushes to the head,
     * but promoting is what guarantees it even if something else got there first. */
    (void)promote(art_mount_path);
    log_info("menu artwork mounted from '%s' and promoted to the head of the resource chain, so "
             "converted files are found before big.lab and LOCALIZE.LAB", art_mount_path);
}

static int32_t __cdecl hook_menu_startup(void)
{
    menu_startup_fn_t original = (menu_startup_fn_t)startup_detour.original;

    mount_once();
    return (original != NULL) ? original() : 0;
}

bool menu_art_source_install(bool enabled, const char *directory)
{
    size_t length;

    if (!enabled) {
        log_info("MenuArtDirectory is empty, so no converted menu artwork is mounted and the "
                 "menus use the game's own");
        return false;
    }

    if (directory != NULL && directory[0] != '\0') {
        _snprintf(art_directory, sizeof art_directory - 1, "%s", directory);
        art_directory[sizeof art_directory - 1] = '\0';
    }

    /* A trailing separator here would become a double one below, and res_addSource compares the
     * last character exactly. */
    length = strlen(art_directory);
    while (length > 0 && (art_directory[length - 1] == '\\' || art_directory[length - 1] == '/')) {
        art_directory[--length] = '\0';
    }
    if (length == 0) {
        _snprintf(art_directory, sizeof art_directory, "%s", MENU_ART_DEFAULT_DIRECTORY);
    }
    _snprintf(art_mount_path, sizeof art_mount_path - 1, "%s\\", art_directory);
    art_mount_path[sizeof art_mount_path - 1] = '\0';

    signature_resolve_table(sites, SITE_COUNT);

    if (sites[SITE_RES_ADD_SOURCE].address == 0 || sites[SITE_RES_PROMOTE_SOURCE].address == 0) {
        log_warning("the resource chain's own mount functions did not resolve, so converted menu "
                    "artwork in '%s' cannot be mounted. Loose files in the game folder still work, "
                    "because the engine promotes the install root by itself", art_directory);
        return false;
    }
    if (sites[SITE_MENU_STARTUP].address == 0 ||
        !detour_install(&startup_detour, sites[SITE_MENU_STARTUP].address,
                        (const void *)hook_menu_startup, MENU_STARTUP_PROLOGUE)) {
        log_warning("swmenu_startup could not be hooked, so there is no moment at which to mount "
                    "'%s'. Loose files in the game folder still work", art_directory);
        return false;
    }

    log_info("converted menu artwork will be mounted from '%s' when the menu system starts",
             art_mount_path);
    return true;
}
