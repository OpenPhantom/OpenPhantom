/* mp_signatures.h: the engine sites a multiplayer session reaches for.
 *
 * The feature is built in layers and only one of them is allowed to know an address. This is that
 * layer's answer table: nothing above it names a VA, and nothing above it needs to.
 *
 * Where the DATA cells live is mp_cells.h, which is built on this one: a cell is read out of an
 * operand of a site resolved here, never written down as a number.
 *
 * The patterns themselves live in three files, split by engine subsystem when the first crossed
 * the size limit: mp_signatures.c carries the scheduler, lifecycle, input and data anchor sites,
 * mp_signatures_puppet.c the body, animation, sabre and impact effect sites, and
 * mp_signatures_world.c the three probes that answer a question about a point in the world.
 *
 * The first two share the enumeration mp_site_t and the resolver merges the second table behind
 * the first, so a caller never knows which of those two files a site came from. The third has its
 * own enumeration and its own resolver, for the reason written above mp_world_site_t below.
 *
 * Three builds, and what separates them. Four of the six executables on a shipped installation,
 * the German retail, the English retail, the LucasArts installation and the BIN copy, are one
 * file; the alternate link and the editor's recompile are the other two, so an offline check that
 * reports six images passing is reporting four copies of one measurement. The alternate link's
 * code section is the same 682,496 bytes from 0x00401000 and differs in exactly 10,152 of them,
 * all between 0x0042DAF9 and 0x0045092C. The shift is piecewise constant: none up to 0x43516D,
 * 33 bytes short of retail from there to 0x4359E8, 8 short from there to 0x437A2D, and none
 * again from 0x437A40. The 33 bytes sit in the AI machine immediately behind its call to the
 * actor spawn:
 *
 *     0043516D  E8 DE 20 00 00               call 0x437250          ; spawn_actor
 *     00435172  83 C4 0C                     add  esp, 0xC
 *     00435175  85 C0 / 74 1D                test eax, eax / je
 *     00435179  8B 4D 08 / 8B 51 14 / 81 E2 00 20 00 00 / 85 D2 / 75 0D
 *     00435189  8B 45 84 / C7 80 C8 00 00 00 01 00 00 00   ; newActor+0xC8 = 1
 *     00435196  E9 BE 00 00 00               jmp  0x435259
 *
 * The alternate link has only the call, the stack adjust and the jump: the gate on
 * actor+0x14 & 0x2000 and the store to +0xC8 do not exist in it, and neither field is named yet.
 * The 25 bytes it carries instead insert a call and four stores before the return loop of the
 * function at 0x004358B0, whose name is the reconstruction's own guess, and the last 8 go into
 * int3 alignment. So exactly two functions in the table move: the activation scan from
 * 0x00437161 to 0x00437159 and the actor spawn from 0x00437250 to 0x00437248; the ten bytes that
 * differ inside the AI tick are all call displacements and its entry stays where it is.
 *
 * The recompile keeps every address below the campaign run and moves the code from the impact
 * lookup onward by 0x60, with the data it names 0x50 lower. Seven patterns of the first draft
 * required an absolute address as a byte: the mover integrator at +0x08, the use latch at +0x05,
 * the impact lookup at +0x05, the AI tick at +0x07, +0x13 and +0x20, the player task at +0x05 and
 * +0x11, the hero spawn at +0x0F and the phase pipeline at +0x1A and +0x27. Wildcarding them
 * changed nothing on the two strict builds, every pattern still matching exactly once and stage
 * two still finding one candidate, and on the recompile six of the seven began to resolve where
 * they had found nothing, at the shifted addresses, which is what says the hits are the same
 * functions.
 *
 * Three of the functions are already detoured by other DLLs in this tree, which is what makes stage
 * two load bearing: the mover integrator by the diagnostics and the framerate fix, the world draw
 * by the enhanced input and one more DLL, and the phase pipeline by the diagnostics.
 * Resolved inside the running game with twenty three other mods loaded, every site and every
 * cell answered its offline address; the integrator and the world draw resolved through their
 * tails because the two DLLs that claim them load first, and the integrator's operand at +8 was
 * read from the executable on disk because its head carried a foreign branch, the first time
 * that path served this table. The phase pipeline resolved through stage one because the
 * diagnostics arm that claims it was off, so its stage two is proven offline only. What has not
 * been checked for any site is reachability: the return gates in front of each function have
 * not been enumerated, except the activation scan's null body test at 0x00437173, which happens
 * to lie inside its pattern.
 */
