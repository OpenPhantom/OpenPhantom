#include "diag_audio.h"

#include "diag_install.h"
#include "diag_log.h"
#include "diag_names.h"

#include "common/detour.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/signature.h"

#include <windows.h>
#include <intrin.h>
#include <mmsystem.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* --- bapsound_play 0x0041681F ---------------------------------------------------------------- *
 * Prologue 6 bytes (55 8B EC 83 EC 18), clean boundary. */
static const uint8_t SIG_SOUND_PLAY[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x18, 0xA1, 0x88, 0xAE, 0x5B, 0x00, 0x89,
    0x45, 0xFC, 0x83, 0x3D, 0xB8, 0xB4, 0x5B, 0x00
};
#define SOUND_PLAY_PROLOGUE 6u

/* --- bapsound_startChannel 0x004169BD --------------------------------------------------------- *
 * The channel allocation. It is where a voice is REJECTED without the caller ever learning why:
 * no ref, DONT_DUP already active, start distance > maxDist, or the pool is full with nothing
 * quieter to steal. Level 2, because it fires per shot and per footstep. */
static const uint8_t SIG_SOUND_START_CHANNEL[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x44, 0x83, 0x3D, 0xB8, 0xB4, 0x5B, 0x00,
    0x00, 0x75, 0x08, 0x83, 0xC8, 0xFF, 0xE9, 0x6B
};
#define SOUND_START_CHANNEL_PROLOGUE 6u

/* --- bapsound_freeChannel 0x00417567 ---------------------------------------------------------- *
 * The other half of the life cycle, and the more interesting one: this is where a voice dies,
 * distance cull (LOOPs only), voice stealing, an animation change (bapobj_playClip frees the
 * channel) or a level change. Everything that "restarts by itself" restarts because of this.
 *   +0x15 : `05 <imm32>` = add eax, &g_channel[0] -> THE CHANNEL BANK ([0x5BAEA0], 12 x 0x80).
 *           The operand is at +0x16 and is checked against the image before use. */
static const uint8_t SIG_SOUND_FREE_CHANNEL[] = {
    0x55, 0x8B, 0xEC, 0x51, 0x83, 0x7D, 0x08, 0x0C, 0x72, 0x05, 0xE9, 0xDF,
    0x00, 0x00, 0x00, 0x8B, 0x45, 0x08, 0xC1, 0xE0, 0x07, 0x05, 0xA0, 0xAE,
    0x5B, 0x00
};
#define SOUND_FREE_CHANNEL_PROLOGUE 8u
#define OFFSET_CHANNEL_BANK      0x16u

/* --- bapsound_setMasterVolume 0x00417379 ------------------------------------------------------ *
 * 2-D voices take the master from Miles, 3-D voices from the loop INSIDE this function, anyone
 * investigating "only the 3-D sounds are too quiet" starts here. */
static const uint8_t SIG_SOUND_MASTER_VOLUME[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x14, 0x83, 0x3D, 0xB8, 0xB4, 0x5B, 0x00,
    0x00, 0x75, 0x05, 0xE9, 0xC8, 0x00, 0x00, 0x00
};
#define SOUND_MASTER_VOLUME_PROLOGUE 6u

/* --- bapsound_periodic 0x00415D1D ------------------------------------------------------------- *
 * Its 0x0D arm FALLS THROUGH into the 0x0E arm, so it runs per RENDER FRAME *and* per SIMULATION
 * SUBSTEP. Here it is only the clock for the channel census; nothing is logged per call.
 * Prologue 11 bytes (55 8B EC 51 + cmp [imm32],0). The `jne` behind it reads the flags the copied
 * `cmp` sets; a `jmp` changes no flags, so the trampoline return is flag-neutral. */
static const uint8_t SIG_SOUND_PERIODIC[] = {
    0x55, 0x8B, 0xEC, 0x51, 0x83, 0x3D, 0xB8, 0xB4, 0x5B, 0x00, 0x00, 0x75,
    0x07, 0xB8, 0x01, 0x00, 0x00, 0x00, 0xEB, 0x55
};
#define SOUND_PERIODIC_PROLOGUE 11u

/* --- bapsound_activatePlace 0x00417711 / deactivatePlace 0x0041778C --------------------------- *
 * B3D_SPLA = a placement of a sound call, the closest thing this engine has to "entering and
 * leaving an audio volume". Script op 0x213 and the AMAP keyframe bit 0x80 switch them.
 * WARNING: `deactivate` stops no running voice, it only sets active = 0, exactly the kind of
 * asymmetry a "the sound will not stop" report hides behind. */
