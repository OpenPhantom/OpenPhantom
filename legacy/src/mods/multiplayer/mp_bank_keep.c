/* mp_bank_keep.c: what a far body's spawn takes from the player sitting here. See the header. */
#include "mp_bank_keep.h"

#include <string.h>

static uint32_t bytes_apart(const uint8_t *a, const uint8_t *b, size_t size)
{
    uint32_t apart = 0;
    size_t   at;

    for (at = 0; at < size; ++at) {
        if (a[at] != b[at]) {
            ++apart;
        }
    }
    return apart;
}

bool mp_bank_keep_restore(const mp_bank_keep_t *kept, mp_bank_keep_t *live,
                          mp_bank_keep_back_t *back)
{
    size_t record;
    bool   moved = false;

    if (kept == NULL || live == NULL || back == NULL) {
        return false;
    }
    memset(back, 0, sizeof *back);

    back->flag_bytes = bytes_apart(kept->flags, live->flags, MP_BANK_KEEP_FLAG_BYTES);
    if (back->flag_bytes != 0u) {
        back->flags = true;
        memcpy(live->flags, kept->flags, MP_BANK_KEEP_FLAG_BYTES);
        moved = true;
    }
    for (record = 0; record < MP_BANK_KEEP_RECORDS; ++record) {
        uint32_t apart = bytes_apart(kept->records[record], live->records[record],
                                     MP_BANK_KEEP_RECORD_BYTES);

        if (apart != 0u) {
            back->records[record] = true;
            back->record_bytes += apart;
            memcpy(live->records[record], kept->records[record], MP_BANK_KEEP_RECORD_BYTES);
            moved = true;
        }
    }
    if (kept->current_player != live->current_player) {
        back->current_player = true;
        live->current_player = kept->current_player;
        moved = true;
    }
    return moved;
}

uint32_t mp_bank_keep_differing(const mp_bank_keep_t *a, const mp_bank_keep_t *b)
{
    uint32_t apart;

    if (a == NULL || b == NULL) {
        return 0u;
    }
    apart = bytes_apart(a->flags, b->flags, sizeof a->flags);
    apart += bytes_apart(&a->records[0][0], &b->records[0][0], sizeof a->records);
    apart += bytes_apart((const uint8_t *)&a->current_player, (const uint8_t *)&b->current_player,
                         sizeof a->current_player);
    return apart;
}

bool mp_bank_keep_precondition(uint32_t status_pointer, uint32_t record_base,
                               uint32_t current_player, int32_t local_hero)
{
    if (local_hero < 0 || local_hero >= (int32_t)MP_BANK_KEEP_RECORDS || record_base == 0u) {
        return false;
    }
    return status_pointer == record_base + (uint32_t)local_hero * MP_BANK_KEEP_RECORD_BYTES &&
           current_player == (uint32_t)local_hero;
}

size_t mp_bank_keep_operands(const uint8_t *code, size_t size, uint32_t value)
{
    size_t found = 0;
    size_t at;

    if (code == NULL || size < sizeof value) {
        return 0;
    }
    for (at = 0; at + sizeof value <= size; ++at) {
        uint32_t word;

        memcpy(&word, &code[at], sizeof word);
        if (word == value) {
            ++found;
        }
    }
    return found;
}
