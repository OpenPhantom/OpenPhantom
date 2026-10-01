/* unittests/mp_voice_stand_in.c: the stand-in engine and world the tests of mp_voice run against.
 * See the header. */
#include "mp_voice_stand_in.h"

#include "unittest.h"

#include "mp_armed.h"
#include "mp_bridge.h"
#include "mp_bridge_drain.h"
#include "mp_bridge_lobby.h"
#include "mp_cutscene.h"
#include "mp_lobby.h"
#include "mp_own_body.h"
#include "mp_range_gate.h"
#include "mp_scene_host.h"
#include "mp_voice.h"
#include "mp_voice_bind.h"
#include "mp_voice_rule.h"

#include "common/host_image.h"
#include "common/logging.h"
#include "common/text.h"

#include <windows.h>

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define MODULE_PROLOGUE 5u

/* Where a channel keeps its flags, its sound reference, its priority and its owner's handle, as
 * the engine lays a channel out. */
#define CHANNEL_FLAGS    0x10u
#define CHANNEL_REF      0x0Cu
#define CHANNEL_PRIORITY 0x70u
#define CHANNEL_OWNER    0x78u

stand_in_t si;

const uint32_t si_actor_body = 0xB0D1u;
const uint32_t si_other_body = 0x07E4u;

const float SI_LINE_AT[3] = { 0.0f, 0.0f, 0.0f };

const si_call_t SI_ENGINE_PLAYS = { 1, false, true, 5 };

static int32_t next_line = 100;

/* ==============================================================================================
 * The log, kept here so the report can be read back. Every entry of common/logging is defined,
 * so the linker never takes the library's file, and nothing is written to disk.
 * ============================================================================================ */

#define LINES_KEPT 512u
#define LINE_BYTES 1024u

static char   kept[LINES_KEPT][LINE_BYTES];
static size_t kept_count;

static void keep(const char *format, va_list arguments)
{
    char *line = kept[kept_count % LINES_KEPT];

    (void)text_vformat(line, LINE_BYTES, format, arguments);
    ++kept_count;
}

void log_init(const char *feature_name, bool truncate)
{
    (void)feature_name;
    (void)truncate;
}

void log_shutdown(void)
{
}

void log_info(const char *format, ...)
{
    va_list arguments;

    va_start(arguments, format);
    keep(format, arguments);
    va_end(arguments);
}

void log_warning(const char *format, ...)
{
    va_list arguments;

    va_start(arguments, format);
    keep(format, arguments);
    va_end(arguments);
}

void log_error(const char *format, ...)
{
    va_list arguments;

    va_start(arguments, format);
    keep(format, arguments);
    va_end(arguments);
}

const char *log_path(void)
{
    return "";
}

size_t si_logged_count(const char *text)
{
    size_t index;
    size_t found = 0u;
    size_t count = kept_count < LINES_KEPT ? kept_count : LINES_KEPT;

    for (index = 0u; index < count; ++index) {
        found += strstr(kept[index], text) != NULL ? 1u : 0u;
    }
    return found;
}

bool si_logged(const char *text)
{
    return si_logged_count(text) != 0u;
}

void si_forget_the_log(void)
{
    kept_count = 0u;
}

/* ==============================================================================================
 * What the module asks for.
 * ============================================================================================ */

static void __cdecl fake_set_field(int32_t field, float value)
{
    if (field == 0) {
        si.volume = value;
    } else if (field == 5) {
        si.priority = (int32_t)value;
    }
}

bool mp_bridge_lobby_setup(mp_lobby_setup_t *out)
{
    if (out == NULL || !si.note_known) {
        return false;
    }
    memset(out, 0, sizeof *out);
    out->flags = si.flags;
    return true;
}

bool mp_bridge_drain_is_client(void)
{
    return si.is_client;
}

bool mp_bridge_joined(void)
{
    return si.joined;
}

