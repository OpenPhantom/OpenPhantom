/* mp_pause_rule.h: the pause menu of a session, as rules with no engine under them.
 *
 * Layer 1. In a session the pause menu does not stop the world: nobody may pause it for everybody
 * when any number of players share it. The menu runs over the world as the engine's own cheat
 * console does, and only this player's input is held; the body goes on being simulated, falls,
 * is hit and can be killed. Everything that decides how that behaves without touching the engine
 * is here, so a test can drive every way in and every way out.
 *
 * Four decisions live here:
 *
 *   which way a press of the pause key goes: the engine's own pause, a session's, or nowhere
 *   because a session's is already up;
 *   which action ids the readers hold while it is up: every id below the menus' own;
 *   when the menu closes itself: a death, a lock that rises, a level outcome that leaves
 *   running, or the end of the session, sampled every drawn frame and latched until it is left;
 *   what the one exit writes: exactly the cells the engine's own pause writes for the same answer.
 *
 * Opening and leaving also say the hold, through mp_armed and as the pause menu's own holder,
 * because that hold and the menu being up are one state and they have one way in and one way out.
 * A hold the chat has taken is its own and is left standing. Nothing else here has a side effect.
 */
#ifndef MULTIPLAYER_MP_PAUSE_RULE_H
#define MULTIPLAYER_MP_PAUSE_RULE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The first action id the menus read. 0x1A to 0x1D are the pad's moves through a menu, read as
 * hold times, and 0x1E to 0x20 its menu buttons, read as held keys. Every id below is the player's
 * or the dialogue's: the two axes, the actions up to 0x15, and 0x16 to 0x18, which the dialogue
 * reads every frame and which are bound to the same keys that work the menu. */
#define MP_PAUSE_FIRST_MENU_ACTION 0x1A

/* The navigation code every screen of the engine's menus reads as "back". */
#define MP_PAUSE_NAV_CANCEL 6

/* What the engine's pause does with the menu's answer. 6 is "leave the level": the outcome goes to
 * 3 and the restore flag to 1, and the simulation gate is left held. Every other answer frees the
 * gate. The pattern of sys_pause pins these numbers byte for byte. */
#define MP_PAUSE_REPLY_QUIT    6
#define MP_PAUSE_OUTCOME_QUIT  3u
#define MP_PAUSE_RESTORE_QUIT  1u
#define MP_PAUSE_GATE_HELD     1u
#define MP_PAUSE_GATE_FREE     0u

/* The level outcome while a level runs. Anything else means the level is over or ending. */
#define MP_PAUSE_OUTCOME_RUNNING 2u

/* How long a close is forced before it is given up. Leaving a tab takes about 0.37 s of sliding,
 * during which no screen reads a code at all, so this is several times the longest ordinary way
 * out. A screen that does not act on a cancel, a message box for instance, is then left to the
 * player. */
#define MP_PAUSE_FORCE_GIVE_UP_MS 3000u

/* A frame drawn this long after the last substep is a stall and not the ordinary gap between two
 * substeps, which is 31 ms. A message box and a load are the two ways the world stops under a
 * session's pause menu. */
#define MP_PAUSE_STALL_MS 100u

/* Whether a reader holds this action id while a session's pause menu is up. */
bool mp_pause_rule_holds_action(int32_t action);

/* Which way a press of the pause key goes. */
typedef enum mp_pause_way {
    MP_PAUSE_WAY_ENGINE = 0,      /* no transport: the engine's own pause, nothing else */
    MP_PAUSE_WAY_ENGINE_UNHELD,   /* a session with no input split: the engine's own, said once */
    MP_PAUSE_WAY_ALREADY_OPEN,    /* a session's menu is up already: nothing */
    MP_PAUSE_WAY_SESSION          /* the menu over a running world */
} mp_pause_way_t;

mp_pause_way_t mp_pause_rule_way(bool open, bool transport, bool input_split);

/* Why the menu closes itself. The number indexes the report's counters; NONE is the player's own
 * way out. */
typedef enum mp_pause_reason {
    MP_PAUSE_REASON_NONE = 0,
    MP_PAUSE_REASON_DEATH,       /* the body is a corpse, or its health is gone while the
                                  * player module runs */
    MP_PAUSE_REASON_SCENE,       /* a dialogue or scene lock rose while it was up */
    MP_PAUSE_REASON_LEVEL,       /* the level outcome left running */
    MP_PAUSE_REASON_SESSION,     /* the transport went down */
    MP_PAUSE_REASON_COUNT
} mp_pause_reason_t;

const char *mp_pause_rule_reason_text(mp_pause_reason_t reason);

/* One look at the world under the menu, taken every drawn frame. A field that could not be read
 * says so and is not judged. */
typedef struct mp_pause_look {
    uint32_t now_ms;
    uint32_t substeps;       /* a count of substeps that only grows */
    bool     gate_held;      /* the simulation gate: a load from inside the menu holds it */
    bool     health_read;
    int32_t  health;
    bool     dead;           /* the body is the engine's corpse */
    bool     module_running; /* the player module reads 1, the one state in which the engine
                              * judges the health at all; false where it did not read */
    bool     lock_read;
    int32_t  lock;           /* the dialogue and scene lock level */
    bool     outcome_read;
    uint32_t outcome;
} mp_pause_look_t;

