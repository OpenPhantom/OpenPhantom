#ifndef DIAG_FRAME_H
#define DIAG_FRAME_H

/* diag_frame.h: why the picture hitches, measured rather than argued about.
 *
 * Everything else in this DLL watches the game. This one watches the machine underneath it, because
 * a hitch that a player sees has at least five possible authors and they are indistinguishable from
 * inside the game's own logic:
 *
 *   * a long frame, where the work of one frame genuinely took several frames' time;
 *   * a stalled frame, where the work was short and something outside the process took the CPU;
 *   * a page fault storm, where the memory the frame touched was not resident;
 *   * a graphics stall, where the CPU waited on the card or on the presentation queue;
 *   * and nothing at all in the engine, where every frame was produced on time and the picture
 *     still moved unevenly, which points at everything downstream of this process.
 *
 * The last one is the reason this exists in the shape it does. Ruling a cause out is worth as much
 * as finding one, and the only way to rule the engine out is to show that its frames really were
 * even at the instant the player saw a hitch. An average over sixty frames cannot do that: one
 * frame in three hundred is exactly what is being complained about and exactly what an average
 * hides. So the instrument keeps a ring of the most recent frames and prints the neighbourhood of
 * each hitch, which is a picture of the event rather than a statistic about the session.
 *
 * ==============================================================================================
 * What it costs, because a diagnostic that perturbs what it measures is worthless here
 *
 * The per-frame half is two counter reads, a subtraction and a store into a ring. Nothing is
 * formatted, nothing is logged and nothing is allocated on the frame path unless a hitch has
 * already happened. Everything expensive, the process and system CPU times, the fault counts and
 * the graphics counter, is sampled once a second on a thread of its own at below normal priority,
 * and the frame path only ever reads the values that thread last published. A measurement taken on
 * the render thread would be a measurement of itself.
 *
 * The same trap reappears in the threshold, in a form that is much easier to miss, and it did catch
 * us: a purely relative trigger with no absolute floor starts calling ordinary scheduler noise a
 * hitch once frames are short, and every dump it then writes goes out synchronously from inside the
 * frame callback and stretches the frame it is describing. The floor and the measurement behind it
 * are in diag_frame.c beside the constant.
 *
 * ==============================================================================================
 * And it installs no hook of its own
 *
 * It uses the frame callback the shared layer already provides, and nothing else. Detouring the
 * frame limiter would give the one number this cannot produce, the split between working and
 * waiting, and it must not: the limiter's prologue is what framerate_fix searches for by pattern,
 * this DLL loads first, and a detour there would leave that feature unable to find its own site and
 * the frame cap silently off. A diagnostic that breaks a feature in order to measure it is not a
 * diagnostic.
 */

/* Levels: 0 off, 1 one summary line a second, 2 also a dump of the frames around every hitch.
 * `hitch_percent` of 0 means the built-in default. Returns 1 when it armed, so the caller can count
 * observers. */
int diag_frame_install(int level, int hitch_percent);

#endif /* DIAG_FRAME_H */
