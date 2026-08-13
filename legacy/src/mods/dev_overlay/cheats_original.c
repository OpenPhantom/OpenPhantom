/* cheats_original.c: the eleven codes the shipped game already understands.
 *
 * ==============================================================================================
 * WHERE THE TWO TABLES COME FROM
 *
 * Both are data, and data cannot be searched for. They are read out of the operands of the one
 * piece of code that touches both, the comparison loop inside the game's own cheat console. In the
 * retail executable, 829,952 bytes, ImageBase 0x400000, that loop reads:
 *
 *   0042FE38  8D 55 E0                 lea  edx,[ebp-0x20]        ; what was typed
 *   0042FE3B  52                       push edx
 *   0042FE3C  8B 45 DC                 mov  eax,[ebp-0x24]        ; the row index
 *   0042FE3F  8B 0C 85 10 C8 4A 00     mov  ecx,[eax*4 + 004AC810] ; THE NAME TABLE
 *   0042FE46  51                       push ecx
 *   0042FE47  E8 04 77 07 00           call the case insensitive compare
 *   0042FE4C  83 C4 08                 add  esp,8
 *   0042FE4F  85 C0                    test eax,eax
 *   0042FE51  75 47                    jnz  next row
 *   0042FE53  8B 55 DC                 mov  edx,[ebp-0x24]
 *   0042FE56  8B 04 95 80 22 88 00     mov  eax,[edx*4 + 00882280] ; THE FLAG ARRAY
 *   0042FE5D  83 F0 01                 xor  eax,1                  ; the toggle, and all of it
 *
 * The name table has exactly one reference in the whole code section and the flag array has four,
 * three of them in these few instructions. So this pattern is the only place both can be picked up
 * together, which is why the site was chosen rather than the tidier looking ones nearby.
 *
 * `xor eax,1` is the entire operation the console performs ON THE FLAG. It does one more thing
 * afterwards that this does not: it prints a line, taken from a parallel table of message ids
 * beside the names, through the on screen crawl. That is a confirmation for somebody who typed a
 * code blind, and the panel shows the state directly instead, so it is left out on purpose rather
 * than missed. Every EFFECT stays the engine's own, because nothing here patches a consumer.
 *
 * The four address bearing fields are masked out of the pattern and read back from the match, so
 * this keeps working under forced ASLR and in the recompiled build where the same code sits at the
 * same place but every data cell has moved.
 * ============================================================================================ */
#include "cheats_original.h"

#include "common/logging.h"
#include "common/memory.h"
#include "common/patch.h"
#include "common/signature.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* 0x0042FE38, forty bytes ending at the toggle itself. */
static const uint8_t SIG_CONSOLE_LOOP[] = {
    0x8D, 0x55, 0xE0,                                /* lea  edx,[ebp-0x20]      */
    0x52,                                            /* push edx                 */
    0x8B, 0x45, 0xDC,                                /* mov  eax,[ebp-0x24]      */
    0x8B, 0x0C, 0x85, 0x00, 0x00, 0x00, 0x00,        /* mov  ecx,[eax*4 + TABLE] */
    0x51,                                            /* push ecx                 */
    0xE8, 0x00, 0x00, 0x00, 0x00,                    /* call the compare         */
    0x83, 0xC4, 0x08,                                /* add  esp,8               */
    0x85, 0xC0,                                      /* test eax,eax             */
    0x75, 0x00,                                      /* jnz  next row            */
    0x8B, 0x55, 0xDC,                                /* mov  edx,[ebp-0x24]      */
    0x8B, 0x04, 0x95, 0x00, 0x00, 0x00, 0x00,        /* mov  eax,[edx*4 + FLAGS] */
    0x83, 0xF0, 0x01                                 /* xor  eax,1               */
};
/* 0xFF where the byte must match, 0x00 where it is an address, a relative target or a displacement
 * this file refuses to depend on. */
static const uint8_t MSK_CONSOLE_LOOP[] = {
    0xFF, 0xFF, 0xFF,
    0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0x00,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF
};
_Static_assert(sizeof(SIG_CONSOLE_LOOP) == sizeof(MSK_CONSOLE_LOOP),
               "the console loop pattern and its mask are different lengths");
#define OFFSET_NAME_TABLE  10u
#define OFFSET_FLAG_ARRAY  33u

typedef struct cheats_original_state {
    bool                  resolved;
    uint32_t              count;
    const char *const    *names;      /* the image's own pointer table */
    volatile int32_t     *flags;      /* eleven int32 of runtime state */
} cheats_original_state_t;

static cheats_original_state_t original_state;

