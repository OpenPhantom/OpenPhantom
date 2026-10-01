/* A savegame cut into notes and put back together, with no socket and no file in the process.
 *
 * What is pinned: the digest against its two known answers, so both ends of the wire agree on a
 * file's name; the chunk's fields against each other, because a stranger writes them; the
 * assembly's completion, which is the digest and not the count; and the one repair the layer
 * makes, throwing away a file that does not hash to its name and asking for it again.
 */
#include "unittest.h"

#include "mp_savefile.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static mp_savefile_assembly_t assembly;   /* 128 KiB, which is why it is not on the stack */

static void fill(uint8_t *file, uint32_t total, uint32_t seed)
{
    uint32_t i;

    for (i = 0; i < total; ++i) {
        seed = seed * 1103515245u + 12345u;
        file[i] = (uint8_t)(seed >> 16);
    }
}

static void check_the_digest(void)
{
    ut_section("the digest is FNV-1a, which both ends have to agree on");
    ut_check(mp_savefile_digest("", 0u) == 2166136261u, "the empty input is the offset basis");
    ut_check(mp_savefile_digest("a", 1u) == 0xE40C292Cu, "and 'a' is the published answer");
    ut_check(mp_savefile_digest("ab", 2u) != mp_savefile_digest("ba", 2u),
             "the order of the bytes is part of the name");
}

static void check_the_chunk_count(void)
{
    ut_section("how many chunks a file is");
    ut_check(mp_savefile_chunk_count(0u) == 0u, "an empty file is no file");
    ut_check(mp_savefile_chunk_count(1u) == 1u, "one byte is one chunk");
    ut_check(mp_savefile_chunk_count(MP_SAVEFILE_CHUNK_PAYLOAD) == 1u, "a full payload is one");
    ut_check(mp_savefile_chunk_count(MP_SAVEFILE_CHUNK_PAYLOAD + 1u) == 2u, "one more is two");
    ut_check(mp_savefile_chunk_count(78992u) == 78u, "a save of 78992 bytes is 78");
    ut_check(mp_savefile_chunk_count(MP_SAVEFILE_MAX_BYTES) == MP_SAVEFILE_MAX_CHUNKS,
             "the ceiling fills the bitmap exactly");
    ut_check(mp_savefile_chunk_count(MP_SAVEFILE_MAX_BYTES + 1u) == 0u,
             "and one past the ceiling is refused");
}

static void check_the_chunk_on_the_wire(void)
{
    static uint8_t      file[3000];
    uint8_t             note[MP_SAVEFILE_CHUNK_BYTES];
    mp_savefile_chunk_t chunk;
    uint32_t            id;
    size_t              length;

    fill(file, sizeof file, 7u);
    id = mp_savefile_digest(file, sizeof file);

    ut_section("a chunk carries what a receiver needs to place it without any other chunk");
    length = mp_savefile_chunk_encode(id, file, sizeof file, 1u, note, sizeof note);
    ut_check(length == MP_SAVEFILE_CHUNK_BYTES, "a middle chunk is a full note");
    ut_check(mp_savefile_is_chunk(note, length), "and is recognised");
    ut_check(mp_savefile_chunk_decode(note, length, &chunk), "and decodes");
    ut_check(chunk.file_id == id && chunk.total == sizeof file && chunk.index == 1u &&
                 chunk.count == 3u && chunk.bytes == MP_SAVEFILE_CHUNK_PAYLOAD,
             "with every field as sent");
    ut_check(memcmp(chunk.payload, file + MP_SAVEFILE_CHUNK_PAYLOAD, chunk.bytes) == 0,
             "and the bytes are the file's second kilobyte");

    length = mp_savefile_chunk_encode(id, file, sizeof file, 2u, note, sizeof note);
    ut_check(length == MP_SAVEFILE_CHUNK_HEAD + (3000u - 2u * MP_SAVEFILE_CHUNK_PAYLOAD),
             "the last chunk carries the remainder and nothing else");
    ut_check(mp_savefile_chunk_decode(note, length, &chunk) && chunk.bytes == 952u,
             "952 bytes of it");

    ut_section("what the encoder refuses");
    ut_check(mp_savefile_chunk_encode(id, file, sizeof file, 3u, note, sizeof note) == 0u,
             "an index past the count");
    ut_check(mp_savefile_chunk_encode(id, file, 0u, 0u, note, sizeof note) == 0u,
             "an empty file");
    ut_check(mp_savefile_chunk_encode(id, file, sizeof file, 0u, note, 100u) == 0u,
             "a buffer too short for the chunk");

    ut_section("what the decoder refuses, because a stranger wrote the head");
    length = mp_savefile_chunk_encode(id, file, sizeof file, 0u, note, sizeof note);
    ut_check(mp_savefile_chunk_decode(note, length, &chunk), "a sound one exists to forge from");
    note[11] = 9u;   /* the count, low byte */
    ut_check(!mp_savefile_chunk_decode(note, length, &chunk),
             "a count that is not the one the total implies");
    note[11] = 3u;
    note[9] = 3u;    /* the index, low byte */
    ut_check(!mp_savefile_chunk_decode(note, length, &chunk), "an index past the count");
    note[9] = 0u;
    note[13] = 1u;   /* the byte count, low byte: 1025 */
    note[14] = 4u;
    ut_check(!mp_savefile_chunk_decode(note, length, &chunk),
             "a length that is not the one the index implies");
    note[13] = 0u;
    note[14] = 4u;
    ut_check(!mp_savefile_chunk_decode(note, length - 1u, &chunk),
             "a note shorter than its own byte count");
    ut_check(!mp_savefile_chunk_decode(note, length, NULL), "and nowhere to put it");
    ut_check(mp_savefile_chunk_decode(note, length, &chunk), "with everything put back it decodes");
}

