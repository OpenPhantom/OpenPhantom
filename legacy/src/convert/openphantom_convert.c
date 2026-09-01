/* openphantom_convert.c: the converter Setup runs, on Windows and under Wine alike.
 *
 * ==============================================================================================
 * Why this exists at all, when two working converters already ship
 *
 * The cutscene and menu converters are PowerShell, and Setup drives them by launching
 * powershell.exe. Wine does not include PowerShell. So on a Steam Deck, or any Proton or Lutris
 * install, the installer itself works perfectly and then both conversion steps fail: the game is
 * installed but the films are still Bink and the menus are still 640x480.
 *
 * The Python versions beside them solve that for somebody willing to open a terminal, which most
 * people are not, and they cannot be driven from Setup either: a Linux python3 launched from inside
 * Wine cannot open a path like C:\Program Files\..., so Setup would have to translate every path
 * through winepath first and hope.
 *
 * A Windows console executable has none of those problems, because Wine runs Windows executables.
 * That is the whole idea here: ONE program, run the same way on both, so Linux stops being a
 * special case in the installer rather than being handled by a second code path that only somebody
 * with a Deck can test.
 *
 * ==============================================================================================
 * What it must stay in step with
 *
 * This is now the third implementation of the same two jobs, which is a real hazard rather than a
 * theoretical one. It is the one Setup uses; the scripts remain for people who run them by hand.
 * A change here that does not change convert_menu.ps1, convert_menu.py, convert_movies.ps1 and
 * convert_movies.py is a bug, and these are the parts that have to agree:
 *
 *   * the LAB directory format and the two picture type tags
 *   * nearest neighbour, sampling from pixel centres
 *   * the output names, which are the archive member names unchanged
 *   * the per-film height cap that keeps the logo inside a width decoders accept
 *   * the FFmpeg arguments
 *   * the machine-readable lines, which Setup parses
 *
 * It carries no game data and redistributes nothing: it reads the archives the player already owns
 * and writes bigger copies of what is inside them.
 */
#include <windows.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CANVAS_WIDTH   640
#define CANVAS_HEIGHT  480

/* The run length encoder writes a literal control word as `run & 0xfff` while advancing the output
 * by the whole run, so a menu canvas wider than 4095 pixels corrupts the stream. */
#define MAX_CANVAS_RATIO (4095.0 / (double)CANVAS_WIDTH)

/* LOGO.BIK is the only 2.35:1 film in the game, so at 2160 lines it is 5082 pixels wide and most
 * H.264 decoders refuse it: the file encodes, the player opens it, the picture is black. */
#define MAX_ENCODED_WIDTH 3840

#define MANIFEST_NAME "openphantom_menu_art.txt"

static int quiet;

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

static unsigned char *read_whole_file(const char *path, long *out_size)
{
    FILE *handle = fopen(path, "rb");
    unsigned char *data;
    long size;

    if (handle == NULL) {
        return NULL;
    }
    if (fseek(handle, 0, SEEK_END) != 0) {
        fclose(handle);
        return NULL;
    }
    size = ftell(handle);
    if (size <= 0 || fseek(handle, 0, SEEK_SET) != 0) {
        fclose(handle);
        return NULL;
    }
    data = (unsigned char *)malloc((size_t)size);
    if (data == NULL) {
        fclose(handle);
        return NULL;
    }
    if (fread(data, 1, (size_t)size, handle) != (size_t)size) {
        free(data);
        fclose(handle);
        return NULL;
    }
    fclose(handle);
    *out_size = size;
    return data;
}

static unsigned read_u32(const unsigned char *p)
{
    return (unsigned)p[0] | ((unsigned)p[1] << 8) | ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
}

static void write_u32(unsigned char *p, unsigned value)
{
    p[0] = (unsigned char)(value & 0xFFu);
    p[1] = (unsigned char)((value >> 8) & 0xFFu);
    p[2] = (unsigned char)((value >> 16) & 0xFFu);
    p[3] = (unsigned char)((value >> 24) & 0xFFu);
}

/* ==============================================================================================
 * The menu artwork
 * ============================================================================================ */

