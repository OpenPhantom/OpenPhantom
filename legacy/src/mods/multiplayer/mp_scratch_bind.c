/* mp_scratch_bind.c: the world scratchpad against the running engine. */
#include "mp_scratch_bind.h"

#include "mp_cells.h"
#include "mp_scratch.h"

#include "common/logging.h"
#include "common/memory.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The world clock inside the level record. Seconds, counted from the level's own beginning. The
 * offset is a required byte in two of the patterns the blackboard cells are read from (`D8 40 54`,
 * an fadd against it, in the timed set and `D8 58 54`, an fcomp, in the expiry test), so a build
 * that moved it would fail to resolve rather than read the wrong field. The unit is read out of
 * the clock setter at 0x0041F0C9:
 *
 *     0041F0D8  mov edx,[ecx+0x50] / mov [eax+0x58],edx      +0x58 = the previous milliseconds
 *     0041F0DE  fld [ebp+0x0C] / fmul [0x4A8214]              [0x4A8214] is 1000.0f
 *     0041F0E7  call __ftol / mov [ecx+0x50],eax              +0x50 = milliseconds, integral
 *     0041F0F5  fld [ebp+0x0C] / fsub [edx+0x54] / fstp [eax+0x5C]   +0x5C = dt, seconds
 *     0041F107  mov [ecx+0x54],edx                            +0x54 = now, seconds, raw */
#define WORLD_CLOCK_OFFSET 0x54u

/* The module messages that move a bank wholesale. Anything else leaves both banks alone. Read
 * out of the jump table of the enemy module procedure at 0x00431DE0 (index bytes at 0x00431EFF,
 * targets at 0x00431ECB, the message less 3 as the index, valid over 0 to 0x15): arm 3 at
 * 0x431F15 zeroes the twelve blackboard dwords and calls the bank clear at 0x43338C; arm 5 at
 * 0x431E50 calls the copy into the checkpoint at 0x4333A5; arm 6 at 0x431FF3 zeroes the
 * blackboard and leaves the bank; arm 7 at 0x431E78 calls the copy back at 0x4333C3; arm 0x0B
 * at 0x432544 writes both banks and all three registers out of a savegame; arms 0x12 at 0x431E46
 * and 0x17 at 0x431E32 call the bank clear; 4, 0x18 and the default arm at 0x431EC4 touch
 * neither. A caller census over the whole code section closes the table: the store has exactly
 * one caller (arm 5), the restart one (arm 7), the clear three (arms 0x17, 0x12 and 3). Message
 * 6 is in the set because it zeroes the blackboard, not because it touches the bank. */
#define MODULE_MSG_LEVEL_OPEN     3
#define MODULE_MSG_LEVEL_BEGIN    5
#define MODULE_MSG_LEVEL_END      6
#define MODULE_MSG_LEVEL_RESTART  7
#define MODULE_MSG_RESTORE        0x0B
#define MODULE_MSG_NEW_GAME_ALT   0x12
#define MODULE_MSG_NEW_GAME       0x17

typedef struct scratch_bind_state {
    bool      installed;
    bool      resync;
    uintptr_t bank;
    uintptr_t flag;
    uintptr_t previous;
    uintptr_t expiry;
    uintptr_t level;
    uint32_t  transitions;
    uint32_t  bank_bytes;
    uint32_t  ai_writes;
    /* Counted apart from bank_bytes because it is the ONE writer that reaches into the per-hero
     * window. A number here says the shared story actually moved this side's bank; a nought with a
     * host that is sending says the two halves are not meeting. */
    uint32_t  quest_writes;
    bool      no_level_logged;
} scratch_bind_state_t;

static scratch_bind_state_t bind;

/* ==============================================================================================
 * Installation.
 * ============================================================================================ */

static bool cell_readable(uintptr_t address, size_t size, const char *what)
{
    if (address == 0) {
        log_error("the scratchpad cell for %s did not resolve, so the campaign will not travel",
                  what);
        return false;
    }
    if (!memory_try_readable(address, size)) {
        log_error("the scratchpad cell for %s resolved to %08X but %u bytes there are not "
                  "readable, which means the operand named something that is not the bank",
                  what, (unsigned)address, (unsigned)size);
        return false;
    }
    return true;
}

