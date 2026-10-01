/* mp_world_phase.h: the phase controller for the map's free runners.
 *
 * A free runner is the one mover type with no anchor. It never rests, never latches and is never
 * opened; its pose is an integral over the whole life of the level, wrapped onto a ring. That is
 * read out of the mover integrator at 0x00409170, whose case 0 is seven instructions with no
 * branch:
 *
 *     004091EF  8B 55 08          mov  edx, [ebp+8]
 *     004091F2  D9 42 1C          fld  dword [edx+0x1C]      ; speed
 *     004091F5  D8 4D F8          fmul dword [ebp-8]         ; times dt
 *     004091F8  8B 45 08          mov  eax, [ebp+8]          ; the mover again
 *     004091FB  D8 40 2C          fadd dword [eax+0x2C]      ; plus pose
 *     004091FE  D9 5D F4          fstp dword [ebp-0xC]
 *     00409201  E9 FA 03 00 00    jmp  0x409600              ; the shared tail
 *
 * It ignores the direction field and has no end state; the jump table at 0x408DC1 sends entry 0
 * of the opener straight to its epilogue, so it is never opened; and the tail at 0x409600 is
 * `if (pose < 0) pose = 0; while (pose > length) pose -= length;`. Every other type comes out of
 * rest at pose zero and is put into motion by a trigger that travels as an event, so its phase is
 * reset every cycle and an error cannot outlive one. The two machines start the free runner's
 * integral at different moments, so it is the one class that drifts apart with no trigger at all,
 * and the field measures it out to the far side of the ring.
 *
 * Because timeBase is rewritten on every tick the dt sum telescopes, and a free runner's pose is
 * a pure function of the level clock: `wrap(P0 + speed * (worldTime - worldTime at the prime))`.
 * P0, the speed and the length are authored and identical on both machines, so the phase offset
 * of every free runner in a level follows from one scalar, the difference between the two
 * machines' seconds since the level started. That is the better account of the cause and the
 * worse correction: a mover at speed 30 laps in 0.97 s, so a clock difference of two seconds is
 * 2.07 laps for it, of which only 0.07 is visible, and correcting through the clock would move it
 * two whole laps to fix a misalignment of seven per cent. For a periodic object the phase is the
 * quantity that matters.
 *
 * What this file holds is the decision, not the write: how far apart two poses are on a ring, how
 * much of that difference is real rather than the grain the two machines can sample it to, and how
 * much of it may be paid off in one integration. It knows nothing about the engine.
 *
 * The correction is a time, never a pose. The engine's integrator advances a mover by
 * `speed * (now - timeBase)` and then sets timeBase to now, so a value written into timeBase
 * changes exactly one advance and is gone:
 *
 *     00409186  D9 45 0C          fld   dword [ebp+0x0C]     ; now
 *     00409189  D8 58 30          fcomp dword [eax+0x30]     ; equal to timeBase?
 *     0040919D  D8 61 30          fsub  dword [ecx+0x30]     ; dt = now less timeBase
 *     004091A3  D8 1D ..          fcomp dword [0x4A80A8]     ; against 0.0f
 *     004091AE  74 07             je                         ; clamp dt to 0 when negative
 *     004091BD  89 42 30          mov   [edx+0x30], eax      ; timeBase = now
 *
 * The write at 0x004091BD stands before the switch at 0x004091E8 and the function has exactly one
 * ret, so no case can get past it. Two safety properties are the engine's and not this module's.
 * A mover cannot be made to run backwards: the clamp at 0x004091AE sits 44 bytes in front of the
 * first multiplication by the speed, so a bias larger than the whole step stalls rather than
 * reverses, and it catches a NaN on the way, since fcomp sets C0 for an unordered compare. And a
 * pose is never written: the pose applier evaluates the keyframe track at whatever pose comes
 * out, so a value the mover never passed through would skip the edges in between, and those are
 * the edges that switch collision faces on and off, start sounds and reveal bodies.
 */
#ifndef MULTIPLAYER_MP_WORLD_PHASE_H
#define MULTIPLAYER_MP_WORLD_PHASE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* As many free runners as the digest can describe, so a mover that was measured can be corrected.
 * The largest shipped level has seventeen; there are 59 over the eleven levels, with integer
 * speeds from 1 to 60 and nineteen of them at exactly 30. The travel length is 29.0 in all 528
 * shipped mover records, so a lap is 29/speed seconds, between 0.48 s and 29 s. */
