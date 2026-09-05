/* two_sided_faces.h: draw a dismembered body with its backfaces, and nothing else.
 *
 * The engine culls backfaces in SOFTWARE, per face, through one byte it reads at 0x0040F3F7; the
 * device is set to D3DCULL_NONE and never culls at all. Clearing that byte around one thing draws
 * it two-sided, which is what softens the see-through hole a severed limb leaves.
 *
 * THE SEAM. It shares nothing with the draw distance but the DLL it ships in: a different engine
 * function, a different byte, a per frame budget of its own, and a switch of its own that ships
 * off. The two site patterns stay with the rest of the site table, because resolving sites is the
 * install sequence's job; what comes across is the prologue this feature's own detour overwrites.
 */
#ifndef TWO_SIDED_FACES_H
#define TWO_SIDED_FACES_H

#include <stdbool.h>
#include <stdint.h>

/* rdThing_Draw's prologue, `83 EC 48 / B9 0C000000`, eight bytes on a clean boundary. Declared
 * here rather than beside the pattern because the detour that overwrites those bytes is this
 * feature's, and the site table needs the same number to search in the detour form. */
#define THING_DRAW_PROLOGUE_SIZE 8u

/* `cull_site` is the resolved mesh cull anchor and `draw_site` the resolved rdThing_Draw. Either
 * may be 0, which declines the whole feature rather than half installing it: a cull word with no
 * writer would be worse than none. */
void two_sided_faces_install(uintptr_t cull_site, uintptr_t draw_site, bool enabled,
                             int max_per_frame);

/* Clears the per frame budget. Called once per rendered frame, from the DLL's own tick. */
void two_sided_faces_begin_frame(void);

#endif /* TWO_SIDED_FACES_H */
