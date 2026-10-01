/* overlay_look.h: the panel's colours, and the one measure more than one file draws against.
 *
 * They were in overlay_draw.c until the title band, the tabs and the search field moved out of it
 * into overlay_frame.c, which is the seam that file's own note had named for a while. Those three
 * blocks draw with the same palette as the rows below them, so the palette is a header now and
 * still exactly one copy of each number. Nothing here decides anything and nothing here draws.
 *
 * ==============================================================================================
 * The colours, and the one rule that makes them work
 *
 * Exactly one rectangle is translucent: the body. Everything else is opaque.
 *
 * That is arithmetic, not taste. A translucent rectangle drawn on top of another translucent
 * rectangle has a contrast that depends on the scene behind both. The panel used to draw its tabs
 * that way, and over a dark interior the active and the idle tab were fifteen steps apart while
 * over a sunlit scene they were one and a half: the tabs converged to the same colour as the game
 * got brighter. The rule under the title had it worse and inverted outright, ending up darker than
 * the surface it was supposed to separate.
 *
 * With one translucent surface, every contrast inside the panel is fixed and the alpha has exactly
 * one job. What that job buys: with ten percent transmission the surface behind the text sits at
 * about 13 over a dark interior and 37 over a white sky, which holds the text above a ten to one
 * contrast ratio in the worst case. A quarter transmission would look better through and drops the
 * bright case to under six to one, which is where a bitmap font over a MOVING picture shimmers.
 * ============================================================================================ */
#ifndef DEV_OVERLAY_OVERLAY_LOOK_H
#define DEV_OVERLAY_OVERLAY_LOOK_H

/* ==============================================================================================
 * Four roles, and nothing else carries meaning
 *
 * A colour that means two things means neither. The panel used to spend blue, two greens, red and
 * four greys on meaning, and the same blue said "this is where you are" on a selection frame and
 * "press me" on a RUN chip. So there are four roles now and every value below belongs to exactly
 * one of them:
 *
 *   amber   where you are, and what is active: the selection frame, the heading's bar, the fold
 *           mark, the filled part and the grip of a track, the chosen segment, a choice's mark.
 *   green   switched on, and only that: the ON chip and a heading's `N on`.
 *   warn    refused, and only that: the refusal band, a row's own refusal, a note that warns.
 *   grey    everything else. RUN and Set are grey chips with bright text: they say nothing about
 *           a state, so they get no colour of their own and stop competing with the selection.
 *
 * Every state stays readable with the colour taken away. ON and OFF are words in a chip and n/a
 * is a word with no chip at all; a chosen segment is a filled box among outlines; a chosen entry
 * is a filled mark among empty ones; the selected row is a frame. The 16 bit buffer bands soft
 * gradients and a player may not separate red from green, so no state is told by hue alone.
 *
 * ==============================================================================================
 * The colours, and the one rule that makes them work
 *
 * Exactly one rectangle is translucent: the body. Everything else is opaque.
 *
 * That is arithmetic, not taste. A translucent rectangle drawn on top of another translucent
 * rectangle has a contrast that depends on the scene behind both. The panel used to draw its tabs
 * that way, and over a dark interior the active and the idle tab were fifteen steps apart while
 * over a sunlit scene they were one and a half: the tabs converged to the same colour as the game
 * got brighter. The rule under the title had it worse and inverted outright, ending up darker than
 * the surface it was supposed to separate.
 *
 * With one translucent surface, every contrast inside the panel is fixed and the alpha has exactly
 * one job. What that job buys: with ten percent transmission the surface behind the text sits at
 * about 13 over a dark interior and 37 over a white sky, which holds the text above a ten to one
 * contrast ratio in the worst case. A quarter transmission would look better through and drops the
 * bright case to under six to one, which is where a bitmap font over a MOVING picture shimmers.
 *
 * The measured pairs, worst case first (white sky behind the body, so the surface is 37/39/45):
 *
 *   text on the body                12.1:1     amber on the body                 9.4:1
 *   dark text on amber              11.7:1     dark text on green                5.6:1
 *   bright text on a grey chip      13.6:1     dimmed text on a grey chip        6.4:1
 *
 * Amber lands at 9.4 rather than above 10, and that is the one number here that does not clear
 * the bar in the note above. It is an accent and never a run of body text: it is a frame, a bar,
 * a four pixel mark and a track. Body text is the 12.1 line and still clears it.
 * ============================================================================================ */

