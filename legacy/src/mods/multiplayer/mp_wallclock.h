/* mp_wallclock.h: one monotonic millisecond clock for the session and the channel.
 *
 * Layer 0. No engine, no address, no socket: it wraps the operating system's performance counter
 * and nothing else, and the unit test drives it with nothing else in the process.
 *
 * Why a clock of its own. The session and the channel take time as a parameter, which is what
 * makes them testable, and the bridge used to feed them a count that advanced by one substep per
 * substep. That count stands still whenever the substeps do, through a level load, a window drag
 * or a lost focus, so a timeout measured on it never fires while the far side is waiting, and a
 * far side that is still ticking drops this one. A clock that runs whether or not the game does
 * is what the timeouts and the keepalives were written for. What the synthetic count cost in the
 * field: after any stall over the connected timeout the host answered the client's fresh request
 * with the old accept until its own count had counted the timeout out again, a dead zone of ten
 * seconds after every level load, and a load over seventy seconds ended the rejoin for good.
 *
 * Why the performance counter and not the tick count. The tick count moves in steps of ten to
 * sixteen milliseconds, and a keepalive gate or a resend throttle of tens of milliseconds
 * measured in such steps is a coin toss at every boundary. The counter resolves microseconds.
 *
 * The base is the first call, so the first value is zero and the count never starts near the
 * wrap; only differences of it are taken anyway, as with every clock in this feature.
 */
#ifndef MULTIPLAYER_MP_WALLCLOCK_H
#define MULTIPLAYER_MP_WALLCLOCK_H

#include <stdint.h>

/* Milliseconds since the first call, never decreasing between two calls. */
uint32_t mp_wallclock_ms(void);

#endif /* MULTIPLAYER_MP_WALLCLOCK_H */
