/* fog_band.h: the numbers the fog band arithmetic is defined by.
 *
 * Split out of fog_regime.c with the arithmetic itself. They are here rather than in fog_band.c
 * because two of them, the reference field of view and the settled cut, are also read by the tick
 * that drives the arithmetic, and one copy of a measured constant is the whole point of writing
 * down where it was measured.
 *
 * Every one of these came out of the retail image or out of a measurement against it, and the
 * comment beside each says which. None of them is a preference.
 */
#ifndef VIEW_DISTANCE_FIX_FOG_BAND_H
#define VIEW_DISTANCE_FIX_FOG_BAND_H

/* The field of view the levels were authored at: rdCamera_new's own first argument, `push
 * 0x42700000` at 0x00417F79, and that call site (bapview_newView) is the only one in the image. */
#define FOG_REFERENCE_FOV_DEGREES 60.0f

/* Half the diagonal of the one-unit cell the circle test measures to. A cell whose CENTRE is just
 * outside the circle is dropped, and geometry inside it can sit up to this much nearer than the
 * centre, so the honest cut edge is the circle minus this. */
#define FOG_CELL_MARGIN_UNITS 0.70710678f

#define FOG_DEGREES_TO_RADIANS 0.017453292f
#define FOG_MAX_HALF_ANGLE      89.0f
#define FOG_MIN_END              2.0f   /* the engine's own floor on the draw distance */
#define FOG_FOLLOW_FLOOR         0.25f  /* a pathological cut edge must not put fog on the lens */
#define FOG_FOV_DEAD_BAND_DEGREES 1.0f  /* see fog_regime_follow_factor */

/* The remaining fraction of any gap after `settle_seconds`, and the gap below which the target is
 * simply taken. One hundredth of a world unit is far under the ramp's own quantisation: the ramp
 * turns the band into 256 alpha steps (0x004020DF multiplies by the 255.0 at [0x4A8020]), so on a
 * 14-unit band one step is 0.055 units. */
#define FOG_DAMP_REMAINDER       0.1f
#define FOG_DEAD_BAND_UNITS      0.01f
#define FOG_MAX_TRUSTED_SECONDS  0.125f

/* THE BAND FOLLOWS A SETTLED DRAW DISTANCE, NOT THE INSTANTANEOUS ONE.
 *
 * The cut edge is not a steady number. The frame governor moves the view scale whenever a scene
 * costs too much, in steps of its own every half second, and every step changes the distance the
 * band is computed from. Measured during the opening cutscene of RACE, where the governor works
 * hardest because the frame rate is worst:
 *
 *   fov 120.0  cut ref 22  live 39  -> band 6.0..19.2   (band was at 3.3..10.7)
 *   fov 120.0  cut ref 22  live 34  -> band 5.2..16.7   (band was at 3.7..11.9)
 *   fov 120.0  cut ref 22  live 33  -> band 5.0..16.2   (band was at 4.6..14.6)
 *   fov 120.0  cut ref 22  live 32  -> band 4.9..15.7   (band was at 5.0..16.2)
 *
 * The field of view never moved and neither did the reference. The live cut fell 39, 34, 33, 32,
 * and the band spent the whole cutscene chasing a target that had already moved again. That is the
 * fog visibly swinging at level entry, which is what issue #30 reports.
 *
 * Easing the CUT rather than only the band is what fixes it. A step becomes a slope, several steps
 * inside one settle become one slope, and the fog ends up where the draw distance ended up without
 * having visited every value on the way. Slower than the band's own settle, deliberately: this is
 * the input, and smoothing an input faster than its consumer only moves the problem. */
#define FOG_CUT_SETTLE_SECONDS   4.0f

#endif /* VIEW_DISTANCE_FIX_FOG_BAND_H */
