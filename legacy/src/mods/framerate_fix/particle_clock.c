/* particle_clock.c: evaluate particle motion at the instant the frame is actually drawing.
 *
 * ============================== Why this is exact, and not an estimate =========================
 *
 * A particle's position is never accumulated. It is evaluated from four constants written once
 * when the particle was emitted, as a closed form parabola in the time since its birth. The
 * integrator emitter_integrateParticles 0x0042161C, read end to end:
 *
 *     00421622  ecx = emitter[0xD4]      -> vec3 out[]   the drawn position
 *     00421631  eax = emitter[0xD8]      -> vec3 p0[]    birth position
 *     0042163D  edx = emitter[0xDC]      -> vec3 v[]     velocity
 *     00421649  ecx = emitter[0xE0]      -> vec3 a[]     acceleration
 *               for i in 0 .. emitter[0x18]-1:
 *     0042169A    t0 = ((float *)emitter[0xE4])[i]        the birth time
 *     004216A9    if t0 == 0.0            -> the slot is free, next
 *     004216BB    if (clockB - t0) > emitter[0x60]        the lifetime, so retire it
 *     004216FE    u  = t0 - clockB                        a negative age
 *     0042170A    u2 = u * u
 *                 out[i] = v[i]*u + p0[i] + a[i]*u2*0.5   with 0.5f read from [0x4A8244]
 *
 * So the position is a pure function of one float. Handing that subtraction a retarded clock does
 * not estimate anything: it evaluates the same parabola at the instant actually being drawn, which
 * is the exact answer rather than a blend between two samples. Nothing needs a previous position
 * and nothing needs a side table, which is also why this module holds no per level state.
 *
 * Only the read at 0x00421701 is repointed. The retirement test three instructions earlier reads
 * the same cell through a different instruction, 0x004216BB, and it keeps the true clock, as do
 * emission, the dormancy stamp, the wake up rebase and the prewarm. The repoint is per operand,
 * which is the whole reason a render setting cannot leak into the simulation here.
 *
 * A sign convention that trips up anyone writing the formula from memory, and it is the engine's
 * own. `u` is `t0 - clock`, which for a live particle is non positive, so the linear term carries
 * the particle along MINUS its stored velocity while the quadratic term, which squares u, carries
 * it along plus its stored acceleration. Four things confirm that rather than one: the cull radius
 * at 0x0042044F computes (|emitter[0x58]| + emitter[0x68]/2) * emitter[0x60], which is the maximum
 * speed times the lifetime and so pins the speed formula; the template default direction is
 * (0, 0, -1), which with the negation launches upward in this Z up world; the built in Bubbles
 * template pairs that direction with a positive acceleration in z, so bubbles rise; and the built
 * in GreenGlobe uses a vector the parser calls "braking", which only deserves that name if the
 * physical velocity is -v[i]. None of it changes the smoothing, which is monotone in u either way.
 *
 * ============================== What this does not fix ========================================
 *
 * Emission is untouched, and that matters. A trail emitter attached to a moving node samples that
 * node's stepped world matrix when it emits, so the particles of a fast trail are laid down in
 * clumps one substep apart. Today the whole trail steps together and the clumping is hidden by
 * the shared stutter. Once the particles glide and the emission points do not, the clumps separate
 * and a fast trail looks beaded in a way it does not today. That is predicted here so it is not
 * read as a regression when somebody sees it. Repairing it means interpolating the attach point at
 * emission time, which is a write on the simulation side, so it is a different change.
 *
 * The sprite cel is untouched deliberately. The renderer forms age over lifetime at 0x0042140C and
 * turns it into an integer cel index, so a smoother age moves that index on a handful of frames
 * per particle and can never make the cel animation look smoother. Saying so is more useful than
 * counting it as a benefit.
 */
#include "particle_clock.h"

