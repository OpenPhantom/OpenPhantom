/* Reading an InstallShield 3 ".Z" archive. */
#include "is3_archive.h"

#include "blast.h"

#include <stdlib.h>
#include <string.h>

/* BIG.Z on the retail disc is 711 MB, so every seek into it has to be 64-bit. The two spellings
 * below are the same function under different names; naming them here keeps the call sites
 * readable and stops the non-MSVC branch of the build from being a fallback that was described
 * but never implemented. */
#if defined(_MSC_VER) || defined(__MINGW32__)
#  define is3_seek64(f, off, whence) _fseeki64((f), (off), (whence))
#  define is3_tell64(f)              _ftelli64(f)
#else
#  define is3_seek64(f, off, whence) fseeko((f), (off), (whence))
#  define is3_tell64(f)              ftello(f)
#endif

#define IS3_SIGNATURE       0x8C655D13u
#define IS3_HEADER_SIZE     255u   /* and therefore also the first member's data offset */
#define IS3_OFF_COUNT       0x0Cu
#define IS3_OFF_TOTAL_SIZE  0x12u
#define IS3_OFF_DIR_OFFSET  0x33u
#define IS3_OFF_DIR_LENGTH  0x37u

/* Directory field positions, relative to the record's name-length byte. */
#define REC_UNCOMPRESSED   (-26)
#define REC_COMPRESSED     (-22)
#define REC_DATA_OFFSET    (-18)
#define REC_RECORD_LENGTH  (-6)
#define REC_FIELD_BYTES     41   /* fields ahead of the name-length byte */
#define REC_TRAILER_BYTES    2   /* the name-length byte itself and the name's terminator */

/* The directory offset in the header does not land on the first record's first byte, so the
 * opening record is found by a short bounded scan. Anything past this is a malformed archive
 * rather than an unusual one. */
#define REC_MAX_SKEW 64

