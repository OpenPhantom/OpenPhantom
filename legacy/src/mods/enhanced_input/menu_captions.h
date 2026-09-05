#ifndef MENU_CAPTIONS_H
#define MENU_CAPTIONS_H

/* What this DLL's three added widgets say, and which of the five languages this machine gets.
 *
 * Split out of input_menu.c along the seam that file's own size note had named. The wording is not
 * the widget group: nothing here knows a rectangle, a bitmap index, a widget id or a patch context,
 * and nothing in the group has to be re-read when a caption changes. The three answers are handed
 * out as plain strings rather than copied into a caller's buffer, so that the one place that owns
 * a caption buffer goes on owning the copy into it.
 *
 * Each call resolves the language again. It is asked once per session, at install, and the answer
 * comes from the host rather than from anything this DLL keeps.
 */

/* The two check-box labels, in the language this machine is running in. */
const char *menu_captions_strafe(void);
const char *menu_captions_free_look(void);

/* The mouse sensitivity caption, as a format string with exactly one `%d`. */
const char *menu_captions_mouse_speed_format(void);

#endif /* MENU_CAPTIONS_H */
