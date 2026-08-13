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

/* Freeze or release. While frozen, both readers answer neutral. */
void input_freeze_set(bool frozen);

#endif /* INPUT_FREEZE_H */
