/* mp_fog_viewers_rule.c: which scripts set a room's fog, and one viewer's way through it. See the
 * header. */
#include "mp_fog_viewers_rule.h"

#include "mp_level_state_rule.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define FNV_OFFSET 2166136261u
#define FNV_PRIME  16777619u

_Static_assert(MP_FOG_ROLES <= 32, "the roles a binding found are the bits of one 32 bit word");

/* FEDSHIP's gas room, script 51 "gase", 65 entries. The flag test at 1 sends the script to its end
 * state; 2wai tests the leave distance at 28 and 4wai the way back at 42. The start is 0sta's two
 * edges and, a substep later, 1gas's green ramp; the leave is 2wai's ramp back and then 3res's
 * band; the way back is 4wai's band and then 5reg's green ramp; the end is zzzz's band. */
static const mp_fog_entry_t GASE_ENTRIES[] = {
    { 1u, 0x602u, 200, MP_FOG_ROLE_END_FLAG },
    { 2u, 0x400u, -1, MP_FOG_ROLE_END_STATE },
    { 28u, 0x107u, 0, MP_FOG_ROLE_LEAVE_TEST },
    { 42u, 0x107u, 0, MP_FOG_ROLE_ENTER_TEST },
    { 7u, 0x606u, 6, MP_FOG_ROLE_START_FIRST },
    { 9u, 0x606u, 7, MP_FOG_ROLE_START_FIRST },
    { 16u, 0x606u, 11, MP_FOG_ROLE_START_SECOND },
    { 29u, 0x606u, 12, MP_FOG_ROLE_LEAVE_FIRST },
    { 35u, 0x606u, 6, MP_FOG_ROLE_LEAVE_SECOND },
    { 37u, 0x606u, 7, MP_FOG_ROLE_LEAVE_SECOND },
    { 43u, 0x606u, 6, MP_FOG_ROLE_ENTER_FIRST },
    { 45u, 0x606u, 7, MP_FOG_ROLE_ENTER_FIRST },
    { 51u, 0x606u, 11, MP_FOG_ROLE_ENTER_SECOND },
    { 60u, 0x606u, 6, MP_FOG_ROLE_END_FIRST },
    { 62u, 0x606u, 7, MP_FOG_ROLE_END_FIRST },
};

/* Every script of the eleven levels that sets fog. The gas room is the viewer's; the swamp's start
 * of the level (GUNGA "1jar") and the race's start (RACE "voic") are the level's. */
static const mp_fog_script_row_t ROWS[] = {
    { "gase", 0x60359C12u, 65u, MP_FOG_CLASS_PER_VIEWER, GASE_ENTRIES,
      sizeof GASE_ENTRIES / sizeof GASE_ENTRIES[0] },
    { "1jar", 0x5A07A51Eu, 207u, MP_FOG_CLASS_SHARED, NULL, 0u },
    { "voic", 0x85DBF7BDu, 200u, MP_FOG_CLASS_SHARED, NULL, 0u },
};

#define ROW_COUNT (sizeof ROWS / sizeof ROWS[0])

size_t mp_fog_viewers_rows(void)
{
    return ROW_COUNT;
}

const mp_fog_script_row_t *mp_fog_viewers_row(size_t index)
{
    return index < ROW_COUNT ? &ROWS[index] : NULL;
}

const mp_fog_script_row_t *mp_fog_viewers_row_of(uint32_t fingerprint)
{
    size_t i;

    if (fingerprint == 0u) {
        return NULL;
    }
    for (i = 0; i < ROW_COUNT; ++i) {
        if (ROWS[i].fingerprint == fingerprint) {
            return &ROWS[i];
        }
    }
    return NULL;
}

/* Three words, the most any handler the rows name reads for one entry. */
#define POOL_READ_WORDS 3u

