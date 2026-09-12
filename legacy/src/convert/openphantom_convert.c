/* openphantom_convert.c: the cutscene converter Setup runs, on Windows and under Wine alike.
 *
 * Why this exists when two working converters already ship
 *
 * The cutscene converter is a PowerShell script, and Setup used to drive it by launching
 * powershell.exe. Wine does not include PowerShell, so on a Steam Deck or any Proton or Lutris
 * install the installer worked and the conversion step failed: the game was installed and the films
 * were still Bink. The Python version beside it needs a terminal, and a Linux python3 launched from
 * inside Wine cannot open a Windows path, so Setup could not drive that either.
 * A Windows console executable has neither problem, because Wine runs Windows executables: one
 * program, run the same way on both.
 *
 * What it must stay in step with
 *
 * This is the third implementation of one job. It is the one Setup uses; the scripts remain for
 * people who run them by hand. A change here that does not change convert_movies.ps1 and
 * convert_movies.py is a bug, and these are the parts that have to agree:
 *
 *   * the per-film height cap that keeps the logo inside a width decoders accept
 *   * the FFmpeg arguments
 *   * the machine-readable lines, which Setup parses
 *
 * It carries no game data and redistributes nothing: it reads the films the player already owns
 * and writes bigger copies of them.
 */
#include <windows.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* LOGO.BIK is the only 2.35:1 film in the game, so at 2160 lines it is 5082 pixels wide and most
 * H.264 decoders refuse it: the file encodes, the player opens it, the picture is black. */
#define MAX_ENCODED_WIDTH 3840

static int quiet;

/* _snprintf neither terminates nor says when it truncates, and every buffer here is a path. */
static void format_path(char *buffer, size_t size, const char *format, ...)
{
    va_list arguments;
    int     written;

    va_start(arguments, format);
    written = _vsnprintf(buffer, size - 1, format, arguments);
    va_end(arguments);
    buffer[(written < 0 || (size_t)written >= size - 1) ? size - 1 : (size_t)written] = '\0';
}

/* Machine-readable, always: these lines are what Setup parses, so they go out whether or not the
 * run is quiet, and they are flushed because Setup reads them as they appear. */
static void emit(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vfprintf(stdout, format, args);
    va_end(args);
    fputc('\n', stdout);
    fflush(stdout);
}

/* For a person. Silent under -quiet so the machine lines are the only output. */
static void note(const char *format, ...)
{
    va_list args;
    if (quiet) {
        return;
    }
    va_start(args, format);
    vfprintf(stdout, format, args);
    va_end(args);
    fputc('\n', stdout);
    fflush(stdout);
}

static void fail(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    fputs("error: ", stderr);
    vfprintf(stderr, format, args);
    va_end(args);
    fputc('\n', stderr);
}

static unsigned read_u32(const unsigned char *p)
{
    return (unsigned)p[0] | ((unsigned)p[1] << 8) | ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
}

/* ==============================================================================================
 * The cutscenes
 * ============================================================================================ */

/* Bink stores its frame size as two little-endian dwords at offset 20 and 24. */
static int bink_frame_size(const char *path, int *out_width, int *out_height)
{
    FILE *handle = fopen(path, "rb");
    unsigned char header[28];

    if (handle == NULL) {
        return 0;
    }
    if (fread(header, 1, sizeof header, handle) != sizeof header || memcmp(header, "BIK", 3) != 0) {
        fclose(handle);
        return 0;
    }
    fclose(handle);
    *out_width  = (int)read_u32(header + 20);
    *out_height = (int)read_u32(header + 24);
    return (*out_width > 0 && *out_height > 0);
}

/* The height to actually encode this film at: the one asked for, unless it would make it too wide.
 * A film whose header cannot be read keeps the requested height, as happened before the
 * cap existed. */
