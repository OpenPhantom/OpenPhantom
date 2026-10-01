/* input_owner.h: who has the pointer, the wheel and the panel's picture right now.
 *
 * Four parties want some of them. The game, while the panel is shut. The panel, open and drawn,
 * which takes the wheel to scroll and cages the pointer. The free camera, whose flight hides the
 * panel when the player asks and takes the mouse to look. And the entity spawner's placement mode,
 * which hides the panel and takes the pointer to point into the world and the wheel to turn.
 *
 * Each of the places that acts on one of them used to ask its own question: the paint asked "open
 * and not hidden", the cage the same, the free camera took the wheel whenever it flew. With the
 * placement mode a third drawer came, and three conditions at three places for one state is how the
 * two halves of a rule start answering differently. So there is one answer, input_owner_now(), and
 * the paint, the scroll, the cage, the placement mode and the pause take it from here. The free
 * camera does not ask: it takes the wheel and the mouse while it flies, and the rule below gives
 * them to it whenever it flies, so the two cannot disagree.
 *
 * The rules, in the order they are decided:
 *
 *   the panel is shut                          the game
 *   the free camera flies                      the free camera if the player hid the panel for
 *                                              it, else the panel; a placement mode that is on
 *                                              waits, since the camera has the mouse
 *   the placement mode is on                   the placement mode
 *   otherwise                                  the panel
 *
 * The panel is drawn only while it owns them, and it holds the simulation paused while it, the
 * camera or the placement mode owns them (the camera's flight has its own hold besides), in a
 * single player game only: in a session the world is not this machine's to stop (session_lock.c).
 * The placement mode is a state of the panel and holds the world as the panel does in a single
 * player game. It lets go for the settle after each copy it places, two substeps (spawn_place.h),
 * so the copy stands where it was put in the held picture.
 * The player stays held by the input freeze throughout.
 *
 * Internal to dev_overlay.
 */
#ifndef DEV_OVERLAY_INPUT_OWNER_H
#define DEV_OVERLAY_INPUT_OWNER_H

#include <stdbool.h>
#include <stdint.h>

typedef enum input_owner {
    INPUT_OWNER_GAME = 0,
    INPUT_OWNER_PANEL,
    INPUT_OWNER_FREE_CAMERA,
    INPUT_OWNER_PLACEMENT
} input_owner_t;

typedef struct input_owner_facts {
    bool panel_open;
    bool hidden_for_flight;   /* the player hid the panel while the camera flies */
    bool free_camera;
    bool placing;             /* the placement mode is on */
} input_owner_facts_t;

/* The rule, pure. */
input_owner_t input_owner_decide(const input_owner_facts_t *facts);

/* Whether the panel's picture is hidden under an owner, and whether the panel holds the simulation
 * paused under it; `settling` is the placement mode's settle after a copy is placed, the one time
 * it lets the world run. */
bool input_owner_hides_panel(input_owner_t owner);
bool input_owner_panel_pauses(input_owner_t owner, bool settling);

/* The owner now, from the panel, the free camera and the placement mode as they stand. */
input_owner_t input_owner_now(void);

/* The player's own hiding of the panel during a flight: toggled by the keys that would otherwise
 * close it, cleared by every open and close, and ended with the flight, which is noticed here and
 * said once. */
void input_owner_toggle_hidden_for_flight(void);
void input_owner_panel_opened(void);
bool input_owner_free_camera_holds_panel(void);

/* The wheel: every message the panel's hook sees is shown here, and the notches since the last take
 * are handed to whoever asks, which is the owner of the wheel. Positive away from the player. */
void    input_owner_observe_wheel(int32_t message, int32_t wparam);
int32_t input_owner_take_wheel(void);

/* Once a frame, and at every open and close: the panel's pause hold made to agree with the
 * owner. */
input_owner_t input_owner_sync(void);

#endif /* DEV_OVERLAY_INPUT_OWNER_H */
