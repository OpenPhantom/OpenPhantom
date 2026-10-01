/* mp_scene_rule.h: the decisions a scene gate makes, as arithmetic.
 *
 * Pure, so all of them run with no game in the process. The reading of the engine and the hulls
 * are in mp_cutscene.c, and for the rule at the end, which player a scene's script meant, in
 * mp_scene_watch.c; what is here is the decision, which is the part worth driving over every
 * value it can be given. The gathering's own state machines are in mp_scene_flow; what is here are
 * the single questions every one of them asks.
 */
#ifndef MULTIPLAYER_MP_SCENE_RULE_H
#define MULTIPLAYER_MP_SCENE_RULE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Whether the hero may be put back, from the engine's own store of the grab.
 *
 * The grab refuses its own work while the module state is nought, so it never parks a nought.
 * The store therefore reads nought exactly when nobody ever parked anything here, and any other
 * value means a grab is standing. One cell, one predicate, and no second state beside the
 * engine's to fall out of step with it.
 *
 * A put-back that is let through where the store is nought writes a nought into the module
 * state, and a player whose module is at nought cannot move, cannot turn and cannot be spawned
 * again inside that level. */
bool mp_scene_putback_allowed(uint32_t saved_module_state);

/* Where the hero is this module's business AT ALL, asked once for both halves of the pair.
 *
 * The grab and the put-back have to answer this the same way or the pair falls apart, and it did:
 * the grab was refused only on a client holding a scene back, while the put-back was refused
 * wherever the engine's store read nought, on every machine. A host has that store at nought at
 * the START of a scene, because the one caller of the put-back is the removal path and it asks for
 * every placement carrying the handover flag, whether a grab ever happened or not. So a host
 * refused its own put-back, and with it the two things the engine's body does besides the module
 * state: the resync of the record from the body, and the blade of the two sabre heroes. The field
 * had a host standing in the caption fade of a cutscene with a black screen and no way on.
 *
 * The lesson of 2026-09-20 was that whoever refuses a raise must refuse the matching lower in the
 * same file. It is not enough to put them in one file: they have to ask ONE question.
 *
 * The third reason is a host gathering the players for a scene: its grab waits until everybody is
 * there. It is in this function and nowhere else, because a grab refused for a reason the put-back
 * does not know is the field run of 2026-09-20 once more. While it holds, a removal of the waiting
 * actor asks the engine's store before it puts anything back; the module is running then, so a
 * refused put-back is harmless and the one the store allows writes a running module onto itself. */
bool mp_scene_hero_is_gated_here(bool client_holds, bool suppressed, bool gather_holds);

/* The lock level a script's scene takes. A menu takes level one on every render it draws, and that
 * is not a scene. One number for the gates, the scene watch and the gathering. */
#define MP_SCENE_LOCK_LEVEL 5

/* Whether a scene runs on this machine by its own cells: the lock at a script's level, or the
 * player module parked by a grab with a body under it. The body is part of it because the park
 * outlives the scene: the put-back never clears its store, so after the first scene of a level
 * every module at nought would read as parked, and a level end, which despawns the body and
 * leaves the module at nought, would read as a scene. A menu's lock of one is no scene, and
 * neither are death and the respawn, which write the module to other values. */
bool mp_scene_running(int32_t lock_level, uint32_t module_state, uint32_t saved_module_state,
                      bool has_body);

/* The mode a player is in, as far as moving him goes. */
typedef enum mp_scene_mode {
    MP_SCENE_MODE_UNREAD = 0,   /* the mode did not read, or its descriptors never resolved */
    MP_SCENE_MODE_PARKABLE,     /* standing, a sabre attack or Panaka: the three the engine's own
                                 * grab takes a player out of for a scene */
    MP_SCENE_MODE_DEATH,
    MP_SCENE_MODE_OTHER         /* a jump, a fall, the water, a ledge, a push block, a gun */
} mp_scene_mode_t;

/* Whether a player may be moved for a scene now, and if not, why not. */
typedef enum mp_scene_move {
    MP_SCENE_MOVE_YES = 0,
    MP_SCENE_MOVE_NO_BODY,      /* no body, or the module not running: a load, a respawn */
    MP_SCENE_MOVE_DEAD,
    MP_SCENE_MOVE_MODE          /* alive, in a mode the teleport would leave in a wrong place */
} mp_scene_move_t;

/* The one question the host asks before it seats itself and a client asks before it seats itself
 * or lets the mirror lock it. The teleport writes a position and clears the ground contact and
 * touches nothing else, so a body in a mode that owns its position, hanging off a ledge, riding a
 * gun, pushing a block, swimming or in the air, would be left in that mode at a place the mode
 * knows nothing about. The allowed list is the engine's own list for the same question: the modes
 * its grab parks a player from. A mode that could not be read is not allowed. `stands` is the
 * engine's live player test. */