/* Nearest neighbour on a 24 bpp bottom-up BI_RGB bitmap.
 *
 * NEAREST NEIGHBOUR IS NOT LAZINESS. After the engine converts a bitmap to 16 bit, a pixel that is
 * exactly zero is a SKIP, i.e. transparent. A smoothing filter invents near-black where black was
 * transparent, and new exact-zero pixels where there were none, so it would halo every button and
 * punch holes in dark artwork. Whole pixel replication cannot invent a colour that was not already
 * in the source, so it is exact on that rule by construction.
 *
 * Rows are built one SOURCE pixel at a time: each source pixel becomes a run of however many
 * destination pixels land on it, so a six times scale does 640 writes per row rather than 3840. A
 * destination row whose source row is the one just built is a straight copy of it, which is most of
 * the rows at any real scale.
 *
 * Returns the new file, and its length through out_size, or NULL when this is not a bitmap this
 * understands, which the caller reports as a skip rather than a failure. */
static unsigned char *scale_bitmap(const unsigned char *data, long size,
                                   double ratio_x, double ratio_y, long *out_size,
                                   int *out_width, int *out_height)
{
    unsigned pixel_offset;
    int width, height, target_width, target_height;
    long source_stride, target_stride, total;
    unsigned bpp, compression;
    unsigned char *out;
    int *runs;
    int x, y, previous_source_y;
    unsigned char *previous_row = NULL;

    if (size < 54 || data[0] != 'B' || data[1] != 'M') {
        return NULL;
    }
    pixel_offset = read_u32(data + 10);
    width        = (int)read_u32(data + 18);
    height       = (int)read_u32(data + 22);
    bpp          = (unsigned)(data[28] | (data[29] << 8));
    compression  = read_u32(data + 30);

    if (bpp != 24 || compression != 0 || width <= 0 || height <= 0 ||
        (long)pixel_offset >= size) {
        return NULL;
    }

    source_stride = ((long)width * 3 + 3) & ~3L;
    if ((long)pixel_offset + source_stride * height > size) {
        return NULL;                        /* truncated; not ours to guess at */
    }

    target_width  = (int)((double)width * ratio_x + 0.5);
    target_height = (int)((double)height * ratio_y + 0.5);
    if (target_width < 1)  { target_width = 1; }
    if (target_height < 1) { target_height = 1; }
    target_stride = ((long)target_width * 3 + 3) & ~3L;
    total = (long)pixel_offset + target_stride * target_height;

    out = (unsigned char *)malloc((size_t)total);
    runs = (int *)calloc((size_t)width, sizeof(int));
    if (out == NULL || runs == NULL) {
        free(out);
        free(runs);
        return NULL;
    }

    memcpy(out, data, pixel_offset);
    write_u32(out + 18, (unsigned)target_width);
    write_u32(out + 22, (unsigned)target_height);
    write_u32(out + 34, (unsigned)(target_stride * target_height));   /* biSizeImage */
    write_u32(out + 2,  (unsigned)total);                            /* bfSize      */

    /* How many destination pixels each source pixel owns. Sampling from pixel CENTRES, which is what
     * keeps the picture from drifting half a source pixel up and to the left. */
    for (x = 0; x < target_width; ++x) {
        int source_x = (int)(((double)x + 0.5) / ratio_x);
        if (source_x >= width)  { source_x = width - 1; }
        if (source_x < 0)       { source_x = 0; }
        runs[source_x] += 1;
    }

    previous_source_y = -1;
    for (y = 0; y < target_height; ++y) {
        unsigned char *row = out + pixel_offset + (long)y * target_stride;
        int source_y = (int)(((double)y + 0.5) / ratio_y);

        if (source_y >= height) { source_y = height - 1; }
        if (source_y < 0)       { source_y = 0; }

        if (source_y == previous_source_y && previous_row != NULL) {
            memcpy(row, previous_row, (size_t)target_stride);
            continue;
        }

        {
            const unsigned char *from = data + pixel_offset + (long)source_y * source_stride;
            unsigned char *write = row;
            int i;

            for (x = 0; x < width; ++x) {
                for (i = 0; i < runs[x]; ++i) {
                    write[0] = from[x * 3 + 0];
                    write[1] = from[x * 3 + 1];
                    write[2] = from[x * 3 + 2];
                    write += 3;
                }
            }
            while (write < row + target_stride) {
                *write++ = 0;                /* the row padding every stride carries */
            }
        }
        previous_row = row;
        previous_source_y = source_y;
    }

    free(runs);
    *out_size = total;
    *out_width = target_width;
    *out_height = target_height;
    return out;
}

