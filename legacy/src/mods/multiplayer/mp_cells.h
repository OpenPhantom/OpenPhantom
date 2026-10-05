/* mp_cells.h: the engine data cells a multiplayer session reads, recovered from operands.
 *
 * Not one address in this module is written down. Every cell is read out of the operand of a site
 * that mp_signatures.c resolved, which is what keeps the answers right on a build where the datum
 * sits somewhere else and after another module has relocated what an operand names.
 *
 * A cell named by more than one operand is required to have them agree. That is the point of
 * carrying two sites for the same cell: one site can be a coincidence, two agreeing operands
 * cannot.
 */
#ifndef MULTIPLAYER_MP_CELLS_H
#define MULTIPLAYER_MP_CELLS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "mp_signatures.h"

typedef enum mp_cell {
    MP_CELL_PR,                     /* the player record pointer, `pr` in the retail build */
    MP_CELL_PHASE_TABLE,            /* the phase table, one entry past the player pointer */
    MP_CELL_PLR_STATUS_POINTER,     /* the active status record; not the array base */
    MP_CELL_PLR_STATUS_ARRAY,       /* the array base; writing here writes hero 0's record */
    MP_CELL_LEVEL_OUTCOME,          /* the death latch, which ends the level for everyone */
    MP_CELL_ENEMY_SUSPEND,          /* one dword that stops the AI */
    MP_CELL_ENEMY_LIVE,             /* how many actors are alive right now */
    MP_CELL_ENEMY_LIVE_PEAK,        /* the high water mark since the level began */
    MP_CELL_DETAIL_LEVEL,           /* the video option that decides which placements exist */
    MP_CELL_TITLE_WIDGETS,          /* the title screen's own widget array */
    MP_CELL_TITLE_BITMAPS,          /* its bitmap name table, for the append's range check */
    MP_CELL_MENU_FONTS,             /* the font name table the frontend shares */
    MP_CELL_TITLE_SCREEN,           /* the title screen's slot, so "is it the one open" is a
                                     * compare */
    MP_CELL_CURRENT_MENU,           /* the screen on show, or nothing; the other half of that
                                     * compare */
    MP_CELL_USE_LATCH,              /* the use key latch, three ticks wide and GLOBAL: the engine
                                     * has one of these for the whole game, with no player named
                                     * on it, which is what makes a client's press usable by a
                                     * host at all */
    MP_CELL_DIALOG_CONFIRM,         /* the dialogue confirm latch; the script opcode that reads it
                                     * CONSUMES it, so a missed one is gone */
    MP_CELL_OBJ_LIST,               /* the object pool head */
    MP_CELL_TASK_SERVICE,           /* the service record both tick tasks register in */
    MP_CELL_SUBSTEP_ALPHA,          /* the interpolation weight the renderer uses: how far the
                                     * frame being drawn sits past the last substep */
    MP_CELL_SUBSTEP_RATE_SWITCH,    /* moves the ladder from 1/32 to 1/64 */
    MP_CELL_FRAME_DELTA,            /* the substep length in seconds */
    MP_CELL_FRAME_CAP_RELEASE,      /* releases the frame cap */
    MP_CELL_VICTIM_IS_PLAYER,       /* cosmetic, and it routes no damage */
    MP_CELL_MOVER_FREEZE,           /* set to 1 and every mover integration is skipped */
    MP_CELL_LEVEL,                  /* the world the level runs in: the mover count at +0x620, the
                                     * table of mover pointers inline at +0x624, the world clock
                                     * the movers integrate against at +0x54 */
    MP_CELL_HERO_BLOCK,             /* the hero scratch block, addressed absolutely */
    MP_CELL_IMPACT_DIFFICULTY,      /* the difficulty the damage table is indexed by */
    MP_CELL_INPUT_DEVICES,          /* the input device array */
    MP_CELL_MODULE_HEAD,            /* first module installed; last to receive a backward
                                     * broadcast */
    MP_CELL_MODULE_TAIL,            /* last installed, and where every backward broadcast starts */
    MP_CELL_TASK_ARRAY,             /* 64 records of 0x2c bytes */
    MP_CELL_TASK_STAGING,           /* five dwords a registration copies into the record it takes */
    MP_CELL_CLOCK_TICKS,            /* the frame counter, one writer in the whole image */
    MP_CELL_NUM_TASKS,              /* how many scheduler slots are in use */
    MP_CELL_ATTACK_HOLD_ACCUM,      /* the hold accumulator one dword past the hero block */
    MP_CELL_PLAYER_TASK_NODE,       /* g_playerTaskNode; its +0x18 is the contact dispatch slot */
    MP_CELL_MSG_SELF,               /* the SENDER of a contact; its +0x0c is the impact code */
    MP_CELL_MSG_IMPACT,             /* the sender's impact code, stamped rather than passed: a
                                     * replay that leaves it holds the previous contact's damage */
    MP_CELL_MSG_OTHER,              /* the receiver of a contact, the dispatcher's discriminator */
    MP_CELL_MSG_CODE,               /* the contact code: the sender's objClass on a cylinder
                                     * overlap, and on a sphere contact either a weapon's own
                                     * contact code or the touched body's class */
    MP_CELL_MSG_A,                  /* the contact's a flag: set when a weapon met a weapon */
    MP_CELL_MSG_B,                  /* set on the message the ACTOR's handler answers with
                                     * effects */
    MP_CELL_HURT_VOICE_NAME,        /* the wav a player plays when something hurts them:
                                     * g_soundName[52], read out of the one operand that
                                     * names it rather than off the table's base */
    MP_CELL_MSG_CONTACT_NODE,       /* the node a blade struck, written by the pair pass just
                                     * before the message and never cleared between contacts */
    MP_CELL_MODE_DEATH_DESC,        /* the death mode descriptor; cause 5 makes it inert */
    MP_CELL_MODE_STAND_DESC,        /* the stand mode descriptor, what a revived body enters */
    MP_CELL_WKERNEL_CLASS,          /* the window class name string the instance guard searches */
    MP_CELL_SWING_TABLE,            /* the 28 sabre swing rows, 0x20 bytes each; the clip is the
                                     * first word */
    MP_CELL_BLOCK_SHOT_AUX,         /* the deflect's continuation, what the aux slot holds while
                                     * a block plays */
    MP_CELL_BLOCK_ATTACK_AUX,       /* the parry's continuation, the same for a parry */
    MP_CELL_CHEAT_HAPPY,            /* the happy cheat: remaps the player's kind 7 shots to
                                     * kind 9 */
    MP_CELL_CHEAT_EVIL_FORCE,       /* the evil force cheat: remaps the force bolt, kind 11, to
                                     * kind 18 */
    MP_CELL_SHOT_TABLE,             /* the 38 shot rows, 0x44 bytes each; the impact code is at
                                     * +0x20 */
    MP_CELL_DRAW_WEIGHT_GATE,       /* set while the simulation is held: the draw then uses the
                                     * frozen weight below instead of the live one */
    MP_CELL_DRAW_WEIGHT_FROZEN,     /* the weight the draw uses while the gate is set */

    /* The world scratchpad: what outlives a level, and is therefore the campaign rather than the
     * scenery. The two banks are 1250 bytes each, the three blackboard arrays four slots each. */
    MP_CELL_STORY_FLAGS,            /* the living campaign bank, 10000 bits the scripts index */
    MP_CELL_STORY_CHECKPOINT,       /* the copy taken when a level begins */
    MP_CELL_AI_FLAG,                /* the four blackboard registers the scripts compare against */
    MP_CELL_AI_FLAG_PREV,           /* the value a timed register falls back to */
    MP_CELL_ENEMY_POOL,             /* the list root every live actor hangs off */
    MP_CELL_ENEMY_TASK_NODE,        /* g_enemyTaskNode; its +0x18 is the contact dispatch slot,
                                     * filled BEFORE the suspend gate, which is why damage still
                                     * crosses while the AI is switched off */
    MP_CELL_ENEMY_TICK_WAIT,        /* how long the tick still waits before it runs */
    MP_CELL_AI_FLAG_EXPIRY,         /* four floats: an ABSOLUTE world time, 0 meaning no timer.
                                     * In one of the three shipped images this address holds the
                                     * integer flags instead, which is why it is read from an
                                     * operand and never written down */
    /* The level flow, all seven read out of the campaign loop's own operands. */
    MP_CELL_START_LEVEL,            /* the index the next round copies into the current level */
    MP_CELL_LEVEL_INDEX,            /* the level being played, 0..10, or -1 for one loaded by
                                     * name */
    MP_CELL_LOAD_BY_NAME,           /* 1 means the next load takes its path verbatim */
    MP_CELL_LOAD_NAME,              /* the 128 byte buffer that path is read out of */
    MP_CELL_LEVEL_TABLE,            /* eleven rows of {b3d path, display name, movie}, stride 12 */
    MP_CELL_GAME_MODE,              /* 2 running, 3 level complete, 1 quit to the menu, >=4 death */
    MP_CELL_RESTORE_PENDING,        /* 1 sends the round back to the title screen after the level
                                     * ends, instead of on to the next one. The engine's own
                                     * "leave level" menu choice writes it, and it is half of the
                                     * only pair that takes a running level back to the menu:
                                     * this cell to 1 FIRST, then MP_CELL_GAME_MODE to 3. */
    MP_CELL_DATA_ROOT,              /* the installation's data folder, every relative path's base */

    MP_CELL_FOOT_MODULE,            /* g_footThing, the base of the footstep module's own
                                     * globals: the frame cursor at +0x14 and the sound
                                     * variant at +0x18, both one cell for every body */
    MP_CELL_CURRENT_PLAYER,         /* 0 obiwan, 1 quigon, 2 panaka, 3 queen; picks the
                                     * frames a run plants its feet on */
    MP_CELL_BLOCK_FX_COOLDOWN,      /* the world time before which no NPC's blade clang plays,
                                     * one float for every actor */
    MP_CELL_COUNT
} mp_cell_t;