static const uint8_t SIG_SOUND_PLACE_ON[] = {
    0x55, 0x8B, 0xEC, 0x51, 0x83, 0x3D, 0xB8, 0xB4, 0x5B, 0x00, 0x00, 0x75,
    0x02, 0xEB, 0x68, 0x83, 0x7D, 0x08, 0x00, 0x74
};
#define SOUND_PLACE_ON_PROLOGUE 11u
static const uint8_t SIG_SOUND_PLACE_OFF[] = {
    0x55, 0x8B, 0xEC, 0x83, 0x3D, 0xB8, 0xB4, 0x5B, 0x00, 0x00, 0x75, 0x02,
    0xEB, 0x12, 0x83, 0x7D, 0x08, 0x00, 0x75, 0x02
};
#define SOUND_PLACE_OFF_PROLOGUE 10u

/* --- bapMusicSetState 0x004105A3 -------------------------------------------------------------- *
 * Twenty lines, and that is the WHOLE music "state machine" on the game's side: refuse when not
 * attached, no-op when the cue is already pending, latch, call the DLL. Everything that looks like
 * an automaton lives in IMUSE.DLL and in the muscript records.
 * WARNING: both setters LATCH BEFORE the DLL call. A cue the DLL rejects (GARDEN's 2950 has no
 * muscript record) therefore leaves the latch out of step with what is audible, which is why
 * this hook logs the latch BEFORE and AFTER the call.
 *   +0x06 : &g_musicAttached [0x5BAB8C]  (0 => this function does NOTHING)
 *   +0x16 : &g_stateLatch    [0x4AA41C] */
static const uint8_t SIG_MUSIC_STATE[] = {
    0x55, 0x8B, 0xEC, 0x51, 0x83, 0x3D, 0x8C, 0xAB, 0x5B, 0x00, 0x00, 0x75,
    0x04, 0x33, 0xC0, 0xEB, 0x56, 0x8B, 0x45, 0x08, 0x3B, 0x05, 0x1C, 0xA4,
    0x4A, 0x00
};
#define MUSIC_STATE_PROLOGUE 11u
#define OFFSET_MUSIC_ATTACHED 0x06u
#define OFFSET_STATE_LATCH    0x16u

/* --- bapMusicSetSequence 0x0041060E ----------------------------------------------------------- *
 * The module's only real branch, and it is not musical: `gateType` comes from +0x36 of the
 * triggering sound call, the comparison operand from its priority, and what is measured is a
 * Count of live actors within `distParam`. Type 1 = "only from N nearby", type 2 = "only up to N".
 * Both comparisons are UNSIGNED.
 *   +0x08 : &g_musicAttached, +0x1B : &g_seqLatch [0x4AA420] */
static const uint8_t SIG_MUSIC_SEQUENCE[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x0C, 0x83, 0x3D, 0x8C, 0xAB, 0x5B, 0x00,
    0x00, 0x75, 0x07, 0x33, 0xC0, 0xE9, 0xA4, 0x00, 0x00, 0x00, 0x8B, 0x45,
    0x08, 0x3B, 0x05, 0x20, 0xA4, 0x4A, 0x00, 0x75
};
#define MUSIC_SEQUENCE_PROLOGUE 6u
#define OFFSET_SEQUENCE_LATCH   0x1Bu

/* --- bapMusicSetVolume 0x004106CC ------------------------------------------------------------- *
 * Goes to the DirectSound buffer the DLL fills. NOT through Miles. */
static const uint8_t SIG_MUSIC_VOLUME[] = {
    0x55, 0x8B, 0xEC, 0x51, 0xC7, 0x45, 0xFC, 0x00, 0x00, 0x00, 0x00, 0xD9,
    0x05, 0x18, 0xA4, 0x4A, 0x00, 0xD8, 0x5D, 0x08
};
#define MUSIC_VOLUME_PROLOGUE 11u

enum {
    SITE_SOUND_PLAY,
    SITE_SOUND_START_CHANNEL,
    SITE_SOUND_FREE_CHANNEL,
    SITE_SOUND_MASTER_VOLUME,
    SITE_SOUND_PERIODIC,
    SITE_SOUND_PLACE_ON,
    SITE_SOUND_PLACE_OFF,
    SITE_MUSIC_STATE,
    SITE_MUSIC_SEQUENCE,
    SITE_MUSIC_VOLUME,
    SITE_COUNT
};

