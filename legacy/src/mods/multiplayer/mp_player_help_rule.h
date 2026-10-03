/* mp_player_help_rule.h: the two buttons of the developer menu, as arithmetic.
 *
 * Layer 1, pure. The developer menu has two buttons under its Multiplayer heading, "Repair lock"
 * and "Teleport to host". A press reaches this feature as a record the other DLL files, and is
 * answered in a second record (common/player_help_note). Every decision about a press is here,
 * so a test walks each of them with no game in the process; the reading of the engine and the
 * calls into it are mp_player_help.c, mp_repair_lock.c and mp_teleport_host.c.
 *
 * The reader:
 *
 *   The records live as long as the process, so a press made in single player or in the session
 *   before this one is still on file when a session arms its reader. The reader takes the serial
 *   it finds at that moment as its MARK and acts only on a press made after the mark, each one
 *   once. No record at all is a mark of nought, and the first press carries one.
 *
 * Repair lock, on either role:
 *
 *   It gives this machine's own player back what holds his controls and his camera. What the
 *   engine holds is planned by mp_scene_free_rule, asked with the button set; what the mod holds
 *   beside it is left first: a scene of the host's, a teleport under way, the chat's open line,
 *   and an input hold whose owner is gone.
 *
 *   NOTHING TO DO means that the plan without its camera bit is empty, no state of the mod was
 *   left and no hold fell. Whether the camera's override stands cannot be read, so it is cleared
 *   on every press all the same, except at the tripod gun and for a dead player, and it does not
 *   count as something that held.
 *
 *   On a host the actor that drives the player's body is told to leave, which ends its script.
 *   Its script runs once more before it goes, so the host's scene is asked to latch its doors for
 *   a while after: but only when something was taken back from the engine, the lock, the bars or
 *   that actor. A press that only left the mod's own state, a host still on his way to a scene,
 *   lets the scene play as the engine takes it, and a second press then lets go of its actor.
 *
 * Teleport to host, on a client:
 *
 *   The table of refusals below is asked at the press and again on every look of the search for
 *   a place, so whatever would have refused the press ends the search as well. Only a place
 *   beside the host is taken: the seat search falls back to an authored point of the level when
 *   it finds none, and a teleport that ended there would put the player at the level's start.
 */
#ifndef MULTIPLAYER_MP_PLAYER_HELP_RULE_H
#define MULTIPLAYER_MP_PLAYER_HELP_RULE_H

#include "mp_scene_flow.h"
#include "mp_scene_free_rule.h"
#include "mp_seat_rule.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* What a press came to, in the words of the answer's record: a PLAYER_HELP_OUTCOME_* and a
 * PLAYER_HELP_REASON_*. */
typedef struct mp_player_help_verdict {
    uint8_t outcome;
    uint8_t reason;
} mp_player_help_verdict_t;

/* ==============================================================================================
 * The reader.
 * ============================================================================================ */

/* The mark a reader takes as it arms: the serial on file, or nought when no ask was found. */
uint32_t mp_player_help_mark(bool found, uint32_t serial);

/* Whether an ask on file is a press to act on: one of the two kinds, with a serial, made after
 * `mark`. The caller moves its mark to that serial as it acts, so each press is acted on once
 * however often it is read. */
bool mp_player_help_is_new(uint8_t kind, uint32_t serial, uint32_t mark);

/* Whether the ask is read on this frame. Until a read has found the note once, a read costs a
 * failed lookup of a name nobody filed, so it is tried at most once in
 * PLAYER_HELP_NOTE_RETRY_MS; after that every frame reads, which is a copy. `since_ms` is the
 * time since the last try, taken as a difference so the clock's wrap does not matter. */
bool mp_player_help_read_due(bool found_once, bool tried, uint32_t since_ms);

/* The ready bits of the answer: that a reader listens, that a repair would be carried out, and
 * that a teleport would. A teleport is a client's alone. */
uint8_t mp_player_help_ready(bool repair_bound, bool is_client, bool teleport_bound);

/* A reason as a line of the log says it. Never NULL; a value outside the list reads as none. */
const char *mp_player_help_reason_text(uint8_t reason);

/* ==============================================================================================
 * Repair lock.
 * ============================================================================================ */

/* What a repair left of the mod's own state, as bits beside the eight of mp_scene_free_rule.
 * Together the two sets are the `released` word of the answer. */
