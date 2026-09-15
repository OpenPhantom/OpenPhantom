/* diag_x87.h: where in the object draw the x87 stack pointer moves.
 *
 * framerate_fix's rider trace found the x87 stack pointer one higher after the player's draw
 * than before it, every frame, with every register tagged empty: a stack underflow, one pop
 * more than push somewhere in that draw, whose store writes the indefinite NaN into whatever
 * float the engine was storing, the blade drawn out of a hand. Further samples put it
 * outside rdThing_Draw and outside the decal submit, and a run with nothing loaded but a
 * frame-end sample showed it belongs to the engine or the Direct3D path under it, not to any
 * DLL of this project.
 *
 * This is the next cut. Seven calls of the object draw are observed, the status word sampled
 * on the way in and the way out of each, and every call across which the pointer moved is
 * counted per function and the first few written out with both words: the model draw
 * (bapthing_dispatch), the puppet track advance, the clip events it raises, the halo and
 * shield pass (fx_thingDraw) and the halos alone, the shadow projector, and the queue flush
 * between asset groups, which ends in Direct3D. Whichever moves the pointer holds the fault,
 * and the flush moving it would put it under dxwrapper and not in the engine. Every 300
 * frames a summary line gives the counts, so a run of a few seconds is enough.
 */
#ifndef DIAG_X87_H
#define DIAG_X87_H

/* X87=1 in [diagnostics]. Returns the number of observers placed. */
int diag_x87_install(int level);

#endif /* DIAG_X87_H */