#include "common/logging.h"
#include "common/memory.h"
#include "common/patch.h"
#include "common/signature.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* --- 0x004216FE  the one clock read that produces a coordinate ------------------------------- *
 *   D9 45 E0           fld  [ebp-0x20]        the birth time
 *   D8 25 48 FA 6B 00  fsub [0x006BFA48]      clockB, its operand at +0x05
 *   D9 55 FC           fst  [ebp-4]
 *   D8 4D FC           fmul [ebp-4]           u*u
 *
 * A four byte scan of the whole image for clockB [0x006BFA48] returns sixteen references, every
 * one of them inside the particle module: two writes, the initialisation to 1000.0 at 0x0041FD54
 * and the per substep advance at 0x00421848, and fourteen reads. Exactly one of the fourteen
 * produces a coordinate and it is this one. The other thirteen are the particle retirement test,
 * the advance's own read, the emitter lifetime and emission duration tests, the emission gate, the
 * rotating nozzle, the dormancy stamp, the wake up rebase, two bind stamps, an emitter age and two
 * prewarm backdates. That census is why this is one operand rather than a rewrite.
 *
 * The obvious pattern is a trap. The bare encoding of `fsub dword [0x006BFA48]`, which is
 * D8 25 48 FA 6B 00, occurs twice in the image. The second is 0x00421DAD inside the rotating
 * nozzle, which has exactly one caller, 0x00421A76 at the head of the multiplier loop in
 * emitter_emit, and therefore runs on the substep clock. Repointing that one would make an
 * emitter's own emission frame depend on the render clock, which is a simulation write on a render
 * path. The three leading bytes D9 45 E0 are what separate the two and they must not be trimmed.
 * For the same reason `fld dword [0x006BFA48]`, D9 05 48 FA 6B 00, occurs seven times and is
 * useless on its own.                                                                           */
static const uint8_t SIG_PARTICLE_POSITION_CLOCK[] = {
    0xD9, 0x45, 0xE0,
    0xD8, 0x25, 0x48, 0xFA, 0x6B, 0x00,
    0xD9, 0x55, 0xFC,
    0xD8, 0x4D, 0xFC
};
#define OFFSET_PARTICLE_CLOCK_OPERAND 0x05u

/* --- 0x00422408  the single call to emitter_drawParticles ------------------------------------ *
 *   85 C0           test eax,eax
 *   75 10           jne  ...
 *   8D 4D C8        lea  ecx,[ebp-0x38]
 *   51              push ecx
 *   8B 55 BC        mov  edx,[ebp-0x44]
 *   52              push edx
 *   E8 ...          call 0x00420DB2            the E8 at +0x0C
 *
 * A rel32 sweep of the whole image gives emitter_drawParticles 0x00420DB2 exactly one caller, this
 * one inside emitter_renderAll, which in turn has exactly one caller of its own. The retarded
 * clock is published here rather than once per frame because the birth clamp below is per emitter.
 *
 * The pattern deliberately ends at the E8, and the redirect's own write begins one byte later at
 * 0x00422409. That is zero bytes of slack and it is the design rather than luck: a pattern that
 * ran on past the rel32 would contain the bytes it goes on to overwrite, so a second run, or
 * another DLL searching the same site, would find nothing and switch itself off.                */
static const uint8_t SIG_PARTICLE_DRAW_CALL[] = {
    0x85, 0xC0, 0x75, 0x10, 0x8D, 0x4D, 0xC8, 0x51, 0x8B, 0x55, 0xBC, 0x52, 0xE8
};
#define OFFSET_PARTICLE_DRAW_CALL 0x0Cu

