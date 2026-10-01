/* mp_signatures_enemy.h: the byte patterns for what the engine does to an actor's body.
 *
 * Patterns only; the rows that use them live in mp_signatures_puppet.c with every other row,
 * because a table of its own would need a merge and mp_signatures.c has no room for one. The two
 * call sites at the end are the exception: each is matched for the address behind its call and
 * nothing else, and is resolved where that address is read, the way the scene gates resolve the
 * three places a script takes the camera.
 *
 * Both of these are the engine changing an actor's body in a way a parked replica never sees: a
 * script hanging a particle emitter on it, and a limb coming off. The footstep pair sits in its
 * own file instead of here, because that one is about a player's puppet.
 *
 * The arrays are extern with an explicit size rather than static with an implicit one, which is
 * what moving a pattern out of its table costs. mp_cell_sites.c records why that is worth saying
 * out loud: the offline verifier once recognised only the static form, and thirty four patterns
 * were outside verification for weeks with nothing reporting it. Both of these are checked by
 * name against all five shipped images.
 */
#ifndef MULTIPLAYER_MP_SIGNATURES_ENEMY_H
#define MULTIPLAYER_MP_SIGNATURES_ENEMY_H

#include <stdint.h>

extern const uint8_t SIG_MP_AI_START_EMITTER[12];
extern const uint8_t SIG_MP_ENEMY_DETACH_PIECE[21];
extern const uint8_t SIG_MP_WITHIN_RANGE[20];
extern const uint8_t SIG_MP_LIGHT_SET[21];
extern const uint8_t SIG_MP_EMITTER_SET[21];
extern const uint8_t SIG_MP_SOUND_SET[21];
extern const uint8_t SIG_MP_SHOT_SHATTER_THING[16];
extern const uint8_t SIG_MP_ENEMY_TICK_ANCHOR_CALL[24];
extern const uint8_t MSK_MP_ENEMY_TICK_ANCHOR_CALL[24];

/* Both heads are push/mov/sub, so the first instruction boundary at or past five is six. That
 * they agree is a coincidence of two ordinary frames and not a rule to copy from one to the
 * other: the neighbouring fade at 0x004393D0 opens with a push of an immediate and takes eight. */
#define AI_START_EMITTER_PROLOGUE   6u
#define ENEMY_DETACH_PIECE_PROLOGUE 6u
#define WITHIN_RANGE_PROLOGUE       6u
/* push ebp; mov ebp,esp; push ecx; mov eax,[ebp+8]: the first boundary past five is 7,
 * and the three share it because they share their head. */
#define SWITCH_ARM_PROLOGUE         7u
/* push ebp; mov ebp,esp; sub esp,7Ch: the boundary at six, and no relative operand in it. */
#define SHOT_SHATTER_THING_PROLOGUE 6u

/* The three calls of the live player test the world anchor reads, each as an offset into the
 * pattern that found its site, where the byte is the call's own `E8`. Two are redirected: the
 * activation scan's, right behind its six byte prologue, and the entity loop's, where its own
 * pattern begins. The third, the burst's, is only read, so that three calls naming one function
 * vouch for it. */
#define ENEMY_SCAN_ANCHOR_CALL         6u
#define ENEMY_TICK_ANCHOR_CALL         0u
#define SHOT_SHATTER_THING_ANCHOR_CALL 15u

/* The activation scan's head, whose call at ENEMY_SCAN_ANCHOR_CALL the anchor repoints. It lives in
 * the first site table and is named here because the declaration of that redirect, which stands
 * beside the entity loop's, needs the pattern's size. */
extern const uint8_t SIG_ENEMY_ACTIVATION_SCAN[20];

/* Two call sites, each with the distance from its first byte to the instruction after its call,
 * which is the address the callee returns to. */
extern const uint8_t SIG_MP_FACING_TEST_RESOLVE[38];
extern const uint8_t MSK_MP_FACING_TEST_RESOLVE[38];
#define FACING_TEST_RESOLVE_RETURN 24u

extern const uint8_t SIG_MP_WARP_RESPAWN_CALL[31];
extern const uint8_t MSK_MP_WARP_RESPAWN_CALL[31];
#define WARP_RESPAWN_CALL_RETURN 28u

#endif /* MULTIPLAYER_MP_SIGNATURES_ENEMY_H */