mp_scene_move_t mp_scene_may_move(bool has_body, bool module_running, bool stands,
                                  mp_scene_mode_t mode);

/* Who asked the engine to respawn the player, by the address the call returns to. */
typedef enum mp_scene_respawn_caller {
    MP_SCENE_RESPAWN_BY_WARP = 0,   /* a script's warp, opcode 0x607 */
    MP_SCENE_RESPAWN_BY_SWAP,       /* the cheats' hero swap, which the lobby uses too */
    MP_SCENE_RESPAWN_BY_IMAGE,      /* any other caller inside the executable */
    MP_SCENE_RESPAWN_BY_DLL,        /* a caller outside it: this feature's own re-entry */
    MP_SCENE_RESPAWN_CALLERS
} mp_scene_respawn_caller_t;

/* A nought return address stands for a site that did not resolve and matches no caller. */
mp_scene_respawn_caller_t mp_scene_respawn_caller(uintptr_t caller, uintptr_t warp_return,
                                                  uintptr_t swap_return, bool inside_image);

/* Where a spawn of a placement carrying the handover flag came from, as the scene watch sorts it:
 * from outside the executable a DLL, from inside it with no script running a savegame being
 * restored, and from inside it with a script running the script's own spawn, which is the only
 * one that is a scene. */
typedef enum mp_scene_hero_origin {
    MP_SCENE_HERO_BY_SCRIPT = 0,
    MP_SCENE_HERO_BY_ENGINE,
    MP_SCENE_HERO_BY_DLL
} mp_scene_hero_origin_t;

mp_scene_hero_origin_t mp_scene_hero_origin(bool inside_image, bool script_running);

/* How many bits differ between two copies of a window of the campaign bank, and how many of those
 * fall in the band [band_first, band_last] of bank bits. `window_first_bit` is the bank bit of the
 * window's first byte; the bank is packed low bit first, which is the engine's own order. */
uint32_t mp_scene_bits_changed(const uint8_t *before, const uint8_t *after, size_t bytes,
                               uint32_t window_first_bit, uint32_t band_first,
                               uint32_t band_last, uint32_t *in_band);

/* Whether a session is under way: started from the lobby and not ended since. */
bool mp_scene_session_runs(bool started, bool ended);

/* Whether this machine is a client of that session, which is the question a scene gate and a
 * movie gate both ask: a scene on a client belongs to the host, and so does a movie.
 *
 * It is one function because it was about to be two. The scene gate has asked it since it was
 * written, inline in the pump that sets the gate; the movie gate needed the same answer in a
 * second pump. Two copies of one question give two answers the day one of them is edited, and
 * the lesson of the scene gate's own pair was that its halves must ask one question, not merely
 * sit in one file. */
bool mp_scene_client_of_a_started_session(bool started, bool ended, bool is_client);

/* Whether a camera take came from a script, by the address it will return to.
 *
 * The argument of the take says which group is wanted, never who asked; the return address says
 * who asked. The sites are handed in rather than written down here, so this file carries no
 * address and none of the three has to be right for it to answer.
 *
 * An empty list answers false for everything, which is the failing-open case: a build whose
 * sites did not resolve refuses nobody and plays as it did before. A nought address is never a
 * script, because a nought entry in the list is a site that was not found. */
bool mp_scene_camera_is_a_script(const uintptr_t *script_returns, size_t count, uintptr_t caller);

/* A near call: the opcode, then a displacement counted from the address behind the call. */
#define MP_SCENE_CALL_OPCODE 0xE8u
#define MP_SCENE_CALL_BYTES  5u

/* One script site as the camera hull reads it: the address its call returns to, and the five
 * bytes in front of that address, which ought to be the call. */
typedef struct mp_scene_call_site {
    uintptr_t return_address;
    uint8_t   call[MP_SCENE_CALL_BYTES];
} mp_scene_call_site_t;

/* What the calls say about where the camera take lives. */
typedef enum mp_scene_callee {
    MP_SCENE_CALLEE_AGREED = 0,   /* every site calls one address, and that is the answer */
    MP_SCENE_CALLEE_NO_SITES,     /* nothing to read it from */
    MP_SCENE_CALLEE_NOT_A_CALL,   /* a site whose five bytes are not a call, or call nought */
    MP_SCENE_CALLEE_DISAGREE      /* two sites call two different addresses */
} mp_scene_callee_t;