/* --- 0x00420DB2  the emitter pool, read out of the callee's own arithmetic ------------------- *
 *   00420DBB  cmp  [ebp+8], 0
 *   00420DC1  cmp  [ebp+8], 0x40
 *   00420DD1  imul eax, eax, 0x124      the stride, at +0x21
 *   00420DD7  add  eax, 0x6BFA50        the pool base, at +0x26
 *
 * The draw takes an emitter index, not a pointer, and getting that wrong killed the clamp in
 * silence. An earlier version of this file declared the argument as a pointer and then asked
 * whether a small integer was readable memory, which it never is, so the birth clamp never ran on
 * a single emitter and the spawn flick it exists to remove was fully present. The design had the
 * prototype right and the code diverged from it, which is the one class of defect a design review
 * cannot catch.
 *
 * Sixty four slots of stride 0x124 based at 0x006BFA50 is confirmed four independent ways: this
 * pair, the same pair in the release path at 0x004214D9 and in the tick at 0x00421883, the literal
 * seeded into the walk cursor at 0x0042214E with `add ecx,0x124` per step, and the level teardown's
 * `rep stosd` of 0x1240 dwords from 0x006BFA50, which is 0x4900 bytes, exactly 64 times 0x124.
 * Every bound check in the module is `cmp index, 0x40`. The stride and the base are read out of
 * those two instructions rather than written down here.
 *
 * The callee returns a value in EAX, zero on the out of range path at 0x00420DC7 and a real one
 * through 0x0042147D, and the thunk below is declared void. That is safe only because this single
 * caller overwrites EAX at 0x00422410 before reading anything, which was checked rather than
 * assumed. The rule in this directory is the opposite one, that a hook returns what the original
 * returned, and it exists because dropping a return value is a silent data fault. This is an
 * exception a caller census earned, not a licence to drop the next one.                          */
static const uint8_t SIG_EMITTER_POOL[] = {
    0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x58, 0x01, 0x00, 0x00, 0x83, 0x7D, 0x08,
    0x00, 0x7C, 0x06, 0x83, 0x7D, 0x08, 0x40, 0x7C, 0x07, 0x33, 0xC0, 0xE9,
    0xAF, 0x06, 0x00, 0x00, 0x8B, 0x45, 0x08, 0x69, 0xC0, 0x24, 0x01, 0x00,
    0x00, 0x05, 0x50, 0xFA, 0x6B, 0x00
};
#define OFFSET_EMITTER_STRIDE 0x21u
#define OFFSET_EMITTER_BASE   0x26u

/* The emitter's newest birth time, and the reason the publish is per emitter.
 *
 * The retarded clock can precede a just emitted particle's birth by up to one substep, and the
 * closed form does not clamp: a negative age places the particle backwards along its own velocity,
 * which is a brief flick at the spawn point. emitter+0x10C is the birth time of the newest
 * particle and it is always at or below the true clock, so clamping to it removes the flick by
 * construction rather than by a tolerance. The relation is not assumed. The emission gate at
 * 0x0042194D permits a burst only when emitter[0x10C] + emitter[0x54] is at or below the clock,
 * the advance at 0x00421D84 stores precisely that value once per burst outside the multiplier
 * loop, and all four writers of the field preserve it. Emission is monotone, so every older
 * particle has an earlier birth time and one float compare per emitter per frame covers the pool.
 *
 * A recycled emitter slot cannot smear either, twice over. There is no previous position to
 * inherit, because the record is birth position, velocity, acceleration and birth time and there
 * is no previous field anywhere in the subsystem, and the allocator zeroes what it hands out, so a
 * freshly bound emitter's birth array is all zeros, which is exactly the free marker the
 * integrator tests at 0x004216A9.                                                                */
#define EMITTER_NEWEST_BIRTH 0x10C

/* The substep period, and the one constant in this file with no anchor in the image.
 *
 * The rate selector inside sys_runSubsteps chooses between 1/32 and 1/64 on the "60fps" cheat flag
 * [0x00882294]. A four byte scan of the whole image for that flag returns exactly two references,
 * both of them the `83 3D` compare form, one in sys_runSubsteps and one in sys_waitForFrame. There
 * is no write, no push of the address and no table entry, and the cell sits past the end of the
 * raw data image, so it is zero initialised and the 1/64 arm cannot be selected from inside the
 * shipped image. PinSimulationRate holds that arm at 1/32 as well.
 *
 * If the period were ever really 1/64, the retardation computed here would be doubled and the
 * clamp above would fire on essentially every newly emitted particle: particles would still move
 * smoothly, would sit up to 15.6 ms of world time behind their emitter, and nothing would be
 * written into the simulation. The invariant at risk is ours rather than the engine's, which is
 * why it is named here instead of being left to be found.                                        */
