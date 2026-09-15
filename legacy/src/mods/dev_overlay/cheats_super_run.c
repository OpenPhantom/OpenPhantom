/* cheats_super_run.c: super run, a higher cap on how fast the player runs.
 *
 * The player's speed is not written by the run key. Plr_StandClipSelect (0x0044CDF0 region)
 * decides each stand tick which clip plays and what the two speed caps should be, and hands them
 * to Plr_RampSpeedCaps (0x0044D05C), which walks the live caps toward them by 0.25 a tick; the
 * integrator then approaches the forward cap at the move drive's rate and multiplies the result
 * by the frame's dt and the turn penalty. The caps are pushed as immediates, 3.5 for a run and
 * 2.0 for a walk, both in world units a second:
 *
 *   0044CDF7  68 00 00 80 3E        push 0.25f            the ramp step
 *   0044CDFC  68 00 00 80 3F        push 1.0f             the backward cap
 *   0044CE01  68 00 00 60 40        push 3.5f             the forward cap, the run
 *   0044CE06  E8 51 02 00 00        call Plr_RampSpeedCaps
 *   0044CE0B  83 C4 0C              add esp,0xC
 *
 * The walk site at 0x0044CF17 has the same shape with 2.0f in the third push, and the two swing
 * lunges reach the same routine with a table value; none of those is touched. The pattern below
 * carries the 3.5 itself, so it can only match the run site, and it matched exactly once in the
 * retail image and nowhere else does a push of 3.5f occur.
 *
 * The cheat is one float: while it is on the immediate holds 3.5 times SuperRunScale, and the
 * engine's own ramp carries the live cap up to it over a few ticks and back down when the cheat
 * goes off. Nothing else changes. The run clip still plays at its authored rate, so above about
 * 1.5x the feet visibly slide; the turn penalty still applies; the wall probe is swept per
 * substep, so at 4x, 0.44 units a step, nothing is passed through. Written through the patch
 * journal with the original remembered, so off puts back the byte the game shipped and a second
 * on is computed from that and never from whatever is there now.
 */
#include "cheats_openphantom.h"
#include "cheats_internal.h"

#include "common/ini.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/patch.h"
#include "common/signature.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define SUPER_RUN_SECTION "dev_overlay"

/* The run site, 0x0044CDF7: the three pushes, the call and the caller's clean-up. The call's
 * displacement is masked; the three immediates are the evidence. */
static const uint8_t SIG_RUN_CAPS[] = {
    0x68, 0x00, 0x00, 0x80, 0x3E,             /* push 0.25f                   */
    0x68, 0x00, 0x00, 0x80, 0x3F,             /* push 1.0f                    */
    0x68, 0x00, 0x00, 0x60, 0x40,             /* push 3.5f                    */
    0xE8, 0x00, 0x00, 0x00, 0x00,             /* call Plr_RampSpeedCaps       */
    0x83, 0xC4, 0x0C                          /* add esp,0xC                  */
};
static const uint8_t MSK_RUN_CAPS[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF
};
_Static_assert(sizeof SIG_RUN_CAPS == sizeof MSK_RUN_CAPS, "mask length");
#define RUN_CAP_OPERAND_OFFSET 11u              /* the imm32 of the third push */
#define RUN_CAP_SHIPPED        3.5f

/* SuperRunScale: 2.0 is twice the run, brisk and still playable; the band's ends are in
 * cheats_openphantom.h with the panel's row, which drags between them: 4.0 is the most the wall
 * probe was reasoned about above, and 1.0 is a row that does nothing, so the floor sits just
 * above it. */
#define SUPER_RUN_SCALE_DEFAULT 2.0f
#define SUPER_RUN_SCALE_KEY     "SuperRunScale"
#define SUPER_RUN_DECIMALS      2

