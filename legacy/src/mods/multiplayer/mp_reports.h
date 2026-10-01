/* mp_reports.h: everything this feature prints when a run ends.
 *
 * Layer 3. A list and not a decision: each entry calls into the module that owns those numbers,
 * and every one of them is written to be safe on a module that never installed. It lives apart
 * from the module entry because it grows whenever a counter is added anywhere in the feature, and
 * multiplayer.c reached the length this tree refuses while carrying it.
 *
 * The question of WHETHER to print stays with the module entry, where the configuration is. This
 * is only the what.
 *
 * `why` is what ended the run, which the lines that take it print as their own reason.
 */
#ifndef MULTIPLAYER_MP_REPORTS_H
#define MULTIPLAYER_MP_REPORTS_H

void mp_reports_run(const char *why);

#endif /* MULTIPLAYER_MP_REPORTS_H */