#define SUBSTEP_SECONDS 0.03125f

/* --- 0x00411063  the substep alpha, and the engine's guard against a frozen simulation ------- *
 *   A1 1C878600           mov eax, [0x0086871C]      the alpha, its operand at +0x01
 *   89 85 E0FBFFFF        mov [ebp-0x420], eax
 *   83 3D E8AC5B00 00     cmp [0x005BACE8], 0        the gate, its operand at +0x0D
 *   74 0C                 je  ...
 *   8B 0D E4AC5B00        mov ecx, [0x005BACE4]      the latch, its operand at +0x16
 *
 * The engine loads the global alpha, tests a gate and uses a second cell instead when the gate is
 * set. That second cell freezes while the gate is armed, which is the engine's own defence against
 * drawing an interpolated pose while the simulation is not advancing. Reading `gate ? latch :
 * global` here matches it exactly, and it creates no ordering against the object draw that owns
 * those cells: in the ungated case this reads the global and never touches the latch, and in the
 * gated case the latch is frozen by construction, so it does not matter which of the two draws
 * runs first in a given frame.
 *
 * All three addresses come out of one matched pattern, and the pattern is resolved here rather
 * than borrowed from the mover feature on purpose. A module that only works while a different
 * switch is on is a dependency nobody can see in the ini.                                       */
static const uint8_t SIG_ALPHA_LATCH[] = {
    0xA1, 0x1C, 0x87, 0x86, 0x00,
    0x89, 0x85, 0xE0, 0xFB, 0xFF, 0xFF,
    0x83, 0x3D, 0xE8, 0xAC, 0x5B, 0x00, 0x00,
    0x74, 0x0C,
    0x8B, 0x0D, 0xE4, 0xAC, 0x5B, 0x00
};
#define OFFSET_ALPHA_GLOBAL 0x01u
#define OFFSET_ALPHA_GATE   0x0Du
#define OFFSET_ALPHA_LATCH  0x16u

typedef void (__cdecl *draw_particles_fn_t)(int32_t emitter_index, void *out);

typedef struct particle_clock_state {
    bool            installed;
    bool            active;
    uintptr_t       original_draw;
    const float    *engine_clock;      /* clockB, read only */
    volatile float  retarded_clock;    /* the cell the repointed operand now reads */
    const uint32_t *alpha_gate;
    const float    *alpha_global;
    const float    *alpha_latch;
    uint32_t        emitter_stride;
    uintptr_t       emitter_base;
    uint32_t        emitters;          /* emitters drawn, kept for a future log line */
    uint32_t        clamped;           /* of those, the ones the birth clamp caught */
} particle_clock_state_t;

static particle_clock_state_t particle_state;

static bool current_alpha(float *out)
{
    if (particle_state.alpha_gate == NULL || particle_state.alpha_global == NULL ||
        particle_state.alpha_latch == NULL) {
        return false;
    }
    *out = (*particle_state.alpha_gate != 0) ? *particle_state.alpha_latch
                                             : *particle_state.alpha_global;
    return (*out > 0.0f) && (*out <= 1.0f);
}

/* Publishes the instant this frame is actually drawing, per emitter, then lets the original run.
 *
 * No path leaves the true clock unrestored. The inactive exit calls the original without ever
 * touching the cell; the no alpha exit writes the true clock before calling the original, so the
 * cell already holds the honest value while it runs; and the main path writes and restores with
 * nothing between them that can return early. The engine is single threaded here and the original
 * cannot reach this function again, so the cell needs no depth counter.
 *
 * The retarded value is computed from the engine's clock every time rather than accumulated. Both
 * particle clocks start at 1000.0 and are never reset, so a float ulp is about 6.1e-5 at the start
 * of a session and the sub substep offset still has roughly five hundred representable steps.
 * After an hour of play the clock is near 4600 and that falls to about sixty four steps, still far
 * finer than a frame. A clock of our own would drift against the one the retirement test reads,
 * and that is the failure this avoids. */
