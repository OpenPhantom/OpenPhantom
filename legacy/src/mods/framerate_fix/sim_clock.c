/* sim_clock.c: stop the simulation clock pair growing, so the interpolation phase stops precessing.
 *
 * ============================== What is actually broken =======================================
 *
 * The tail of sys_runSubsteps builds the interpolation alpha out of the two simulation clocks and
 * the step:
 *
 *     004757FF  D9 05 94928600   fld  dword [0x00869294]   the simulation target
 *     00475805  D8 25 28878600   fsub dword [0x00868728]   minus the simulation time
 *     0047580B  fadd dword [0x00868714]                    plus the step
 *     00475811  fdiv dword [0x00868714]                    over the step
 *     00475817  fstp dword [0x0086871C]                    the alpha the draw uses
 *
 * so alpha is built from the difference of the pair and from nothing else. Both clocks are float32
 * and both are zeroed when a level opens, at 0x00475621 and 0x0047562B, in the taskman module's
 * message 5 arm. The target then takes an addition of the frame delta every frame, and the rounding
 * of that addition is set by how large the target has grown: one unit in the last place is about
 * 0.95 microseconds ten seconds into a level, 15.3 at two hundred seconds and 61 at six hundred.
 *
 * Modelling those instructions with a perfectly regular frame at a 160 fps cap reproduces what a
 * player sees. At two hundred seconds of level time the alpha ladder drifts by about 1.95e-04 of a
 * substep per frame, spans 0.001 to 0.812 and creeps roughly 0.0117 per sixty frame window, and one
 * window in the run reports thirteen substeps where the others report twelve. That last item is the
 * visible event: a substep boundary that should land on the fifth frame lands on the fourth, those
 * four frames each advance a quarter of a step rather than a fifth, and everything drawn between
 * simulation steps moves about 25 per cent too fast for one frame. At two hundred seconds that
 * recurs about every thousand frames, roughly six seconds; at six hundred seconds it is every three
 * seconds. The drift is deterministic, it changes with the power of two band the clock is in, and
 * it changes sign with it, running near zero at some level times and largest around two to four
 * hundred seconds. A defect whose period changes while the player stands still is a strong
 * fingerprint, and it is the prediction this feature was built against. Those figures come from a
 * model of the arithmetic rather than from a measured session.
 *
 * There is a second consequence, smaller and steadier: the systematic part of the drift is a rate
 * error. At two hundred seconds of level time the accumulator runs about 6.1 microseconds per frame
 * ahead of real time, which is 0.098 per cent fast, or about 0.59 s of extra simulation over ten
 * minutes of play.
 *
 * ============================== Why this pair and not the frame delta =========================
 *
 * The simulation target at [0x00869294] has seven references and the simulation time at
 * [0x00868728] has seven, and every one of them is inside sys_runSubsteps except the two zeroing
 * writes named above. The frame delta cell has seventy references spread over thirteen modules.
 * That is the whole argument for touching the pair: the blast radius is one function plus one reset
 * arm, and the defect lives there rather than in the addend.
 *
 * The subtraction at 0x00475805 is between two float32 values that are within one simulation step
 * of each other, and such a subtraction is exact. So all the precision that is lost is lost in the
 * accumulation, and none of it in the comparison. Taking the same exactly representable amount off
 * both cells therefore leaves the difference bit for bit while collapsing the rounding back to
 * where it was when the level opened.
 *
 * The amount is always a power of two no greater than the live value, which makes it a whole
 * multiple of the unit in the last place of anything at least as large, so both subtractions round
 * nothing at all. The running total of what has been taken away is kept in double, because a
 * float32 total would reintroduce the very defect this removes.
 *
 * ============================== The absolute value is consumed, and how that is paid ==========
 *
 * bapmap_setWorldClock writes the world clock at world+0x54 and derives an integer millisecond tick
 * at world+0x50 from the same argument. It has exactly two callers, at 0x00475794 and 0x004757B4,
 * both inside sys_runSubsteps: the first arm passes the simulation target, the second passes the
 * simulation time plus one step. Detouring the function rather than the two call sites covers both.
 *
 * The world clock is what bapmap_tickMover compares for exact equality against every mover's own
 * copy at mover+0x30, so a rebase that reached it would be felt. The cost is smaller than three
 * earlier versions of this comment claimed, and the bytes are what settle it. The early return at
 * 0x00409191 tests equality only; a clock that moved backwards is not equal, so the body runs. The
 * delta it then computes is negative and is clamped to zero at 0x004091A3, and mover+0x30 is
 * restamped at 0x004091BD whichever way that clamp went. So a backwards world clock would cost one
 * zero length tick per mover and not a freeze, which is what the earlier reading claimed.
 *
 * What it would really cost is every absolute deadline stamped from world+0x54. Those would be
 * extended by the rebase and would never expire. So the offset is added back inside the hook, the
 * world keeps the absolute time it has always had, and no cell outside sys_runSubsteps moves.
 *
 * ============================== Two repairs that look cleaner and are worse ===================
 *
 * Rewriting the alpha cell after the fact. It is tempting because that cell has only four
 * references, two writes and two reads, and both reads happen after sys_runSubsteps in the frame
 * order, so a chained detour could let the original run and then publish a smoother number. Reject
 * it: the visible defect is not that alpha is slightly wrong, it is that a substep boundary landed
 * a frame early, and that decision is made by the accumulator rather than by alpha. Publishing a
 * smooth alpha over a boundary that moved makes the drawn position disagree with the simulated one,
 * which trades a periodic speed pulse for a periodic one frame snap. That is worse and harder to
 * see coming.
 *
 * Feeding the accumulator the cap period instead of the measured frame delta. This was the original
 * proposal and it does not work. Running the model with 0, 0.5 and 5 microseconds of overshoot
 * gives a bit identical ladder, because at two hundred seconds of level time one unit in the last
 * place of the target is already three times larger than the overshoot the change would remove. It
 * also breaks the case where the machine cannot hold the cap, because a fixed period per frame lets
 * the simulation fall behind real time without bound.
 *
 * Computing the frame delta in double is a different repair for a different defect, and the two do
 * not overlap. It changes the addend by an amount comparable to one unit in the last place of the
 * destination, which moves the phase of a deterministic rounding pattern without changing its size.
 * Neither feature is a substitute for the other.
 *
 * ============================== Status ========================================================
 *
 * On by default, RebaseSimClock=1, played and accepted by the maintainer. The arithmetic it stands
 * on is covered by the unit test, which is a different claim again, and the numbers above are a
 * byte census and a model rather than a measurement of a session. Every address quoted is from the
 * retail executable and is an annotation only; the sites below are found by pattern.
 */