static signature_t sites[SITE_COUNT] = {
    SIGNATURE_ENTRY("sound_play",           SIG_SOUND_PLAY),
    SIGNATURE_ENTRY("sound_start_channel",  SIG_SOUND_START_CHANNEL),
    SIGNATURE_ENTRY("sound_free_channel",   SIG_SOUND_FREE_CHANNEL),
    SIGNATURE_ENTRY("sound_master_volume",  SIG_SOUND_MASTER_VOLUME),
    SIGNATURE_ENTRY("sound_periodic",       SIG_SOUND_PERIODIC),
    SIGNATURE_ENTRY("sound_place_on",       SIG_SOUND_PLACE_ON),
    SIGNATURE_ENTRY("sound_place_off",      SIG_SOUND_PLACE_OFF),
    SIGNATURE_ENTRY("music_set_state",      SIG_MUSIC_STATE),
    SIGNATURE_ENTRY("music_set_sequence",   SIG_MUSIC_SEQUENCE),
    SIGNATURE_ENTRY("music_set_volume",     SIG_MUSIC_VOLUME)
};

/* B3D_SCAL, a "sound call", 0x40 bytes. */
#define SCAL_NAME       0x00   /* char[0x18]. THE WAV; srefIndex is only a preload hint */
#define SCAL_FLAGS      0x18
#define SCAL_VOLUME     0x1C
#define SCAL_MIN_DIST   0x28
#define SCAL_MAX_DIST   0x2C
#define SCAL_PRIORITY   0x30
#define SCAL_DETAIL     0x36   /* uint16. WAV: detail level, SEQ: gate type */
#define SCAL_IMUSE_CUE  0x3C

#define SCAL_FLAG_MUSIC_STATE    0x00000008u
#define SCAL_FLAG_MUSIC_SEQUENCE 0x00000400u
#define CHANNEL_FLAG_FREE        0x00080000u

/* bapChannel, 12 slots, stride 0x80. */
#define CHANNEL_STRIDE     0x80
#define CHANNEL_COUNT        12
#define CHANNEL_IS_3D      0x08
#define CHANNEL_SOUND_REF  0x0C
#define CHANNEL_FLAGS      0x10
#define CHANNEL_BASE_VOL   0x18
#define CHANNEL_MIN_DIST   0x20
#define CHANNEL_MAX_DIST   0x24
#define CHANNEL_DISTANCE   0x40
#define CHANNEL_PRIORITY   0x70
#define CHANNEL_OWNER_HANDLE 0x78
#define SOUND_REF_NAME     0x04   /* char[0x34] */

/* B3D_SPLA, a "sound place", 0x3C bytes. */
#define SPLA_NAME       0x00   /* char[0x10] */
#define SPLA_INTERVAL   0x1C
#define SPLA_SOUND_CALL 0x2C
#define SPLA_CHANNEL    0x38

#define FLAG_TEXT_MAX 128

typedef int32_t (__cdecl *sound_play_fn_t)(const void *sound, int32_t *handle, const float *pos);
typedef int32_t (__cdecl *sound_start_channel_fn_t)(const void *sound, const float *pos);
typedef void    (__cdecl *sound_free_channel_fn_t)(uint32_t channel);
typedef void    (__cdecl *sound_master_volume_fn_t)(int32_t volume);
typedef uint32_t (__cdecl *sound_periodic_fn_t)(void);
typedef void    (__cdecl *sound_place_fn_t)(void *place);
typedef uint32_t (__cdecl *music_state_fn_t)(int32_t cue);
typedef uint32_t (__cdecl *music_sequence_fn_t)(int32_t cue, int32_t gate_type, uint32_t priority,
                                                float distance);
typedef void    (__cdecl *music_volume_fn_t)(float volume);

typedef struct diag_audio_state {
    detour_t  play;
    detour_t  start_channel;
    detour_t  free_channel;
    detour_t  master_volume;
    detour_t  periodic;
    detour_t  place_on;
    detour_t  place_off;
    detour_t  music_state;
    detour_t  music_sequence;
    detour_t  music_volume;

    uint8_t  *channel_bank;
    int32_t  *music_attached;
    int32_t  *state_latch;
    int32_t  *sequence_latch;

    int       census_milliseconds;
    DWORD     last_census;
    bool      sites_resolved;
} diag_audio_state_t;

