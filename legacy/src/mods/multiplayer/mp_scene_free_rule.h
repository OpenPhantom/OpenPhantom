/* mp_scene_free_rule.h: what of a scene's hold on a player is given back, as arithmetic.
 *
 * Layer 1, pure. A scene takes a player in five ways, each with a cell of its own in the engine
 * and a way back of its own: the lock, the input mode the lock sets, the bars, the camera's
 * override, and the player module, parked under an actor that drives the player's body. This is
 * the one decision about giving them back: out of one look at those cells and what is asked, the
 * plan of what is given back now. The look and the calls are mp_scene_free.c.
 *
 * The rules:
 *
 *   Nothing held, nothing planned. A lock or an input mode that did not read, -1, is not held.
 *
 *   The LOCK goes when it stands above nought, at `min_lock` or above, and no menu of the engine
 *   is open. A menu's close puts back the input mode its open found, the lock's, so a lock
 *   released under an open menu would leave that mode standing over a lock of nought.
 *
 *   The INPUT MODE goes when it is the lock's, no menu is open, and the lock is at nought or
 *   goes in this plan. No other mode is ever set back, a menu's own least of all. While a
 *   conversation's answers are open the mode is the conversation's, which sets it before it
 *   raises its lock. On a host the mode alone goes only for the button: the one way it is left
 *   standing there is a menu's close, which has a look of its own once a frame.
 *
 *   The BARS go when they are on.
 *
 *   Whether the CAMERA's override stands is not read, so it is cleared along with a lock or
 *   with the bars, and for the button always. Never at the tripod gun or for a dead player,
 *   whose override is the engine's own, never where the clearing did not resolve, and never
 *   from a look taken inside a bank window, which cannot tell a gun or a corpse.
 *
 *   The MODULE of a client goes with the actor that drives its body: a stopped module, a state
 *   in the engine's store, a body, and an actor that carries the handover bit on that body.
 *   The same picture on a host is the ACTOR, and only for whoever asks for it. A stopped module
 *   proves nothing by itself: the developer menu stops it as well.
 *
 *   The STORE of a client goes when it holds a state while the module is not stopped: nothing
 *   on a client parked that module, and the next put-back would write the store over whatever
 *   the module is by then.
 *
 *   A module that stands stopped with a body and NOBODY driving it goes for the button alone,
 *   and not while the developer menu holds the module itself.
 *
 *   Nothing that writes the player's record is planned inside a bank window, where that record
 *   is a far body's.
 *
 *   For the button, an open answer list of a conversation keeps its own lock and the camera:
 *   the engine would raise that lock again on the next frame, and the camera is the list's. A
 *   script's lock above it goes all the same.
 */
#ifndef MULTIPLAYER_MP_SCENE_FREE_RULE_H
#define MULTIPLAYER_MP_SCENE_FREE_RULE_H

#include <stdbool.h>
#include <stdint.h>

/* What is given back, as bits: of what is asked, of a plan, and of what was done. */
#define MP_SCENE_FREE_LOCK         0x0001u  /* the engine's release of the lock, at any level */
#define MP_SCENE_FREE_BARS         0x0002u  /* the letterbox told to go */
#define MP_SCENE_FREE_CAMERA       0x0004u  /* the camera's override cleared */
#define MP_SCENE_FREE_INPUT_MODE   0x0008u  /* the input mode set to play */
#define MP_SCENE_FREE_MODULE       0x0010u  /* a client: the actor that drives its body leaves by
                                             * the engine's removal, which puts the module back */
#define MP_SCENE_FREE_ACTOR        0x0020u  /* a host: that actor leaves at the end of its tick */
#define MP_SCENE_FREE_STORE        0x0040u  /* a client: a store nothing here wrote is cleared */
#define MP_SCENE_FREE_MODULE_ALONE 0x0080u  /* the button: a stopped module nobody drives */

#define MP_SCENE_FREE_BITS 8u

/* Everything a client that is never held asks for on every substep. */
#define MP_SCENE_FREE_CLIENT_ASKS                                                       \
    (MP_SCENE_FREE_LOCK | MP_SCENE_FREE_BARS | MP_SCENE_FREE_CAMERA |                  \
     MP_SCENE_FREE_INPUT_MODE | MP_SCENE_FREE_MODULE | MP_SCENE_FREE_STORE)