#define MP_REPAIR_LEFT_SCENE    0x0100u   /* a host: what the mod held of a scene */
#define MP_REPAIR_LEFT_TELEPORT 0x0200u   /* a teleport to the host that was under way */
#define MP_REPAIR_CLOSED_CHAT   0x0400u   /* the chat's open line */
#define MP_REPAIR_DROPPED_PAUSE 0x0800u   /* the pause menu's input hold with no menu open */
#define MP_REPAIR_DROPPED_CHAT  0x1000u   /* the chat's input hold with no line open */

#define MP_REPAIR_OF_THE_MOD                                                            \
    (MP_REPAIR_LEFT_SCENE | MP_REPAIR_LEFT_TELEPORT | MP_REPAIR_CLOSED_CHAT |           \
     MP_REPAIR_DROPPED_PAUSE | MP_REPAIR_DROPPED_CHAT)

/* How long after a press the player is looked at again when an actor was told to leave, in
 * seconds of the world's own clock: three substeps of a thirty-second of a second, and a little
 * more. The engine removes the actor at the end of its next tick, so a module that still stands
 * stopped under it by then means the actors do not tick. The world's clock and not a count of
 * substeps, because the count this feature can read between two substeps stands still on a host
 * nobody has joined. */
#define MP_REPAIR_LOOK_AGAIN_SECONDS 0.1f

/* How long after a press the latch's catch is said, in seconds of the same clock: the latch
 * stands for sixty four substeps, which is two seconds at thirty two of them a second and less
 * with a shorter substep, and a little more. */
#define MP_REPAIR_LATCH_SECONDS 2.25f

/* Whether a repair is refused at the door, and why: with no level running there is nothing of
 * the engine's to give back, and without the lock's release bound nothing could be carried out.
 * False lets it through, and `reason` is none then. */
bool mp_repair_refused(bool level_running, bool bound, uint8_t *reason);

/* Everything the button asks mp_scene_free_rule for. A client's actor is removed here and takes
 * the module with it, and a store nothing here wrote is cleared; a host's actor is told to
 * leave. */
uint32_t mp_repair_asks(bool is_client);

/* The input holds nobody owns any more, as mp_armed_holder_t bits out of `holders`: the pause
 * menu's with no menu of the engine open, and the chat's with no line open. The hold of a
 * host's scene is never one of them: it is decided again on every substep by the scene. */
uint32_t mp_repair_orphans(uint32_t holders, bool menu_open, bool chat_typing);

/* Whether a press let go of nothing: nothing of the engine's but the camera, and nothing of the
 * mod's own. `freed` is mp_scene_free's bits, `of_the_mod` the MP_REPAIR_* bits. */
bool mp_repair_found_nothing(uint32_t freed, uint32_t of_the_mod);

/* Whether the host's scene latches its doors after this press: on a host, and only when the
 * lock, the bars or the driving actor was taken back from the engine. `given` is what
 * mp_scene_free_now answered. */
bool mp_repair_latches(bool is_client, uint32_t given);

/* Whether a press only left what the mod held of a scene, with nothing of the engine's taken
 * back: the host was still on his way, the scene plays from here as the engine takes it, and a
 * second press lets go of its actor. */
bool mp_repair_only_left_the_mod(uint32_t of_the_mod, uint32_t given);

/* Why something was left standing, as the answer's one reason, out of the bits the plan left:
 * an open menu first, then a conversation, the developer menu's own hold, the gun, a dead
 * player, and a clearing of the camera that is not bound. `menu_holds` says that the pause
 * menu's input hold stands under its open menu, which is an open menu as well: the player is
 * held, by a menu he closes himself. None when nothing was left. */
uint8_t mp_repair_reason(uint8_t left_because, bool menu_holds);

/* The answer of a repair that was let through, out of what was done and not out of what was
 * planned: done when something was let go, with why the rest was left standing; nothing to do
 * when nothing was planned; and refused as not bound when the engine carried out none of a
 * plan. `given` is what mp_scene_free_now answered for `plan`. */
mp_player_help_verdict_t mp_repair_verdict(uint32_t plan, uint32_t given, uint32_t of_the_mod,
                                           uint8_t left_because, bool menu_holds);

/* The answer's `released` word: what the engine gave back in the low byte, what the mod left
 * above it. */
uint16_t mp_repair_released(uint32_t given, uint32_t of_the_mod);

/* What of a plan was not carried out. The input mode is not missed where the lock was released:
 * the engine's release sets the mode to play as the lock falls, and the setter is then left
 * uncalled. */
uint32_t mp_repair_not_carried_out(uint32_t plan, uint32_t given);