#ifndef MULTIPLAYER_MP_SIGNATURES_H
#define MULTIPLAYER_MP_SIGNATURES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "common/signature.h"

/* Every site the feature reaches for, in ascending retail address order so that the list can be
 * held against a disassembly listing without sorting it first. The addresses in the comments are
 * the retail ones and are there to be read, not used: the alternate link moves two of them and the
 * recompile moves nearly all of them. */
typedef enum mp_site {
    MP_SITE_BAPMAP_OPEN_MOVER,      /* 0x00408B50  every player side trigger of a mover ends here */
    MP_SITE_BAPMAP_CLOSE_MOVER,     /* 0x00408DF5  the script's close: forces the open-hold latch */
    MP_SITE_BAPMAP_TICK_MOVER,      /* 0x00409170  one mover integrated for one substep */
    MP_SITE_BAPMAP_TICK_MOVERS,     /* 0x0040A7D1  frame catch-all; names the world three times */
    MP_SITE_WITHIN_RANGE,           /* 0x00428EB3  the ONE range test: the activation scan
                                     * and the removal test are its only two callers */
    MP_SITE_RESOLVE_TARGET,         /* 0x0042B61C  which body a script command means */
    MP_SITE_ENEMY_TICK_ALL,         /* 0x00432BF2  carries the AI suspend switch */

    MP_SITE_ENEMY_LATCH_USE,        /* 0x00433CEF  the whole body is one store to the use latch;
                                     * the pattern reaches into the neighbour at 0x00433CFE for a
                                     * tail, and a detour there would break it silently */
    MP_SITE_AI_RUN,                 /* 0x00433D0B  the AI virtual machine */
    MP_SITE_ENEMY_ACTIVATION_SCAN,  /* 0x00437161  returns early when no player is alive; the
                                     * alternate link has it at 0x00437159 */
    MP_SITE_SPAWN_ACTOR,            /* 0x00437250  an actor record becomes a body; the alternate
                                     * link has it at 0x00437248 */
    MP_SITE_ENEMY_DELETE,           /* 0x00437850  an actor leaves, with the reason that decides
                                     * what its placement becomes */
    MP_SITE_SYS_STARTUP,            /* 0x0043E613  the bootstrap anchor. Thirty five bytes with
                                     * the operands wildcarded, because the tail of a twenty
                                     * five byte form matches nine times, one past the ceiling
                                     * stage two accepts; the pushed .data literal behind the
                                     * prologue is what makes it unique */
    MP_SITE_CAMPAIGN_RUN,           /* 0x0043EB2A  the bootstrap fallback */
    MP_SITE_IMPACT_LOOKUP,          /* 0x004477D0  the damage table read, not a receiver */
    MP_SITE_PLAYER_TICK_TASK,       /* 0x00447D38  the player's own task body */
    MP_SITE_PLAYER_SPAWN_HERO,      /* 0x00447E58  clears bank 0 whatever the pointer says */
    MP_SITE_PLR_RUN_PHASES,         /* 0x00448297  the thirteen phase pipeline */
    MP_SITE_PLR_PICKUP,             /* 0x00448894  the player takes a pickup; bit 3 of the pickup
                                     * body's flags is its gate */
    MP_SITE_PLR_ENTER_STAND,        /* 0x0044CD1C  the return-to-normal mode entry */
    MP_SITE_PLR_ENTER_DEATH,        /* 0x004500B0  retail death; the damage post's one detour */
    MP_SITE_STATUS_SET_ACTIVE,      /* 0x00459AB7  entry; index 4 or more reaches the null assert */
    MP_SITE_STATUS_SET_HEALTH,      /* 0x00459EA9  writes through the UNBANKED status pointer */
    MP_SITE_INPUT_DIGITAL_AXIS,     /* 0x0046507E  clamped absolute axis read, no player argument */
    MP_SITE_INPUT_AXIS,             /* 0x004652C3  analogue read, no player argument */
    MP_SITE_INPUT_IS_HELD,          /* 0x004653CE  button read, no player argument */
    MP_SITE_INPUT_HOLD_RELEASE,     /* 0x00465502  tap-versus-hold read, no player argument */
    MP_SITE_MODULE_INSTALL,         /* 0x0046ED64  called rather than patched */
    MP_SITE_TASK_REGISTER,          /* 0x0047563D  returns a pointer, and -1 on failure */
    MP_SITE_DEATH_LATCH_SET,        /* 0x004503A8 and 0x004503F8, two matches by design */
    MP_SITE_STATUS_POINTER_WRITE,   /* 0x00459BBA  the only writer of the status pointer */
    MP_SITE_OBJ_LIST_WALK,          /* 0x0041108F  names the object pool twice. Thirty seven
                                     * bytes, reaching into the gather test, because the bare
                                     * pool walk idiom matches fifteen times in every image */
    MP_SITE_SUBSTEP_ALPHA_STORE,    /* 0x00475817  writes the interpolation alpha */
    MP_SITE_SUBSTEP_RATE_SWITCH,    /* 0x00475737  the 1/32 against 1/64 arm */
    MP_SITE_FRAME_CAP_RELEASE,      /* 0x00475BAE  the frame cap's only reader. Sixteen bytes,
                                     * with the float load behind the test, because the bare
                                     * test matches three times */
    MP_SITE_VICTIM_IS_PLAYER_SET,   /* 0x00454AD5  writes the cosmetic victim flag */
    MP_SITE_VICTIM_IS_PLAYER_TEST,  /* 0x00453B08  reads it */
    MP_SITE_MODULE_LIST_LINK,       /* 0x0046EDE0  names the list head twice, the tail four times */
    MP_SITE_TASK_STAGING,           /* 0x004756A4  the five dwords a task registration inherits */
    MP_SITE_CLOCK_TICKS,            /* 0x0046C1B5  the one instruction that counts frames */
    MP_SITE_TASK_COUNT,             /* 0x004755D4  names the scheduler's own slot counter */
    MP_SITE_PLR_ATTACK_HOLD,        /* 0x0044B0C2  the one operand naming the hold accumulator */
    MP_SITE_PLAYER_SAVE,            /* 0x004479D2  writes the hero block into the savegame */
    MP_SITE_PLAYER_RESTORE,         /* 0x00447AB1  reads it back */
    MP_SITE_PLAYER_RESPAWN_AT,      /* 0x00447C90  addresses the block absolutely at +0x4 */
    MP_SITE_PLAYER_DESPAWN,         /* 0x00448201  addresses the block absolutely at +0x4 */
    MP_SITE_CONTACT_SLOT_STORE,     /* 0x00448025  names g_playerTaskNode and the contact proc */
    MP_SITE_POST_CONTACT,           /* 0x00414C99  names the six contact message globals */
    MP_SITE_DEATH_DESC_STORE,       /* 0x00450110  names the death descriptor and the cause field */
    MP_SITE_WKERNEL_CREATE,         /* 0x00498F24  single-instance guard; names its class string */
    MP_SITE_SHOT_KIND_REMAP,        /* 0x00453DCE  in shot_spawn: evil force remap and the table */
    MP_SITE_ENEMY_LIVE_PEAK,        /* 0x00433329  live actor count and peak, each named twice */
    MP_SITE_DETAIL_LEVEL_TICK,      /* 0x00433217  the reaction layer's patience test names the
                                     * detail level */
    MP_SITE_DETAIL_LEVEL_SCAN,      /* 0x00437214  and so does the activation scan's own gate */

    /* The world scratchpad. Every one of these is a data anchor and none is a detour target: the
     * five cells behind them move between the three shipped images, and one of them moves onto
     * another one's address, so they are read out of operands or not at all. */
    MP_SITE_STORY_SAVE_PAIR,        /* 0x004321E7  both banks into the save block, length and all */
    MP_SITE_STORY_RESTORE_PAIR,     /* 0x004325E1  and back out of it */
    MP_SITE_STORY_COPY_PAIR,        /* 0x004333AA  checkpoint taken and restored, each bank twice */
    MP_SITE_STORY_CLEAR,            /* 0x00433395  the clear that proves the bank is 1250 bytes */
    MP_SITE_STORY_OP_SET_GLOB,      /* 0x00434A0A  the script opcode that sets a campaign bit */
    MP_SITE_STORY_KEY_USE,          /* 0x0044CA78  a key being spent; the only site outside the two
                                     * compilands the others share */
    MP_SITE_STORY_PAUSE_INV,        /* 0x00442F52  the inventory reader, naming the window floor */
    MP_SITE_AIFLAG_SAVE,            /* 0x00432198  all three blackboard arrays to the save block */
    MP_SITE_AIFLAG_RESTORE,         /* 0x004325A6  and back */
    MP_SITE_AIFLAG_SET_TIMED,       /* 0x0042E506  arms a timed flag; proves clock offset 0x54 */
    MP_SITE_AIFLAG_EXPIRE,          /* 0x0042E58F  lets one run out; states the same offset again */
    MP_SITE_AIFLAG_INIT_CLEAR,      /* 0x00431F6E  twelve operands: the four slot layout, read */
    MP_SITE_AIFLAG_CLOSE_CLEAR,     /* 0x00432052  the same layout from a second compilation */

    /* The enemy pool, its task record and its tick wait. */
    MP_SITE_ENEMY_ON_CONTACT,       /* 0x00436A68  every actor's contact handler. The hit relay
                                     * detours it to report and perform hits between machines.
                                     * Nothing detours it to keep a parked replica out of
                                     * contacts: the handler's FIRST gate already returns the
                                     * same answer for a victim in the standby state, which is
                                     * the state parking puts a replica in, and such a detour
                                     * repeated the engine's own test six bytes earlier */
    MP_SITE_ENEMY_POOL_NEW,         /* 0x00431F45  the pool being created; pins its shape too */
    MP_SITE_ENEMY_POOL_WALK,        /* 0x00432C47  the tick walking it, for the second witness */

    /* The frontend. These are functions this feature calls, one of them also hulled (the
     * navigation code), plus the one screen it appends a line to. */
    MP_SITE_SWMENU_BUILD,           /* 0x0045E7A3  adopts a widget array, once, per screen */
    MP_SITE_SWMENU_OPEN,            /* 0x0045D9F5  puts a built screen on the stack */
    MP_SITE_SWMENU_CLOSE,           /* 0x0045DB7A  takes it off again and frees its bitmaps */
    MP_SITE_SWMENU_PUMP_FRAME,      /* 0x0045F631  one frame of a screen: input, draw, present */
    MP_SITE_SWMENU_TAKE_NAV,        /* 0x0045EA62  the navigation code, cleared as it is read */
    MP_SITE_SWMENU_FOCUS_ID,        /* 0x0045EA21  which widget has the focus, -1 for none */
    MP_SITE_SWMENU_WIDGET_STATE,    /* 0x0045DCE1  a widget's state, which is a list box's row */
    MP_SITE_SWMENU_SET_EDIT_TEXT,   /* 0x0045EADC  fills an edit field; an unchecked strcpy */
    MP_SITE_SWMENU_GET_EDIT_TEXT,   /* 0x0045EB3A  reads one back */
    MP_SITE_TITLE_MAIN_MENU,        /* 0x00440315  names its widgets, bitmaps, fonts and slot */
    MP_SITE_SWMENU_LAST_FOCUS,      /* 0x0045EA00  which widget had focus before the mouse left */
    MP_SITE_SWWIDGET_FOCUS_BY_ID,   /* 0x00462CE1  puts the focus back on one */

    /* Sound. The funnel every route into audio ends in, and the one routine that plays a fight
     * without saying where it is. */
    MP_SITE_BAPSOUND_PLAY,          /* 0x0041681F  the funnel; a null third argument is a voice
                                     * with no place, no distance gate and no falloff */
    MP_SITE_NPC_BLOCK_IMPACT_FX,    /* 0x0042E133  the blade clangs, played without a position,
                                     * behind one 0.2 s cooldown cell for every actor together */

    /* The level flow, which is what a lobby steers: which level the next round of the campaign
     * loads, how a level is loaded by name, when a level is running, where the data root is, and
     * the two ways a game is entered other than by starting one. */
    MP_SITE_CAMPAIGN_ROUND,         /* 0x0043EBE0  start level into current level, then the title */
    MP_SITE_CAMPAIGN_LOAD,          /* 0x0043EC2A  the load branch: five cells out of one run */
    MP_SITE_LEVEL_HANDOVER,         /* 0x0043ED50  the hand-over to the level; names the game mode.
                                     * NOT MP_SITE_CAMPAIGN_RUN above: that is the same function's
                                     * ENTRY, which the bootstrap may already carry a detour on */
    MP_SITE_PATH_PREFIX,            /* 0x0043F93C  the path builder; names the data root */
    MP_SITE_HERO_SWAP,              /* 0x004302AA  the cheats' own hero swap, by hero index */
    MP_SITE_PLAYER_TELEPORT,        /* 0x00451266  seats the player at a point and a heading, and
                                     * clears the ground contact block first, which is what every
                                     * level warp and every cutscene placement goes through */
    MP_SITE_SAVE_LOAD_NAMED,        /* 0x0045158F  restores a savegame by file name */
    MP_SITE_LOAD_GAME_SCREEN,       /* 0x00440826  the shipped load screen; a detour drives it */
    MP_SITE_CONTACT_NODE_READ,      /* 0x0044884F  the one read of the node a blade struck, and a
                                     * second witness for the player record */

    /* The body, animation, sabre and impact effect cluster, whose rows live in
     * mp_signatures_puppet.c and are merged behind the ones above. Ascending within the block up
     * to the update track; the two resource anchors after it were appended later and are out of
     * address order, because the enumeration may only grow at its end. The first entry of the
     * block is where the second table begins. */
    MP_SITE_BAPOBJ_DRAW_ALL,        /* 0x00411028  the gather array with no bounds check, and
                                     * the frame set-up that names the interpolation weight */
    MP_SITE_THING_ALLOC,            /* 0x0041223E  takes a slot out of the object pool */
    MP_SITE_THING_FREE,             /* 0x004123B2  gives one back */
    MP_SITE_BAPOBJ_PLAY_CLIP,       /* 0x0041263F  one clip on one body; the death clip's door */
    MP_SITE_BAPOBJ_PLAY_OVERLAY,    /* 0x004128C1  plays the upper-body overlay clip on one body */
    MP_SITE_BAPOBJ_STOP_OVERLAY,    /* 0x00412BAF  ends the overlay; mode 0 resets the slot to -1 */
    MP_SITE_BAPOBJ_NODE_SPHERE,     /* 0x00414231  a node's world sphere; the sabre node's centre */
    MP_SITE_PLR_ARMED_CONTACT,      /* 0x0044855C  every blade contact's door; carries the two aux
                                     * operands */
    MP_SITE_PLR_TICK_BLADE_LIGHT,   /* 0x00449B9D  moves the blade's embedded light to the sabre
                                     * node */
    MP_SITE_PLR_SET_WEAPON,         /* 0x0044B268  five-mode weapon select; mode 2 sets a slot
                                     * raw */
    MP_SITE_PLR_START_FORCE_PUSH,   /* 0x0044BCDE  plays the push overlay and arms the bolt's aux */
    MP_SITE_PLR_START_BLOCK_SHOT,   /* 0x0044DBC6  the deflect: answers 1 with the block clip
                                     * playing */
    MP_SITE_PLR_BLOCK_ATTACK,       /* 0x0044E166  the parry: answers 1 with the parry clip
                                     * playing */
    MP_SITE_PLR_TEST_SWING_WORLD,   /* 0x0044E6E2  the swept blade against the level: scorch and
                                     * spark */
    MP_SITE_PLR_START_SWING,        /* 0x0044E858  arms a sabre swing from a swing table row */
    MP_SITE_PLR_CLEAR_SWING_CONTACT, /* 0x004509BB  the one end of every sabre action: disarms
                                     * the blade */
    MP_SITE_PLR_PLAY_IMPACT_VOICE,  /* 0x00450E98  one of four impact voice groups, behind its own
                                     * cooldown */
    MP_SITE_PLR_FLASH_AT,           /* 0x00450EED  the impact flash sprite; nineteen bytes whole */
    MP_SITE_PLR_SPARK_AT,           /* 0x00450F00  the impact spark emitter */
    MP_SITE_SHOT_SPAWN,             /* 0x00453CD2  three gates on shooterClass == 1; names the
                                     * happy cheat */
    MP_SITE_RDPUPPET_UPDATE_TRACK,  /* 0x00483D20  advances one track; with an override, by
                                     * exactly that */

    /* The resource anchors, appended out of address order because the enumeration is append only
     * and a row inserted in the middle would renumber every site behind it. Both belong to the
     * asset a far body wears: the first is the one instruction pair inside player_spawnHero that
     * names the four hero asset names and calls the loader, the second is the release. */
    MP_SITE_HERO_ASSET_TABLE,       /* 0x00447ECF  the hero name table and the call to res_Alloc */
    MP_SITE_RES_FREE,               /* 0x0047221F  gives one resource reference back */
    /* Appended for the size of a far body, out of address order for the same reason. */
    MP_SITE_BAPOBJ_SET_SCALE,       /* 0x00412569  the object's three scale factors and its
                                     * handle's culling radius */
    /* Appended for the two effects a hurt far body owes itself, out of address order for the
     * same reason as everything above: this enumeration is append only, and mp_sites[] is a
     * positional list with no assertion over its length. */
    MP_SITE_BAPSOUND_PLAY_NAME,     /* 0x00416787  one named sound, no place of its own */
    MP_SITE_EMITTER_SPAWN_AT,       /* 0x004245D0  a builtin particle template at a point */
    MP_SITE_HURT_VOICE_READ,        /* 0x00448812  an anchor: its operand names the wav a
                                     * player plays when something hurts them */
    MP_SITE_AI_START_EMITTER,       /* 0x004369C1  a script hangs an emitter on an actor */
    MP_SITE_ENEMY_DETACH_PIECE,     /* 0x0042E900  the engine takes a limb off an actor */
    MP_SITE_FOOTSTEP_TICK,          /* 0x00437AC0  one body's footfall for this substep */
    MP_SITE_FOOT_RUN,               /* 0x00437C87  anchor: which hero is walking */
    MP_SITE_LIGHT_SET,              /* 0x0042E2F9  a script switches a level light */
    MP_SITE_EMITTER_SET,            /* 0x0042E346  a script switches an emitter placement */
    MP_SITE_SOUND_SET,              /* 0x0042E37B  a script switches a sound placement */
    /* The two heads a far Jedi's blade is drawn through with vertices of its own. The length tick
     * that used to write a far body's length into the shared mesh left this block when nothing
     * called it any more; its row went with it, which is why every site behind it moved up one. */
    MP_SITE_THING_DISPATCH,         /* 0x00417930  one render handle drawn; answers visibility */
    MP_SITE_HALO_DRAW_FOR_THING,    /* 0x00439A54  every glow card one object owns */
    MP_SITE_SHOT_SHATTER_THING,     /* 0x00456901  a body bursts into its pieces */
    MP_SITE_ENEMY_TICK_ANCHOR_CALL, /* 0x00432C30  the entity loop's live player test, a call */
    MP_SITE_COUNT
} mp_site_t;