/* One cell the exit writes, in the order the engine's own pause writes them. */
typedef enum mp_pause_cell_written {
    MP_PAUSE_WRITE_OUTCOME = 0,
    MP_PAUSE_WRITE_RESTORE,
    MP_PAUSE_WRITE_GATE
} mp_pause_cell_written_t;

typedef struct mp_pause_write {
    mp_pause_cell_written_t cell;
    uint32_t                value;
} mp_pause_write_t;

#define MP_PAUSE_EXIT_WRITES_MAX 3u

/* What the last time the menu was left looked like, for its line. */
typedef struct mp_pause_left {
    int32_t           reply;
    mp_pause_reason_t reason;
    uint32_t          duration_ms;
    uint32_t          frames;
    uint32_t          gate_frames;
    uint32_t          stalled_frames;
    uint32_t          longest_stretch_ms;
    uint32_t          substeps;
    uint32_t          cancels;
    bool              loaded;        /* the gate was held on some frame: a savegame was loaded */
} mp_pause_left_t;

typedef struct mp_pause_session {
    /* The one opening that is up, if any. */
    bool              open;
    uint32_t          opened_ms;
    uint32_t          opened_substeps;
    int32_t           lock_floor;        /* the lowest lock since the opening; above it is a rise */
    mp_pause_reason_t reason;            /* latched until the menu is left */
    uint32_t          reason_ms;
    uint32_t          cancels;           /* cancels handed to the screens for the reason */
    bool              given_up;
    bool              loaded;
    uint32_t          frames;
    uint32_t          gate_frames;
    uint32_t          stalled_frames;
    uint32_t          last_substeps;
    uint32_t          last_substep_ms;
    uint32_t          longest_stretch_ms;
    bool              kept_open_parked;  /* no health, and no running module to judge it */

    /* Every opening so far. */
    uint32_t          opened_total;
    uint32_t          held_note_refusals;  /* openings whose hold the session note refused */
    uint32_t          left_to_play;
    uint32_t          left_after_load;
    uint32_t          left_to_quit;
    uint32_t          refused_by_engine;
    uint32_t          closed_by[MP_PAUSE_REASON_COUNT];
    uint32_t          given_up_total;
    uint32_t          rearmed_total;       /* give ups a key of the player's started over */
    uint32_t          frames_total;
    uint32_t          gate_frames_total;
    uint32_t          stalled_frames_total;
    uint32_t          longest_stretch_total_ms;
    uint32_t          substeps_total;
    uint32_t          kept_open_parked_total;   /* openings that stayed open for that */
    mp_pause_left_t   last;
} mp_pause_session_t;

/* The menu goes up: the per-opening record starts over and the hold is said. False when it was up
 * already. `note_said` receives whether the session note took the hold; the hold stands in this
 * process either way, and only another mod's input path is left without it. */
bool mp_pause_rule_open(mp_pause_session_t *session, bool lock_read, int32_t lock,
                        uint32_t substeps, uint32_t now_ms, bool *note_said);

/* One drawn frame under the menu. Counts it, and latches the first close reason it finds. Nothing
 * is judged on a frame with the gate held, because a load from inside the menu rewrites the very
 * cells a reason is read from.
 *
 * A death is what the engine lets in: the corpse flag, or no health while the player module runs.
 * The engine judges the health only in the first phase of the running module, so a player a scene
 * has parked, or one whose module is dying or respawning, is not dead by his health. A menu that
 * closed itself for him on every opening left him no way out at all; such an opening stays open
 * and is marked once. */
void mp_pause_rule_look(mp_pause_session_t *session, const mp_pause_look_t *look);

/* The transport went down under the menu. True when this latched the reason. */
bool mp_pause_rule_session_ended(mp_pause_session_t *session, uint32_t now_ms);

/* What a screen's navigation read answers while the menu is up. The player's own code always goes
 * where it was going; on a frame with no code of the player's a latched reason answers a cancel,
 * until the give up time has passed. After a give up the player's next code starts the close over.
 * `gave_up` receives whether this call gave the close up. */
int32_t mp_pause_rule_nav(mp_pause_session_t *session, int32_t code, uint32_t now_ms,
                          bool *gave_up);

/* The one way out, whatever the answer and whatever the reason: fills `writes` with the cells the
 * engine's own pause writes for this answer, releases its hold, counts the leaving, remembers it
 * in `last` and clears the opening. Returns how many writes were filled, 0 when the menu was not
 * up. */
size_t mp_pause_rule_leave(mp_pause_session_t *session, int32_t reply, uint32_t now_ms,
                           mp_pause_write_t *writes, size_t capacity);

/* Which pump the player list runs a frame with. The engine's own screens choose by the backdrop
 * cell: nought runs the world's frame, anything else the menu's own. The list follows that rule,
 * and only over a session's pause menu. */
typedef enum mp_pause_pump {
    MP_PAUSE_PUMP_MENU = 0,
    MP_PAUSE_PUMP_WORLD
} mp_pause_pump_t;

mp_pause_pump_t mp_pause_rule_pump(bool session_menu_open, bool backdrop_read,
                                   uint32_t backdrop, bool world_pump_known);

#endif /* MULTIPLAYER_MP_PAUSE_RULE_H */