/* Asked by the session's other reading, which these tests do not exercise; a client's host is on
 * the line exactly while this world is joined, and the transport is the menu's. */
uint64_t mp_bridge_drain_host_connection(void)
{
    return si.joined ? 1u : 0u;
}

bool mp_bridge_lobby_connected(void)
{
    return si.joined;
}

bool mp_bridge_armed_by_menu(void)
{
    return true;
}

int32_t mp_cutscene_lock_level(void)
{
    return si.lock;
}

void mp_cutscene_set_line_camera(mp_cutscene_line_camera_fn_t judge)
{
    si.line_camera = judge;
}

bool mp_own_body_place(float out[3], bool *off_the_model)
{
    if (off_the_model != NULL) {
        *off_the_model = false;
    }
    if (!si.body_known) {
        return false;
    }
    memcpy(out, si.body, sizeof si.body);
    return true;
}

bool mp_range_gate_player(size_t bank, float out[3])
{
    if (out == NULL || bank == 0u || bank > si.far_players) {
        return false;
    }
    memcpy(out, si.far_at, sizeof si.far_at);
    return true;
}

bool mp_scene_for_all(mp_scene_known_t *known)
{
    if (known != NULL) {
        memset(known, 0, sizeof *known);
        known->anchor_known = si.anchor_known;
        known->gathered     = si.gathered;
        known->serial       = si.serial;
        memcpy(known->anchor, si.anchor, sizeof known->anchor);
    }
    return si.scene_for_all;
}

const char *mp_voice_bind_cells(mp_voice_cells_t *out)
{
    memset(out, 0, sizeof *out);
    if (si.module_proc == NULL) {
        return "the stand-in dialogue module could not be built";
    }
    out->reach           = SI_REACH;
    out->reach_at[0]     = 0x004172E4u;
    out->reach_at[1]     = 0x004172F3u;
    out->set_field       = (uintptr_t)&fake_set_field;
    out->volume_cell     = (uintptr_t)&si.volume;
    out->eye_cell        = (uintptr_t)&si.eye_pointer;
    out->option_cell     = (uintptr_t)&si.option;
    out->shown_cell      = (uintptr_t)&si.shown;
    out->bark_cell       = (uintptr_t)&si.bark;
    out->speaker_cell    = (uintptr_t)&si.speaker_held;
    out->restart_cell    = (uintptr_t)&si.restart_latch;
    out->module_proc     = (uintptr_t)si.module_proc;
    out->module_prologue = MODULE_PROLOGUE;
    return NULL;
}

const char *mp_voice_bind_reply(mp_voice_reply_site_t *out)
{
    memset(out, 0, sizeof *out);
    return "the reply is not voiced in this test";
}

/* The voice's own numbers, 4.0f and 8.0f at the lock level 5, made radii by the rule itself. */
const char *mp_voice_bind_radii(const mp_voice_cells_t *rule, float factor_raw,
                                mp_voice_hear_cells_t *hear, mp_voice_hearing_t *hearing,
                                bool *factor_as_given)
{
    float factor = mp_voice_hear_factor(factor_raw, factor_as_given);

    memset(hear, 0, sizeof *hear);
    memset(hearing, 0, sizeof *hearing);
    hear->min_free_bits  = 0x40800000u;
    hear->min_scene_bits = 0x41000000u;
    hear->lock_level     = 5;
    hear->free_at        = 0x00417321u;
    hear->scene_at       = 0x00417310u;
    hear->lock_at        = 0x00417302u;
    if (si.hearing_refused ||
        !mp_voice_hear_from(hear->min_free_bits, hear->min_scene_bits, hear->lock_level, 5,
                            factor, rule->reach, hearing)) {
        hearing->free   = rule->reach;
        hearing->scene  = rule->reach;
        hearing->admit  = rule->reach;
        hearing->factor = factor;
        hearing->lock   = 5;
        return "the stand-in's radii are refused";
    }
    return NULL;
}

