/* mp_voice_bind.c: where the judgement of a spoken line reaches the engine. See the header. */
#include "mp_voice_bind.h"

#include "mp_scene_rule.h"
#include "mp_signatures.h"
#include "mp_signatures_dialog.h"
#include "mp_signatures_voice.h"
#include "mp_voice_rule.h"

#include "common/memory.h"
#include "common/patch.h"
#include "common/signature.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The byte in front of a push's immediate, and the push of one byte the lock level is pushed by. */
#define PUSH_OPCODE      0x68u
#define PUSH_BYTE_OPCODE 0x6Au

/* The arm of field 5 in the setter: `fld [ebp+0xC]`, a call to the float to int conversion, and
 * `mov [cell], eax`. `mov [cell], imm32` is how playByName writes a field back. */
#define FLD_OPCODE             0xD9u
#define FLD_ARGUMENT_MODRM     0x45u
#define FLD_ARGUMENT_DISP      0x0Cu
#define FLD_BYTES              3u
#define CALL_OPCODE            0xE8u
#define STORE_EAX_OPCODE       0xA3u
#define STORE_IMMEDIATE_OPCODE 0xC7u

/* A channel is 0x80 bytes: both the search and the freeing index the bank with `shl 7`, which is a
 * required byte of both patterns. */
#define MP_VOICE_BANK_STRIDE 0x80u

/* Where the speak entry's compare and Dialog_ForceRestart name the speaker, and where the latter
 * names the restart latch. */
#define SPEAK_SPEAKER_OPERAND   0x1Eu
#define RESTART_SPEAKER_OPERAND 0x04u
#define RESTART_LATCH_OPERAND   0x0Fu

static bool read_cell(const signature_t *site, size_t offset, uintptr_t *cell)
{
    return signature_read_address_operand(site, offset, cell) && *cell != 0u &&
           memory_is_inside_image(*cell, sizeof(uint32_t));
}

static bool voice_cell(mp_voice_site_t site, size_t offset, uintptr_t *cell)
{
    return read_cell(mp_signatures_voice_site(site), offset, cell);
}

/* The reach: the two pushes in the voice's own body, the far distance the engine admits by and the
 * range beside it, which have to be one number. */
static const char *bind_the_reach(mp_voice_cells_t *out)
{
    uintptr_t play   = mp_signatures_voice_address(MP_VOICE_SITE_PLAY);
    uint8_t   ops[2] = { 0u, 0u };
    uint32_t  range  = 0u;
    uint32_t  far    = 0u;

    if (play == 0u) {
        return "the voice did not resolve";
    }
    out->reach_at[0] = play + MP_VOICE_PLAY_RANGE_PUSH;
    out->reach_at[1] = play + MP_VOICE_PLAY_FAR_PUSH;
    if (!memory_read_u8(out->reach_at[0], &ops[0]) || !memory_read_u8(out->reach_at[1], &ops[1]) ||
        ops[0] != PUSH_OPCODE || ops[1] != PUSH_OPCODE ||
        !memory_read_u32(out->reach_at[0] + MP_VOICE_PLAY_PUSH_IMMEDIATE, &range) ||
        !memory_read_u32(out->reach_at[1] + MP_VOICE_PLAY_PUSH_IMMEDIATE, &far) ||
        !mp_voice_reach_from(far, range, &out->reach)) {
        return "the two pushes of the reach in the voice do not name one number above nought";
    }
    return NULL;
}

/* Field 0: the setter both calls of the voice name, which has to be where the setter's own pattern
 * stands, and the cell its first arm stores, which its switch table has to send field 0 to. */
static const char *bind_the_field(mp_voice_cells_t *out)
{
    uintptr_t play      = mp_signatures_voice_address(MP_VOICE_SITE_PLAY);
    uintptr_t setter    = mp_signatures_voice_address(MP_VOICE_SITE_SET_FIELD);
    uintptr_t named[2]  = { 0u, 0u };
    uintptr_t table     = 0u;
    uint32_t  first_arm = 0u;

    if (setter == 0u || !patch_read_call_target(play + MP_VOICE_PLAY_RANGE_SET_CALL, &named[0]) ||
        !patch_read_call_target(play + MP_VOICE_PLAY_FAR_SET_CALL, &named[1]) ||
        named[0] != setter || named[1] != setter) {
        return "the two calls of the field setter in the voice do not name the setter";
    }
    if (!voice_cell(MP_VOICE_SITE_SET_FIELD, MP_VOICE_SET_FIELD_TABLE, &table) ||
        !memory_read_u32(table, &first_arm) ||
        first_arm != setter + MP_VOICE_SET_FIELD_FIRST_ARM ||
        !voice_cell(MP_VOICE_SITE_SET_FIELD, MP_VOICE_SET_FIELD_VOLUME, &out->volume_cell)) {
        return "the switch of the field setter does not send field 0 to the arm that stores it";
    }
    out->set_field = setter;
    return NULL;
}

