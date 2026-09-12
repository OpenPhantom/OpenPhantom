/* text.c: see text.h. */
#include "text.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

size_t text_vformat(char *buffer, size_t size, const char *format, va_list arguments)
{
    int written;

    if (buffer == NULL || size == 0) {
        return 0;
    }

    /* One byte short, so the terminator below always has a home whatever the call did. */
    written = _vsnprintf(buffer, size - 1, format, arguments);
    if (written < 0 || (size_t)written >= size - 1) {
        buffer[size - 1] = '\0';
        return size - 1;
    }
    buffer[written] = '\0';
    return (size_t)written;
}

size_t text_format(char *buffer, size_t size, const char *format, ...)
{
    va_list arguments;
    size_t  stored;

    va_start(arguments, format);
    stored = text_vformat(buffer, size, format, arguments);
    va_end(arguments);
    return stored;
}