/* The first site of the second table. Everything from here to MP_SITE_COUNT is a row of
 * mp_signatures_puppet.c, copied behind the main table before any lookup. */
#define MP_SITE_PUPPET_FIRST MP_SITE_BAPOBJ_DRAW_ALL


/* ==============================================================================================
 * The world probes, and why they are a SECOND enumeration rather than a third block of the first.
 *
 * mp_signatures_puppet.c hangs its rows behind the main table by index: the merge writes them at
 * MP_SITE_PUPPET_FIRST + i, and a static assert holds its length against
 * `MP_SITE_COUNT - MP_SITE_PUPPET_FIRST`. That arrangement has exactly one place a third block can
 * be spliced in, the merge itself, and the merge is a static function over a static array inside
 * mp_signatures.c. Growing mp_site_t without touching that function leaves rows in the merged
 * table that no file ever fills: they are all zero, and the resolver would walk them, search an
 * empty pattern and report three nameless sites as unresolved. A log line that names nothing is
 * worse than no line.
 *
 * So the three world probes carry their own enumeration and their own table, resolved by the
 * shared resolver in common/signature.c. They are declared here, beside mp_site_t, because this
 * is where a reader looks for "which engine entry points does the multiplayer feature reach for";
 * the patterns are in mp_signatures_world.c. Nothing else about them differs: address-free
 * patterns, absolute operands wildcarded, every one declared as a detour target so the two stage
 * rule holds if another module writes a branch over a head.
 *
 * All three answer a question about a point in the world, which is the reason they belong in one
 * table: where a re-entry may put a body.
 * ============================================================================================ */
