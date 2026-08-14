/* Reading an InstallShield 3 ".Z" archive.
 *
 * The format, as measured on the retail disc rather than taken from any specification:
 *
 *   offset 0x00  u32  0x8C655D13, the signature
 *   offset 0x0C  u16  number of members
 *   offset 0x12  u32  total archive size, which is also where the directory ends
 *   offset 0x33  u32  file offset of the member directory
 *   offset 0x37  u32  length of that directory
 *   offset 0xFF       the first member's data begins here; the header is 255 bytes, not 256
 *
 * Every member is stored back to back from 0xFF, each compressed with PKWARE DCL implode.
 *
 * A directory record is self-describing, which is what makes walking it safe: six bytes before the
 * name-length byte sits the record's own length, and that length added to the current record's
 * name-length position lands exactly on the next one. The fields are addressed relative to the
 * name-length byte because that is the one position in a record that can be found by inspection.
 */
#ifndef IS3_ARCHIVE_H
#define IS3_ARCHIVE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define IS3_MAX_NAME    255
#define IS3_MAX_MEMBERS 4096

typedef enum is3_result {
    IS3_OK,
    IS3_ERR_OPEN,             /* the archive could not be opened */
    IS3_ERR_READ,             /* a read stopped short of what the directory promised */
    IS3_ERR_NOT_AN_ARCHIVE,   /* the signature is not 0x8C655D13 */
    IS3_ERR_DIRECTORY,        /* the directory is missing, oversized, or does not parse */
    IS3_ERR_MEMBER_NOT_FOUND,
    IS3_ERR_RANGE,            /* a member claims data outside the file */
    IS3_ERR_MEMORY,
    IS3_ERR_DECOMPRESS,
    IS3_ERR_WRITE,
    IS3_ERR_SIZE_MISMATCH     /* decompressed length is not what the directory recorded */
} is3_result_t;

typedef struct is3_member {
    char     name[IS3_MAX_NAME + 1];
    uint32_t offset;             /* absolute file offset of the compressed data */
    uint32_t compressed_size;
    uint32_t uncompressed_size;
} is3_member_t;

/* The member table is allocated rather than inlined: one entry is about 270 bytes, and a fixed
 * array big enough for the plausibility limit would be a megabyte of automatic storage in every
 * caller, which is a stack overflow on the default thread stack rather than a waste of space. */
typedef struct is3_archive {
    FILE         *file;
    int64_t       file_size;
    size_t        member_count;
    is3_member_t *members;
} is3_archive_t;

is3_result_t is3_open(const char *path, is3_archive_t *archive);
void         is3_close(is3_archive_t *archive);

/* Case-insensitive, because the directory records a member as "big.lab" while the disc and the
 * installed folder both spell it in capitals. Returns NULL when there is no such member. */
const is3_member_t *is3_find_member(const is3_archive_t *archive, const char *name);

/* Decompresses one member to a file. The size the directory recorded is verified against what
 * actually came out, and a mismatch fails rather than leaving a plausible-looking file behind. */
is3_result_t is3_extract(
    const is3_archive_t *archive,
    const is3_member_t  *member,
    const char          *output_path);

const char *is3_result_text(is3_result_t result);

#endif /* IS3_ARCHIVE_H */
