/* view_lead.c: the mouse on the render clock, the body on the simulation clock, and the one term
 * that keeps the two from being drawn twice.
 *
 * SIZE NOTE: a little over eight hundred lines, and about half of that is the measurement rather
 * than the mechanism. That is deliberate and it is the lesson of the first field round: the
 * mechanism was right, the instrument reported the one speed nobody was complaining about, and a
 * whole test round was spent finding that out. The seam is the census and the dump together: they
 * share nothing with the accumulator but the drawn yaw and one flag, and lifting the pair out
 * would leave about four hundred lines here. It is named rather than taken, because the two halves
 * are read together every time either of them is touched. Whoever takes it should move that whole
 * responsibility and not trim the byte evidence above to get under a limit.
 *
 * The design and the arithmetic are in the header. What is written here is what a maintainer has to
 * hold on to while changing it:
 *
 *   * Nothing is written to the engine from this file. It answers a number of degrees; the caller
 *     adds them to the yaw the camera update has just composed. The engine recomposes that yaw from
 *     its own cells on every frame, so a frame this feature sits out leaves nothing behind and
 *     there is no restore path that can be got wrong.
 *   * The bank is a ledger. Every degree that arrives is either still banked, handed to a
 *     simulation step, or dropped at a release, and the census reports the residual. That is the
 *     one failure mode that would otherwise stay invisible: motion invented or lost between the
 *     per-frame drain and the per-step handover does not look like anything on screen until much
 *     later, when the camera and the body have quietly parted company.
 *   * The camera may lead the body and it may not be leashed to it. The gap is bounded by one step
 *     of hand movement and closes by itself, because the body is handed exactly the same degrees
 *     one step later. Clamping the camera against the body would make the camera a function of the
 *     body again, which is the whole of what this removes.
 *
 * ==============================================================================================
 * The frame, in program order
 *
 * Read out of sys_frame at 0x0043E9F2 rather than assumed. Addresses throughout are retail
 * WMAIN.EXE, 829,952 bytes, ImageBase 0x400000.
 *
 *     0043E9F6  call 0x00475B75      the frame limiter
 *     0043E9FB  mov  eax, [0x008A011C]
 *     0043EA01  call 0x004181B9      bapview_buildCamView          the eye is composed
 *     0043EA09  call 0x00401D30      bapdraw_setFrameState
 *     0043EA0E  call 0x0046C108      render_frameBegin
 *     0043EA27  call 0x004756FC      sys_runSubsteps               the 32 Hz simulation runs
 *     0043EA2F  call 0x0043F57A
 *     0043EA48  call 0x0046F4A9      broadcast message 0x0D        the poll and updateCam
 *     0043EA50  call 0x00402155      bapdraw_flushQueue
 *     0043EA60  call 0x0046F4A9      broadcast message 0x11
 *     0043EAA6  call 0x0046C139      render_frameEnd               the bank is filled here
 *
 * Three consequences the design rests on:
 *
 *   * the eye is composed from `view->euler.y` before the substeps, so the yaw a frame draws was
 *     computed at the end of the previous frame. That one frame of latency is the engine's and is
 *     neither added to nor removed by this feature;
 *   * the per-frame drain sits between the step that consumed and the frame end that refills, so
 *     the two drains cannot be reordered against each other by an edit of ours;
 *   * the bank is filled after the camera update, so a sample collected at the end of frame N is
 *     first drawn on frame N+1 whatever else changes. That is the engine's own layout rather than
 *     a choice made here.
 *
 * ==============================================================================================
 * Why writing the drawn yaw after the original is safe and stateless
 *
 * The tail of bapview_updateCam after it stores the yaw, in full:
 *
 *     00418FAD  cmp  [0x005BB4E4], 0     a countdown, decremented if non-zero
 *     00418FC5  cmp  [0x005BB538], 0     the region record
 *     00418FD6  and  eax, 0x10           its "fast lag" bit
 *     00418FDD  mov  [0x008A0104], 0x3ECCCCCD   two lag constants, 0.4 and 0.4
 *     00418FE7  mov  [0x008A0100], 0x3ECCCCCD
 *     00418FF3  mov  [0x008A0104], 0x3F6AAAAB   or 0.917 and 0.96
 *     00418FFD  mov  [0x008A0100], 0x3F75C28F
 *     00419007  xor  eax, eax
 *     0041900C  ret
 *
 * Nothing there reads the yaw back. A census of the whole image says the same thing more strongly:
 * bapview_updateCam, 0x00418544 to 0x0041900C, contains exactly one dword store to `[reg + 0x38]`,
 * the one at 0x00418FAA. Across the whole of .text there are 64 dword stores to `[reg + 0x38]`, of
 * which the only candidates that could be this record are the camera creation path at 0x00417F45
 * (`mov [eax+0x38], 0`, a reset) and the debug free flight camera at 0x00419136 to 0x00419380,
 * which is not on the shipped path. The pointer to the record, [0x008A011C], has 97 references
 * image wide and the overwhelming majority of them are inside bapview.
 *
 * So the engine recomposes the field from its own cells on the next frame. A frame this feature
 * sits out is a frame drawn exactly as the engine composed it, and switching the key off leaves
 * nothing behind.
 */