static int encode_height(const char *path, int requested)
{
    int width = 0, height = 0, widest, capped;

    if (requested <= 0 || !bink_frame_size(path, &width, &height)) {
        return requested;
    }
    widest = (int)((double)MAX_ENCODED_WIDTH * height / width);
    if (widest >= requested) {
        return requested;
    }
    capped = (widest / 2) * 2;              /* H.264 4:2:0 needs both sides even */
    return (capped >= 2) ? capped : requested;
}

static int run_ffmpeg(const char *command_line)
{
    STARTUPINFOA startup;
    PROCESS_INFORMATION process;
    DWORD code = (DWORD)-1;
    char *mutable_line = _strdup(command_line);

    if (mutable_line == NULL) {
        return -2;
    }
    ZeroMemory(&startup, sizeof startup);
    startup.cb = sizeof startup;

    if (!CreateProcessA(NULL, mutable_line, NULL, NULL, FALSE, 0, NULL, NULL, &startup, &process)) {
        free(mutable_line);
        return -2;
    }
    WaitForSingleObject(process.hProcess, INFINITE);
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hProcess);
    CloseHandle(process.hThread);
    free(mutable_line);
    return (int)code;
}

static int convert_movies(const char *game, const char *output, const char *ffmpeg,
                          int height, int force)
{
    char pattern[MAX_PATH];
    char source[MAX_PATH];
    WIN32_FIND_DATAA found;
    HANDLE search;
    int converted = 0, skipped = 0, failed = 0;

    format_path(source, sizeof source, "%s\\GAMEDATA\\MOVIE", game);
    format_path(pattern, sizeof pattern, "%s\\*.bik", source);

    if (quiet) {
        emit("FFMPEG %s", ffmpeg);
    } else {
        note("Using %s", ffmpeg);
    }

    CreateDirectoryA(output, NULL);

    search = FindFirstFileA(pattern, &found);
    if (search == INVALID_HANDLE_VALUE) {
        if (quiet) { emit("DONE converted=0 skipped=0 failed=0"); }
        else       { note("No .BIK files in %s", source); }
        return 1;
    }

    do {
        char base[MAX_PATH], bik[MAX_PATH], final[MAX_PATH], temp[MAX_PATH];
        char filters[128], command[MAX_PATH * 4];
        char *dot;
        int this_height, code;

        if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            continue;
        }
        format_path(base, sizeof base, "%s", found.cFileName);
        dot = strrchr(base, '.');
        if (dot != NULL) { *dot = '\0'; }
        _strlwr(base);

        format_path(bik, sizeof bik, "%s\\%s", source, found.cFileName);
        format_path(final, sizeof final, "%s\\%s.mp4", output, base);
        format_path(temp, sizeof temp, "%s\\%s.converting.mp4", output, base);
        bik[sizeof bik - 1] = final[sizeof final - 1] = temp[sizeof temp - 1] = '\0';

        if (!force && GetFileAttributesA(final) != INVALID_FILE_ATTRIBUTES) {
            ++skipped;
            if (quiet) { emit("SKIP %s", found.cFileName); }
            else       { note("  %-14s already converted", found.cFileName); }
            continue;
        }

        if (height > 0) {
            this_height = encode_height(bik, height);
            if (this_height != height) {
                note("  %s: %d lines rather than %d, so it stays inside %d pixels wide",
                     found.cFileName, this_height, height, MAX_ENCODED_WIDTH);
            }
            format_path(filters, sizeof filters, "scale=-2:%d:flags=lanczos", this_height);
        } else {
            /* Ten of the eleven films have an odd height, which H.264 cannot encode. Cropping
             * drops at most one row and one column and leaves every remaining pixel as it was
             * decoded, where scaling would resample the whole frame and padding would bake in a
             * black row. */
            format_path(filters, sizeof filters, "crop=trunc(iw/2)*2:trunc(ih/2)*2");
        }

        format_path(command, sizeof command,
                    "\"%s\" -y -i \"%s\" -vf %s -c:v libx264 -preset slow -crf 18 "
                    "-pix_fmt yuv420p -profile:v high -c:a aac -b:a 192k -movflags +faststart%s "
                    "\"%s\"",
                    ffmpeg, bik, filters, quiet ? " -loglevel quiet" : "", temp);

        note("  %-14s converting...", found.cFileName);
        code = run_ffmpeg(command);

        if (code == 0 && GetFileAttributesA(temp) != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA(final);
            if (MoveFileA(temp, final)) {
                ++converted;
                if (quiet) { emit("OK %s", found.cFileName); }
                continue;
            }
            code = -1;
        }

        ++failed;
        DeleteFileA(temp);
        if (quiet) { emit("FAIL %s %d", found.cFileName, code ? code : -1); }
        else       { note("    failed (code %d)", code); }
    } while (FindNextFileA(search, &found));

    FindClose(search);

    if (quiet) {
        emit("DONE converted=%d skipped=%d failed=%d", converted, skipped, failed);
    } else {
        note("");
        note("%d converted, %d already there, %d failed", converted, skipped, failed);
    }
    return (failed > 0 && converted == 0) ? 1 : 0;
}