mp_fog_operand_t mp_fog_viewers_operand(int32_t opcode, int32_t operand, uint32_t pool_words)
{
    int32_t code = opcode & MP_FOG_OP_MASK;

    if ((opcode & MP_FOG_OP_INLINE) != 0) {
        return MP_FOG_OPERAND_INLINE;
    }
    if (code == MP_FOG_OP_HEAD || code == MP_FOG_OP_STATE) {
        return MP_FOG_OPERAND_NAME;
    }
    if (code != MP_FOG_OP_DIRECTOR && code != MP_FOG_OP_DISTANCE) {
        return MP_FOG_OPERAND_UNUSED;
    }
    if (operand < 0 || pool_words < POOL_READ_WORDS ||
        (uint32_t)operand > pool_words - POOL_READ_WORDS) {
        return MP_FOG_OPERAND_PAST_POOL;
    }
    return MP_FOG_OPERAND_POOL;
}

uint32_t mp_fog_viewers_pool_words(uint32_t pool, uint32_t end)
{
    return end > pool ? (end - pool) / 4u : 0u;
}

static uint32_t fnv_word(uint32_t value, uint32_t word)
{
    size_t i;

    for (i = 0; i < 4u; ++i) {
        value ^= (word >> (i * 8u)) & 0xFFu;
        value *= FNV_PRIME;
    }
    return value;
}

static bool is_pool_director(const mp_fog_script_entry_t *entry)
{
    return (entry->opcode & MP_FOG_OP_INLINE) == 0 &&
           (entry->opcode & MP_FOG_OP_MASK) == MP_FOG_OP_DIRECTOR;
}

bool mp_fog_viewers_fingerprint(uint32_t entry_count, mp_fog_read_fn_t read, const void *script,
                                uint32_t *fingerprint, uint32_t *fog_commands)
{
    uint32_t value = fnv_word(FNV_OFFSET, entry_count);
    uint32_t found = 0;
    uint32_t i;

    if (fingerprint != NULL) {
        *fingerprint = 0u;
    }
    if (fog_commands != NULL) {
        *fog_commands = 0u;
    }
    if (read == NULL) {
        return false;
    }
    for (i = 0; i < entry_count; ++i) {
        mp_fog_script_entry_t entry;

        if (!read(script, i, &entry)) {
            return false;
        }
        if (!is_pool_director(&entry) || !mp_level_state_fog_command(entry.word[0])) {
            continue;
        }
        ++found;
        value = fnv_word(value, i);
        value = fnv_word(value, (uint32_t)entry.word[0]);
        value = fnv_word(value, (uint32_t)entry.word[1]);
        value = fnv_word(value, (uint32_t)entry.word[2]);
    }
    if (fingerprint != NULL) {
        *fingerprint = found != 0u ? value : 0u;
    }
    if (fog_commands != NULL) {
        *fog_commands = found;
    }
    return true;
}

static float float_of(int32_t word)
{
    float value;

    memcpy(&value, &word, sizeof value);
    return value;
}

/* Where a director entry of each role goes: its transition and its half. */
static bool half_of(uint8_t role, mp_fog_transition_t *transition, size_t *half)
{
    switch (role) {
    case MP_FOG_ROLE_START_FIRST:
    case MP_FOG_ROLE_START_SECOND:
        *transition = MP_FOG_START;
        *half       = role == MP_FOG_ROLE_START_SECOND ? 1u : 0u;
        return true;
    case MP_FOG_ROLE_LEAVE_FIRST:
    case MP_FOG_ROLE_LEAVE_SECOND:
        *transition = MP_FOG_LEAVE;
        *half       = role == MP_FOG_ROLE_LEAVE_SECOND ? 1u : 0u;
        return true;
    case MP_FOG_ROLE_ENTER_FIRST:
    case MP_FOG_ROLE_ENTER_SECOND:
        *transition = MP_FOG_ENTER;
        *half       = role == MP_FOG_ROLE_ENTER_SECOND ? 1u : 0u;
        return true;
    case MP_FOG_ROLE_END_FIRST:
        *transition = MP_FOG_END;
        *half       = 0u;
        return true;
    default:
        return false;
    }
}

/* One entry of a row against the script: true when it holds what the row says, and its value is
 * then in `out`. */
