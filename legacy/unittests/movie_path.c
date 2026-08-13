/* movie_path.c: the one piece of fmv_player that can be checked without the game.
 *
 * What it decides is which file on disk a movie name turns into, and both ways of getting that
 * wrong are silent. Build the wrong path and every movie falls back to Bink while the log says
 * nothing was found, which reads exactly like "no converted files yet". Build a path that got
 * truncated on the way and the answer is about a different file entirely, which is worse, because
 * GetFileAttributesW will happily answer about that one.
 *
 * The engine's four movie call sites all pass a backslash-relative name with no extension
 * ("movie\arena"), and host_directory() answers with a trailing backslash or with an empty string
 * when it could not work out where the executable is. Both of those are properties this file
 * pins down, because nothing else in the DLL would notice if they changed.
 */
#include "unittest.h"

#include "movie_path.h"

#include <string.h>

static int equals(const char *actual, const char *expected)
{
    return strcmp(actual, expected) == 0;
}

int main(void)
{
    char path[64];
    char base[16];

    /* --- the basename, which is the part the engine's own naming decides ---------------------- */
    ut_section("the basename");

    ut_check(movie_path_basename("movie\\arena", base, sizeof base) && equals(base, "arena"),
             "the engine's own \"movie\\arena\" gives \"arena\"");
    ut_check(movie_path_basename("arena", base, sizeof base) && equals(base, "arena"),
             "a name with no directory at all is its own basename");
    ut_check(movie_path_basename("movie/arena", base, sizeof base) && equals(base, "arena"),
             "a forward slash separates too, so a future call site cannot break this quietly");
    ut_check(movie_path_basename("a\\b/c\\arena", base, sizeof base) && equals(base, "arena"),
             "the LAST separator is the one that counts, whichever kind it is");

    ut_check(!movie_path_basename(NULL, base, sizeof base), "a null name is refused");
    ut_check(!movie_path_basename("", base, sizeof base), "an empty name is refused");
    ut_check(!movie_path_basename("movie\\", base, sizeof base),
             "a name that ends in a separator has no basename and is refused");
    ut_check(!movie_path_basename("arena", base, 0), "a zero-sized buffer is refused");

    /* Exactly at the limit and exactly one past it. "arena" is five characters, so six bytes is
     * the smallest buffer that can hold it with its terminator. */
    ut_check(movie_path_basename("arena", base, 6) && equals(base, "arena"),
             "a buffer of exactly the right size is enough");
    ut_check(!movie_path_basename("arena", base, 5),
             "one byte short is refused rather than truncated");

    /* --- the whole path ---------------------------------------------------------------------- */
    ut_section("the whole path");

    ut_check(movie_path_build("C:\\Games\\TPM\\", "movies_hd", "movie\\arena", "mp4",
                              path, sizeof path) &&
             equals(path, "C:\\Games\\TPM\\movies_hd\\arena.mp4"),
             "the shipped configuration builds <game>\\movies_hd\\arena.mp4");

    /* host_directory() documents a trailing backslash. An ini value has whatever the player typed.
     * Every combination has to land on the same path, because otherwise the defect is a doubled
     * separator that only some installations ever see. */
    ut_check(movie_path_build("C:\\g\\", "d", "n", "x", path, sizeof path) &&
             equals(path, "C:\\g\\d\\n.x"),
             "a trailing backslash on the game directory is not doubled");
    ut_check(movie_path_build("C:\\g", "d", "n", "x", path, sizeof path) &&
             equals(path, "C:\\g\\d\\n.x"),
             "a game directory without a trailing backslash gets one");
    ut_check(movie_path_build("C:\\g\\", "d\\", "n", "x", path, sizeof path) &&
             equals(path, "C:\\g\\d\\n.x"),
             "a movie directory written with a trailing backslash is not doubled either");
    ut_check(movie_path_build("C:\\g\\", "d/", "n", "x", path, sizeof path) &&
             equals(path, "C:\\g\\d/n.x"),
             "a forward slash already there is left as it is, since Windows accepts both");

    /* The empty answer host_directory() gives when it could not find the executable. The path has
     * to stay relative rather than becoming an absolute one on the current drive. */
    ut_check(movie_path_build("", "d", "n", "x", path, sizeof path) && equals(path, "d\\n.x"),
             "an unknown game directory gives a relative path, not a leading backslash");

    ut_check(movie_path_build("C:\\g\\", "movies_hd", "movie\\arena", ".mp4",
                              path, sizeof path) &&
             equals(path, "C:\\g\\movies_hd\\arena.mp4"),
             "an extension written with its dot gives the same path as one without");
    ut_check(movie_path_build("C:\\g\\", "d", "n", "..x", path, sizeof path) &&
             equals(path, "C:\\g\\d\\n..x"),
             "a second dot is left alone, so a typo stays visible instead of being repaired");

    ut_section("refusals");

    ut_check(!movie_path_build(NULL, "d", "n", "x", path, sizeof path),
             "a null game directory is refused");
    ut_check(!movie_path_build("C:\\g\\", NULL, "n", "x", path, sizeof path),
             "a null movie directory is refused");
    ut_check(!movie_path_build("C:\\g\\", "d", NULL, "x", path, sizeof path),
             "a null movie name is refused");
    ut_check(!movie_path_build("C:\\g\\", "d", "n", NULL, path, sizeof path),
             "a null extension is refused");
    ut_check(!movie_path_build("C:\\g\\", "d", "n", "x", NULL, sizeof path),
             "a null output buffer is refused");

    /* An ini file with the key present but empty is the realistic case for both of these, and
     * neither may produce a path that happens to name a directory or an extensionless file. */
    ut_check(!movie_path_build("C:\\g\\", "", "n", "x", path, sizeof path),
             "an empty MovieDirectory is refused rather than searching the game folder itself");

    /* The same refusal has to cover the values that are not empty but still name nothing, because
     * they reach exactly the state the empty check exists to prevent: the game folder itself. */
    ut_check(!movie_path_build("C:\\g\\", "\\", "n", "x", path, sizeof path),
             "a MovieDirectory of one separator is refused too");
    ut_check(!movie_path_build("C:\\g\\", "\\\\", "n", "x", path, sizeof path),
             "a MovieDirectory of several separators is refused");
    ut_check(!movie_path_build("C:\\g\\", "/", "n", "x", path, sizeof path),
             "a MovieDirectory of one forward slash is refused");
    ut_check(movie_path_build("C:\\g\\", "\\d", "n", "x", path, sizeof path),
             "a MovieDirectory that merely STARTS with a separator still names something");
    ut_check(!movie_path_build("C:\\g\\", "d", "n", "", path, sizeof path),
             "an empty Extension is refused");
    ut_check(!movie_path_build("C:\\g\\", "d", "n", ".", path, sizeof path),
             "an Extension of just a dot is refused, since dropping it leaves nothing");
    ut_check(!movie_path_build("C:\\g\\", "d", "movie\\", "x", path, sizeof path),
             "a movie name with no basename is refused");

    /* --- truncation, at the boundary and one past it ------------------------------------------ */
    ut_section("truncation");

    /* "" + "d" + "n" + "x" is "d\n.x", five characters, so six bytes is exactly enough. */
    ut_check(movie_path_build("", "d", "n", "x", path, 6) && equals(path, "d\\n.x"),
             "a buffer of exactly the right size is enough");
    ut_check(!movie_path_build("", "d", "n", "x", path, 5),
             "one byte short is refused rather than truncated");
    ut_check(!movie_path_build("", "d", "n", "x", path, 1),
             "a buffer with room for the terminator alone is refused");
    ut_check(!movie_path_build("", "d", "n", "x", path, 0), "a zero-sized buffer is refused");

    /* Each stage has to be the one that refuses when it is the one that does not fit, so a failure
     * cannot depend on which part of the path happened to be long. */
    ut_check(!movie_path_build("C:\\a_very_long_game_directory_name\\", "d", "n", "x", path, 16),
             "an over-long game directory is refused");
    ut_check(!movie_path_build("", "a_very_long_movie_directory_name", "n", "x", path, 16),
             "an over-long movie directory is refused");
    ut_check(!movie_path_build("", "d", "a_very_long_movie_name_indeed", "x", path, 16),
             "an over-long movie name is refused");
    ut_check(!movie_path_build("", "d", "n", "a_very_long_extension", path, 16),
             "an over-long extension is refused");

    /* --- the folder, which decides whether this feature does anything at all ------------------ */
    ut_section("the folder");

    /* No trailing separator: the caller hands this straight to the file system, and a trailing
     * backslash asks a different question on a drive root than it does anywhere else. */
    ut_check(movie_path_directory("C:\\Games\\TPM\\", "movies_hd", path, sizeof path) &&
             equals(path, "C:\\Games\\TPM\\movies_hd"),
             "the shipped configuration gives <game>\\movies_hd with no trailing separator");
    ut_check(movie_path_directory("C:\\g", "d", path, sizeof path) && equals(path, "C:\\g\\d"),
             "a game directory without a trailing backslash still gets exactly one separator");
    ut_check(movie_path_directory("C:\\g\\", "d\\", path, sizeof path) && equals(path, "C:\\g\\d"),
             "a movie directory written with a trailing backslash loses it");
    ut_check(movie_path_directory("C:\\g\\", "d\\\\", path, sizeof path) && equals(path, "C:\\g\\d"),
             "several trailing separators are all removed");
    ut_check(movie_path_directory("", "d", path, sizeof path) && equals(path, "d"),
             "an unknown game directory leaves the movie directory relative");

    ut_check(!movie_path_directory("C:\\g\\", "", path, sizeof path),
             "an empty MovieDirectory is refused here too, not answered with the game folder");
    ut_check(!movie_path_directory("C:\\g\\", "\\", path, sizeof path),
             "a MovieDirectory of one separator is refused here too");
    ut_check(!movie_path_directory("C:\\g\\", "\\\\", path, sizeof path),
             "a MovieDirectory of several separators is refused here too");

    /* Both functions have to refuse the same values, or the install check and the per-movie lookup
     * end up talking about different folders. This is the pair that used to disagree on a drive
     * root, where the trim produced "C:" (the current directory on that drive) on one side. */
    ut_check(!movie_path_directory("C:\\", "\\", path, sizeof path) ==
             !movie_path_build("C:\\", "\\", "n", "x", path, sizeof path),
             "on a drive root the two functions agree about refusing a separator-only folder");
    ut_check(!movie_path_directory(NULL, "d", path, sizeof path),
             "a null game directory is refused");
    ut_check(!movie_path_directory("C:\\g\\", "d", path, 6),
             "a buffer one byte short is refused");
    ut_check(movie_path_directory("C:\\g\\", "d", path, 7) && equals(path, "C:\\g\\d"),
             "a buffer of exactly the right size is enough");

    /* The two functions must agree about which folder is meant, because one decides whether the
     * feature installs at all and the other decides which file every movie looks for. */
    {
        char folder[64];
        char file[64];

        ut_check(movie_path_directory("C:\\g\\", "movies_hd", folder, sizeof folder) &&
                 movie_path_build("C:\\g\\", "movies_hd", "movie\\arena", "mp4", file,
                                  sizeof file) &&
                 strncmp(file, folder, strlen(folder)) == 0 && file[strlen(folder)] == '\\',
                 "the file always sits directly inside the folder the install check tested");
    }

    return ut_summary("movie path");
}
