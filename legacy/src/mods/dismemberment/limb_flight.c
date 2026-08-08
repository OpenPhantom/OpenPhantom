/* limb_flight.c: the flight, the rest state, and the prevRot maintenance the engine forgot.
 * The reverse-engineering account of WHY each of these exists is in limb_flight.h.
 *
 * SIZE NOTE (rule 9): 666 lines, of which 434 are code and 147 are the measurements and byte
 * evidence that justify each constant. Removing them to reach 600 is explicitly forbidden.
 */
#include "limb_flight.h"

#include "dismemberment.h"

#include "common/detour.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/patch.h"
#include "common/signature.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* --- 0x0042F64C  candy_stuntTick: the flight of the severed piece, entry --------------------- *
 *   55 8B EC 83 EC 08        push ebp / mov ebp,esp / sub esp,8      (6 B, clean boundary)
 *   A1 24 87 86 00           mov eax,[0x868724]
 *   83 78 28 00              cmp [eax+0x28],0
 *   0F 85 <rel32>            jne ...
 *   68 C4 FD 89 00           push 0x89FDC4      <- g_stuntBlock, operand at +22
 *
 * The block, byte-proven from the two ground arms 0x42F920 / 0x42F9ED:
 *   +0x04/08/0C  spin.x/y/z      +0x10/14/18  vel.x/y/z
 *   +0x1C        life            +0x68        pos.z        +0x6C  ground delta                  */
static const uint8_t SIG_STUNT_TICK[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x08, 0xA1, 0x24, 0x87, 0x86, 0x00,
    0x83, 0x78, 0x28, 0x00, 0x0F
};
#define STUNT_TICK_PROLOGUE_SIZE  6u
#define OFFSET_STUNT_BLOCK_POINTER 22u
#define OPCODE_PUSH_IMM32       0x68u

/* --- 0x0042FB2D  candy_stuntOnContact: the contact handler of the flying piece ---------------- *
 *   55 8B EC 83 EC 30            prologue, 6 bytes
 *   A1 48928600                  mov eax,[0x869248]  = g_msgCode
 *   89 45 F4
 *   8B 0D 44928600               mov ecx,[0x869244]  = g_msgOther
 *   89 4D F8
 *
 * Why this function must be hooked. Its whole body, read instruction by instruction:
 *
 *     blk = g_stuntBlock;
 *     dir = other->pos, self->pos;  dir.z = 0;   ; FLAT, BEFORE normalising
 *     len = vec3_normalize(&dir);
 *     dir *= g_msgTag / len;                      ; DISTANCE-INVERSE
 *     blk->vel += dir;
 *     blk->spin.x = 3.3 +- 3.3;                   ; RE-ROLLED at full strength
 *     blk->spin.y = 0;
 *     blk->spin.z = 2.0 +- 2.0;
 *
 * "spinning in circles" is the re-roll, damping in the tick cannot win against a reassignment,
 * and a piece lying on a corpse or another piece is in contact constantly. "on the ground" is the
 * missing vertical: the impulse has z := 0 and grows with 1/distance, and two touching things
 * have a distance near zero.
 *
 * And the pointer is valid here; that was the question this could have failed on.
 * candy_stuntOnContact reads the GLOBAL block, not its own. task_run 0x475953, the delivery path
 * for contacts, sets `*pWakeFlag = wakeValue` BEFORE the call and clears it afterwards. During
 * the handler the global therefore points at the block of exactly the piece that was hit.
 * Several pieces do not disturb each other, checked, not assumed. */
static const uint8_t SIG_STUNT_CONTACT[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x30,
    0xA1, 0x48, 0x92, 0x86, 0x00, 0x89, 0x45, 0xF4,
    0x8B, 0x0D, 0x44, 0x92, 0x86, 0x00, 0x89, 0x4D, 0xF8
};
#define STUNT_CONTACT_PROLOGUE_SIZE 6u

