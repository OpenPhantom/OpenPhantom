/* dialogue_anim_fix.c: stop Obi-Wan's leftover talk animation during the Mos Espa opening exchange
 * with Qui-Gon.
 *
 * ============================== What is actually broken =======================================
 *
 * Field report: right at the start of level 6, Mos Espa, in the opening in-engine cutscene, Obi-Wan
 * and Qui-Gon talk. Qui-Gon's line should be the one animating, but Obi-Wan's head keeps moving as
 * if he were still talking. Confirmed live with a diagnostics build that watched both actors'
 * internal state frame by frame.
 *
 * The dialogue system itself is clean: the single global "who is speaking" cell (Dialog_SpeakSingle,
 * 0x00430D12) latches and clears correctly for every line, with no stale value and no skipped
 * switch. The head motion is not driven by dialogue state at all. It is a SEPARATE animation
 * channel, script opcode 0x202 "Animation" (the FSM interpreter's own case for it, inside
 * 0x00433D0B):
 *
 *   case 0x202:
 *     actor+0x1C0 = local_c[1];         <- ALWAYS rewritten, every time this node is visited
 *     if (actor+0x1BC != actor+0x1C0) { ... }
 *     local_1c = FUN_0042E3AD(actor, duration);
 *     break;
 *
 * FUN_0042E3AD only calls the real trigger (FUN_0041263F, "SetPrimaryAnim") when actor+0x1C0 and
 * actor+0x1BC differ, then latches actor+0x1BC to match. A live capture across the whole exchange
 * shows the reported shape exactly: Obi-Wan's actor+0x1C0 sits at his talk animation id for the
 * entire time Qui-Gon is speaking, only changing right before Obi-Wan's own next line. The critical
 * detail, learned from a first attempt at this fix that had no visible effect: the FSM interpreter
 * does not run this case once and move on. It stays parked on this exact node, frame after frame,
 * for as long as its own return value keeps saying "not done yet" (indefinitely, for a plain
 * Animation node with no explicit stop condition), and EVERY visit rewrites actor+0x1C0 back to
 * that line's own talk animation id unconditionally. A one-time correction the instant Qui-Gon's
 * line starts gets silently overwritten on the very next frame by Obi-Wan's own still-running node.
 *
 * ============================== What this does, and how narrowly ===============================
 *
 * A per-frame correction while it is armed, and it is ONLY EVER armed for this one conversation:
 *
 *   1. campaign_loadLevel (0x0043F70A, hooked below) names the level file being loaded. Arming
 *      requires "espa.b3d" (case-sensitive; every level path this engine loads is already lower
 *      case, so no fold is needed) - any other level disarms and forgets everything.
 *   2. Even while armed, an actor is only ever watched if their own body resolves (through the same
 *      body -> rdThing -> model3 name-string chain the earlier diagnostics build used) to a name
 *      starting "obinpc" or "pquigon". No other actor in Mos Espa, dialogue or not, is ever touched.
 *   3. Once armed and watching, an actor who is not the current global speaker and whose own talk-
 *      animation target is still non-idle is switched to idle through FUN_0042E3AD, the engine's
 *      own debounce and trigger - exactly what a correctly authored "Animation: idle" node would
 *      do - but only ONCE per stale streak, not every frame. actor+0x1BC (the id FUN_0042E3AD
 *      believes is already playing) is then kept in sync with whatever actor+0x1C0 the superseded
 *      actor's own script node keeps rewriting every frame, WITHOUT calling the trigger again, so
 *      their own next visit to that node sees no change and does not retrigger anything itself
 *      either. The idle animation switched to on the first frame is left alone after that, free to
 *      keep playing and looping normally. Calling the real trigger every frame instead (an earlier
 *      version of this fix did) restarts both animations from their own first frame every single
 *      frame forever, in an endless tug of war with the actor's own script node, which is what "he
 *      just pauses in place entirely" was: neither pose ever gets past its opening frame. This runs
 *      late enough in the frame (the shared render_frameEnd hook every other fix in this project's
 *      DLL set already uses) to land after that frame's own FSM tick, so the idle pose it forces is
 *      the one that actually gets drawn, even though the superseded actor's own node re-asserts its
 *      stale target moments earlier in the very same frame.
 *   4. The moment nobody has actually been speaking for HoldSeconds (the same single speaker cell
 *      and the dialogue-active flag Dialog_SpeakSingle's own timeout handler already clears between
 *      lines, so no extra bookkeeping is needed), this DISARMS itself completely: not just released
 *      until the next line, but off for the rest of this level, until the next campaign_loadLevel
 *      re-arms it. Those two globals blink to "nobody" for a moment between every line of the SAME
 *      exchange too, not only at its end, which is why this needs an actual hold timer rather than
 *      reacting to the first gap it sees.
 *
 * FUN_0042E3AD is never detoured, only called: this fix does not want to run every time the game's
 * own script evaluates that opcode, only once a frame, and only while armed. Nothing here touches
 * level data, and nothing here changes any line, subtitle, timing or camera; only a leftover talk
 * pose on one actor, in one scene, is told to stop.
 *
 * ============================== Why this narrow, instead of the first version ==================
 *
 * The first build of this fix was a GENERIC rule: any actor who had ever spoken, anywhere, held
 * their own talk-animation channel hostage for the rest of the session whenever they were not the
 * current speaker, which is true of them forever after their one line. Opcode 0x202 "Animation" is
 * not dialogue-specific - a level's own script reaches for it for ordinary gameplay animation too -
 * and that generic rule was overwriting THAT the instant it landed on actor+0x1C0, which is what
 * "some characters completely stop animating at all" was. Scoping arming to one level file and
 * watching by name to two specific actors means this can only ever act on the one conversation it
 * was written for; it does nothing anywhere else in the game, on purpose.
 */
