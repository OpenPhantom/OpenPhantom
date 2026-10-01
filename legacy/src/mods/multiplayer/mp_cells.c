/* mp_cells.c: read every data cell out of the operand of a resolved site.
 *
 * The alternative was a table of numbers, and the numbers would have been wrong in two ways that
 * both fail quietly. They are wrong on any build but the retail one, where the recompile puts the
 * same data 0x50 lower. And they go stale the moment another module in this tree relocates a table
 * on purpose, which large_textures and view_distance_fix both do: the operand then names the new
 * home and a written down number names the old one.
 *
 * signature_read_address_operand does the deciding about which copy of the bytes to believe when
 * somebody has already written a branch over a site, so this file never reads memory itself.
 */
#include "mp_cells.h"

#include "common/logging.h"
#include "common/memory.h"
#include "common/signature.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Where each cell is named. A cell with more than one row is proven rather than looked up: every
 * row has to answer, and they all have to answer the same thing.
 *
 * What the first rows read on the retail image, with the instruction the operand sits in and, in
 * brackets, how often the cell's dword occurs in the code section at every alignment: pr is
 * 0x004B5220, out of the phase runner's `8B 0D` load at +0x27 and the death latch's `A1` at
 * +0x0B (1668); the phase table 0x004B5228 out of the runner's `83 3C 85` compare at +0x1A; the
 * status pointer 0x0086D57C out of the `89 0D` store at +0x0B of the status pointer write, and
 * the array base 0x0086D5A0 out of the `81 C1` add five bytes in front of it (104 and 7); the
 * level outcome 0x00881368 out of the death latch's `C7 05` store of 4 at +0x02 (18); the enemy
 * suspend 0x006C4D9C out of the enemy tick's `83 3D` at +0x2D (5); the use latch 0x006C4DAC out
 * of the `C7 05` that stores 3; the object list 0x008A01DC out of the walk's `A1` at +0x01 and
 * `8B 0D` at +0x10 (29); the task service 0x00868724 out of five loads across two sites; the
 * substep alpha 0x0086871C out of the `D9 1D` store (4); the rate switch 0x00882294 (2) and the
 * frame delta 0x00868714 out of one site, the delta again out of the frame cap release's `D9 05`
 * and the release itself 0x004B7D78 (1); the victim flag 0x006CFAB8 out of its `89 15` store and
 * its `83 3D` test (2); the mover freeze 0x005B5FCC; the hero block 0x006CF640 out of the spawn's
 * `BF` immediate at +0x0F; the difficulty 0x00872FA0 out of the impact lookup's `A1` (15, the
 * damage lookup among them); the input devices 0x006D3758 out of the axis reader's `8B 88`.
 *
 * Five of those only became readable when the required operands in their patterns were made
 * wildcards: the use latch, the mover freeze, the hero block, the difficulty and the task service.
 * With the operand pinned there was nothing to read that the pattern had not already decided. */