static diag_audio_state_t audio_state;

/* ============================================================================================ */
static void resolve_sites_once(void)
{
    if (audio_state.sites_resolved) {
        return;
    }
    audio_state.sites_resolved = true;
    signature_resolve_table(sites, SITE_COUNT);
}

static const char *channel_wav(int channel)
{
    const uint8_t *record;
    const uint8_t *reference;

    if (audio_state.channel_bank == NULL || channel < 0 || channel >= CHANNEL_COUNT) {
        return "?";
    }
    record = audio_state.channel_bank + channel * CHANNEL_STRIDE;
    reference = *(const uint8_t * const *)(record + CHANNEL_SOUND_REF);
    if (reference == NULL) {
        return "-";
    }
    return diag_safe_string((const char *)(reference + SOUND_REF_NAME), 0x34);
}

static int32_t __cdecl hook_sound_play(const void *sound, int32_t *handle, const float *position)
{
    sound_play_fn_t original = (sound_play_fn_t)audio_state.play.original;
    const uint8_t  *call = (const uint8_t *)sound;
    int32_t         result;
    uint32_t        flags;
    char            flag_text[FLAG_TEXT_MAX];

    result = original(sound, handle, position);
    if (call == NULL) {
        return result;
    }

    flags = *(const uint32_t *)(call + SCAL_FLAGS);
    diag_sound_flags(flags, flag_text, sizeof(flag_text));

    if ((flags & SCAL_FLAG_MUSIC_STATE) != 0) {
        diag_log_write("mus  <- SCAL: STATE cue %s",
                       diag_numbered_name(diag_music_states,
                                          *(const int32_t *)(call + SCAL_IMUSE_CUE)));
    } else if ((flags & SCAL_FLAG_MUSIC_SEQUENCE) != 0) {
        diag_log_write("mus  <- SCAL: SEQ   cue %s  gate=%u prio=%d dist=%.1f",
                       diag_numbered_name(diag_music_sequences,
                                          *(const int32_t *)(call + SCAL_IMUSE_CUE)),
                       (unsigned)*(const uint16_t *)(call + SCAL_DETAIL),
                       (int)*(const int32_t *)(call + SCAL_PRIORITY),
                       (double)*(const float *)(call + SCAL_MAX_DIST));
    } else if (position != NULL) {
        diag_log_write("aud  play \"%s\" fl=%04X[%s] vol=%.2f prio=%d d=[%.1f..%.1f] "
                       "pos=(%.1f,%.1f,%.1f) -> %s%d",
                       diag_safe_string((const char *)(call + SCAL_NAME), 0x18),
                       (unsigned)flags, flag_text,
                       (double)*(const float *)(call + SCAL_VOLUME),
                       (int)*(const int32_t *)(call + SCAL_PRIORITY),
                       (double)*(const float *)(call + SCAL_MIN_DIST),
                       (double)*(const float *)(call + SCAL_MAX_DIST),
                       (double)position[0], (double)position[1], (double)position[2],
                       (result < 0) ? "REJECTED " : "ch ", (int)result);
    } else {
        diag_log_write("aud  play \"%s\" fl=%04X[%s] vol=%.2f prio=%d 2D -> %s%d",
                       diag_safe_string((const char *)(call + SCAL_NAME), 0x18),
                       (unsigned)flags, flag_text,
                       (double)*(const float *)(call + SCAL_VOLUME),
                       (int)*(const int32_t *)(call + SCAL_PRIORITY),
                       (result < 0) ? "REJECTED " : "ch ", (int)result);
    }
    return result;
}

static int32_t __cdecl hook_sound_start_channel(const void *sound, const float *position)
{
    sound_start_channel_fn_t original =
        (sound_start_channel_fn_t)audio_state.start_channel.original;
    int32_t result = original(sound, position);

    if (sound != NULL) {
        diag_log_write("aud  channel \"%s\" -> %s%d",
                       diag_safe_string((const char *)((const uint8_t *)sound + SCAL_NAME), 0x18),
                       (result < 0) ? "NONE " : "", (int)result);
    }
    return result;
}