/* The shot table's shape, for the fingerprint: 38 rows of 0x44 bytes, and the dword at +0x20 of
 * each row is the impact code the shot module's own init overwrites from damage.txt. */
#define MP_CELLS_SHOT_ROWS       38u
#define MP_CELLS_SHOT_ROW_BYTES  0x44u
#define MP_CELLS_SHOT_ROW_IMPACT 0x20u
/* The row's actor template, which shot_init 0x0045230D resolves for every row just before it reads
 * damage.txt into the impact column. 0 in all 38 rows of the image, so a row that holds one says
 * the impact column is damage.txt's and not the baked one. */
#define MP_CELLS_SHOT_ROW_ACTOR  0x40u

/* The world record MP_CELL_LEVEL points at, as far as this feature reads it. One definition, so
 * that the map and the enemy binding cannot disagree about where a field is.
 *
 * The placement table is behind a pointer and the mover table is inline; that is the engine's
 * choice and both offsets are proven at the sites that read them: the activation scan for the
 * placements (0x00437195 and 0x004371A6) and the mover tick for the movers. */
#define MP_LEVEL_PLAYER_START     0x18u   /* vec3, the position the level itself authored for the
                                           * player. Its one consumer in the engine is the player
                                           * module's level-begin arm, which hands it and the yaw
                                           * below straight to the hero spawn */