#include "view_lead.h"

#include "free_look_math.h"
#include "raw_mouse.h"

#include "common/ini.h"
#include "common/logging.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define INPUT_SECTION "enhanced_input"

/* The most the camera may be shown ahead of the body, in degrees.
 *
 * It is a plausibility bolt rather than a taste value, and it bounds a fault rather than a hand. At
 * the fastest sweep a person makes, about 3000 degrees per second, one simulation step of banked
 * movement is 94 degrees, so nothing a hand does can reach this. What can is a bank that keeps
 * filling while no step consumes it, which is a state the collector's own dormancy guard already
 * ends after an eighth of a second. The body still receives every banked degree; only the drawn
 * lead is bounded, and it recovers on the next step. */
#define MAX_LEAD_DEGREES 120.0f

/* The ledger residual that is worth a line. Degrees are banked and handed over in single precision
 * and counted here in double, so a small difference is rounding rather than a leak. A tenth of a
 * degree over a whole measurement window is far below anything a player could see and far above
 * what rounding can produce. */
#define LEDGER_TOLERANCE_DEGREES 0.1

/* ==============================================================================================
 * The measurement
 *
 * What is measured is the second difference of the drawn yaw: the change, from one rendered frame
 * to the next, of how far the camera turned. A camera advancing by an equal amount every frame
 * measures zero however fast it is turning, and every kink the eye reads is one of these.
 *
 * The headline number is deliberately not the median on its own, because the median alone can move
 * in either direction here and would then be quoted as a verdict. The plan this was built to asked
 * for the median of the absolute second difference normalised by the mean absolute first
 * difference, and predicted it would fall to about a fifth. That prediction does not survive
 * contact with the arithmetic. Removing the simulation grid also removes the averaging it was
 * performing: today the engine draws one step's worth of hand movement as five equal frames, which
 * hides the count quantisation inside each frame, and afterwards each frame is drawn as the hand
 * actually moved it. For a hand at a constant speed the per-frame numbers therefore get rougher,
 * not smoother, while the thing a player sees gets better.
 *
 * So the verdict is the split. Every frame is tagged with whether a simulation step ran on it, and
 * the roughness is averaged over the two groups separately. The defect is that the whole of the
 * change lands on the one frame in five that ran a step, so today the ratio between the two groups
 * should be large, and if the feature does what it claims it should fall toward one. That ratio is
 * free of hand speed, free of frame rate and free of the sensitivity setting, which is what makes
 * two runs in one sitting comparable at all. The median and the mean are printed beside it because
 * the plan asked for them.
 * ============================================================================================ */
#define CENSUS_MIN_FRAMES     600u      /* about four seconds at a high frame rate */
#define CENSUS_MIN_DEGREES    60.0      /* enough turn that the roughness means something */
#define CENSUS_MAX_FRAMES   20000u      /* give up and report what there is */
#define CENSUS_MAX_SAMPLES    256u      /* kept sorted, so the median is free at the end */

/* THE SPEED BANDS, and they exist because the first version of this instrument measured the one
 * regime nobody is complaining about.
 *
 * The report it produced covered 0.9 degrees per frame, about 80 degrees a second, which is a
 * comfortable look around. The complaint is that slow movement is fine, medium movement breaks the
 * illusion and fast movement reads as skipped frames, so a single average over a window that
 * happened to be slow answers a question nobody asked and answers it reassuringly.
 *
 * The bands are absolute degrees per rendered frame rather than fractions of the mean, because the
 * eye reads an absolute jump on the screen: at a 90 degree field of view across 1920 pixels, one
 * degree is 21 pixels wherever it sits in the sweep. The boundaries are one and four degrees per
 * frame, which at 90 frames a second is 90 and 360 degrees a second.
 *
 * A SKIP is counted separately, because the roughness average cannot express it: it is a frame
 * whose drawn step changed by more than the whole of the previous step, which is what "it stopped
 * and then jumped" is. A perfectly even sweep produces none at any speed. */