/* A row's name has to be a printable, reasonably short, null terminated string inside the image.
 * The longest the game ships is "but i feel so good" at eighteen characters, so a generous ceiling
 * still catches a pointer that landed in the middle of something else. */
#define MAX_NAME_LENGTH 48u

static bool name_is_plausible(const char *text)
{
    uint32_t i;

    if (!memory_is_inside_image((uintptr_t)text, 1)) {
        return false;
    }
    for (i = 0; i < MAX_NAME_LENGTH; ++i) {
        if (!memory_is_inside_image((uintptr_t)(text + i), 1)) {
            return false;
        }
        if (text[i] == '\0') {
            return i > 0;                    /* an empty row is not a code */
        }
        if (text[i] < 0x20 || text[i] > 0x7E) {
            return false;
        }
    }
    return false;                            /* never terminated: not a string */
}

/* Walks the table until the NULL that bounds it in the image, refusing anything that does not read
 * like a row. The console's own loop stops at eleven, so a table that somehow ran longer would mean
 * the pattern matched something else entirely and the whole feature declines. */
static bool read_table(uintptr_t table)
{
    const char *const *rows = (const char *const *)table;
    uint32_t           i;

    for (i = 0; i <= CHEATS_ORIGINAL_MAX; ++i) {
        const char *text;

        if (!memory_is_inside_image((uintptr_t)&rows[i], sizeof(const char *))) {
            log_warning("the cheat table at %08X runs outside the image at row %u, refused",
                        (unsigned)table, (unsigned)i);
            return false;
        }
        text = rows[i];
        if (text == NULL) {
            break;
        }
        if (!name_is_plausible(text)) {
            log_warning("row %u of the cheat table at %08X does not read like a code, refused",
                        (unsigned)i, (unsigned)table);
            return false;
        }
    }

    if (i != CHEATS_ORIGINAL_MAX) {
        log_warning("the cheat table at %08X holds %u rows, expected %u, refused",
                    (unsigned)table, (unsigned)i, (unsigned)CHEATS_ORIGINAL_MAX);
        return false;
    }

    original_state.names = rows;
    original_state.count = i;
    return true;
}

bool cheats_original_resolve(void)
{
    uintptr_t site;
    uint32_t  table = 0;
    uint32_t  flags = 0;

    if (original_state.resolved) {
        return true;
    }

    site = signature_find_unique(SIG_CONSOLE_LOOP, MSK_CONSOLE_LOOP, sizeof SIG_CONSOLE_LOOP);
    if (site == 0) {
        log_warning("the cheat console's compare loop did not resolve, so the game's own codes are "
                    "not offered. Nothing is patched and the console itself still works.");
        return false;
    }

    if (!memory_read_u32(site + OFFSET_NAME_TABLE, &table) ||
        !memory_read_u32(site + OFFSET_FLAG_ARRAY, &flags)) {
        log_warning("the two table operands at %08X could not be read, refused", (unsigned)site);
        return false;
    }
    if (!memory_is_inside_image(table, sizeof(const char *) * (CHEATS_ORIGINAL_MAX + 1u)) ||
        !memory_is_inside_image(flags, sizeof(int32_t) * CHEATS_ORIGINAL_MAX)) {
        log_warning("the cheat tables at %08X and %08X are not both inside the image, refused",
                    (unsigned)table, (unsigned)flags);
        return false;
    }

    if (!read_table((uintptr_t)table)) {
        return false;
    }

    original_state.flags = (volatile int32_t *)(uintptr_t)flags;
    original_state.resolved = true;
    log_info("the game's own %u cheat codes are available, names at %08X and their state at %08X. "
             "A row is switched the way the console switches it, so nothing downstream can tell "
             "the two apart.", (unsigned)original_state.count, (unsigned)table, (unsigned)flags);
    return true;
}

uint32_t cheats_original_count(void)
{
    return original_state.resolved ? original_state.count : 0u;
}

const char *cheats_original_name(uint32_t index)
{
    if (!original_state.resolved || index >= original_state.count) {
        return NULL;
    }
    return original_state.names[index];
}

/* Read fresh every time. The console can flip a row behind us, a save game restores the block, and
 * a panel that cached this would show a state the game no longer has. */
bool cheats_original_is_on(uint32_t index)
{
    if (!original_state.resolved || index >= original_state.count) {
        return false;
    }
    return original_state.flags[index] != 0;
}

bool cheats_original_toggle(uint32_t index)
{
    int32_t value;

    if (!original_state.resolved || index >= original_state.count) {
        return false;
    }
    value = original_state.flags[index] ^ 1;
    original_state.flags[index] = value;
    return value != 0;
}