/* ==============================================================================================
 * The conversation, a FIFTH enumeration, and it is one for the reason the third and fourth are.
 *
 * A block appended to mp_site_t would have to be spliced into the merge that hangs
 * mp_signatures_puppet.c behind the main table, and that merge has exactly one splice point.
 * Growing mp_site_t without touching it leaves rows nobody fills, which the resolver then walks
 * and reports as nameless unresolved sites.
 *
 * All three answer one question, what is being SAID, which is what makes them a table rather than
 * three loose lookups. The speak entry is hulled on both sides; the restart latch is only ever
 * called; the append is only ever read, for the three cells of the answer menu the host watches
 * its own answer through. Every one is a detour target in the table anyway so that the two stage
 * rule holds if another module writes a branch over its head. It does: `diagnostics` hulls the
 * speak entry too, when it is switched on.
 *
 * The speak entry and the restart latch both name the same two cells, [0x00882180] the speaker
 * lock and [0x008821BC] the restart latch, and each names both. Two patterns in two functions
 * agreeing about one address is a stronger claim than one pattern naming it twice.
 * ============================================================================================ */
typedef enum mp_dialog_site {
    MP_DIALOG_SITE_SPEAK_SINGLE,   /* 0x00430D12  "say this line": the one door a spoken line
                                    * takes, and its whole content is an index into the book */
    MP_DIALOG_SITE_FORCE_RESTART,  /* 0x00430E69  arms "re-open even for the same speaker", which
                                    * is what makes a replayed line play its voice and not only
                                    * refresh the subtitle */
    MP_DIALOG_SITE_ADD_CHOICE,     /* 0x00430E1B  appends one answer row AND switches the input to
                                    * menu mode; three operands name the block's active flag, its
                                    * row count and the row array, and the count is named twice.
                                    * Never hulled: the host reads its cells to watch its answer */
    MP_DIALOG_SITE_COUNT
} mp_dialog_site_t;