const char *mp_voice_bind_answer(const mp_voice_cells_t *rule, mp_voice_answer_cells_t *out)
{
    (void)rule;
    memset(out, 0, sizeof *out);
    out->voices_cell      = (uintptr_t)&si.voices;
    out->latch_cell       = (uintptr_t)&si.latch;
    out->bank.base        = (uintptr_t)si.bank;
    out->bank.count       = SI_CHANNELS;
    out->bank.stride      = SI_CHANNEL_BYTES;
    out->bank.flags_at    = CHANNEL_FLAGS;
    out->bank.free_bit    = SI_FREE_BIT;
    out->bank.playing_bit = SI_PLAYING_BIT;
    out->bank.priority_at = CHANNEL_PRIORITY;
    out->bank.owner_at    = CHANNEL_OWNER;
    out->bank.ref_at      = CHANNEL_REF;
    return NULL;
}

const char *mp_voice_bind_priority(const mp_voice_cells_t *rule, mp_voice_priority_cells_t *out)
{
    (void)rule;
    memset(out, 0, sizeof *out);
    out->cell        = (uintptr_t)&si.priority;
    out->resting     = SI_PRIORITY_RESTING;
    out->named_at[0] = 0x00416766u;
    out->named_at[1] = 0x004172A1u;
    return NULL;
}

/* ==============================================================================================
 * The bank.
 * ============================================================================================ */

static void put_u32(size_t channel, size_t at, uint32_t value)
{
    memcpy(si.bank + channel * SI_CHANNEL_BYTES + at, &value, sizeof value);
}

void si_bank_free(size_t channel)
{
    put_u32(channel, CHANNEL_FLAGS, SI_FREE_BIT);
    put_u32(channel, CHANNEL_PRIORITY, 0u);
    put_u32(channel, CHANNEL_OWNER, 0u);
    put_u32(channel, CHANNEL_REF, 0u);
}

void si_bank_clear(void)
{
    size_t channel;

    for (channel = 0u; channel < SI_CHANNELS; ++channel) {
        si_bank_free(channel);
    }
}

void si_bank_hold(size_t channel, uint32_t priority, bool playing, const void *owner)
{
    put_u32(channel, CHANNEL_FLAGS, playing ? SI_PLAYING_BIT : 0u);
    put_u32(channel, CHANNEL_PRIORITY, priority);
    put_u32(channel, CHANNEL_OWNER, (uint32_t)(uintptr_t)owner);
    put_u32(channel, CHANNEL_REF, (uint32_t)(uintptr_t)&si.ref);
}

uint32_t si_bank_owner(size_t channel)
{
    uint32_t owner = 0u;

    memcpy(&owner, si.bank + channel * SI_CHANNEL_BYTES + CHANNEL_OWNER, sizeof owner);
    return owner;
}

/* ==============================================================================================
 * The stand-in engine.
 * ============================================================================================ */

static uint8_t *make_module_proc(void)
{
    uint8_t *page = (uint8_t *)VirtualAlloc(NULL, 0x1000, MEM_COMMIT | MEM_RESERVE,
                                            PAGE_EXECUTE_READWRITE);
    uint32_t cell = (uint32_t)(uintptr_t)&si.option;

    if (page == NULL) {
        return NULL;
    }
    page[0] = 0xA1u;
    memcpy(page + 1, &cell, sizeof cell);
    page[5] = 0xC3u;
    FlushInstructionCache(GetCurrentProcess(), page, 0x1000);
    return page;
}