#define MP_LEVEL_PLAYER_START_YAW 0x28u   /* f32, degrees, the same call's third argument */
#define MP_LEVEL_PLACEMENT_COUNT 0x204u   /* i32, how many placements the level authored */
#define MP_LEVEL_PLACEMENTS      0x20Cu   /* pointer to an array of placement record pointers */
#define MP_LEVEL_MOVER_COUNT     0x620u   /* i32, and the identity of a level on the wire */

/* The hero block MP_CELL_HERO_BLOCK points at, and the fields of it read outside the body
 * modules. All byte-confirmed against player_spawnHero 0x00447E58.
 *
 * MODULE_STATE is the gate every placement and every hero swap has to pass: player_respawnAt
 * 0x00447C90 returns without a word unless it reads 1, and the spawn writes the 1 in its LAST
 * line, so the whole of a level load reads 0. A caller that does not test it fails silently. */
#define MP_HERO_BLOCK_HERO_ACTOR   0x00u   /* the loaded actor asset, what res_Alloc handed back */
#define MP_HERO_BLOCK_MODULE_STATE 0x04u   /* 1 while a player exists and is not parked */
/* That one, by name, because a repair has to write it and a magic number in a write is worse
 * than a magic number in a comparison. */
#define MP_HERO_MODULE_RUNNING     1u
/* Five of them: 0 idle, 1 running, 2 quitting, 3 dying, 4 respawning. A counter that only says
 * "not running" adds four different answers together. */