static bool bind_entry(const mp_fog_entry_t *want, const mp_fog_script_entry_t *got,
                       mp_fog_bound_t *out)
{
    bool                inline_operand = (got->opcode & MP_FOG_OP_INLINE) != 0;
    mp_fog_transition_t transition     = MP_FOG_START;
    size_t              half           = 0u;
    mp_fog_half_t      *slot;

    if ((got->opcode & MP_FOG_OP_MASK) != (int32_t)want->opcode) {
        return false;
    }
    switch (want->role) {
    case MP_FOG_ROLE_END_FLAG:
        return inline_operand && got->word[0] == want->word;
    case MP_FOG_ROLE_END_STATE:
        out->end_state = got->word[0];
        return inline_operand;
    case MP_FOG_ROLE_LEAVE_TEST:
    case MP_FOG_ROLE_ENTER_TEST: {
        mp_fog_test_t *test = want->role == MP_FOG_ROLE_LEAVE_TEST ? &out->leave : &out->enter;

        if (inline_operand || got->word[0] != want->word || got->word[1] < 0 ||
            got->word[1] >= MP_FOG_COMPARISONS) {
            return false;
        }
        test->mode     = got->word[1];
        test->distance = float_of(got->word[2]);
        return true;
    }
    default:
        break;
    }
    if (inline_operand || got->word[0] != want->word || !mp_level_state_fog_command(want->word) ||
        !half_of(want->role, &transition, &half)) {
        return false;
    }
    slot = &out->half[transition][half];
    if (slot->count >= MP_FOG_HALF_MAX) {
        return false;
    }
    slot->command[slot->count].command = got->word[0];
    slot->command[slot->count].a1      = got->word[1];
    slot->command[slot->count].a2      = (uint32_t)got->word[2];
    ++slot->count;
    return true;
}

uint32_t mp_fog_viewers_bind(const mp_fog_script_row_t *row, uint32_t entry_count,
                             mp_fog_read_fn_t read, const void *script, mp_fog_bound_t *out)
{
    uint32_t wrong = 0;
    uint32_t roles = 0;
    size_t   i;

    if (out == NULL) {
        return 1u;
    }
    memset(out, 0, sizeof *out);
    out->leave.mode = -1;
    out->enter.mode = -1;
    if (row == NULL || row->cls != MP_FOG_CLASS_PER_VIEWER || row->entries == NULL ||
        read == NULL || entry_count != row->entry_count) {
        return 1u;
    }
    out->row = row;
    for (i = 0; i < row->entry_rows; ++i) {
        const mp_fog_entry_t *want = &row->entries[i];
        mp_fog_script_entry_t got;

        if (want->entry >= entry_count || !read(script, want->entry, &got) ||
            !bind_entry(want, &got, out)) {
            ++wrong;
            continue;
        }
        roles |= 1u << want->role;
    }
    /* Every transition needs its first half and both tests their values, or the viewer would be
     * driven by a script it only half read. */
    if ((roles & (1u << MP_FOG_ROLE_END_STATE)) == 0u ||
        (roles & (1u << MP_FOG_ROLE_LEAVE_TEST)) == 0u ||
        (roles & (1u << MP_FOG_ROLE_ENTER_TEST)) == 0u || out->half[MP_FOG_START][0].count == 0u ||
        out->half[MP_FOG_LEAVE][0].count == 0u || out->half[MP_FOG_ENTER][0].count == 0u ||
        out->half[MP_FOG_END][0].count == 0u) {
        ++wrong;
    }
    if (wrong != 0u) {
        out->row = NULL;
    }
    return wrong;
}

bool mp_fog_viewers_compare(float value, int32_t mode, float against)
{
    switch (mode) {
    case 0:
        return value == against;
    case 1:
        return value >= against;
    case 2:
        return value <= against;
    case 3:
        return value != against;
    case 4:
        return value > against;
    case 5:
        return value < against;
    default:
        return false;
    }
}

