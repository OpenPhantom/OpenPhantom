/* mp_signatures_script_sound.h: where an actor's script plays a sound, and the two engine routines
 * the multiplayer calls for it, as byte patterns.
 *
 * Layer 2. Three sites:
 *
 *   the CALL the script opcode for a sound makes, found by the instructions that push its three
 *   arguments and ended on the E8 with its operand masked: the call is repointed while a session
 *   is armed, and its pattern and every other one have to find it again afterwards;
 *
 *   the routine that call reaches, "play sound call N at P", found by its body: the pattern pins
 *   the two fields of the world record it reads, the count of sound calls and the table of them,
 *   and the record's size, which the multiplayer reads with it; the world cell it names is read
 *   out and has to be the one the cell table resolved;
 *
 *   the routine that keeps a playing channel where it is and lets the caller's position go, found
 *   by its head with the channel bank's operand masked. sound_lifetime_fix hulls it, so the
 *   head may be a branch.
 */
#ifndef MULTIPLAYER_MP_SIGNATURES_SCRIPT_SOUND_H
#define MULTIPLAYER_MP_SIGNATURES_SCRIPT_SOUND_H

#include "common/signature.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum mp_script_sound_site {
    MP_SCRIPT_SOUND_SITE_CALL,        /* 0x0043504B  the sound opcode's call, E8 at +31 */
    MP_SCRIPT_SOUND_SITE_PLAY_CALL,   /* 0x00417143  bapsound_playCall */
    MP_SCRIPT_SOUND_SITE_PIN_CHANNEL, /* 0x00417826  bapsound_pinChannel */
    MP_SCRIPT_SOUND_SITE_COUNT
} mp_script_sound_site_t;

/* The two fields of the world record the play routine reads, and the size of one sound call
 * record, all three pinned by the play routine's own pattern. */
#define MP_SCRIPT_SOUND_LEVEL_CALL_COUNT 0xCC4u
#define MP_SCRIPT_SOUND_LEVEL_CALLS      0xCC8u
#define MP_SCRIPT_SOUND_RECORD_BYTES     0x40u

/* The channel bank the pin routine indexes, and the fields of one channel the multiplayer reads:
 * the sound it holds (0 for a free slot) and the cell it writes -1 into when it ends. */
#define MP_SCRIPT_SOUND_CHANNELS        12
#define MP_SCRIPT_SOUND_CHANNEL_STRIDE  0x80u
#define MP_SCRIPT_SOUND_CHANNEL_REF     0x0Cu
#define MP_SCRIPT_SOUND_CHANNEL_OWNER   0x78u

/* Resolves the three sites once and answers the same afterwards; the first call logs one line per
 * site and how long the search took. Answers how many resolved. */
size_t mp_signatures_script_sound_resolve(void);

/* The address of the sound opcode's E8, or 0 when its site did not resolve or the byte there is
 * not an E8. */
uintptr_t mp_signatures_script_sound_call(void);

/* The play routine and the pin routine, 0 when their site did not resolve. */
uintptr_t mp_signatures_script_sound_address(mp_script_sound_site_t site);

/* The world cell the play routine reads, and the channel bank the pin routine indexes, read out of
 * their operands; false when they did not read. */
bool mp_signatures_script_sound_level_cell(uintptr_t *cell);
bool mp_signatures_script_sound_channel_bank(uintptr_t *bank);

/* The table, for the unit test that holds its shape. */
const signature_t *mp_signatures_script_sound_sites(size_t *count);

#endif /* MULTIPLAYER_MP_SIGNATURES_SCRIPT_SOUND_H */
