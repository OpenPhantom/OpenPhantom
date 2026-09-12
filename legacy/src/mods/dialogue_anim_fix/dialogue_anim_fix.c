/* dialogue_anim_fix.c: stop a character's talk animation carrying on after their line, in the
 * scenes where the script leaves it parked.
 *
 * SIZE NOTE: over the 600 line mark, and most of it is the account below of what is actually
 * broken and the two mistakes already made here, with the five patterns and their evidence. The
 * code is three small hooks and one per-frame pass, and the reason each exists is not recoverable
 * from the code alone.
 *
 * ============================== What is actually broken =======================================
 *
 * Field report: right at the start of level 6, Mos Espa, in the opening in-engine cutscene, Obi-Wan
 * and Qui-Gon talk. Qui-Gon's line should be the one animating, but Obi-Wan's head keeps moving as
 * if he were still talking. Confirmed live with a diagnostics build that watched both actors'
 * internal state frame by frame.
 *
 * The dialogue system itself is clean: the single global "who is speaking" cell
 * (Dialog_SpeakSingle, 0x00430D12) latches and clears correctly for every line, with no stale
 * value and no skipped switch. The head motion is not driven by dialogue state at all. It is a
 * SEPARATE animation channel, script opcode 0x202 "Animation" (the FSM interpreter's own case for
 * it, inside 0x00433D0B):
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
 * A per-frame correction while it is armed, and it is ONLY EVER armed for the scenes in the scope
 * table below, each a level file and the model names of the actors reported in it:
 *
 *   1. campaign_loadLevel (0x0043F70A, hooked below) names the level file being loaded. Arming
 *      requires the path to contain a scope's level name (case-sensitive; every level path this
 *      engine loads is already lower case, so no fold is needed); any other level disarms and
 *      forgets everything.
 *   2. Even while armed, an actor is only ever watched if their own body resolves (through the
 *      same body -> rdThing -> model3 name-string chain the earlier diagnostics build used) to a
 *      name starting with one of that scope's prefixes. No other actor in the level, dialogue or
 *      not, is ever touched.
 *   3. While a watched actor is the current global speaker, the id their own script keeps
 *      asking for in actor+0x1C0 is remembered: that is the animation their line was played
 *      with, and the only one ever held off. The frame their line ends, if their script is
 *      still asking for that same id, they are switched to the scene's rest animation through
 *      FUN_0042E3AD, the engine's own debounce and trigger, exactly what a correctly authored
 *      "Animation: idle" node would do, ONCE. actor+0x1BC (the id FUN_0042E3AD believes is
 *      already playing) is then kept at the remembered id every frame, WITHOUT calling the
 *      trigger again, so their own next visit to the parked node sees no change and does not
 *      retrigger anything itself either. The rest clip switched to on the first frame is left
 *      alone after that until its track reports complete, and then started again, as the
 *      engine's own idle mode does with a clip: the stand and talk clips on these models are
 *      authored as one pass of a few seconds, and FUN_0042E3AD itself returns 1 on that same
 *      flag so a script can move on. Clips 0, 3 and 7 all played once and froze until
 *      the replay was added. Calling the real trigger every frame instead (an earlier version of
 *      this fix did) restarts both animations from their own first frame every single frame
 *      forever, in an endless tug of war with the actor's own script node. That was the "he just
 *      pauses in place entirely" report: neither pose ever gets past its opening frame. Both
 *      cells go back to the held id straight after a trigger, because the script only visits its
 *      node on a simulation step and a rest id left in actor+0x1C0 over a rendered frame without
 *      one reads as the script moving on. This runs late enough in the frame
 *      (the shared render_frameEnd hook every other fix in this project's DLL set already uses)
 *      to land after that frame's own FSM tick, so the rest pose it forces is the one that
 *      actually gets drawn, even though the actor's own node re-asserts its stale target moments
 *      earlier in the very same frame.
 *   4. The hold ends the moment the script asks for anything else. actor+0x1BC still names the
 *      held id, so the new request reads as a change to FUN_0042E3AD and plays for real; a walk,
 *      a gesture, or the actor's next line all go through untouched. A version that held off
 *      every non-idle id an actor asked for while somebody else spoke idled the jail prisoner's
 *      walk between his two lines, which was "when he's running he has no animation". Only the
 *      id seen during the actor's own line is ever held, and only while the script keeps
 *      asking for exactly that.
 *   5. A scene whose exchange ends, like the Mos Espa cutscene, disarms itself completely the
 *      moment nobody has been speaking for HoldSeconds (the same single speaker cell and the
 *      dialogue-active flag Dialog_SpeakSingle's own timeout handler already clears between
 *      lines, so no extra bookkeeping is needed): not just released until the next line, but off
 *      for the rest of this level, until the next campaign_loadLevel re-arms it. Those two
 *      globals blink to "nobody" for a moment between every line of the SAME exchange too, not
 *      only at its end, so this needs an actual hold timer rather than reacting to the first gap
 *      it sees. A scene whose script parks on the talk node for the rest of the level, like the
 *      jail, says so in its row and stays armed, because the parked node keeps asking every
 *      frame for as long as the level lasts.
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
 * not dialogue-specific; a level's own script reaches for it for ordinary gameplay animation too,
 * and that generic rule was overwriting THAT the instant it landed on actor+0x1C0. That was the
 * "some characters completely stop animating at all" report. Scoping arming to a level file and
 * watching by name means this can only ever act on the conversations it was written for, and
 * holding only the id seen during the actor's own line means it cannot act on any other animation
 * even there; it does nothing anywhere else in the game, on purpose.
 */