#define MP_HERO_MODULE_STATES      5u
/* The player's own object. Nought here is the hole between a despawn and the next spawn, and a
 * module state of nought means two different things either side of it. */
#define MP_HERO_BLOCK_OBJECT       0x0Cu
/* Where a grab parks the state above, and the only memory in the engine that a grab happened at
 * all. player_suspend writes it and player_resume reads it back; nothing else in either build
 * touches it, which makes the pair a stack one deep. A nought here means nobody on this machine
 * ever parked the hero, because the grab refuses its own work while the state above is nought and
 * so never parks one. */
#define MP_HERO_BLOCK_SAVED_MODULE_STATE 0x08u
#define MP_HERO_BLOCK_HACTOR       0x0Cu   /* the raw bapObj pointer the spawn allocates */
#define MP_HERO_BLOCK_HERO_INDEX   0x6Cu   /* which hero the player is, and its status index */
#define MP_HERO_BLOCK_POS        0x118u
#define MP_HERO_BLOCK_HEADING    0x2A0u    /* one float */
/* The fall's own state: 0 as a fall begins, 2 once it is decided, 3 while a landing plays out.
 * The fall update at 0x0044F162 writes the 2 when more than eight units of air are covered and
 * the body is not over water, in the same block in which it rewrites camera region 13, forces
 * the camera onto it and pins the camera's anchor. Only the next fall's entry and a spawn's wipe
 * of the block write it again, so a body that died of that fall still carries the 2. */
#define MP_HERO_BLOCK_FALL_STATE 0x358u
/* Written to 1 by the death entry and never cleared in place: a body that comes back is a NEW one
 * out of player_spawnHero, so this reads 0 again only after a despawn and a spawn. It is the
 * engine's own answer to "is this player a corpse", and it is what `sys_pause` tests before it
 * will open the pause menu, so a corpse that is never asked back is a game with no way out. */
#define MP_HERO_BLOCK_DEAD       0x394u

/* THE LOADED ACTOR, as far as the identity of an appearance goes. What MP_HERO_BLOCK_HERO_ACTOR
 * points at is the mounted .baf, and its header carries its own file name at +0x08 in a 32 byte
 * field. That field is the only thing about a character that means the same on two machines: the
 * hero index has four values and every added character rides one of them, while the name names a
 * file.
 *
 * Two rules follow from a census of the shipped actors and neither is optional. The field is cut
 * at its FIRST zero byte, because two of the 265 shipped actors carry bytes after it that a raw
 * comparison of all 32 reports as a difference. And it is compared lower case, because nothing
 * guarantees the case a name was authored in matches the case a caller asks with. */
#define MP_ACTOR_NAME            0x08u
#define MP_ACTOR_NAME_BYTES      0x20u

/* A placement record, one entry of the table above, as far as more than one module reads it:
 * the authored flags word, the deactivation radius the removal test uses, and the spawn state
 * the scan writes and the removal rewrites (0 not spawned, 1 live, 2 buried). */
#define MP_PLACEMENT_FLAGS       0x00u
#define MP_PLACEMENT_MOVE_MODE   0x08u    /* i32, 0..8. A placement of 2 or more is NEVER snapped
                                           * to the ground: fish, vehicles, props and pickups keep
                                           * the height the editor gave them, so such a record
                                           * names a point in the air as readily as a point on the
                                           * floor */
#define MP_PLACEMENT_START_YAW   0x24u    /* f32, the authored facing in DEGREES, which is the
                                           * unit the player's own heading is kept in */
#define MP_PLACEMENT_WAKE_RANGE  0x28u    /* f32, the activation scan's radius, measured from
                                           * the position below; exactly 0 is no test at all */
#define MP_PLACEMENT_DEACT_RANGE 0x2Cu
/* u16[4], the AMAP movers this placement's script is allowed to drive. It is the field that
 * says a placement HOLDS LEVEL GEOMETRY: a lift, a grate, a platform door. The engine's own
 * script ops 0x205, 0x206 and 0x211 index it, and a placement with all four at zero drives
 * nothing, whatever else it does.
 *
 * It is the right question for an arena to ask, and the class is not: the class is a PERCEPTION
 * and collision band, not a description of what the placement builds. */
#define MP_PLACEMENT_MOVER_SLOTS 0x94u