#define CENSUS_BANDS            3u
#define CENSUS_BAND_SLOW_MAX    1.0f
#define CENSUS_BAND_MEDIUM_MAX  4.0f
#define CENSUS_MIN_MOVING_FRAMES 200u   /* medium and fast together, before the report is worth it */

/* Enough to carry a whole fast sweep and its run-out, few enough that it cannot fill a disk. */
#define MAX_DUMP_FRAMES 400

typedef struct yaw_census {
    bool     open;
    bool     reported;
    uint32_t frames;                /* frames with a usable first difference */
    uint32_t chain;                 /* consecutive usable frames, for the second difference */
    float    previous_yaw;
    float    previous_first;
    double   first_sum;             /* the drawn turn, unsigned, summed */
    double   second_sum;
    uint32_t second_frames;
    double   second_on_step;        /* the same, split by whether a step ran on that frame */
    uint32_t frames_on_step;
    double   second_off_step;
    uint32_t frames_off_step;
    float    second[CENSUS_MAX_SAMPLES];
    uint32_t kept;

    /* Indexed by speed band: slow, medium, fast. `skips` counts the frames whose drawn step
     * changed by more than the whole of the previous step. */
    uint32_t band_frames[CENSUS_BANDS];
    double   band_first[CENSUS_BANDS];
    double   band_second[CENSUS_BANDS];
    uint32_t band_skips[CENSUS_BANDS];

    /* The denominator of the whole analysis, and it used to be printed by the other census, which
     * the per-frame path stopped running. How many device reports land in a rendered frame decides
     * how much of the frame-to-frame variation is the sampling boundary rather than the hand: at
     * six reports a frame, one report either way is already a sixth of the movement. */
    uint32_t frames_seen;
    unsigned packet_baseline;
} yaw_census_t;

typedef struct view_lead_state {
    bool  enabled;                  /* the key */
    bool  active;                   /* the key and every dependency */
    bool  installed;

    /* Degrees banked by the rendered frames since the last simulation step took them. */
    float pending;
    /* What the last step was handed, which is what the engine's interpolation is paying out. */
    float substep_mouse;

    /* The ledger. Doubles because they run for a whole session and a float would stop counting. */
    double banked_total;
    double handed_total;
    double dropped_total;
    bool   warned_ledger;
    bool   warned_lead_bound;

    /* Set by the step handover, consumed by the census sample: which frames ran a step. */
    bool  substep_since_sample;

    /* THE BURST, and it exists because an average cannot answer "did that frame draw nothing".
     *
     * Every aggregate above says how rough a band was; none of them can tell hand acceleration
     * from lumpy delivery, and those two want opposite repairs. A run of consecutive frames
     * through one fast sweep can, because the two look nothing alike written out: a hand that
     * accelerates produces a rising sequence, and a delivery that stalls produces a step of
     * almost nothing followed by a double. Armed by NewMouseInputDump, fires once. */
    int   dump_frames;
    int   dump_left;
    bool  dump_armed;
    uint32_t dump_index;
    float last_frame_degrees;   /* what the bank handed this frame */
    float last_lead;
    float last_alpha;

    yaw_census_t census;
} view_lead_state_t;

static view_lead_state_t lead_state;

/* ==============================================================================================
 * The pure half
 * ============================================================================================ */
float view_lead_degrees(float pending_degrees, float substep_mouse_degrees, float alpha)
{
    float unpaid;
    float lead;

    if (!isfinite(pending_degrees) || !isfinite(substep_mouse_degrees) || !isfinite(alpha)) {
        return 0.0f;
    }

    /* The engine's weight is built from two clocks and can leave [0, 1] when a frame is long enough
     * to owe more time than one step covers. Beyond 1 the engine is extrapolating, and the part of
     * the last step's turn it has left to pay out is then nothing rather than a negative amount. */
    if (alpha < 0.0f) {
        alpha = 0.0f;
    }
    if (alpha > 1.0f) {
        alpha = 1.0f;
    }

    unpaid = substep_mouse_degrees * (1.0f - alpha);
    lead   = pending_degrees + unpaid;

    if (!isfinite(lead)) {
        return 0.0f;
    }
    if (lead >  MAX_LEAD_DEGREES) {
        return  MAX_LEAD_DEGREES;
    }
    if (lead < -MAX_LEAD_DEGREES) {
        return -MAX_LEAD_DEGREES;
    }
    return lead;
}