#include "dialogue_anim_fix.h"
#include "idle_clip.h"

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
_Static_assert(sizeof SIG_DIALOG_BOX_START == sizeof MSK_DIALOG_BOX_START,
               "the dialog box start pattern and its mask are different lengths");
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

/* --- Dialog_SpeakSingle 0x00430D12, the owner of the two cells the per-frame pass reads ---------
 * Resolved and never detoured by THIS DLL, and declared as a detour target all the same, because
 * camera_handback_fix and diagnostics both detour it and both load first: by the time this looks,
 * the six byte prologue is a jump, and a plain pattern found nothing. The two cells used to be
 * written down as addresses; they are read out of this function's own operands now, since it is
 * the one place both are written from:
 *
 *   +0x1C  8B 0D <speaker>       mov ecx,[g_dialogSpeaker]     is this actor already speaking
 *   +0x3D  89 15 <speaker>       mov [g_dialogSpeaker],edx     no: they are now
 *   +0x78  C7 05 <active> 01..   mov [g_dialogActive],1        and a line is in progress
 *
 * The speaker cell appears twice and the two operands have to agree, which is the check that the
 * pattern matched the function and not merely its shape. Every displacement and every other
 * absolute operand is masked; the 130 bytes are unique on what is left. */
static const uint8_t SIG_SPEAK_SINGLE[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x0C,                  /* push ebp / mov ebp,esp / sub esp,0xC */
    0xC7, 0x45, 0xF8, 0x00, 0x00, 0x00, 0x00,            /* mov [ebp-8],0                        */
    0x8B, 0x45, 0x10, 0x50,                              /* push [ebp+0x10]                      */
    0xE8, 0x00, 0x00, 0x00, 0x00,                        /* call (the line's duration)           */
    0x83, 0xC4, 0x04, 0xD9, 0x5D, 0xFC,                  /* add esp,4 / fstp [ebp-4]             */
    0x8B, 0x0D, 0x00, 0x00, 0x00, 0x00,                  /* mov ecx,[g_dialogSpeaker]            */
    0x3B, 0x4D, 0x08, 0x75, 0x09,                        /* cmp ecx,[ebp+8] / jne                */
    0x83, 0x3D, 0x00, 0x00, 0x00, 0x00, 0x00, 0x74, 0x59, /* cmp [a third cell],0 / je */
    0x6A, 0x00, 0xE8, 0x00, 0x00, 0x00, 0x00,            /* push 0 / call (stop the last line)   */
    0x83, 0xC4, 0x04, 0x8B, 0x55, 0x08,                  /* add esp,4 / mov edx,[ebp+8]          */
    0x89, 0x15, 0x00, 0x00, 0x00, 0x00,                  /* mov [g_dialogSpeaker],edx            */
    0x8B, 0x45, 0x14, 0x50, 0x6A, 0x00, 0x8B, 0x4D, 0x10, /* push [ebp+0x14] / push 0 / */
    0x51, 0xE8, 0x00, 0x00, 0x00, 0x00,                  /* push [ebp+0x10] / call (play it)     */
    0x83, 0xC4, 0x0C, 0x83, 0x7D, 0x0C, 0x00, 0x7C, 0x0C, /* add esp,0xC / cmp [ebp+0xC],0 / jl */
    0x8B, 0x55, 0x0C, 0x52, 0xE8, 0x00, 0x00, 0x00, 0x00, /* push [ebp+0xC] / call (camera) */
    0x83, 0xC4, 0x04, 0xA1, 0x00, 0x00, 0x00, 0x00,      /* add esp,4 / mov eax,[the clock]      */
    0xD9, 0x40, 0x54, 0xD8, 0x45, 0xFC,                  /* fld [eax+0x54] / fadd [ebp-4]        */
    0xD9, 0x1D, 0x00, 0x00, 0x00, 0x00,                  /* fstp [the line's end time]           */
    0xC7, 0x05, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00  /* mov [g_dialogActive],1        */
};
static const uint8_t MSK_SPEAK_SINGLE[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF
};
_Static_assert(sizeof SIG_SPEAK_SINGLE == sizeof MSK_SPEAK_SINGLE,
               "the Dialog_SpeakSingle pattern and its mask are different lengths");