#include "dialogue_anim_fix.h"

#include "common/detour.h"
#include "common/frame_hook.h"
#include "common/host_image.h"
#include "common/ini.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/signature.h"

#include <windows.h>
#include <mmsystem.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define DIALOGUE_ANIM_FIX_SECTION "dialogue_anim_fix"

/* --- campaign_loadLevel 0x0043F70A -------------------------------------------------------------
 * Byte-identical to the earlier diagnostics probe's own "level_load" site. `path` is the level
 * file being loaded, e.g. "level\espa.b3d". Prologue stops on a clean boundary at 9 bytes,
 * well short of the first CALL. */
static const uint8_t SIG_LEVEL_LOAD[] = {
    0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x88, 0x00, 0x00, 0x00, 0x56, 0x57, 0xC7,
    0x05, 0xD8, 0xCF, 0x6C, 0x00, 0x01, 0x00, 0x00
};
#define LEVEL_LOAD_PROLOGUE 9u

/* --- opcode 0x500 "Dialog Box" 0x004358B0, byte-identical to the probe this fix's own diagnosis
 * used, confirmed against WMAIN.EXE on disk. Prologue stops before the CALL at +0x0D, a CALL can
 * never sit inside a detour's relocated prologue. */
static const uint8_t SIG_DIALOG_BOX_START[] = {
    0x55,                                        /* push ebp                       */
    0x8B, 0xEC,                                  /* mov ebp,esp                    */
    0x83, 0xEC, 0x10,                            /* sub esp,0x10                   */
    0xC7, 0x45, 0xF4, 0x00, 0x00, 0x00, 0x00,    /* mov [ebp-0xc],0                */
    0xE8, 0x00, 0x00, 0x00, 0x00,                /* call <masked>                  */
    0x85, 0xC0,                                  /* test eax,eax                   */
    0x75, 0x09,                                  /* jnz +9                         */
    0x83, 0x3D, 0x9C, 0x4D, 0x6C, 0x00, 0x00,    /* cmp dword ptr [0x006c4d9c],0   */
    0x74, 0x0C                                   /* jz +0xc                        */
};
static const uint8_t MSK_DIALOG_BOX_START[] = {
    0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF
};
_Static_assert(sizeof(SIG_DIALOG_BOX_START) == sizeof(MSK_DIALOG_BOX_START),
               "the dialog-box-start pattern and its mask are different lengths");
#define DIALOG_BOX_START_PROLOGUE 13u

/* --- opcode 0x504 "Statement" 0x00435A0A, no absolute address in this stretch, no masking
 * needed. Prologue stops at the last boundary at or under DETOUR_PROLOGUE_MAX (16 bytes), right
 * after `mov [eax+0x78],edx`, five plain MOV/PUSH instructions in. */