/* The eye, the subtitle option, the line on show and the channel of a line. */
static const char *bind_the_cells(mp_voice_cells_t *out)
{
    uintptr_t render  = mp_signatures_voice_address(MP_VOICE_SITE_RENDER_OPTION);
    uintptr_t update  = mp_signatures_voice_address(MP_VOICE_SITE_UPDATE_OPTION);
    uintptr_t calls   = mp_signatures_voice_address(MP_VOICE_SITE_VOICE_CALLS);
    uintptr_t play    = mp_signatures_voice_address(MP_VOICE_SITE_PLAY);
    uintptr_t again   = 0u;
    uintptr_t draw[2] = { 0u, 0u };

    if (!voice_cell(MP_VOICE_SITE_EYE, MP_VOICE_EYE_CELL, &out->eye_cell)) {
        return "the eye of the sound module did not resolve";
    }
    if (!voice_cell(MP_VOICE_SITE_RENDER_OPTION, MP_VOICE_RENDER_OPTION_CELL, &out->option_cell) ||
        !voice_cell(MP_VOICE_SITE_UPDATE_OPTION, MP_VOICE_UPDATE_OPTION_CELL, &again) ||
        again != out->option_cell) {
        return "the subtitle option is not named the same where the subtitle is drawn and measured";
    }
    if (!voice_cell(MP_VOICE_SITE_RENDER_OPTION, MP_VOICE_RENDER_SHOWN_CELL, &out->shown_cell) ||
        !voice_cell(MP_VOICE_SITE_UPDATE_OPTION, MP_VOICE_UPDATE_SHOWN_CELL, &again) ||
        again != out->shown_cell ||
        !patch_read_call_target(render + MP_VOICE_RENDER_DRAW_CALL, &draw[0]) ||
        !patch_read_call_target(update + MP_VOICE_UPDATE_DRAW_CALL, &draw[1]) ||
        draw[0] != draw[1]) {
        return "the line on show is not named the same where the subtitle is drawn and measured";
    }
    if (!voice_cell(MP_VOICE_SITE_VOICE_CALLS, MP_VOICE_CALLS_BARK_RESET, &out->bark_cell) ||
        !voice_cell(MP_VOICE_SITE_VOICE_CALLS, MP_VOICE_CALLS_BARK_HANDLE, &again) ||
        again != out->bark_cell || !patch_read_call_target(calls + MP_VOICE_CALLS_BARK, &draw[0]) ||
        draw[0] != play) {
        return "the channel of a line is not named the same where it is reset and handed on";
    }
    return NULL;
}

/* The engine's own edge: the speaker the speak entry compares against, named by the restart latch
 * as well, and the latch itself. */
static const char *bind_the_edge(mp_voice_cells_t *out)
{
    const signature_t *speak   = mp_signatures_dialog_site(MP_DIALOG_SITE_SPEAK_SINGLE);
    const signature_t *restart = mp_signatures_dialog_site(MP_DIALOG_SITE_FORCE_RESTART);
    uintptr_t          again   = 0u;

    if (!read_cell(speak, SPEAK_SPEAKER_OPERAND, &out->speaker_cell) ||
        !read_cell(restart, RESTART_SPEAKER_OPERAND, &again) || again != out->speaker_cell ||
        !read_cell(restart, RESTART_LATCH_OPERAND, &out->restart_cell)) {
        return "the speaker is not named the same by the speak entry and the restart latch";
    }
    return NULL;
}

const char *mp_voice_bind_cells(mp_voice_cells_t *out)
{
    const char *why;

    memset(out, 0, sizeof *out);
    (void)mp_signatures_voice_resolve();
    why = bind_the_reach(out);
    why = (why != NULL) ? why : bind_the_field(out);
    why = (why != NULL) ? why : bind_the_cells(out);
    why = (why != NULL) ? why : bind_the_edge(out);
    out->module_proc     = mp_signatures_voice_address(MP_VOICE_SITE_MODULE_PROC);
    out->module_prologue = mp_signatures_voice_prologue(MP_VOICE_SITE_MODULE_PROC);
    if (why == NULL && out->module_proc == 0u) {
        why = "the dialogue module did not resolve";
    }
    return why;
}