#define SPEAK_SINGLE_PROLOGUE              6u      /* push ebp / mov ebp,esp / sub esp,0xC */
#define SPEAK_SINGLE_SPEAKER_READ_OPERAND  0x1Eu
#define SPEAK_SINGLE_SPEAKER_WRITE_OPERAND 0x3Fu
#define SPEAK_SINGLE_ACTIVE_OPERAND        0x7Au

enum {
    SITE_LEVEL_LOAD,
    SITE_DIALOG_BOX_START,
    SITE_DIALOG_STATEMENT,
    SITE_ANIM_RECHECK,
    SITE_SPEAK_SINGLE,
    SITE_COUNT
};

static signature_t sites[SITE_COUNT] = {
    SIGNATURE_ENTRY_DETOUR("level_load", SIG_LEVEL_LOAD, LEVEL_LOAD_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR_MASKED("dialog_box_start", SIG_DIALOG_BOX_START, MSK_DIALOG_BOX_START,
                                  DIALOG_BOX_START_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR("dialog_statement", SIG_DIALOG_STATEMENT, DIALOG_STATEMENT_PROLOGUE),
    SIGNATURE_ENTRY("anim_recheck",       SIG_ANIM_RECHECK),
    SIGNATURE_ENTRY_DETOUR_MASKED("speak_single", SIG_SPEAK_SINGLE, MSK_SPEAK_SINGLE,
                                  SPEAK_SINGLE_PROLOGUE)
};

#define ACTOR_PLACEMENT_OFFSET      0x04u    /* char[12], the placement label the spawn path
                                              * copies in; the diagnostics census prints it */
#define ACTOR_PLACEMENT_SIZE          12u
#define ACTOR_OWN_BODY_OFFSET       0x34u    /* actor record -> its own body pointer */
#define ACTOR_HEALTH_OFFSET         0x38u    /* int, the spawn path's local_8[0xe]; below 1 is
                                              * dead, and the diagnostics census reads the same
                                              * cell */
#define ACTOR_ANIM_TARGET_OFFSET    0x1C0u   /* the id last requested for the primary anim */
#define ACTOR_ANIM_CURRENT_OFFSET   0x1BCu   /* the id FUN_0042E3AD believes is already playing */
#define ANIM_ID_NONE                   -1    /* never a real id, forces a clean retrigger on
                                              * sight */
#define BODY_THING_OFFSET           0x9Cu    /* body -> rdThing*, same offset dismemberment.c and
                                              * the earlier diagnostics build both already read */
#define THING_MODEL3_OFFSET         0x04u    /* rdThing -> model3* */
#define THING_PUPPET_OFFSET         0x18u    /* rdThing -> rdPuppet* */
#define BODY_CURRENT_CLIP_OFFSET    0xE8u    /* body -> the clip last put on its base layer */
#define BODY_PRIMARY_SLOT_OFFSET    0xECu    /* body -> the puppet track its base clip is on */
#define PUPPET_TRACKS_OFFSET        0x08u    /* rdPuppet -> tracks[], 0x14C bytes each */
#define PUPPET_TRACK_STRIDE         0x14Cu
#define TRACK_COMPLETE_OFFSET       0x140u   /* int, raised when the clip has played through */
#define PUPPET_TRACK_LIMIT             8     /* a slot index past this is not a slot */
#define MAX_TRACKED_ACTORS               4u  /* two speakers, with headroom to spare */

/* The scenes this acts in. One row per level: the actors by the first letters of their model's
 * own name, which is the same string the census in diagnostics resolves, the clip the actor is
 * put in once their line is over, and whether the exchange ends. A new report adds a row here
 * from one census run with Dialogue=1 and Characters=1.
 *
 *   espa.b3d   Mos Espa's opening cutscene: Obi-Wan's head keeps talking through Qui-Gon's line.
 *              The exchange ends and the fix disarms after it.
 *   queen.b3d  the Theed jail: a prisoner spoken to keeps the talking animation after the
 *              conversation is over, his script parked on its talk node for the rest of the
 *              level (issue 26). The census read him at 6/6 before he is spoken to, 8/8 through
 *              both his lines, 2/2 running between them, and 8/8 in script mode 7 for good after
 *              the second. His model, nabcit2, carries ten clips: 0 stnd1, 1 walk1, 2 run1,
 *              3 talk1, 4 hit1, 5 die1, 6 lmout, 7 talk2, 8 talk3, 9 butn1. Clips 0, 3, 6 and
 *              7 were each tried in the cell and none reads as a man waiting: the stand is a
 *              held pose and the other three are talking. So the stand, his own idle, is
 *              written over with the generated one in idle_clip.c before he is put in it, and
 *              the hands-up pass he sits in before he is spoken to is left as shipped. The row
 *              names him twice over, the model nabcit2 and the placement enemy031, so the other
 *              citizens in the level, some on the same model family, are never watched; the
 *              idle is written over the model's stand only once he is being held, so a run in
 *              which he is never spoken to changes nothing at all. Nothing ends, so nothing
 *              disarms. */
typedef struct scene_scope {
    const char *level_file;
    const char *placement;          /* one placement label, or NULL for any with the model */
    int32_t     rest_anim;          /* the clip the actor is put in after their line */
    bool        generate_idle;      /* write the idle in idle_clip.c over that clip first */
    bool        exchange_ends;      /* disarm after HoldSeconds of silence */
    const char *prefixes[3];        /* NULL terminated */
} scene_scope_t;

static const scene_scope_t SCOPES[] = {
    { "espa.b3d",  NULL,       0, false, true,  { "obinpc", "pquigon", NULL } },
    { "queen.b3d", "enemy031", 0, true,  false, { "nabcit2", NULL, NULL } }
};
#define SCOPE_COUNT (sizeof SCOPES / sizeof SCOPES[0])
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

    /* Dialog_SpeakSingle's own single-slot "who is speaking" cell, and the flag it raises for a
     * line in progress and clears between lines and at the real end. Both read out of that
     * function's operands at install. */
    const volatile uint32_t *current_speaker;
    const volatile uint32_t *dialogue_active;

    bool                 armed;     /* only true while the current level is in SCOPES */
    const scene_scope_t *scope;     /* the row of SCOPES this level matched, NULL when none */

    int32_t  tracked_actors[MAX_TRACKED_ACTORS];
    uint32_t tracked_count;
    int32_t  spoken_id[MAX_TRACKED_ACTORS]; /* the id asked for during their line, NONE between */
    int32_t  held_id[MAX_TRACKED_ACTORS];   /* the id being held off, NONE when none */

    uint32_t hold_ms;
    DWORD    last_dialogue_activity_tick;   /* 0 = no dialogue observed since the last arm or
                                             * release */

} dialogue_anim_fix_state_t;

static dialogue_anim_fix_state_t fix_state;

static void load_config(void)
{
    float hold_seconds;

    fix_state.enabled = ini_read_bool(DIALOGUE_ANIM_FIX_SECTION, "Enabled", true);

    hold_seconds = ini_read_float(DIALOGUE_ANIM_FIX_SECTION, "HoldSeconds", DEFAULT_HOLD_SECONDS);
    /* Written as a NOT so that a value which is not a number lands on the floor rather than
     * through both arms: every comparison against a NaN is false, and the cast of one to an
     * unsigned is undefined. On x86 it produced 0x80000000, which is a hold of 24 days and no
     * disarm for the rest of the session. */
    if (!(hold_seconds >= 0.5f)) {
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
    uint32_t i;

    fix_state.tracked_count = 0;
    for (i = 0; i < MAX_TRACKED_ACTORS; ++i) {
        fix_state.spoken_id[i] = ANIM_ID_NONE;
        fix_state.held_id[i]   = ANIM_ID_NONE;
    }
}

/* model3's own first bytes ARE a short name string, the same technique retail's own giant-model
 * special case in rdThing_Draw uses, and the same one the diagnostics build that first isolated
 * this bug already relied on. Returns false on any unreadable link in the chain, which reads as
 * "not a name we recognise" and leaves the actor untouched, the safe default. The reads are the
 * faulting kind rather than the asking kind: this runs inside the engine's own dialogue opcodes,
 * on pointers it handed over a moment ago. */
static bool actor_name_starts_with(int32_t actor_record, const char *prefix)
{
    void  *body = NULL;
    void  *thing = NULL;
    void  *model3 = NULL;
    char   name[9] = {0};
    size_t prefix_len = strlen(prefix);

    if (!memory_try_read((uintptr_t)actor_record + ACTOR_OWN_BODY_OFFSET, &body, sizeof(body)) ||
        body == NULL) {
        return false;
    }
    if (!memory_try_read((uintptr_t)body + BODY_THING_OFFSET, &thing, sizeof(thing)) ||
        thing == NULL) {
        return false;
    }
    if (!memory_try_read((uintptr_t)thing + THING_MODEL3_OFFSET, &model3, sizeof(model3)) ||
        model3 == NULL) {
        return false;
    }
    if (prefix_len >= sizeof(name) ||
        !memory_try_read((uintptr_t)model3, name, prefix_len)) {
        return false;
    }
    return memcmp(name, prefix, prefix_len) == 0;
}

/* The placement label is not unique across the game, so a row never relies on it alone: it
 * narrows a model match to one placement within the one level the row names. */
static bool actor_placement_is(int32_t actor_record, const char *placement)
{
    char name[ACTOR_PLACEMENT_SIZE + 1] = {0};

    return memory_try_read((uintptr_t)actor_record + ACTOR_PLACEMENT_OFFSET, name,
                           ACTOR_PLACEMENT_SIZE) &&
           strcmp(name, placement) == 0;
}

static bool actor_is_conversation_participant(int32_t actor_record)
{
    const scene_scope_t *scope = fix_state.scope;
    size_t               i;

    if (scope == NULL) {
        return false;
    }
    if (scope->placement != NULL && !actor_placement_is(actor_record, scope->placement)) {
        return false;
    }
    for (i = 0; scope->prefixes[i] != NULL; ++i) {
        if (actor_name_starts_with(actor_record, scope->prefixes[i])) {
            return true;
        }
    }
    return false;
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

/* Arm only for a level in the scope table; anything else disarms and forgets whatever was being
 * watched before, which also covers leaving the level and coming back later; a fresh load re-arms
 * from nothing. */
static int32_t __cdecl hook_level_load(const char *path)
{
    level_load_fn_t original = (level_load_fn_t)fix_state.level_load.original;
    int32_t         result;
    size_t          i;

    fix_state.scope = NULL;
    if (path != NULL) {
        for (i = 0; i < SCOPE_COUNT; ++i) {
            if (strstr(path, SCOPES[i].level_file) != NULL) {
                fix_state.scope = &SCOPES[i];
                break;
            }
        }
    }
    fix_state.armed = (fix_state.scope != NULL);
    release_all_tracked_actors();
    fix_state.last_dialogue_activity_tick = 0;
    if (fix_state.armed) {
        log_info("dialogue_anim_fix: armed for \"%s\", watching for %s%s%s%s%s", path,
                 fix_state.scope->prefixes[0],
                 fix_state.scope->prefixes[1] ? "/" : "",
                 fix_state.scope->prefixes[1] ? fix_state.scope->prefixes[1] : "",
                 fix_state.scope->placement ? " placed as " : "",
                 fix_state.scope->placement ? fix_state.scope->placement : "");
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

/* The clip on the body's base layer right now, or NONE when the body does not read. The engine
 * puts a clip there on paths that never pass through actor+0x1C0: a death is one, played
 * straight onto the body by the hit handling while the parked script node still asks for its
 * talk id every step. */
static int32_t body_current_clip(int32_t actor)
{
    uint32_t body = 0;
    int32_t  clip = ANIM_ID_NONE;

    if (!memory_try_read((uintptr_t)actor + ACTOR_OWN_BODY_OFFSET, &body, sizeof(body)) ||
        body == 0 ||
        !memory_try_read((uintptr_t)body + BODY_CURRENT_CLIP_OFFSET, &clip, sizeof(clip))) {
        return ANIM_ID_NONE;
    }
    return clip;
}

/* The base clip's track has played through. The rest clips are authored as one pass, a few
 * seconds of standing, and the engine's own idle replays them from this flag; a script parked
 * on a talk node never gets there, so the hold does it instead. */
static bool rest_clip_has_finished(int32_t actor)
{
    uint32_t body = 0;
    uint32_t thing = 0;
    uint32_t puppet = 0;
    int32_t  slot = -1;
    int32_t  complete = 0;

    return memory_try_read((uintptr_t)actor + ACTOR_OWN_BODY_OFFSET, &body, sizeof(body)) &&
           body != 0 &&
           memory_try_read((uintptr_t)body + BODY_THING_OFFSET, &thing, sizeof(thing)) &&
           thing != 0 &&
           memory_try_read((uintptr_t)thing + THING_PUPPET_OFFSET, &puppet, sizeof(puppet)) &&
           puppet != 0 &&
           memory_try_read((uintptr_t)body + BODY_PRIMARY_SLOT_OFFSET, &slot, sizeof(slot)) &&
           slot >= 0 && slot < PUPPET_TRACK_LIMIT &&
           memory_try_read((uintptr_t)puppet + PUPPET_TRACKS_OFFSET +
                           (uint32_t)slot * PUPPET_TRACK_STRIDE + TRACK_COMPLETE_OFFSET,
                           &complete, sizeof(complete)) &&
           complete != 0;
}

/* The one real switch: the actor is put at the scene's rest animation through the engine's own
 * debounce and trigger, and then the id their script keeps asking for is written back as the
 * one already playing, so the parked node's next visit sees no change. Called once when the hold
 * begins and again each time the rest clip has played through. */
static void play_rest_clip(int32_t actor, int32_t held)
{
    *(int32_t *)((uintptr_t)actor + ACTOR_ANIM_CURRENT_OFFSET) = ANIM_ID_NONE;
    *(int32_t *)((uintptr_t)actor + ACTOR_ANIM_TARGET_OFFSET) = fix_state.scope->rest_anim;
    fix_state.anim_recheck(actor, 0);
    /* Both cells back to the held id: the trigger has already fired, and the script only visits
     * its node on a simulation step, so a rest id left in actor+0x1C0 over a rendered frame
     * without one reads as the script moving on and drops the hold. */
    *(int32_t *)((uintptr_t)actor + ACTOR_ANIM_TARGET_OFFSET) = held;
    *(int32_t *)((uintptr_t)actor + ACTOR_ANIM_CURRENT_OFFSET) = held;
}

static void begin_hold(uint32_t i, int32_t actor, int32_t id)
{
    uint32_t body = 0;

    if (fix_state.scope->generate_idle &&
        memory_try_read((uintptr_t)actor + ACTOR_OWN_BODY_OFFSET, &body, sizeof body) &&
        body != 0) {
        (void)idle_clip_install(body, fix_state.scope->rest_anim);
    }
    play_rest_clip(actor, id);
    fix_state.held_id[i] = id;
    log_info("dialogue_anim_fix: actor %08X's line is over and their script still asks for "
             "animation %d every frame, so they are put at rest (%d) and that request is "
             "answered without a retrigger until it changes", (unsigned)actor, id,
             fix_state.scope->rest_anim);
}

/* If nobody has actually been speaking for HoldSeconds in a scene whose exchange ends, this
 * DISARMS: not just a release until the next line, but off for the rest of this level, same as
 * if a different level had just loaded. Returns true when it did. */
static bool disarm_if_exchange_over(DWORD now, bool anyone_speaking)
{
    if (anyone_speaking) {
        fix_state.last_dialogue_activity_tick = now;
        return false;
    }
    if (!fix_state.scope->exchange_ends || fix_state.last_dialogue_activity_tick == 0 ||
        (uint32_t)(now - fix_state.last_dialogue_activity_tick) <= fix_state.hold_ms) {
        return false;
    }
    log_info("dialogue_anim_fix: no dialogue activity for %.1f s, the exchange is over, so "
             "this disarms for the rest of this level", (double)fix_state.hold_ms / 1000.0);
    fix_state.armed = false;
    release_all_tracked_actors();
    fix_state.last_dialogue_activity_tick = 0;
    return true;
}

/* Once a rendered frame, after that frame's own FSM tick has already run. For every tracked
 * actor: while they speak, remember what their script asks for; the frame their line ends, if it
 * is still asking for that, hold it off; while held, answer the parked node every frame without
 * a retrigger; the moment it asks for anything else, or they speak again, let go. */
static void on_frame_correct_stale_speakers(void)
{
    uint32_t current_speaker;
    uint32_t dialogue_active;
    uint32_t i;

    if (!fix_state.armed || fix_state.anim_recheck == NULL || fix_state.tracked_count == 0) {
        return;
    }
    current_speaker = *fix_state.current_speaker;
    dialogue_active = *fix_state.dialogue_active;
    if (disarm_if_exchange_over(timeGetTime(), current_speaker != 0 || dialogue_active != 0)) {
        return;
    }

    for (i = 0; i < fix_state.tracked_count; ++i) {
        int32_t  actor = fix_state.tracked_actors[i];
        uint32_t body = 0;
        int32_t  wanted = 0;

        if (!memory_try_read((uintptr_t)actor + ACTOR_OWN_BODY_OFFSET, &body, sizeof(body)) ||
            !memory_try_read((uintptr_t)actor + ACTOR_ANIM_TARGET_OFFSET, &wanted,
                             sizeof(wanted))) {
            fix_state.spoken_id[i] = ANIM_ID_NONE;
            fix_state.held_id[i]   = ANIM_ID_NONE;
            continue;
        }
        if (body == current_speaker) {
            if (fix_state.held_id[i] != ANIM_ID_NONE) {
                /* they are speaking for real again: leave a value behind that can never match a
                 * real animation id, so their own script's next Animation node is guaranteed to
                 * read as a CHANGE and retrigger for real, even on the unlikely chance it reuses
                 * the exact id this fix was holding off. */
                *(int32_t *)((uintptr_t)actor + ACTOR_ANIM_CURRENT_OFFSET) = ANIM_ID_NONE;
                fix_state.held_id[i] = ANIM_ID_NONE;
            }
            fix_state.spoken_id[i] = wanted;
            continue;
        }
        if (fix_state.spoken_id[i] != ANIM_ID_NONE) {
            /* their line ended this frame */
            int32_t health = 0;

            (void)memory_try_read((uintptr_t)actor + ACTOR_HEALTH_OFFSET, &health,
                                  sizeof(health));
            if (wanted == fix_state.spoken_id[i] && wanted != fix_state.scope->rest_anim &&
                health > 0) {
                /* A death cry goes through the same say path as a line, with the die clip
                 * asked for throughout, and the jail prisoner was stood back up out of his
                 * own death by a hold that did not look. A dead actor is never held. */
                begin_hold(i, actor, wanted);
            }
            fix_state.spoken_id[i] = ANIM_ID_NONE;
            continue;
        }
        if (fix_state.held_id[i] == ANIM_ID_NONE) {
            continue;
        }
        if (wanted != fix_state.held_id[i]) {
            /* actor+0x1BC still names the held id, so this request already read as a change to
             * the engine's own debounce this frame and is playing; nothing to hand back. */
            log_info("dialogue_anim_fix: actor %08X's script moved on to animation %d, the hold "
                     "on %d is released", (unsigned)actor, wanted, fix_state.held_id[i]);
            fix_state.held_id[i] = ANIM_ID_NONE;
            continue;
        }
        if (body_current_clip(actor) != fix_state.scope->rest_anim) {
            /* The engine put something else on the body itself, a death the first time this was
             * seen: the die clip raises the complete flag at its marker frame, and the replay
             * below stood him back up in the middle of it. Both cells stay at the held id, so
             * the parked node still does not retrigger, and the clip the engine chose plays out. */
            log_info("dialogue_anim_fix: actor %08X's body is playing clip %d, put there by the "
                     "engine itself, so the hold on %d is released", (unsigned)actor,
                     body_current_clip(actor), fix_state.held_id[i]);
            fix_state.held_id[i] = ANIM_ID_NONE;
            continue;
        }
        /* Their own script node rewrote actor+0x1C0 back to the held id on its OWN visit earlier
         * this frame, and would retrigger it the moment it next sees a mismatch against
         * actor+0x1BC. Keeping that in step satisfies the check WITHOUT calling the trigger, so
         * the rest animation switched to when the hold began keeps playing; when it has played
         * through it is started again, as the engine's own idle mode does with a clip. */
        if (rest_clip_has_finished(actor)) {
            play_rest_clip(actor, fix_state.held_id[i]);
        } else {
            *(int32_t *)((uintptr_t)actor + ACTOR_ANIM_CURRENT_OFFSET) = fix_state.held_id[i];
        }
    }
}

/* The two cells the per-frame pass reads, out of Dialog_SpeakSingle's own operands. The speaker
 * cell is read from both of its operands and the two have to agree; a build in which they did not
 * has matched something that is not this function, and the fix stays off rather than watch a
 * cell that is not the speaker. */
static bool resolve_dialogue_cells(void)
{
    uintptr_t site = sites[SITE_SPEAK_SINGLE].address;
    uint32_t  speaker_read = 0;
    uint32_t  speaker_write = 0;
    uint32_t  active = 0;

    if (site == 0) {
        log_warning("Dialog_SpeakSingle did not resolve, so the speaker and the line-in-progress "
                    "cells are unknown and this fix stays off");
        return false;
    }
    if (!memory_read_u32(site + SPEAK_SINGLE_SPEAKER_READ_OPERAND, &speaker_read) ||
        !memory_read_u32(site + SPEAK_SINGLE_SPEAKER_WRITE_OPERAND, &speaker_write) ||
        !memory_read_u32(site + SPEAK_SINGLE_ACTIVE_OPERAND, &active) ||
        speaker_read != speaker_write ||
        !memory_is_inside_image(speaker_read, sizeof(uint32_t)) ||
        !memory_is_inside_image(active, sizeof(uint32_t))) {
        log_warning("Dialog_SpeakSingle at %08X reads the speaker from %08X and writes it at "
                    "%08X, with the line flag at %08X, which is not the shape expected, so this "
                    "fix stays off", (unsigned)site, (unsigned)speaker_read,
                    (unsigned)speaker_write, (unsigned)active);
        return false;
    }
    fix_state.current_speaker = (const volatile uint32_t *)(uintptr_t)speaker_read;
    fix_state.dialogue_active = (const volatile uint32_t *)(uintptr_t)active;
    log_info("the speaker cell is at %08X and the line-in-progress flag at %08X, both read out "
             "of Dialog_SpeakSingle at %08X", (unsigned)speaker_read, (unsigned)active,
             (unsigned)site);
    return true;
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
    if (!resolve_dialogue_cells()) {
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

    if (fix_state.dialog_box_start.original == NULL &&
        fix_state.dialog_statement.original == NULL) {
        log_warning("neither dialogue trigger hooked, this fix cannot do anything this session");
        return;
    }

    if (!frame_hook_add(on_frame_correct_stale_speakers)) {
        log_warning("the per-frame hook could not be installed, arming will still be tracked but "
                    "nothing will ever be corrected");
        return;
    }

    log_info("armed only in %u scenes, %s and %s: while there, the one animation a named "
             "speaker's line was played with is held off after that line for as long as their "
             "script keeps asking for it",
             (unsigned)SCOPE_COUNT, SCOPES[0].level_file, SCOPES[1].level_file);
}