static const uint8_t SIG_DIALOG_STATEMENT[] = {
    0x55,                                        /* push ebp                    */
    0x8B, 0xEC,                                  /* mov ebp,esp                 */
    0x51,                                        /* push ecx                    */
    0x8B, 0x45, 0x08,                            /* mov eax,[ebp+8]             */
    0x8B, 0x4D, 0x10,                            /* mov ecx,[ebp+0x10]          */
    0x8B, 0x51, 0x04,                            /* mov edx,[ecx+4]             */
    0x89, 0x50, 0x78,                            /* mov [eax+0x78],edx          */
    0x8B, 0x45, 0x10,                            /* mov eax,[ebp+0x10]          */
    0x8B, 0x08,                                  /* mov ecx,[eax]               */
    0x89, 0x4D, 0xFC,                            /* mov [ebp-4],ecx             */
    0x83, 0x7D, 0xFC, 0x10,                      /* cmp dword ptr [ebp-4],0x10  */
    0x7C, 0x07,                                  /* jl +7                       */
    0xC7, 0x45, 0xFC, 0xFF, 0xFF, 0xFF, 0xFF     /* mov dword ptr [ebp-4],-1    */
};
#define DIALOG_STATEMENT_PROLOGUE 16u

/* --- FUN_0042E3AD, the primary-animation debounce and trigger opcode 0x202 "Animation" itself
 * calls. Resolved but NEVER detoured, only called directly, once a frame, and only while armed. No
 * absolute address in this stretch either. */
static const uint8_t SIG_ANIM_RECHECK[] = {
    0x55,                                  /* push ebp                      */
    0x8B, 0xEC,                            /* mov ebp,esp                   */
    0x8B, 0x45, 0x08,                      /* mov eax,[ebp+8]               */
    0x8B, 0x48, 0x34,                      /* mov ecx,[eax+0x34]            */
    0x8B, 0x51, 0x14,                      /* mov edx,[ecx+0x14]            */
    0x8B, 0x45, 0x08,                      /* mov eax,[ebp+8]               */
    0x8B, 0x88, 0xC0, 0x01, 0x00, 0x00,    /* mov ecx,[eax+0x1c0]           */
    0x3B, 0x8A, 0xC8, 0x00, 0x00, 0x00,    /* cmp ecx,[edx+0xc8]            */
    0x7E, 0x19                             /* jle +0x19                     */
};

enum {
    SITE_LEVEL_LOAD,
    SITE_DIALOG_BOX_START,
    SITE_DIALOG_STATEMENT,
    SITE_ANIM_RECHECK,
    SITE_COUNT
};

static signature_t sites[SITE_COUNT] = {
    SIGNATURE_ENTRY("level_load",         SIG_LEVEL_LOAD),
    SIGNATURE_ENTRY_MASKED("dialog_box_start", SIG_DIALOG_BOX_START, MSK_DIALOG_BOX_START),
    SIGNATURE_ENTRY("dialog_statement",   SIG_DIALOG_STATEMENT),
    SIGNATURE_ENTRY("anim_recheck",       SIG_ANIM_RECHECK)
};

#define ACTOR_OWN_BODY_OFFSET       0x34u    /* actor record -> its own body pointer */
#define ACTOR_ANIM_TARGET_OFFSET    0x1C0u   /* the id last requested for the primary anim */
#define ACTOR_ANIM_CURRENT_OFFSET   0x1BCu   /* the id FUN_0042E3AD believes is already playing */
#define ANIM_ID_IDLE                    0
#define ANIM_ID_NONE                   -1    /* never a real id, forces a clean retrigger on sight */
#define DIALOG_CURRENT_SPEAKER_ADDR 0x00882180u  /* Dialog_SpeakSingle's own single-slot cell */
#define DIALOG_ACTIVE_FLAG_ADDR     0x00882184u  /* cleared between lines AND at the real end */
#define BODY_THING_OFFSET           0x9Cu    /* body -> rdThing*, same offset dismemberment.c and
                                              * the earlier diagnostics build both already read */
#define THING_MODEL3_OFFSET         0x04u    /* rdThing -> model3* */
#define MAX_TRACKED_ACTORS               4u  /* Obi-Wan and Qui-Gon, with headroom to spare */
#define LEVEL_FILE_NAME             "espa.b3d"
#define DEFAULT_HOLD_SECONDS           3.0f   /* longer than the gap between two lines of the SAME
                                               * exchange, short enough to let go promptly once it
                                               * is genuinely over */

typedef int32_t (__cdecl *level_load_fn_t)(const char *path);
typedef int32_t (__cdecl *dialog_box_start_fn_t)(int32_t actor_record, void *node, int32_t *data);
typedef void    (__cdecl *dialog_statement_fn_t)(int32_t actor_record, void *node, int32_t *data);
typedef int32_t (__cdecl *anim_recheck_fn_t)(int32_t actor_record, int32_t duration);

