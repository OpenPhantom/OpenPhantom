/* input_freeze.h: stop the game reacting while the panel is open, at the layer that actually feeds
 * it.
 *
 * The first attempt at this took window messages and believed that was a lock. It is not: movement,
 * turning and firing are polled from the input device through a layer that never looks at the
 * message queue, which is also why the game's own cheat console does not stop the world either.
 *
 * So the freeze sits on the two functions the whole game asks for input through, and answers them
 * the way THEY answer before the device is ready: an axis reads as nothing, a delta reads as zero.
 * That is a state the engine already handles on every frame between startup and the first poll, so
 * nothing downstream meets a value it has not seen before.
 */
#ifndef INPUT_FREEZE_H
#define INPUT_FREEZE_H

#include <stdbool.h>
#include <stdint.h>

/* Places both detours. False when neither resolved, and then the panel still opens and still works,
 * it simply does not stop the player moving. That is worth having and the log says so. */
bool input_freeze_install(void);

/* Whether the freeze is armed at all, for the caller that has to be honest in its log line. */
bool input_freeze_is_available(void);

/* Who is asking. One bit each, because they can overlap: the panel while it is open, and the
 * free camera while it flies with the world running, when the flight keys must not reach the
 * player. */
typedef enum input_freeze_holder {
    INPUT_FREEZE_PANEL       = 1u << 0,
    INPUT_FREEZE_FREE_CAMERA = 1u << 1
} input_freeze_holder_t;

/* Takes or releases one holder. Both readers answer neutral while any holder has it. Idempotent
 * per holder, so a caller may drive it from its own state every frame. */
void input_freeze_hold(input_freeze_holder_t who, bool held);

#endif /* INPUT_FREEZE_H */