typedef enum mp_world_site {
    MP_WORLD_SITE_PROBE_FLOOR,        /* 0x0040BE00  fills the ground contact block; its signed
                                       * distance is the height of the floor over the point, and
                                       * 3.4e38 means there is no floor there at all */
    MP_WORLD_SITE_HEAD_CLEARANCE,     /* 0x0040C464  how much room is over a point, 0.0 for clear.
                                       * It only counts faces carrying the mask it is handed, and
                                       * the only mask the game ships is the low ceiling flag */
    MP_WORLD_SITE_WALKABLE_DISTANCE,  /* 0x0040DAA1  0.0 means the whole line is walkable; any
                                       * other value is the distance to the cell that stopped it */
    MP_WORLD_SITE_COUNT
} mp_world_site_t;

/* ==============================================================================================
 * The drawing surface, a FOURTH enumeration, for the same reason the third one exists.
 *
 * The merge that hangs mp_signatures_puppet.c behind the main table has one splice point, and it
 * is a static function over a static array inside mp_signatures.c. Growing mp_site_t without
 * touching that function leaves rows nobody fills, which the resolver then walks and reports as
 * unresolved sites with no name. So a new subject gets its own table and its own resolver.
 *
 * All nine answer one question: how does a mod put a rectangle and a line of text into the frame
 * the engine is already drawing. Seven are entry points and two are anchors whose operands carry
 * a cell address; the anchors are the reason the enumeration counts eleven answers rather than
 * nine, because the screen size anchor yields two cells.
 * ============================================================================================ */
