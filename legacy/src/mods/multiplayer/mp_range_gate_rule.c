/* mp_range_gate_rule.c: which player an enemy's range is measured against. */
#include "mp_range_gate_rule.h"

#include <stddef.h>
#include <string.h>

bool mp_range_gate_within(const float at[3], const float player[3], float radius)
{
    float dx, dy, dz;

    if (at == NULL || player == NULL) {
        return false;
    }
    dx = at[0] - player[0];
    dy = at[1] - player[1];
    dz = at[2] - player[2];
    /* z, then y, then x, and the multiply on the right: this is the engine's own expression at
     * 0x00428eb3, kept term for term so the two cannot disagree at the radius. */
    return dz * dz + dy * dy + dx * dx < radius * radius;
}

bool mp_range_gate_any_within(const mp_range_gate_players_t *players, const float at[3],
                              float radius)
{
    size_t row;
    size_t rows;

    if (players == NULL || at == NULL) {
        return false;
    }
    rows = players->count;
    if (rows > sizeof players->have / sizeof players->have[0]) {
        rows = sizeof players->have / sizeof players->have[0];
    }
    for (row = 0; row < rows; ++row) {
        if (players->have[row] && mp_range_gate_within(at, players->positions[row], radius)) {
            return true;
        }
    }
    return false;
}

size_t mp_range_gate_count_callers(const uint8_t *code, size_t size, uint32_t code_va,
                                   uint32_t target, uint32_t *returns, size_t max_returns)
{
    size_t found = 0;
    size_t at;

    if (code == NULL || size < MP_RANGE_GATE_CALL_BYTES) {
        return 0;
    }
    for (at = 0; at + MP_RANGE_GATE_CALL_BYTES <= size; ++at) {
        uint32_t displacement;
        uint32_t after;

        if (code[at] != MP_RANGE_GATE_CALL_OPCODE) {
            continue;
        }
        memcpy(&displacement, &code[at + 1u], sizeof displacement);
        after = code_va + (uint32_t)at + MP_RANGE_GATE_CALL_BYTES;
        if (after + displacement != target) {
            continue;
        }
        if (returns != NULL && found < max_returns) {
            returns[found] = after;
        }
        ++found;
    }
    return found;
}
