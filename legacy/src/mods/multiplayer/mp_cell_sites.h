/* mp_cell_sites.h: the data anchor patterns, declared for the one table that carries them.
 *
 * These are the sites that exist so a cell can be read out of an operand instead of written down
 * as a number; none of them is a detour target. They live in their own translation unit because
 * the pattern file hit the hard size limit, and this was the seam its own size note had named all
 * along. The array sizes are part of the declarations on purpose: the table's entry macros take
 * sizeof, and a definition that drifts from its declared size is a compile error in the defining
 * file rather than a wrong length at run time.
 */
#ifndef MULTIPLAYER_MP_CELL_SITES_H
#define MULTIPLAYER_MP_CELL_SITES_H

#include <stdint.h>

extern const uint8_t SIG_DEATH_LATCH_SET[24];
extern const uint8_t MSK_DEATH_LATCH_SET[24];
extern const uint8_t SIG_STATUS_POINTER_WRITE[15];
extern const uint8_t MSK_STATUS_POINTER_WRITE[15];
extern const uint8_t SIG_OBJ_LIST_WALK[37];
extern const uint8_t MSK_OBJ_LIST_WALK[37];
extern const uint8_t SIG_PLR_ATTACK_HOLD[11];
extern const uint8_t MSK_PLR_ATTACK_HOLD[11];
extern const uint8_t SIG_CONTACT_SLOT_STORE[15];
extern const uint8_t MSK_CONTACT_SLOT_STORE[15];
extern const uint8_t SIG_POST_CONTACT[57];
extern const uint8_t MSK_POST_CONTACT[57];
extern const uint8_t SIG_CONTACT_NODE_READ[44];
extern const uint8_t MSK_CONTACT_NODE_READ[44];
extern const uint8_t SIG_DEATH_DESC_STORE[38];
extern const uint8_t MSK_DEATH_DESC_STORE[38];
extern const uint8_t SIG_MP_TASK_COUNT[38];
extern const uint8_t MSK_MP_TASK_COUNT[38];
extern const uint8_t SIG_CLOCK_TICKS[24];
extern const uint8_t MSK_CLOCK_TICKS[24];
extern const uint8_t SIG_MODULE_LIST_LINK[66];
extern const uint8_t MSK_MODULE_LIST_LINK[66];
extern const uint8_t SIG_TASK_STAGING[32];
extern const uint8_t MSK_TASK_STAGING[32];
extern const uint8_t SIG_SUBSTEP_ALPHA_STORE[10];
extern const uint8_t MSK_SUBSTEP_ALPHA_STORE[10];
extern const uint8_t SIG_SUBSTEP_RATE_SWITCH[19];
extern const uint8_t MSK_SUBSTEP_RATE_SWITCH[19];
extern const uint8_t SIG_FRAME_CAP_RELEASE[16];
extern const uint8_t MSK_FRAME_CAP_RELEASE[16];
extern const uint8_t SIG_VICTIM_IS_PLAYER_SET[12];
extern const uint8_t MSK_VICTIM_IS_PLAYER_SET[12];
extern const uint8_t SIG_VICTIM_IS_PLAYER_TEST[9];
extern const uint8_t MSK_VICTIM_IS_PLAYER_TEST[9];
extern const uint8_t SIG_WKERNEL_CREATE[48];
extern const uint8_t MSK_WKERNEL_CREATE[48];
extern const uint8_t SIG_MP_SHOT_KIND_REMAP[58];
extern const uint8_t MSK_MP_SHOT_KIND_REMAP[58];

extern const uint8_t SIG_ENEMY_LIVE_PEAK[30];
extern const uint8_t MSK_ENEMY_LIVE_PEAK[30];
extern const uint8_t SIG_DETAIL_LEVEL_TICK[22];
extern const uint8_t MSK_DETAIL_LEVEL_TICK[22];
extern const uint8_t SIG_DETAIL_LEVEL_SCAN[20];
extern const uint8_t MSK_DETAIL_LEVEL_SCAN[20];

/* The world scratchpad. Thirteen sites for five cells: seven name the campaign bank, four name
 * its checkpoint, and six name all three blackboard arrays at once. */
extern const uint8_t SIG_STORY_SAVE_PAIR[36];
extern const uint8_t MSK_STORY_SAVE_PAIR[36];
extern const uint8_t SIG_STORY_RESTORE_PAIR[52];
extern const uint8_t MSK_STORY_RESTORE_PAIR[52];
extern const uint8_t SIG_STORY_COPY_PAIR[55];
extern const uint8_t MSK_STORY_COPY_PAIR[55];
extern const uint8_t SIG_STORY_CLEAR[16];
extern const uint8_t MSK_STORY_CLEAR[16];
extern const uint8_t SIG_STORY_OP_SET_GLOB[34];
extern const uint8_t MSK_STORY_OP_SET_GLOB[34];
extern const uint8_t SIG_STORY_KEY_USE[41];
extern const uint8_t MSK_STORY_KEY_USE[41];
extern const uint8_t SIG_STORY_PAUSE_INV[42];
extern const uint8_t MSK_STORY_PAUSE_INV[42];
extern const uint8_t SIG_AIFLAG_SAVE[60];
extern const uint8_t MSK_AIFLAG_SAVE[60];
extern const uint8_t SIG_AIFLAG_RESTORE[57];
extern const uint8_t MSK_AIFLAG_RESTORE[57];
extern const uint8_t SIG_AIFLAG_SET_TIMED[76];
extern const uint8_t MSK_AIFLAG_SET_TIMED[76];
extern const uint8_t SIG_AIFLAG_EXPIRE[59];
extern const uint8_t MSK_AIFLAG_EXPIRE[59];
extern const uint8_t SIG_AIFLAG_INIT_CLEAR[74];
extern const uint8_t MSK_AIFLAG_INIT_CLEAR[74];
extern const uint8_t SIG_AIFLAG_CLOSE_CLEAR[74];
extern const uint8_t MSK_AIFLAG_CLOSE_CLEAR[74];

extern const uint8_t SIG_ENEMY_ON_CONTACT[35];
extern const uint8_t MSK_ENEMY_ON_CONTACT[35];

/* The enemy pool. Two sites; the second exists so the pool is named by two independently
 * compiled runs of code rather than once. */
extern const uint8_t SIG_ENEMY_POOL_NEW[41];
extern const uint8_t MSK_ENEMY_POOL_NEW[41];
extern const uint8_t SIG_ENEMY_POOL_WALK[46];
extern const uint8_t MSK_ENEMY_POOL_WALK[46];

#endif /* MULTIPLAYER_MP_CELL_SITES_H */
