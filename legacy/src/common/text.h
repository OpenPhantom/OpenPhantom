/* text.h: one bounded formatter, so no call site has to remember the terminator.
 *
 * _snprintf on this toolchain neither terminates nor reports when it truncates: it fills the whole
 * buffer and answers -1. The tree used three idioms around that, sizeof plus a terminating store
 * on the next line, sizeof minus one relying on a byte nothing had written, and the full size with
 * no store at all. The third is a latent overrun the day a label grows. This is the one form.
 */
#ifndef COMMON_TEXT_H
#define COMMON_TEXT_H

#include <stdarg.h>
#include <stddef.h>

/* Formats into `buffer`, always terminating it. Returns the number of characters stored, not
 * counting the terminator, so a full buffer answers `size - 1`. A zero `size` stores nothing and
 * answers 0. */
size_t text_format(char *buffer, size_t size, const char *format, ...);
size_t text_vformat(char *buffer, size_t size, const char *format, va_list arguments);

#endif /* COMMON_TEXT_H */
