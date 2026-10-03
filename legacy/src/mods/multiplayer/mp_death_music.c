/* mp_death_music.c: the music of this player across his own death, bound to the engine. What is
 * owed and when is the rule's header; this file finds the two setters and their latches and
 * carries the rule out.
 *
 * Everything is read out of the retail death entry, which names both setters with the two cues the
 * rule is about, fifty nine bytes in:
 *
 *     004500EB  68 E8 03 00 00     push 1000              the null state, the silence
 *     004500F0  E8 AE 04 FC FF     call bapMusicSetState
 *     004500F5  83 C4 04           add  esp,4
 *     004500F8  6A 00 6A 00 6A 00  push 0, three times
 *     004500FE  68 B4 0A 00 00     push 0xAB4             the death piece
 *     00450103  E8 06 05 FC FF     call bapMusicSetSequence
 *     00450108  83 C4 10           add  esp,0x10
 *
 * The window lies behind the six byte prologue the damage post's detour occupies, and the two call
 * distances are the only bytes of it that are not compared.
 *
 * Each setter names its latch in the compare that makes an equal cue a no-op:
 *
 *     bapMusicSetState, the compare seventeen bytes in and its operand at +0x16:
 *     004105B4  8B 45 08           mov eax,[ebp+8]
 *     004105B7  3B 05 1C A4 4A 00  cmp eax,[state latch]
 *     004105BD  75 04              jnz
 *
 *     bapMusicSetSequence, the compare twenty two bytes in and its operand at +0x1B:
 *     00410624  8B 45 08           mov eax,[ebp+8]
 *     00410627  3B 05 20 A4 4A 00  cmp eax,[sequence latch]
 *     0041062D  75 07              jnz
 *
 * Both compares lie behind the prologues a diagnostic's detour of the setters occupies, eleven and
 * six bytes. A setter whose call another module has pointed somewhere else does not show the
 * compare where it is looked for, and the binding is then refused with a line.
 *
 * The state setter is called through the address the death entry calls it by, so a detour on it
 * sees this call as it sees the engine's. The sequence setter is never called: its latch is
 * released by a write into the cell, which is what the engine does to both latches before it sets
 * the cues of a restored savegame.
 */
#include "mp_death_music.h"

#include "mp_death_music_rule.h"
#include "mp_signatures.h"

#include "common/logging.h"
#include "common/memory.h"
#include "common/patch.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* bapMusicSetState (cue), cdecl: the death entry pushes one argument and takes four bytes back. */
typedef int32_t(__cdecl *set_state_fn_t)(int32_t cue);

/* The two music calls inside the death entry. */
#define DEATH_MUSIC_OFFSET  0x3Bu
#define DEATH_STATE_CALL    0x05u
#define DEATH_SEQUENCE_CALL 0x18u

static const uint8_t SIG_DEATH_MUSIC[] = {
    0x68, 0xE8, 0x03, 0x00, 0x00, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x83, 0xC4,
    0x04, 0x6A, 0x00, 0x6A, 0x00, 0x6A, 0x00, 0x68, 0xB4, 0x0A, 0x00, 0x00,
    0xE8, 0x00, 0x00, 0x00, 0x00, 0x83, 0xC4, 0x10
};
static const uint8_t MSK_DEATH_MUSIC[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF
};
_Static_assert(sizeof SIG_DEATH_MUSIC == sizeof MSK_DEATH_MUSIC, "a mask byte for every byte");
_Static_assert(DEATH_SEQUENCE_CALL + 5u <= sizeof SIG_DEATH_MUSIC, "both calls lie in the window");

/* The compare a setter names its latch in: `mov eax,[ebp+8]`, `cmp eax,[latch]`, and a short `jnz`
 * behind the operand. */
static const uint8_t LATCH_COMPARE[] = { 0x8B, 0x45, 0x08, 0x3B, 0x05 };
#define LATCH_OPERAND       (sizeof LATCH_COMPARE)
#define LATCH_BRANCH        (LATCH_OPERAND + sizeof(uint32_t))
#define LATCH_BYTES         (LATCH_BRANCH + 1u)
#define OPCODE_JNZ_SHORT    0x75u
#define STATE_COMPARE_AT    0x11u
#define SEQUENCE_COMPARE_AT 0x16u