/* One member of a LABN archive: the name, and where its bytes are. */
typedef struct lab_member {
    char  name[64];
    long  offset;
    long  size;
} lab_member_t;

/* The LAB directory: a 16 byte header (magic, version, count, name table size), then one 16 byte
 * record per member holding name offset, data offset, size and a BYTE REVERSED type tag, then the
 * name blob, then the member data uncompressed.
 *
 * Only the two picture tags are wanted. Returns how many were found, or -1 when this is not a LABN
 * archive at all. */
static int read_lab_directory(const unsigned char *data, long size,
                              lab_member_t *out, int capacity)
{
    unsigned count, name_bytes;
    long names_at;
    unsigned i;
    int found = 0;

    if (size < 16 || memcmp(data, "LABN", 4) != 0) {
        return -1;
    }
    count      = read_u32(data + 8);
    name_bytes = read_u32(data + 12);
    if (count > 100000u) {
        return -1;
    }
    names_at = 16 + (long)count * 16;
    if (names_at + (long)name_bytes > size) {
        return -1;
    }

    for (i = 0; i < count && found < capacity; ++i) {
        const unsigned char *record = data + 16 + (long)i * 16;
        unsigned name_offset = read_u32(record + 0);
        unsigned data_offset = read_u32(record + 4);
        unsigned member_size = read_u32(record + 8);
        char tag[5];
        const char *name;
        size_t length;

        /* Stored byte reversed, so 'MENU' is written 'UNEM'. */
        tag[0] = (char)record[15];
        tag[1] = (char)record[14];
        tag[2] = (char)record[13];
        tag[3] = (char)record[12];
        tag[4] = '\0';
        if (strcmp(tag, "MENU") != 0 && strcmp(tag, "BMPS") != 0) {
            continue;
        }
        if ((long)name_offset >= (long)name_bytes) {
            continue;
        }
        if ((long)data_offset + (long)member_size > size) {
            continue;
        }

        name = (const char *)(data + names_at + name_offset);
        length = strlen(name);
        if (length == 0 || length >= sizeof out[found].name) {
            continue;
        }
        memcpy(out[found].name, name, length + 1);
        out[found].offset = (long)data_offset;
        out[found].size   = (long)member_size;
        ++found;
    }
    return found;
}

/* obi.ini's screen size, set to what was just converted for.
 *
 * NOT A CONVENIENCE. The engine's menu blitter clips against the canvas rather than against the
 * screen buffer it draws into, so running the game SMALLER than the artwork writes past the end of
 * that buffer. Matching the two is what avoids it.
 *
 * Every other line is passed through exactly as found, because that file holds key bindings and
 * calibration somebody may have spent time on. */
