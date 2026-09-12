/* dialogue_menu_fix.c: a conversation that has opened is not closed under the player while its
 * own voice line is still playing, or while the player's heading drifts inside it.
 *
 * ============================== What is actually broken =======================================
 *
 * Field report (issue 19): in the final level, the soldier held in the palace who greets the
 * Queen with "you saved us" says it, the subtitle flashes twice, and he says it again before the
 * conversation settles. The same shape shows up on other talking characters now and then: the
 * player is let go after the opening line, the line repeats, and only then is the player held
 * in the conversation.
 *
 * Two clocks disagree about when a line is over, and a script that asks one of them opens a gap
 * the other closes.
 *
 * A branching conversation is a script node, opcode 0x500 (ai_runMenu, 0x004358B0), that the
 * actor's script visits every simulation step for as long as the menu is open. Each visit calls
 * Dialog_SpeakSingle (0x00430D12), which for the same speaker mid line only refreshes the pacing
 * stamp: the line's end is pushed out to now plus the longer of two seconds and the text's own
 * length at a twentieth of a second a character (DLG_LineDuration, 0x0043114F; the engine has
 * no clip length lookup, the timer follows the string). Dialog_Render (0x00430434) closes the
 * conversation, and lets the player go, the frame the world clock passes that stamp while rows
 * are on the screen.
 *
 * The soldier's script, read off the opcode trace, gates the menu on opcode 0x605 "Check For"
 * mode 6, Dialog_ActorTalking (0x0043116F), which with voices on answers "is the bark channel
 * live": the menu is reached only while the actor's own voice is NOT playing. So the tick the
 * menu opens, the voice starts, and for the length of the clip the node is never visited and
 * nothing refreshes the stamp. "Queen Amidala, you saved us!" is short text, a two second stamp,
 * and a voice clip a little over two seconds: the box closes a few frames before the voice ends,
 * the player is released, the voice ends, the gate passes, and the same line starts from the
 * top. Whether the second start plays the voice again depends on Dialog_PlayVoice's debounce
 * (the same line twice in a row is a no-op): a bark from another soldier in between resets it,
 * which is the run that started the line three times. A line whose text outlasts its voice never
 * shows this, so most conversations are fine and a few short greetings are not.
 *
 * There is a second gate in front of the same node, in the dispatcher itself (inside ai_run,
 * 0x00433D0B): once the script reaches the menu, it only runs while enemy_isFacingTarget
 * (0x00435656) with its second argument set says yes, which asks for the player inside the
 * actor's field of view, facing the actor within 45 degrees, within a unit in height and within
 * two units on x and y. The rows lock the input to the menu, so the player cannot walk away,
 * but a heading that drifts out of the 45 degrees while the box is open skips the node all the
 * same; the stamp runs out and the same thing happens: released, and the line starts again once
 * they face him. A run with the voice hold in place still restarted the line once, a second
 * after the voice ended, with the menu node not visited in between; the facing test is the one
 * gate left in front of it.
 *
 * ============================== What this does =================================================
 *
 * Two things, one per gate.
 *
 * Once a frame, at the shared render_frameEnd hook, while a conversation is active with rows on
 * the screen and its speaker's bark channel is still live, Dialog_HoldChannel (0x00430DEC) is
 * called: the engine's own "keep the channel warm while a 0x500 is being held off", which sets
 * the pacing stamp to at least one second from now. That is what ai_runMenu itself does when it
 * refuses a visit, applied to the visits the script never makes. The moment the voice ends the
 * script's gate passes, the node refreshes the stamp itself, and this does nothing. A voice
 * channel that never reports done would hold the box open with the player in it, so the hold
 * is capped at fifteen seconds of one conversation, after which the engine closes it as it
 * always did; a real line is over long before that.
 *
 * And enemy_isFacingTarget is detoured. The original runs first and its answer stands, with one
 * exception: when it says no, the second argument is set, a conversation is active with rows on
 * the screen, and its speaker lock is this actor's own body, the answer is yes. That is the
 * state in which the player is already held in this actor's menu and cannot leave it by any
 * means the test measures. The first visit still needs the real test to pass, a menu with no
 * rows is not touched, another actor's menu is not touched, and the moment the conversation
 * closes, by a choice or by the script, the test is the original again.
 *
 * Every cell read is taken out of the engine's own operands at install, and each one is read
 * from two places that have to agree: the speaker lock and the active flag from
 * Dialog_SpeakSingle, the row count and the bark channel from Dialog_Render against the
 * record's own layout, the pacing stamp from Dialog_HoldChannel's two operands. A build that
 * laid the record out differently switches this off instead of writing the wrong word.
 */