/* --- 0x0042F79A  candy_stuntTick: the TUMBLE of a severed piece ------------------------------- *
 *   89 45 FC / DB 45 FC          rand15() -> int
 *   D8 0D 84834A00               fmul [0x4A8384] = 1/32768   (the normaliser. DO NOT TOUCH,
 *                                                             it has four readers)
 *   D8 0D 88834A00               fmul [0x4A8388] = 6.6       <- operand at +0x0E
 *   D8 2D 8C834A00               fsubr[0x4A838C] = 3.3       <- operand at +0x14
 *   ... (a call rel32 in between, which is why the pattern ends after 24 bytes)
 *   D8 0D 90834A00               fmul [0x4A8390] = 4.0       <- operand at +0x40
 *   D8 2D 94834A00               fsubr[0x4A8394] = 2.0       <- operand at +0x46
 *
 * Base and range must be scaled by the SAME factor, or the distribution becomes asymmetric. */
static const uint8_t SIG_STUNT_SPIN[] = {
    0x89, 0x45, 0xFC, 0xDB, 0x45, 0xFC,
    0xD8, 0x0D, 0x84, 0x83, 0x4A, 0x00,
    0xD8, 0x0D, 0x88, 0x83, 0x4A, 0x00,
    0xD8, 0x2D, 0x8C, 0x83, 0x4A, 0x00
};
#define OFFSET_SPIN_X_RANGE 0x0Eu   /* 6.6 */
#define OFFSET_SPIN_X_BASE  0x14u   /* 3.3 */
#define OFFSET_SPIN_Z_RANGE 0x40u   /* 4.0, behind the rel32, opcode checked before use */
#define OFFSET_SPIN_Z_BASE  0x46u   /* 2.0, same */
/* The yaw kick in the FLIGHT arm: candy_stuntTick 0x42F920 adds [0x4A83A0] = 90.0 to rot.y PER
 * SUBSTEP, i.e. 2880 deg/s, and that arm damps NOTHING. Exactly one reader in the image. */
#define OFFSET_SPIN_YAW_KICK 0x194u  /* 0x42F92E - 0x42F79A */

/* --- 0x0042F86A  candy_stuntTick: the GRAVITY of the flying piece ---------------------------- *
 *   8B 0D C4FD8900               mov ecx,[g_stuntBlock]
 *   D9 41 18                     fld  [block+0x18] = vel.z
 *   D8 25 98834A00               fsub [0x4A8398] = 0.2        <- operand at +0x0B
 *
 * 0.2 PER SUBSTEP is 0.2*32*32 = 204.8 u/s^2. FIVE TIMES world gravity (40 u/s^2).
 * [0x4A8398] has exactly ONE reader in the whole image. */
static const uint8_t SIG_STUNT_GRAVITY[] = {
    0x8B, 0x0D, 0xC4, 0xFD, 0x89, 0x00, 0xD9, 0x41, 0x18, 0xD8, 0x25, 0x98, 0x83, 0x4A, 0x00
};
#define OFFSET_STUNT_GRAVITY 0x0Bu

enum {
    SITE_STUNT_TICK,
    SITE_STUNT_CONTACT,
    SITE_STUNT_SPIN,
    SITE_STUNT_GRAVITY,
    SITE_COUNT
};

static signature_t sites[SITE_COUNT] = {
    SIGNATURE_ENTRY("stunt_tick",    SIG_STUNT_TICK),
    SIGNATURE_ENTRY("stunt_contact", SIG_STUNT_CONTACT),
    SIGNATURE_ENTRY("stunt_spin",    SIG_STUNT_SPIN),
    SIGNATURE_ENTRY("stunt_gravity", SIG_STUNT_GRAVITY)
};

/* The stunt block, and the bapObj behind it. */
#define STUNT_OBJECT        0x00
#define STUNT_SPIN_X        0x04
#define STUNT_VELOCITY_X    0x10
#define STUNT_LIFE          0x1C
#define STUNT_GROUND_DELTA  0x6C
#define STUNT_INITIAL_LIFE  5.0f    /* the immediate at 0x42E965 */
#define OBJECT_ROTATION     0x3C
#define OBJECT_PREVIOUS_ROT 0x60

#define TRACKED_PIECES       3      /* how many pieces the sampling follows */
#define SAMPLE_EVERY_NTH     4      /* every 4th substep -> 8 lines per second per piece */
#define PREVIOUS_ROT_SLOTS  12
#define MAX_SETTLE_MESSAGES 16
#define MAX_KICK_MESSAGES    8

typedef int32_t (__cdecl *stunt_fn_t)(void);