static void set_game_resolution(const char *game, int width, int height)
{
    char path[MAX_PATH];
    unsigned char *data;
    long size;
    FILE *out;
    long i, line_start;
    int wrote_width = 0, wrote_height = 0;

    _snprintf(path, sizeof path - 1, "%s\\obi.ini", game);
    path[sizeof path - 1] = '\0';

    data = read_whole_file(path, &size);
    if (data == NULL) {
        out = fopen(path, "wb");
        if (out != NULL) {
            fprintf(out, "screen_width=%d\r\nscreen_height=%d\r\n", width, height);
            fclose(out);
            note("  created obi.ini with screen_width=%d, screen_height=%d", width, height);
        }
        return;
    }

    out = fopen(path, "wb");
    if (out == NULL) {
        free(data);
        note("  obi.ini could not be written; set screen_width=%d and screen_height=%d by hand",
             width, height);
        return;
    }

    line_start = 0;
    for (i = 0; i <= size; ++i) {
        if (i == size || data[i] == '\n') {
            long length = i - line_start;
            const char *line = (const char *)(data + line_start);

            if (length > 0 || i < size) {
                if (_strnicmp(line, "screen_width", 12) == 0 && strchr(line, '=') != NULL &&
                    (size_t)length < 64) {
                    fprintf(out, "screen_width=%d\r\n", width);
                    wrote_width = 1;
                } else if (_strnicmp(line, "screen_height", 13) == 0 &&
                           strchr(line, '=') != NULL && (size_t)length < 64) {
                    fprintf(out, "screen_height=%d\r\n", height);
                    wrote_height = 1;
                } else if (length > 0) {
                    fwrite(line, 1, (size_t)length, out);
                    if (i < size) {
                        fputc('\n', out);
                    }
                } else if (i < size) {
                    fputc('\n', out);
                }
            }
            line_start = i + 1;
        }
    }
    if (!wrote_width)  { fprintf(out, "screen_width=%d\r\n", width); }
    if (!wrote_height) { fprintf(out, "screen_height=%d\r\n", height); }

    fclose(out);
    free(data);
    note("  obi.ini set to screen_width=%d, screen_height=%d", width, height);
}

