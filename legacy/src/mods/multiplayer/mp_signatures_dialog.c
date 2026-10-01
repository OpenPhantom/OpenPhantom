/* mp_signatures_dialog.c: the three entry points of the one global conversation object.
 *
 * One subject: what is being SAID. The speak entry is the only door a spoken line takes, the
 * restart latch is what makes a line said again on the far machine play its voice instead of only
 * refreshing the subtitle, and the append is where the answer menu's cells are read from. The
 * enumeration is mp_dialog_site_t in mp_signatures.h, which also says why it is an enumeration of
 * its own rather than a block of mp_site_t.
 *
 * The first two patterns name the SAME two cells, 0x00882180 the speaker lock and 0x008821BC the
 * restart latch, and each names both. That is a two-witness pair rather than two guesses: two
 * patterns in two functions agreeing about one address says more than one pattern naming it twice.
 *
 * All three are declared as detour targets even though only the speak entry is hulled, so the two
 * stage resolver survives another module writing a branch over a head. It does: `diagnostics`
 * hulls the speak entry as well when it is switched on.
 */
#include "mp_signatures_dialog.h"

#include "mp_signatures.h"

#include "common/logging.h"
#include "common/signature.h"

#include <stddef.h>
#include <stdint.h>

/* ==============================================================================================
 * Dialog_SpeakSingle 0x00430D12. Thirty-eight bytes:
 *
 *   55 8B EC 83 EC 0C            push ebp; mov ebp,esp; sub esp,0xC
 *   C7 45 F8 00000000            mov [ebp-8],0                     ; started = 0
 *   8B 45 10 50                  mov eax,[ebp+0x10]; push eax      ; the line id
 *   E8 rel32                     call DLG_LineDuration             ; masked, a distance
 *   83 C4 04                     add esp,4
 *   D9 5D FC                     fstp [ebp-4]                      ; the duration
 *   8B 0D abs32                  mov ecx,[g_dlg.pSpeakerLock]      ; masked
 *   3B 4D 08                     cmp ecx,[ebp+8]                   ; against the caller's speaker
 *   75 09                        jne
 *   83 3D abs32 00               cmp [g_dlg.bForceRestart],0       ; masked
 *
 * The `50` after the line id load is what pins the argument ORDER: the duration is taken of the
 * THIRD argument, which is the only reading under which this is "say line N".
 * ============================================================================================ */
static const uint8_t SIG_DLG_SPEAK_SINGLE[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x0C, 0xC7, 0x45, 0xF8, 0x00, 0x00, 0x00,
    0x00, 0x8B, 0x45, 0x10, 0x50, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x83, 0xC4,
    0x04, 0xD9, 0x5D, 0xFC, 0x8B, 0x0D, 0x00, 0x00, 0x00, 0x00, 0x3B, 0x4D,
    0x08, 0x75
};
static const uint8_t MSK_DLG_SPEAK_SINGLE[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF,
    0xFF, 0xFF
};
/* push ebp; mov ebp,esp; sub esp,0xC */
#define DLG_SPEAK_SINGLE_PROLOGUE 6u

/* Dialog_ForceRestart 0x00430E69, twenty-five bytes end to end, and the pattern stops there:
 *
 *   55 8B EC                     push ebp; mov ebp,esp
 *   A1 abs32                     mov eax,[g_dlg.pSpeakerLock]      ; masked
 *   3B 45 08                     cmp eax,[ebp+8]
 *   75 0A                        jne over
 *   C7 05 abs32 01000000         mov [g_dlg.bForceRestart],1       ; masked, the 1 is required
 *   5D C3                        pop ebp; ret
 *
 * It used to carry three more bytes, the head of Dialog_Close behind it, so that a whole
 * short function was not the whole match. That is the dependency the rule at the head of
 * mp_signatures.c forbids, and it cost this patch: camera_handback_fix hulls Dialog_Close,
 * writes its branch over exactly those three bytes and loads first, so the pattern matched
 * nothing and the site switched itself off in the field. The twenty five bytes of the
 * function itself still match once on every image the offline check knows.
 *
 * The `01 00 00 00` is unmasked deliberately: it is the whole meaning of the function, and a build
 * that stored anything else there would be arming something this module has not read. */
static const uint8_t SIG_DLG_FORCE_RESTART[] = {
    0x55, 0x8B, 0xEC, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x3B, 0x45, 0x08, 0x75,
    0x0A, 0xC7, 0x05, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x5D,
    0xC3
};
static const uint8_t MSK_DLG_FORCE_RESTART[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF
};

