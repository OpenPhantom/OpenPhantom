/* power_throttling.h: ask Windows not to slow this process down or coarsen its timers while its
 * window is behind another.
 *
 * Windows 11 may treat a process whose window is minimised, hidden or behind another as background
 * work, the treatment called EcoQoS: it may run it on efficiency cores at a lower clock, and it may
 * ignore the timer resolution the process asked for. For most programs that is right. For a game it
 * is right only while nobody is playing it, and the multiplayer is tested with several instances on
 * one machine of which only one window can be in front, while a real session has a host that is
 * not always the window in front either. Whether this is what made the instances behind the others
 * lose frames is not established; the frame line of the diagnostics counts the frames begun on an
 * efficiency core so that it can be.
 *
 * HighQos=1 opts out of both, the documented way for a process to say its work is not background
 * work. Nothing about how the game plays changes. Either way the state Windows ends up with is
 * written into the log once a start.
 */
#ifndef POWER_THROTTLING_H
#define POWER_THROTTLING_H

#include <stdbool.h>

/* Applies HighQos and writes the resulting state into the log. The two process information calls
 * are looked up at run time, so a Windows older than 8 loses the line and not the DLL. */
void power_throttling_apply(bool high_qos);

/* How the log names one switch: "left to Windows" when `bit` is not in `control_mask`, "held on"
 * when it is in both masks and "held off" when it is in the control mask alone. Pure. */
const char *power_throttling_state_name(unsigned long control_mask, unsigned long state_mask,
                                        unsigned long bit);

#endif /* POWER_THROTTLING_H */