static int convert_menu(const char *game, const char *output, int screen_width, int screen_height,
                        int uniform, int keep_resolution)
{
    static const char *archives[2] = { "big.lab", "LOCALIZE.LAB" };
    lab_member_t members[512];
    char manifest_path[MAX_PATH];
    FILE *manifest;
    double ratio_x, ratio_y;
    int archive;
    int written = 0, skipped = 0, total = 0;
    struct { unsigned char *data; long size; int count; int first; } loaded[2];

    if (screen_width < CANVAS_WIDTH || screen_height < CANVAS_HEIGHT) {
        fail("%dx%d is smaller than the menus' own %dx%d; there is nothing to upscale to.",
             screen_width, screen_height, CANVAS_WIDTH, CANVAS_HEIGHT);
        return 2;
    }

    if (uniform) {
        double fit = (double)screen_width / CANVAS_WIDTH;
        double tall = (double)screen_height / CANVAS_HEIGHT;
        ratio_x = ratio_y = (fit < tall) ? fit : tall;
    } else {
        ratio_x = (double)screen_width / CANVAS_WIDTH;
        ratio_y = (double)screen_height / CANVAS_HEIGHT;
    }
    if (ratio_x > MAX_CANVAS_RATIO) { ratio_x = MAX_CANVAS_RATIO; }
    if (ratio_y > MAX_CANVAS_RATIO) { ratio_y = MAX_CANVAS_RATIO; }

    note("Converting for %dx%d (%s): %.3f across, %.3f down, canvas %dx%d",
         screen_width, screen_height, uniform ? "uniform" : "stretch", ratio_x, ratio_y,
         (int)(CANVAS_WIDTH * ratio_x + 0.5), (int)(CANVAS_HEIGHT * ratio_y + 0.5));

    CreateDirectoryA(output, NULL);

    /* Both archives are read before anything is written, so the count can be announced first: a
     * caller driving this needs the total to size a progress bar and cannot get one afterwards. */
    for (archive = 0; archive < 2; ++archive) {
        char path[MAX_PATH];
        int count;

        loaded[archive].data = NULL;
        loaded[archive].count = 0;
        loaded[archive].first = total;

        _snprintf(path, sizeof path - 1, "%s\\%s", game, archives[archive]);
        path[sizeof path - 1] = '\0';
        loaded[archive].data = read_whole_file(path, &loaded[archive].size);
        if (loaded[archive].data == NULL) {
            note("  %s not present, skipped", archives[archive]);
            continue;
        }
        count = read_lab_directory(loaded[archive].data, loaded[archive].size,
                                   members + total, (int)(sizeof members / sizeof members[0]) - total);
        if (count < 0) {
            note("  %s is not a LABN archive, skipped", archives[archive]);
            free(loaded[archive].data);
            loaded[archive].data = NULL;
            continue;
        }
        loaded[archive].count = count;
        total += count;
        note("  %s - %d pictures", archives[archive], count);
    }

    if (quiet) {
        emit("TOTAL %d", total);
    }

    _snprintf(manifest_path, sizeof manifest_path - 1, "%s\\%s", output, MANIFEST_NAME);
    manifest_path[sizeof manifest_path - 1] = '\0';
    manifest = fopen(manifest_path, "wb");
    if (manifest != NULL) {
        fprintf(manifest, "# OpenPhantom menu artwork, made from your own game by "
                          "openphantom_convert.\r\n");
        fprintf(manifest, "# %dx%d %s\r\n", screen_width, screen_height,
                uniform ? "uniform" : "stretch");
        fprintf(manifest, "# Delete this folder to undo it.\r\n");
    }

    for (archive = 0; archive < 2; ++archive) {
        int i;
        if (loaded[archive].data == NULL) {
            continue;
        }
        for (i = 0; i < loaded[archive].count; ++i) {
            lab_member_t *member = &members[loaded[archive].first + i];
            unsigned char *bigger;
            long bigger_size;
            int out_w = 0, out_h = 0;
            char destination[MAX_PATH];
            FILE *file;

            bigger = scale_bitmap(loaded[archive].data + member->offset, member->size,
                                  ratio_x, ratio_y, &bigger_size, &out_w, &out_h);
            if (bigger == NULL) {
                ++skipped;
                if (quiet) { emit("SKIP %s", member->name); }
                continue;
            }

            _snprintf(destination, sizeof destination - 1, "%s\\%s", output, member->name);
            destination[sizeof destination - 1] = '\0';
            file = fopen(destination, "wb");
            if (file == NULL || fwrite(bigger, 1, (size_t)bigger_size, file) != (size_t)bigger_size) {
                if (file != NULL) { fclose(file); }
                free(bigger);
                ++skipped;
                if (quiet) { emit("SKIP %s", member->name); }
                continue;
            }
            fclose(file);
            free(bigger);

            ++written;
            if (manifest != NULL) {
                fprintf(manifest, "%s\r\n", member->name);
            }
            if (quiet) {
                emit("OK %s %dx%d %ld", member->name, out_w, out_h, bigger_size);
            }
        }
        free(loaded[archive].data);
    }

    if (manifest != NULL) {
        fclose(manifest);
    }

    /* After the pictures, so a run that failed half way does not leave the game pointed at a size
     * the artwork was never finished for. */
    if (!keep_resolution && written > 0) {
        set_game_resolution(game, screen_width, screen_height);
    }

    if (quiet) {
        emit("DONE written=%d skipped=%d", written, skipped);
    } else {
        note("");
        note("%d pictures written, %d skipped", written, skipped);
    }
    return (written > 0) ? 0 : 1;
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
 * A film whose header cannot be read keeps the requested height, which is what happened before the
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

    _snprintf(source, sizeof source - 1, "%s\\GAMEDATA\\MOVIE", game);
    source[sizeof source - 1] = '\0';
    _snprintf(pattern, sizeof pattern - 1, "%s\\*.bik", source);
    pattern[sizeof pattern - 1] = '\0';

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
        _snprintf(base, sizeof base - 1, "%s", found.cFileName);
        base[sizeof base - 1] = '\0';
        dot = strrchr(base, '.');
        if (dot != NULL) { *dot = '\0'; }
        _strlwr(base);

        _snprintf(bik,   sizeof bik - 1,   "%s\\%s", source, found.cFileName);
        _snprintf(final, sizeof final - 1, "%s\\%s.mp4", output, base);
        _snprintf(temp,  sizeof temp - 1,  "%s\\%s.converting.mp4", output, base);
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
            _snprintf(filters, sizeof filters - 1, "scale=-2:%d:flags=lanczos", this_height);
        } else {
            /* Ten of the eleven films have an odd height, which H.264 cannot encode. Cropping drops
             * at most one row and one column and leaves every remaining pixel as it was decoded,
             * where scaling would resample the whole frame and padding would bake in a black row. */
            _snprintf(filters, sizeof filters - 1, "crop=trunc(iw/2)*2:trunc(ih/2)*2");
        }
        filters[sizeof filters - 1] = '\0';

        _snprintf(command, sizeof command - 1,
                  "\"%s\" -y -i \"%s\" -vf %s -c:v libx264 -preset slow -crf 18 "
                  "-pix_fmt yuv420p -profile:v high -c:a aac -b:a 192k -movflags +faststart%s "
                  "\"%s\"",
                  ffmpeg, bik, filters, quiet ? " -loglevel quiet" : "", temp);
        command[sizeof command - 1] = '\0';

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
        "openphantom_convert - makes bigger copies of the pictures and films in your own game.\n"
        "\n"
        "  openphantom_convert menu   --game DIR --width W --height H [--uniform]\n"
        "                             [--output DIR] [--keep-resolution] [--quiet]\n"
        "  openphantom_convert movies --game DIR [--height LINES] [--ffmpeg PATH]\n"
        "                             [--output DIR] [--force] [--quiet]\n"
        "\n"
        "Reads your own game's archives and writes beside them. Nothing is downloaded and no\n"
        "game file is modified, apart from the screen size in obi.ini after a menu conversion,\n"
        "which --keep-resolution declines.\n", stderr);
}