#include "sim_clock.h"

#include "common/detour.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/signature.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* --- 0x004757FF  the alpha computation, and where the two clock addresses are read from ------ *
 * Neither address is written down. The pattern is the fld and the fsub that open the block quoted
 * in the header, and the two operands are read out of it: D9 /0 is fld m32fp and D8 /4 is fsub
 * m32fp, so the operands sit at +0x02 and +0x08. */
static const uint8_t SIG_SIM_CLOCK_PAIR[] = {
    0xD9, 0x05, 0x94, 0x92, 0x86, 0x00,
    0xD8, 0x25, 0x28, 0x87, 0x86, 0x00
};
#define OFFSET_SIM_TARGET 0x02u
#define OFFSET_SIM_TIME   0x08u

/* --- 0x0041F0C9  bapmap_setWorldClock, the one consumer of the absolute value ----------------- *
 * The prologue is seven bytes and that is an instruction boundary:
 *
 *     0041F0C9  55 8B EC              push ebp / mov ebp, esp
 *     0041F0CC  83 7D 08 00           cmp  dword [ebp+8], 0     the world pointer
 *     0041F0D0  74 38                 je   past the body        a null world does nothing
 *     ...
 *     0041F0DE  D9 45 0C              fld  dword [ebp+0xC]      the time, argument two
 *     0041F0E1  D8 0D 14824A00        fmul dword [0x004A8214]   1000.0, on the way to the tick
 *
 * The pattern reaches past the prologue into the two loads of the world pointer and the copy of the
 * previous millisecond tick, which is what makes it unique. */
static const uint8_t SIG_SET_WORLD_CLOCK[] = {
    0x55, 0x8B, 0xEC, 0x83, 0x7D, 0x08, 0x00, 0x74, 0x38,
    0x8B, 0x45, 0x08, 0x8B, 0x4D, 0x08, 0x8B, 0x51, 0x50, 0x89, 0x50, 0x58
};
#define SET_WORLD_CLOCK_PROLOGUE 7u

/* Rebase once the clock passes this, and take away the largest power of two that fits. That keeps
 * the live value inside one binade above the threshold, so its rounding stays near a quarter of a
 * microsecond instead of growing with the level. Two seconds is low enough for that rounding to be
 * irrelevant and high enough that a rebase happens about once every two seconds rather than every
 * frame. */
#define REBASE_THRESHOLD_SECONDS 2.0f

typedef void (__cdecl *set_world_clock_fn_t)(void *world, float time);

typedef struct sim_clock_state {
    bool            installed;
    bool            active;
    detour_t        set_world_clock;
    volatile float *target;
    volatile float *time;
    double          offset;          /* everything taken away so far, in double on purpose */
    float           last_seen;       /* to notice the engine zeroing the clocks itself */
    uint32_t        rebases;
    uint32_t        resets;
} sim_clock_state_t;

static sim_clock_state_t sim_state;

/* The world must never see the rebase, so the offset goes back on before the engine writes its
 * clock and its millisecond tick. */
static void __cdecl hook_set_world_clock(void *world, float time)
{
    set_world_clock_fn_t original = (set_world_clock_fn_t)sim_state.set_world_clock.original;

    if (!sim_state.active) {
        original(world, time);
        return;
    }
    original(world, (float)((double)time + sim_state.offset));
}

/* The largest power of two at or below the live value, or 0 while the clock is still small enough
 * that its resolution is not a problem. Being a power of two is what makes both subtractions exact:
 * it is a whole multiple of the unit in the last place of anything at least as large, so nothing
 * rounds and the difference between the two clocks, which is all the interpolation depends on,
 * survives bit for bit. */