static void __cdecl hook_draw_particles(int32_t emitter_index, void *out)
{
    draw_particles_fn_t original = (draw_particles_fn_t)particle_state.original_draw;
    const uint8_t      *emitter  = NULL;
    float               alpha;
    float               now;
    float               retarded;

    if (!particle_state.active || particle_state.engine_clock == NULL) {
        original(emitter_index, out);
        return;
    }

    now = *particle_state.engine_clock;
    if (!current_alpha(&alpha)) {
        particle_state.retarded_clock = now;         /* degrade to the engine's own behaviour */
        original(emitter_index, out);
        return;
    }

    /* The same bound the callee applies, so an out of range index reaches it untouched rather than
     * being turned into an address here. */
    if (particle_state.emitter_base != 0 && emitter_index >= 0 && emitter_index < 0x40) {
        emitter = (const uint8_t *)(particle_state.emitter_base +
                                    (uintptr_t)emitter_index * particle_state.emitter_stride);
    }

    retarded = now - (1.0f - alpha) * SUBSTEP_SECONDS;
    ++particle_state.emitters;

    /* The faulting form rather than the asking one: this runs once per active emitter per frame,
     * and the asking form is a system call. */
    if (emitter != NULL &&
        memory_try_readable((uintptr_t)emitter, EMITTER_NEWEST_BIRTH + sizeof(float))) {
        float newest = *(const float *)(emitter + EMITTER_NEWEST_BIRTH);

        if (retarded < newest) {
            retarded = newest;
            ++particle_state.clamped;
        }
    }

    particle_state.retarded_clock = retarded;
    original(emitter_index, out);
    particle_state.retarded_clock = now;         /* nothing outside the draw sees a fake clock */
}

static bool resolve_alpha(void)
{
    uintptr_t site = signature_find_unique(SIG_ALPHA_LATCH, NULL, sizeof(SIG_ALPHA_LATCH));
    uint32_t  address;

    if (site == 0) {
        return false;
    }
    if (!memory_read_u32(site + OFFSET_ALPHA_GLOBAL, &address) ||
        !memory_is_inside_image(address, sizeof(float))) {
        return false;
    }
    particle_state.alpha_global = (const float *)(uintptr_t)address;

    if (!memory_read_u32(site + OFFSET_ALPHA_GATE, &address) ||
        !memory_is_inside_image(address, sizeof(uint32_t))) {
        return false;
    }
    particle_state.alpha_gate = (const uint32_t *)(uintptr_t)address;

    if (!memory_read_u32(site + OFFSET_ALPHA_LATCH, &address) ||
        !memory_is_inside_image(address, sizeof(float))) {
        return false;
    }
    particle_state.alpha_latch = (const float *)(uintptr_t)address;
    return true;
}

static bool resolve_emitter_pool(void)
{
    uintptr_t pool = signature_find_unique(SIG_EMITTER_POOL, NULL, sizeof(SIG_EMITTER_POOL));
    uint32_t  value;

    if (pool == 0 || !memory_read_u32(pool + OFFSET_EMITTER_STRIDE, &value) || value == 0) {
        log_warning("the emitter pool stride did not resolve, so the birth clamp could not be "
                    "built and particle motion is not smoothed");
        return false;
    }
    particle_state.emitter_stride = value;

    if (!memory_read_u32(pool + OFFSET_EMITTER_BASE, &value) ||
        !memory_is_inside_image(value, sizeof(float))) {
        log_warning("the emitter pool base reads %08X, outside the image, refused",
                    (unsigned)value);
        return false;
    }
    particle_state.emitter_base = (uintptr_t)value;
    return true;
}