/* The receiver asks by mask, below, and check_the_mask proves it. Of the cursor shaped request
 * only the recogniser is left. */

/* Sends every chunk of `file` into the assembly, in the order the indices say. */
static void deliver(const uint8_t *file, uint32_t total, uint32_t id, uint16_t first,
                    uint16_t last_exclusive)
{
    uint8_t             note[MP_SAVEFILE_CHUNK_BYTES];
    mp_savefile_chunk_t chunk;
    uint16_t            index;

    for (index = first; index < last_exclusive; ++index) {
        size_t length = mp_savefile_chunk_encode(id, file, total, index, note, sizeof note);

        if (length != 0u && mp_savefile_chunk_decode(note, length, &chunk)) {
            (void)mp_savefile_assembly_take(&assembly, &chunk);
        }
    }
}

static void check_the_assembly(void)
{
    static uint8_t      file[78992];   /* the size of the save the field run used */
    uint8_t             note[MP_SAVEFILE_CHUNK_BYTES];
    mp_savefile_chunk_t chunk;
    uint32_t            id;
    size_t              length;

    fill(file, sizeof file, 17u);
    id = mp_savefile_digest(file, sizeof file);

    ut_section("a file arrives in pieces and is whole when the digest says so");
    mp_savefile_assembly_reset(&assembly);
    ut_check(mp_savefile_assembly_open(&assembly, id, sizeof file), "the file is named");
    ut_check(mp_savefile_mask_first_missing(assembly.have, assembly.count) == 0u,
             "and nothing is in yet");
    ut_check(mp_savefile_assembly_percent(&assembly) == 0u, "nought per cent");
    deliver(file, sizeof file, id, 0u, 40u);
    ut_check(mp_savefile_mask_first_missing(assembly.have, assembly.count) == 40u,
             "after forty chunks the first missing is the forty first");
    ut_check(mp_savefile_assembly_percent(&assembly) == 40u * 100u / 78u, "fifty one per cent");
    ut_check(!assembly.complete, "and it is not complete");
    deliver(file, sizeof file, id, 40u, 78u);
    ut_check(assembly.complete, "with the rest it is");
    ut_check(mp_savefile_mask_first_missing(assembly.have, assembly.count) == 78u,
             "nothing is missing");
    ut_check(mp_savefile_assembly_percent(&assembly) == 100u, "a hundred per cent");
    ut_check(memcmp(assembly.bytes, file, sizeof file) == 0, "and the bytes are the file's");
    ut_check(assembly.chunks_taken == 78u && assembly.chunks_refused == 0u, "78 taken, 0 refused");

    ut_section("the same file named again throws nothing away");
    ut_check(mp_savefile_assembly_open(&assembly, id, sizeof file) && assembly.complete,
             "a repeat of the setup note leaves a complete file complete");

    ut_section("a repeat is not a refusal, and a chunk for another file is");
    length = mp_savefile_chunk_encode(id, file, sizeof file, 5u, note, sizeof note);
    ut_check(mp_savefile_chunk_decode(note, length, &chunk) &&
                 mp_savefile_assembly_take(&assembly, &chunk) && assembly.chunks_repeated == 1u,
             "a chunk into a complete file is a repeat, the tail of the stream that completed it");
    ut_check(assembly.complete && memcmp(assembly.bytes, file, sizeof file) == 0,
             "and it changes nothing");
    ut_check(mp_savefile_assembly_open(&assembly, id + 1u, sizeof file) && !assembly.complete &&
                 mp_savefile_mask_first_missing(assembly.have, assembly.count) == 0u,
             "a different name empties the assembly");
    ut_check(!mp_savefile_assembly_take(&assembly, &chunk), "and the old file's chunk is refused");
    ut_check(assembly.chunks_refused == 1u, "which is counted");

    ut_section("a file that does not hash to its name is thrown away and asked for again");
    mp_savefile_assembly_reset(&assembly);
    ut_check(mp_savefile_assembly_open(&assembly, id ^ 0x5A5A5A5Au, sizeof file),
             "a name that is not the file's");
    deliver(file, sizeof file, id ^ 0x5A5A5A5Au, 0u, 78u);
    ut_check(!assembly.complete, "every chunk in, and it is not complete");
    ut_check(assembly.files_rejected == 1u, "one file rejected");
    ut_check(assembly.open && mp_savefile_mask_first_missing(assembly.have, assembly.count) == 0u,
             "and the assembly is open again from the first chunk");

    ut_section("out of order is fine, twice is fine");
    mp_savefile_assembly_reset(&assembly);
    (void)mp_savefile_assembly_open(&assembly, id, sizeof file);
    deliver(file, sizeof file, id, 50u, 78u);
    deliver(file, sizeof file, id, 0u, 60u);
    ut_check(assembly.complete && assembly.chunks_repeated == 10u,
             "chunks 50 to 59 arrived twice and the file is whole");

    ut_section("what the assembly refuses to open");
    ut_check(!mp_savefile_assembly_open(&assembly, id, 0u), "an empty file");
    ut_check(!mp_savefile_assembly_open(&assembly, id, MP_SAVEFILE_MAX_BYTES + 1u),
             "and one past the ceiling");
    ut_check(assembly.complete, "and a refused open leaves the file that was there");
}