typedef struct dialogue_anim_fix_state {
    bool     installed;
    bool     enabled;

    detour_t level_load;
    detour_t dialog_box_start;
    detour_t dialog_statement;

    anim_recheck_fn_t anim_recheck;   /* resolved address, called directly, never detoured */

    bool     armed;    /* only true while the current level is LEVEL_FILE_NAME */

    int32_t  tracked_actors[MAX_TRACKED_ACTORS];
    uint32_t tracked_count;
    bool     forcing[MAX_TRACKED_ACTORS];   /* was this actor being corrected last frame */

    uint32_t hold_ms;
    DWORD    last_dialogue_activity_tick;   /* 0 = no dialogue observed since the last arm/release */

    uint32_t corrections_this_second;
    DWORD    corrections_log_tick;
} dialogue_anim_fix_state_t;

static dialogue_anim_fix_state_t fix_state;

static void load_config(void)
{
    float hold_seconds;

    fix_state.enabled = ini_read_bool(DIALOGUE_ANIM_FIX_SECTION, "Enabled", true);

    hold_seconds = ini_read_float(DIALOGUE_ANIM_FIX_SECTION, "HoldSeconds", DEFAULT_HOLD_SECONDS);
    if (hold_seconds < 0.5f) {
        hold_seconds = 0.5f;
    } else if (hold_seconds > 30.0f) {
        hold_seconds = 30.0f;
    }
    fix_state.hold_ms = (uint32_t)(hold_seconds * 1000.0f);
}

/* Every tracked actor is let go: this fix's hands come off them completely until the level is
 * loaded again (which re-arms) or, while still armed, one of the two watched names next speaks. */
static void release_all_tracked_actors(void)
{
    fix_state.tracked_count = 0;
    memset(fix_state.forcing, 0, sizeof(fix_state.forcing));
}

/* model3's own first bytes ARE a short name string - the same technique retail's own giant-model
 * special case in rdThing_Draw uses, and the same one the diagnostics build that first isolated
 * this bug already relied on. Returns false on any unreadable link in the chain, which reads as
 * "not a name we recognise" and leaves the actor untouched, the safe default. */
static bool actor_name_starts_with(int32_t actor_record, const char *prefix)
{
    void  *body = NULL;
    void  *thing = NULL;
    void  *model3 = NULL;
    char   name[9] = {0};
    size_t prefix_len = strlen(prefix);

    if (!memory_read((uintptr_t)actor_record + ACTOR_OWN_BODY_OFFSET, &body, sizeof(body)) ||
        body == NULL) {
        return false;
    }
    if (!memory_read((uintptr_t)body + BODY_THING_OFFSET, &thing, sizeof(thing)) || thing == NULL) {
        return false;
    }
    if (!memory_read((uintptr_t)thing + THING_MODEL3_OFFSET, &model3, sizeof(model3)) ||
        model3 == NULL) {
        return false;
    }
    if (prefix_len >= sizeof(name) ||
        !memory_read((uintptr_t)model3, name, prefix_len)) {
        return false;
    }
    return memcmp(name, prefix, prefix_len) == 0;
}

static bool actor_is_conversation_participant(int32_t actor_record)
{
    return actor_name_starts_with(actor_record, "obinpc") ||
           actor_name_starts_with(actor_record, "pquigon");
}

/* Remember an actor only while armed and only when their own name matches this one conversation.
 * Capped and silent past the cap, which two names with headroom should never reach. */
static void track_actor(int32_t actor_record)
{
    uint32_t i;

    if (!fix_state.armed || actor_record == 0 || !actor_is_conversation_participant(actor_record)) {
        return;
    }
    for (i = 0; i < fix_state.tracked_count; ++i) {
        if (fix_state.tracked_actors[i] == actor_record) {
            return;
        }
    }
    if (fix_state.tracked_count < MAX_TRACKED_ACTORS) {
        fix_state.tracked_actors[fix_state.tracked_count] = actor_record;
        ++fix_state.tracked_count;
    }
    fix_state.last_dialogue_activity_tick = timeGetTime();
}

/* Arm only for espa.b3d; anything else disarms and forgets whatever was being watched before,
 * which also covers leaving Mos Espa and coming back later - a fresh load re-arms from nothing. */