typedef struct limb_flight_state {
    detour_t   tick_detour;
    detour_t   contact_detour;
    uint8_t  **stunt_block_pointer;

    /* Sampling. */
    uint8_t   *sampled_block[TRACKED_PIECES];
    int        sample_tick[TRACKED_PIECES];

    /* prevRot maintenance. */
    uint8_t   *previous_rot_owner[PREVIOUS_ROT_SLOTS];
    float      previous_rot[PREVIOUS_ROT_SLOTS][3];

    /* Which blocks have already been reported at rest. Remembering only the LAST one is not
     * enough: two pieces settling in the same second alternate, so "block != last" is true every
     * time and the line repeats. Sixteen messages for three pieces is what that looked like. */
    uint8_t   *settled_reported[MAX_SETTLE_MESSAGES];
    int        settle_messages;
    int        kick_messages;
} limb_flight_state_t;

static limb_flight_state_t flight_state;

/* ============================================================================================ */
static bool scale_constant_at(uintptr_t operand_address, float scale, const char *what)
{
    uint32_t constant_address;
    float    value;

    if (!memory_read_u32(operand_address, &constant_address) ||
        !memory_is_inside_image(constant_address, sizeof(float))) {
        log_warning("%s -> constant %08X is outside the image, refused",
                    what, (unsigned)constant_address);
        return false;
    }
    if (!memory_read(constant_address, &value, sizeof(value))) {
        return false;
    }
    if (!(value > 0.0f) || value > 1000.0f) {
        log_warning("%s -> [%08X] = %.4f is not plausible, refused",
                    what, (unsigned)constant_address, (double)value);
        return false;
    }
    if (patch_write_f32(constant_address, value * scale) != PATCH_RESULT_OK) {
        return false;
    }

    log_info("%s [%08X] %.4f -> %.4f", what, (unsigned)constant_address,
             (double)value, (double)(value * scale));
    return true;
}

static int tune_spin(float scale)
{
    uintptr_t site = sites[SITE_STUNT_SPIN].address;
    uint8_t   opcode_z_range;
    uint8_t   opcode_z_base;
    int       tuned = 0;

    if (site == 0) {
        log_warning("stunt_spin did not resolve, the tumble stays at up to 211 deg/s");
        return 0;
    }

    tuned += scale_constant_at(site + OFFSET_SPIN_X_RANGE, scale, "tumble X range") ? 1 : 0;
    tuned += scale_constant_at(site + OFFSET_SPIN_X_BASE,  scale, "tumble X base")  ? 1 : 0;

    /* The two trailing operands sit behind a rel32 and are therefore not in the pattern. Check
     * the opcode first, never read blind. */
    if (memory_read_u8(site + OFFSET_SPIN_Z_RANGE - 2, &opcode_z_range) &&
        memory_read_u8(site + OFFSET_SPIN_Z_BASE  - 2, &opcode_z_base) &&
        opcode_z_range == 0xD8 && opcode_z_base == 0xD8) {
        tuned += scale_constant_at(site + OFFSET_SPIN_Z_RANGE, scale, "tumble Z range") ? 1 : 0;
        tuned += scale_constant_at(site + OFFSET_SPIN_Z_BASE,  scale, "tumble Z base")  ? 1 : 0;
    } else {
        log_warning("the Z tumble operands do not have the expected shape, skipped");
    }

    return tuned;
}

static int tune_yaw_kick(float scale)
{
    uintptr_t site = sites[SITE_STUNT_SPIN].address;
    uintptr_t operand;
    uint8_t   opcode;
    uint32_t  constant_address;
    float     value;

    if (site == 0) {
        return 0;
    }
    operand = site + OFFSET_SPIN_YAW_KICK;

    if (!memory_read_u8(operand - 2, &opcode) || opcode != 0xD8) {
        log_warning("the yaw kick operand does not have the expected shape, skipped");
        return 0;
    }
    if (!memory_read_u32(operand, &constant_address) ||
        !memory_read(constant_address, &value, sizeof(value))) {
        return 0;
    }
    if (!(value > 80.0f) || value >= 100.0f) {
        log_warning("the yaw kick is %.2f instead of 90, skipped", (double)value);
        return 0;
    }

    return scale_constant_at(operand, scale, "yaw kick in flight") ? 1 : 0;
}

