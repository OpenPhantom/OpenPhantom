/* mp_chat_draw.h: the chat box in the picture.
 *
 * Layer 3. Drawn with the engine's own renderer, through the surface the scoreboard resolved, on
 * the first module message of the engine's frame end, which the multiplayer's head node hears after
 * the heads up display, the subtitles and the fade. No patch and no detour: a rectangle and some
 * strings on a frame the engine is already drawing, once a frame.
 *
 * The box stands left, its bottom edge at 72 percent of the screen's height, above the health and
 * force bars and clear of the subtitles at every size from 720 lines up. It is sixteen pixels in,
 * forty percent of the screen wide and never wider than 520 pixels. While nobody types there is no
 * box, only the rows of the newest lines, names in amber and text in white, each fading after its
 * twelve seconds; while the player types a translucent box shows six rows of lines in full and the
 * input row under them.
 *
 * It is drawn only where the chat can be typed: a transport stands, a level runs, and no menu,
 * movie or load covers it.
 */
#ifndef MULTIPLAYER_MP_CHAT_DRAW_H
#define MULTIPLAYER_MP_CHAT_DRAW_H

/* The fan out of the module messages, every one of them; the box acts on the frame end's first. */
void mp_chat_draw_note_module_message(int message);

/* Once a frame from the frame pump, after the frame the box was drawn into: the boundary the box's
 * once a frame guard counts against. */
void mp_chat_draw_frame(void);

/* The report's line. Counts only. */
void mp_chat_draw_report(void);

#endif /* MULTIPLAYER_MP_CHAT_DRAW_H */