/* A line for each of the first judgements of a process; the report counts them all. */
#define MUSIC_LINES_MAX 8u

typedef struct death_music_state {
    bool             tried;
    bool             bound;
    set_state_fn_t   set_state;
    uintptr_t        state_latch;
    uintptr_t        sequence_latch;
    mp_death_music_t rule;

    uint32_t unread;            /* looks on which a latch did not read */
    uint32_t landings;
    uint32_t judged;
    uint32_t beds_back;
    uint32_t sequences_freed;
    uint32_t free_failed;       /* the write into the sequence latch was refused */
    uint32_t lines;
} death_music_state_t;

static death_music_state_t dm;

/* The latch a setter compares its argument with, or false where the compare is not there. */
static bool latch_of(uintptr_t setter, size_t compare_at, uintptr_t *latch)
{
    uint8_t  bytes[LATCH_BYTES];
    uint32_t cell = 0u;

    if (!memory_read(setter + compare_at, bytes, sizeof bytes) ||
        memcmp(bytes, LATCH_COMPARE, sizeof LATCH_COMPARE) != 0 ||
        bytes[LATCH_BRANCH] != OPCODE_JNZ_SHORT) {
        return false;
    }
    /* A latch is a cell of the image's own data. An operand that names anything else was not cut
     * from the setter, and writing through it would write into a stranger. */
    memcpy(&cell, bytes + LATCH_OPERAND, sizeof cell);
    if (!memory_is_inside_image((uintptr_t)cell, sizeof(int32_t)) ||
        !memory_is_readable_range((uintptr_t)cell, sizeof(int32_t))) {
        return false;
    }
    *latch = (uintptr_t)cell;
    return true;
}

/* NULL when everything is bound, and otherwise the reason it is not. */
static const char *bind_the_music(void)
{
    uint8_t   window[sizeof SIG_DEATH_MUSIC];
    uintptr_t death    = mp_signatures_address(MP_SITE_PLR_ENTER_DEATH);
    uintptr_t state    = 0u;
    uintptr_t sequence = 0u;
    size_t    i;

    if (death == 0u) {
        return "the death entry did not resolve";
    }
    if (!memory_read(death + DEATH_MUSIC_OFFSET, window, sizeof window)) {
        return "the death entry does not read where its music calls are";
    }
    for (i = 0u; i < sizeof window; ++i) {
        if ((window[i] & MSK_DEATH_MUSIC[i]) != (SIG_DEATH_MUSIC[i] & MSK_DEATH_MUSIC[i])) {
            return "the death entry does not call the music where the retail one does";
        }
    }
    if (!patch_read_call_target(death + DEATH_MUSIC_OFFSET + DEATH_STATE_CALL, &state) ||
        !patch_read_call_target(death + DEATH_MUSIC_OFFSET + DEATH_SEQUENCE_CALL, &sequence)) {
        return "a music call of the death entry does not lead into the image";
    }
    if (!latch_of(state, STATE_COMPARE_AT, &dm.state_latch)) {
        return "the state setter does not show the compare with its latch";
    }
    if (!latch_of(sequence, SEQUENCE_COMPARE_AT, &dm.sequence_latch)) {
        return "the sequence setter does not show the compare with its latch";
    }
    if (dm.state_latch == dm.sequence_latch) {
        return "the two setters name one latch";
    }
    dm.set_state = (set_state_fn_t)state;
    return NULL;
}

/* Bound once, at the first look of a session. The sites are resolved long before that. */
static bool bound(void)
{
    const char *why;

    if (dm.tried) {
        return dm.bound;
    }
    dm.tried = true;
    why = bind_the_music();
    if (why != NULL) {
        log_warning("the music cannot be put back after this player's death: %s. After a death "
                    "the music stays silent until the player walks through the next sound place, "
                    "and a second death plays no death piece, as before", why);
        return false;
    }
    dm.bound = true;
    log_info("the music is kept across this player's death: the state setter at %08X with its "
             "latch at %08X, the sequence latch at %08X; the bed he had is put back once he "
             "stands again, and the death piece is taken out of the sequence latch",
             (unsigned)(uintptr_t)dm.set_state, (unsigned)dm.state_latch,
             (unsigned)dm.sequence_latch);
    return true;
}