/* Dialog_AddChoice 0x00430E1B, forty bytes, and it is the richest of the three: three absolute
 * operands and two required immediates that are the function's whole meaning.
 *
 *   55 8B EC 51                  push ebp; mov ebp,esp; push ecx
 *   83 3D abs32 00               cmp [g_dlg.bActive],0        ; masked, operand at +6
 *   75 02 EB 3B                  jne +2 / jmp out
 *   83 3D abs32 09               cmp [g_dlg.choiceCount],9    ; masked, operand at +17
 *   7D 28                        jge out                      ; the 9 is REQUIRED: it is the cap
 *   A1 abs32                     mov eax,[g_dlg.choiceCount]  ; masked, operand at +25
 *   C1 E0 04                     shl eax,4                    ; the 4 is REQUIRED: the row stride
 *   05 abs32                     add eax,g_dlg.choice         ; masked, operand at +33
 *   89 45 FC                     mov [ebp-4],eax
 *
 * The row count is named TWICE inside one pattern, at +17 and +25, and both are read and required
 * to be equal. The cap and the stride are left unmasked because they are not addresses: a build
 * that allowed ten rows or laid them out at a different stride is one this module has not read,
 * and it should fail to resolve rather than walk past the end of an array.
 *
 * Nothing hulls this one any more; its three operands are what the host watches its own answer
 * through. It stays declared as a detour target so the two stage rule holds if another module
 * writes a branch over its head. */
static const uint8_t SIG_DLG_ADD_CHOICE[] = {
    0x55, 0x8B, 0xEC, 0x51, 0x83, 0x3D, 0x00, 0x00, 0x00, 0x00, 0x00, 0x75,
    0x02, 0xEB, 0x3B, 0x83, 0x3D, 0x00, 0x00, 0x00, 0x00, 0x09, 0x7D, 0x28,
    0xA1, 0x00, 0x00, 0x00, 0x00, 0xC1, 0xE0, 0x04, 0x05, 0x00, 0x00, 0x00,
    0x00, 0x89, 0x45, 0xFC
};
static const uint8_t MSK_DLG_ADD_CHOICE[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00,
    0x00, 0xFF, 0xFF, 0xFF
};
/* ELEVEN, NOT SIX. `push ebp; mov ebp,esp; push ecx` is four bytes, one short of the branch that
 * replaces the prologue, so the prologue has to reach into the compare that follows, and the
 * compare is SEVEN bytes, `83 3D 84 21 88 00 00`, not two. Six once copied `83 3D` alone into a
 * trampoline, where the jump back that the detour appends became the compare's own operand, and
 * the bytes after it were executed as code: EIP outside every module the first time that trampoline
 * ran. The operand comment above says "operand at +6", and +6 is where the operand BEGINS, not
 * where the instruction ends. That eleven ends on an instruction boundary rests on the count here:
 * four bytes of frame and seven of compare. */
#define DLG_ADD_CHOICE_PROLOGUE 11u

static signature_t dialog_sites[MP_DIALOG_SITE_COUNT] = {
    SIGNATURE_ENTRY_DETOUR_MASKED("dialog_speak_single", SIG_DLG_SPEAK_SINGLE,
                                  MSK_DLG_SPEAK_SINGLE, DLG_SPEAK_SINGLE_PROLOGUE),
    /* The prologue is the three bytes of `push ebp; mov ebp,esp` plus the load that follows, which
     * is five: enough for a branch, and it is declared even though nothing hulls this one. */
    SIGNATURE_ENTRY_DETOUR_MASKED("dialog_force_restart", SIG_DLG_FORCE_RESTART,
                                  MSK_DLG_FORCE_RESTART, 8u),
    SIGNATURE_ENTRY_DETOUR_MASKED("dialog_add_choice", SIG_DLG_ADD_CHOICE, MSK_DLG_ADD_CHOICE,
                                  DLG_ADD_CHOICE_PROLOGUE)
};

size_t mp_signatures_dialog_resolve(void)
{
    size_t resolved;

    /* The shared resolver carries the two stage rule and logs one line per site, naming the branch
     * it took. Repeating it here would be a second implementation of the one thing in this project
     * that must not have two. */
    resolved = signature_resolve_table(dialog_sites, MP_DIALOG_SITE_COUNT);
    log_info("%u of %u conversation site(s) resolved", (unsigned)resolved,
             (unsigned)MP_DIALOG_SITE_COUNT);
    return resolved;
}

uintptr_t mp_signatures_dialog_address(mp_dialog_site_t site)
{
    if ((size_t)site >= (size_t)MP_DIALOG_SITE_COUNT) {
        return 0u;
    }
    return dialog_sites[site].address;
}

size_t mp_signatures_dialog_prologue(mp_dialog_site_t site)
{
    if ((size_t)site >= (size_t)MP_DIALOG_SITE_COUNT) {
        return 0u;
    }
    return dialog_sites[site].detour_prologue;
}

const signature_t *mp_signatures_dialog_site(mp_dialog_site_t site)
{
    if ((size_t)site >= (size_t)MP_DIALOG_SITE_COUNT) {
        return NULL;
    }
    return &dialog_sites[site];
}

const signature_t *mp_signatures_dialog_sites(size_t *count)
{
    if (count != NULL) {
        *count = (size_t)MP_DIALOG_SITE_COUNT;
    }
    return dialog_sites;
}
