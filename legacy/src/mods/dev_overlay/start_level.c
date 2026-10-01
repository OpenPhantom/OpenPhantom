/* start_level.c: see start_level.h. */
#include "start_level.h"

#include "common/detour.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/signature.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* --- campaign_loadLevel 0x0043F70A, detoured ------------------------------------------------- *
 * The same head dialogue_anim_fix detours, with the one absolute operand masked here: the store
 * of 1 into the not-in-gameplay cell that follows the prologue. Prologue nine bytes. */
static const uint8_t SIG_LEVEL_LOAD[] = {
    0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x88, 0x00, 0x00, 0x00, 0x56, 0x57, 0xC7,
    0x05, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00
};
static const uint8_t MSK_LEVEL_LOAD[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF
};
_Static_assert(sizeof SIG_LEVEL_LOAD == sizeof MSK_LEVEL_LOAD,
               "the level load pattern and its mask are different lengths");
#define LEVEL_LOAD_PROLOGUE 9u

/* --- 0x0043EBD6  campaign_run's restart, a data site --------------------------------------- *
 *   C7 05 <restore> 00 00 00 00     g_restorePending = 0
 *   A1 <start>                      eax = g_startLevelIndex
 *   A3 <index>                      g_campaignLevelIndex = eax
 *   6A 17 6A 00 E8 <rel32>          module_broadcast(0, NEW_GAME)
 *   83 C4 08
 *   C7 05 <notInGameplay> 01 00 00 00
 * Three cells read out of it: the restore flag at +2 and the campaign index at +16; the start
 * index at +11 is the front end's, which this does not need. */
static const uint8_t SIG_CAMPAIGN_RESTART[] = {
    0xC7, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xA1, 0x00, 0x00, 0x00, 0x00,
    0xA3, 0x00, 0x00, 0x00, 0x00,
    0x6A, 0x17, 0x6A, 0x00, 0xE8, 0x00, 0x00, 0x00, 0x00,
    0x83, 0xC4, 0x08,
    0xC7, 0x05, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00
};
static const uint8_t MSK_CAMPAIGN_RESTART[] = {
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF
};
_Static_assert(sizeof SIG_CAMPAIGN_RESTART == sizeof MSK_CAMPAIGN_RESTART,
               "the campaign restart pattern and its mask are different lengths");
#define RESTART_RESTORE_OPERAND 2u
#define RESTART_INDEX_OPERAND   16u

/* --- 0x0043EC59  the New Game load, a data site -------------------------------------------- *
 *   8B 15 <index>          edx = g_campaignLevelIndex
 *   6B D2 0C               edx *= 12                 the row stride, three pointers
 *   8B 82 <table>          eax = g_levelOrder[edx].b3d
 *   50 68                  push eax / push g_emptyString
 * The table at +9; the index at +2 must agree with the restart's. */
static const uint8_t SIG_NEW_GAME_LOAD[] = {
    0x8B, 0x15, 0x00, 0x00, 0x00, 0x00, 0x6B, 0xD2, 0x0C, 0x8B, 0x82, 0x00, 0x00, 0x00, 0x00,
    0x50, 0x68
};
static const uint8_t MSK_NEW_GAME_LOAD[] = {
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF
};
_Static_assert(sizeof SIG_NEW_GAME_LOAD == sizeof MSK_NEW_GAME_LOAD,
               "the new game load pattern and its mask are different lengths");
#define LOAD_INDEX_OPERAND 2u
#define LOAD_TABLE_OPERAND 11u

/* --- 0x0043ED6B  the level loop, a data site ----------------------------------------------- *
 *   C7 05 <outcome> 02 00 00 00     g_levelOutcome = RUNNING
 *   83 3D <outcome> 02              while (g_levelOutcome == RUNNING)
 *   75 07 E8 <rel32> EB F0              sys_frame()
 * The outcome cell at +2 and +11, and the two must agree. */
static const uint8_t SIG_LEVEL_LOOP[] = {
    0xC7, 0x05, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
    0x83, 0x3D, 0x00, 0x00, 0x00, 0x00, 0x02, 0x75, 0x07, 0xE8, 0x00, 0x00, 0x00, 0x00,
    0xEB, 0xF0
};
static const uint8_t MSK_LEVEL_LOOP[] = {
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF
};
_Static_assert(sizeof SIG_LEVEL_LOOP == sizeof MSK_LEVEL_LOOP,
               "the level loop pattern and its mask are different lengths");