/* Why something that was asked for and is held was left standing, as bits. */
#define MP_SCENE_FREE_LEFT_MENU         0x01u  /* the lock: a menu of the engine is open */
#define MP_SCENE_FREE_LEFT_CONVERSATION 0x02u  /* a conversation's answers are open, and its lock,
                                                * its input mode or the camera is its own */
#define MP_SCENE_FREE_LEFT_BANK         0x04u  /* a bank window: the record is a far body's */
#define MP_SCENE_FREE_LEFT_OVERLAY      0x08u  /* the module: the developer menu holds it itself */
#define MP_SCENE_FREE_LEFT_GUN          0x10u  /* the camera: the tripod gun's override */
#define MP_SCENE_FREE_LEFT_DEAD         0x20u  /* the camera: a dead player's */
#define MP_SCENE_FREE_LEFT_UNBOUND      0x40u  /* the camera: its clearing did not resolve */

/* One look at what holds this machine's own player. */
typedef struct mp_scene_free_look {
    bool     client;          /* a client of a started session */
    bool     menu_open;       /* a menu of the engine */
    int32_t  lock_level;      /* -1 where it does not read or cannot be released */
    int32_t  input_mode;      /* -1 where it does not read */
    bool     bars_on;
    bool     camera_bound;    /* the clearing of the camera's override resolved */
    bool     dead;
    bool     at_gun;
    uint32_t module;          /* the player module's state, 0 stopped and 1 running */
    uint32_t store;           /* the engine's store of a parked module's state */
    bool     has_body;
    bool     driven;          /* an actor with the handover bit drives the player's body */
    bool     bank_window;
    bool     conversation;    /* a conversation's answer list is open */
    bool     overlay_holds;   /* the developer menu holds the module itself */
    bool     button;          /* the player asks for himself, through the developer menu */
} mp_scene_free_look_t;

/* What of `asked` is held now and may be given back; nought when nothing is. `min_lock` is the
 * lowest lock level that counts: the script's level for a client that is never held, one for
 * the button. `left_because`, when given, says why something asked for and held was left. */
uint32_t mp_scene_free_plan(const mp_scene_free_look_t *look, uint32_t asked, int32_t min_lock,
                            uint8_t *left_because);

/* ==============================================================================================
 * Where the two releases live, read out of the two script ends.
 * ============================================================================================ */

/* A script gives a scene back at two places, the end of the camera dolly opcode and the end of
 * the lock opcode, and both are the same twelve bytes in front of the address the lock's release
 * returns to: `call` the clearing of the camera's override, `push imm8` the level, `call` the
 * lock's release. Five, two and five bytes. */
#define MP_SCENE_FREE_END_BYTES 12u
#define MP_SCENE_FREE_ENDS      2u

/* One end as the binding reads it. `read` is false for an end whose place did not resolve or
 * whose bytes would not read, and such an end proves nothing either way. */
typedef struct mp_scene_free_end {
    bool      read;
    uintptr_t return_address;                    /* where its call of the release returns to */
    uint8_t   bytes[MP_SCENE_FREE_END_BYTES];    /* the twelve bytes in front of that address */
} mp_scene_free_end_t;

/* What the ends say about the lock's release found at `release`. */
typedef struct mp_scene_free_ends {
    bool      release_holds;   /* no end calls another address where the release should be */
    uint32_t  witnesses;       /* ends whose last five bytes call `release` */
    uintptr_t disagreeing;     /* the address an end calls instead, 0 when none does */
    uintptr_t camera_off;      /* the clearing of the camera's override, 0 where not proven */
} mp_scene_free_ends_t;

/* The lock's release is found by its own pattern and held against the ends: an end that calls
 * another address means one of the two is not what it was taken for, and then the release is not
 * to be called. The clearing of the camera's override has no pattern of its own and is taken
 * from the ends alone, so it needs both of them: each a witness of the release, each with the
 * push in its place, and both calling one address that is not the release itself. Whether that
 * address lies in the image is the binding's to ask. */
void mp_scene_free_read_ends(uintptr_t release, const mp_scene_free_end_t ends[MP_SCENE_FREE_ENDS],
                             mp_scene_free_ends_t *out);

#endif /* MULTIPLAYER_MP_SCENE_FREE_RULE_H */