/* The address the script sites call, read out of their own call operands.
 *
 * The camera take is found this way rather than by its own bytes because its head is the one part
 * of it another module is entitled to overwrite: a DLL that detours it first leaves a branch where
 * the pattern's first bytes were, and what is left of the pattern is too common to name a
 * function. The callers are nobody's to change, and each of them names the function in its call.
 *
 * Every site has to name the same one. One site is enough to answer, and the caller says how many
 * the answer rests on; two sites naming two functions mean one of them is not the site it was
 * taken for, and nothing is answered rather than the wrong thing. `entry` is nought whenever the
 * answer is not MP_SCENE_CALLEE_AGREED. */
mp_scene_callee_t mp_scene_camera_callee(const mp_scene_call_site_t *sites, size_t count,
                                         uintptr_t *entry);

/* Whose a take of the camera by a script's dolly is, on the host. */
typedef enum mp_scene_camera_owner {
    MP_SCENE_CAMERA_OF_ITS_OWN = 0,   /* no scene runs: the take is a scene of its own */
    MP_SCENE_CAMERA_OF_THE_LOCK,      /* the lock stands at a script's level */
    MP_SCENE_CAMERA_OF_ALL            /* a scene the host gathered everybody for runs */
} mp_scene_camera_owner_t;

/* Inside a locked scene a take is that scene moving its camera, and inside a scene that runs for
 * everybody it is that scene's as well: once the players are gathered, the player who set the
 * scene off stands beside its actor and is its actor's freshest answer, so the take would read as
 * a far player's scene of its own and the host would lose the camera of its own scene. A gathered
 * hero scene need not raise the lock, which is why the lock alone does not answer. Only a take
 * outside both is weighed as a scene of its own. `for_all` is mp_scene_for_all. */
mp_scene_camera_owner_t mp_scene_camera_owner(int32_t lock_level, bool for_all);

/* ==============================================================================================
 * Which player a scene's script meant, the rule a host measures by before it moves anybody.
 * ============================================================================================ */

/* How long an actor's last answer about a player still speaks for its script, in substeps. One
 * second at 32 Hz: long enough for a test in one state, a change of state and the scene opcode on
 * the next tick; short enough that a proximity test from a fight a minute ago names nobody. It is
 * the first thing the field measurement is for, and nothing else rests on it yet. */
#define MP_SCENE_OWN_ANSWER_SUBSTEPS 32u

/* What the actor running a scene's script last heard when it asked for a player. */
typedef enum mp_scene_answer {
    MP_SCENE_ANSWER_NONE = 0,     /* no player kind was ever resolved for this actor */
    MP_SCENE_ANSWER_STALE,        /* resolved, but longer ago than a scene can be about */
    MP_SCENE_ANSWER_NOT_A_PLAYER, /* resolved lately, and the answer was an ally or nobody */
    MP_SCENE_ANSWER_PLAYER        /* resolved lately, with a player's body */
} mp_scene_answer_t;

/* `age` is the substep now less the substep of the answer, in unsigned arithmetic, so an answer
 * stamped after `now`, which only a restarted counter produces, reads as stale and not as fresh. */
mp_scene_answer_t mp_scene_answer_of(bool on_record, bool was_a_player, uint32_t age);

/* The rule that decided, in the order they are asked. */
typedef enum mp_scene_trigger_rule {
    MP_SCENE_BY_OWN_ANSWER = 0,   /* the actor's own last answer was a player, lately */
    MP_SCENE_BY_LAST_ATTACKER,    /* the actor died, and a player hurt it lately */
    MP_SCENE_BY_HOST_ANCHOR       /* nothing named a player: the world is the host's */
} mp_scene_trigger_rule_t;

/* What is known about the actor when its script begins a scene. The banks are this machine's:
 * 0 is its own player, 1 and up the far ones. */
typedef struct mp_scene_evidence {
    mp_scene_answer_t own;             /* its last answer about a player */
    uint8_t           own_bank;        /* whose body that answer was, for MP_SCENE_ANSWER_PLAYER */
    bool              died;            /* its health is at or below nought, which is what the
                                        * engine's own wait for a death tests */
    bool              attacker_known;  /* a player's body hurt it lately */
    uint8_t           attacker_bank;
} mp_scene_evidence_t;

/* Which bank's player the script meant, and by which rule. Only a player's body is ever the
 * answer, never an ally, and never "whoever stands nearest": a scene begun from a blackboard flag
 * is run by a machine that stands anywhere, and the nearest player to it is nobody's trigger.
 * Without a fresh answer of its own, an actor that died was meant for whoever killed it, and every
 * other scene is anchored on the host, whose world it is. `bank` is 0 for the host. */
mp_scene_trigger_rule_t mp_scene_trigger(const mp_scene_evidence_t *evidence, uint8_t *bank);

#endif /* MULTIPLAYER_MP_SCENE_RULE_H */