bool mp_scratch_bind_install(void)
{
    const size_t slots = MP_SCRATCH_AI_SLOTS * sizeof(int32_t);

    if (bind.installed) {
        return true;
    }
    memset(&bind, 0, sizeof bind);

    bind.bank     = mp_cells_address(MP_CELL_STORY_FLAGS);
    bind.flag     = mp_cells_address(MP_CELL_AI_FLAG);
    bind.previous = mp_cells_address(MP_CELL_AI_FLAG_PREV);
    bind.expiry   = mp_cells_address(MP_CELL_AI_FLAG_EXPIRY);
    bind.level    = mp_cells_address(MP_CELL_LEVEL);

    if (!cell_readable(bind.bank, MP_SCRATCH_BANK_BYTES, "the living campaign bank") ||
        !cell_readable(bind.flag, slots, "the blackboard registers") ||
        !cell_readable(bind.previous, slots, "the blackboard fallback values") ||
        !cell_readable(bind.expiry, slots, "the blackboard expiry times") ||
        !cell_readable(bind.level, sizeof(uint32_t), "the level pointer")) {
        return false;
    }

    /* On one of the three shipped images the flag array sits at the address another image keeps
     * the expiry times at. Two cells that resolved to the same place would therefore not look
     * wrong at all, and every timer would be read as a flag. That is why the cells are resolved
     * by operand and not by address table: thirteen data anchor patterns, each matching exactly
     * once in each of the three images, name the living bank through six sites and ten
     * independent operands, the flags through six and seven, the fallback values through six
     * and six, the expiry times through six and eight. The four slot layout is read the same
     * way: the two blackboard clears each carry twelve absolute operands with a stride of four,
     * three groups of four, in two register orders from two compilations of the same source. */
    if (bind.flag == bind.expiry || bind.flag == bind.previous || bind.previous == bind.expiry) {
        log_error("two of the three blackboard registers resolved to the same address, so the "
                  "operands were read from a build this pattern set does not describe");
        return false;
    }

    bind.installed = true;
    bind.resync    = true;   /* nothing is known about the far side yet */
    log_info("the world scratchpad is bound: bank %08X, blackboard %08X/%08X/%08X, level %08X",
             (unsigned)bind.bank, (unsigned)bind.flag, (unsigned)bind.previous,
             (unsigned)bind.expiry, (unsigned)bind.level);
    return true;
}

bool mp_scratch_bind_installed(void)
{
    return bind.installed;
}

/* ==============================================================================================
 * The clock.
 * ============================================================================================ */

bool mp_scratch_bind_world_now(float *out)
{
    uint32_t world = 0;
    float    now   = 0.0f;

    if (!bind.installed || out == NULL) {
        return false;
    }
    if (!memory_try_read_u32(bind.level, &world) || world == 0) {
        return false;   /* a menu, not a fault */
    }
    if (!memory_try_read((uintptr_t)world + WORLD_CLOCK_OFFSET, &now, sizeof now)) {
        return false;
    }
    *out = now;
    return true;
}

/* ==============================================================================================
 * The bank.
 * ============================================================================================ */

bool mp_scratch_bind_read_bank(uint8_t *out)
{
    if (!bind.installed || out == NULL) {
        return false;
    }
    return memory_try_read(bind.bank, out, MP_SCRATCH_BANK_BYTES);
}

bool mp_scratch_bind_write_bank(const uint8_t *want, size_t *bytes_written)
{
    uint8_t  live[MP_SCRATCH_BANK_BYTES];
    uint32_t at      = 0;
    uint32_t written = 0;

    if (bytes_written != NULL) {
        *bytes_written = 0;
    }
    if (!bind.installed || want == NULL) {
        return false;
    }
    if (!memory_try_read(bind.bank, live, sizeof live)) {
        return false;
    }

    /* Only the bytes that differ, and in runs, so a bank that agrees costs no write at all. The
     * per-hero window is stepped over here as well as refused by the decoder: one player's keys
     * are worth two guards. */
    while (at < MP_SCRATCH_BANK_BYTES) {
        uint32_t start;
        uint32_t count;

        if (mp_scratch_is_per_hero(at) || want[at] == live[at]) {
            ++at;
            continue;
        }
        start = at;
        while (at < MP_SCRATCH_BANK_BYTES && !mp_scratch_is_per_hero(at) && want[at] != live[at]) {
            ++at;
        }
        count = at - start;
        if (!memory_try_write(bind.bank + start, want + start, count)) {
            log_error("%u bytes of the campaign bank at offset %u would not write, so this side "
                      "is now PARTLY updated: %u bytes went in before it failed",
                      (unsigned)count, (unsigned)start, (unsigned)written);
            return false;
        }
        written += count;
    }

    bind.bank_bytes += written;
    if (bytes_written != NULL) {
        *bytes_written = written;
    }
    return true;
}

/* ==============================================================================================
 * The quest band, which is the one part of the per-hero window that is not a hero's.
 *
 * Both of these read the whole 1250 byte bank and write back only what moved. That is not wasteful
 * in any way that matters, it runs once a substep at most and the bank is a memcpy, and it is
 * what keeps the splice honest: the codec is handed a real copy of the live bank and puts thirty
 * four bits into it, so every other bit of the window comes back exactly as it went in, including
 * the ones nobody in this tree has identified.
 * ============================================================================================ */