static const mp_operand_t mp_operand_table[] = {
    { MP_CELL_PR,                  MP_SITE_PLR_RUN_PHASES,        0x27 },
    { MP_CELL_PR,                  MP_SITE_DEATH_LATCH_SET,       0x0B },
    { MP_CELL_PHASE_TABLE,         MP_SITE_PLR_RUN_PHASES,        0x1A },
    /* The pointer and the array base are five bytes apart in one instruction pair inside the
     * active player setter, and writing to the base instead of the pointer writes into hero 0's
     * record, which is why both are carried:
     *
     *   00459BBA  6B C9 4C            imul ecx, ecx, 0x4C      the record stride
     *   00459BBD  81 C1 A0 D5 86 00   add  ecx, 0x0086D5A0     the array base
     *   00459BC3  89 0D 7C D5 86 00   mov  [0x0086D57C], ecx   the pointer
     *
     * The cell has two writers: this one, and a `C7 05 <cell> 00000000` at 0x00458BB2 that the
     * new game arm clears it with. A census by the load and store opcode buckets missed the
     * second, because an immediate store is neither; a census is only as complete as its opcode
     * list. The setter's own entry at 0x00459AB7 is a different site from this anchor: it is the
     * detour target and carries the index range test that walls the fourth status slot, whose
     * assert handler is null in the retail build. */
    { MP_CELL_PLR_STATUS_POINTER,  MP_SITE_STATUS_POINTER_WRITE,  0x0B },
    { MP_CELL_PLR_STATUS_ARRAY,    MP_SITE_STATUS_POINTER_WRITE,  0x05 },
    { MP_CELL_LEVEL_OUTCOME,       MP_SITE_DEATH_LATCH_SET,       0x02 },
    { MP_CELL_ENEMY_SUSPEND,       MP_SITE_ENEMY_TICK_ALL,        0x2D },
    { MP_CELL_ENEMY_LIVE,          MP_SITE_ENEMY_LIVE_PEAK,       0x01 },
    { MP_CELL_ENEMY_LIVE,          MP_SITE_ENEMY_LIVE_PEAK,       0x0F },
    { MP_CELL_ENEMY_LIVE_PEAK,     MP_SITE_ENEMY_LIVE_PEAK,       0x07 },
    { MP_CELL_ENEMY_LIVE_PEAK,     MP_SITE_ENEMY_LIVE_PEAK,       0x15 },
    { MP_CELL_DETAIL_LEVEL,        MP_SITE_DETAIL_LEVEL_TICK,     0x08 },
    { MP_CELL_DETAIL_LEVEL,        MP_SITE_DETAIL_LEVEL_SCAN,     0x0A },
    { MP_CELL_TITLE_WIDGETS,       MP_SITE_TITLE_MAIN_MENU,       0x2C },
    { MP_CELL_MENU_FONTS,          MP_SITE_TITLE_MAIN_MENU,       0x31 },
    { MP_CELL_TITLE_BITMAPS,       MP_SITE_TITLE_MAIN_MENU,       0x38 },
    { MP_CELL_TITLE_SCREEN,        MP_SITE_TITLE_MAIN_MENU,       0x3D },
    /* All three moved by ten on 2026-09-07 when the pattern grew a ten byte prefix in front,
     * so that the restore flag has an operand of its own to be read out of. */
    { MP_CELL_RESTORE_PENDING,     MP_SITE_CAMPAIGN_ROUND,        0x02 },
    { MP_CELL_START_LEVEL,         MP_SITE_CAMPAIGN_ROUND,        0x0B },
    { MP_CELL_LEVEL_INDEX,         MP_SITE_CAMPAIGN_ROUND,        0x10 },
    { MP_CELL_LEVEL_INDEX,         MP_SITE_CAMPAIGN_LOAD,         0x0B },
    { MP_CELL_LEVEL_INDEX,         MP_SITE_CAMPAIGN_LOAD,         0x2A },
    { MP_CELL_LOAD_BY_NAME,        MP_SITE_CAMPAIGN_LOAD,         0x02 },
    { MP_CELL_LOAD_NAME,           MP_SITE_CAMPAIGN_LOAD,         0x14 },
    { MP_CELL_LEVEL_TABLE,         MP_SITE_CAMPAIGN_LOAD,         0x33 },
    { MP_CELL_GAME_MODE,           MP_SITE_LEVEL_HANDOVER,        0x1B },
    { MP_CELL_DATA_ROOT,           MP_SITE_PATH_PREFIX,           0x07 },
    /* The teleport names the player record twice, once for the ground contact block it clears and
     * once for the position it seats. Two more witnesses for a cell that already has several, and
     * they are also what says the teleport pattern landed on the function it was cut from: a match
     * elsewhere would disagree with the rest and the cell would be refused rather than believed. */
    { MP_CELL_PR,                  MP_SITE_PLAYER_TELEPORT,       0x0D },
    { MP_CELL_PR,                  MP_SITE_PLAYER_TELEPORT,       0x1A },
    { MP_CELL_CURRENT_MENU,        MP_SITE_SWMENU_CLOSE,          0x04 },
    { MP_CELL_CURRENT_MENU,        MP_SITE_SWMENU_CLOSE,          0x13 },
    { MP_CELL_USE_LATCH,           MP_SITE_ENEMY_LATCH_USE,       0x05 },
    { MP_CELL_DIALOG_CONFIRM,      MP_SITE_ENEMY_LATCH_USE,       0x16 },
    { MP_CELL_OBJ_LIST,            MP_SITE_OBJ_LIST_WALK,         0x01 },
    { MP_CELL_OBJ_LIST,            MP_SITE_OBJ_LIST_WALK,         0x10 },
    { MP_CELL_TASK_SERVICE,        MP_SITE_PLAYER_TICK_TASK,      0x05 },
    { MP_CELL_TASK_SERVICE,        MP_SITE_PLAYER_TICK_TASK,      0x11 },
    { MP_CELL_TASK_SERVICE,        MP_SITE_ENEMY_TICK_ALL,        0x07 },
    { MP_CELL_TASK_SERVICE,        MP_SITE_ENEMY_TICK_ALL,        0x13 },
    { MP_CELL_TASK_SERVICE,        MP_SITE_ENEMY_TICK_ALL,        0x20 },
    { MP_CELL_SUBSTEP_ALPHA,       MP_SITE_SUBSTEP_ALPHA_STORE,   0x02 },
    /* The three weight cells of the draw, out of the world draw's frame set up:
     *
     *   00411063  A1 1C 87 86 00          mov eax, [weight]      the live one
     *   0041106E  83 3D E8 AC 5B 00 00    cmp dword [gate], 0    set while the simulation is held
     *   00411077  8B 0D E4 AC 5B 00       mov ecx, [frozen]      what the draw uses then
     *
     * The alpha now has two operands from two functions and they agree on all six shipped
     * executables: the retail five answer 0086871C on both sides and the recompile 008686BC,
     * with the gate and the frozen weight at 005BACE8 and 005BACE4 there against 005BAC98 and
     * 005BAC94 on the recompile. A wrong offset would have produced a disagreement rather than a
     * plausible number. Reading the live weight alone would be wrong on every frame where the
     * simulation was held and the draw kept running: a body drawn on it would keep interpolating
     * through a pause. */
    { MP_CELL_SUBSTEP_ALPHA,       MP_SITE_BAPOBJ_DRAW_ALL,       0x3C },
    { MP_CELL_DRAW_WEIGHT_GATE,    MP_SITE_BAPOBJ_DRAW_ALL,       0x48 },
    { MP_CELL_DRAW_WEIGHT_FROZEN,  MP_SITE_BAPOBJ_DRAW_ALL,       0x51 },
    { MP_CELL_SUBSTEP_RATE_SWITCH, MP_SITE_SUBSTEP_RATE_SWITCH,   0x02 },
    { MP_CELL_FRAME_DELTA,         MP_SITE_SUBSTEP_RATE_SWITCH,   0x0B },
    { MP_CELL_FRAME_DELTA,         MP_SITE_FRAME_CAP_RELEASE,     0x0B },
    { MP_CELL_FRAME_CAP_RELEASE,   MP_SITE_FRAME_CAP_RELEASE,     0x02 },
    { MP_CELL_VICTIM_IS_PLAYER,    MP_SITE_VICTIM_IS_PLAYER_SET,  0x02 },
    { MP_CELL_VICTIM_IS_PLAYER,    MP_SITE_VICTIM_IS_PLAYER_TEST, 0x02 },
    { MP_CELL_MOVER_FREEZE,        MP_SITE_BAPMAP_TICK_MOVER,     0x08 },
    /* The world cell, 0x008A0060 on the retail build, out of three operands of the mover tick at
     * 0x0040A7D1: the `83 3D` compare at +0x08, the `8B 0D` load at +0x2E that is followed by a
     * compare against [ecx+0x620], and the `8B 0D` load at +0x42 that is followed by
     * `mov edx, [ecx+eax*4+0x624]`. That last one is one indirection, so the mover table is
     * inline in the record; the reconstruction declares it as a pointer to pointers, and code
     * that followed the declaration would dereference one level too many. Three rows rather than
     * one because any one could be a coincidence of the pattern and three that disagree poison
     * the cell and say so. */
    { MP_CELL_LEVEL,               MP_SITE_BAPMAP_TICK_MOVERS,    0x08 },
    { MP_CELL_LEVEL,               MP_SITE_BAPMAP_TICK_MOVERS,    0x2E },
    { MP_CELL_LEVEL,               MP_SITE_BAPMAP_TICK_MOVERS,    0x42 },
    { MP_CELL_HERO_BLOCK,          MP_SITE_PLAYER_SPAWN_HERO,     0x0F },
    { MP_CELL_IMPACT_DIFFICULTY,   MP_SITE_IMPACT_LOOKUP,         0x05 },
    { MP_CELL_INPUT_DEVICES,       MP_SITE_INPUT_AXIS,            0x18 },
    { MP_CELL_MODULE_TAIL,         MP_SITE_MODULE_LIST_LINK,      0x05 },
    { MP_CELL_MODULE_TAIL,         MP_SITE_MODULE_LIST_LINK,      0x17 },
    { MP_CELL_MODULE_TAIL,         MP_SITE_MODULE_LIST_LINK,      0x1F },
    { MP_CELL_MODULE_TAIL,         MP_SITE_MODULE_LIST_LINK,      0x2D },
    { MP_CELL_MODULE_HEAD,         MP_SITE_MODULE_LIST_LINK,      0x33 },
    { MP_CELL_MODULE_HEAD,         MP_SITE_MODULE_LIST_LINK,      0x3E },
    { MP_CELL_TASK_ARRAY,          MP_SITE_TASK_STAGING,          0x08 },
    { MP_CELL_TASK_STAGING,        MP_SITE_TASK_STAGING,          0x12 },
    { MP_CELL_TASK_STAGING,        MP_SITE_TASK_STAGING,          0x1C },
    { MP_CELL_CLOCK_TICKS,         MP_SITE_CLOCK_TICKS,           0x02 },
    { MP_CELL_CLOCK_TICKS,         MP_SITE_CLOCK_TICKS,           0x0B },
    { MP_CELL_OBJ_LIST,            MP_SITE_THING_ALLOC,           0x05 },
    { MP_CELL_NUM_TASKS,           MP_SITE_TASK_COUNT,            0x10 },
    { MP_CELL_ATTACK_HOLD_ACCUM,   MP_SITE_PLR_ATTACK_HOLD,       0x04 },
    { MP_CELL_PR,                  MP_SITE_PLAYER_SAVE,           0x05 },
    { MP_CELL_PR,                  MP_SITE_PLAYER_DESPAWN,        0x04 },
    { MP_CELL_PLAYER_TASK_NODE,    MP_SITE_CONTACT_SLOT_STORE,    0x04 },
    /* The six globals the contact poster at 0x00414C99 publishes are one straight run of stores
     * at its head: self at +0x07, other at +0x10, code at +0x19, a at +0x21 (an `A3` store,
     * 0x0086924C on retail), b at +0x2A (an `89 0D` store, 0x00869250), and the sender's impact
     * code at +0x35. The pattern used to stop at 29 bytes, one store short of the fourth global,
     * and was lengthened to 57 rather than given a second anchor; it still matches exactly once
     * on all six images. b is the fork the effect layer stands on, the message a handler answers
     * with a spark rather than with damage; a is set when a weapon met a weapon. */
    { MP_CELL_MSG_SELF,            MP_SITE_POST_CONTACT,          0x07 },
    { MP_CELL_MSG_IMPACT,          MP_SITE_POST_CONTACT,          0x35 },
    { MP_CELL_MSG_OTHER,           MP_SITE_POST_CONTACT,          0x10 },
    { MP_CELL_MSG_CODE,            MP_SITE_POST_CONTACT,          0x19 },
    { MP_CELL_MSG_A,               MP_SITE_POST_CONTACT,          0x21 },
    { MP_CELL_MSG_B,               MP_SITE_POST_CONTACT,          0x2A },
    /* The seventh contact cell is not an argument of the message and is therefore not named by
     * post_contact at all: the pair pass writes it and the player's receiver reads it. The read is
     * the only operand there is, and it names the player record in the same run, which is what
     * says the pattern landed in the receiver rather than somewhere that merely looks like it. */
    { MP_CELL_MSG_CONTACT_NODE,    MP_SITE_CONTACT_NODE_READ,     0x08 },
    /* The hurt voice's wav, from the one instruction that loads it. The site is an anchor
     * and nothing else: it is never detoured. */
    { MP_CELL_HURT_VOICE_NAME,     MP_SITE_HURT_VOICE_READ,       0x04 },
    { MP_CELL_FOOT_MODULE,         MP_SITE_FOOTSTEP_TICK,         0x0A },
    { MP_CELL_CURRENT_PLAYER,      MP_SITE_FOOT_RUN,              0x07 },
    { MP_CELL_PR,                  MP_SITE_CONTACT_NODE_READ,     0x1F },
    { MP_CELL_PR,                  MP_SITE_DEATH_DESC_STORE,      0x0C },
    { MP_CELL_PR,                  MP_SITE_DEATH_DESC_STORE,      0x19 },
    { MP_CELL_MODE_DEATH_DESC,     MP_SITE_DEATH_DESC_STORE,      0x13 },
    { MP_CELL_MODE_STAND_DESC,     MP_SITE_PLR_ENTER_STAND,       0x0B },
    { MP_CELL_WKERNEL_CLASS,       MP_SITE_WKERNEL_CREATE,        0x09 },
    /* The swing starter reads its table five times and only the last reading, the `83 B9`
     * compare of [ecx+table] against 0x55 at +0xF7, names the base (0x004B4E00 on retail): the
     * others name a column, the node at +8, the radius at +0xC, the impact at +0x10 and the
     * direction class at +0x14, and would have to be corrected by the column offset, which a
     * cell never is because a cell is an operand's value and nothing else. The player pointer
     * is read from the same site at +0x20, behind the eight byte prologue the dev overlay's
     * branch overwrites, never from the `A1` at +0x03: with that DLL loaded first the bytes at
     * +0x04 are its jump distance. The recompile puts both cells 0x50 lower, at 0x004B4DB0 and
     * 0x004B51D0, read out of its own operands. */
    { MP_CELL_PR,                  MP_SITE_PLR_START_SWING,       0x20 },
    { MP_CELL_SWING_TABLE,         MP_SITE_PLR_START_SWING,       0xF7 },
    /* The two continuations are code addresses, not data: 0x0044BC44 and 0x0044BCA0 are what
     * the engine's own deflect and parry write into pr+0x64, and what the armed contact compares
     * that slot against with `81 78 64` at +0x0E and `81 7A 64` at +0x39. The aux pointer table
     * at 0x004B54F0 carries the same two in its first two slots and was not made a site, because
     * one anchor that names both is enough. */
    { MP_CELL_PR,                  MP_SITE_PLR_ARMED_CONTACT,     0x07 },
    { MP_CELL_BLOCK_SHOT_AUX,      MP_SITE_PLR_ARMED_CONTACT,     0x0E },
    { MP_CELL_BLOCK_ATTACK_AUX,    MP_SITE_PLR_ARMED_CONTACT,     0x39 },
    /* The happy cheat, 0x008822A4, has three references: its test in the shot spawner
     * (0x00453CF6), a second deeper in the same function (0x00453ED3) and one in the shot tick
     * (0x004565CD). The evil force cheat, 0x00882290, is tested once, at the remap. The remap is
     * a data anchor inside the spawner, 0xFC bytes behind its head:
     *
     *   00453DCE  83 3D [90 22 88 00] 00     cmp g_cheatEvilForce, 0            (+0x02)
     *   00453DD5  74 0D 83 7D 08 0B 75 07    kind == 11?
     *   00453DDD  C7 45 08 12 00 00 00       kind = 18
     *   00453DEA  83 7D FC 00 72 06 ...      range check against the 38 rows
     *   00453DFD  8B 45 FC 6B C0 44          row * 0x44
     *   00453E03  05 [10 57 4B 00]           + the shot table                   (+0x36)
     *
     * One match on all six images, the recompile at 0x00453D6E. The `6B C0 44` stride is a
     * required byte run: it is what says this is the shot table and not another range checked
     * lookup. The table, 0x004B5710 on retail, has six references: the init's two loops
     * (0x00452332, 0x004523CF), the teardown (0x0045247B), this site, the contact handler
     * (0x00454C6E) and the tick (0x004554BC). */
    { MP_CELL_CHEAT_HAPPY,         MP_SITE_SHOT_SPAWN,            0x24 },
    { MP_CELL_CHEAT_EVIL_FORCE,    MP_SITE_SHOT_KIND_REMAP,       0x02 },
    { MP_CELL_SHOT_TABLE,          MP_SITE_SHOT_KIND_REMAP,       0x36 },

    /* The world scratchpad. Ten rows for the living bank across six sites, and one of those sites
     * is in a third compiland, which is what makes their agreement mean anything: the other nine
     * could all be wrong together if a whole object file had been relocated. */
    { MP_CELL_STORY_FLAGS,         MP_SITE_STORY_SAVE_PAIR,       0x06 },
    { MP_CELL_STORY_FLAGS,         MP_SITE_STORY_RESTORE_PAIR,    0x06 },
    { MP_CELL_STORY_FLAGS,         MP_SITE_STORY_COPY_PAIR,       0x06 },
    { MP_CELL_STORY_FLAGS,         MP_SITE_STORY_COPY_PAIR,       0x29 },
    { MP_CELL_STORY_FLAGS,         MP_SITE_STORY_CLEAR,           0x03 },
    { MP_CELL_STORY_FLAGS,         MP_SITE_STORY_OP_SET_GLOB,     0x09 },
    { MP_CELL_STORY_FLAGS,         MP_SITE_STORY_OP_SET_GLOB,     0x19 },
    { MP_CELL_STORY_FLAGS,         MP_SITE_STORY_KEY_USE,         0x17 },
    { MP_CELL_STORY_FLAGS,         MP_SITE_STORY_KEY_USE,         0x25 },
    { MP_CELL_STORY_FLAGS,         MP_SITE_STORY_PAUSE_INV,       0x0D },

    /* The checkpoint has four operands and all four are in one compiland, because the whole image
     * names this cell four times and no more. That is a real limit on how well it can be proven,
     * and it is stated here rather than left to be inferred from the row count. */
    { MP_CELL_STORY_CHECKPOINT,    MP_SITE_STORY_SAVE_PAIR,       0x18 },
    { MP_CELL_STORY_CHECKPOINT,    MP_SITE_STORY_RESTORE_PAIR,    0x26 },
    { MP_CELL_STORY_CHECKPOINT,    MP_SITE_STORY_COPY_PAIR,       0x0B },
    { MP_CELL_STORY_CHECKPOINT,    MP_SITE_STORY_COPY_PAIR,       0x24 },

    /* The blackboard. Six sites name all three arrays, so each cell gets a sixfold agreement
     * rather than the twofold this table normally asks for. That is not thoroughness for its own
     * sake: these three cells are the ones whose addresses TRADE PLACES between shipped images. */
    { MP_CELL_AI_FLAG,             MP_SITE_AIFLAG_SAVE,           0x09 },
    { MP_CELL_AI_FLAG,             MP_SITE_AIFLAG_RESTORE,        0x0D },
    { MP_CELL_AI_FLAG,             MP_SITE_AIFLAG_SET_TIMED,      0x1D },
    { MP_CELL_AI_FLAG,             MP_SITE_AIFLAG_SET_TIMED,      0x31 },
    { MP_CELL_AI_FLAG,             MP_SITE_AIFLAG_EXPIRE,         0x37 },
    { MP_CELL_AI_FLAG,             MP_SITE_AIFLAG_INIT_CLEAR,     0x04 },
    { MP_CELL_AI_FLAG,             MP_SITE_AIFLAG_CLOSE_CLEAR,    0x04 },
    { MP_CELL_AI_FLAG_PREV,        MP_SITE_AIFLAG_SAVE,           0x1D },
    { MP_CELL_AI_FLAG_PREV,        MP_SITE_AIFLAG_RESTORE,        0x21 },
    { MP_CELL_AI_FLAG_PREV,        MP_SITE_AIFLAG_SET_TIMED,      0x24 },
    { MP_CELL_AI_FLAG_PREV,        MP_SITE_AIFLAG_EXPIRE,         0x30 },
    { MP_CELL_AI_FLAG_PREV,        MP_SITE_AIFLAG_INIT_CLEAR,     0x34 },
    { MP_CELL_AI_FLAG_PREV,        MP_SITE_AIFLAG_CLOSE_CLEAR,    0x37 },
    { MP_CELL_AI_FLAG_EXPIRY,      MP_SITE_AIFLAG_SAVE,           0x31 },
    { MP_CELL_AI_FLAG_EXPIRY,      MP_SITE_AIFLAG_RESTORE,        0x35 },
    { MP_CELL_AI_FLAG_EXPIRY,      MP_SITE_AIFLAG_SET_TIMED,      0x03 },
    { MP_CELL_AI_FLAG_EXPIRY,      MP_SITE_AIFLAG_SET_TIMED,      0x46 },
    { MP_CELL_AI_FLAG_EXPIRY,      MP_SITE_AIFLAG_EXPIRE,         0x0B },
    { MP_CELL_AI_FLAG_EXPIRY,      MP_SITE_AIFLAG_EXPIRE,         0x1F },
    { MP_CELL_AI_FLAG_EXPIRY,      MP_SITE_AIFLAG_INIT_CLEAR,     0x1D },
    { MP_CELL_AI_FLAG_EXPIRY,      MP_SITE_AIFLAG_CLOSE_CLEAR,    0x1E },

    /* Two more rows for the level, from the two sites that measure an expiry against its clock.
     * They are here because a cell already resolved elsewhere gaining two independent witnesses
     * costs nothing and closes the question of whether the clock those sites use is that one. */
    { MP_CELL_LEVEL,               MP_SITE_AIFLAG_SET_TIMED,      0x36 },
    { MP_CELL_LEVEL,               MP_SITE_AIFLAG_EXPIRE,         0x04 },

    /* The enemy pool. The creation site names all three cells in one run; the walk names the pool
     * a second time and the live count a third, so neither rests on one compiland. */
    { MP_CELL_ENEMY_TASK_NODE,     MP_SITE_ENEMY_POOL_NEW,        0x04 },
    { MP_CELL_ENEMY_POOL,          MP_SITE_ENEMY_POOL_NEW,        0x1B },
    { MP_CELL_ENEMY_TICK_WAIT,     MP_SITE_ENEMY_POOL_NEW,        0x21 },
    { MP_CELL_ENEMY_POOL,          MP_SITE_ENEMY_POOL_WALK,       0x01 },
    { MP_CELL_ENEMY_POOL,          MP_SITE_ENEMY_POOL_WALK,       0x1A },
    { MP_CELL_ENEMY_LIVE,          MP_SITE_ENEMY_POOL_WALK,       0x10 },

    /* The clang's cooldown, from the `fld` that tests it. It has one witness: its only other
     * operand is the `fstp` that re-arms it, past the end of the pattern, and a row may only name
     * an operand its own pattern holds masked. The pattern itself proves the read is the cooldown
     * test, because the compare against the world clock after it is part of what it matches. */
    { MP_CELL_BLOCK_FX_COOLDOWN,   MP_SITE_NPC_BLOCK_IMPACT_FX,   0x0D }
};