/* ==============================================================================================
 * Configuration and installation
 * ============================================================================================ */
void view_lead_load_config(void)
{
    /* OFF BY DEFAULT. It changes where the camera points within a simulation step, which is a
     * change in feel rather than the repair of a fault, and the whole point of the measurement
     * below is that the two settings can be compared in one sitting. */
    lead_state.enabled = ini_read_bool(INPUT_SECTION, "NewMouseInput", false);

    /* Measurement. The first fast sweep after this many frames of the game running writes one line
     * per rendered frame, then never again this session. */
    lead_state.dump_frames = ini_read_int(INPUT_SECTION, "NewMouseInputDump", 0);
    if (lead_state.dump_frames < 0) {
        lead_state.dump_frames = 0;
    }
    if (lead_state.dump_frames > MAX_DUMP_FRAMES) {
        lead_state.dump_frames = MAX_DUMP_FRAMES;
    }
    lead_state.dump_armed = (lead_state.dump_frames > 0);
}

bool view_lead_is_enabled(void)
{
    return lead_state.enabled;
}

bool view_lead_is_active(void)
{
    return lead_state.active;
}

/* --- the two yaw arms, and why the plain one has to be forced -------------------------------- *
 * bapview_updateCam picks between two ways of arriving at the drawn yaw, on whether the camera's
 * own turn rate is exactly zero:
 *
 *     00418EBC  mov     [0x005BB4FC], 0
 *     00418EC6  fld     [0x008A006C]         camTurn
 *     00418ECC  fcomp   [0x004A8168]         0.0
 *     00418ED2  fnstsw  ax
 *     00418ED4  test    ah, 0x40             C3, set when equal
 *     00418ED7  jne     0x00418F86           equal, take arm B
 *
 *     arm A   00418EDD  mov  eax, [0x008A011C]
 *             00418EE2  fld  [eax + 0x38]    the previous drawn yaw, read back
 *             00418EE5  fstp [ebp - 0x20]
 *             00418EE8  fld  [ebp - 0x28]    the interpolated heading
 *             00418EEB  fadd [0x005BB534]    plus the yaw offset
 *             00418EF5  call 0x004940AB      wrapped, into [ebp-0x70]
 *             00418F6D  fld  [0x004A8170]    0.5
 *             00418F73  fmul [ebp - 0x20]
 *             00418F76  fld  [0x004A8170]    0.5
 *             00418F7C  fmul [ebp - 0x70]
 *             00418F7F  faddp st(1)          half the old drawn yaw, half the new target
 *
 *     arm B   00418F86  fld  [ebp - 0x28]
 *             00418F89  fadd [0x005BB534]
 *
 *     both    00418F92  mov  ecx, [ebp - 0x20]
 *             00418F96  call 0x004940AB      wrap360
 *             00418FA1  mov  edx, [0x008A011C]
 *             00418FAA  mov  [edx + 0x38], eax       view->euler.y
 *
 * 0x00418EE2 is the reason this feature refuses to arm unless the plain arm is forced. Arm A reads
 * back the yaw that was drawn on the previous frame, so a lead added to that yaw would be fed into
 * the next frame's own answer and would compound rather than being recomputed from nothing. Arm B
 * is heading plus offset and carries no history at all, which is what makes a per-frame lead a
 * quantity that can simply be added and then forgotten. */
