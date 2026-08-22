/* sfx_mute.c: silences the SPECIFIC sound responsible for the audible thud behind the post-movie
 * curtain (video_overlay.c) - not the master SFX volume, not dialogue, not music.
 *
 * ==============================================================================================
 * FINDING THE ACTUAL SOUND
 *
 * Two earlier attempts, both replaced:
 *
 * 1. Muted bapsound_setMasterVolume for the curtain's own duration. Confirmed live: it also cut
 *    off the start of the level's own opening line ("I have a bad feeling about this" losing its
 *    first three words), because that line starts inside the SAME window as the thud, on the SAME
 *    engine audio channel that approach silenced wholesale.
 * 2. Suppressed FUN_00417143, the wrapper the level script interpreter's own "Sound" opcode
 *    (0x603, sitting right next to 0x604 "LOCK Player" in the same switch) calls. Confirmed live,
 *    with a diagnostics build watching every call to bapsound_play by name: this was never the
 *    right call in the first place. Opcode 0x603 does not fire for this cutscene at all.
 *
 * A live capture (diagnostics build, `[diagnostics] Audio=1`) of every sound played from level load
 * through the first two spoken lines shows exactly one candidate: `FSMJCON1.wav`, playing once,
 * 2-D, right in the gap between the level becoming visible and Obi-Wan's own first line
 * (`OBm0030.wav`) - nothing else plays in that window that is not an ambient loop already rejected
 * for being out of range, or a menu sound from well before the level even loads. Named for whatever
 * put it there, not for what it is; the timing is the evidence.
 *
 * ==============================================================================================
 * WHAT THIS DOES
 *
 * Detours bapsound_play (0x0041681F, byte-identical to diagnostics/diag_audio.c's own
 * SIG_SOUND_PLAY) and, while suppression is armed, skips exactly the call whose sound record's own
 * name matches `FSMJCON1.wav` - every other sound, including both spoken lines, passes through
 * untouched. Armed and disarmed exactly like the two earlier attempts were: begin() when the
 * curtain arms, watching pPlayer+0xA0 (the position-override flag video_overlay.c's own header
 * documents) so end() can fire the instant that transient actually finishes, with the curtain's own
 * timer as a fallback cap if the flag is never seen.
 */
#include "sfx_mute.h"

#include "common/detour.h"
#include "common/frame_hook.h"
#include "common/host_image.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/signature.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* --- bapsound_play 0x0041681F, byte-identical to diagnostics/diag_audio.c's own SIG_SOUND_PLAY -- */
static const uint8_t SIG_SOUND_PLAY[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x18, 0xA1, 0x88, 0xAE, 0x5B, 0x00, 0x89,
    0x45, 0xFC, 0x83, 0x3D, 0xB8, 0xB4, 0x5B, 0x00
};
#define SOUND_PLAY_PROLOGUE 6u

/* B3D_SCAL, a "sound call": the WAV name is a char[0x18] at its own offset 0. srefIndex is only a
 * preload hint, the name is the actual file. */
#define SCAL_NAME_OFFSET 0x00u
#define SCAL_NAME_LENGTH 0x18u

#define SUPPRESSED_SOUND_NAME "FSMJCON1.wav"

/* --- Plr_RunPhases 0x00448297, used only to derive pPlayer's own pointer slot; never detoured.
 * Byte-identical to diag_flow.c's, cutscene_pose_sync.c's and video_overlay.c's own reasoning for
 * this site.
 *   +0x27 : &pPlayer [0x4B5220] */
static const uint8_t SIG_PLAYER_RUN_PHASES[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x08, 0xC7, 0x45, 0xF8, 0x00, 0x00, 0x00,
    0x00, 0xC7, 0x45, 0xFC, 0x00, 0x00, 0x00, 0x00, 0x8B, 0x45, 0xF8, 0x83,
    0x3C, 0x85, 0x28, 0x52, 0x4B, 0x00, 0x01, 0x0F, 0x84, 0x95, 0x00, 0x00,
    0x00, 0x8B, 0x0D, 0x20, 0x52, 0x4B, 0x00
};
#define OFFSET_PLAYER_POINTER 0x27u

/* Plr_CommitPose (0x0044C06B): while pPlayer+0xA0 is nonzero, the player's position is force-copied
 * every substep from pPlayer+0x124 - the exact mechanism behind the position-settle transient
 * documented in video_overlay.c. Watched here (never written) so suppression can end the instant
 * that transient is actually over, rather than always riding out the curtain's own worst-case
 * timer. */
#define PLAYER_OVERRIDE_FLAG_OFFSET 0xA0u

enum {
    SITE_SOUND_PLAY,
    SITE_PLAYER_RUN_PHASES,
    SITE_COUNT
};

static signature_t sites[SITE_COUNT] = {
    SIGNATURE_ENTRY_DETOUR("sfx_mute_sound_play", SIG_SOUND_PLAY, SOUND_PLAY_PROLOGUE),
    SIGNATURE_ENTRY("sfx_mute_player_run_phases", SIG_PLAYER_RUN_PHASES)
};

typedef int32_t (__cdecl *sound_play_fn_t)(const void *sound, int32_t *handle, const float *pos);

typedef struct sfx_mute_state {
    bool             resolved;
    detour_t         sound_play;
    uint32_t        *player_pointer_slot;
    bool             suppressing;
    bool             override_seen_active;
    uint32_t         suppressed_count;
} sfx_mute_state_t;