static int32_t __cdecl hook_level_load(const char *path)
{
    level_load_fn_t original = (level_load_fn_t)fix_state.level_load.original;
    bool            is_espa = (path != NULL) && (strstr(path, LEVEL_FILE_NAME) != NULL);
    int32_t         result;

    fix_state.armed = is_espa;
    release_all_tracked_actors();
    fix_state.last_dialogue_activity_tick = 0;
    if (is_espa) {
        log_info("dialogue_anim_fix: armed for \"%s\", watching for obinpc/pquigon", path);
    }

    result = original(path);
    return result;
}

static int32_t __cdecl hook_dialog_box_start(int32_t actor_record, void *node, int32_t *data)
{
    dialog_box_start_fn_t original =
        (dialog_box_start_fn_t)fix_state.dialog_box_start.original;
    int32_t                result = original(actor_record, node, data);

    track_actor(actor_record);
    return result;
}

static void __cdecl hook_dialog_statement(int32_t actor_record, void *node, int32_t *data)
{
    dialog_statement_fn_t original =
        (dialog_statement_fn_t)fix_state.dialog_statement.original;

    original(actor_record, node, data);
    track_actor(actor_record);
}

/* Once a rendered frame, after that frame's own FSM tick has already run: every tracked actor who
 * is not the current speaker and whose own talk-animation target is still non-idle gets forced
 * back to idle. Their own script node will rewrite it again on the NEXT frame if it is still
 * parked there, which is exactly why this has to run every frame rather than once.
 *
 * Before any of that: if nobody has actually been speaking for HoldSeconds, this DISARMS - not
 * just a release until the next line, but off for the rest of this level, same as if a different
 * level had just loaded. The two globals blink to "nobody" for a moment between every line of the
 * same exchange too, not only at its end, which is why this needs an actual hold timer. */
static void on_frame_correct_stale_speakers(void)
{
    uint32_t current_speaker = 0;
    uint32_t dialogue_active = 0;
    DWORD    now;
    uint32_t i;
    uint32_t corrected_now = 0;

    if (!fix_state.armed || fix_state.anim_recheck == NULL || fix_state.tracked_count == 0) {
        return;
    }
    memory_read_u32(DIALOG_CURRENT_SPEAKER_ADDR, &current_speaker);
    memory_read_u32(DIALOG_ACTIVE_FLAG_ADDR, &dialogue_active);

    now = timeGetTime();
    if (current_speaker != 0 || dialogue_active != 0) {
        fix_state.last_dialogue_activity_tick = now;
    } else if (fix_state.last_dialogue_activity_tick != 0 &&
              (uint32_t)(now - fix_state.last_dialogue_activity_tick) > fix_state.hold_ms) {
        log_info("dialogue_anim_fix: no dialogue activity for %.1f s, the exchange is over - "
                 "disarming for the rest of this level",
                 (double)fix_state.hold_ms / 1000.0);
        fix_state.armed = false;
        release_all_tracked_actors();
        fix_state.last_dialogue_activity_tick = 0;
        return;
    }

    for (i = 0; i < fix_state.tracked_count; ++i) {
        int32_t  actor = fix_state.tracked_actors[i];
        uint32_t body = 0;
        int32_t  target = 0;

        if (!memory_read((uintptr_t)actor + ACTOR_OWN_BODY_OFFSET, &body, sizeof(body))) {
            fix_state.forcing[i] = false;
            continue;
        }
        if (body == current_speaker) {
            if (fix_state.forcing[i]) {
                /* they are speaking for real again: leave a value behind that can never match a
                 * real animation id, so their own script's next Animation node is guaranteed to
                 * read as a CHANGE and retrigger for real, even on the unlikely chance it reuses
                 * the exact id this fix was holding them at idle against. */
                *(int32_t *)((uintptr_t)actor + ACTOR_ANIM_CURRENT_OFFSET) = ANIM_ID_NONE;
                fix_state.forcing[i] = false;
            }
            continue;
        }
        if (!memory_read((uintptr_t)actor + ACTOR_ANIM_TARGET_OFFSET, &target, sizeof(target)) ||
            target == ANIM_ID_IDLE) {
            fix_state.forcing[i] = false;
            continue;
        }

        if (!fix_state.forcing[i]) {
            /* First frame of this correction: do the real switch, through the engine's own
             * debounce and trigger, exactly once. */
            *(int32_t *)((uintptr_t)actor + ACTOR_ANIM_TARGET_OFFSET) = ANIM_ID_IDLE;
            fix_state.anim_recheck(actor, 0);
            fix_state.forcing[i] = true;
            ++fix_state.corrections_this_second;
        }
        /* Every frame, forced or not: their own script node keeps rewriting actor+0x1C0 back to
         * the stale id on its OWN next visit (earlier in this same frame, before this hook runs),
         * and would retrigger it for real the moment it next sees a mismatch against actor+0x1BC.
         * Keeping actor+0x1BC in sync with whatever their script just wrote satisfies that check
         * WITHOUT calling the trigger again, so the idle animation this fix switched to on the
         * first frame is left alone to keep playing and looping normally instead of being
         * restarted from its own first frame every single frame, which is what "he just pauses in
         * place entirely" was: two animations endlessly restarting each other, neither ever
         * getting past its opening pose. */
        *(int32_t *)((uintptr_t)actor + ACTOR_ANIM_CURRENT_OFFSET) = target;
        ++corrected_now;
    }

    if (corrected_now == 0) {
        return;
    }
    {
        if (fix_state.corrections_log_tick == 0 ||
            (int32_t)(now - fix_state.corrections_log_tick) >= 0) {
            log_info("dialogue_anim_fix: %u actor(s) held off their own stale talk animation this "
                     "second (%u new transitions caught)",
                     (unsigned)corrected_now, (unsigned)fix_state.corrections_this_second);
            fix_state.corrections_this_second = 0;
            fix_state.corrections_log_tick = now + 1000u;
        }
    }
}

