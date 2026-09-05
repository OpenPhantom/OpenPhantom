/* translucent_fog.h: an alpha-blended face is drawn with the fog switched off, and that may be the
 * flicker.
 *
 * std3D_deferFace clears D3DRENDERSTATE_FOGENABLE for any face carrying either alpha-blend bit,
 * unless it carries 0x200000. That is deliberate and old: table fog and alpha blending together
 * were unreliable on the hardware this was written for.
 *
 * The consequence is not old. The level-of-detail cross-fade makes geometry translucent WHILE IT
 * CROSSES, so a surface loses its fog for the length of the transition and comes back with it.
 * Where the band saturates inside the draw cut the background behind that surface is already fog
 * colour, so an unfogged face reads at full brightness against it. That is a band at a fixed
 * distance from the camera, which moves with the camera, is invisible when the band saturates
 * beyond the cut because there is then no fog-coloured background to stand out against, and is
 * perfectly steady when the camera is still because nothing is crossing the transition.
 *
 * Every one of those matches what was measured: a band held constant at both our record and the
 * device globals, a gathered cell count with a median change of zero, flat frame times, and no
 * draw-path counter above a third of its ceiling.
 *
 * This is a question, not an answer. Keeping fog on translucent faces is not obviously correct: it
 * is the behaviour the engine deliberately avoided, and it may look wrong in its own way.
 */
#ifndef VIEW_DISTANCE_FIX_TRANSLUCENT_FOG_H
#define VIEW_DISTANCE_FIX_TRANSLUCENT_FOG_H

#include <stdbool.h>

/* False leaves the engine alone, which is the shipped answer. True turns the fog-enable clear into
 * a no-op so an alpha-blended face is fogged like any other. Logs whichever it did, and refuses
 * rather than guessing if the site or the byte does not check out. */
void translucent_fog_install(bool keep_fog_on_translucent);

#endif /* VIEW_DISTANCE_FIX_TRANSLUCENT_FOG_H */
