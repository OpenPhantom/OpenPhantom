/* overlay_notice.h: what the panel turned down, said once, where the player is looking.
 *
 * A refusal in this panel used to be silence. A number outside its ends was dropped and the row
 * went on showing the value it already had; a key the panel needs for itself was not bound and the
 * row went on showing the key it already had; a settings file that could not be written lost the
 * setting and nothing said so. In every one of those the panel does exactly what it does when the
 * change WORKED and the value happened to be the same, so there is no way to tell a refusal from a
 * no-op by looking.
 *
 * So there is one cell here for the last refusal, and one band directly above the footer that
 * draws it. One cell and not a list: two sentences would need a height that grows, and the height
 * of this band is taken off the rows.
 *
 * ==============================================================================================
 * It never goes away on a clock
 *
 * This is the whole reason the module exists rather than a timer beside the drawing. The band
 * costs height while it stands: it takes the last row that fits, or makes the panel taller by its
 * own height (overlay_layout.c). Coming and going there moves no row above it, but if it expired
 * by itself, the place it held would turn back into a row, or into the footer, while the pointer
 * stood still, and a click aimed there would land on whatever took its place.
 *
 * It goes when the next action comes, which is the moment the player is already doing something:
 * a click, a key, a pad press. overlay_notice_act() is called at each of those doors BEFORE the
 * action runs, so an action that is itself refused puts its own sentence up afterwards and the
 * sentence stands.
 *
 * Nothing here logs. A refusal on screen is not a log entry; the modules that write settings log
 * their own failures where they happen.
 *
 * One sentence that stands here is not a refusal. The chat key row says, after a binding, that a
 * running session takes the key within a second: another DLL reads it from the file, and a player
 * who pressed the new key at once and saw nothing would otherwise take the binding for lost. It is
 * put up as a confirmation, and the cell remembers which kind it holds, so the band draws it in the
 * ordinary text colour: in the warning colour a sentence that says it worked reads as one that
 * says it did not.
 */
#ifndef DEV_OVERLAY_OVERLAY_NOTICE_H
#define DEV_OVERLAY_OVERLAY_NOTICE_H

#include <stdbool.h>

/* What the band can hold, in characters, and it is a budget and not a buffer size.
 *
 * The band runs the panel's whole width: WIDTH_MAX less the edge padding at each side is 44.5
 * text heights (overlay_layout.c, overlay_look.h), and this font's widest glyph is 40 heights to
 * 48 characters. 44.5 times 48 over 40 is 53. Converted here the same way unittests/overlay_model.c
 * converts the row budget, and for the same reason: nothing measures a font where a sentence is
 * written, so the cut has to be in characters.
 *
 * A sentence longer than this is CUT here, with the two dots the drawing puts on a label it has to
 * shorten, so nothing longer than the band can hold ever reaches the drawing. The drawing fits it
 * against the real font as well, which is what catches a narrow panel; this is what catches a
 * sentence somebody wrote too long. */
#define OVERLAY_NOTICE_CHARS 53u
#define OVERLAY_NOTICE_MAX   (OVERLAY_NOTICE_CHARS + 1u)

/* Puts `text` up as the standing refusal, replacing whatever stood. The last one said is the one
 * shown: two refusals inside one action are one action's answer, and the second is the later
 * word about it. NULL and an empty string say nothing and leave what stands alone, so a caller
 * that has no sentence cannot silently clear one. */
void overlay_notice_say(const char *text);

/* The same, for a sentence that says something WORKED, and the one kind of sentence here that is
 * not a refusal. It takes the band on the same terms: it replaces what stood, goes on the next
 * action and never on a clock, and NULL or an empty string leave what stands alone. */
void overlay_notice_confirm(const char *text);

/* Whether the standing sentence was put up as a confirmation. False when a refusal stands and
 * when nothing does; the painter colours the band by this and not by reading the words. */
bool overlay_notice_confirms(void);

/* The standing sentence, or NULL when nothing stands. The layout asks it what the band costs and
 * the drawing asks it what to write, so both read one answer. */
const char *overlay_notice_text(void);

/* A player has acted. Whatever stood goes; whatever this same action says afterwards stands.
 * Called at the panel's doors, not on a clock, and never from the drawing. */
void overlay_notice_act(void);

/* Forgets everything, for the panel closing. A sentence about a keystroke from before the panel
 * was shut describes something the player has long since moved on from. */
void overlay_notice_forget(void);

#endif /* DEV_OVERLAY_OVERLAY_NOTICE_H */