static void tune_flight_constants(void)
{
    const limb_config_t *config = limb_config();
    int                  tuned = 0;

    if (config->spin_scale != 1.0f) {
        tuned += tune_spin(config->spin_scale);
    }
    if (config->yaw_scale != 1.0f) {
        tuned += tune_yaw_kick(config->yaw_scale);
    }
    if (config->gravity_scale != 1.0f) {
        uintptr_t site = sites[SITE_STUNT_GRAVITY].address;

        if (site == 0) {
            log_warning("stunt_gravity did not resolve, gravity stays at five times world");
        } else {
            tuned += scale_constant_at(site + OFFSET_STUNT_GRAVITY, config->gravity_scale,
                                       "gravity of the piece") ? 1 : 0;
        }
    }

    if (tuned != 0) {
        log_info("flight tuned - %d constants. Tumble x%.2f, gravity x%.2f (0.2 per substep is "
                 "204.8 u/s^2 = 5x world gravity)",
                 tuned, (double)config->spin_scale, (double)config->gravity_scale);
    }
}

/* ============================================================================================
 * THE SAMPLING, because two theories in a row without data were already two too many.
 *
 * Recorded AFTER the original tick but BEFORE our own intervention:
 *   life         remaining time (5.0 -> 0), says whether the rest phase is reached at all
 *   rot          obj+0x3C, the actual attitude. If it turns while spin == 0, SOMEBODY ELSE is
 *                writing it and no number in this block is the cause.
 *   spin         block+0x04, our lever
 *   vel          block+0x10, of which z is deliberately not zeroed
 *   groundDelta  block+0x6C, DECIDES THE ARM:  3.4e38 or > 1  => arm A (damps nothing)
 *                                              0 < d <= 1     => arm B (damps)
 *                                              d <= 0         => airborne, nothing at all
 * ============================================================================================ */
static void sample_stunt(uint8_t *block, float life)
{
    const float *spin = (const float *)(block + STUNT_SPIN_X);
    const float *velocity = (const float *)(block + STUNT_VELOCITY_X);
    float        ground_delta = *(const float *)(block + STUNT_GROUND_DELTA);
    uint8_t     *object;
    const float *rotation;
    const char  *arm;
    int          slot = -1;
    int          index;

    for (index = 0; index < TRACKED_PIECES; ++index) {
        if (flight_state.sampled_block[index] == block) {
            slot = index;
            break;
        }
    }
    if (slot < 0) {
        for (index = 0; index < TRACKED_PIECES; ++index) {
            if (flight_state.sampled_block[index] == NULL) {
                flight_state.sampled_block[index] = block;
                slot = index;
                break;
            }
        }
    }
    if (slot < 0) {
        return;                                    /* more pieces than we follow: stay quiet */
    }
    if ((flight_state.sample_tick[slot]++ % SAMPLE_EVERY_NTH) != 0) {
        return;
    }

    if (!memory_read((uintptr_t)(block + STUNT_OBJECT), &object, sizeof(object)) ||
        object == NULL) {
        return;
    }
    rotation = (const float *)(object + OBJECT_ROTATION);

    arm = (ground_delta > 3.0e38f) ? "A(no surface)"
        : (ground_delta > 1.0f)    ? "A(buried)"
        : (ground_delta > 0.0f)    ? "B(ground)"
                                   : "-(air)";

    log_info("sever[%d] t=%.2f %s gd=%.3f rot(%.1f %.1f %.1f) spin(%.4f %.4f %.4f) "
             "vel(%.4f %.4f %.4f)",
             slot, (double)(STUNT_INITIAL_LIFE - life), arm, (double)ground_delta,
             (double)rotation[0], (double)rotation[1], (double)rotation[2],
             (double)spin[0], (double)spin[1], (double)spin[2],
             (double)velocity[0], (double)velocity[1], (double)velocity[2]);
}