#include "dialogue_menu_fix.h"

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

#define DIALOGUE_MENU_FIX_SECTION "dialogue_menu_fix"

/* --- enemy_isFacingTarget 0x00435656 -----------------------------------------------------------
 *   55 8B EC 83 EC 2C            push ebp / mov ebp,esp / sub esp,0x2C
 *   C7 45 EC 00 00 00 00         mov [ebp-0x14],0            the find mode
 *   8B 45 EC 50                  push [ebp-0x14]
 *   8D 4D F4 51                  lea ecx,[ebp-0xC] / push ecx  the goal
 *   8B 55 08 52                  push [ebp+8]                the actor
 *   E8 <rel32>                   call resolve_target
 *   83 C4 0C 85 C0 75 07         add esp,0xC / test eax,eax / jne
 *   33 C0 E9 2A 02 00 00         xor eax,eax / jmp to the epilogue
 *
 * The prologue is six bytes to a clean boundary. The call's displacement is the one masked
 * stretch; the jump's own displacement is a fixed part of the function and is matched. The
 * dispatcher is its only caller. */
static const uint8_t SIG_IS_FACING_TARGET[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x2C,
    0xC7, 0x45, 0xEC, 0x00, 0x00, 0x00, 0x00,
    0x8B, 0x45, 0xEC, 0x50,
    0x8D, 0x4D, 0xF4, 0x51,
    0x8B, 0x55, 0x08, 0x52,
    0xE8, 0x00, 0x00, 0x00, 0x00,
    0x83, 0xC4, 0x0C, 0x85, 0xC0, 0x75, 0x07,
    0x33, 0xC0, 0xE9, 0x2A, 0x02, 0x00, 0x00
};
static const uint8_t MSK_IS_FACING_TARGET[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
_Static_assert(sizeof SIG_IS_FACING_TARGET == sizeof MSK_IS_FACING_TARGET,
               "the facing test pattern and its mask are different lengths");
#define IS_FACING_TARGET_PROLOGUE 6u

#define ACTOR_OWN_BODY_OFFSET     0x34u   /* actor record -> its own body */

/* --- Dialog_SpeakSingle 0x00430D12, resolved for its operands and never detoured here ---------
 * The same pattern dialogue_anim_fix resolves it by, declared as a detour target because other
 * DLLs detour it and load first, so its prologue is a jump by the time this looks.
 *
 *   +0x1C  8B 0D <speaker>       mov ecx,[g_dlg.pSpeakerLock]
 *   +0x3D  89 15 <speaker>       mov [g_dlg.pSpeakerLock],edx
 *   +0x78  C7 05 <active> 01..   mov [g_dlg.bActive],1 */
static const uint8_t SIG_SPEAK_SINGLE[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x0C,
    0xC7, 0x45, 0xF8, 0x00, 0x00, 0x00, 0x00,
    0x8B, 0x45, 0x10, 0x50,
    0xE8, 0x00, 0x00, 0x00, 0x00,
    0x83, 0xC4, 0x04, 0xD9, 0x5D, 0xFC,
    0x8B, 0x0D, 0x00, 0x00, 0x00, 0x00,
    0x3B, 0x4D, 0x08, 0x75, 0x09,
    0x83, 0x3D, 0x00, 0x00, 0x00, 0x00, 0x00, 0x74, 0x59,
    0x6A, 0x00, 0xE8, 0x00, 0x00, 0x00, 0x00,
    0x83, 0xC4, 0x04, 0x8B, 0x55, 0x08,
    0x89, 0x15, 0x00, 0x00, 0x00, 0x00,
    0x8B, 0x45, 0x14, 0x50, 0x6A, 0x00, 0x8B, 0x4D, 0x10,
    0x51, 0xE8, 0x00, 0x00, 0x00, 0x00,
    0x83, 0xC4, 0x0C, 0x83, 0x7D, 0x0C, 0x00, 0x7C, 0x0C,
    0x8B, 0x55, 0x0C, 0x52, 0xE8, 0x00, 0x00, 0x00, 0x00,
    0x83, 0xC4, 0x04, 0xA1, 0x00, 0x00, 0x00, 0x00,
    0xD9, 0x40, 0x54, 0xD8, 0x45, 0xFC,
    0xD9, 0x1D, 0x00, 0x00, 0x00, 0x00,
    0xC7, 0x05, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00
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
#define SPEAK_SINGLE_PROLOGUE              6u
#define SPEAK_SINGLE_SPEAKER_READ_OPERAND  0x1Eu
#define SPEAK_SINGLE_SPEAKER_WRITE_OPERAND 0x3Fu
#define SPEAK_SINGLE_ACTIVE_OPERAND        0x7Au

/* --- Dialog_Render 0x00430434, the close --------------------------------------------------------
 *   55 8B EC 83 EC 0C                push ebp / mov ebp,esp / sub esp,0xC
 *   83 3D <active> 00 / 75 07        cmp [g_dlg.bActive],0 / jne
 *   33 C0 / E9 54 01 00 00           xor eax,eax / jmp out
 *   83 3D <rows> 00 / 7E 0A          cmp [g_dlg.choiceCount],0 / jle
 *   6A 01 / E8 <rel32> / 83 C4 04    Dialog_EnterInputLock(1)
 *   E8 <rel32>                       candy_debugHook
 *   83 3D <voice option> 00 / 74 25  cmp [g_optPlayDialogVoice],0 / je
 *   83 3D <rows> 00 / 75 1C          cmp [g_dlg.choiceCount],0 / jne
 *   83 3D <bark> 00 / 7D 11          cmp [g_dlg.hVoiceBark],0 / jge
 *
 * The row count appears twice and the two have to agree; the bark channel is read from the
 * last compare. Detoured by nobody, the pattern is the pristine head. */
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
#define RENDER_ACTIVE_OPERAND              0x08u
#define RENDER_ROWS_OPERAND                0x18u
#define RENDER_ROWS_AGAIN_OPERAND          0x39u
#define RENDER_BARK_OPERAND                0x42u

/* --- Dialog_HoldChannel 0x00430DEC, called, never detoured -------------------------------------
 *   55 8B EC 51                      push ebp / mov ebp,esp / push ecx
 *   A1 <level> / D9 40 54            mov eax,[g_level] / fld [eax+0x54]     the world clock
 *   D8 05 <one>                      fadd [1.0f]
 *   D9 55 FC                         fst [ebp-4]
 *   D8 1D <stamp>                    fcomp [g_dlg.lineEndStamp]
 *   DF E0 F6 C4 41 75 09             fnstsw ax / test ah,0x41 / jne
 *   8B 4D FC 89 0D <stamp>           mov ecx,[ebp-4] / mov [g_dlg.lineEndStamp],ecx
 *   8B E5 5D C3                      mov esp,ebp / pop ebp / ret
 *
 * The stamp appears twice and the two have to agree. The whole body is the pattern. */
static const uint8_t SIG_HOLD_CHANNEL[] = {
    0x55, 0x8B, 0xEC, 0x51,
    0xA1, 0x00, 0x00, 0x00, 0x00, 0xD9, 0x40, 0x54,
    0xD8, 0x05, 0x00, 0x00, 0x00, 0x00,
    0xD9, 0x55, 0xFC,
    0xD8, 0x1D, 0x00, 0x00, 0x00, 0x00,
    0xDF, 0xE0, 0xF6, 0xC4, 0x41, 0x75, 0x09,
    0x8B, 0x4D, 0xFC, 0x89, 0x0D, 0x00, 0x00, 0x00, 0x00,
    0x8B, 0xE5, 0x5D, 0xC3
};
static const uint8_t MSK_HOLD_CHANNEL[] = {
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF
};
_Static_assert(sizeof SIG_HOLD_CHANNEL == sizeof MSK_HOLD_CHANNEL,
               "the Dialog_HoldChannel pattern and its mask are different lengths");
#define HOLD_STAMP_COMPARE_OPERAND         0x17u
#define HOLD_STAMP_STORE_OPERAND           0x27u

/* The one conversation record, 0x00882180 in retail; every offset is checked against the
 * operands above before it is used. */
#define DIALOG_ACTIVE_OFFSET               0x04u
#define DIALOG_ROWS_OFFSET                 0x0Cu
#define DIALOG_STAMP_OFFSET                0x24u
#define DIALOG_BARK_OFFSET                 0x40u

#define HOLD_CAP_MS                        15000u

enum {
    SITE_IS_FACING_TARGET,
    SITE_SPEAK_SINGLE,
    SITE_DIALOG_RENDER,
    SITE_HOLD_CHANNEL,
    SITE_COUNT
};

static signature_t sites[SITE_COUNT] = {
    SIGNATURE_ENTRY_DETOUR_MASKED("enemy_isFacingTarget", SIG_IS_FACING_TARGET,
                                  MSK_IS_FACING_TARGET, IS_FACING_TARGET_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR_MASKED("Dialog_SpeakSingle", SIG_SPEAK_SINGLE, MSK_SPEAK_SINGLE,
                                  SPEAK_SINGLE_PROLOGUE),
    SIGNATURE_ENTRY_MASKED("Dialog_Render", SIG_DIALOG_RENDER, MSK_DIALOG_RENDER),
    SIGNATURE_ENTRY_MASKED("Dialog_HoldChannel", SIG_HOLD_CHANNEL, MSK_HOLD_CHANNEL)
};

typedef void    (__cdecl *hold_channel_fn_t)(void);
typedef int32_t (__cdecl *is_facing_target_fn_t)(int32_t actor, int32_t check_player);

static struct {
    bool     installed;
    bool     enabled;

    hold_channel_fn_t hold_channel;
    detour_t          is_facing_target;

    const volatile uint32_t *speaker_lock;
    const volatile int32_t  *active;
    const volatile int32_t  *rows;
    const volatile int32_t  *bark;

    /* one hold at a time: the speaker whose voice is being waited on and since when */
    uint32_t held_speaker;
    DWORD    held_since;
    bool     capped;
    uint32_t holds;

    /* the actor whose menu the facing test is being answered for, and for how many steps */
    int32_t  facing_actor;
    uint32_t facing_steps;
    uint32_t facing_holds;
} fix_state;

static void end_hold(DWORD now)
{
    if (fix_state.held_speaker != 0) {
        ++fix_state.holds;
        log_info("held speaker %08X's conversation open for %.2f s while their voice line was "
                 "still playing%s (hold %u this session)", (unsigned)fix_state.held_speaker,
                 (double)(uint32_t)(now - fix_state.held_since) / 1000.0,
                 fix_state.capped ? ", up to the cap" : "", (unsigned)fix_state.holds);
    }
    fix_state.held_speaker = 0;
    fix_state.capped       = false;
}

/* Once a rendered frame, after Dialog_Render has had its look at the stamp: while the box is
 * open with rows and the speaker's own voice is still playing, the stamp is kept ahead of the
 * clock the way ai_runMenu keeps it when it refuses a visit. */
static void on_frame_hold_open_conversation(void)
{
    DWORD    now = timeGetTime();
    uint32_t speaker = *fix_state.speaker_lock;

    if (*fix_state.active == 0 || *fix_state.rows <= 0 || speaker == 0 || *fix_state.bark < 0) {
        end_hold(now);
        return;
    }
    if (speaker != fix_state.held_speaker) {
        end_hold(now);
        fix_state.held_speaker = speaker;
        fix_state.held_since   = now;
    }
    if ((uint32_t)(now - fix_state.held_since) > HOLD_CAP_MS) {
        fix_state.capped = true;                 /* the engine's own close takes it from here */
        return;
    }
    fix_state.hold_channel();
}

static void end_facing_hold(void)
{
    if (fix_state.facing_actor != 0) {
        ++fix_state.facing_holds;
        log_info("the facing test said no for %u step(s) while actor %08X's menu was open with "
                 "rows on the screen; the menu was kept open (%u this session)",
                 (unsigned)fix_state.facing_steps, (unsigned)fix_state.facing_actor,
                 (unsigned)fix_state.facing_holds);
    }
    fix_state.facing_actor = 0;
    fix_state.facing_steps = 0;
}

/* The original answers first. Its no is turned into a yes only while this actor's own menu is
 * already open with rows, the one state the test cannot legitimately end. */
static int32_t __cdecl hook_is_facing_target(int32_t actor, int32_t check_player)
{
    is_facing_target_fn_t original = (is_facing_target_fn_t)fix_state.is_facing_target.original;
    int32_t               result = original(actor, check_player);
    uint32_t              body = 0;

    if (result != 0 || check_player == 0) {
        end_facing_hold();
        return result;
    }
    if (*fix_state.active == 0 || *fix_state.rows <= 0 ||
        !memory_try_read((uintptr_t)actor + ACTOR_OWN_BODY_OFFSET, &body, sizeof body) ||
        body == 0 || body != *fix_state.speaker_lock) {
        end_facing_hold();
        return 0;
    }
    if (fix_state.facing_actor != actor) {
        end_facing_hold();
        fix_state.facing_actor = actor;
    }
    ++fix_state.facing_steps;
    return 1;
}

/* Every cell out of the engine's own operands, each one confirmed from a second place. */
static bool resolve_cells(void)
{
    uintptr_t speak  = sites[SITE_SPEAK_SINGLE].address;
    uintptr_t render = sites[SITE_DIALOG_RENDER].address;
    uintptr_t hold   = sites[SITE_HOLD_CHANNEL].address;
    uint32_t  speaker_read = 0;
    uint32_t  speaker_write = 0;
    uint32_t  active = 0;
    uint32_t  active_again = 0;
    uint32_t  rows = 0;
    uint32_t  rows_again = 0;
    uint32_t  bark = 0;
    uint32_t  stamp = 0;
    uint32_t  stamp_again = 0;

    if (speak == 0 || render == 0 || hold == 0) {
        log_warning("Dialog_SpeakSingle, Dialog_Render and Dialog_HoldChannel did not all "
                    "resolve, so the conversation record is unknown and this fix stays off");
        return false;
    }
    if (!memory_read_u32(speak + SPEAK_SINGLE_SPEAKER_READ_OPERAND, &speaker_read) ||
        !memory_read_u32(speak + SPEAK_SINGLE_SPEAKER_WRITE_OPERAND, &speaker_write) ||
        !memory_read_u32(speak + SPEAK_SINGLE_ACTIVE_OPERAND, &active) ||
        !memory_read_u32(render + RENDER_ACTIVE_OPERAND, &active_again) ||
        !memory_read_u32(render + RENDER_ROWS_OPERAND, &rows) ||
        !memory_read_u32(render + RENDER_ROWS_AGAIN_OPERAND, &rows_again) ||
        !memory_read_u32(render + RENDER_BARK_OPERAND, &bark) ||
        !memory_read_u32(hold + HOLD_STAMP_COMPARE_OPERAND, &stamp) ||
        !memory_read_u32(hold + HOLD_STAMP_STORE_OPERAND, &stamp_again) ||
        speaker_read != speaker_write ||
        active != active_again || active != speaker_read + DIALOG_ACTIVE_OFFSET ||
        rows != rows_again || rows != speaker_read + DIALOG_ROWS_OFFSET ||
        bark != speaker_read + DIALOG_BARK_OFFSET ||
        stamp != stamp_again || stamp != speaker_read + DIALOG_STAMP_OFFSET ||
        !memory_is_inside_image(speaker_read, DIALOG_BARK_OFFSET + sizeof(uint32_t))) {
        log_warning("the conversation record does not have the shape expected (speaker %08X "
                    "and %08X, active %08X and %08X, rows %08X and %08X, bark %08X, stamp %08X "
                    "and %08X), so this fix stays off", (unsigned)speaker_read,
                    (unsigned)speaker_write, (unsigned)active, (unsigned)active_again,
                    (unsigned)rows, (unsigned)rows_again, (unsigned)bark, (unsigned)stamp,
                    (unsigned)stamp_again);
        return false;
    }
    fix_state.speaker_lock = (const volatile uint32_t *)(uintptr_t)speaker_read;
    fix_state.active       = (const volatile int32_t *)(uintptr_t)active;
    fix_state.rows         = (const volatile int32_t *)(uintptr_t)rows;
    fix_state.bark         = (const volatile int32_t *)(uintptr_t)bark;
    fix_state.hold_channel = (hold_channel_fn_t)hold;
    log_info("the conversation record is at %08X (speaker lock, active flag, row count, pacing "
             "stamp, bark channel), read out of Dialog_SpeakSingle at %08X, Dialog_Render at "
             "%08X and Dialog_HoldChannel at %08X", (unsigned)speaker_read, (unsigned)speak,
             (unsigned)render, (unsigned)hold);
    return true;
}

void dialogue_menu_fix_install(void)
{
    if (fix_state.installed) {
        return;
    }
    fix_state.installed = true;

    log_init("dialogue_menu_fix", false);

    if (!host_image_resolve()) {
        log_error("no 32-bit host image, this fix stays off");
        return;
    }
    fix_state.enabled = ini_read_bool(DIALOGUE_MENU_FIX_SECTION, "Enabled", true);
    if (!fix_state.enabled) {
        log_info("disabled");
        return;
    }

    /* Everything is resolved before anything is written, so a site that is missing switches
     * the whole fix off with nothing live behind it. The detour goes in first: it stands for
     * the life of the process, and the frame hook is the one step that can still refuse. */
    signature_resolve_table(sites, SITE_COUNT);
    if (!resolve_cells()) {
        return;
    }
    if (sites[SITE_IS_FACING_TARGET].address == 0) {
        log_warning("enemy_isFacingTarget did not resolve, this fix stays off");
        return;
    }
    if (!detour_install(&fix_state.is_facing_target, sites[SITE_IS_FACING_TARGET].address,
                        (const void *)hook_is_facing_target, IS_FACING_TARGET_PROLOGUE)) {
        log_warning("the detour on enemy_isFacingTarget failed, this fix stays off");
        return;
    }
    if (!frame_hook_add(on_frame_hold_open_conversation)) {
        log_warning("the per-frame hook could not be installed; the facing test is already "
                    "answered for an open menu and stays so, but a voice longer than its text "
                    "still closes the box");
        return;
    }
    log_info("hooked enemy_isFacingTarget at %08X, and a conversation with rows on the screen "
             "is kept open while its speaker's voice line is still playing, through "
             "Dialog_HoldChannel, for at most %u s of one conversation",
             (unsigned)sites[SITE_IS_FACING_TARGET].address, (unsigned)(HOLD_CAP_MS / 1000u));
}