static uint32_t read_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t read_u16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static int ascii_lower(int c)
{
    return (c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c;
}

/* A record is accepted only when its own length agrees with its name length and the name is
 * printable and terminated. Both together are what make the scan below trustworthy rather than a
 * guess that happens to work on one file. */
static bool record_is_plausible(const uint8_t *dir, size_t dir_length, size_t name_len_pos)
{
    uint8_t  name_length;
    uint32_t record_length;
    size_t   i;

    if (name_len_pos < (size_t)REC_FIELD_BYTES || name_len_pos >= dir_length) {
        return false;
    }
    name_length = dir[name_len_pos];
    if (name_length == 0 || name_length > IS3_MAX_NAME) {
        return false;
    }
    if (name_len_pos + 1u + name_length >= dir_length) {
        return false;
    }
    if (dir[name_len_pos + 1u + name_length] != 0) {
        return false;
    }
    for (i = 0; i < name_length; ++i) {
        uint8_t c = dir[name_len_pos + 1u + i];
        if (c < 0x20 || c > 0x7E) {
            return false;
        }
    }

    record_length = read_u32(dir + name_len_pos + REC_RECORD_LENGTH);
    return record_length == (uint32_t)(REC_FIELD_BYTES + REC_TRAILER_BYTES + name_length);
}

static is3_result_t parse_directory(
    is3_archive_t *archive,
    const uint8_t *dir,
    size_t         dir_length,
    size_t         member_count)
{
    size_t name_len_pos = 0;
    size_t skew;
    size_t i;

    for (skew = 0; skew < REC_MAX_SKEW; ++skew) {
        if (record_is_plausible(dir, dir_length, (size_t)REC_FIELD_BYTES + skew)) {
            name_len_pos = (size_t)REC_FIELD_BYTES + skew;
            break;
        }
    }
    if (skew == REC_MAX_SKEW) {
        return IS3_ERR_DIRECTORY;
    }

    for (i = 0; i < member_count; ++i) {
        is3_member_t *member = &archive->members[i];
        uint8_t       name_length;
        uint32_t      record_length;

        if (!record_is_plausible(dir, dir_length, name_len_pos)) {
            return IS3_ERR_DIRECTORY;
        }
        name_length = dir[name_len_pos];

        memcpy(member->name, dir + name_len_pos + 1u, name_length);
        member->name[name_length] = '\0';
        member->uncompressed_size = read_u32(dir + name_len_pos + REC_UNCOMPRESSED);
        member->compressed_size   = read_u32(dir + name_len_pos + REC_COMPRESSED);
        member->offset            = read_u32(dir + name_len_pos + REC_DATA_OFFSET);

        if ((int64_t)member->offset < (int64_t)IS3_HEADER_SIZE ||
            (int64_t)member->offset + (int64_t)member->compressed_size > archive->file_size) {
            return IS3_ERR_RANGE;
        }

        record_length = read_u32(dir + name_len_pos + REC_RECORD_LENGTH);
        name_len_pos += record_length;
    }

    archive->member_count = member_count;
    return IS3_OK;
}

is3_result_t is3_open(const char *path, is3_archive_t *archive)
{
    uint8_t  header[IS3_HEADER_SIZE];
    uint8_t *dir;
    uint32_t dir_offset;
    uint32_t dir_length;
    uint16_t member_count;
    is3_result_t result;

    memset(archive, 0, sizeof(*archive));

    archive->file = fopen(path, "rb");
    if (archive->file == NULL) {
        return IS3_ERR_OPEN;
    }
    if (is3_seek64(archive->file, 0, SEEK_END) != 0) {
        return IS3_ERR_READ;
    }
    archive->file_size = is3_tell64(archive->file);
    if (is3_seek64(archive->file, 0, SEEK_SET) != 0) {
        return IS3_ERR_READ;
    }
    if (archive->file_size < (int64_t)IS3_HEADER_SIZE) {
        return IS3_ERR_NOT_AN_ARCHIVE;
    }
    if (fread(header, 1, IS3_HEADER_SIZE, archive->file) != IS3_HEADER_SIZE) {
        return IS3_ERR_READ;
    }
    if (read_u32(header) != IS3_SIGNATURE) {
        return IS3_ERR_NOT_AN_ARCHIVE;
    }

    member_count = read_u16(header + IS3_OFF_COUNT);
    dir_offset   = read_u32(header + IS3_OFF_DIR_OFFSET);
    dir_length   = read_u32(header + IS3_OFF_DIR_LENGTH);

    if (member_count == 0 || member_count > IS3_MAX_MEMBERS) {
        return IS3_ERR_DIRECTORY;
    }
    /* The directory is a few hundred bytes even for a large archive; a huge value here means the
     * header was misread, and allocating on it would be the wrong response. */
    if (dir_length == 0 || dir_length > (1u << 20)) {
        return IS3_ERR_DIRECTORY;
    }
    if ((int64_t)dir_offset + (int64_t)dir_length > archive->file_size) {
        return IS3_ERR_DIRECTORY;
    }

    /* The record scan reads up to REC_FIELD_BYTES ahead of the header's directory offset, so the
     * buffer starts that far back. */
    if (dir_offset < (uint32_t)REC_FIELD_BYTES) {
        return IS3_ERR_DIRECTORY;
    }
    archive->members = (is3_member_t *)calloc(member_count, sizeof(is3_member_t));
    if (archive->members == NULL) {
        return IS3_ERR_MEMORY;
    }

    dir = (uint8_t *)malloc(dir_length + (size_t)REC_FIELD_BYTES);
    if (dir == NULL) {
        return IS3_ERR_MEMORY;
    }
    if (is3_seek64(archive->file, (int64_t)dir_offset - REC_FIELD_BYTES, SEEK_SET) != 0 ||
        fread(dir, 1, dir_length + (size_t)REC_FIELD_BYTES, archive->file)
            != dir_length + (size_t)REC_FIELD_BYTES) {
        free(dir);
        return IS3_ERR_READ;
    }

    result = parse_directory(archive, dir, dir_length + (size_t)REC_FIELD_BYTES, member_count);
    free(dir);
    return result;
}

void is3_close(is3_archive_t *archive)
{
    if (archive->file != NULL) {
        fclose(archive->file);
        archive->file = NULL;
    }
    free(archive->members);
    archive->members = NULL;
    archive->member_count = 0;
}

const is3_member_t *is3_find_member(const is3_archive_t *archive, const char *name)
{
    size_t i;

    for (i = 0; i < archive->member_count; ++i) {
        const char *a = archive->members[i].name;
        const char *b = name;

        while (*a != '\0' && *b != '\0' && ascii_lower((unsigned char)*a) == ascii_lower((unsigned char)*b)) {
            a++;
            b++;
        }
        if (*a == '\0' && *b == '\0') {
            return &archive->members[i];
        }
    }
    return NULL;
}

typedef struct extract_sink {
    FILE   *file;
    int64_t written;
} extract_sink_t;

static bool write_chunk(void *user, const uint8_t *data, size_t size)
{
    extract_sink_t *sink = (extract_sink_t *)user;

    if (fwrite(data, 1, size, sink->file) != size) {
        return false;
    }
    sink->written += (int64_t)size;
    return true;
}

is3_result_t is3_extract(
    const is3_archive_t *archive,
    const is3_member_t  *member,
    const char          *output_path)
{
    uint8_t       *compressed;
    extract_sink_t sink;
    blast_result_t blast_status;
    is3_result_t   result = IS3_OK;

    compressed = (uint8_t *)malloc(member->compressed_size);
    if (compressed == NULL) {
        return IS3_ERR_MEMORY;
    }
    if (is3_seek64(archive->file, (int64_t)member->offset, SEEK_SET) != 0 ||
        fread(compressed, 1, member->compressed_size, archive->file) != member->compressed_size) {
        free(compressed);
        return IS3_ERR_READ;
    }

    sink.file = fopen(output_path, "wb");
    sink.written = 0;
    if (sink.file == NULL) {
        free(compressed);
        return IS3_ERR_WRITE;
    }

    blast_status = blast_decompress(compressed, member->compressed_size, write_chunk, &sink);
    free(compressed);

    if (blast_status == BLAST_ERR_WRITE) {
        result = IS3_ERR_WRITE;
    } else if (blast_status != BLAST_OK) {
        result = IS3_ERR_DECOMPRESS;
    } else if (sink.written != (int64_t)member->uncompressed_size) {
        result = IS3_ERR_SIZE_MISMATCH;
    }

    if (fclose(sink.file) != 0 && result == IS3_OK) {
        result = IS3_ERR_WRITE;
    }

    /* A file that failed verification must not be left where something else may pick it up and
     * treat it as finished. */
    if (result != IS3_OK) {
        remove(output_path);
    }
    return result;
}

const char *is3_result_text(is3_result_t result)
{
    switch (result) {
    case IS3_OK:                    return "ok";
    case IS3_ERR_OPEN:              return "the archive could not be opened";
    case IS3_ERR_READ:              return "the archive could not be read to the end of a member";
    case IS3_ERR_NOT_AN_ARCHIVE:    return "this file is not an InstallShield 3 archive";
    case IS3_ERR_DIRECTORY:         return "the member directory does not parse";
    case IS3_ERR_MEMBER_NOT_FOUND:  return "the archive does not contain that member";
    case IS3_ERR_RANGE:             return "a member claims data outside the archive";
    case IS3_ERR_MEMORY:            return "out of memory";
    case IS3_ERR_DECOMPRESS:        return "the compressed stream is damaged";
    case IS3_ERR_WRITE:             return "the output file could not be written";
    case IS3_ERR_SIZE_MISMATCH:     return "the extracted size is not the size the archive records";
    default:                        return "unknown error";
    }
}