void view_lead_install(bool bank_is_live, bool yaw_arm_is_forced)
{
    if (lead_state.installed) {
        return;
    }
    lead_state.installed = true;

    if (!lead_state.enabled) {
        log_info("NewMouseInput=0, the view angle stays a simulation quantity: it advances once per "
                 "simulation step and the camera interpolates between the last two of them, which "
                 "is the engine's own behaviour. A held direction key asks for the same number of "
                 "degrees every step, so that interpolation draws a straight line and the key is "
                 "smooth; the mouse asks for a different number every step, so above about 32 "
                 "frames per second several frames in a row are drawn at one turn rate and then it "
                 "changes. NewMouseInput=1 puts the mouse on the render clock instead.");
        return;
    }

    /* Each refusal is named rather than counted, because the two have completely different repairs
     * and a player who is told only that the feature is off cannot tell which he is looking at. */
    if (!bank_is_live) {
        log_warning("NewMouseInput=1 needs the mouse to be collected once per rendered frame and it "
                    "is not, so it stays OFF. Without the bank the axis is read live and is not "
                    "cleared by reading it, so a second reader would be handed the same movement "
                    "and the view would turn twice as far. MouseAccumulate=1 is what turns the "
                    "collector on; the log above says why it is not running.");
        return;
    }
    if (!yaw_arm_is_forced) {
        log_warning("NewMouseInput=1 needs the camera's plain yaw arm and it is not being forced, "
                    "so it stays OFF. The engine's other arm eases the camera toward its target and "
                    "reads back the yaw that was drawn on the previous frame, so the extra turn "
                    "shown on one frame would be fed into the next one and would compound instead "
                    "of being recomputed. MouseLookRigidCamera=1 forces the plain arm; if that is "
                    "already 1, the cell that selects the arm did not resolve in this build and the "
                    "line above says so.");
        return;
    }

    lead_state.active = true;
    log_info("NewMouseInput=1: the mouse now advances the DRAWN view angle once per rendered frame "
             "and the simulation step is handed the total afterwards as a displacement, so the "
             "camera turns by what the hand did on that frame instead of by a fifth of what it did "
             "over the last simulation step. The body still owns the heading and receives every "
             "degree; it simply receives them one step later, so the camera leads it by up to one "
             "step of hand movement where it used to lag by the same amount. A held direction key "
             "is deliberately untouched: its turn is already constant, so the engine's own "
             "interpolation is exactly right for it. Nothing is written to the engine, the drawn "
             "angle is adjusted after the camera update has composed it, so switching this off is "
             "immediate and leaves nothing behind.");
}

/* ==============================================================================================
 * The two clocks
 * ============================================================================================ */
void view_lead_add_frame(float degrees)
{
    float banked;

    if (!lead_state.active || !isfinite(degrees)) {
        return;
    }

    banked = lead_state.pending + degrees;
    if (!isfinite(banked)) {
        return;
    }
    lead_state.pending           = banked;
    lead_state.banked_total     += (double)degrees;
    lead_state.last_frame_degrees = degrees;
}

/* The argument is what the engine's own per-step drain has already handed over, and the total of
 * the two is recorded as this step's mouse turn. That total is what the interpolation is about to
 * pay out, and both drains feed it: at the instant the steering phase runs, the bank holds the
 * sample collected at the end of the previous frame, which the per-frame drain has not seen yet,
 * so the split between the two is not fixed and must not be assumed. The arithmetic is indifferent
 * to where the split falls as long as the recorded total is the sum, which is the property to hold
 * on to before changing either call. */
float view_lead_take_substep(float degrees_taken_from_the_bank)
{
    float banked = lead_state.pending;

    /* Set whether or not the feature is on. The measurement needs to know which frames ran a step
     * in both settings, or the two runs it exists to compare are not comparable. */
    lead_state.substep_since_sample = true;

    if (!lead_state.active) {
        return 0.0f;
    }

    lead_state.pending       = 0.0f;
    lead_state.handed_total += (double)banked;

    /* What the interpolation is about to pay out is the WHOLE mouse turn this step applies, which
     * is what the engine's own per-step drain handed over as well as what was banked here. Leaving
     * the first term out would leave that part of the turn to be drawn twice. */
    lead_state.substep_mouse = isfinite(degrees_taken_from_the_bank)
                                   ? (banked + degrees_taken_from_the_bank)
                                   : banked;
    return banked;
}

void view_lead_release(void)
{
    lead_state.dropped_total += (double)lead_state.pending;
    lead_state.pending       = 0.0f;
    lead_state.substep_mouse = 0.0f;

    /* The camera has been taken by something else, so the yaw is about to jump for a reason that
     * has nothing to do with the hand. Measuring across that would report the jump as roughness. */
    lead_state.census.chain = 0u;
}