static void __cdecl hook_sound_free_channel(uint32_t channel)
{
    sound_free_channel_fn_t original =
        (sound_free_channel_fn_t)audio_state.free_channel.original;
    char     wav[48];
    uint32_t flags = 0;

    if (channel < CHANNEL_COUNT && audio_state.channel_bank != NULL) {
        flags = *(const uint32_t *)(audio_state.channel_bank + channel * CHANNEL_STRIDE
                                    + CHANNEL_FLAGS);
        strncpy(wav, channel_wav((int)channel), sizeof(wav) - 1);
        wav[sizeof(wav) - 1] = '\0';
    } else {
        wav[0] = '?';
        wav[1] = '\0';
    }

    /* THE OWNER HANDLE, READ BEFORE THE ORIGINAL RUNS because the original clears it.
     *
     * bapsound_play stores the address the caller passed for its channel handle, and
     * bapsound_freeChannel writes -1 through it when the voice ends. Three call sites in the
     * projectile code pass the address of a stack local and then call bapsound_pinChannel,
     * which detaches the position by copying it and leaves this pointer alone. The frame
     * returns and the pointer stays, so the write lands in a frame that no longer exists.
     *
     * Above esp is a live frame, where the write corrupts something another function is still
     * using; below esp is unused space, where it does nothing. That distinction is why the
     * offset is printed rather than only the address. */
    if (channel < CHANNEL_COUNT && audio_state.channel_bank != NULL) {
        const void *owner = *(void * const *)(audio_state.channel_bank
                                              + channel * CHANNEL_STRIDE
                                              + CHANNEL_OWNER_HANDLE);
        uintptr_t   at    = (uintptr_t)owner;
        uintptr_t   base  = (uintptr_t)__readfsdword(0x04);
        uintptr_t   limit = (uintptr_t)__readfsdword(0x08);

        if (at >= limit && at < base) {
            uintptr_t here = (uintptr_t)&owner;
            diag_log_write("aud  STALE OWNER ch %u \"%s\" -> %08X, in the stack %08X..%08X, "
                           "%s esp by %u bytes", (unsigned)channel, wav, (unsigned)at,
                           (unsigned)limit, (unsigned)base,
                           (at >= here) ? "ABOVE" : "below",
                           (unsigned)((at >= here) ? (at - here) : (here - at)));
        }
    }

    original(channel);

    /* A channel that was already free is "freed" with no effect; that is not an event. */
    if ((flags & CHANNEL_FLAG_FREE) == 0) {
        char flag_text[FLAG_TEXT_MAX];
        diag_sound_flags(flags, flag_text, sizeof(flag_text));
        diag_log_write("aud  stop  ch %u \"%s\" [%s]", (unsigned)channel, wav, flag_text);
    }
}

static void __cdecl hook_sound_master_volume(int32_t volume)
{
    sound_master_volume_fn_t original =
        (sound_master_volume_fn_t)audio_state.master_volume.original;

    original(volume);
    diag_log_write("aud  SVOL = %d (0..127; 2D takes the master from Miles, 3D from the loop here)",
                   (int)volume);
}

static void write_channel_census(void)
{
    char flag_text[FLAG_TEXT_MAX];
    int  index;
    int  live = 0;

    if (audio_state.channel_bank == NULL) {
        return;
    }

    for (index = 0; index < CHANNEL_COUNT; ++index) {
        const uint8_t *record = audio_state.channel_bank + index * CHANNEL_STRIDE;
        uint32_t       flags = *(const uint32_t *)(record + CHANNEL_FLAGS);

        if ((flags & CHANNEL_FLAG_FREE) != 0) {
            continue;
        }
        ++live;
        diag_sound_flags(flags, flag_text, sizeof(flag_text));
        diag_log_write("aud  [census] ch %2d %-3s \"%-12s\" vol=%.2f d=%.1f [%.1f..%.1f] "
                       "prio=%u [%s]",
                       index, (*(const int32_t *)(record + CHANNEL_IS_3D) != 0) ? "3D" : "2D",
                       channel_wav(index),
                       (double)*(const float *)(record + CHANNEL_BASE_VOL),
                       (double)*(const float *)(record + CHANNEL_DISTANCE),
                       (double)*(const float *)(record + CHANNEL_MIN_DIST),
                       (double)*(const float *)(record + CHANNEL_MAX_DIST),
                       (unsigned)*(const uint32_t *)(record + CHANNEL_PRIORITY), flag_text);
    }

    if (live == 0) {
        diag_log_write("aud  [census] all %d channels free", CHANNEL_COUNT);
    }
}