static bool read_the_latches(int32_t *state, int32_t *sequence)
{
    uint32_t raw_state    = 0u;
    uint32_t raw_sequence = 0u;

    if (!memory_try_read_u32(dm.state_latch, &raw_state) ||
        !memory_try_read_u32(dm.sequence_latch, &raw_sequence)) {
        return false;
    }
    *state    = (int32_t)raw_state;
    *sequence = (int32_t)raw_sequence;
    return true;
}

/* The judgement, carried out. The latch is released before the setter is called, so the line
 * reads both as they stand when everything is done. */
static void carry_out(const mp_death_music_look_t *look, const mp_death_music_act_t *act)
{
    const int32_t free_cue       = MP_DEATH_MUSIC_LATCH_FREE;
    int32_t       state_after    = look->state;
    int32_t       sequence_after = look->sequence;
    bool          freed          = false;

    ++dm.judged;
    if (act->free_sequence) {
        freed = memory_try_write(dm.sequence_latch, &free_cue, sizeof free_cue);
        if (freed) {
            ++dm.sequences_freed;
        } else {
            ++dm.free_failed;
        }
    }
    if (act->put_bed_back) {
        (void)dm.set_state(act->bed);
        ++dm.beds_back;
    }
    if (dm.lines >= MUSIC_LINES_MAX) {
        return;
    }
    ++dm.lines;
    (void)read_the_latches(&state_after, &sequence_after);
    log_info("the music after this player's death, half a second after he stood up: the bed he "
             "had was %d and the state latch read %d, %s; the sequence latch read %d, %s; the "
             "latches now read state %d, sequence %d",
             dm.rule.have_bed ? (int)dm.rule.bed : -1, (int)look->state,
             act->put_bed_back ? "so the bed was put back through the engine's own setter"
                               : "so nothing was set",
             (int)look->sequence,
             !act->free_sequence ? "which is not the death piece, so it was left"
             : freed ? "the death piece, and was released"
                     : "the death piece, and the write that releases it was refused",
             (int)state_after, (int)sequence_after);
}

void mp_death_music_tick(bool lives, bool corpse, uint32_t world, uint32_t now_ms)
{
    mp_death_music_look_t look;
    mp_death_music_act_t  act;

    if (!bound()) {
        return;
    }
    memset(&look, 0, sizeof look);
    memset(&act, 0, sizeof act);
    look.world        = world;
    look.now_ms       = now_ms;
    look.lives        = lives;
    look.corpse       = corpse;
    look.latches_read = read_the_latches(&look.state, &look.sequence);
    if (!look.latches_read) {
        ++dm.unread;
    }
    mp_death_music_step(&dm.rule, &look, &act);
    if (act.landed) {
        ++dm.landings;
    }
    if (act.judged) {
        carry_out(&look, &act);
    }
}

void mp_death_music_report(void)
{
    if (!dm.tried) {
        log_info("  the music across a death: never asked about, no frame of a session ran");
        return;
    }
    if (!dm.bound) {
        log_info("  the music across a death: NOT bound, the setters or their latches did not "
                 "resolve");
        return;
    }
    log_info("  the music across a death: %u return(s) of this player from a death, %u judged "
             "half a second later; the bed put back %u time(s), the death piece taken out of the "
             "sequence latch %u time(s) and that write refused %u time(s); %u look(s) on which a "
             "latch did not read%s",
             (unsigned)dm.landings, (unsigned)dm.judged, (unsigned)dm.beds_back,
             (unsigned)dm.sequences_freed, (unsigned)dm.free_failed, (unsigned)dm.unread,
             dm.rule.settling ? "; one return is still inside its half second at the report" : "");
}