float view_lead_current(float alpha)
{
    float lead;

    if (!lead_state.active) {
        return 0.0f;
    }

    lead = view_lead_degrees(lead_state.pending, lead_state.substep_mouse, alpha);
    lead_state.last_lead  = lead;
    lead_state.last_alpha = alpha;
    if (!lead_state.warned_lead_bound &&
        (lead >= MAX_LEAD_DEGREES || lead <= -MAX_LEAD_DEGREES)) {
        lead_state.warned_lead_bound = true;
        log_warning("the camera was asked to lead the body by more than %.0f degrees and was held "
                    "there. That is more than the fastest hand produces in one simulation step, so "
                    "it means movement was banked while no step consumed it. The body still gets "
                    "every degree and the camera catches up on the next step; this is reported "
                    "once.", (double)MAX_LEAD_DEGREES);
    }
    return lead;
}

/* ==============================================================================================
 * The measurement
 * ============================================================================================ */
static void census_keep(yaw_census_t *census, float second)
{
    /* Inserted in order rather than sorted at the end, which costs a few dozen moves per sample and
     * nothing where it would be noticed. Once the array is full the SMALLEST value is dropped,
     * which biases the median upward and therefore cannot flatter the result. */
    if (census->kept < CENSUS_MAX_SAMPLES) {
        uint32_t slot = census->kept;

        ++census->kept;
        while (slot > 0u && census->second[slot - 1u] > second) {
            census->second[slot] = census->second[slot - 1u];
            --slot;
        }
        census->second[slot] = second;
        return;
    }
    if (second > census->second[0]) {
        uint32_t slot = 0u;

        while (slot + 1u < CENSUS_MAX_SAMPLES && census->second[slot + 1u] < second) {
            census->second[slot] = census->second[slot + 1u];
            ++slot;
        }
        census->second[slot] = second;
    }
}

/* One line per rendered frame through one fast sweep, so that the sequence can be read rather than
 * averaged. What each column separates:
 *
 *   step    what the camera actually turned this frame. A near zero in the middle of a sweep is a
 *           SKIP, and no hand produces one.
 *   banked  what the mouse handed over for this frame. Zero here with a normal step means the
 *           drawing lost it; zero in both means the device delivered nothing that frame, which is
 *           the report boundary rather than anything this DLL does.
 *   lead    what was added to the engine's own composition, and alpha is what the engine was
 *           interpolating with. The two together say whether a jump came from our term or its.
 *   step?   whether a simulation step ran on that frame. */
static void dump_frame(float first, bool on_step)
{
    float magnitude = (first < 0.0f) ? -first : first;

    if (lead_state.dump_left <= 0) {
        if (!lead_state.dump_armed || magnitude < CENSUS_BAND_MEDIUM_MAX) {
            return;
        }
        lead_state.dump_armed = false;
        lead_state.dump_left  = lead_state.dump_frames;
        lead_state.dump_index = 0u;
        log_info("view lead dump: %d frames from the first fast sweep, one line each. step is what "
                 "the camera turned, banked is what the mouse handed over for that frame.",
                 lead_state.dump_frames);
    }

    log_info("  f%-3u step %+8.3f  banked %+8.3f  lead %+8.3f  alpha %5.3f  step %d",
             lead_state.dump_index, (double)first, (double)lead_state.last_frame_degrees,
             (double)lead_state.last_lead, (double)lead_state.last_alpha, on_step ? 1 : 0);
    ++lead_state.dump_index;
    --lead_state.dump_left;
}

static unsigned band_of(float first)
{
    float magnitude = (first < 0.0f) ? -first : first;

    if (magnitude < CENSUS_BAND_SLOW_MAX) {
        return 0u;
    }
    if (magnitude < CENSUS_BAND_MEDIUM_MAX) {
        return 1u;
    }
    return 2u;
}

/* One line per band, printed only for the bands that were actually reached. A band with no frames
 * in it means the hand never went that fast during the window, which is a fact about the test and
 * not about the build, so it says so rather than printing zeroes. */