#define MP_WORLD_PHASE_SLOTS 64u

/* The substep, which is the engine's own simulation step and the rate this module runs at. */
#define MP_WORLD_PHASE_SUBSTEP 0.03125f

/* How much of one substep a single integration may be shifted by. A quarter means the mover runs
 * at most 25 per cent fast or slow, which is inside the spread it already sees from the frame
 * clock, and it keeps the bite STRICTLY below one substep. That is not a comfort margin: the
 * path at 0x00409191 returns without writing timeBase when now equals it, so a bite of a whole
 * substep could land exactly on the next now and leave its own bias standing. A quarter cannot,
 * and the unit test drives every bite through that bound. */
#define MP_WORLD_PHASE_BITE 0.25f

/* A legitimate advance between two looks is bounded by the engine's own frame time cap of a tenth
 * of a second. Anything past that is another writer having set timeBase: the level prime does it
 * for every mover, and so does a savegame load. A debt measured before that is a debt against a
 * world that is gone.
 *
 * The writers were counted rather than assumed, by a byte census over the whole retail code for
 * stores at displacement 0x30, cross checked against all 35 sites that read the world's mover
 * table at displacement 0x624. Five: the integrator at 0x004091BD on every tick; eight sites in
 * the opener, which the jump table keeps away from type 0; the level prime at 0x00408AE5, which
 * sets timeBase for every mover; 0x0040AB8B, type 5 only; and the savegame restore at 0x0040AE2D
 * and 0x0040AF0F. The two that reach a free runner are the prime and the restore, and this bound
 * is what survives them without a site of its own. */
#define MP_WORLD_PHASE_JUMP_SECONDS 0.15f

typedef struct mp_world_phase_slot {
    uint16_t id;
    bool     used;
    bool     have_pose;
    float    owed;        /* seconds of this mover's own timeline still to pay; + means ahead */
    float    last_pose;   /* the pose at the last look, to tell an integration from a still one */
} mp_world_phase_slot_t;

typedef struct mp_world_phase {
    mp_world_phase_slot_t slot[MP_WORLD_PHASE_SLOTS];
    uint32_t              taken;      /* debts booked */
    uint32_t              settled;    /* debts paid off to inside the dead band */
    uint32_t              discarded;  /* debts dropped because another writer moved the mover */
    uint32_t              nudges;     /* integrations actually shifted */
    uint32_t              full;       /* measurements with no slot left */
    uint32_t              worst_milli;/* the largest debt ever booked, thousandths of the travel */
} mp_world_phase_t;

void mp_world_phase_reset(mp_world_phase_t *phase);

/* How far the local pose is AHEAD of the wire's, the short way round the ring, in (-0.5, +0.5].
 * Both arguments are fractions of the travel. */
float mp_world_phase_delta(float local_fraction, float wire_fraction);

/* The dead band for one mover, as a fraction of its travel: two substeps of its own motion. The
 * two machines sample the same mover up to a substep apart and the measurement crosses a wire, so
 * a difference smaller than this is not a disagreement, it is the grain. A mover fast enough for
 * this to be a large fraction is one whose phase cannot be measured finely, which is a property of
 * the mover and not of the controller: for the 25 shipped records at speed 30 and above the band is
 * 65 to 129 thousandths of the ring, because two machines sampling 31 ms apart cannot phase match a
 * lap of 0.48 s more finely than that. That is why a field run is judged by the "beyond a quarter"
 * bucket reaching zero rather than by everything falling under a sixteenth, which 25 of the 59
 * could never do. */
float mp_world_phase_dead_band(float speed, float length);

/* Book what one measurement says. `delta` is the signed fraction from mp_world_phase_delta. A
 * difference inside the dead band settles the mover instead of booking anything. Returns true when
 * a debt now stands against this mover. */
bool mp_world_phase_measure(mp_world_phase_t *phase, uint16_t id, float delta, float speed,
                            float length);

/* What to add to this mover's timeBase before its next integration, in seconds; positive slows it
 * down. Zero when nothing is owed, when the mover has not moved since the last look, or when it
 * moved further than one integration can account for, which means the debt is against a world that
 * no longer exists and is dropped. Call once per substep per mover with a debt. */
float mp_world_phase_bite(mp_world_phase_t *phase, uint16_t id, float pose, float speed,
                          float length);

/* How many movers still owe something. */
uint32_t mp_world_phase_owing(const mp_world_phase_t *phase);

#endif /* MULTIPLAYER_MP_WORLD_PHASE_H */
