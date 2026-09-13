/* speaker_gesture.c: see speaker_gesture.h. */
#include "speaker_gesture.h"

#include "common/frame_hook.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/signature.h"

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* --- What a trace of one scene showed, the player character's body through two lines --------- *
 *
 *   19.3  line 2942 starts   base clip 1, stnd-no2, a stand fidget, 29 frames at 10 a second
 *   22.2                     its track reports complete: the body holds the last frame
 *   26.8  line 2942 ends     base clip 0, stnd-no1, the stand, which loops
 *   37.7  line 2943 starts   base clip 39, cutscn3, a gesture, 29 frames at 17 a second
 *   38.8                     complete, and held, through the rest of the line
 *
 * The clip names and lengths are the player model's own table, read out of obiwan.baf. The
 * scene actors' bodies do the same with their own gesture clips. The speaker lock is the body
 * the line is credited to, so following it covers every speaker without naming any. A first
 * version followed the player's own body and matched nothing: in a scene the player character's
 * lines are spoken through a scene actor, whose body holds the lock. */

/* --- Dialog_Render 0x00430434, for the bark channel handle ------------------------------------ *
 * The same head dialogue_menu_fix reads the row count and the bark channel from; here only the
 * bark channel is wanted, the handle of the voice line playing, minus one when none is. */
static const uint8_t SIG_DIALOG_RENDER[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x0C,
    0x83, 0x3D, 0x00, 0x00, 0x00, 0x00, 0x00, 0x75, 0x07,
    0x33, 0xC0, 0xE9, 0x54, 0x01, 0x00, 0x00,
    0x83, 0x3D, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7E, 0x0A,
    0x6A, 0x01, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x83, 0xC4, 0x04,
    0xE8, 0x00, 0x00, 0x00, 0x00,
    0x83, 0x3D, 0x00, 0x00, 0x00, 0x00, 0x00, 0x74, 0x25,
    0x83, 0x3D, 0x00, 0x00, 0x00, 0x00, 0x00, 0x75, 0x1C,
    0x83, 0x3D, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7D, 0x11
};
static const uint8_t MSK_DIALOG_RENDER[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF
};
_Static_assert(sizeof SIG_DIALOG_RENDER == sizeof MSK_DIALOG_RENDER,
               "the Dialog_Render pattern and its mask are different lengths");
#define RENDER_BARK_OPERAND 0x42u
#define DIALOG_BARK_OFFSET  0x40u    /* from the speaker lock, the record's own layout */

/* --- bapobj_playClip 0x0041263F, called, never detoured ------------------------------------- *
 *   55 8B EC 83 EC 10            push ebp / mov ebp,esp / sub esp,0x10
 *   8B 45 08 89 45 F0            the body into a local
 *   C7 45 F8 00 00 00 00         ret = 0
 *   8B 4D F0 83 79 14 00 75 1B   body->pActor == NULL: the assert
 * Mode 4 is the crossfade, the mode the script interpreter itself uses. */
static const uint8_t SIG_PLAY_CLIP[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x10,
    0x8B, 0x45, 0x08, 0x89, 0x45, 0xF0,
    0xC7, 0x45, 0xF8, 0x00, 0x00, 0x00, 0x00,
    0x8B, 0x4D, 0xF0, 0x83, 0x79, 0x14, 0x00, 0x75, 0x1B
};
#define PLAY_CLIP_CROSSFADE 4

/* --- bapsound_pinChannel 0x00417826, for the channel bank, read, never detoured ------------- *
 * The same head sound_lifetime_fix reads the bank from: the channel index times the stride,
 * 0x80, plus the bank, whose operand sits at +0x0B. A voice channel's record names the sample
 * the voice plays on, a plain one or a Miles 3D one, and the sample knows how far in it is. */