/* Designated by the enum value, not by position. The first version was a bare list and it had
 * drifted: eight cells were added to the enum without a name, so every name from the sixth on
 * belonged to a different cell and the last eight were NULL. A NULL through a %s is a visible
 * failure, and a wrong name is worse than none because it reads as an answer. Written this way a
 * missing entry can only be NULL, never a shift, and the test walks the table for both.
 */
static const char *const mp_cell_names[MP_CELL_COUNT] = {
    [MP_CELL_PR]                  = "pr",
    [MP_CELL_PHASE_TABLE]         = "phase_table",
    [MP_CELL_PLR_STATUS_POINTER]  = "plr_status_pointer",
    [MP_CELL_PLR_STATUS_ARRAY]    = "plr_status_array",
    [MP_CELL_LEVEL_OUTCOME]       = "level_outcome",
    [MP_CELL_ENEMY_SUSPEND]       = "enemy_suspend",
    [MP_CELL_ENEMY_LIVE]          = "enemy_live",
    [MP_CELL_ENEMY_LIVE_PEAK]     = "enemy_live_peak",
    [MP_CELL_DETAIL_LEVEL]        = "detail_level",
    [MP_CELL_TITLE_WIDGETS]       = "title_widgets",
    [MP_CELL_TITLE_BITMAPS]       = "title_bitmaps",
    [MP_CELL_MENU_FONTS]          = "menu_fonts",
    [MP_CELL_TITLE_SCREEN]        = "title_screen",
    [MP_CELL_CURRENT_MENU]        = "current_menu",
    [MP_CELL_START_LEVEL]         = "start_level",
    [MP_CELL_LEVEL_INDEX]         = "level_index",
    [MP_CELL_LOAD_BY_NAME]        = "load_by_name",
    [MP_CELL_LOAD_NAME]           = "load_name",
    [MP_CELL_LEVEL_TABLE]         = "level_table",
    [MP_CELL_GAME_MODE]           = "game_mode",
    [MP_CELL_RESTORE_PENDING]     = "restore_pending",
    [MP_CELL_DATA_ROOT]           = "data_root",
    [MP_CELL_USE_LATCH]           = "use_latch",
    [MP_CELL_DIALOG_CONFIRM]      = "dialog_confirm",
    [MP_CELL_OBJ_LIST]            = "obj_list",
    [MP_CELL_TASK_SERVICE]        = "task_service",
    [MP_CELL_SUBSTEP_ALPHA]       = "substep_alpha",
    [MP_CELL_SUBSTEP_RATE_SWITCH] = "substep_rate_switch",
    [MP_CELL_FRAME_DELTA]         = "frame_delta",
    [MP_CELL_FRAME_CAP_RELEASE]   = "frame_cap_release",
    [MP_CELL_VICTIM_IS_PLAYER]    = "victim_is_player",
    [MP_CELL_MOVER_FREEZE]        = "mover_freeze",
    [MP_CELL_LEVEL]               = "level",
    [MP_CELL_HERO_BLOCK]          = "hero_block",
    [MP_CELL_IMPACT_DIFFICULTY]   = "impact_difficulty",
    [MP_CELL_INPUT_DEVICES]       = "input_devices",
    [MP_CELL_MODULE_HEAD]         = "module_head",
    [MP_CELL_MODULE_TAIL]         = "module_tail",
    [MP_CELL_TASK_ARRAY]          = "task_array",
    [MP_CELL_TASK_STAGING]        = "task_staging",
    [MP_CELL_CLOCK_TICKS]         = "clock_ticks",
    [MP_CELL_NUM_TASKS]           = "num_tasks",
    [MP_CELL_ATTACK_HOLD_ACCUM]   = "attack_hold_accum",
    [MP_CELL_PLAYER_TASK_NODE]    = "player_task_node",
    [MP_CELL_MSG_SELF]            = "msg_self",
    [MP_CELL_MSG_IMPACT]          = "msg_impact",
    [MP_CELL_MSG_OTHER]           = "msg_other",
    [MP_CELL_MSG_CODE]            = "msg_code",
    [MP_CELL_MSG_A]               = "msg_a",
    [MP_CELL_MSG_B]               = "msg_b",
    [MP_CELL_MSG_CONTACT_NODE]    = "msg_contact_node",
    [MP_CELL_HURT_VOICE_NAME]     = "hurt_voice_name",
    [MP_CELL_FOOT_MODULE]         = "foot_module",
    [MP_CELL_CURRENT_PLAYER]      = "current_player",
    [MP_CELL_MODE_DEATH_DESC]     = "mode_death_desc",
    [MP_CELL_MODE_STAND_DESC]     = "mode_stand_desc",
    [MP_CELL_WKERNEL_CLASS]       = "wkernel_class",
    [MP_CELL_SWING_TABLE]         = "swing_table",
    [MP_CELL_BLOCK_SHOT_AUX]      = "block_shot_aux",
    [MP_CELL_BLOCK_ATTACK_AUX]    = "block_attack_aux",
    [MP_CELL_CHEAT_HAPPY]         = "cheat_happy",
    [MP_CELL_CHEAT_EVIL_FORCE]    = "cheat_evil_force",
    [MP_CELL_SHOT_TABLE]          = "shot_table",
    [MP_CELL_DRAW_WEIGHT_GATE]    = "draw_weight_gate",
    [MP_CELL_DRAW_WEIGHT_FROZEN]  = "draw_weight_frozen",
    [MP_CELL_STORY_FLAGS]         = "story_flags",
    [MP_CELL_STORY_CHECKPOINT]    = "story_checkpoint",
    [MP_CELL_AI_FLAG]             = "ai_flag",
    [MP_CELL_AI_FLAG_PREV]        = "ai_flag_prev",
    [MP_CELL_AI_FLAG_EXPIRY]      = "ai_flag_expiry",
    [MP_CELL_ENEMY_POOL]          = "enemy_pool",
    [MP_CELL_ENEMY_TASK_NODE]     = "enemy_task_node",
    [MP_CELL_ENEMY_TICK_WAIT]     = "enemy_tick_wait",
    [MP_CELL_BLOCK_FX_COOLDOWN]   = "block_fx_cooldown",
};