void si_set_the_world(bool note_known, uint8_t flags, bool is_client, bool joined)
{
    static const float far_off[3] = { 5000.0f, 0.0f, 0.0f };

    si.note_known      = note_known;
    si.flags           = flags;
    si.is_client       = is_client;
    si.joined          = joined;
    si.body_known      = true;
    memcpy(si.body, far_off, sizeof si.body);
    si.far_players     = 0u;
    si.scene_for_all   = false;
    si.anchor_known    = false;
    si.gathered        = false;
    si.serial          = 0u;
    si.lock            = 0;
    si.voices          = 1;
    si.volume          = SI_RESTING;
    si.priority        = SI_PRIORITY_RESTING;
    si.call            = SI_ENGINE_PLAYS;
    si.bark            = -1;
    si.block_active    = 1u;
    si.eye_pointer     = (uint32_t)(uintptr_t)si.eye;
    si_bank_clear();
    mp_armed_set_transport(true, !is_client);
}

int32_t si_next_line(void)
{
    return ++next_line;
}

/* What the stand-in engine does inside the speak entry's call, in the engine's own order: the
 * voice option, the record, the latch against the same line, and then the voice. A placed voice
 * reads fields 0 and 5 and has them put back to rest by playByName; its channel is held for the
 * line with the line's handle as its owner, as bapsound_play leaves it. */
static void the_engine_voices(si_spoken_t *said)
{
    int32_t priority = SI_PRIORITY_RESTING;

    if (si.call.starts == 0 || si.voices == 0 || !si.call.has_a_record ||
        si.latch == said->line) {
        return;
    }
    si.latch = said->line;
    si.bark  = -1;
    if (said->handed != NULL) {
        priority    = si.priority;
        si.volume   = SI_RESTING;
        si.priority = SI_PRIORITY_RESTING;
    }
    if (si.call.channel >= 0) {
        si.bark = si.call.channel;
        si_bank_hold((size_t)si.call.channel, (uint32_t)priority, true, &si.bark);
    }
}

si_spoken_t si_say_line(const void *speaker, const float *position, int32_t line)
{
    si_spoken_t said;

    said.line            = line;
    si.speaker_held      = si.call.holds_the_speaker ? (uint32_t)(uintptr_t)speaker : 0u;
    said.handed          = mp_voice_line_begin(speaker, said.line, position);
    said.volume_during   = si.volume;
    said.priority_during = si.priority;
    said.camera          = si.line_camera == NULL || si.line_camera();
    the_engine_voices(&said);
    mp_voice_line_end(said.line, si.call.starts);
    said.volume_after   = si.volume;
    said.priority_after = si.priority;
    return said;
}

si_spoken_t si_say(const void *speaker, const float *position)
{
    return si_say_line(speaker, position, si_next_line());
}

si_spoken_t si_speak(void)
{
    return si_say(&si_other_body, SI_LINE_AT);
}

bool si_say_again_line(int32_t line)
{
    si_spoken_t       said;
    mp_voice_replay_t replay;

    said.line   = line;
    replay      = mp_voice_replay_begin(MP_VOICE_FROM_HOST, said.line, SI_LINE_AT);
    said.handed = replay.place;
    if (!replay.say) {
        return false;
    }
    the_engine_voices(&said);
    mp_voice_replay_end(said.line, si.call.starts);
    return true;
}

bool si_say_again(void)
{
    return si_say_again_line(si_next_line());
}

typedef uint32_t(__cdecl *module_proc_fn_t)(int32_t message, int32_t argument, float dt);

uint32_t si_message(int32_t which)
{
    return ((module_proc_fn_t)(uintptr_t)si.module_proc)(which, 0, 0.016f);
}

bool si_start(void)
{
    ut_check(host_image_resolve(), "this test's own image stands in for the engine's");
    si.module_proc = make_module_proc();
    si.eye_pointer = (uint32_t)(uintptr_t)si.eye;
    memcpy(si.eye, SI_LINE_AT, sizeof si.eye);
    (void)text_format(si.ref.name, sizeof si.ref.name, "QGm3219.wav");
    si.latch = -1;
    si_set_the_world(false, 0u, false, false);
    return mp_voice_install((uintptr_t)&si.block_active) && mp_voice_reach() == SI_REACH;
}