/* i32, and it is NOT what the placement builds. At the engine's own spawner the value goes into
 * the body's two perception fields, objClass and shooterClass, and nowhere else: it decides who
 * collides with whom, who may shoot whom, and which impact column is read. The editor's table
 * names the bands None, Player, Enemy, Civilian, Tripod, Turret, AI Tank, NPC Ally and then the
 * pickups from ten up. Class 0 is INERT and collides with nothing at all.
 *
 * The comment here used to say "what the placement builds", and the arena was written from that
 * reading: it buried two classes and left every civilian, ally and scripted actor standing. */
#define MP_PLACEMENT_CLASS_ID    0x34u
#define MP_PLACEMENT_MIN_DIFF    0x3Cu    /* i32, the activation scan skips the record while the
                                           * difficulty setting is BELOW this */
#define MP_PLACEMENT_DETAIL_GATE 0x40u    /* i32, and it skips it while this is ABOVE the detail
                                           * level, so the two gates read in opposite directions */
#define MP_PLACEMENT_POSITION    0xACu    /* vec3, the authored standing position. The script warp
                                           * takes this field and the yaw above as the pair it
                                           * hands to the player respawn */
#define MP_PLACEMENT_SPAWN_STATE 0xC8u

/* The flag bit that makes a placement the PLAYER'S body rather than somebody to fight. The spawn
 * builds no actor for it and parks it in state 0x10; three of the twenty five records that carry it
 * also carry an enemy class, so a filter that reads the class alone takes the player with it. */
#define MP_PLACEMENT_F_HOSTS_PLAYER 0x2000u

/* The actor built from a placement record, 0 while none: written by spawn_actor 0x00437250 and
 * cleared by every delete. The record is variable in length, a head of 0xF0 bytes and then the
 * route nodes; the table of placements a script names by slot sits at +0x6C with no length of
 * its own, a 0 there being padding and placement 0 alike, and nothing here reads it. */
#define MP_PLACEMENT_LIVE_ACTOR       0xD0u

/* A live actor, the engine's `character`, in the fields a scene reads to see where a scripted walk
 * goes and whether it gets there. The chase opcode is the only writer of the walk request and the
 * enemy tick clears it before every script run, so a 1 read after that tick means the chase ran in
 * it. The chase stores its goal in the target field, except while it walks a detour, when the
 * field holds the detour point instead.
 *
 * Where the engine reads and writes them: the waypoint by set_waypoint 0x0042BF60; the move mode
 * by spawn_actor 0x00437250 from the record and by the script's opcode 0x409 in ai_run
 * 0x00433D0B, which clamps it to 0 to 8; the speed by opcode 0x408 and read by the chase,
 * move_chaseDrive 0x00429DF4, which walks backwards below 0; the target stored by the chase (at
 * 0x00429E39) and read again while the detour flag or the clear sight flag stands, and by
 * move_trackZ 0x0042A8DE for a flier; the walk request set by opcode 0x201 in ai_run and cleared
 * by enemy_preTick 0x00435C67 before every script run; the detour flag set by the chase when it
 * finds a detour and cleared when it reaches the detour point, without storing the goal again.
 *
 * And the fields that say where an actor's script stands, which the line of a standing scene
 * prints: the state the actor list's tick, enemy_tickAll 0x00432BF2, switches on, 1 active, 3
 * standing by and 0x10 waiting for the player's body; the health the wait for a death, opcode
 * 0x102, holds against nought; the five counters of opcodes 0x101, 0x10A, 0x203, 0x204 and 0x412
 * in ai_run; the script's own state, which opcode 0x400 sets through ai_setMode 0x004335A5 and
 * which indexes the script's table of state labels in ai_reloadState; and the clip the body
 * plays beside the clip the script asked for by opcode 0x202, which op_animation 0x0042E3AD makes
 * the playing one. */
#define MP_CHARACTER_PLACEMENT        0x10u   /* the authored record this actor was made from */
#define MP_CHARACTER_STATE_FLAGS      0x14u
#define MP_CHARACTER_STATE            0x20u   /* i32 */
#define MP_CHARACTER_BODY             0x34u
#define MP_CHARACTER_HEALTH           0x38u   /* i32 */
#define MP_CHARACTER_WAYPOINT         0x58u
#define MP_CHARACTER_COUNTERS         0x5Cu   /* i32[5] */
#define MP_CHARACTER_COUNTER_COUNT    5u
#define MP_CHARACTER_SCRIPT_STATE     0x7Cu   /* i32 */
#define MP_CHARACTER_MOVE_MODE        0x98u   /* i32; scripts rewrite it, so a live actor is read
                                               * here and not off its record. An odd mode walks
                                               * through bodies and geometry */