/* ============================================================================================
 * The cause of the spinning, and it is in the DRAWING, not in the physics.
 *
 * Measured, not asserted (120 samples over three pieces):
 *
 *   sever[0] t=0.53 B(ground) gd=0.048 rot(-0.7 -1.6 -0.0) spin(0.0000 0.0000 0.0000) vel(0 0 0.032)
 *   sever[0] t=2.66 B(ground) gd=0.048 rot(-0.7 -1.6 -0.0) spin(0.0000 0.0000 0.0000) vel(0 0 0.032)
 *
 * `spin` is zero from 0.5 s on and `rot` is LITERALLY constant over seconds. 112 of 120 samples
 * are in arm B, the damping one. the piece lies still in the simulation. That retired three
 * hypotheses at once, the contact re-roll, the undamped arm A, and the distance-inverse
 * impulse. None of them was the cause.
 *
 * The reason is one level up, in bapobj_drawAll:
 *
 *     eul.y = angle_diff(o->prevRot.y, o->rot.y) * alpha + o->prevRot.y;
 *
 * The DRAWN attitude is interpolated between prevRot (obj+0x60) and rot (obj+0x3C). prevRot is
 * maintained by the player, by every actor, and once by the piece's CREATION, but
 * candy_stuntTick maintains prevPos BY HAND and prevRot NEVER. So prevRot stays at its creation
 * value (about 0) while rot runs to e.g. (-1.5, -29.1, 0.6). The drawn yaw therefore saws 32
 * times a second between 0 and -29 degrees, on a piece that is physically completely at rest.
 *
 * This is a defect of the original engine, not ours. The yaw lerp above is in the shipped game;
 * the frame-rate DLL's pitch/roll interpolation only widens it from one axis to three and makes
 * it more noticeable.
 *
 * THE TREATMENT is what the engine already does for prevPos: maintain the field.
 * ============================================================================================ */
static void maintain_previous_rotation(uint8_t *block)
{
    uint8_t *object;
    float   *rotation;
    float   *previous;
    int      slot = -1;
    int      free_slot = -1;
    int      index;

    if (!memory_read((uintptr_t)(block + STUNT_OBJECT), &object, sizeof(object)) ||
        object == NULL) {
        return;
    }
    rotation = (float *)(object + OBJECT_ROTATION);
    previous = (float *)(object + OBJECT_PREVIOUS_ROT);

    for (index = 0; index < PREVIOUS_ROT_SLOTS; ++index) {
        if (flight_state.previous_rot_owner[index] == block) {
            slot = index;
            break;
        }
        if (flight_state.previous_rot_owner[index] == NULL && free_slot < 0) {
            free_slot = index;
        }
    }

    if (slot < 0) {
        /* First sighting: prevRot = rot. One frame without interpolation is correct; there IS
         * no previous attitude yet. If the table is full it is likewise equalised: better a hard
         * frame than the sawtooth. */
        previous[0] = rotation[0];
        previous[1] = rotation[1];
        previous[2] = rotation[2];
        if (free_slot < 0) {
            return;
        }
        slot = free_slot;
        flight_state.previous_rot_owner[slot] = block;
    } else {
        previous[0] = flight_state.previous_rot[slot][0];
        previous[1] = flight_state.previous_rot[slot][1];
        previous[2] = flight_state.previous_rot[slot][2];
    }

    flight_state.previous_rot[slot][0] = rotation[0];
    flight_state.previous_rot[slot][1] = rotation[1];
    flight_state.previous_rot[slot][2] = rotation[2];
}

static void release_previous_rotation_slot(const uint8_t *block)
{
    int index;

    for (index = 0; index < PREVIOUS_ROT_SLOTS; ++index) {
        if (flight_state.previous_rot_owner[index] == block) {
            flight_state.previous_rot_owner[index] = NULL;
        }
    }
}

/* ============================================================================================
 * THE REST STATE the engine does not have.
 *
 * Arm B (0 < delta <= 1) damps spin with -0.5 and vel with 0.667; arm A (delta == 3.4e38 or
 * delta > 1) adds 90 degrees of yaw PER SUBSTEP and leaves spin ALONE. So a piece lying slightly
 * more than one unit above the surface, or one whose ground query finds no surface, runs arm A
 * forever, and arm A damps nothing. Scaling the constants makes the tumble slower but never ends
 * it; the engine simply frees the piece after 5 s.
 *
 * So we build the rest state, TIME-BASED rather than through velocity thresholds. The reason is
 * honesty about what is known: what absolute magnitude `vel` has is not cleanly proven (per
 * substep or per second), and a threshold on a quantity whose unit one is guessing is a bet. The
 * life in +0x1C, by contrast, is byte-proven in seconds (0x42FAE4 subtracts g_frameDelta), and a
 * per-substep factor is unit-free.
 *
 *   until SettleSeconds:  spin *= SettleDamping per substep  -> a visible run-out
 *   afterwards:           spin = 0, vel.x/y = 0              -> it lies still
 *
 * vel.z is left alone: if the piece is still falling at that point, it should finish falling.
 * ============================================================================================ */