bool mp_scratch_bind_read_quest(mp_quest_set_t *out)
{
    uint8_t live[MP_SCRATCH_BANK_BYTES];

    if (!bind.installed || out == NULL || !memory_try_read(bind.bank, live, sizeof live)) {
        return false;
    }
    mp_quest_from_bank(live, out);
    return true;
}

bool mp_scratch_bind_write_quest(const mp_quest_set_t *set, bool *changed)
{
    uint8_t live[MP_SCRATCH_BANK_BYTES];
    uint8_t want[MP_SCRATCH_BANK_BYTES];
    uint32_t first;
    uint32_t last;
    uint32_t count;

    if (changed != NULL) {
        *changed = false;
    }
    if (!bind.installed || set == NULL || !memory_try_read(bind.bank, live, sizeof live)) {
        return false;
    }
    memcpy(want, live, sizeof want);
    mp_quest_into_bank(set, want);

    /* Only the bytes the band actually spans, and only when one of them moved. The band is five
     * bytes wide at most, so this is the whole write; a bank that already agrees costs nothing,
     * which matters because the host repeats its truth and most repeats say nothing new. */
    first = MP_QUEST_FIRST_BIT >> 3;
    last  = MP_QUEST_LAST_BIT >> 3;
    count = last - first + 1u;
    if (memcmp(live + first, want + first, count) == 0) {
        return true;
    }
    if (!memory_try_write(bind.bank + first, want + first, count)) {
        log_error("the shared quest bits would not write into the campaign bank, so this side's "
                  "story is now behind the host's and will stay behind");
        return false;
    }
    ++bind.quest_writes;
    if (changed != NULL) {
        *changed = true;
    }
    return true;
}

/* ==============================================================================================
 * The blackboard.
 * ============================================================================================ */

bool mp_scratch_bind_read_ai(int32_t *flag, int32_t *previous, float *expiry)
{
    const size_t slots = MP_SCRATCH_AI_SLOTS * sizeof(int32_t);

    if (!bind.installed || flag == NULL || previous == NULL || expiry == NULL) {
        return false;
    }
    return memory_try_read(bind.flag, flag, slots) &&
           memory_try_read(bind.previous, previous, slots) &&
           memory_try_read(bind.expiry, expiry, slots);
}

bool mp_scratch_bind_write_ai(const mp_scratch_ai_t *ai)
{
    const size_t slots = MP_SCRATCH_AI_SLOTS * sizeof(int32_t);
    float        expiry[MP_SCRATCH_AI_SLOTS];
    float        now = 0.0f;
    uint32_t     i;

    if (!bind.installed || ai == NULL) {
        return false;
    }

    /* Without a clock there is no way to turn a remaining time into an absolute one, and writing
     * the flags alone would arm timers that never expire. So the whole write waits. */
    if (!mp_scratch_bind_world_now(&now)) {
        if (!bind.no_level_logged) {
            bind.no_level_logged = true;
            log_warning("the blackboard arrived while no level is open, so it was not written: "
                        "its expiry times are measured against a clock that does not exist yet");
        }
        return false;
    }
    bind.no_level_logged = false;

    for (i = 0; i < MP_SCRATCH_AI_SLOTS; ++i) {
        expiry[i] = mp_scratch_expiry_at(ai->remaining[i], now);
    }
    if (!memory_try_write(bind.flag, ai->flag, slots) ||
        !memory_try_write(bind.previous, ai->previous, slots) ||
        !memory_try_write(bind.expiry, expiry, sizeof expiry)) {
        return false;
    }
    ++bind.ai_writes;
    return true;
}

/* ==============================================================================================
 * The transition gate.
 * ============================================================================================ */

void mp_scratch_bind_note_module_message(int msg)
{
    switch (msg) {
    case MODULE_MSG_LEVEL_OPEN:
    case MODULE_MSG_LEVEL_BEGIN:
    case MODULE_MSG_LEVEL_END:
    case MODULE_MSG_LEVEL_RESTART:
    case MODULE_MSG_RESTORE:
    case MODULE_MSG_NEW_GAME_ALT:
    case MODULE_MSG_NEW_GAME:
        break;
    default:
        return;
    }
    if (!bind.installed) {
        return;
    }
    bind.resync = true;
    ++bind.transitions;
}

bool mp_scratch_bind_take_resync(void)
{
    bool due = bind.resync;

    bind.resync = false;
    return due;
}

void mp_scratch_bind_counters(uint32_t *transitions, uint32_t *bank_bytes, uint32_t *ai_writes,
                              uint32_t *quest_writes)
{
    if (transitions != NULL) {
        *transitions = bind.transitions;
    }
    if (bank_bytes != NULL) {
        *bank_bytes = bind.bank_bytes;
    }
    if (ai_writes != NULL) {
        *ai_writes = bind.ai_writes;
    }
    if (quest_writes != NULL) {
        *quest_writes = bind.quest_writes;
    }
}