/* The reply's call and the bark's have to name one voice, and where the voice's own pattern stands
 * when it resolved; the place inside a body is the required byte of the add behind the live player
 * call. The sites are read after the table is resolved here, so the reply binds whichever binding
 * runs first. */
const char *mp_voice_bind_reply(mp_voice_reply_site_t *out)
{
    uintptr_t calls;
    uintptr_t play;
    uintptr_t named[2] = { 0u, 0u };
    uint8_t   offset   = 0u;

    memset(out, 0, sizeof *out);
    (void)mp_signatures_voice_resolve();
    calls = mp_signatures_voice_address(MP_VOICE_SITE_VOICE_CALLS);
    play  = mp_signatures_voice_address(MP_VOICE_SITE_PLAY);
    if (calls == 0u) {
        return "the calls that voice a line did not resolve";
    }
    if (!patch_read_call_target(calls + MP_VOICE_CALLS_REPLY, &named[0]) ||
        !patch_read_call_target(calls + MP_VOICE_CALLS_BARK, &named[1]) ||
        named[0] != named[1] || (play != 0u && named[0] != play)) {
        return "the reply and the bark do not call one voice";
    }
    if (!memory_read_u8(calls + MP_VOICE_CALLS_PLACE_OFFSET, &offset) || offset == 0u) {
        return "the place inside a body could not be read";
    }
    out->call        = calls + MP_VOICE_CALLS_REPLY;
    out->voice       = named[0];
    out->body_offset = offset;
    return NULL;
}

/* ==============================================================================================
 * The hearing radius, the engine's answer and the priority.
 * ============================================================================================ */

static bool opcode_at(uintptr_t at, uint8_t opcode)
{
    uint8_t byte = 0u;

    return at != 0u && memory_read_u8(at, &byte) && byte == opcode;
}

static bool cell_at(uintptr_t at, uintptr_t *cell)
{
    uint32_t value = 0u;

    if (at == 0u || !memory_read_u32(at, &value) || value == 0u ||
        !memory_is_inside_image((uintptr_t)value, sizeof(uint32_t))) {
        return false;
    }
    *cell = (uintptr_t)value;
    return true;
}

static bool one_bit(uint32_t value)
{
    return value != 0u && (value & (value - 1u)) == 0u;
}

static const char *bind_the_hearing(const mp_voice_cells_t *rule, mp_voice_hear_cells_t *out)
{
    uintptr_t play = mp_signatures_voice_address(MP_VOICE_SITE_PLAY);
    uintptr_t hear = mp_signatures_voice_address(MP_VOICE_SITE_HEAR);
    uintptr_t named[2] = { 0u, 0u };
    uint8_t   lock     = 0u;

    memset(out, 0, sizeof *out);
    if (rule == NULL || rule->set_field == 0u || play == 0u || hear == 0u) {
        return "the lock and the radii of the voice did not resolve";
    }
    if (hear != play + MP_VOICE_HEAR_BEHIND_PLAY) {
        return "the lock and the radii do not stand right behind the voice's own pushes";
    }
    out->lock_at  = hear + MP_VOICE_HEAR_LOCK_PUSH;
    out->scene_at = hear + MP_VOICE_HEAR_SCENE_PUSH;
    out->free_at  = hear + MP_VOICE_HEAR_FREE_PUSH;
    if (!opcode_at(out->lock_at, PUSH_BYTE_OPCODE) || !memory_read_u8(out->lock_at + 1u, &lock) ||
        !opcode_at(out->scene_at, PUSH_OPCODE) || !opcode_at(out->free_at, PUSH_OPCODE) ||
        !memory_read_u32(out->scene_at + MP_VOICE_PLAY_PUSH_IMMEDIATE, &out->min_scene_bits) ||
        !memory_read_u32(out->free_at + MP_VOICE_PLAY_PUSH_IMMEDIATE, &out->min_free_bits)) {
        return "the pushes of the lock and the two radii could not be read";
    }
    if (!patch_read_call_target(hear + MP_VOICE_HEAR_SCENE_SET_CALL, &named[0]) ||
        !patch_read_call_target(hear + MP_VOICE_HEAR_FREE_SET_CALL, &named[1]) ||
        named[0] != rule->set_field || named[1] != rule->set_field) {
        return "the two radii are not handed to the field setter the voice calls";
    }
    out->lock_level = (int32_t)(int8_t)lock;
    return NULL;
}