static uint32_t __cdecl hook_sound_periodic(void)
{
    sound_periodic_fn_t original = (sound_periodic_fn_t)audio_state.periodic.original;
    uint32_t            result = original();
    DWORD               now;

    if (audio_state.census_milliseconds > 0) {
        now = timeGetTime();
        if (now - audio_state.last_census >= (DWORD)audio_state.census_milliseconds) {
            audio_state.last_census = now;
            write_channel_census();
        }
    }
    return result;
}

static void __cdecl hook_sound_place_on(void *place)
{
    sound_place_fn_t original = (sound_place_fn_t)audio_state.place_on.original;

    original(place);
    if (place != NULL) {
        diag_log_write("aud  zone ON  \"%s\" scal=%d interval=%.2fs channel=%d",
                       diag_safe_string((const char *)place + SPLA_NAME, 0x10),
                       (int)*(const int32_t *)((const uint8_t *)place + SPLA_SOUND_CALL),
                       (double)*(const float *)((const uint8_t *)place + SPLA_INTERVAL),
                       (int)*(const int32_t *)((const uint8_t *)place + SPLA_CHANNEL));
    }
}

static void __cdecl hook_sound_place_off(void *place)
{
    sound_place_fn_t original = (sound_place_fn_t)audio_state.place_off.original;

    original(place);
    if (place != NULL) {
        diag_log_write("aud  zone OFF \"%s\" scal=%d channel=%d  (stops NO running voice)",
                       diag_safe_string((const char *)place + SPLA_NAME, 0x10),
                       (int)*(const int32_t *)((const uint8_t *)place + SPLA_SOUND_CALL),
                       (int)*(const int32_t *)((const uint8_t *)place + SPLA_CHANNEL));
    }
}

/* ============================================================================================ */
static uint32_t __cdecl hook_music_state(int32_t cue)
{
    music_state_fn_t original = (music_state_fn_t)audio_state.music_state.original;
    int32_t  before = (audio_state.state_latch != NULL) ? *audio_state.state_latch : -1;
    uint32_t result = original(cue);
    int32_t  after  = (audio_state.state_latch != NULL) ? *audio_state.state_latch : -1;

    if (audio_state.music_attached != NULL && *audio_state.music_attached == 0) {
        diag_log_write("mus  state %s -> NOT ATTACHED (g_musicAttached==0), call discarded",
                       diag_numbered_name(diag_music_states, cue));
    } else if (before == after) {
        diag_log_write("mus  state %s -> unchanged (no-op)",
                       diag_numbered_name(diag_music_states, cue));
    } else {
        diag_log_write("mus  state %s <- %s   (latch BEFORE the DLL call, r=%u)",
                       diag_numbered_name(diag_music_states, after),
                       diag_numbered_name(diag_music_states, before), (unsigned)result);
    }
    return result;
}

static uint32_t __cdecl hook_music_sequence(int32_t cue, int32_t gate_type, uint32_t priority,
                                            float distance)
{
    music_sequence_fn_t original = (music_sequence_fn_t)audio_state.music_sequence.original;
    int32_t  before = (audio_state.sequence_latch != NULL) ? *audio_state.sequence_latch : -1;
    uint32_t result = original(cue, gate_type, priority, distance);
    int32_t  after  = (audio_state.sequence_latch != NULL) ? *audio_state.sequence_latch : -1;

    if (audio_state.music_attached != NULL && *audio_state.music_attached == 0) {
        diag_log_write("mus  seq   %s -> NOT ATTACHED, call discarded",
                       diag_numbered_name(diag_music_sequences, cue));
    } else if (before == after) {
        diag_log_write("mus  seq   %s -> not adopted (same cue, or gate %d / prio %u / dist %.1f "
                       "not satisfied)", diag_numbered_name(diag_music_sequences, cue),
                       (int)gate_type, (unsigned)priority, (double)distance);
    } else {
        diag_log_write("mus  seq   %s <- %s   (gate=%d prio=%u dist=%.1f, r=%u)",
                       diag_numbered_name(diag_music_sequences, after),
                       diag_numbered_name(diag_music_sequences, before),
                       (int)gate_type, (unsigned)priority, (double)distance, (unsigned)result);
    }
    return result;
}

static void __cdecl hook_music_volume(float volume)
{
    music_volume_fn_t original = (music_volume_fn_t)audio_state.music_volume.original;

    original(volume);
    diag_log_write("mus  MVOL = %.3f (goes to the DirectSound buffer, not through Miles)",
                   (double)volume);
}