static int32_t __cdecl hook_stunt_tick(void)
{
    stunt_fn_t           original = (stunt_fn_t)flight_state.tick_detour.original;
    const limb_config_t *config = limb_config();
    int32_t              result = original();
    uint8_t             *block;
    float               *spin;
    float               *velocity;
    float                life;

    if (flight_state.stunt_block_pointer == NULL) {
        return result;
    }
    block = *flight_state.stunt_block_pointer;

    /* The slot is released as soon as the piece disappears, otherwise the table holds a dead
     * block pointer and the next stunt inherits a foreign attitude at the same address. */
    if (result != 0) {
        release_previous_rotation_slot(block);
        return result;                             /* -1: the piece has already been freed */
    }
    if (block == NULL) {
        return result;
    }

    life = *(const float *)(block + STUNT_LIFE);
    if (!(life > 0.0f) || life > STUNT_INITIAL_LIFE + 1.0f) {
        return result;                             /* not plausible: hands off */
    }

    if (config->diagnostics) {
        sample_stunt(block, life);                 /* BEFORE the intervention: the raw state */
    }

    maintain_previous_rotation(block);             /* THE actual fix, see above */

    spin     = (float *)(block + STUNT_SPIN_X);
    velocity = (float *)(block + STUNT_VELOCITY_X);

    if (life <= STUNT_INITIAL_LIFE - config->settle_seconds) {
        spin[0] = 0.0f;
        spin[1] = 0.0f;
        spin[2] = 0.0f;
        velocity[0] = 0.0f;
        velocity[1] = 0.0f;                        /* vel.z NOT - it may finish falling */

        /* ONCE PER PIECE, and once really means once. A one-shot flag hid what was being
         * reported (one message for five severings); remembering only the last block then went
         * the other way and printed sixteen messages for three pieces, because two of them
         * settled in the same second and alternated. Both times the message lied about how many
         * pieces reached rest. */
        if (flight_state.settle_messages < MAX_SETTLE_MESSAGES) {
            bool already_reported = false;
            int  slot;

            for (slot = 0; slot < flight_state.settle_messages; ++slot) {
                if (flight_state.settled_reported[slot] == block) {
                    already_reported = true;
                    break;
                }
            }
            if (!already_reported) {
                flight_state.settled_reported[flight_state.settle_messages] = block;
                ++flight_state.settle_messages;
                log_info("piece %d is at rest (block %08X, after %.2f s). The engine itself has "
                         "no rest state, arm A of candy_stuntTick never damps the tumble.",
                         flight_state.settle_messages, (unsigned)(uintptr_t)block,
                         (double)config->settle_seconds);
            }
        }
    } else {
        float damping = config->settle_damping;

        spin[0] *= damping;
        spin[1] *= damping;
        spin[2] *= damping;
    }

    return result;
}

/* In the rest phase the handler is not run at all. In the original it ALWAYS returns 1 (including
 * for every message id other than 0x23), so a bare 1 is bit-identical for everything except 0x23
 * and, for 0x23, exactly the intended abstention.
 * Bit 1 of the return value is a COMMAND to task_run (zero the countdown); the original never sets
 * it, so neither do we.
 *
 * BEFORE the rest phase everything stays as it was, the first impact should look lively. The
 * distance-inverse impulse is only MEASURED there, not capped: what absolute magnitude `vel` has
 * is not proven, and a threshold on a quantity whose unit one is guessing is a bet. */
