/* mp_level_state_journal_rule.c: the level's journal. See the header. */
#include "mp_level_state_journal_rule.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The numbers are the 65535 values 1..65535 in a circle. */
#define NUMBERS 65535u

uint16_t mp_level_journal_next(uint16_t sequence)
{
    return sequence >= (uint16_t)NUMBERS ? (uint16_t)1u : (uint16_t)(sequence + 1u);
}

/* How far `a` lies ahead of `b` going forward; from none, a number is as far as its value. */
static uint32_t ahead(uint16_t a, uint16_t b)
{
    if (b == 0u) {
        return a;
    }
    return ((uint32_t)a + NUMBERS - (uint32_t)b) % NUMBERS;
}

bool mp_level_journal_after(uint16_t a, uint16_t b)
{
    uint32_t distance;

    if (a == 0u) {
        return false;
    }
    distance = ahead(a, b);
    return distance != 0u && distance <= NUMBERS / 2u;
}

uint32_t mp_level_journal_between(uint16_t from, uint16_t to)
{
    uint32_t distance = ahead(to, from);

    return distance == 0u ? 0u : distance - 1u;
}

static bool kind_known(uint8_t kind)
{
    return kind > (uint8_t)MP_LEVEL_JOURNAL_NONE && kind < (uint8_t)MP_LEVEL_JOURNAL_KINDS;
}

uint16_t mp_level_journal_push(mp_level_journal_t *journal, uint8_t kind, uint8_t a, uint16_t b,
                               uint32_t c)
{
    mp_level_journal_entry_t *entry;

    if (journal == NULL || !kind_known(kind)) {
        return 0u;
    }
    if (journal->count == (uint8_t)MP_LEVEL_STATE_JOURNAL_MAX) {
        memmove(&journal->entry[0], &journal->entry[1],
                (MP_LEVEL_STATE_JOURNAL_MAX - 1u) * sizeof journal->entry[0]);
        --journal->count;
    }
    journal->newest = mp_level_journal_next(journal->newest);
    entry           = &journal->entry[journal->count];
    entry->sequence = journal->newest;
    entry->kind     = kind;
    entry->a        = a;
    entry->b        = b;
    entry->c        = c;
    ++journal->count;
    return journal->newest;
}

void mp_level_journal_to_note(const mp_level_journal_t *journal, mp_level_state_note_t *note)
{
    if (journal == NULL || note == NULL || journal->newest == 0u) {
        return;
    }
    note->parts |= (uint8_t)MP_LEVEL_STATE_PART_JOURNAL;
    note->journal_newest = journal->newest;
    note->journal_count  = journal->count;
    memcpy(note->journal, journal->entry, sizeof note->journal);
}

mp_level_journal_plan_t mp_level_journal_plan(const mp_level_state_note_t *note, bool known,
                                              uint16_t last)
{
    mp_level_journal_plan_t plan;
    bool                    carried;
    size_t                  i;

    memset(&plan, 0, sizeof plan);
    plan.newest = last;
    carried = note != NULL && (note->parts & MP_LEVEL_STATE_PART_JOURNAL) != 0u;
    if (!known) {
        /* Everything the window holds is only counted, and the numbering starts from its end. */
        plan.first_snapshot = true;
        plan.count          = carried ? note->journal_count : 0u;
        plan.newest         = carried ? note->journal_newest : 0u;
        return plan;
    }
    if (!carried || !mp_level_journal_after(note->journal_newest, last)) {
        return plan;
    }
    plan.newest = note->journal_newest;
    for (i = 0; i < note->journal_count; ++i) {
        if (mp_level_journal_after(note->journal[i].sequence, last)) {
            break;
        }
    }
    if (i == note->journal_count) {
        /* Newer numbers exist and the window holds none of them: all of them were lost. */
        plan.jump = true;
        plan.lost = mp_level_journal_between(last, note->journal_newest) + 1u;
        return plan;
    }
    plan.first = i;
    plan.count = note->journal_count - i;
    if (note->journal[i].sequence != mp_level_journal_next(last)) {
        plan.jump = true;
        plan.lost = mp_level_journal_between(last, note->journal[i].sequence);
    }
    return plan;
}

bool mp_level_journal_is_moment(uint8_t kind)
{
    return kind == (uint8_t)MP_LEVEL_JOURNAL_CRAWL;
}