int main(int argc, char **argv)
{
    const char *game = NULL, *output = NULL, *ffmpeg = NULL;
    char output_buffer[MAX_PATH], ffmpeg_buffer[MAX_PATH];
    int width = 0, height = 0, uniform = 0, keep_resolution = 0, force = 0;
    int i, movies;

    if (argc < 2) {
        usage();
        return 2;
    }
    movies = (strcmp(argv[1], "movies") == 0);
    if (!movies && strcmp(argv[1], "menu") != 0) {
        usage();
        return 2;
    }

    for (i = 2; i < argc; ++i) {
        if (strcmp(argv[i], "--game") == 0 && i + 1 < argc)          { game = argv[++i]; }
        else if (strcmp(argv[i], "--output") == 0 && i + 1 < argc)   { output = argv[++i]; }
        else if (strcmp(argv[i], "--ffmpeg") == 0 && i + 1 < argc)   { ffmpeg = argv[++i]; }
        else if (strcmp(argv[i], "--width") == 0 && i + 1 < argc)    { width = atoi(argv[++i]); }
        else if (strcmp(argv[i], "--height") == 0 && i + 1 < argc)   { height = atoi(argv[++i]); }
        else if (strcmp(argv[i], "--uniform") == 0)                  { uniform = 1; }
        else if (strcmp(argv[i], "--keep-resolution") == 0)          { keep_resolution = 1; }
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
        _snprintf(probe, sizeof probe - 1, "%s\\WMAIN.EXE", game);
        probe[sizeof probe - 1] = '\0';
        if (GetFileAttributesA(probe) == INVALID_FILE_ATTRIBUTES) {
            fail("there is no WMAIN.EXE in %s, so that is not the game folder.", game);
            return 2;
        }
    }

    if (output == NULL) {
        _snprintf(output_buffer, sizeof output_buffer - 1, "%s\\%s", game,
                  movies ? "movies_hd" : "menu_hd");
        output_buffer[sizeof output_buffer - 1] = '\0';
        output = output_buffer;
    }

    if (movies) {
        if (ffmpeg == NULL) {
            _snprintf(ffmpeg_buffer, sizeof ffmpeg_buffer - 1, "%s\\mods\\fmv\\ffmpeg.exe", game);
            ffmpeg_buffer[sizeof ffmpeg_buffer - 1] = '\0';
            ffmpeg = ffmpeg_buffer;
        }
        if (GetFileAttributesA(ffmpeg) == INVALID_FILE_ATTRIBUTES) {
            fail("no ffmpeg at %s. Pass --ffmpeg with its path.", ffmpeg);
            return 2;
        }
        return convert_movies(game, output, ffmpeg, height, force);
    }

    if (width <= 0 || height <= 0) {
        fail("menu conversion needs --width and --height.");
        return 2;
    }
    return convert_menu(game, output, width, height, uniform, keep_resolution);
}
