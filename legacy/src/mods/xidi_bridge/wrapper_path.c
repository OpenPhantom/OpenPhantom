#include "wrapper_path.h"

#include <string.h>

static bool is_separator(char character)
{
    return character == '\\' || character == '/';
}

/* Case folding for ASCII only, which is what a comparison of two Windows paths needs here. The
 * locale-aware forms would make the answer depend on the process locale, and a path that counts as
 * inside the game folder under one locale and outside it under another is worse than a wrong
 * answer, because it is not reproducible. */
static char fold(char character)
{
    if (character >= 'A' && character <= 'Z') {
        return (char)(character - 'A' + 'a');
    }
    return is_separator(character) ? '\\' : character;
}

bool wrapper_path_inside(const char *directory, const char *path)
{
    size_t length;
    size_t index;

    if (directory == NULL || path == NULL || directory[0] == '\0' || path[0] == '\0') {
        return false;
    }

    length = strlen(directory);
    /* A trailing separator is not part of what has to match: it is re-checked below as the
     * boundary, so "C:\Game" and "C:\Game\" ask the same question. */
    while (length > 0 && is_separator(directory[length - 1])) {
        --length;
    }
    if (length == 0) {
        return false;
    }

    for (index = 0; index < length; ++index) {
        if (path[index] == '\0' || fold(path[index]) != fold(directory[index])) {
            return false;
        }
    }

    /* The character after the directory decides it. A separator means `path` continues below the
     * directory; anything else means this is a different name that happens to start the same way. */
    return is_separator(path[length]);
}

bool wrapper_path_join(const char *directory, const char *file_name,
                       char *buffer, size_t buffer_size)
{
    size_t directory_length;
    size_t file_length;
    bool   needs_separator;
    size_t total;

    if (buffer == NULL || buffer_size == 0) {
        return false;
    }
    buffer[0] = '\0';

    if (directory == NULL || file_name == NULL ||
        directory[0] == '\0' || file_name[0] == '\0') {
        return false;
    }

    directory_length = strlen(directory);
    file_length = strlen(file_name);
    needs_separator = !is_separator(directory[directory_length - 1]);

    total = directory_length + (needs_separator ? 1u : 0u) + file_length;
    if (total + 1 > buffer_size) {
        return false;
    }

    memcpy(buffer, directory, directory_length);
    if (needs_separator) {
        buffer[directory_length] = '\\';
        ++directory_length;
    }
    memcpy(buffer + directory_length, file_name, file_length);
    buffer[total] = '\0';
    return true;
}