static uintptr_t mp_cell_address[MP_CELL_COUNT];

uintptr_t mp_cells_address(mp_cell_t cell)
{
    if ((size_t)cell >= MP_CELL_COUNT) {
        return 0u;
    }
    return mp_cell_address[cell];
}

const char *mp_cells_name(mp_cell_t cell)
{
    if ((size_t)cell >= MP_CELL_COUNT) {
        return NULL;
    }
    return mp_cell_names[cell];
}

const mp_operand_t *mp_cells_operands(size_t *count)
{
    if (count != NULL) {
        *count = sizeof(mp_operand_table) / sizeof(mp_operand_table[0]);
    }
    return mp_operand_table;
}

/* Reads one operand at one match. The match address is passed in rather than taken from the site,
 * because the death latch has two of them and the second one is the cross check worth making. */
static bool read_operand(const signature_t *site, uintptr_t match, size_t offset,
                         uintptr_t *address)
{
    signature_t probe;

    if (site == NULL || match == 0u) {
        return false;
    }
    probe = *site;
    probe.address = match;
    return signature_read_address_operand(&probe, offset, address);
}

/* An operand that reads but disagrees with an earlier one poisons its cell for the rest of the
 * run, which is why the refused marker is a separate value rather than a missing entry: a later
 * row must not be able to revive a cell an earlier one refused. */
