/* scorch_reach.h: a burn reaches every polygon its mark touches.
 *
 * A scorch mark is stamped on every polygon its sphere touches, one decal per polygon, and two
 * things in fx_scorch stop it reaching a polygon it should (issue 28: a blast mark cut along a
 * straight line, one half on the sand and the other missing).
 *
 * The polygons come from a query of the cells around the impact. The query box is size times two,
 * capped at 1.9 units; the sphere test that follows is the full size. The ground scorch is placed
 * at size 1.25, already past the cap, so a polygon whose cell lies more than 0.95 units from the
 * impact is never asked. The cap goes, so the box is always what the sphere is.
 *
 * Each gathered quad is tested against the sphere as two triangles, vertices 0-1-2 and then
 * 1-2-3, and those two do not tile a quad: the wedge along the edge from vertex 3 back to vertex
 * 0 is never tested. A mark centred on the polygon next door, just across that edge, overlaps
 * the quad only in that wedge, so the quad is skipped and the mark ends in a straight line along
 * the edge. One edge in four of every floor quad does it. The second triangle becomes 0-2-3, the
 * pair that tiles the quad and the pair the renderer draws it as, at that one call site only.
 *
 * And the projector refuses any polygon whose normal faces along the shot, the right test for
 * the polygon the shot hits and the wrong one for its neighbours in the splash: ground falling
 * away from a low shot by a few degrees more than the shot's own angle reads as facing away,
 * and the mark stops dead at every fold in the sand. The threshold moves from zero to about 45
 * degrees, so a neighbour that has merely turned a little is stamped and one on the far side of
 * a ridge is still refused.
 */
#ifndef SCORCH_REACH_H
#define SCORCH_REACH_H

#include <stdbool.h>

/* Returns true when all three repairs are in place. */
bool scorch_reach_install(void);

#endif /* SCORCH_REACH_H */
