/* focus_guard.h: the window changes hands and the engine is never told.
 *
 * Why this is a separate file from cursor_anchor.c
 *
 * cursor_anchor.c repairs one piece of engine arithmetic: the re-centring warp compares CLIENT
 * coordinates against a SCREEN point. That repair is complete and it is about a coordinate space.
 *
 * This file is about something else: who owns the foreground, and what has to happen at the moment
 * that changes. Two things do, and neither of them is a coordinate:
 *
 *   1. the pointer has to be held inside the client area while we are in front and let go the
 *      instant we are not, because the engine's only confinement is "warp it back when a mouse
 *      message arrives", and a pointer that crosses the window edge between two messages stops
 *      generating those messages;
 *   2. the DirectInput keyboard and mouse have to be re-acquired, because the engine opens them
 *      FOREGROUND, Windows unacquires them behind its back when the window goes to the background,
 *      and the retail build contains no code path that ever acquires them again.
 *
 * Both are consequences of one event, and the event is observed in one place, so they live in one
 * file. Neither belongs in cursor_anchor.c, which would then be doing two jobs.
 *
 * WHAT IT HOLDS: exactly one piece of process-global state, the ClipCursor rectangle, and only
 * while the game window is the foreground window. Every path out of that state is enumerated in
 * focus_guard.c above focus_guard_actions().
 */
#ifndef FOCUS_GUARD_H
#define FOCUS_GUARD_H

#include <stdbool.h>

typedef struct focus_guard_config {
    bool confine_pointer;   /* hold the pointer inside the client area while we are in front */
    bool reacquire_input;   /* send the engine its own resume pair when the foreground comes back */

    /* Changes NOTHING about the behaviour and only what the install line claims. A window left at
     * screen 0,0 at the size of the desktop has its client edge on the desktop edge, so the
     * confinement below has nothing to hold the pointer back from; the moment the window is moved
     * or shrunk it does. Both cases are worth having and only one of them is load-bearing, and a
     * log that does not distinguish them sends the next reader here for a fault that is elsewhere.
     * The input re-acquire is unaffected either way: it is broken in the retail build regardless of
     * where the window sits. */
    bool window_is_moved;
} focus_guard_config_t;

/* Resolves what it needs, registers the per-frame tick and logs which branch it took. Returns
 * true only when something is really being done from now on. */
bool focus_guard_install(const focus_guard_config_t *config);

/* Drops the pointer confinement. Safe to call when nothing was ever installed, and safe to call
 * from DLL_PROCESS_DETACH: it touches no engine memory and takes no lock. */
void focus_guard_shutdown(void);

/* ---- the decision, exposed because it is the part that can be tested without a window ------- *
 * The whole safety argument of this feature is one sentence, "a confinement we hold is released
 * on every path that takes the foreground away", and that sentence is a property of this
 * function alone. The caller only performs what it returns. */
typedef struct focus_guard_inputs {
    bool state_known;       /* false on the very first tick: there is no previous state yet */
    bool had_focus;         /* what the previous tick observed */
    bool has_focus;         /* what this tick observes */
    bool confining;         /* we are currently holding a ClipCursor rectangle */
    bool confine_wanted;    /* the configuration key, and it may be re-read at run time */
    bool reacquire_wanted;  /* the configuration key */
} focus_guard_inputs_t;

/* Macros rather than an enum: the result is carried as `unsigned`, and an enum constant is an
 * `int`, so every mask test and every comparison would be a signed/unsigned mix that /W4 is right
 * to complain about. */
#define FOCUS_ACTION_NONE      0x0u
#define FOCUS_ACTION_CONFINE   0x1u  /* apply or refresh the clip rectangle */
#define FOCUS_ACTION_RELEASE   0x2u  /* ClipCursor(NULL) */
#define FOCUS_ACTION_ACQUIRE   0x4u  /* the engine's own resume pair: setFocus(1) + resync */
#define FOCUS_ACTION_UNACQUIRE 0x8u  /* the engine's own suspend message: setFocus(0) */

unsigned focus_guard_actions(const focus_guard_inputs_t *inputs);

#endif /* FOCUS_GUARD_H */