void mp_fog_viewers_viewer_init(mp_fog_viewer_t *viewer)
{
    if (viewer != NULL) {
        viewer->stage   = (uint8_t)MP_FOG_STAGE_NEVER;
        viewer->pending = (uint8_t)MP_FOG_TRANSITIONS;
    }
}

static mp_fog_step_t play(mp_fog_viewer_t *viewer, const mp_fog_bound_t *bound,
                          mp_fog_transition_t transition, uint8_t stage)
{
    mp_fog_step_t step;

    memset(&step, 0, sizeof step);
    step.outcome    = (uint8_t)MP_FOG_PLAY;
    step.transition = (uint8_t)transition;
    step.half       = 0u;
    viewer->stage   = stage;
    viewer->pending = bound->half[transition][1].count != 0u ? (uint8_t)transition
                                                             : (uint8_t)MP_FOG_TRANSITIONS;
    return step;
}

static mp_fog_step_t just(mp_fog_outcome_t outcome)
{
    mp_fog_step_t step;

    memset(&step, 0, sizeof step);
    step.outcome = (uint8_t)outcome;
    return step;
}

mp_fog_step_t mp_fog_viewers_decide(mp_fog_viewer_t *viewer, const mp_fog_bound_t *bound,
                                    const mp_fog_input_t *in)
{
    mp_fog_step_t step;
    uint8_t       was;

    if (viewer == NULL || bound == NULL || bound->row == NULL || in == NULL) {
        return just(MP_FOG_NOTHING);
    }
    /* A second half is due a substep after its first, whatever else happened since, as the
     * script's next state would run it. */
    if (viewer->pending < (uint8_t)MP_FOG_TRANSITIONS) {
        step            = just(MP_FOG_PLAY);
        step.transition = viewer->pending;
        step.half       = 1u;
        viewer->pending = (uint8_t)MP_FOG_TRANSITIONS;
        return step;
    }
    if (viewer->stage == (uint8_t)MP_FOG_STAGE_ENDED) {
        return just(MP_FOG_NOTHING);
    }
    if (in->ended) {
        was = viewer->stage;
        viewer->stage = (uint8_t)MP_FOG_STAGE_ENDED;
        if (was == (uint8_t)MP_FOG_STAGE_NEVER) {
            return just(MP_FOG_ENDED_UNSEEN);
        }
        step = play(viewer, bound, MP_FOG_END, (uint8_t)MP_FOG_STAGE_ENDED);
        step.green_at_end = was == (uint8_t)MP_FOG_STAGE_IN;
        return step;
    }
    if (!in->active) {
        return just(MP_FOG_NOTHING);
    }
    if (in->dead) {
        return just(MP_FOG_UNDECIDED_DEAD);
    }
    if (!in->measured) {
        return just(MP_FOG_UNDECIDED_UNMEASURED);
    }
    switch ((mp_fog_stage_t)viewer->stage) {
    case MP_FOG_STAGE_NEVER:
        if (mp_fog_viewers_compare(in->distance, bound->leave.mode, bound->leave.distance)) {
            viewer->stage = (uint8_t)MP_FOG_STAGE_OUT;
            return just(MP_FOG_STARTED_AWAY);
        }
        return play(viewer, bound, MP_FOG_START, (uint8_t)MP_FOG_STAGE_IN);
    case MP_FOG_STAGE_IN:
        if (mp_fog_viewers_compare(in->distance, bound->leave.mode, bound->leave.distance)) {
            return play(viewer, bound, MP_FOG_LEAVE, (uint8_t)MP_FOG_STAGE_OUT);
        }
        return just(MP_FOG_NOTHING);
    case MP_FOG_STAGE_OUT:
        if (mp_fog_viewers_compare(in->distance, bound->enter.mode, bound->enter.distance)) {
            return play(viewer, bound, MP_FOG_ENTER, (uint8_t)MP_FOG_STAGE_IN);
        }
        return just(MP_FOG_NOTHING);
    case MP_FOG_STAGE_ENDED:
    default:
        return just(MP_FOG_NOTHING);
    }
}