double sim_clock_rebase_step(float live)
{
    int    exponent;
    double step;

    if (!(live > REBASE_THRESHOLD_SECONDS)) {
        return 0.0;                              /* also the case just after a level load */
    }
    (void)frexp((double)live, &exponent);
    step = ldexp(1.0, exponent - 1);              /* frexp returns a mantissa in [0.5, 1) */
    if (!(step > 0.0) || !(step <= (double)live)) {
        return 0.0;
    }
    return step;
}

void sim_clock_sample(void)
{
    float  live;
    double step;

    if (!sim_state.active) {
        return;
    }
    live = *sim_state.time;

    /* The engine zeroes both clocks itself when a level opens, at 0x00475621 and 0x0047562B, and an
     * offset carried across that boundary would make the new level's world clock start at the
     * previous level's duration. The reconstructed time would then grow with the session rather
     * than with the level, which is the opposite of what this feature is for. A clock that went
     * backwards is that event, and nothing else here can make it go backwards. */
    if (live < sim_state.last_seen) {
        ++sim_state.resets;
        log_info("the simulation clock went backwards, %.3f s to %.3f s, so a level has opened "
                 "(level boundary %u, %u rebases so far). The %.0f s of accumulated offset is "
                 "dropped and the world clock restarts with the level.",
                 (double)sim_state.last_seen, (double)live, (unsigned)sim_state.resets,
                 (unsigned)sim_state.rebases, sim_state.offset);
        sim_state.offset = 0.0;
    }
    sim_state.last_seen = live;

    step = sim_clock_rebase_step(live);
    if (step == 0.0) {
        return;
    }

    /* Both cells lose the same amount, so their difference, which is all alpha depends on, is
     * untouched. The order does not matter because no substep can run between these two stores:
     * this is called from the once per frame hook, outside sys_runSubsteps. */
    *sim_state.target = (float)((double)*sim_state.target - step);
    *sim_state.time   = (float)((double)live - step);
    sim_state.offset  += step;
    sim_state.last_seen = *sim_state.time;
    ++sim_state.rebases;

    /* One line the first time it happens. Without it the log cannot tell a feature that is working
     * from one that resolved its sites and then never fired, and that is the failure that reads
     * most like success. Afterwards it happens about every two seconds and is not worth a line. */
    if (sim_state.rebases == 1u) {
        log_info("first rebase at %.3f s of level time, %.0f s taken off both clocks and added "
                 "back to the world clock", (double)live, step);
    }
}

void sim_clock_install(bool enabled)
{
    uintptr_t pair;
    uintptr_t site;
    uint32_t  address;

    if (sim_state.installed) {
        return;
    }
    sim_state.installed = true;

    if (!enabled) {
        log_info("RebaseSimClock=0, the interpolation phase keeps precessing as the level clock "
                 "grows");
        return;
    }

    pair = signature_find_unique(SIG_SIM_CLOCK_PAIR, NULL, sizeof(SIG_SIM_CLOCK_PAIR));
    if (pair == 0) {
        log_warning("the simulation clock pair did not resolve, no rebase");
        return;
    }
    if (!memory_read_u32(pair + OFFSET_SIM_TARGET, &address) ||
        !memory_is_inside_image(address, sizeof(float))) {
        log_warning("the simulation target operand reads %08X, outside the image, refused",
                    (unsigned)address);
        return;
    }
    sim_state.target = (volatile float *)(uintptr_t)address;

    if (!memory_read_u32(pair + OFFSET_SIM_TIME, &address) ||
        !memory_is_inside_image(address, sizeof(float))) {
        log_warning("the simulation time operand reads %08X, outside the image, refused",
                    (unsigned)address);
        return;
    }
    sim_state.time = (volatile float *)(uintptr_t)address;

    /* The world clock detour goes on first, and the feature is abandoned if it cannot be placed. A
     * rebase without it would move the time every mover compares for equality and would extend
     * every absolute deadline stamped from the world clock. */
    site = signature_find_detour_target(SIG_SET_WORLD_CLOCK, NULL, sizeof(SIG_SET_WORLD_CLOCK),
                                        SET_WORLD_CLOCK_PROLOGUE);
    if (site == 0) {
        log_warning("bapmap_setWorldClock did not resolve, so no rebase is performed. Moving the "
                    "clocks without restoring the world time would extend every absolute deadline "
                    "stamped from it, and those would never expire");
        return;
    }
    if (!detour_install(&sim_state.set_world_clock, site, (const void *)hook_set_world_clock,
                        SET_WORLD_CLOCK_PROLOGUE)) {
        log_warning("bapmap_setWorldClock could not be detoured, no rebase");
        return;
    }

    sim_state.active = true;
    log_info("simulation clock rebasing active (target %08X, time %08X, world clock restored at "
             "%08X). Alpha depends on the difference of the pair, which a shared subtraction "
             "leaves bit for bit, and the world keeps the absolute time every mover compares "
             "against.",
             (unsigned)(uintptr_t)sim_state.target, (unsigned)(uintptr_t)sim_state.time,
             (unsigned)site);
}
