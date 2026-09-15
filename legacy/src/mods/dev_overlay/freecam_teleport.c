/* freecam_teleport.c: see freecam_teleport.h. */
#include "freecam_teleport.h"

#include "cheats_internal.h"
#include "cheats_openphantom.h"
#include "floor_probe.h"
#include "overlay_input.h"
#include "player_slot.h"

#include "common/logging.h"

#include <stddef.h>
#include <stdint.h>

/* How far above the player the camera may be and still bring them to it, in world units.
 *
 * Measured against the PLAYER, never probed under the camera. This was a correction. The
 * first version of this asked the engine's floor probe what was under the camera. That probe is
 * scoped to the cell its point sits in, and a camera flown above the level is in no cell, so it
 * answered "no floor" for solid ground and the teleport was refused almost every time it was
 * wanted. A field session logged twelve refusals, every one of them that false void, and the
 * only teleports that got through were within a few units of standing height.
 *
 * The player is always inside the world, so their own height is a reference that cannot lie.
 * The camera's height above it is not the exact drop, since the ground under the camera may be
 * lower still, but it is the right shape of number and it is never wrong about the direction.
 *
 * Eighty is measured, not derived, and that distinction cost a crash. It was briefly raised to
 * 350 on the reasoning that the fall grace in cheats_fall_consequences.c suppresses the landing
 * damage and both deaths for ten seconds, and that ten seconds of falling at 40 units/s^2
 * clamped to 40 units/s (player+0x80, and 0x004a86f8) covers 380 units. That arithmetic is
 * correct and it answers the wrong question. The grace decides whether the player SURVIVES the
 * landing. It says nothing about whether the engine can run the fall at all.
 *
 * Field evidence, from a session logged at successively greater heights: teleports at 30.0,
 * 51.5 and 72.3 were fine, and one at 110.8 over ground at roughly 25 to 30, so a drop of
 * about 85 units, took the game down. That sits just past this limit, which is where the
 * earlier note had already put it from the engine's own fall thresholds.
 *
 * For scale, in these same units the engine takes fall damage at 3.5, kills at 6, and calls a
 * fall significant at 8, and a level's fog band runs 8 to 32. Eighty is ten times the point at
 * which a fall becomes serious to this engine, so every ordinary rooftop or cliff drop goes
 * through and only a flight well above the level is refused. Do not raise it again without a
 * session that actually survives the higher number. */
#define FREECAM_MAX_TELEPORT_DROP 80.0f

/* The teleport, if the bound key asked for it, and before the simulation is released:
 * one write into a world that is still frozen, so the player's own physics resumes
 * FROM the new place rather than being fought at it. That ordering is the whole
 * difference between this and the noclip this feature replaced.
 *
 * The position is all that moves. Velocity, mode and heading keep whatever they held
 * when flight began, so stepping out mid-air is a fall from wherever the camera was
 * left, which is the point of the key. The follow camera needs nothing either way:
 * the very next updateCam recomputes it from the player, wherever the player now is.
 *
 * One thing is refused: a drop past FREECAM_MAX_TELEPORT_DROP, measured from the
 * player, below. The refusal this replaced is worth keeping on record. The first
 * version asked the engine's own probe whether there was ground under the CAMERA and
 * declined the teleport when there was not, or when the drop was over eighty units.
 * The floor refusal fired constantly in ordinary use, because that probe is scoped to
 * the cell its point sits in: fly above the level and the point is in no cell, so no
 * floor polygon is ever offered and the answer is "void" whatever is really
 * underneath. A field session logged twelve refusals and not one was the height cap;
 * every one was that false void, and every teleport that did go through was within a
 * few units of standing height. Flying up and dropping the player in is what this key
 * is for, so the probe is never asked about the camera any more; the height cap
 * stayed, measured from the one point the probe can be trusted at. */