/* ============================================================================================ */
int diag_audio_install(int audio_level, int census_milliseconds)
{
    int installed = 0;

    if (audio_level <= 0) {
        return 0;
    }
    resolve_sites_once();

    audio_state.channel_bank =
        (uint8_t *)diag_derive_address(sites, SITE_SOUND_FREE_CHANNEL, OFFSET_CHANNEL_BANK, "channel bank");
    audio_state.census_milliseconds = census_milliseconds;

    installed += diag_install_observer(sites, SITE_SOUND_PLAY, &audio_state.play,
                                  (const void *)hook_sound_play, SOUND_PLAY_PROLOGUE,
                                  "every sound call: WAV, flags, volume, priority, distances, "
                                  "position, channel, and both MUSIC branches (flag 0x8 / 0x400)")
                 ? 1 : 0;
    installed += diag_install_observer(sites, SITE_SOUND_FREE_CHANNEL, &audio_state.free_channel,
                                  (const void *)hook_sound_free_channel,
                                  SOUND_FREE_CHANNEL_PROLOGUE,
                                  "every channel release (distance cull, voice stealing, "
                                  "animation change, level end)") ? 1 : 0;
    installed += diag_install_observer(sites, SITE_SOUND_MASTER_VOLUME, &audio_state.master_volume,
                                  (const void *)hook_sound_master_volume,
                                  SOUND_MASTER_VOLUME_PROLOGUE, "SVOL changes") ? 1 : 0;
    installed += diag_install_observer(sites, SITE_SOUND_PLACE_ON, &audio_state.place_on,
                                  (const void *)hook_sound_place_on, SOUND_PLACE_ON_PROLOGUE,
                                  "sound zone activated (op 0x213 / AMAP bit 0x80)") ? 1 : 0;
    installed += diag_install_observer(sites, SITE_SOUND_PLACE_OFF, &audio_state.place_off,
                                  (const void *)hook_sound_place_off, SOUND_PLACE_OFF_PROLOGUE,
                                  "sound zone deactivated") ? 1 : 0;

    if (census_milliseconds > 0) {
        if (audio_state.channel_bank == NULL) {
            log_warning("  channel census OFF - the channel bank could not be derived");
        } else {
            installed += diag_install_observer(sites, SITE_SOUND_PERIODIC, &audio_state.periodic,
                                          (const void *)hook_sound_periodic,
                                          SOUND_PERIODIC_PROLOGUE,
                                          "clock for the channel census") ? 1 : 0;
        }
    }
    if (audio_level >= 2) {
        installed += diag_install_observer(sites, SITE_SOUND_START_CHANNEL, &audio_state.start_channel,
                                      (const void *)hook_sound_start_channel,
                                      SOUND_START_CHANNEL_PROLOGUE,
                                      "channel allocation and its rejections") ? 1 : 0;
    }

    return installed;
}

int diag_music_install(int music_level)
{
    int installed = 0;

    if (music_level <= 0) {
        return 0;
    }
    resolve_sites_once();

    audio_state.music_attached =
        (int32_t *)diag_derive_address(sites, SITE_MUSIC_STATE, OFFSET_MUSIC_ATTACHED, "g_musicAttached");
    audio_state.state_latch =
        (int32_t *)diag_derive_address(sites, SITE_MUSIC_STATE, OFFSET_STATE_LATCH, "g_stateLatch");
    audio_state.sequence_latch =
        (int32_t *)diag_derive_address(sites, SITE_MUSIC_SEQUENCE, OFFSET_SEQUENCE_LATCH, "g_seqLatch");

    installed += diag_install_observer(sites, SITE_MUSIC_STATE, &audio_state.music_state,
                                  (const void *)hook_music_state, MUSIC_STATE_PROLOGUE,
                                  "music STATE (latch before/after, with symbol names)") ? 1 : 0;
    installed += diag_install_observer(sites, SITE_MUSIC_SEQUENCE, &audio_state.music_sequence,
                                  (const void *)hook_music_sequence, MUSIC_SEQUENCE_PROLOGUE,
                                  "music SEQUENCE including gate type, priority and radius")
                 ? 1 : 0;
    installed += diag_install_observer(sites, SITE_MUSIC_VOLUME, &audio_state.music_volume,
                                  (const void *)hook_music_volume, MUSIC_VOLUME_PROLOGUE,
                                  "MVOL changes") ? 1 : 0;

    return installed;
}