/* The mask, and the one thing it must refuse.
 *
 * The note is the receiver's whole state, so a stranger who writes one can make the sender skip
 * slices it never sent. Everything is therefore held to what the declared count implies: the
 * length has to be head plus exactly the bytes that many bits need, to the byte, or the note is
 * refused. A note that claimed more chunks than it carried bits for would be read off its own end.
 */
static void check_the_mask(void)
{
    uint8_t        mask[MP_SAVEFILE_MASK_BYTES_FOR(MP_SAVEFILE_MAX_CHUNKS)];
    uint8_t        note[MP_SAVEFILE_ACK_BYTES];
    const uint8_t *back = NULL;
    uint32_t       id = 0;
    uint16_t       count = 0;
    size_t         length;

    ut_section("the mask reads the same bits on both sides");

    memset(mask, 0, sizeof mask);
    ut_check(!mp_savefile_mask_has(mask, 0u) && !mp_savefile_mask_has(mask, 77u),
             "an empty mask holds nothing");
    ut_check(mp_savefile_mask_count(mask, 78u) == 0u, "and counts nothing");
    ut_check(mp_savefile_mask_first_missing(mask, 78u) == 0u,
             "and the lowest missing is the first");

    mp_savefile_mask_set(mask, 0u);
    mp_savefile_mask_set(mask, 7u);
    mp_savefile_mask_set(mask, 8u);
    mp_savefile_mask_set(mask, 77u);
    ut_check(mp_savefile_mask_has(mask, 0u) && mp_savefile_mask_has(mask, 7u) &&
             mp_savefile_mask_has(mask, 8u) && mp_savefile_mask_has(mask, 77u),
             "the four that were set are set, byte boundaries included");
    ut_check(!mp_savefile_mask_has(mask, 6u) && !mp_savefile_mask_has(mask, 9u),
             "and their neighbours are not");
    ut_check(mp_savefile_mask_count(mask, 78u) == 4u, "four of seventy eight");
    ut_check(mp_savefile_mask_first_missing(mask, 78u) == 1u, "the lowest missing is one");

    ut_section("the acknowledgement on the wire");

    length = mp_savefile_ack_encode(0xABCDEF01u, 78u, mask, note, sizeof note);
    ut_checkf(length == MP_SAVEFILE_ACK_HEAD + MP_SAVEFILE_MASK_BYTES_FOR(78u),
              "a mask of 78 chunks is head plus ten bytes, not %u", (unsigned)length);
    ut_check(mp_savefile_is_ack(note, length), "and it is recognised by its tag and length");
    ut_check(!mp_savefile_is_chunk(note, length) && !mp_savefile_is_request(note, length),
             "and by nothing else this module carries");
    ut_check(mp_savefile_ack_decode(note, length, &id, &count, &back) && id == 0xABCDEF01u &&
             count == 78u && back != NULL,
             "it decodes to the file and the count it was written with");
    ut_check(mp_savefile_mask_count(back, 78u) == 4u &&
             mp_savefile_mask_has(back, 77u) && !mp_savefile_mask_has(back, 76u),
             "and the bits that come back are the bits that went out");

    ut_section("what the acknowledgement refuses");

    ut_check(!mp_savefile_ack_decode(note, length - 1u, &id, &count, &back),
             "a note one byte short of what its count needs, which would be read off its end");
    ut_check(!mp_savefile_ack_decode(note, length + 1u, &id, &count, &back),
             "and one byte long, which means the count and the bytes disagree");
    ut_check(mp_savefile_ack_encode(1u, 0u, mask, note, sizeof note) == 0u,
             "a count of nothing is not a file");
    ut_check(mp_savefile_ack_encode(1u, MP_SAVEFILE_MAX_CHUNKS + 1u, mask, note, sizeof note) == 0u,
             "and a count past the ceiling is refused rather than trusted");
    ut_check(mp_savefile_ack_encode(1u, 78u, mask, note, 8u) == 0u,
             "a buffer too small refuses rather than writing a short mask "
             "that reads as a full one");
}

