/* movie_path.c: see movie_path.h. Pure string arithmetic, no Windows, no engine. */
#include "movie_path.h"

#include <string.h>

static bool is_separator(char character)
{
    return character == '\\' || character == '/';
}

/* Appends with one bounds check and a guaranteed terminator.
 *
 * Hand-rolled rather than reached for from the CRT on purpose. The bounded forms that look right
 * here do not all terminate on truncation, and this file's whole job is to produce a path that is
 * either correct or refused. `*length` is only advanced when the whole text fit. */
static bool append(char *out, size_t out_size, size_t *length, const char *text)
{
    size_t text_length = strlen(text);

    if (text_length + 1 > out_size - *length) {
        return false;
    }
    memcpy(out + *length, text, text_length);
    *length += text_length;
    out[*length] = '\0';
    return true;
}

static bool append_character(char *out, size_t out_size, size_t *length, char character)
{
    if (out_size - *length < 2) {
        return false;
    }
    out[*length] = character;
    ++(*length);
    out[*length] = '\0';
    return true;
}

/* Separates only when there is not already one, so a directory written with or without a trailing
 * backslash produces the same path rather than one with a doubled separator in it. */
static bool append_separator_if_needed(char *out, size_t out_size, size_t *length)
{
    if (*length > 0 && is_separator(out[*length - 1])) {
        return true;
    }
    return append_character(out, out_size, length, '\\');
}

/* A directory has to name something. Refusing the empty string is not enough on its own: a value of
 * "\" or "\\" is not empty, survives that check, and then means the game folder itself, which is
 * exactly the state the empty-value refusal exists to prevent. It also makes the two public
 * functions disagree - on a drive root one of them trims down to "C:", which names the current
 * directory on that drive rather than its root, while the other keeps a separator. Refusing the
 * whole class here is cheaper than making both agree about a value nobody meant to write. */
static bool names_something(const char *directory)
{
    const char *cursor;

    for (cursor = directory; *cursor != '\0'; ++cursor) {
        if (!is_separator(*cursor)) {
            return true;
        }
    }
    return false;
}

static const char *find_basename(const char *name)
{
    const char *base = name;
    const char *cursor;

    for (cursor = name; *cursor != '\0'; ++cursor) {
        if (is_separator(*cursor)) {
            base = cursor + 1;
        }
    }
    return base;
}

/* ============================================================================================ */
bool movie_path_basename(const char *name, char *out, size_t out_size)
{
    const char *base;
    size_t      length = 0;

    if (name == NULL || out == NULL || out_size == 0) {
        return false;
    }

    base = find_basename(name);
    if (*base == '\0') {
        return false;   /* empty, or nothing after the last separator */
    }
    return append(out, out_size, &length, base);
}

/* The two directories joined, with exactly one separator between them and none at the end. Shared
 * by both public functions so the folder movie_path_directory() reports on is the same one
 * movie_path_build() looks a file up in, by construction rather than by two matching edits. */
static bool build_directory(const char *host_directory, const char *movie_directory, char *out,
                            size_t out_size, size_t *length)
{
    *length = 0;

    /* host_directory() answers with a trailing backslash, and with an empty string when it could
     * not work out where the executable is. The empty case must not turn into a leading backslash,
     * which is why the separator is appended conditionally rather than unconditionally. */
    if (!append(out, out_size, length, host_directory)) {
        return false;
    }
    if (*length > 0 && !append_separator_if_needed(out, out_size, length)) {
        return false;
    }
    return append(out, out_size, length, movie_directory);
}

bool movie_path_directory(const char *host_directory, const char *movie_directory, char *out_path,
                          size_t out_size)
{
    size_t length;

    if (host_directory == NULL || movie_directory == NULL || out_path == NULL || out_size == 0 ||
        !names_something(movie_directory)) {
        return false;
    }
    if (!build_directory(host_directory, movie_directory, out_path, out_size, &length)) {
        return false;
    }

    /* A folder written in the ini with a trailing backslash would otherwise be handed to the file
     * system with one, which is a different thing to ask about on a drive root. */
    while (length > 1 && is_separator(out_path[length - 1])) {
        --length;
        out_path[length] = '\0';
    }
    return length > 0;
}

bool movie_path_build(const char *host_directory, const char *movie_directory, const char *name,
                      const char *extension, char *out_path, size_t out_size)
{
    const char *base;
    size_t      length = 0;

    if (host_directory == NULL || movie_directory == NULL || name == NULL || extension == NULL ||
        out_path == NULL || out_size == 0) {
        return false;
    }

    /* A leading dot is dropped once, not repeatedly: ".mp4" and "mp4" are the two spellings a
     * player writes into an ini file, and "..mp4" is a typo that should stay visible rather than
     * be silently repaired into something that then fails to match any file. */
    if (*extension == '.') {
        ++extension;
    }
    if (!names_something(movie_directory) || *extension == '\0') {
        return false;
    }

    base = find_basename(name);
    if (*base == '\0') {
        return false;
    }

    if (!build_directory(host_directory, movie_directory, out_path, out_size, &length)) {
        return false;
    }
    if (!append_separator_if_needed(out_path, out_size, &length)) {
        return false;
    }
    if (!append(out_path, out_size, &length, base)) {
        return false;
    }
    if (!append_character(out_path, out_size, &length, '.')) {
        return false;
    }
    return append(out_path, out_size, &length, extension);
}
