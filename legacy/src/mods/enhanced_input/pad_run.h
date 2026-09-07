/* pad_run.h: walking on a light push and running on a full one.
 *
 * WHY IT IS A DETOUR AND NOT A SPEED. Running in this engine is not a faster walk, it is a
 * different branch. Plr_StandClipSelect asks control_isHeld for the Run action and, when it is
 * held, plays the runFwd clip, reports footstep state 2 and ramps the speed cap to 3.5; otherwise
 * it plays walkFwd, reports state 1 and ramps the cap to 2.0. So the gait is chosen by a BUTTON,
 * which j0nny's decompilation notes in as many words, and there is no speed at which the engine
 * decides to run of its own accord.
 *
 * WHY THE GAIT IS A STEP RATHER THAN A SLIDE. The clips play at a fixed rate: nothing in the engine
 * scales a clip by the speed the body is actually moving. A continuously variable pace would
 * therefore slide the feet along the ground, and the harder the player pushed the worse it would
 * look. Two gaits at their own authored speeds keep the feet planted, which is also exactly what
 * "push lightly to walk, fully to run" means on a console pad.
 *
 * WHAT THIS DOES. Chains control_isHeld and answers true for the Run action while the left stick is
 * pushed past its threshold. Everything else is passed straight through, including the Run action
 * itself when the stick is not pushed far enough, so a player who has bound Run to a key still has
 * it and the keyboard scheme is untouched. It never answers FALSE on its own account: a held key
 * reaching the original still wins.
 */
#ifndef ENHANCED_INPUT_PAD_RUN_H
#define ENHANCED_INPUT_PAD_RUN_H

#include <stdbool.h>

/* Optional. A failure is a named degraded mode, not a refusal: the stick still steers and still
 * carries a magnitude, the player simply keeps whatever gait the Run key gives them. */
bool pad_run_install(void);

#endif /* ENHANCED_INPUT_PAD_RUN_H */
