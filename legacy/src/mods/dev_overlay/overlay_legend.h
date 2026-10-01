/* overlay_legend.h: what stands in the panel's footer, as text and nothing else.
 *
 * The panel could be driven from the keyboard for a while before anything said so. The arrows, the
 * two sideways keys, Return and Escape all do something, and the only way to find out was to read
 * the source or to press them. This is the line that says it, plus the one thing on the right that
 * a player cannot work out by looking: how much there is on the open tab, or, while a multiplayer
 * session runs, that one does and which end of it this machine is.
 *
 * Text only. The drawing is overlay_frame.c's, so the words and the measuring can be checked
 * without a window, and the one rule that could go wrong quietly, what is dropped when the room
 * runs out, is a function with an answer rather than a sequence of ifs in a painter.
 */
#ifndef DEV_OVERLAY_OVERLAY_LEGEND_H
#define DEV_OVERLAY_OVERLAY_LEGEND_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define OVERLAY_LEGEND_KEYS 4u

/* One key cap and what it does: the word inside the box, and the words in grey after it. */
typedef struct overlay_legend_key {
    const char *cap;
    const char *what;
} overlay_legend_key_t;

/* The caps in the order they are drawn, or NULL past the end. */
const overlay_legend_key_t *overlay_legend_key(uint32_t index);

/* What the footer says in place of the caps while a key row waits for its key, or NULL while none
 * waits and the caps stand. `capturing` is the model's own answer to whether a key row waits.
 *
 * While one waits the next key of any kind is that row's binding, Escape included, so every cap
 * would name a key that does not do what the cap says. They stand down for as long as the wait
 * lasts, and this one sentence stands in their place. */
const char *overlay_legend_prompt(bool capturing);

/* The right hand end of the footer: `session - host` or `session - client` while one runs, and
 * the open tab's tally otherwise. Never empty.
 *
 * The session is asked of session_lock.c, the one place in the panel that reads the multiplayer's
 * note, and the tally is handed in by the caller out of the model, which is the one place that
 * knows what it built. Neither is counted here.
 *
 * It says no number of players, because there is none to say: the note carries whether a session
 * runs and who hosts it, and nothing else. */
void overlay_legend_right(char *out, size_t size, uint32_t headings, uint32_t rows);

/* How much of the footer fits in the room the rows already decided the panel would be.
 *
 * The footer may not make the panel wider. The panel is as wide as its longest name plus its
 * widest chip and is clamped against nothing else, so a legend that asked for more would either
 * push the rows' own text out or run off a narrow display. It gives up its parts in a fixed
 * order instead: first the grey words after the caps, then the tally on the right, and the caps
 * themselves last, because a player who can see only one thing should see which keys work. */
typedef enum overlay_legend_fit {
    OVERLAY_LEGEND_FULL = 0,   /* the caps, the words beside them, and the right hand text */
    OVERLAY_LEGEND_CAPS,       /* the caps and the right hand text                         */
    OVERLAY_LEGEND_BARE        /* the caps alone                                           */
} overlay_legend_fit_t;

/* `room` and the three widths are the caller's own measurements, in the caller's own unit; only
 * their order matters here. `words` is what the grey text after the caps costs on top of the caps
 * themselves. */
overlay_legend_fit_t overlay_legend_fit(float room, float caps, float words, float right);

/* How many of the key caps fit in `room`, at most OVERLAY_LEGEND_KEYS.
 *
 * overlay_legend_fit() says what the band gives up as the room runs out, and its last answer
 * is "the caps alone". It never asked whether the caps ALONE fit, and at the panel's narrowest
 * they need not: the four of them want about 26 average character widths plus four boxes of
 * padding, against a room of 22.5 text heights, so past an average advance of about seven
 * tenths of a capital's height the last cap does not fit. The engine has no clipping, so it
 * was drawn past the panel's right edge and over whatever the game had there. Nothing in the
 * build or in a log would have said so.
 *
 * `cap_widths` is OVERLAY_LEGEND_KEYS box widths in the caller's own unit and `gap` is what
 * the painter leaves between two boxes. The gap after the last cap drawn is not charged,
 * because nothing is drawn in it. */
uint32_t overlay_legend_caps_that_fit(float room, float gap, const float *cap_widths);

#endif /* DEV_OVERLAY_OVERLAY_LEGEND_H */