void particle_clock_install(bool enabled)
{
    uintptr_t operand_site;
    uintptr_t call_site;
    uint32_t  cell;

    if (particle_state.installed) {
        return;
    }
    particle_state.installed = true;

    if (!enabled) {
        log_info("InterpolateParticles=0, particle motion keeps stepping at the simulation rate");
        return;
    }
    if (!resolve_alpha()) {
        log_warning("the substep alpha latch did not resolve, particle motion is not smoothed");
        return;
    }
    if (!resolve_emitter_pool()) {
        return;
    }

    operand_site = signature_find_unique(SIG_PARTICLE_POSITION_CLOCK, NULL,
                                         sizeof(SIG_PARTICLE_POSITION_CLOCK));
    if (operand_site == 0) {
        log_warning("the particle position clock read did not resolve, motion is not smoothed");
        return;
    }
    if (!memory_read_u32(operand_site + OFFSET_PARTICLE_CLOCK_OPERAND, &cell) ||
        !memory_is_inside_image(cell, sizeof(float))) {
        log_warning("the particle clock operand reads %08X, outside the image, refused",
                    (unsigned)cell);
        return;
    }
    particle_state.engine_clock   = (const float *)(uintptr_t)cell;
    particle_state.retarded_clock = *particle_state.engine_clock;

    call_site = signature_find_unique(SIG_PARTICLE_DRAW_CALL, NULL,
                                      sizeof(SIG_PARTICLE_DRAW_CALL));
    if (call_site == 0) {
        log_warning("the particle draw call did not resolve, motion is not smoothed");
        return;
    }
    call_site += OFFSET_PARTICLE_DRAW_CALL;

    /* A call site is redirected and never detoured, because it is not a function entry. The
     * original target is read before it is overwritten, and the redirect goes on before the
     * operand: an operand repointed at a cell that nobody updates would evaluate every particle in
     * the game against a frozen clock, which is a worse picture than the one being repaired.
     *
     * The active flag is set after the last write for the same reason. Between the redirect and
     * that flag the hook can already be entered, and on that path it finds the flag clear and
     * passes straight through to the original, which by then is the only thing it could do
     * correctly. The original target is always known before the hook can be reached at all. */
    if (!patch_read_call_target(call_site, &particle_state.original_draw)) {
        log_warning("no call at %08X, particle motion is not smoothed", (unsigned)call_site);
        return;
    }
    if (patch_redirect_call(call_site, (const void *)hook_draw_particles) != PATCH_RESULT_OK) {
        log_warning("the particle draw call at %08X could not be redirected, motion is not "
                    "smoothed", (unsigned)call_site);
        return;
    }

    if (patch_repoint_operand(operand_site + OFFSET_PARTICLE_CLOCK_OPERAND, cell,
                              (uint32_t)(uintptr_t)&particle_state.retarded_clock)
        != PATCH_RESULT_OK) {
        log_warning("the particle clock operand at %08X could not be repointed, so the draw "
                    "redirect is being put back and motion is not smoothed",
                    (unsigned)(operand_site + OFFSET_PARTICLE_CLOCK_OPERAND));
        if (patch_redirect_call(call_site, (const void *)particle_state.original_draw)
            != PATCH_RESULT_OK) {
            log_error("and the redirect at %08X could not be put back; set "
                      "InterpolateParticles=0 and restart", (unsigned)call_site);
        }
        return;
    }

    particle_state.active = true;
    log_info("particle motion is evaluated at the drawn instant (operand %08X now reads our cell, "
             "draw call %08X, emitter pool %08X stride %u). The position is a closed form in the "
             "clock, so this is the exact position at that instant rather than a blend.",
             (unsigned)(operand_site + OFFSET_PARTICLE_CLOCK_OPERAND), (unsigned)call_site,
             (unsigned)particle_state.emitter_base, (unsigned)particle_state.emitter_stride);
}