/* ============================================================================================ */
static void usage(void)
{
    fputs(
        "openphantom_convert: makes bigger copies of the films in your own game.\n"
        "\n"
        "  openphantom_convert movies --game DIR [--height LINES] [--ffmpeg PATH]\n"
        "                             [--output DIR] [--force] [--quiet]\n"
        "\n"
        "Reads your own game's films and writes beside them. Nothing is downloaded and no game\n"
        "file is modified.\n", stderr);
}

int main(int argc, char **argv)
{
    const char *game = NULL, *output = NULL, *ffmpeg = NULL;
    char output_buffer[MAX_PATH], ffmpeg_buffer[MAX_PATH];
    int height = 0, force = 0;
    int i;

    /* The first word names the job. Only the films are converted now; the word stays so the
     * command line Setup writes is the one it always wrote. */
    if (argc < 2 || strcmp(argv[1], "movies") != 0) {
        usage();
        return 2;
    }

    for (i = 2; i < argc; ++i) {
        if (strcmp(argv[i], "--game") == 0 && i + 1 < argc)          { game = argv[++i]; }
        else if (strcmp(argv[i], "--output") == 0 && i + 1 < argc)   { output = argv[++i]; }
        else if (strcmp(argv[i], "--ffmpeg") == 0 && i + 1 < argc)   { ffmpeg = argv[++i]; }
        else if (strcmp(argv[i], "--height") == 0 && i + 1 < argc)   { height = atoi(argv[++i]); }
        else if (strcmp(argv[i], "--force") == 0)                    { force = 1; }
        else if (strcmp(argv[i], "--quiet") == 0)                    { quiet = 1; }
        else { fail("unknown argument: %s", argv[i]); usage(); return 2; }
    }

    if (game == NULL) {
        fail("--game is required.");
        return 2;
    }
    {
        char probe[MAX_PATH];
        format_path(probe, sizeof probe, "%s\\WMAIN.EXE", game);
        if (GetFileAttributesA(probe) == INVALID_FILE_ATTRIBUTES) {
            fail("there is no WMAIN.EXE in %s, so that is not the game folder.", game);
            return 2;
        }
    }

    if (output == NULL) {
        format_path(output_buffer, sizeof output_buffer, "%s\\movies_hd", game);
        output = output_buffer;
    }
    if (ffmpeg == NULL) {
        format_path(ffmpeg_buffer, sizeof ffmpeg_buffer, "%s\\mods\\fmv\\ffmpeg.exe", game);
        ffmpeg = ffmpeg_buffer;
    }
    if (GetFileAttributesA(ffmpeg) == INVALID_FILE_ATTRIBUTES) {
        fail("no ffmpeg at %s. Pass --ffmpeg with its path.", ffmpeg);
        return 2;
    }
    return convert_movies(game, output, ffmpeg, height, force);
}