#define LOOP_OUTCOME_OPERAND_A 2u
#define LOOP_OUTCOME_OPERAND_B 12u

#define LEVEL_ROW_STRIDE     12u     /* three pointers: the .b3d path, the title, the movie */
#define LEVEL_PATH_MAX       32u
#define OUTCOME_FAILED_FIRST 4       /* 4 to 10 are the fail variants; 2 running, 3 complete */

enum {
    SITE_LEVEL_LOAD,
    SITE_CAMPAIGN_RESTART,
    SITE_NEW_GAME_LOAD,
    SITE_LEVEL_LOOP,
    SITE_COUNT
};

static signature_t sites[SITE_COUNT] = {
    SIGNATURE_ENTRY_DETOUR_MASKED("campaign_loadLevel", SIG_LEVEL_LOAD, MSK_LEVEL_LOAD,
                                  LEVEL_LOAD_PROLOGUE),
    SIGNATURE_ENTRY_MASKED("campaign_restart", SIG_CAMPAIGN_RESTART, MSK_CAMPAIGN_RESTART),
    SIGNATURE_ENTRY_MASKED("new_game_load", SIG_NEW_GAME_LOAD, MSK_NEW_GAME_LOAD),
    SIGNATURE_ENTRY_MASKED("level_loop", SIG_LEVEL_LOOP, MSK_LEVEL_LOOP)
};

typedef int32_t (__cdecl *level_load_fn_t)(const char *path);

static struct {
    bool                     available;
    int                      level;          /* 1 based; 0 = the game's own first level */
    detour_t                 load;
    volatile int32_t        *campaign_index;
    const volatile int32_t  *restore_pending;
    const volatile int32_t  *outcome;
    uintptr_t                table;          /* g_levelOrder, eleven rows */
    uint32_t                 redirects;
} start;

/* The .b3d path of row `index` (0 based), or 0 when it does not read. */
static uintptr_t row_path(int index)
{
    uint32_t path = 0;

    if (index < 0 || index >= START_LEVEL_COUNT ||
        !memory_try_read(start.table + (uint32_t)index * LEVEL_ROW_STRIDE, &path, sizeof path)) {
        return 0;
    }
    return (uintptr_t)path;
}

void start_level_stem(int level, char *out, uint32_t out_size)
{
    char      path[LEVEL_PATH_MAX + 1] = {0};
    uintptr_t address;
    char     *stem;
    char     *dot;

    if (out == NULL || out_size == 0) {
        return;
    }
    out[0] = '\0';
    address = start.available ? row_path(level - 1) : 0;
    if (address == 0 || !memory_try_read(address, path, LEVEL_PATH_MAX)) {
        return;
    }
    path[LEVEL_PATH_MAX] = '\0';
    stem = strrchr(path, '\\');
    stem = (stem != NULL) ? stem + 1 : path;
    dot = strchr(stem, '.');
    if (dot != NULL) {
        *dot = '\0';
    }
    strncpy(out, stem, out_size - 1);
    out[out_size - 1] = '\0';
}

/* A new game's first load goes to the chosen row instead; every other load is passed through.
 * See the header for the three tests. */
static int32_t __cdecl hook_level_load(const char *path)
{
    level_load_fn_t original = (level_load_fn_t)start.load.original;
    int32_t         index = *start.campaign_index;
    int32_t         outcome = *start.outcome;
    uintptr_t       first = row_path(0);
    uintptr_t       chosen;
    char            stem[16];

    /* An outcome of 4 or more is a failure the front end came back from with a restart, and a
     * restart of level one is not a new game. */
    if (start.level <= 1 || index != 0 || *start.restore_pending != 0 ||
        outcome < 0 || outcome >= OUTCOME_FAILED_FIRST || path == NULL || first == 0 ||
        (uintptr_t)path != first) {
        return original(path);
    }
    chosen = row_path(start.level - 1);
    if (chosen == 0) {
        return original(path);
    }
    *start.campaign_index = start.level - 1;
    ++start.redirects;
    start_level_stem(start.level, stem, sizeof stem);
    log_info("a new game starts at level %d, %s: the campaign index is set to %d and the "
             "load goes to that row's file instead of the first (start %u)", start.level, stem,
             start.level - 1, (unsigned)start.redirects);
    return original((const char *)chosen);
}