/* What a look after a press does. */
typedef enum mp_repair_after {
    MP_REPAIR_AFTER_WAIT = 0,   /* not enough of the world's time has passed */
    MP_REPAIR_AFTER_DUE,
    MP_REPAIR_AFTER_DROP        /* the world of the press is gone, or its clock does not read */
} mp_repair_after_t;

/* Whether a look after a press is due: `wait_seconds` of the world's clock after `then`, in the
 * same world. A clock that reads less than at the press is another level's. */
mp_repair_after_t mp_repair_after(bool world_reads, bool same_world, float then, float now,
                                  float wait_seconds);

/* What became of an actor that was told to leave, by a second look at the player. */
typedef enum mp_repair_actor {
    MP_REPAIR_ACTOR_LEFT = 0,       /* the module runs: the engine removed it and put him back */
    MP_REPAIR_ACTOR_STILL_DRIVES,   /* stopped and still driven: the actors do not tick */
    MP_REPAIR_ACTOR_LEFT_STOPPED    /* nobody drives, and the module still stands stopped */
} mp_repair_actor_t;

mp_repair_actor_t mp_repair_actor_after(const mp_scene_free_look_t *look);

/* ==============================================================================================
 * Teleport to host.
 * ============================================================================================ */

/* A player this near the host already stands beside him: the reach of the seat search's own
 * outer ring. A place it would hand out lies no further off, so a player inside it gains nothing
 * from a move. */
#define MP_TELEPORT_NEAR MP_SEAT_RING_FAR

/* How long a place beside the host is searched for, in substeps: five seconds. The seat search's
 * own bounds are three seconds of empty looks on a good anchor, after which it falls back, and
 * twenty in all while the host rides a lift, falls or swims; a button waits for neither. */
#define MP_TELEPORT_SEARCH_SUBSTEPS 160u

/* What the door of a teleport looks at. */
typedef struct mp_teleport_look {
    bool  is_client;          /* this machine is a client of somebody else's world */
    bool  level_of_session;   /* a level of a started session runs here */
    bool  bound;              /* the seat's probes and the scene's fade and modes resolved */
    bool  dead;               /* this machine's own player is a corpse */
    bool  busy;               /* a re-entry or a move of a living player is under way */
    bool  teleporting;        /* a teleport of this player is under way already */
    bool  at_gun;
    bool  overlay_holds;      /* the ask said the developer menu still holds the player */
    bool  host_elsewhere;     /* the pose kept for the host is another world's */
    bool  host_pose;          /* a pose of the host in this world */
    bool  host_dead;
    bool  host_stands;        /* alive and not dead, in the host's own words */
    bool  distance_known;     /* both poses read */
    float distance;           /* from this player to the host */
} mp_teleport_look_t;

/* The door: OPEN lets the teleport begin; every other outcome is the answer, with its reason.
 * In this order: the host himself has nowhere to go (nothing to do); no level of a started
 * session; nothing bound; this player dead, whom the re-entry brings back beside a standing
 * player anyway; a re-entry, a move or another teleport under way; at the tripod gun, whose
 * camera and turret a move would leave behind; held by the developer menu; the host in another
 * level; no pose of the host yet; the host dead or not standing; and beside him already (nothing
 * to do). A distance that is not a finite number is not near. */
mp_player_help_verdict_t mp_teleport_door(const mp_teleport_look_t *look);

/* What one look of the search comes to. */
typedef enum mp_teleport_search {
    MP_TELEPORT_SEARCH_GOES_ON = 0,
    MP_TELEPORT_SEARCH_SEATED,    /* a place beside the host: the move begins */
    MP_TELEPORT_SEARCH_NO_SEAT    /* given up: the search left the host, or its time ran out */
} mp_teleport_search_t;

/* `found` is the seat search's answer to this look and `beside_the_anchor` whether its wish is
 * still in its first stage after the look, which is asked before a seat is used: the wish moves
 * on to its fallback inside a look that found nothing. A seat found on the last substep of the
 * bound is taken. */
mp_teleport_search_t mp_teleport_search(bool found, bool beside_the_anchor, uint32_t substeps);

/* Whether the move has ended, by the stage of its seat, and how: done, or given up. */
bool mp_teleport_moved(mp_scene_seat_stage_t stage, mp_player_help_verdict_t *verdict);

/* Why a teleport that something else ended is refused: the level changed under it, or it was
 * given up in the world it began in. */
uint8_t mp_teleport_left_reason(bool world_changed);

#endif /* MULTIPLAYER_MP_PLAYER_HELP_RULE_H */