static void census_report_bands(void)
{
    static const char *const names[CENSUS_BANDS] = {
        "slow   (under 1 deg/frame)",
        "medium (1 to 4 deg/frame) ",
        "fast   (over 4 deg/frame) "
    };
    yaw_census_t *census = &lead_state.census;
    unsigned      band;

    for (band = 0u; band < CENSUS_BANDS; ++band) {
        double speed;
        double roughness = 0.0;

        if (census->band_frames[band] == 0u) {
            log_info("  %s no frames, the hand never went that fast in this window",
                     names[band]);
            continue;
        }
        speed = census->band_first[band] / (double)census->band_frames[band];
        if (speed > 0.0) {
            roughness = 100.0 * (census->band_second[band] / (double)census->band_frames[band])
                        / speed;
        }
        log_info("  %s %6u frames, mean turn %6.3f deg/frame, roughness %5.1f percent, %u skips "
                 "(%.1f percent of them)",
                 names[band], census->band_frames[band], speed, roughness,
                 census->band_skips[band],
                 100.0 * (double)census->band_skips[band] / (double)census->band_frames[band]);
    }
    log_info("  a SKIP is a frame whose drawn step changed by more than the whole of the previous "
             "step, which is what \"it stopped and then jumped\" is. An even sweep produces none at "
             "any speed. Roughness is the mean change of the drawn step against the mean step, so "
             "it is comparable between bands and between runs; the absolute jump a band implies is "
             "its roughness times its mean turn, and at a 90 degree field of view one degree is "
             "about 21 pixels on a 1920 wide screen.");
}

static void census_report(void)
{
    yaw_census_t *census = &lead_state.census;
    double        speed;
    double        mean = 0.0;
    double        median = 0.0;
    double        on_step = 0.0;
    double        off_step = 0.0;
    double        residual;

    census->reported = true;

    speed = census->first_sum / (double)census->frames;
    if (speed > 0.0) {
        if (census->second_frames > 0u) {
            mean = 100.0 * (census->second_sum / (double)census->second_frames) / speed;
        }
        if (census->kept > 0u) {
            median = 100.0 * (double)census->second[census->kept / 2u] / speed;
        }
        if (census->frames_on_step > 0u) {
            on_step = 100.0 * (census->second_on_step / (double)census->frames_on_step) / speed;
        }
        if (census->frames_off_step > 0u) {
            off_step = 100.0 * (census->second_off_step / (double)census->frames_off_step) / speed;
        }
    }

    log_info("drawn view turn measured over %u rendered frames, NewMouseInput=%d: roughness on the "
             "frames that ran a simulation step %.1f percent, on the frames that did not %.1f, "
             "ratio %.1f. Median over all frames %.1f, mean %.1f. Mean turn %.3f degrees per frame "
             "over %u frames that ran a step and %u that did not. The ratio is the answer: the "
             "defect is that the whole of the change in turn rate lands on the one frame in five "
             "that ran a step, so a large ratio is the stepping and a ratio near one is its "
             "absence. The median and the mean are printed beside it because they are what an "
             "earlier plan asked for, and they can move either way: drawing each frame as the hand "
             "actually moved it also stops the simulation step from averaging the count "
             "quantisation away.",
             census->frames, lead_state.active ? 1 : 0, on_step, off_step,
             (off_step > 0.0) ? (on_step / off_step) : 0.0, median, mean, speed,
             census->frames_on_step, census->frames_off_step);

    census_report_bands();

    {
        unsigned packets = raw_mouse_packet_count() - census->packet_baseline;

        if (packets > 0u && census->frames_seen > 0u) {
            log_info("  the device reported %u times over %u rendered frames, %.2f per frame. One "
                     "report either way is %.1f percent of a frame's movement whatever the hand is "
                     "doing, so that number is the floor the bands above can reach without "
                     "smoothing. Raising the mouse's own polling rate lowers it AND shortens the "
                     "filter, which measures itself in report intervals.",
                     packets, census->frames_seen,
                     (double)packets / (double)census->frames_seen,
                     100.0 * (double)census->frames_seen / (double)packets);
        } else {
            log_info("  no raw device reports arrived in this window, so the mouse was read through "
                     "the engine's own device and the report timing is not recoverable");
        }
    }

    residual = lead_state.banked_total - lead_state.handed_total - lead_state.dropped_total -
               (double)lead_state.pending;
    if (residual < 0.0) {
        residual = -residual;
    }
    if (!lead_state.active) {
        return;
    }
    log_info("the mouse ledger over the same window: %.1f degrees banked by the rendered frames, "
             "%.1f handed to the simulation, %.1f dropped where the camera was not ours, %.2f still "
             "banked. The residual is %.4f degrees and it must be rounding: anything else means "
             "movement was invented or lost between the two clocks, which is the one fault here "
             "that nothing on screen would show until the camera and the body had quietly parted "
             "company.",
             lead_state.banked_total, lead_state.handed_total, lead_state.dropped_total,
             (double)lead_state.pending, residual);

    if (residual > LEDGER_TOLERANCE_DEGREES && !lead_state.warned_ledger) {
        lead_state.warned_ledger = true;
        log_warning("the mouse ledger does not balance: %.4f degrees are unaccounted for. Every "
                    "degree banked by a rendered frame must end up handed to a simulation step, "
                    "dropped at a release, or still banked.", residual);
    }
}