/* --- role: where you are, and what is active ------------------------------------------------ */
#define C_ACCENT        0xFFF2C76Bu   /* amber, and the ONLY thing that says "here"             */
#define C_ACCENT_DIM    0xFF8A6A24u   /* the same accent with the attention taken out of it     */
#define C_ACCENT_TEXT   0xFF0F1318u   /* what is written ON amber, 11.7:1                       */

/* --- role: switched on ---------------------------------------------------------------------- */
/* ON and OFF get a chip; n/a does not. That is structural rather than decorative: n/a is not a
 * state the cheat is in, it is the absence of the control, so nothing control shaped goes behind
 * it. Telling it apart by colour alone was the failure, and telling it apart by whether a
 * rectangle is there at all cannot fail. */
#define C_CHIP_ON       0xFF3E9E5Cu
#define C_CHIP_ON_TEXT  0xFF0F1318u   /* near black on green: inverted reads as "this is on" */

/* --- role: refused --------------------------------------------------------------------------- */
/* The one warning colour, and it is the value it has always been. It is used for the refusal band
 * above the footer and for the one row that carries a refusal of its own, and for nothing else; a
 * second warning colour would be two answers to one question.
 *
 * It is now warm alongside a warm accent, which it was not before: against the amber above it
 * measures 1.4:1, so the two are told apart by where they stand and not by hue. Nothing depends
 * on separating them at a glance today, because a refusal is a full width band above the footer
 * or a note on the name column and neither is ever the shape an accent takes. If that stops
 * being true, this is the value to move, not the accent. */
#define C_WARN          0xFFE2A445u

/* --- role: everything else, in four steps of grey -------------------------------------------- */
/* Text, dimmed, pale, taken. A row's name is text; a note and an unavailable name are dimmed; the
 * search placeholder and the word n/a are pale, because both are meant to be read past. */
#define C_TEXT          0xFFE9E7E2u
#define C_TEXT_DIM      0xFF99A1ABu
#define C_TEXT_PALE     0xFF6B737Du
#define C_TEXT_TAKEN    0xFF586069u

/* Four surfaces, from the body up: sunk, raised, line, strong line. Nothing else fills. */
#define C_SURF_SUNK     0xFF0F1318u
#define C_SURF_RAISED   0xFF191E25u
#define C_LINE          0xFF262D35u
#define C_LINE_STRONG   0xFF333C46u

#define C_PANEL_BODY    0xE60D0F16u   /* the only translucent one, and this is its whole budget */
#define C_BORDER        C_TEXT_TAKEN  /* mid grey: the only value visible against BOTH sides    */
#define C_BAND_TITLE    C_SURF_RAISED
#define C_RULE          C_LINE_STRONG

#define C_TAB_ON_FILL   C_SURF_RAISED
#define C_TAB_ON_TEXT   C_TEXT
#define C_TAB_OFF_TEXT  C_TEXT_DIM

#define C_FIELD_FILL    C_SURF_SUNK
#define C_FIELD_BORDER  C_LINE_STRONG
#define C_PLACEHOLDER   C_TEXT_PALE
#define C_TYPED         C_TEXT

#define C_GROUP_BAND    C_SURF_RAISED
#define C_GROUP_TEXT    C_TEXT
#define C_GROUP_HOT     C_LINE
#define C_ROW_TEXT      C_TEXT
#define C_ROW_TEXT_DIM  C_TEXT_DIM
#define C_ROW_HOT       C_SURF_RAISED   /* the same as the active tab: "current" is one colour */
#define C_LEADER        C_LINE
#define C_LEADER_HOT    C_LINE_STRONG

#define C_CHIP_OFF      C_SURF_RAISED
#define C_CHIP_OFF_TEXT C_TEXT_DIM
#define C_STATE_NA      C_TEXT_PALE

/* An action is not a state, on or off, so it gets neither chip colour, and it no longer gets a
 * colour of its own either. RUN and Set were accent blue, which is the panel's word for "this is
 * where you are", and a fixed word on eight rows at once outshouted the one row the player had
 * actually selected. A grey chip with bright text still reads as a button, because the chip is
 * the shape that says button and the text is the brightest in the panel. */
#define C_CHIP_ACTION      C_SURF_RAISED
#define C_CHIP_ACTION_TEXT C_TEXT

#define C_POINTER       0xFFFFFFFFu

/* The margin inside the left and right borders, in text heights. Every band of the panel starts
 * and ends on it, the drawing and the hit test both. It stood in three files, and the last of
 * the three was overlay_layout.c, where the search field was hit against a second copy of the
 * same number the field is drawn with. */
#define OVERLAY_EDGE_PAD 0.75f

#endif /* DEV_OVERLAY_OVERLAY_LOOK_H */