const char *mp_voice_bind_radii(const mp_voice_cells_t *rule, float factor_raw,
                                mp_voice_hear_cells_t *hear, mp_voice_hearing_t *hearing,
                                bool *factor_as_given)
{
    float       factor = mp_voice_hear_factor(factor_raw, factor_as_given);
    const char *why    = bind_the_hearing(rule, hear);

    if (why == NULL && !mp_voice_hear_from(hear->min_free_bits, hear->min_scene_bits,
                                           hear->lock_level, MP_SCENE_LOCK_LEVEL, factor,
                                           rule->reach, hearing)) {
        why = "the two radii or the lock level are not the ones a voice is written with";
    }
    if (why != NULL) {
        memset(hearing, 0, sizeof *hearing);
        hearing->free   = rule->reach;
        hearing->scene  = rule->reach;
        hearing->admit  = rule->reach;
        hearing->factor = factor;
        hearing->lock   = MP_SCENE_LOCK_LEVEL;
    }
    return why;
}

/* The bank as the search walks it and as the freeing indexes it: the base plus the flags' offset
 * has to be the flags cell the search reads, the priority is read twice by the search, and the
 * owner's handle three times by the freeing. */
static const char *bind_the_bank(mp_voice_bank_t *bank)
{
    uintptr_t search = mp_signatures_voice_address(MP_VOICE_SITE_CHANNEL_SEARCH);
    uintptr_t freeing = mp_signatures_voice_address(MP_VOICE_SITE_FREE_CHANNEL);
    uintptr_t flags = 0u;
    uintptr_t priority[2] = { 0u, 0u };
    uint8_t   count = 0u;
    uint8_t   at[5] = { 0u, 0u, 0u, 0u, 0u };

    if (search == 0u || freeing == 0u) {
        return "the channel search or the freeing of a channel did not resolve";
    }
    if (!memory_read_u8(search + MP_VOICE_SEARCH_COUNT, &count) || count == 0u ||
        count > MP_VOICE_CHANNELS_MAX || !cell_at(freeing + MP_VOICE_FREE_BANK, &bank->base) ||
        !memory_read_u8(freeing + MP_VOICE_FREE_FLAGS_AT, &at[0]) ||
        !cell_at(search + MP_VOICE_SEARCH_FLAGS_CELL, &flags) ||
        flags != bank->base + at[0]) {
        return "the bank is not named the same where a channel is sought and where it is freed";
    }
    if (!memory_read_u32(search + MP_VOICE_SEARCH_FREE_BIT, &bank->free_bit) ||
        !memory_read_u32(freeing + MP_VOICE_FREE_PLAYING_BIT, &bank->playing_bit) ||
        !one_bit(bank->free_bit) || !one_bit(bank->playing_bit) ||
        bank->free_bit == bank->playing_bit) {
        return "the free bit and the playing bit are no two single bits";
    }
    if (!cell_at(search + MP_VOICE_SEARCH_PRIORITY, &priority[0]) ||
        !cell_at(search + MP_VOICE_SEARCH_PRIORITY_LOAD, &priority[1]) ||
        priority[0] != priority[1] || priority[0] < bank->base ||
        priority[0] - bank->base + sizeof(uint32_t) > MP_VOICE_BANK_STRIDE) {
        return "the priority of a channel is not named the same twice inside a channel";
    }
    if (!memory_read_u8(freeing + MP_VOICE_FREE_OWNER_TEST, &at[1]) ||
        !memory_read_u8(freeing + MP_VOICE_FREE_OWNER_LOAD, &at[2]) ||
        !memory_read_u8(freeing + MP_VOICE_FREE_OWNER_CLEAR, &at[3]) ||
        !memory_read_u8(freeing + MP_VOICE_FREE_REF_AT, &at[4]) || at[1] != at[2] ||
        at[1] != at[3] || (size_t)at[1] + sizeof(uint32_t) > MP_VOICE_BANK_STRIDE ||
        (size_t)at[4] + sizeof(uint32_t) > MP_VOICE_BANK_STRIDE) {
        return "the owner's handle of a channel is not named the same three times";
    }
    bank->count       = count;
    bank->stride      = MP_VOICE_BANK_STRIDE;
    bank->flags_at    = at[0];
    bank->priority_at = priority[0] - bank->base;
    bank->owner_at    = at[1];
    bank->ref_at      = at[4];
    if (!memory_is_inside_image(bank->base, bank->count * bank->stride)) {
        return "the bank does not lie inside the image";
    }
    return NULL;
}