typedef enum mp_hud_site {
    MP_HUD_SITE_DRAW_QUAD,      /* 0x00419660  a filled, blended rectangle in screen pixels */
    MP_HUD_SITE_DRAW_TEXT,      /* 0x0046B3C0  one string; x and y are fractions of the canvas */
    MP_HUD_SITE_FONT_SELECT,    /* 0x0046B13B  which of the sixteen font slots the setters mean */
    MP_HUD_SITE_FONT_COLOUR,    /* 0x0046B179  the colour of the next string */
    MP_HUD_SITE_FONT_ALIGN,     /* 0x0046B23C  0 centres, 1 starts at x, 2 ends at x */
    MP_HUD_SITE_FONT_GLYPH_SCALE, /* 0x0046B293  the glyph size, against the authored 640 by 480 */
    MP_HUD_SITE_FONT_POS_SCALE, /* 0x0046B2BA  what x and y are multiplied by before the canvas */
    MP_HUD_SITE_SYS_FONT,       /* 0x0046B74F  an anchor; its operand names the built in slot */
    MP_HUD_SITE_SCREEN_SIZE,    /* 0x00439476  an anchor; its two operands name width and height */
    MP_HUD_SITE_COUNT
} mp_hud_site_t;

/* The one mask the shipped game hands the head clearance probe, at all three of its call sites.
 * The probe drops every face that does not carry it, so a mask of 0 makes it answer "clear"
 * unconditionally rather than "no filter". */