#define MP_CELL_REFUSED ((uintptr_t)1u)

static void record(mp_cell_t cell, uintptr_t value)
{
    uintptr_t seen = mp_cell_address[cell];

    if (seen == MP_CELL_REFUSED) {
        return;
    }
    if (seen == 0u) {
        mp_cell_address[cell] = value;
        return;
    }
    if (seen != value) {
        log_warning("  cell %-20s disagrees: %08X against %08X, so it is refused",
                    mp_cell_names[cell], (unsigned)value, (unsigned)seen);
        mp_cell_address[cell] = MP_CELL_REFUSED;
    }
}

/* The second match of the death latch names the same two cells as the first, so reading them there
 * as well is what turns one site into evidence. */
static void read_second_death_latch(void)
{
    static const size_t    offsets[] = { 0x02, 0x0B };
    static const mp_cell_t cells[]   = { MP_CELL_LEVEL_OUTCOME, MP_CELL_PR };
    const signature_t     *site      = mp_signatures_site(MP_SITE_DEATH_LATCH_SET);
    uintptr_t              match     = mp_signatures_death_latch(1);
    size_t                 index;

    if (match == 0u) {
        return;
    }

    for (index = 0; index < sizeof(offsets) / sizeof(offsets[0]); ++index) {
        uintptr_t value = 0u;

        if (!read_operand(site, match, offsets[index], &value) || value == 0u) {
            log_warning("  cell %-20s second death latch operand did not read",
                        mp_cell_names[cells[index]]);
            mp_cell_address[cells[index]] = MP_CELL_REFUSED;
            continue;
        }
        record(cells[index], value);
    }
}

