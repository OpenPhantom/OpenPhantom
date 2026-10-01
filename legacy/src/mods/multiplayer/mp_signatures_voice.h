/* mp_signatures_voice.h: the places a spoken line is judged and heard through, as byte patterns.
 *
 * Layer 2. Its own table, as the crates and the scenes have theirs, because the main table's file
 * is at its size limit. One subject: where a line's voice, its subtitle and the reply of the player
 * reach the engine, and where the engine's answer to a voice can be read. Only the dialogue
 * module's head is hulled and only the reply's call is repointed; every other site is read, for a
 * number, a cell or the address a call names, and each of those is named by two sites that have to
 * agree.
 */
#ifndef MULTIPLAYER_MP_SIGNATURES_VOICE_H
#define MULTIPLAYER_MP_SIGNATURES_VOICE_H

#include "common/signature.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum mp_voice_site {
    MP_VOICE_SITE_PLAY,           /* 0x004172B4  bapsound_playVoice, its reach and its setter */
    MP_VOICE_SITE_SET_FIELD,      /* 0x0041670F  bapsound_setField, and the cell of field 0 */
    MP_VOICE_SITE_EYE,            /* 0x00416A69  startChannel picks the eye for a placed voice */
    MP_VOICE_SITE_RENDER_OPTION,  /* 0x00430502  Dialog_Render draws the subtitle row */
    MP_VOICE_SITE_UPDATE_OPTION,  /* 0x0043089A  Dialog_Update measures the subtitle row */
    MP_VOICE_SITE_MODULE_PROC,    /* 0x004302F5  dialog_moduleProc, hulled for the frame message */
    MP_VOICE_SITE_VOICE_CALLS,    /* 0x0043133B  Dialog_PlayVoice voices a reply and a bark */
    MP_VOICE_SITE_HEAR,           /* 0x00417302  bapsound_playVoice: the lock and the two radii */
    MP_VOICE_SITE_BY_NAME_RESET,  /* 0x0041726F  bapsound_playByName puts the six fields back */
    MP_VOICE_SITE_CHANNEL_SEARCH, /* 0x00416B1B  bapsound_startChannel seeks a free or lower one */
    MP_VOICE_SITE_FREE_CHANNEL,   /* 0x0041756F  bapsound_freeChannel, the bank and the owner */
    MP_VOICE_SITE_OPTION,         /* 0x00431263  Dialog_PlayVoice asks the voice option */
    MP_VOICE_SITE_LATCH,          /* 0x004312E4  Dialog_PlayVoice's latch against the same line */
    MP_VOICE_SITE_RENDER_VOICES,  /* 0x0043045D  Dialog_Render closes a line whose voice went */
    MP_VOICE_SITE_COUNT
} mp_voice_site_t;

/* Where in each pattern the numbers, cells and calls stand. Offsets from the pattern's start. */
#define MP_VOICE_PLAY_RANGE_PUSH      0x30u   /* push 100.0f, the range */
#define MP_VOICE_PLAY_FAR_PUSH        0x3Fu   /* push 100.0f, the distance the engine admits by */
#define MP_VOICE_PLAY_PUSH_IMMEDIATE  1u      /* the float behind each push's opcode */
#define MP_VOICE_PLAY_RANGE_SET_CALL  0x37u   /* call bapsound_setField(2, ...) */
#define MP_VOICE_PLAY_FAR_SET_CALL    0x46u   /* call bapsound_setField(4, ...) */
#define MP_VOICE_SET_FIELD_TABLE      0x16u   /* the jump table of the field switch */
#define MP_VOICE_SET_FIELD_FIRST_ARM  0x1Au   /* the arm of field 0, the table's first entry */
#define MP_VOICE_SET_FIELD_VOLUME     0x1Fu   /* mov [field 0], edx */
#define MP_VOICE_EYE_CELL             0x0Fu   /* mov ecx, [the pointer to the eye] */
#define MP_VOICE_RENDER_OPTION_CELL   0x13u   /* cmp [subtitles on], 0 */
#define MP_VOICE_RENDER_SHOWN_CELL    0x24u   /* mov ecx, [the line on show] */
#define MP_VOICE_RENDER_DRAW_CALL     0x2Fu   /* call DLG_DrawLine */
#define MP_VOICE_UPDATE_OPTION_CELL   0x08u
#define MP_VOICE_UPDATE_SHOWN_CELL    0x14u
#define MP_VOICE_UPDATE_DRAW_CALL     0x1Du   /* call DLG_DrawLine */
#define MP_VOICE_CALLS_PLACE_OFFSET   0x0Du   /* add edx, the place inside a body */
#define MP_VOICE_CALLS_REPLY          0x1Eu   /* call bapsound_playVoice for the reply: repointed */
#define MP_VOICE_CALLS_BARK_RESET     0x2Au   /* mov [the bark channel], -1 */
#define MP_VOICE_CALLS_BARK_HANDLE    0x37u   /* push &the bark channel */
#define MP_VOICE_CALLS_BARK           0x3Fu   /* call bapsound_playVoice for a line */

