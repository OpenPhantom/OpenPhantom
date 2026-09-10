/* object_interpolation.h: draw every object from a previous position the carry cannot flatten.
 *
 * bapobj_drawAll already blends each object between its previous and current position on the
 * substep alpha, and has since 1999. The pair it reads is the problem, not the blend: a character
 * riding a platform has its previous position overwritten with its current one inside the same
 * simulation step, so the blend has nothing to work with and the rider steps at 32 Hz while the
 * platform it stands on is drawn every frame. object_track.h carries the measurement and the two
 * writers.
 *
 * This replaces the region of bapobj_drawAll that computes the blend, with a call that fills the
 * same six stack locals from a previous position remembered in object_track.c instead. The
 * engine's own pair is still the fallback, used unchanged for any object the tracker cannot answer
 * for, so the worst case is the behaviour that shipped.
 *
 * No engine record is written. obj->pos and obj->prevPos are read and never stored to, and the
 * only writes are to the caller's own frame, which is the same convention draw_interpolation.c
 * already follows for the drawn pitch and roll.
 */
#ifndef OBJECT_INTERPOLATION_H
#define OBJECT_INTERPOLATION_H

#include <stdbool.h>

/* `travel_limit` is the furthest an object may travel in one simulation step before the blend
 * refuses it and the current position is drawn unblended. Zero disables that test. */
/* `mode` is 0 off, 1 the remembered previous position, 2 a deliberate PASSTHROUGH, and 3 the
 * passthrough again with a report of where the remembered position would have differed.
 *
 * On the passthrough: the
 * patch is placed and the call is made, but the arithmetic is the engine's own, reading the
 * object's previous position exactly as the replaced instructions did.
 *
 * Mode 2 exists because mode 1 was played once and refused, with characters flying around the
 * level, and two explanations were left standing: the placement of the patch, or the position
 * it feeds the blend. Mode 2 separates them in one run. If the game is normal under it, the
 * region, the calling convention, the six locals and the alpha are all right and the fault is
 * in the remembered position alone. If it still flies, none of that is right and the tracker
 * was never the problem. */
void object_interpolation_install(int mode, float travel_limit);

/* Once per rendered frame, from the frame callback. The tracker ages its entries in frames, and
 * this is the only thing that tells it a frame has passed. */
void object_interpolation_frame(void);

#endif /* OBJECT_INTERPOLATION_H */
