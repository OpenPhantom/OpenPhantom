/* PKWARE Data Compression Library "implode" decompressor.
 *
 * This is the compression inside an InstallShield 3 archive. It is a 1990 format and it is fully
 * documented; nothing here was guessed. A stream is two header bytes, the literal coding mode and
 * the dictionary size, followed by a bit stream of literals and length/distance pairs.
 *
 * The dictionary is at most 4096 bytes, so decompression needs a 4096-byte window and nothing
 * else: output is handed to the caller in chunks as it is produced, and a 121 MB member never
 * exists in memory at once.
 */
#ifndef BLAST_H
#define BLAST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum blast_result {
    BLAST_OK,
    BLAST_ERR_LITERAL_MODE,     /* first header byte was not 0 or 1 */
    BLAST_ERR_DICTIONARY_SIZE,  /* second header byte was not 4, 5 or 6 */
    BLAST_ERR_TRUNCATED,        /* the bit stream ended before the end code */
    BLAST_ERR_DISTANCE,         /* a copy reached back before the start of the output */
    BLAST_ERR_WRITE             /* the sink refused a chunk */
} blast_result_t;

/* Called with each produced chunk, in order. Return false to abort the decompression; that
 * surfaces as BLAST_ERR_WRITE. */
typedef bool (*blast_write_fn)(void *user, const uint8_t *data, size_t size);

blast_result_t blast_decompress(
    const uint8_t *input,
    size_t         input_size,
    blast_write_fn write,
    void          *user);

/* A human-readable reason, for logging. Never NULL. */
const char *blast_result_text(blast_result_t result);

#endif /* BLAST_H */