#define MP_CHARACTER_MOVE_SPEED       0x9Cu   /* f32; negative walks backwards, zero only turns */
#define MP_CHARACTER_MOVE_TARGET      0xA0u   /* vec3 */
#define MP_CHARACTER_POS              0xD0u   /* vec3 */
#define MP_CHARACTER_MOVE_REQUESTED   0x174u  /* i32 */
#define MP_CHARACTER_CLIP             0x1BCu  /* i32, the clip the body plays */
#define MP_CHARACTER_CLIP_ASKED       0x1C0u  /* i32, the clip the script asked for */
#define MP_CHARACTER_F_DETOUR         0x400000u

/* The two classes an arena buries. 8 is the tank, which spawn_actor maps to 2 before it builds. */
/* The pickup band, closed at both ends, exactly as the engine's own contact handler tests it:
 * `kind >= 0x0a && kind <= 0x1b` is what sends a touch to Plr_PickUp. ABOVE the band the classes
 * are damage arms rather than pickups: 0x1e is the burning death and 0x20 upward is ordinary
 * contact damage, so an open-ended test would call one of those a health pack.
 *
 * Two classes inside the band, 0x0b and 0x0c, reach no arm of the handler at all and are placed
 * by no shipped level. */
#define MP_PLACEMENT_CLASS_FIRST_PICKUP 10
#define MP_PLACEMENT_CLASS_LAST_PICKUP  0x1B

#define MP_PLACEMENT_CLASS_ENEMY 2
#define MP_PLACEMENT_CLASS_TANK  8

/* What a placement's spawn state means to the activation scan (0x00437161): it skips anything
 * that is not zero, so 2 is how a level is emptied without touching the scan itself. */
#define MP_PLACEMENT_STATE_ASLEEP 0u
#define MP_PLACEMENT_STATE_LIVE   1u
#define MP_PLACEMENT_STATE_BURIED 2u

/* One operand of one site. Rows are what the resolver walks and what a test checks: an operand a
 * caller means to READ has to be a wildcard in its pattern, or the pattern pins the very value the
 * operand exists to report. */
typedef struct mp_operand {
    mp_cell_t cell;
    mp_site_t site;
    uint16_t  offset;
} mp_operand_t;

/* Reads every cell out of the sites resolved by mp_signatures_resolve, which must have run first.
 * Returns how many cells came back with an address.
 *
 * A cell that could not be read, or whose operands disagreed, is always reported.
 * `log_every_cell` adds the ordinary ones as well. */
size_t mp_cells_resolve(bool log_every_cell);

/* 0 when the cell could not be recovered or its operands disagreed. */
uintptr_t mp_cells_address(mp_cell_t cell);

/* The cell's name for a log line, or NULL for an index out of range. */
const char *mp_cells_name(mp_cell_t cell);

/* The operand table, so that a test can check its shape without a game. */
const mp_operand_t *mp_cells_operands(size_t *count);

/* The damage table as one number: the 38 runtime impact codes of the shot table, which the shot
 * module's own init overwrites from damage.txt. Two machines whose numbers differ fire different
 * weapons for different damage from the same event, and a join between them is refused with the
 * file's name. The difficulty, the two cheats that remap a shot's kind and the detail level were
 * hashed with it until wire 35; the host's difficulty holds on every client since wire 33, the
 * cheats are the host's in a session, and the detail level only gates the activation scan, which a
 * client parks, so none of them decides anything a client runs for the host. 0 is the unread
 * marker, so a hash that lands on it is reported as 1. */
uint32_t mp_cells_damage_table_hash(const uint32_t impacts[MP_CELLS_SHOT_ROWS]);

/* Where this machine's own player stands: the hero block's position, through the same try-read
 * every reader of that block uses. False with no block or an unreadable one. A substep old at
 * worst, which for a distance test is nothing. */
bool mp_cells_hero_position(float out[3]);

#endif /* MULTIPLAYER_MP_CELLS_H */
