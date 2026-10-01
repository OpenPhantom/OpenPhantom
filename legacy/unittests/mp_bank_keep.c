/* mp_bank_keep.c: what a far body's spawn takes from the player sitting here, and gives back.
 *
 * The window cannot be driven here: its cells resolve to nothing without the game. So the engine's
 * two writers are modelled byte for byte, status_setActivePlayer at 0x00459AB7 and the starting
 * kit at 0x00448046 to 0x004480BB, and the window runs over the model in the order the spawn runs
 * it: snapshot, swap in, the spawn's set_active and kit, the local set_active, the give back, the
 * swap out. Without the give back the model loses the player's keys exactly as the field run did,
 * which is what makes a green result here mean something.
 */
#include "unittest.h"

#include "mp_bank_keep.h"

#include <stdint.h>
#include <string.h>

/* Where in its record status_setActivePlayer stores and loads the six bytes. */
#define FLAGS_IN_RECORD 0x44u

/* The engine's cells as the model holds them, plus the bank's status copy the pointer names
 * inside the window. `pointer` is a record index, or -1 for the bank's copy. */
typedef struct model {
    mp_bank_keep_t cells;
    uint8_t        bank_copy[MP_BANK_KEEP_RECORD_BYTES];
    int            pointer;
} model_t;

static uint8_t *record_named(model_t *m, int pointer)
{
    return pointer < 0 ? m->bank_copy : m->cells.records[pointer];
}

/* 0x00459AB7: the current player first, then the six bytes stored into the record the pointer
 * names, the pointer moved to the new hero's record, and the six bytes loaded out of it. */
static void model_set_active(model_t *m, int hero)
{
    m->cells.current_player = (uint32_t)hero;
    memcpy(record_named(m, m->pointer) + FLAGS_IN_RECORD, m->cells.flags,
           MP_BANK_KEEP_FLAG_BYTES);
    m->pointer = hero;
    memcpy(m->cells.flags, record_named(m, m->pointer) + FLAGS_IN_RECORD,
           MP_BANK_KEEP_FLAG_BYTES);
}

/* 0x00448046 to 0x004480BB, with the table at 0x004B51C0: an ammunition count at +0x10 + w*4 and
 * the start weapon at +0x0C, in the record the pointer names. */
static void model_starting_kit(model_t *m, int hero)
{
    static const uint32_t weapon[4] = { 1u, 1u, 10u, 9u };
    static const uint32_t count[4]  = { 1u, 1u, 200u, 1u };
    uint8_t              *record    = record_named(m, m->pointer);

    memcpy(record + 0x10u + weapon[hero] * 4u, &count[hero], sizeof count[hero]);
    memcpy(record + 0x0Cu, &weapon[hero], sizeof weapon[hero]);
}

typedef enum give_back_when {
    GIVE_BACK_NEVER,
    GIVE_BACK_AFTER_LAST_WRITER,
    GIVE_BACK_TOO_EARLY
} give_back_when_t;

static void run_window(model_t *m, int local, int slot, give_back_when_t when,
                       mp_bank_keep_back_t *back)
{
    mp_bank_keep_t kept = m->cells;

    memset(back, 0, sizeof *back);
    memcpy(m->bank_copy, m->cells.records[local], MP_BANK_KEEP_RECORD_BYTES);  /* refresh */
    m->pointer = -1;                                                             /* swap in */
    model_set_active(m, slot);                                                   /* the spawn */
    model_starting_kit(m, slot);
    if (when == GIVE_BACK_TOO_EARLY) {
        (void)mp_bank_keep_restore(&kept, &m->cells, back);
    }
    model_set_active(m, local);                                  /* the local set_active */
    if (when == GIVE_BACK_AFTER_LAST_WRITER) {
        (void)mp_bank_keep_restore(&kept, &m->cells, back);
    }
    m->pointer = local;                                          /* swap out puts it back */
}

/* A player standing in hero `local`, who picked a key up after the last own spawn: the live six
 * bytes carry it, the local record still holds the six bytes of that spawn. */
static void a_player_with_a_new_key(model_t *m, int local)
{
    size_t record;

    memset(m, 0, sizeof *m);
    for (record = 0; record < MP_BANK_KEEP_RECORDS; ++record) {
        memset(m->cells.records[record], (int)(0x10u + record), MP_BANK_KEEP_RECORD_BYTES);
        memset(m->cells.records[record] + FLAGS_IN_RECORD, 0,
               MP_BANK_KEEP_FLAG_BYTES);
    }
    memset(m->cells.flags, 0, sizeof m->cells.flags);
    m->cells.flags[2] = 0x20u;      /* bit 69, the key the field run lost */
    m->cells.current_player = (uint32_t)local;
    m->pointer = local;
}

static void test_without_the_give_back_the_key_is_lost(void)
{
    model_t             m;
    mp_bank_keep_back_t back;

    a_player_with_a_new_key(&m, 0);
    run_window(&m, 0, 0, GIVE_BACK_NEVER, &back);
    ut_check(m.cells.flags[2] == 0u,
             "the model without the give back loses the key: the local set_active loads the six "
             "bytes of the last own spawn, which is the defect the field run showed");
}