static float clamp_scale(float scale)
{
    if (!(scale >= SUPER_RUN_SCALE_MIN)) {
        return SUPER_RUN_SCALE_MIN;   /* the NOT catches a value that is not a number */
    }
    if (scale > SUPER_RUN_SCALE_MAX) {
        return SUPER_RUN_SCALE_MAX;
    }
    return scale;
}

static struct {
    uintptr_t       operand;     /* the imm32 of the run cap push, 0 until resolved */
    float           scale;
    patch_journal_t journal;     /* holds the shipped 3.5 while the cheat is on */
} super_run;

void install_super_run(void)
{
    uintptr_t site;
    uint32_t  shipped_bits;
    float     shipped;

    super_run.scale = clamp_scale(ini_read_float(SUPER_RUN_SECTION, SUPER_RUN_SCALE_KEY,
                                                 SUPER_RUN_SCALE_DEFAULT));

    site = signature_find_unique(SIG_RUN_CAPS, MSK_RUN_CAPS, sizeof SIG_RUN_CAPS);
    if (site == 0) {
        log_warning("the run speed caps did not resolve, so super run is offered as unavailable");
        return;
    }
    if (!memory_read_u32(site + RUN_CAP_OPERAND_OFFSET, &shipped_bits)) {
        log_warning("the run cap at %08X could not be read, so super run is offered as "
                    "unavailable", (unsigned)(site + RUN_CAP_OPERAND_OFFSET));
        return;
    }
    memcpy(&shipped, &shipped_bits, sizeof shipped);
    if (shipped != RUN_CAP_SHIPPED) {
        log_warning("the run cap at %08X reads %.2f, not the 3.5 the game shipped, so super run "
                    "is offered as unavailable", (unsigned)(site + RUN_CAP_OPERAND_OFFSET),
                    (double)shipped);
        return;
    }
    super_run.operand = site + RUN_CAP_OPERAND_OFFSET;
    own_state.cheats[CHEATS_OWN_SUPER_RUN].available = true;
    log_info("super run: the run cap's 3.5 at %08X becomes %.2f while the cheat is on "
             "(SuperRunScale=%.2f), pushed to the engine's own ramp each stand tick",
             (unsigned)super_run.operand, (double)(RUN_CAP_SHIPPED * super_run.scale),
             (double)super_run.scale);
}

/* On writes the raised cap over the shipped one through the journal; off undoes the journal,
 * which is the one write the engine's own bytes come back from. A write that does not land
 * leaves the row off, and the log says so. */
bool cheats_super_run_apply(bool on)
{
    float raised = RUN_CAP_SHIPPED * super_run.scale;

    if (super_run.operand == 0) {
        return false;
    }
    if (!on) {
        patch_journal_undo(&super_run.journal);
        patch_journal_reset(&super_run.journal);
        return true;
    }
    /* A second on, from the setter with the cheat already on, first puts the shipped bytes back
     * so the journal's before-bytes are always the game's own and never a raised value. */
    patch_journal_undo(&super_run.journal);
    patch_journal_reset(&super_run.journal);
    if (patch_journal_write_bytes(&super_run.journal, super_run.operand, &raised,
                                  sizeof raised) != PATCH_RESULT_OK) {
        log_warning("super run: the run cap at %08X could not be written, the cheat stays off",
                    (unsigned)super_run.operand);
        return false;
    }
    return true;
}

float cheats_openphantom_super_run_scale(void)
{
    return super_run.scale;
}

bool cheats_openphantom_super_run_set_scale(float scale)
{
    super_run.scale = clamp_scale(scale);
    /* Re-applied before the file is written, so the speed follows the hand even on a machine
     * whose settings file cannot be written; the file is the memory, the bytes are the cheat. */
    if (own_state.cheats[CHEATS_OWN_SUPER_RUN].on) {
        (void)cheats_super_run_apply(true);
    }
    return ini_write_float(SUPER_RUN_SECTION, SUPER_RUN_SCALE_KEY, super_run.scale,
                           SUPER_RUN_DECIMALS);
}