size_t mp_cells_resolve(bool log_every_cell)
{
    size_t              operand_count = 0;
    const mp_operand_t *operands      = mp_cells_operands(&operand_count);
    size_t              known         = 0;
    size_t              index;

    for (index = 0; index < MP_CELL_COUNT; ++index) {
        mp_cell_address[index] = 0u;
    }

    for (index = 0; index < operand_count; ++index) {
        const mp_operand_t *row   = &operands[index];
        const signature_t  *site  = mp_signatures_site(row->site);
        uintptr_t           value = 0u;

        if (site == NULL || site->address == 0u) {
            continue;   /* the site said so already; a second line adds nothing */
        }
        if (!read_operand(site, site->address, row->offset, &value) || value == 0u) {
            log_warning("  cell %-20s operand +%#04x of %s did not read",
                        mp_cell_names[row->cell], (unsigned)row->offset, site->name);
            mp_cell_address[row->cell] = MP_CELL_REFUSED;
            continue;
        }
        record(row->cell, value);
    }

    read_second_death_latch();

    for (index = 0; index < MP_CELL_COUNT; ++index) {
        if (mp_cell_address[index] == MP_CELL_REFUSED) {
            mp_cell_address[index] = 0u;
        }
        if (mp_cell_address[index] == 0u) {
            log_warning("  cell %-20s UNKNOWN", mp_cell_names[index]);
            continue;
        }
        ++known;
        if (log_every_cell) {
            log_info("  cell %-20s -> %08X", mp_cell_names[index],
                     (unsigned)mp_cell_address[index]);
        }
    }

    log_info("%u of %u cells known", (unsigned)known, (unsigned)MP_CELL_COUNT);
    return known;
}