void freecam_teleport_player_to(float x, float y, float z)
{
    uint8_t *player = (uint8_t *)player_slot_current();

    /* Too high to survive the arrival? Asked before anything is written, because
     * refusing has to leave the player exactly where they were. A refusal then falls
     * through to the same path the plain return key takes, so the camera snaps back to
     * the player and the key reads as "not from here" rather than as a key that did
     * nothing.
     *
     * Height only, and the floor probe is asked about the PLAYER, never about the
     * camera; see the note on FREECAM_MAX_TELEPORT_DROP for the probe under the
     * camera that was tried and what it cost. */
    if (player != NULL) {
        const float *standing = (const float *)(player + PLAYER_POSITION_OFFSET);
        float        above    = z - standing[2];
        float        player_air = 0.0f;

        /* The player may be off the ground themselves, mid-fall from a previous
         * teleport or stood on something with a long way down. Then the drop is
         * farther than the camera's height above them and measuring only that would
         * wave through exactly the fall this refuses. The probe is asked HERE, at the
         * player, the one place it can be trusted: the player is inside the world, so
         * the cell lookup it depends on succeeds. Asking it about the camera is what
         * broke this before. */
        if (floor_probe_below(standing, &player_air) == FLOOR_PROBE_FOUND) {
            above += player_air;
        }

        if (above > FREECAM_MAX_TELEPORT_DROP) {
            log_info("the teleport was refused and the camera returned instead: it "
                     "is %.0f units of drop, past the %.0f this engine can finish a "
                     "fall from",
                     (double)above, (double)FREECAM_MAX_TELEPORT_DROP);
            player = NULL;
        }
    }

    if (player != NULL) {
        /* BOTH copies of the position. Dropping from height did not work because
         * only one was written. Plr_CommitPose (0x0044C06B) opens with
         *
         *     if (pPlayer+0xA0 != 0) { +0x118 = +0x124; ... }
         *
         * so on any frame the player is moving, pos is replaced wholesale by
         * desiredPos. Writing pos alone survived only while the player happened to
         * be standing still. The moment the movement phase ran it recomputed
         * desiredPos from our new position with its own ground resolution applied,
         * committed that back over pos, and the player arrived at the right x and y
         * planted on the floor. Writing desiredPos ALONE was tried even earlier and
         * left them where they started, which is the same bug from the other side.
         * Both, and neither copy can put them back.
         *
         * The camera's own Z, deliberately. Standing the player on the floor beneath
         * the camera was tried and taken back out: dropping somebody in from height
         * is the thing people want this key for. The only bound on the drop is the
         * height cap already passed above. */
        float *position = (float *)(player + PLAYER_POSITION_OFFSET);
        float *desired  = (float *)(player + PLAYER_DESIRED_POSITION_OFFSET);
        float *vertical = (float *)(player + PLAYER_VERTICAL_VELOCITY_OFFSET);

        position[0] = desired[0] = x;
        position[1] = desired[1] = y;
        position[2] = desired[2] = z;

        /* From rest. The resolver decays this by gravity every step and adds it to
         * the height, so zero is a fall that starts the instant the world resumes.
         * Left alone it would carry whatever the player held when flight began,
         * which for somebody who was running is a sideways launch rather than a
         * drop. */
        *vertical = 0.0f;

        /* The arrival is a FALL, from whatever altitude the camera was flown to, and
         * it is not one the player chose to take. The same five consequences jump
         * boost suppresses are suppressed for it, ending on the first landing. See
         * cheats_openphantom_grant_fall_grace. */
        cheats_openphantom_grant_fall_grace();

        /* F4 means "put me there and play", and the panel holds the simulation just as
         * the camera does; without this the move would not resolve until the panel was
         * closed by hand, and the first attempt therefore looked like it did nothing
         * until then. */
        overlay_input_close();

        log_info("the free camera teleport key dropped the player at %.1f %.1f %.1f",
                 (double)x, (double)y, (double)z);
    } else if (player_slot_current() == NULL) {
        log_warning("the teleport key asked to drop the player at the camera, but "
                    "there is no player record to move; the camera returned instead");
    }
}
