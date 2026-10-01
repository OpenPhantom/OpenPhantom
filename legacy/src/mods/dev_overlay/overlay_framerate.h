/* overlay_framerate.h: the panel's fourth OpenPhantom group, the frame rate.
 *
 * A group of its own for the same reason Window mode became one: these rows answer a single
 * question somebody arrives with, and it is a question that has cost real time. The frame limit
 * decides how fast frames are PRODUCED, and nothing in the shipped stack ties that to how fast
 * they are SHOWN, so a limit that does not match the screen's refresh rate leaves the display
 * repeating frames on an irregular pattern. Everything in motion judders slightly while the
 * frame counter reads perfectly steady, and it looks like a fault in the interpolation.
 *
 * Measured: a limit of 100 on a 144 Hz screen leaves 44 refreshes a second repeating a frame, and
 * a limit of 60 on a 90 Hz Steam Deck OLED leaves 30. Both were mistaken for a fault in the patch
 * and cost an evening of looking in the wrong place, which is the argument for putting this in
 * front of somebody rather than in a settings file.
 *
 * The fraction row is the answer to the screen the machine cannot keep up with. A 240 Hz screen on
 * a modest machine, or a wide cutscene shot on any machine, cannot hold the refresh, and the only
 * other rates that are even on a screen are its integer fractions: 144, 72, 48, 36. Auto lets
 * framerate_fix step between them on a second's evidence; a digit pins one for the player who
 * knows the machine and wants it to stop thinking.
 *
 * Like every other row that reaches out of this DLL, these write the settings file. The keys are
 * MatchDisplayRefresh, RefreshDivisor and TargetFps in the [framerate_fix] section. That DLL owns
 * the cap and re-reads all three about once a second, so a change here takes effect within that
 * second, live, without restarting. Nothing here calls into it, because feature DLLs in this tree
 * do not depend on each other.
 */
#ifndef DEV_OVERLAY_OVERLAY_FRAMERATE_H
#define DEV_OVERLAY_OVERLAY_FRAMERATE_H

#include "overlay_model.h"

#include <stdbool.h>
#include <stdint.h>

/* The match toggle, the fraction of the screen's rate, the typed limit, and the three lines of the
 * note under them. */
#define OVERLAY_FRAMERATE_ROW_COUNT 6u

/* Fills one row. `editing_text` is the live text while the limit is being typed into, or NULL. */
void overlay_framerate_row(uint32_t slot, const char *editing_text, overlay_row_t *out);

/* The match toggle, and the fraction row, which steps through auto, 1, 2, 3, 4 and back to auto on
 * each press. Answers false when the settings file could not be written; the panel shows that as
 * the row failing rather than silently doing nothing. */
bool overlay_framerate_toggle(uint32_t slot);

/* Accepts a typed frame limit. Anything outside 0 to 1000 is refused and the row keeps its old
 * value; 0 means no limit at all. */
bool overlay_framerate_accept_value(uint32_t slot, const char *text);

#endif /* DEV_OVERLAY_OVERLAY_FRAMERATE_H */