void dialogue_anim_fix_install(void)
{
    if (fix_state.installed) {
        return;
    }
    fix_state.installed = true;

    log_init("dialogue_anim_fix", false);

    if (!host_image_resolve()) {
        log_error("no 32-bit host image, this fix stays off");
        return;
    }

    load_config();
    if (!fix_state.enabled) {
        log_info("disabled");
        return;
    }

    signature_resolve_table(sites, SITE_COUNT);

    if (sites[SITE_ANIM_RECHECK].address != 0) {
        fix_state.anim_recheck = (anim_recheck_fn_t)sites[SITE_ANIM_RECHECK].address;
    } else {
        log_warning("the primary-animation trigger did not resolve, this fix stays off");
        return;
    }

    if (sites[SITE_LEVEL_LOAD].address != 0) {
        if (!detour_install(&fix_state.level_load, sites[SITE_LEVEL_LOAD].address,
                            (const void *)hook_level_load, LEVEL_LOAD_PROLOGUE)) {
            log_warning("the detour on campaign_loadLevel failed, this fix cannot arm itself and "
                        "stays off");
            return;
        }
    } else {
        log_warning("campaign_loadLevel did not resolve, this fix cannot arm itself and stays off");
        return;
    }

    if (sites[SITE_DIALOG_BOX_START].address != 0) {
        if (detour_install(&fix_state.dialog_box_start, sites[SITE_DIALOG_BOX_START].address,
                           (const void *)hook_dialog_box_start, DIALOG_BOX_START_PROLOGUE)) {
            log_info("hooked opcode 0x500 Dialog Box at %08X",
                     (unsigned)sites[SITE_DIALOG_BOX_START].address);
        } else {
            log_warning("the detour on opcode 0x500 Dialog Box failed");
        }
    } else {
        log_warning("opcode 0x500 Dialog Box did not resolve");
    }

    if (sites[SITE_DIALOG_STATEMENT].address != 0) {
        if (detour_install(&fix_state.dialog_statement, sites[SITE_DIALOG_STATEMENT].address,
                           (const void *)hook_dialog_statement, DIALOG_STATEMENT_PROLOGUE)) {
            log_info("hooked opcode 0x504 Statement at %08X",
                     (unsigned)sites[SITE_DIALOG_STATEMENT].address);
        } else {
            log_warning("the detour on opcode 0x504 Statement failed");
        }
    } else {
        log_warning("opcode 0x504 Statement did not resolve");
    }

    if (fix_state.dialog_box_start.original == NULL && fix_state.dialog_statement.original == NULL) {
        log_warning("neither dialogue trigger hooked, this fix cannot do anything this session");
        return;
    }

    if (!frame_hook_add(on_frame_correct_stale_speakers)) {
        log_warning("the per-frame hook could not be installed, arming will still be tracked but "
                    "nothing will ever be corrected");
        return;
    }

    log_info("armed only for \"%s\": while there, Obi-Wan or Qui-Gon's leftover talk animation is "
             "held at idle every frame the other one is actually speaking, and this disarms itself "
             "for good once their exchange is over", LEVEL_FILE_NAME);
}
