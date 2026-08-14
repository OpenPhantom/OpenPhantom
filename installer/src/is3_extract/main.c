/* Extract one member from an InstallShield 3 ".Z" archive.
 *
 * This exists because the game's disc ships GAMEDATA\GOBS\BIG.Z and the game needs big.lab, and
 * the only thing that ever turned one into the other was the disc's own 16-bit installer, which
 * no longer runs. Bundling the expanded archive instead would mean shipping 121 MB of somebody
 * else's game data in an installer, which is exactly what this avoids.
 *
 * The exit code is the whole interface for the caller. It is never 0 unless a member was written
 * AND its length matched what the archive recorded, so "the file exists" and "the file is right"
 * are the same statement.
 */
#include "is3_archive.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* Exit codes. Stable: the installer maps them to messages, so their meanings must not be
 * renumbered. */
#define EXIT_OK              0
#define EXIT_USAGE           1
#define EXIT_OPEN            2
#define EXIT_NOT_AN_ARCHIVE  3
#define EXIT_DIRECTORY       4
#define EXIT_MEMBER_MISSING  5
#define EXIT_DECOMPRESS      6
#define EXIT_WRITE           7
#define EXIT_VERIFY          8
#define EXIT_OTHER           9

static FILE *log_file = NULL;

static void report(const char *format, ...)
{
    va_list args;

    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputc('\n', stderr);

    if (log_file != NULL) {
        va_start(args, format);
        vfprintf(log_file, format, args);
        va_end(args);
        fputc('\n', log_file);
        fflush(log_file);
    }
}

static int exit_code_for(is3_result_t result)
{
    switch (result) {
    case IS3_OK:                   return EXIT_OK;
    case IS3_ERR_OPEN:             return EXIT_OPEN;
    case IS3_ERR_NOT_AN_ARCHIVE:   return EXIT_NOT_AN_ARCHIVE;
    case IS3_ERR_DIRECTORY:        return EXIT_DIRECTORY;
    case IS3_ERR_RANGE:            return EXIT_DIRECTORY;
    case IS3_ERR_MEMBER_NOT_FOUND: return EXIT_MEMBER_MISSING;
    case IS3_ERR_DECOMPRESS:       return EXIT_DECOMPRESS;
    case IS3_ERR_WRITE:            return EXIT_WRITE;
    case IS3_ERR_SIZE_MISMATCH:    return EXIT_VERIFY;
    default:                       return EXIT_OTHER;
    }
}

static void print_usage(void)
{
    fprintf(stderr,
            "is3_extract: read an InstallShield 3 .Z archive\n"
            "\n"
            "  is3_extract --list <archive>\n"
            "  is3_extract <archive> <member> <output-file> [--log <file>]\n"
            "\n"
            "Exit codes: 0 ok, 1 usage, 2 cannot open, 3 not an archive, 4 bad directory,\n"
            "            5 no such member, 6 damaged stream, 7 cannot write, 8 size mismatch.\n");
}

static int list_members(const char *archive_path)
{
    is3_archive_t archive;
    is3_result_t  result;
    size_t        i;

    result = is3_open(archive_path, &archive);
    if (result != IS3_OK) {
        report("%s: %s", archive_path, is3_result_text(result));
        is3_close(&archive);
        return exit_code_for(result);
    }

    printf("%zu member(s) in %s\n", archive.member_count, archive_path);
    for (i = 0; i < archive.member_count; ++i) {
        const is3_member_t *m = &archive.members[i];
        printf("  %-24s offset %10u  compressed %10u  expands to %10u\n",
               m->name, m->offset, m->compressed_size, m->uncompressed_size);
    }
    is3_close(&archive);
    return EXIT_OK;
}

static int extract_member(const char *archive_path, const char *member_name, const char *output_path)
{
    is3_archive_t       archive;
    const is3_member_t *member;
    is3_result_t        result;

    result = is3_open(archive_path, &archive);
    if (result != IS3_OK) {
        report("%s: %s", archive_path, is3_result_text(result));
        is3_close(&archive);
        return exit_code_for(result);
    }

    member = is3_find_member(&archive, member_name);
    if (member == NULL) {
        report("%s: no member called \"%s\" (the archive holds %zu)",
               archive_path, member_name, archive.member_count);
        is3_close(&archive);
        return EXIT_MEMBER_MISSING;
    }

    report("extracting %s: %u compressed bytes at offset %u, expecting %u bytes out",
           member->name, member->compressed_size, member->offset, member->uncompressed_size);

    result = is3_extract(&archive, member, output_path);
    is3_close(&archive);

    if (result != IS3_OK) {
        report("%s: %s", output_path, is3_result_text(result));
        return exit_code_for(result);
    }

    report("wrote %s, %u bytes, length verified against the archive directory",
           output_path, member->uncompressed_size);
    return EXIT_OK;
}

int main(int argc, char **argv)
{
    int i;
    int status;

    /* The log path is optional and is scanned for first, so that a failure in argument handling
     * itself still reaches the file the caller nominated. */
    for (i = 1; i + 1 < argc; ++i) {
        if (strcmp(argv[i], "--log") == 0) {
            log_file = fopen(argv[i + 1], "a");
            break;
        }
    }

    if (argc == 3 && strcmp(argv[1], "--list") == 0) {
        status = list_members(argv[2]);
    } else if (argc >= 4 && strcmp(argv[1], "--list") != 0) {
        status = extract_member(argv[1], argv[2], argv[3]);
    } else {
        print_usage();
        status = EXIT_USAGE;
    }

    if (log_file != NULL) {
        fclose(log_file);
    }
    return status;
}