static void test_the_same_hero_gives_everything_back(void)
{
    model_t             m;
    model_t             before;
    mp_bank_keep_back_t back;

    a_player_with_a_new_key(&m, 2);
    before = m;
    run_window(&m, 2, 2, GIVE_BACK_AFTER_LAST_WRITER, &back);
    ut_check(memcmp(&m.cells, &before.cells, sizeof m.cells) == 0,
             "a spawn of the player's own hero leaves the six bytes, all four records and the "
             "current player as they were");
    ut_check(back.flags && back.flag_bytes == 1u, "the one byte with the key went back");
    ut_check(back.records[2] && !back.records[0] && !back.records[1] && !back.records[3],
             "Panaka's own record went back, and only that one");
    ut_check(m.pointer == 2, "and the pointer names the player's record again");
}

static void test_another_hero_gives_everything_back(void)
{
    model_t             m;
    model_t             before;
    mp_bank_keep_back_t back;

    a_player_with_a_new_key(&m, 0);
    before = m;
    run_window(&m, 0, 2, GIVE_BACK_AFTER_LAST_WRITER, &back);
    ut_check(memcmp(&m.cells, &before.cells, sizeof m.cells) == 0,
             "a spawn of another hero leaves everything as it was too");
    ut_check(back.records[2],
             "the starting kit written into the other hero's record, which would have given "
             "this player 200 blaster rounds on its next turn as that hero, went back");
}

/* The order is the window's, which is why the swap out gives back and no caller does. Before the
 * local set_active the pointer still names the spawned hero's record, so that call stores the
 * given back bytes into it and loads the stale ones out of the player's own. */
static void test_a_give_back_before_the_last_writer_is_not_enough(void)
{
    model_t             m;
    model_t             before;
    mp_bank_keep_back_t back;

    a_player_with_a_new_key(&m, 0);
    run_window(&m, 0, 2, GIVE_BACK_TOO_EARLY, &back);
    ut_check(m.cells.flags[2] == 0u,
             "given back before the local set_active, a spawn of another hero loses the key "
             "all the same");

    a_player_with_a_new_key(&m, 0);
    before = m;
    run_window(&m, 0, 0, GIVE_BACK_TOO_EARLY, &back);
    ut_check(memcmp(&m.cells, &before.cells, sizeof m.cells) != 0,
             "and a spawn of the player's own hero keeps the key but leaves the record's six "
             "bytes changed, so neither is the state the window found");
}

static void test_nothing_moved_writes_nothing(void)
{
    mp_bank_keep_t      kept;
    mp_bank_keep_t      live;
    mp_bank_keep_back_t back;

    memset(&kept, 0x5A, sizeof kept);
    live = kept;
    ut_check(!mp_bank_keep_restore(&kept, &live, &back), "an untouched image moved nothing");
    ut_check(!back.flags && !back.records[0] && !back.records[3] && !back.current_player &&
                 back.flag_bytes == 0u && back.record_bytes == 0u,
             "and nothing is marked to be written");
}

static void test_the_current_player_goes_back(void)
{
    mp_bank_keep_t      kept;
    mp_bank_keep_t      live;
    mp_bank_keep_back_t back;

    memset(&kept, 0, sizeof kept);
    kept.current_player = 1u;
    live = kept;
    live.current_player = 3u;
    ut_check(mp_bank_keep_restore(&kept, &live, &back) && back.current_player &&
                 live.current_player == 1u,
             "a current player the window moved is put back, not left to the local set_active");
    ut_check(mp_bank_keep_differing(&kept, &live) == 0u, "and the images agree afterwards");
}

static void test_the_precondition(void)
{
    const uint32_t base = 0x0086D5A0u;

    ut_check(mp_bank_keep_precondition(base + 2u * 0x4Cu, base, 2u, 2),
             "the pointer on hero 2's record and the index on hero 2: the window may open");
    ut_check(!mp_bank_keep_precondition(base, base, 2u, 2),
             "the pointer on hero 0 while the index says 2: refused, the two would come back "
             "on different heroes");
    ut_check(!mp_bank_keep_precondition(base + 2u * 0x4Cu, base, 0u, 2),
             "the index on another hero: refused");
    ut_check(!mp_bank_keep_precondition(base, base, 0u, 4) &&
                 !mp_bank_keep_precondition(base, base, 0u, -1),
             "a hero outside the four records: refused");
    ut_check(!mp_bank_keep_precondition(0u, 0u, 0u, 0), "no record base: refused");
}

static void test_the_operand_census(void)
{
    uint8_t        code[16];
    const uint32_t cell = 0x00881B06u;

    memset(code, 0x90, sizeof code);
    memcpy(&code[3], &cell, sizeof cell);
    memcpy(&code[11], &cell, sizeof cell);
    ut_check(mp_bank_keep_operands(code, sizeof code, cell) == 2u,
             "a value named twice is counted twice, at any offset");
    ut_check(mp_bank_keep_operands(code, 3u, cell) == 0u, "three bytes hold no dword");
    ut_check(mp_bank_keep_operands(NULL, 16u, cell) == 0u, "no code, nothing named");
}

int main(void)
{
    test_without_the_give_back_the_key_is_lost();
    test_the_same_hero_gives_everything_back();
    test_another_hero_gives_everything_back();
    test_a_give_back_before_the_last_writer_is_not_enough();
    test_nothing_moved_writes_nothing();
    test_the_current_player_goes_back();
    test_the_precondition();
    test_the_operand_census();

    return ut_summary("mp_bank_keep");
}