void view_lead_census_sample(float drawn_yaw)
{
    yaw_census_t *census = &lead_state.census;
    bool          on_step = lead_state.substep_since_sample;
    float         first;
    float         second;

    lead_state.substep_since_sample = false;
    ++census->frames_seen;

    if (census->reported || !isfinite(drawn_yaw)) {
        census->chain = 0u;
        return;
    }
    if (census->chain == 0u) {
        census->chain        = 1u;
        census->previous_yaw = drawn_yaw;
        return;
    }

    first = free_look_wrap180(drawn_yaw - census->previous_yaw);
    census->previous_yaw = drawn_yaw;
    if (!isfinite(first)) {
        census->chain = 0u;
        return;
    }

    /* A still camera measures the still camera. Frames in which nothing turned carry no slope and
     * would only dilute the average with zeroes. */
    if (first == 0.0f) {
        census->chain          = 1u;
        census->previous_first = 0.0f;
        return;
    }

    dump_frame(first, on_step);

    if (!census->open) {
        census->open = true;

        /* The ledger is zeroed with the window it is reported against, so that "over the same
         * window" on the report line is true rather than nearly true. Everything banked before the
         * first frame of real movement belongs to a menu or a cutscene and would only make the
         * residual look like a leak.
         *
         * The opening balance is not zero, and getting that wrong is what the ledger caught itself
         * doing in the field: whatever is in the bank at this instant has not been handed over yet
         * and will be, so a window that starts counting from zero reports exactly that much as
         * invented movement. It is booked as already banked. */
        lead_state.banked_total  = (double)lead_state.pending;
        lead_state.handed_total  = 0.0;
        lead_state.dropped_total = 0.0;
        census->packet_baseline  = raw_mouse_packet_count();

        /* The frame counter is zeroed with the packet baseline, because the report divides one by
         * the other and they have to span the same window. It used to run from the very first call
         * while the baseline was taken here, so the printed reports-per-frame was the packets of
         * this window over the frames of a longer one, and it read low by whatever the menu and the
         * loading screens had contributed. The same log block then printed two different frame
         * counts for one measurement, which is how the error was found; a rate that reads low
         * looks exactly like a device delivering fewer reports than it should. */
        census->frames_seen = 0u;
    }
    ++census->frames;
    census->first_sum += (first < 0.0f) ? -(double)first : (double)first;

    if (census->chain >= 2u) {
        unsigned band = band_of(first);
        float    previous = (census->previous_first < 0.0f) ? -census->previous_first
                                                            : census->previous_first;

        second = first - census->previous_first;
        if (second < 0.0f) {
            second = -second;
        }

        /* Banded on THIS frame's speed, because that is the speed the player was moving at when he
         * saw the kink. */
        ++census->band_frames[band];
        census->band_first[band]  += (first < 0.0f) ? -(double)first : (double)first;
        census->band_second[band] += (double)second;
        if (second >= previous) {
            ++census->band_skips[band];
        }

        census->second_sum += (double)second;
        ++census->second_frames;
        if (on_step) {
            census->second_on_step += (double)second;
            ++census->frames_on_step;
        } else {
            census->second_off_step += (double)second;
            ++census->frames_off_step;
        }
        census_keep(census, second);
    }

    census->previous_first = first;
    if (census->chain < 2u) {
        ++census->chain;
    }

    /* The window has to CONTAIN the regime being asked about. An earlier version closed after 600
     * frames of whatever happened to be going on, and reported a comfortable look around as though
     * it were the answer. So it waits for enough frames of real movement, and gives up at the frame
     * cap so that a session which never sweeps still reports something. */
    if ((census->frames >= CENSUS_MIN_FRAMES && census->first_sum >= CENSUS_MIN_DEGREES &&
         (census->band_frames[1] + census->band_frames[2]) >= CENSUS_MIN_MOVING_FRAMES) ||
        census->frames >= CENSUS_MAX_FRAMES) {
        census_report();
    }
}
