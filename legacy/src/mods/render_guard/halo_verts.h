/* halo_verts.h: a halo drawn from twelve floats nothing wrote.
 *
 * halo_draw (0x00439AB6) keeps a 12 float buffer on its stack for the four vertices of the node
 * the halo hangs on, and fills it with one call of bapobj_getNodeMeshVerts (0x0041378A). That
 * routine writes nothing when the node index is past the model's count or the node carries no
 * mesh; it returns, and halo_draw reads the two vertices it wants out of whatever the stack held
 * before the call. The halo is then a screen quad between two points that came from a stale
 * frame: usually small and culled by the routine's own "shorter than two pixels" test, sometimes a
 * pale bar out of a Jedi's hand, and with the x87 stack one short (node_verts.h) a NaN, which
 * that test cannot cull at all. Which of the three depends on what the calls before it left on
 * the stack, so the bar came and went with every change to the code around it, and a detour
 * placed in front of the draw to observe it made it go.
 *
 * Measured 2026-09-15 in Mos Espa with the guard's own log line: "halo on obinpc.3do node 12:
 * the node carries no mesh", once, and the bar out of Obi-Wan's hand gone. Node 12 is the last
 * of the four blade halos every Jedi model is given, and on the NPC Obi-Wan model that node is
 * present, switched on, and empty. Under Wine on NVIDIA and on Intel and on the Steam Deck,
 * across several levels, the same one line and no other model named.
 *
 * The repair diverts halo_draw's call alone, the other two callers keep the routine: the
 * replacement makes the same two tests, copies at most the four vertices the buffer holds when
 * they pass, and zeroes all twelve floats when they do not. Two zero vertices project to one
 * point, the quad is shorter than two pixels, and the routine's own cull leaves nothing drawn.
 * The first failure on each model and node is logged with the reason, so the log names the model
 * and the node.
 */
#ifndef HALO_VERTS_H
#define HALO_VERTS_H

/* Reads [render_guard] GuardHaloVerts, on by default. Runs after node_verts_install(): whether
 * the caller still pops a float after the call is read from the bytes and the replacement
 * returns one, or nothing, to match. */
void halo_verts_install(void);

#endif /* HALO_VERTS_H */