static int32_t __cdecl hook_stunt_contact(void)
{
    stunt_fn_t           original = (stunt_fn_t)flight_state.contact_detour.original;
    const limb_config_t *config = limb_config();
    uint8_t             *block;
    float                life;
    float                before[3];
    float                delta[3];
    int32_t              result;

    if (flight_state.stunt_block_pointer == NULL ||
        (block = *flight_state.stunt_block_pointer) == NULL) {
        return original();
    }

    life = *(const float *)(block + STUNT_LIFE);
    if (life > 0.0f && life <= STUNT_INITIAL_LIFE - config->settle_seconds) {
        return 1;                                  /* no impulse, no new roll */
    }

    before[0] = *(const float *)(block + STUNT_VELOCITY_X);
    before[1] = *(const float *)(block + STUNT_VELOCITY_X + 4);
    before[2] = *(const float *)(block + STUNT_VELOCITY_X + 8);

    result = original();

    if (flight_state.kick_messages < MAX_KICK_MESSAGES) {
        delta[0] = *(const float *)(block + STUNT_VELOCITY_X)     - before[0];
        delta[1] = *(const float *)(block + STUNT_VELOCITY_X + 4) - before[1];
        delta[2] = *(const float *)(block + STUNT_VELOCITY_X + 8) - before[2];

        if (delta[0] != 0.0f || delta[1] != 0.0f) {
            ++flight_state.kick_messages;
            log_info("contact impulse %d, dvel (%.3f %.3f %.3f) per substep, %.2f s of life "
                     "left. The impulse is distance-inverse and has NO vertical component "
                     "(candy_stuntOnContact zeroes z before normalising), large numbers here are "
                     "the cause of the sliding.",
                     flight_state.kick_messages, (double)delta[0], (double)delta[1],
                     (double)delta[2], (double)life);
        }
    }

    return result;
}

/* ============================================================================================ */
static bool resolve_stunt_block(uintptr_t tick_site)
{
    uint8_t  opcode;
    uint32_t pointer_address;

    if (!memory_read_u8(tick_site + OFFSET_STUNT_BLOCK_POINTER - 1, &opcode) ||
        opcode != OPCODE_PUSH_IMM32) {
        log_warning("no `push imm32` in front of the block pointer, refused");
        return false;
    }
    if (!memory_read_u32(tick_site + OFFSET_STUNT_BLOCK_POINTER, &pointer_address) ||
        !memory_is_inside_image(pointer_address, sizeof(uint32_t))) {
        log_warning("g_stuntBlock would be at %08X - refused", (unsigned)pointer_address);
        return false;
    }

    flight_state.stunt_block_pointer = (uint8_t **)(uintptr_t)pointer_address;
    return true;
}

void limb_flight_install(void)
{
    const limb_config_t *config = limb_config();
    uintptr_t            tick_site;
    uintptr_t            contact_site;

    signature_resolve_table(sites, SITE_COUNT);
    tune_flight_constants();

    if (config->settle_seconds <= 0.0f) {
        log_info("SettleSeconds=0, no rest state; the piece tumbles until it is freed after 5 s");
        return;
    }

    tick_site = sites[SITE_STUNT_TICK].address;
    if (tick_site == 0) {
        log_warning("stunt_tick did not resolve, the severed piece keeps tumbling until it is "
                    "freed after 5 s");
        return;
    }
    if (!resolve_stunt_block(tick_site)) {
        return;
    }

    if (!detour_install(&flight_state.tick_detour, tick_site,
                        (const void *)hook_stunt_tick, STUNT_TICK_PROLOGUE_SIZE)) {
        log_error("candy_stuntTick at %08X cannot be hooked", (unsigned)tick_site);
        flight_state.stunt_block_pointer = NULL;
        return;
    }
    log_info("rest state for severed pieces active (%08X, g_stuntBlock=%08X), tumble x%.2f per "
             "substep, at rest for good after %.2f s",
             (unsigned)tick_site, (unsigned)(uintptr_t)flight_state.stunt_block_pointer,
             (double)config->settle_damping, (double)config->settle_seconds);

    /* Without this second hook the rest state is ineffective, the tick damps, the contact
     * re-rolls, and without both together the re-roll wins. A failure here is therefore reported
     * LOUDLY rather than silently accepted. */
    contact_site = sites[SITE_STUNT_CONTACT].address;
    if (contact_site == 0) {
        log_warning("stunt_contact did not resolve, the rest state REMAINS INEFFECTIVE, because "
                    "candy_stuntOnContact re-rolls the tumble on every contact and pushes "
                    "horizontally");
        return;
    }
    if (!detour_install(&flight_state.contact_detour, contact_site,
                        (const void *)hook_stunt_contact, STUNT_CONTACT_PROLOGUE_SIZE)) {
        log_error("candy_stuntOnContact (%08X) cannot be hooked, the rest state REMAINS "
                  "INEFFECTIVE", (unsigned)contact_site);
        return;
    }
    log_info("contact handler hooked (%08X), no impulse and no new tumble roll during the rest "
             "phase. Before it, the impulse is only measured, not capped.", (unsigned)contact_site);
}