static sfx_mute_state_t mute_state;

static bool is_suppressed_sound(const void *sound)
{
    const char *name = (const char *)sound + SCAL_NAME_OFFSET;
    size_t      length;

    if (sound == NULL) {
        return false;
    }
    for (length = 0; length < SCAL_NAME_LENGTH && name[length] != '\0'; ++length) {
        /* just measuring, strnicmp below does the real comparison */
    }
    return length == (sizeof(SUPPRESSED_SOUND_NAME) - 1) &&
           _strnicmp(name, SUPPRESSED_SOUND_NAME, sizeof(SUPPRESSED_SOUND_NAME) - 1) == 0;
}

static int32_t __cdecl hook_sound_play(const void *sound, int32_t *handle, const float *pos)
{
    sound_play_fn_t original = (sound_play_fn_t)mute_state.sound_play.original;

    if (mute_state.suppressing && is_suppressed_sound(sound)) {
        ++mute_state.suppressed_count;
        return -1;   /* the same "not played" answer a rejected channel allocation returns */
    }
    return original(sound, handle, pos);
}

/* Reads the operand behind Plr_RunPhases' own `mov ecx,[pPlayer]`, the same technique
 * cutscene_pose_sync.c's resolve_player_pointer_slot uses, inlined here for the same reason. */
static uint32_t *resolve_player_pointer_slot(void)
{
    uintptr_t site = sites[SITE_PLAYER_RUN_PHASES].address;
    uint32_t  address = 0;

    if (site == 0) {
        return NULL;
    }
    if (!memory_read_u32(site + OFFSET_PLAYER_POINTER, &address) ||
        !memory_is_inside_image(address, sizeof(uint32_t))) {
        return NULL;
    }
    return (uint32_t *)(uintptr_t)address;
}

/* Runs once per real frame WHILE suppressing, watching for the position-override transient (see
 * the offset's own comment) to actually finish, so suppression can end the moment it does rather
 * than always riding out the curtain's own worst-case timer. `override_seen_active` guards against
 * the flag simply not having engaged YET at the instant suppression begins - the ordinary case,
 * since sfx_mute_begin runs right as the movie ends and the cutscene lock that arms the override
 * has not fired yet - so a read of 0 only ends suppression once the flag has actually been seen
 * on. */
static void sfx_mute_frame_tick(void)
{
    uint32_t player = 0;
    int32_t  override_flag = 0;

    if (!mute_state.suppressing || mute_state.player_pointer_slot == NULL) {
        return;
    }
    if (!memory_read((uintptr_t)mute_state.player_pointer_slot, &player, sizeof(player)) ||
        player == 0 ||
        !memory_read((uintptr_t)player + PLAYER_OVERRIDE_FLAG_OFFSET, &override_flag,
                     sizeof(override_flag))) {
        return;
    }

    if (override_flag != 0) {
        mute_state.override_seen_active = true;
        return;
    }
    if (mute_state.override_seen_active) {
        sfx_mute_end();
    }
}

void sfx_mute_install(void)
{
    signature_resolve_table(sites, SITE_COUNT);

    if (sites[SITE_SOUND_PLAY].address == 0) {
        log_warning("sfx_mute: bapsound_play did not resolve, the post-movie curtain will stay "
                    "audible: the landing thud behind it will still be heard");
        return;
    }
    if (!detour_install(&mute_state.sound_play, sites[SITE_SOUND_PLAY].address,
                        (const void *)hook_sound_play, SOUND_PLAY_PROLOGUE)) {
        log_error("sfx_mute: the detour at %08X failed, the post-movie curtain will stay audible",
                  (unsigned)sites[SITE_SOUND_PLAY].address);
        return;
    }
    mute_state.resolved = true;

    /* Best effort and never fatal: without it, suppression simply rides out the curtain's own
     * timer instead of ending early. */
    mute_state.player_pointer_slot = resolve_player_pointer_slot();
    if (mute_state.player_pointer_slot == NULL || !frame_hook_add(sfx_mute_frame_tick)) {
        log_warning("sfx_mute: the player's own position-override flag is not watchable, so "
                    "suppression always runs the full curtain duration instead of ending the "
                    "moment the transient it exists for actually finishes");
    }

    log_info("sfx_mute: bapsound_play at %08X is watched for \"%s\", suppressed for exactly as "
             "long as the post-movie curtain's own transient actually runs - every other sound, "
             "including both spoken lines, is untouched",
             (unsigned)sites[SITE_SOUND_PLAY].address, SUPPRESSED_SOUND_NAME);
}

void sfx_mute_begin(void)
{
    if (!mute_state.resolved || mute_state.suppressing) {
        return;
    }
    mute_state.suppressing = true;
    mute_state.override_seen_active = false;
}

void sfx_mute_end(void)
{
    if (!mute_state.resolved || !mute_state.suppressing) {
        return;
    }
    mute_state.suppressing = false;
    if (mute_state.suppressed_count > 0) {
        log_info("sfx_mute: suppressed %u call(s) to \"%s\" behind the curtain",
                 (unsigned)mute_state.suppressed_count, SUPPRESSED_SOUND_NAME);
        mute_state.suppressed_count = 0;
    }
}
