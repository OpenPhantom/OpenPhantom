/* speaker_gesture.c: see speaker_gesture.h. */
#include "speaker_gesture.h"

#include "common/frame_hook.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/signature.h"

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

/* --- Dialog_Render 0x00430434, for the bark channel handle ------------------------------------- *
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

/* The body, the same layout every other reader of it in this project uses. */
#define OBJECT_THING_OFFSET        0x9Cu
#define OBJECT_BASE_CLIP_OFFSET    0xE8u
#define OBJECT_BASE_SLOT_OFFSET    0xECu
#define THING_PUPPET_OFFSET        0x18u
#define PUPPET_TRACKS_OFFSET       0x08u
#define PUPPET_TRACK_STRIDE        0x14Cu
#define TRACK_COMPLETE_OFFSET      0x140u
#define PUPPET_TRACK_LIMIT         8

enum {
    SITE_DIALOG_RENDER,
    SITE_PLAY_CLIP,
    SITE_COUNT
};

static signature_t sites[SITE_COUNT] = {
    SIGNATURE_ENTRY_MASKED("Dialog_Render", SIG_DIALOG_RENDER, MSK_DIALOG_RENDER),
    SIGNATURE_ENTRY("bapobj_playClip", SIG_PLAY_CLIP)
};

typedef int32_t (__cdecl *play_clip_fn_t)(uint32_t body, int32_t clip, int32_t mode);

static struct {
    const volatile uint32_t *speaker_lock;
    const volatile int32_t  *bark;
    play_clip_fn_t           play_clip;

    uint32_t line_speaker;     /* the body of the line being followed, 0 between lines */
    int32_t  line_clip;
    uint32_t line_replays;
    uint32_t lines;
} gesture;

/* True when the base track of `body` has played through; false when it has not or when the
 * chain to it does not read. */
static bool base_clip_complete(uint32_t body)
{
    uint32_t thing = 0;
    uint32_t puppet = 0;
    int32_t  slot = -1;
    int32_t  complete = 0;

    return memory_try_read((uintptr_t)body + OBJECT_THING_OFFSET, &thing, sizeof thing) &&
           thing != 0 &&
           memory_try_read((uintptr_t)thing + THING_PUPPET_OFFSET, &puppet, sizeof puppet) &&
           puppet != 0 &&
           memory_try_read((uintptr_t)body + OBJECT_BASE_SLOT_OFFSET, &slot, sizeof slot) &&
           slot >= 0 && slot < PUPPET_TRACK_LIMIT &&
           memory_try_read((uintptr_t)puppet + PUPPET_TRACKS_OFFSET +
                           (uint32_t)slot * PUPPET_TRACK_STRIDE + TRACK_COMPLETE_OFFSET,
                           &complete, sizeof complete) &&
           complete != 0;
}

static void end_line(void)
{
    if (gesture.line_speaker != 0) {
        ++gesture.lines;
        if (gesture.line_replays != 0) {
            log_info("speaker %08X's line ended: clip %d was started again %u time(s) so it "
                     "ran for the whole line (line %u)", (unsigned)gesture.line_speaker,
                     (int)gesture.line_clip, (unsigned)gesture.line_replays,
                     (unsigned)gesture.lines);
        }
    }
    gesture.line_speaker = 0;
    gesture.line_replays = 0;
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
    if (base_clip_complete(body)) {
        gesture.play_clip(body, clip, PLAY_CLIP_CROSSFADE);
        ++gesture.line_replays;
    }
}

bool speaker_gesture_install(const volatile uint32_t *speaker_lock)
{
    uint32_t bark = 0;

    signature_resolve_table(sites, SITE_COUNT);
    if (sites[SITE_DIALOG_RENDER].address == 0 || sites[SITE_PLAY_CLIP].address == 0) {
        log_warning("the sites the gesture repeat needs did not both resolve, so a gesture "
                    "still holds its last frame for the rest of a long line");
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
             "through on the speaker's body while the voice is still playing is started again "
             "(bark channel %08X, bapobj_playClip %08X)", (unsigned)bark,
             (unsigned)sites[SITE_PLAY_CLIP].address);
    return true;
}
