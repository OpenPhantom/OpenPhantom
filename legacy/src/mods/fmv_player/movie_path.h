/* movie_path.h: the engine's own movie name turned into the path of a converted file.
 *
 * This is split out of fmv_player.c for one reason. It is the only thing in this DLL that can be
 * checked without a window, a video file and a running game: everything else here is Win32 side
 * effects or libVLC. Keeping it in its own file is what lets unittests/movie_path.c link the real
 * function rather than a copy of it.
 */
#ifndef MOVIE_PATH_H
#define MOVIE_PATH_H

#include <stdbool.h>
#include <stddef.h>

/* The part of `name` after the last separator: "movie\arena" -> "arena".
 *
 * Both separators are accepted. The engine writes backslashes at all four of its call sites, but
 * accepting a forward slash costs one comparison and removes a way for this to be quietly wrong if
 * that ever stops being true.
 *
 * False when `name` is NULL or empty, when nothing follows the last separator, or when the result
 * does not fit. `out` is null-terminated on success; its contents are unspecified otherwise. */
bool movie_path_basename(const char *name, char *out, size_t out_size);

/* <host_directory><movie_directory>\<basename of name>.<extension>, so
 * "C:\Games\TPM\" + "movies_hd" + "movie\arena" + "mp4" gives
 * "C:\Games\TPM\movies_hd\arena.mp4".
 *
 * A trailing separator on either directory is optional and exactly one is written either way,
 * which is what lets host_directory() keep its documented trailing backslash and an ini value keep
 * whatever the player typed, without this function needing to know about either.
 *
 * A single leading dot on `extension` is ignored, because "mp4" and ".mp4" are both what somebody
 * writes into an ini file and neither should produce "arena..mp4".
 *
 * False on any NULL argument, on an empty movie directory, extension or basename, and on anything
 * that does not fit. Truncation is a failure rather than a shorter answer: a truncated path names
 * a different file, and the caller's next step is to ask the file system whether that file exists.
 * `out_path`'s contents are unspecified when this returns false. */
bool movie_path_build(const char *host_directory, const char *movie_directory, const char *name,
                      const char *extension, char *out_path, size_t out_size);

/* The folder those files live in, <host_directory><movie_directory>, with no trailing separator,
 * for the one caller that asks the file system whether the folder is there at all before deciding
 * whether this feature has anything to do. Built by the same rules as movie_path_build so the two
 * cannot disagree about which folder is meant. False on the same conditions. */
bool movie_path_directory(const char *host_directory, const char *movie_directory, char *out_path,
                          size_t out_size);

#endif /* MOVIE_PATH_H */