/* FNV-1a over little-endian dwords. The bank module carries the same eight lines for its digest,
 * and this file cannot call them: the bank is an engine binding layer above this one, and the
 * layering is what keeps this module testable without a game. */
static uint32_t hash_dword(uint32_t sum, uint32_t value)
{
    size_t index;

    for (index = 0; index < sizeof value; ++index) {
        sum ^= (value >> (8u * index)) & 0xFFu;
        sum *= 16777619u;
    }
    return sum;
}

static uint32_t hash_impacts(const uint32_t impacts[MP_CELLS_SHOT_ROWS])
{
    uint32_t sum = 2166136261u;
    size_t   row;

    for (row = 0; row < MP_CELLS_SHOT_ROWS; ++row) {
        sum = hash_dword(sum, impacts[row]);
    }
    return sum;
}

/* The row layout is the engine's: the shot table is 38 rows of 0x44 bytes and the impact code is
 * the dword at +0x20 of each row, the one field the shot init at 0x0045230D overwrites from its
 * damage file as the line's number plus 0x29, one line per row until the file runs out (25 lines
 * in the shipped file). So the hash has to be taken after that init has run, which is the shot
 * module's own message 3; asked earlier it hashes the baked column and two peers agree on it
 * whatever their files say. */
uint32_t mp_cells_damage_table_hash(const uint32_t impacts[MP_CELLS_SHOT_ROWS])
{
    uint32_t sum;

    if (impacts == NULL) {
        return 0u;
    }
    sum = hash_impacts(impacts);
    return (sum == 0u) ? 1u : sum;
}

bool mp_cells_hero_position(float out[3])
{
    uintptr_t block = mp_cells_address(MP_CELL_HERO_BLOCK);

    return out != NULL && block != 0u &&
           memory_try_read(block + MP_HERO_BLOCK_POS, out, 3u * sizeof(float));
}