/* The lock push and the two full volume radii stand right behind the voice's own pattern, in the
 * same function: push 5, call the lock test, then push 8.0f or 4.0f for field 3. */
#define MP_VOICE_HEAR_BEHIND_PLAY     0x4Eu   /* the hear pattern's distance from the voice's */
#define MP_VOICE_HEAR_LOCK_PUSH       0x00u   /* push the lock level, one byte */
#define MP_VOICE_HEAR_SCENE_PUSH      0x0Eu   /* push the radius under a scene's lock */
#define MP_VOICE_HEAR_SCENE_SET_CALL  0x15u   /* call bapsound_setField(3, ...) */
#define MP_VOICE_HEAR_FREE_PUSH       0x1Fu   /* push the radius outside it */
#define MP_VOICE_HEAR_FREE_SET_CALL   0x26u   /* call bapsound_setField(3, ...) */

/* bapsound_setField's arm for field 5, the priority: fld the value, call the float to int
 * conversion, store eax. The arm is the table's sixth entry. */
#define MP_VOICE_SET_FIELD_PRIORITY   5u
#define MP_VOICE_PRIORITY_ARM_STORE   0x08u   /* mov [g_fldPriority], eax behind fld and call */

/* playByName writing the six field defaults back after a call that used them. */
#define MP_VOICE_RESET_VOLUME_CELL    0x02u   /* mov [field 0], 2.0f */
#define MP_VOICE_RESET_PRIORITY_STORE 0x32u   /* mov [field 5], the resting priority */
#define MP_VOICE_RESET_PRIORITY_CELL  0x34u
#define MP_VOICE_RESET_PRIORITY_VALUE 0x38u

/* The channel search of bapsound_startChannel: the count it walks, the flags it tests for a free
 * channel and the bit, and the priority it compares twice. */
#define MP_VOICE_SEARCH_COUNT         0x15u   /* cmp [i], the number of channels */
#define MP_VOICE_SEARCH_FLAGS_CELL    0x24u   /* mov edx, [i*stride + the first channel's flags] */
#define MP_VOICE_SEARCH_FREE_BIT      0x2Au   /* and edx, the free bit */
#define MP_VOICE_SEARCH_PRIORITY      0x89u   /* cmp eax, [i*stride + the first priority] */
#define MP_VOICE_SEARCH_PRIORITY_LOAD 0x97u   /* mov edx, the same, for the lowest so far */

/* bapsound_freeChannel behind its prologue: the bank, the flags and the playing bit, the owner's
 * handle named three times and the channel's sound reference. */
#define MP_VOICE_FREE_BANK            0x0Eu   /* add eax, the bank */
#define MP_VOICE_FREE_FLAGS_AT        0x1Au   /* mov edx, [channel + flags] */
#define MP_VOICE_FREE_PLAYING_BIT     0x1Du   /* and edx, the playing bit */
#define MP_VOICE_FREE_OWNER_TEST      0x4Eu   /* cmp [channel + owner], 0 */
#define MP_VOICE_FREE_OWNER_LOAD      0x57u   /* mov ecx, [channel + owner] */
#define MP_VOICE_FREE_OWNER_CLEAR     0x63u   /* mov [channel + owner], 0 */
#define MP_VOICE_FREE_REF_AT          0x6Du   /* cmp [channel + the sound reference], 0 */

/* The voice option where Dialog_PlayVoice asks it first and where Dialog_Render asks it, the
 * latch named where it is compared and where it is set, and the bark channel Render tests. */
#define MP_VOICE_OPTION_CELL          0x02u
#define MP_VOICE_LATCH_COMPARE        0x05u
#define MP_VOICE_LATCH_STORE          0x15u
#define MP_VOICE_RENDER_VOICES_CELL   0x07u
#define MP_VOICE_RENDER_BARK_CELL     0x19u

/* Resolves the table once and answers the same afterwards. Answers how many resolved. */
size_t mp_signatures_voice_resolve(void);

/* 0 when the site did not resolve. */
uintptr_t mp_signatures_voice_address(mp_voice_site_t site);

/* The prologue declared for a function head, for a caller that hulls it; 0 for the others. */
size_t mp_signatures_voice_prologue(mp_voice_site_t site);

/* One site, so a caller can read an operand out of it through the shared reader. */
const signature_t *mp_signatures_voice_site(mp_voice_site_t site);

#endif /* MULTIPLAYER_MP_SIGNATURES_VOICE_H */