#define MP_SURF_LOW_CEILING 0x0800u

/* What the ground probe writes for "there is no floor under this point". The engine's own float
 * maximum, compared for equality at its two readers rather than tested for magnitude. */
#define MP_PROBE_NO_FLOOR 3.4e+38f

/* The death latch is written by three sites and matched at two of them. The player's death writes
 * the pair, level outcome 4 and death cause 5, at 0x004503A8, 0x004503C3 and 0x004503F8; the
 * middle one loads the player pointer through ECX rather than EAX, and covering it would cost a
 * wildcard on the register byte for no gain. The recompile has the two at 0x00450348 and
 * 0x00450398. The value of the site is not the address: two matches, each naming two cells, have
 * to agree with each other, and the cell table checks that they do. */
#define MP_DEATH_LATCH_MATCHES 2u

/* Resolves every site. Returns how many resolved. Safe to call again; the second call redoes the
 * work and reports the same thing.
 *
 * What is always logged is the branch each site took, because that is the part a reader cannot
 * reconstruct afterwards: a site that did not resolve, and a site that resolved only because the
 * resolver fell back to its tail, which means another module had already written a branch over its
 * head. `log_every_site` adds the ordinary ones as well, one line of address per site. */
size_t mp_signatures_resolve(bool log_every_site);

/* 0 when the site did not resolve. Callers must check: every one of these is optional, and a site
 * that did not resolve disables whatever needed it rather than guessing. */
uintptr_t mp_signatures_address(mp_site_t site);

/* Both matches of the death latch. Index below MP_DEATH_LATCH_MATCHES; 0 when unresolved. */
uintptr_t mp_signatures_death_latch(size_t index);

/* The prologue length declared for a detour target, or 0 when the site is not one. */
size_t mp_signatures_prologue(mp_site_t site);

/* How many matches a site is expected to have. One, except for the death latch. */
size_t mp_signatures_expected_matches(mp_site_t site);

/* One site, or NULL for an index out of range. mp_cells.c reads operands through this. */
const signature_t *mp_signatures_site(mp_site_t site);

/* The whole merged table, MP_SITE_COUNT long and indexed by mp_site_t, so that a test can check
 * its shape without a game and the diagnostics can walk every site in one loop. */
const signature_t *mp_signatures_sites(size_t *count);

#endif /* MULTIPLAYER_MP_SIGNATURES_H */
