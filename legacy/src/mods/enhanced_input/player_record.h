#ifndef PLAYER_RECORD_H
#define PLAYER_RECORD_H

#include <stdint.h>

/* The player record's field offsets, shared by the two source files of this DLL. The address of
 * the record itself is never hard-coded: it is read out of the instruction that loads it.
 *
 * Each offset carries the site it was read at, so the claim can be checked against the image
 * without leaving this file. */

#define PLAYER_MODULE_STATE  0x004   /* only 1 runs Plr_RunPhases; the death arm is not 1     */
#define PLAYER_ACTOR         0x00C   /* hActor: the body the pose is written to               */
#define PLAYER_CUR_MODE      0x060   /* pCurMode: a POINTER at the mode descriptor  (0x4479FD) */
#define PLAYER_FRAME_DELTA   0x074   /* the dt the integrator works with            (0x44A69F) */
#define PLAYER_DT_SCALE30    0x078   /* frameDt / (1/30): the engine's own substep scale       */
#define PLAYER_MOVE_DRIVE    0x0A4   /* this substep's speed delta                  (0x449EFC) */
#define PLAYER_CURRENT_SPEED 0x0B0   /* the ramped speed, and it is SIGNED          (0x44A87C) */
#define PLAYER_FACING_X      0x100   /* the unit facing, rebuilt from heading       (0x44A6D5) */
#define PLAYER_FACING_Y      0x104   /*                                             (0x44A6DC) */
#define PLAYER_MOVE_INPUT    0x154   /* bit 0 = forward, bit 1 = backward           (0x449EF0) */
#define PLAYER_HEADING       0x2A0   /* the view direction in degrees               (0x44A6B2) */
#define PLAYER_TURN_WHEEL    0x2A4   /* the turn rate in degrees per second         (0x44A083) */

/* The two node INDICES the steering lean twists, filled at startup from the engine's own node-name
 * table: [0x448114] takes name id 1 = "head", [0x448137] takes id 2 = "chest". Both are unsigned,
 * the setter's bounds check is an unsigned compare against numNodes.
 *
 * An index of 0 does NOT mean "first node", it means the lookup FAILED: the finder returns 0 when
 * the name is absent, and 0 is the model root, which is the node the sideways walk latches. A rig
 * without a chest or a head must therefore be left alone rather than twisted at its root. */
#define PLAYER_CHEST_NODE    0x044   /* name id 2                                   (0x448137) */
#define PLAYER_HEAD_NODE     0x054   /* name id 1, a CHILD of the chest             (0x448114) */

/* The chest claim. The auto-aim writes the aim bearing here and takes the chest node with it; the
 * weapon code releases both together. While it is non-zero the chest belongs to the aim, exactly
 * as node 0 belongs to the hit reaction while the drop timer runs. */
#define PLAYER_CHEST_CLAIM   0x178   /*                                             (0x44BA08) */
#define PLAYER_DROP_TIMER    0x2A8   /* the hurt lock; while it runs the flinch owns node 0    */
#define PLAYER_RECORD_SIZE   0x2AC   /* dropTimer at 0x2A8 is the highest field this DLL touches */

/* Index 0 of the mode table at [0x4B54B0], which lists the fourteen descriptors in the order of the
 * mode enum. Stand is the only mode whose own tick is the walk/run clip selector. */
#define PLAYER_MODE_STAND       0
/* The two melee modes. Their descriptors, kMode_SabreAttackDesc [0x4B52C8] and kMode_PanakaDesc
 * [0x4B52E8], both read { default, default, default, default, own tick, skip, skip, run-and-pin }:
 * index 2 is the steer phase and the pin at index 7 hands the integrate and every phase after it
 * back to the defaults. So Plr_Integrate [0x0044A59E] turns the heading from the turn cell in a
 * swing exactly as it does standing, and the blade goes with it: Plr_TestSwingWorld [0x0044E6E2]
 * sweeps a capsule from the contact node's last world position to its current one every substep.
 * Neither melee tick writes the heading; nothing outside them was swept for. */
#define PLAYER_MODE_SABRE_ATTACK 3
#define PLAYER_MODE_PANAKA       4
/* The three modes the body is off the ground in and the engine still runs the steer and the
 * integrate: horizontal motion there is the ordinary facing times curSpeed, so turning the
 * heading redirects the flight. FixedJump is deliberately not among them, it follows an
 * authored arc and its descriptor skips the steer phase outright. */
#define PLAYER_MODE_JUMP        6
#define PLAYER_MODE_JEDI_JUMP   7
#define PLAYER_MODE_FALL        8
#define PLAYER_MODE_SIDLE      10    /* builds its displacement from launchVel, not curSpeed */
#define PLAYER_MODE_FIXED_JUMP 11    /* follows a ballistic arc                              */
#define PLAYER_MODE_MAX        13
#define PLAYER_MODE_TABLE_MAX  32    /* the shipped table has 14 entries and a NULL */

/* The model root. Rotating it turns the whole visible body, and it is the node the engine's own
 * hit reaction rotates, so this is a use the shipped code already makes of it. */
#define MODEL_ROOT_NODE      0u

/* bapobj_setNodeYaw. Fails SILENTLY when the node index is out of range, so a return of 0 must
 * never be read as "the model has no such node, carry on". */
typedef int32_t (__cdecl *set_node_yaw_fn_t)(void *body, uint32_t node, float degrees);

#endif /* PLAYER_RECORD_H */