static bool read_cells(void)
{
    uint32_t restore = 0, index_a = 0, index_b = 0, table = 0, outcome_a = 0, outcome_b = 0;
    uintptr_t restart = sites[SITE_CAMPAIGN_RESTART].address;
    uintptr_t load = sites[SITE_NEW_GAME_LOAD].address;
    uintptr_t loop = sites[SITE_LEVEL_LOOP].address;

    if (!memory_read_u32(restart + RESTART_RESTORE_OPERAND, &restore) ||
        !memory_read_u32(restart + RESTART_INDEX_OPERAND, &index_a) ||
        !memory_read_u32(load + LOAD_INDEX_OPERAND, &index_b) ||
        !memory_read_u32(load + LOAD_TABLE_OPERAND, &table) ||
        !memory_read_u32(loop + LOOP_OUTCOME_OPERAND_A, &outcome_a) ||
        !memory_read_u32(loop + LOOP_OUTCOME_OPERAND_B, &outcome_b)) {
        log_warning("start level: an operand did not read, so the row stays unavailable");
        return false;
    }
    if (index_a != index_b || outcome_a != outcome_b ||
        !memory_is_inside_image(restore, sizeof(int32_t)) ||
        !memory_is_inside_image(index_a, sizeof(int32_t)) ||
        !memory_is_inside_image(outcome_a, sizeof(int32_t)) ||
        !memory_is_inside_image(table, LEVEL_ROW_STRIDE * START_LEVEL_COUNT)) {
        log_warning("start level: the cells read as restore %08X, index %08X/%08X, outcome "
                    "%08X/%08X, table %08X, which do not agree or are outside the image, so the "
                    "row stays unavailable", (unsigned)restore, (unsigned)index_a,
                    (unsigned)index_b, (unsigned)outcome_a, (unsigned)outcome_b,
                    (unsigned)table);
        return false;
    }
    start.restore_pending = (const volatile int32_t *)(uintptr_t)restore;
    start.campaign_index  = (volatile int32_t *)(uintptr_t)index_a;
    start.outcome         = (const volatile int32_t *)(uintptr_t)outcome_a;
    start.table           = (uintptr_t)table;
    return true;
}

bool start_level_install(void)
{
    size_t i;

    signature_resolve_table(sites, SITE_COUNT);
    for (i = 0; i < SITE_COUNT; ++i) {
        if (sites[i].address == 0) {
            log_warning("start level: %s did not resolve, so the row stays unavailable",
                        sites[i].name);
            return false;
        }
    }
    if (!read_cells()) {
        return false;
    }
    if (!detour_install(&start.load, sites[SITE_LEVEL_LOAD].address,
                        (const void *)hook_level_load, LEVEL_LOAD_PROLOGUE)) {
        log_warning("start level: the detour on campaign_loadLevel at %08X failed, so the row "
                    "stays unavailable", (unsigned)sites[SITE_LEVEL_LOAD].address);
        return false;
    }
    start.available = true;
    log_info("start level: a new game can begin at any of the %d levels of the table at %08X "
             "(campaign_loadLevel %08X, index cell %08X, outcome cell %08X, restore cell %08X)",
             START_LEVEL_COUNT, (unsigned)start.table,
             (unsigned)sites[SITE_LEVEL_LOAD].address, (unsigned)(uintptr_t)start.campaign_index,
             (unsigned)(uintptr_t)start.outcome, (unsigned)(uintptr_t)start.restore_pending);
    return true;
}

bool start_level_is_available(void)
{
    return start.available;
}

int start_level_get(void)
{
    return start.level;
}

void start_level_set(int level)
{
    if (level < 0) {
        level = 0;
    } else if (level > START_LEVEL_COUNT) {
        level = START_LEVEL_COUNT;
    }
    start.level = level;
}
