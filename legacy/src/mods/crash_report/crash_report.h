/* crash_report.h: write a usable report when the process dies hard.
 *
 * Produces: crash_report.dll
 */
#ifndef CRASH_REPORT_H
#define CRASH_REPORT_H

void crash_report_install(void);

/* Called as the process ends. The running count of first-chance access violations goes into the
 * log one last time, a count of zero included, because it is the only total there is: the table
 * names eight sites and counts everything after them without a word. A process that dies hard
 * never gets here, and its crash report carries the same count. */
void crash_report_shutdown(void);

/* The milestone the count has passed and not yet reported, or 0 for none. `total` is the count so
 * far and `reported` the last milestone printed, 0 before the first. The milestones are 16, 64,
 * 256 and 1024; a count that jumped past several since the last look answers only the largest,
 * and past 1024 only the process end reports. Pure, so the one rule that decides whether the log
 * floods or stays silent is tested without a fault. */
long crash_report_av_milestone(long total, long reported);

#endif /* CRASH_REPORT_H */