static const uint8_t SIG_PIN_CHANNEL[] = {
    0x55, 0x8B, 0xEC, 0x51, 0x8B, 0x45, 0x08, 0xC1, 0xE0, 0x07, 0x05,
    0x00, 0x00, 0x00, 0x00,
    0x89, 0x45, 0xFC, 0x8B, 0x4D, 0xFC, 0x83, 0x79, 0x0C, 0x00, 0x74, 0x27
};
static const uint8_t MSK_PIN_CHANNEL[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
_Static_assert(sizeof SIG_PIN_CHANNEL == sizeof MSK_PIN_CHANNEL,
               "the pin channel pattern and its mask are different lengths");
#define PIN_CHANNEL_BANK_OPERAND   0x0Bu
#define CHANNEL_STRIDE             0x80u
#define CHANNEL_COUNT              12
#define CHANNEL_SAMPLE_OFFSET      0x00u    /* the Miles sample handle, when the slot is plain */
#define CHANNEL_SAMPLE_3D_OFFSET   0x04u    /* the Miles 3D sample handle, when it is 3D */
#define CHANNEL_IS_3D_OFFSET       0x08u

/* Miles, out of the game's own mss32.dll. A spoken line placed in the world is a 3D sample,
 * which answers in bytes at its playback rate, 16 bit mono, the only form a 3D sample takes;
 * a line with no position is a plain sample, which answers in milliseconds. */
typedef int32_t (__stdcall *ail_3d_bytes_fn_t)(uint32_t sample);
typedef int32_t (__stdcall *ail_3d_rate_fn_t)(uint32_t sample);
typedef void    (__stdcall *ail_ms_position_fn_t)(uint32_t sample, int32_t *total_ms,
                                                  int32_t *current_ms);
#define SAMPLE_3D_BYTES_PER_FRAME  2.0f


/* The body, the same layout every other reader of it in this project uses. */
#define OBJECT_THING_OFFSET        0x9Cu
#define OBJECT_BASE_CLIP_OFFSET    0xE8u
#define OBJECT_BASE_SLOT_OFFSET    0xECu
#define THING_PUPPET_OFFSET        0x18u
#define PUPPET_TRACKS_OFFSET       0x08u
#define PUPPET_TRACK_STRIDE        0x14Cu
#define TRACK_KEYFRAME_OFFSET      0x128u   /* the clip block the track plays */
#define TRACK_MODE_OFFSET          0x13Cu   /* how the clip ends, the word below */
#define TRACK_COMPLETE_OFFSET      0x140u
#define KEYFRAME_FPS_OFFSET        0x30u    /* float */
#define KEYFRAME_FRAMES_OFFSET     0x34u
#define PUPPET_TRACK_LIMIT         8

/* A replay has to fit inside what is left of the voice, or the speaker gestures on after it has
 * stopped: a pass is one gesture, and the pass running when the voice stopped ran to its end,
 * up to three seconds past the line. So a clip is only started again when the voice has at
 * least the clip's length still to play, with this much grace for the crossfade the scene's
 * idle comes in on. The last pass ends on its last frame before the voice does, as the one
 * pass the scene started always did, and the idle follows from that pose. */
#define REPLAY_FIT_GRACE_SECONDS   0.2f

/* The track's mode word is copied from the clip's own flags whenever the clip is put on, and
 * decides what the puppet does when the clip runs out: with bit 0x01 the clip stops on its last
 * frame, with none of the end bits it wraps and plays again. The scene's Animation opcode puts a
 * gesture on and then sets that bit, so a gesture plays once; bapobj_playClip on its own puts
 * the clip's flags back without it, so a clip started again here would loop until the scene
 * changed it, and the first build of this had every speaker gesturing on past their line.
 * The word is read before each replay and written back after, so a replay ends as the pass the
 * scene started did: on its last frame, held, and the scene's idle follows as it always did. */

enum {
    SITE_DIALOG_RENDER,
    SITE_PLAY_CLIP,
    SITE_PIN_CHANNEL,
    SITE_COUNT
};

static signature_t sites[SITE_COUNT] = {
    SIGNATURE_ENTRY_MASKED("Dialog_Render", SIG_DIALOG_RENDER, MSK_DIALOG_RENDER),
    SIGNATURE_ENTRY("bapobj_playClip", SIG_PLAY_CLIP),
    SIGNATURE_ENTRY_MASKED("bapsound_pinChannel", SIG_PIN_CHANNEL, MSK_PIN_CHANNEL)
};

typedef int32_t (__cdecl *play_clip_fn_t)(uint32_t body, int32_t clip, int32_t mode);

static struct {
    const volatile uint32_t *speaker_lock;
    const volatile int32_t  *bark;
    play_clip_fn_t           play_clip;
    uintptr_t                channel_bank;
    ail_3d_bytes_fn_t        sample_3d_length;
    ail_3d_bytes_fn_t        sample_3d_offset;
    ail_3d_rate_fn_t         sample_3d_rate;
    ail_ms_position_fn_t     sample_ms_position;

    uint32_t line_speaker;     /* the body of the line being followed, 0 between lines */
    int32_t  line_clip;
    uint32_t line_replays;
    float    line_held_seconds;  /* the voice left when a pass was first declined, 0 if none */
    uint32_t lines;
} gesture;

/* The track the base clip of `body` is on, or 0 when the chain to it does not read. */
static uintptr_t base_track(uint32_t body)
{
    uint32_t thing = 0;
    uint32_t puppet = 0;
    int32_t  slot = -1;

    if (!memory_try_read((uintptr_t)body + OBJECT_THING_OFFSET, &thing, sizeof thing) ||
        thing == 0 ||
        !memory_try_read((uintptr_t)thing + THING_PUPPET_OFFSET, &puppet, sizeof puppet) ||
        puppet == 0 ||
        !memory_try_read((uintptr_t)body + OBJECT_BASE_SLOT_OFFSET, &slot, sizeof slot) ||
        slot < 0 || slot >= PUPPET_TRACK_LIMIT) {
        return 0;
    }
    return (uintptr_t)puppet + PUPPET_TRACKS_OFFSET + (uint32_t)slot * PUPPET_TRACK_STRIDE;
}

static bool base_clip_complete(uint32_t body)
{
    uintptr_t track = base_track(body);
    int32_t   complete = 0;

    return track != 0 &&
           memory_try_read(track + TRACK_COMPLETE_OFFSET, &complete, sizeof complete) &&
           complete != 0;
}


/* How much of the voice on channel `channel` is still to play, in seconds. False when the
 * channel or its sample does not answer, in which case the caller replays as if it fitted. */
static bool voice_seconds_left(int32_t channel, float *left)
{
    uintptr_t record;
    int32_t   is_3d = 0;
    uint32_t  sample = 0;
    int32_t   total_ms = 0;
    int32_t   current_ms = 0;
    int32_t   length;
    int32_t   offset;
    int32_t   rate;

    if (channel < 0 || channel >= CHANNEL_COUNT || gesture.channel_bank == 0) {
        return false;
    }
    record = gesture.channel_bank + (uint32_t)channel * CHANNEL_STRIDE;
    if (!memory_try_read(record + CHANNEL_IS_3D_OFFSET, &is_3d, sizeof is_3d)) {
        return false;
    }
    if (is_3d != 0) {
        if (!memory_try_read(record + CHANNEL_SAMPLE_3D_OFFSET, &sample, sizeof sample) ||
            sample == 0) {
            return false;
        }
        length = gesture.sample_3d_length(sample);
        offset = gesture.sample_3d_offset(sample);
        rate   = gesture.sample_3d_rate(sample);
        if (length <= 0 || rate <= 0 || offset < 0 || offset > length) {
            return false;
        }
        *left = (float)(length - offset) / ((float)rate * SAMPLE_3D_BYTES_PER_FRAME);
        return true;
    }
    if (!memory_try_read(record + CHANNEL_SAMPLE_OFFSET, &sample, sizeof sample) ||
        sample == 0) {
        return false;
    }
    gesture.sample_ms_position(sample, &total_ms, &current_ms);
    if (total_ms <= 0 || current_ms < 0 || current_ms > total_ms) {
        return false;
    }
    *left = (float)(total_ms - current_ms) / 1000.0f;
    return true;
}

/* The length of the clip on `track` in seconds, or 0 when it does not read. */
static float track_clip_seconds(uintptr_t track)
{
    uint32_t keyframe = 0;
    float    fps = 0.0f;
    int32_t  frames = 0;

    if (track == 0 ||
        !memory_try_read(track + TRACK_KEYFRAME_OFFSET, &keyframe, sizeof keyframe) ||
        keyframe == 0 ||
        !memory_try_read((uintptr_t)keyframe + KEYFRAME_FPS_OFFSET, &fps, sizeof fps) ||
        !memory_try_read((uintptr_t)keyframe + KEYFRAME_FRAMES_OFFSET, &frames, sizeof frames) ||
        !(fps > 0.0f) || frames <= 0) {
        return 0.0f;
    }
    return (float)frames / fps;
}

/* Whether another pass of the clip on `track` ends before the voice does. */
static bool replay_fits(uintptr_t track)
{
    float left = 0.0f;
    float clip = track_clip_seconds(track);

    if (clip <= 0.0f || !voice_seconds_left(*gesture.bark, &left)) {
        return true;
    }
    if (left + REPLAY_FIT_GRACE_SECONDS >= clip) {
        return true;
    }
    if (gesture.line_held_seconds == 0.0f) {
        gesture.line_held_seconds = left;
    }
    return false;
}

/* The base clip put on again with the mode word the pass before it had. */
static void replay_base_clip(uint32_t body, int32_t clip)
{
    uintptr_t track = base_track(body);
    uint32_t  mode = 0;
    bool      have_mode = track != 0 &&
                          memory_try_read(track + TRACK_MODE_OFFSET, &mode, sizeof mode);

    gesture.play_clip(body, clip, PLAY_CLIP_CROSSFADE);
    track = base_track(body);
    if (have_mode && track != 0) {
        *(volatile uint32_t *)(track + TRACK_MODE_OFFSET) = mode;
    }
}

static void end_line(void)
{
    if (gesture.line_speaker != 0) {
        ++gesture.lines;
        if (gesture.line_replays != 0) {
            log_info("speaker %08X's line ended: clip %d was started again %u time(s), and "
                     "held its last frame for the final %.1f s of the voice, where another "
                     "pass would not have fitted (line %u)",
                     (unsigned)gesture.line_speaker, (int)gesture.line_clip,
                     (unsigned)gesture.line_replays, (double)gesture.line_held_seconds,
                     (unsigned)gesture.lines);
        }
    }
    gesture.line_speaker      = 0;
    gesture.line_replays      = 0;
    gesture.line_held_seconds = 0.0f;
}

/* Once a frame. The current speaker's body while the voice is still playing: a clip that has
 * played through is started again. */
static void on_frame_repeat_gesture(void)
{
    uint32_t body = *gesture.speaker_lock;
    int32_t  clip = 0;

    if (body == 0 || *gesture.bark < 0 ||
        !memory_try_read((uintptr_t)body + OBJECT_BASE_CLIP_OFFSET, &clip, sizeof clip)) {
        end_line();
        return;
    }
    if (gesture.line_speaker != body) {
        end_line();
        gesture.line_speaker = body;
    }
    gesture.line_clip = clip;
    if (base_clip_complete(body) && replay_fits(base_track(body))) {
        replay_base_clip(body, clip);
        ++gesture.line_replays;
    }
}

/* The channel bank out of bapsound_pinChannel and the four Miles calls out of the game's own
 * mss32.dll, loaded long before any mod is. */
static bool bind_voice_clock(void)
{
    uint32_t bank = 0;
    HMODULE  miles = GetModuleHandleA("mss32.dll");

    if (!memory_read_u32(sites[SITE_PIN_CHANNEL].address + PIN_CHANNEL_BANK_OPERAND, &bank) ||
        !memory_is_inside_image(bank, CHANNEL_STRIDE * CHANNEL_COUNT)) {
        log_warning("the channel bank operand read as %08X, which is not a bank sized range "
                    "inside the image, so the gesture repeat stays off", (unsigned)bank);
        return false;
    }
    if (miles == NULL) {
        log_warning("mss32.dll is not loaded, so the gesture repeat stays off");
        return false;
    }
    gesture.channel_bank       = (uintptr_t)bank;
    gesture.sample_3d_length   = (ail_3d_bytes_fn_t)(void *)
                                 GetProcAddress(miles, "_AIL_3D_sample_length@4");
    gesture.sample_3d_offset   = (ail_3d_bytes_fn_t)(void *)
                                 GetProcAddress(miles, "_AIL_3D_sample_offset@4");
    gesture.sample_3d_rate     = (ail_3d_rate_fn_t)(void *)
                                 GetProcAddress(miles, "_AIL_3D_sample_playback_rate@4");
    gesture.sample_ms_position = (ail_ms_position_fn_t)(void *)
                                 GetProcAddress(miles, "_AIL_sample_ms_position@12");
    if (gesture.sample_3d_length == NULL || gesture.sample_3d_offset == NULL ||
        gesture.sample_3d_rate == NULL || gesture.sample_ms_position == NULL) {
        log_warning("mss32.dll does not export the sample position calls, so the gesture "
                    "repeat stays off");
        return false;
    }
    return true;
}

bool speaker_gesture_install(const volatile uint32_t *speaker_lock)
{
    uint32_t bark = 0;

    signature_resolve_table(sites, SITE_COUNT);
    if (sites[SITE_DIALOG_RENDER].address == 0 || sites[SITE_PLAY_CLIP].address == 0 ||
        sites[SITE_PIN_CHANNEL].address == 0) {
        log_warning("the sites the gesture repeat needs did not all resolve, so a gesture "
                    "still holds its last frame for the rest of a long line");
        return false;
    }
    if (!bind_voice_clock()) {
        return false;
    }
    if (!memory_read_u32(sites[SITE_DIALOG_RENDER].address + RENDER_BARK_OPERAND, &bark) ||
        bark != (uint32_t)(uintptr_t)speaker_lock + DIALOG_BARK_OFFSET) {
        log_warning("the bark channel at %08X is not where the conversation record puts it, "
                    "so the gesture repeat stays off", (unsigned)bark);
        return false;
    }
    gesture.speaker_lock = speaker_lock;
    gesture.bark         = (const volatile int32_t *)(uintptr_t)bark;
    gesture.play_clip    = (play_clip_fn_t)sites[SITE_PLAY_CLIP].address;

    if (!frame_hook_add(on_frame_repeat_gesture)) {
        log_warning("the per-frame hook could not be installed, so the gesture repeat stays off");
        return false;
    }
    log_info("a speaker keeps animating for the whole of their line: a clip that has played "
             "through on the speaker's body is started again while the voice has at least the "
             "clip's length still to play (bark channel %08X, bapobj_playClip %08X, channel "
             "bank %08X)", (unsigned)bark, (unsigned)sites[SITE_PLAY_CLIP].address,
             (unsigned)gesture.channel_bank);
    return true;
}