const char *mp_voice_bind_answer(const mp_voice_cells_t *rule, mp_voice_answer_cells_t *out)
{
    uintptr_t option = mp_signatures_voice_address(MP_VOICE_SITE_OPTION);
    uintptr_t render = mp_signatures_voice_address(MP_VOICE_SITE_RENDER_VOICES);
    uintptr_t latch  = mp_signatures_voice_address(MP_VOICE_SITE_LATCH);
    uintptr_t again  = 0u;

    memset(out, 0, sizeof *out);
    if (rule == NULL || rule->bark_cell == 0u) {
        return "the rule's channel of a line is not bound";
    }
    if (option == 0u || render == 0u || latch == 0u) {
        return "the voice option, the latch or the render of a line did not resolve";
    }
    if (!cell_at(option + MP_VOICE_OPTION_CELL, &out->voices_cell) ||
        !cell_at(render + MP_VOICE_RENDER_VOICES_CELL, &again) || again != out->voices_cell) {
        return "the voice option is not named the same where a line is voiced and where it is "
               "drawn";
    }
    if (!cell_at(render + MP_VOICE_RENDER_BARK_CELL, &again) || again != rule->bark_cell) {
        return "the render does not test the channel of a line the voice is handed";
    }
    if (!cell_at(latch + MP_VOICE_LATCH_COMPARE, &out->latch_cell) ||
        !cell_at(latch + MP_VOICE_LATCH_STORE, &again) || again != out->latch_cell) {
        return "the latch against the same line is not named the same where it is compared and set";
    }
    return bind_the_bank(&out->bank);
}

/* Field 5: the setter's switch sends it to an arm of fld, call and store, and the store's cell has
 * to be the one playByName's reset writes back, whose value is the resting priority. The same
 * reset names field 0, which has to be the rule's. The switch's table is read behind the setter's
 * prologue, where no hull of its head reaches. */
const char *mp_voice_bind_priority(const mp_voice_cells_t *rule, mp_voice_priority_cells_t *out)
{
    uintptr_t setter = mp_signatures_voice_address(MP_VOICE_SITE_SET_FIELD);
    uintptr_t reset  = mp_signatures_voice_address(MP_VOICE_SITE_BY_NAME_RESET);
    uintptr_t table  = 0u;
    uint32_t  arm   = 0u;
    uintptr_t cell  = 0u;
    uint32_t  value = 0u;
    uint8_t   fld[3] = { 0u, 0u, 0u };

    memset(out, 0, sizeof *out);
    if (rule == NULL || rule->set_field == 0u || setter != rule->set_field || reset == 0u ||
        !cell_at(setter + MP_VOICE_SET_FIELD_TABLE, &table) ||
        !memory_read_u32(table + MP_VOICE_SET_FIELD_PRIORITY * sizeof(uint32_t), &arm)) {
        return "the field setter's switch or playByName's reset did not resolve";
    }
    if ((uintptr_t)arm <= rule->set_field || (uintptr_t)arm >= table ||
        !memory_try_read((uintptr_t)arm, fld, sizeof fld) || fld[0] != FLD_OPCODE ||
        fld[1] != FLD_ARGUMENT_MODRM || fld[2] != FLD_ARGUMENT_DISP ||
        !opcode_at((uintptr_t)arm + FLD_BYTES, CALL_OPCODE) ||
        !opcode_at((uintptr_t)arm + MP_VOICE_PRIORITY_ARM_STORE, STORE_EAX_OPCODE) ||
        !cell_at((uintptr_t)arm + MP_VOICE_PRIORITY_ARM_STORE + 1u, &out->cell)) {
        return "the setter's arm for field 5 is not a load, a conversion and a store";
    }
    if (!opcode_at(reset + MP_VOICE_RESET_PRIORITY_STORE, STORE_IMMEDIATE_OPCODE) ||
        !cell_at(reset + MP_VOICE_RESET_PRIORITY_CELL, &cell) || cell != out->cell ||
        !cell_at(reset + MP_VOICE_RESET_VOLUME_CELL, &cell) || cell != rule->volume_cell) {
        return "playByName's reset does not write back the fields the setter stores";
    }
    if (!memory_read_u32(reset + MP_VOICE_RESET_PRIORITY_VALUE, &value) || value == 0u ||
        value >= (uint32_t)MP_VOICE_PRESENTED_PRIORITY) {
        return "the resting priority is not a priority below the one a presented voice is handed";
    }
    out->resting     = (int32_t)value;
    out->named_at[0] = (uintptr_t)arm + MP_VOICE_PRIORITY_ARM_STORE;
    out->named_at[1] = reset + MP_VOICE_RESET_PRIORITY_STORE;
    return NULL;
}