/* The lane's pace: whole slices out of thousandths that gather with wall time. */
static void check_the_pace(void)
{
    mp_savefile_pace_t pace;
    uint32_t           laid = 0;
    uint32_t           now  = 100000u;
    int                tick;

    ut_section("the lane's pace: slices a second, whatever the tick");
    memset(&pace, 0, sizeof pace);
    ut_check(mp_savefile_pace_fill(&pace, 1000u, 256u, 8u) == 8u, "a fresh bucket starts full");
    mp_savefile_pace_spend(&pace, 8u);
    ut_check(mp_savefile_pace_fill(&pace, 1000u, 256u, 8u) == 0u, "and is empty once spent");
    ut_check(mp_savefile_pace_fill(&pace, 1001u, 256u, 8u) == 0u,
             "a millisecond later a quarter of a slice is not a slice");
    ut_check(mp_savefile_pace_fill(&pace, 1004u, 256u, 8u) == 1u,
             "four milliseconds make one out of the thousandths that gathered");
    mp_savefile_pace_spend(&pace, 5u);
    ut_check(mp_savefile_pace_fill(&pace, 1004u, 256u, 8u) == 0u,
             "spending more than it holds leaves it empty, not owing");
    ut_check(mp_savefile_pace_fill(&pace, 61000u, 256u, 8u) == 8u,
             "a minute idle fills it only to the brim");

    memset(&pace, 0, sizeof pace);
    for (tick = 0; tick < 1000; ++tick) {
        uint32_t can = mp_savefile_pace_fill(&pace, now, 256u, 8u);

        mp_savefile_pace_spend(&pace, can);
        laid += can;
        now  += 1u;
    }
    ut_checkf(laid >= 250u && laid <= 256u + 8u,
              "a thousand ticks a millisecond apart laid %u, the second's 256 and the brim",
              (unsigned)laid);
    ut_check(mp_savefile_pace_fill(NULL, 0u, 1u, 1u) == 0u, "no bucket holds nothing");
}

int main(void)
{
    check_the_digest();
    check_the_chunk_count();
    check_the_chunk_on_the_wire();
    check_the_assembly();
    check_the_mask();
    check_the_pace();

    return ut_summary("mp_savefile");
}
