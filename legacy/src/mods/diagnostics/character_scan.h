/* character_scan.h: the arithmetic behind the character census, with no engine in it.
 *
 * The census itself has to read a live process, so it cannot be tested without the game. Everything
 * that can be decided from numbers alone lives here instead and is covered by unittests: where a
 * pool slot starts, whether that slot is occupied, whether a character is near enough to report,
 * which way it is moving, and how to turn a fixed width name field into a string that is safe to
 * print. diag_characters.c reads memory and calls into this; this reads nothing. */
#ifndef DIAGNOSTICS_CHARACTER_SCAN_H
#define DIAGNOSTICS_CHARACTER_SCAN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The engine's own fixed size pool, compiled from util/list.c and recovered from the allocator at
 * 0x0046e92e and the matching free at 0x0046eaff. The header is six dwords followed by the slot
 * array; each slot is a link word followed by the payload the caller sees.
 *
 *   header[1]  the head of the singly linked list of occupied slots, threaded through link words
 *   header[2]  the iteration cursor, which is why diag_characters.c walks the pool itself rather
 *              than using the engine's own iterator
 *   header[3]  the payload size in bytes
 *   header[4]  the number of slots
 *   header[6]  the first slot
 *
 * The allocator scans for a link word of -1, takes that slot, and returns link + 4. The free path
 * unlinks the slot and writes -1 back. So -1 in the link word means free and anything else means
 * occupied, and a read only walk of the slot array needs nothing from the engine at all. */
#define CHARACTER_POOL_ELEMENT_SIZE_OFFSET 0x0Cu
#define CHARACTER_POOL_CAPACITY_OFFSET     0x10u
#define CHARACTER_POOL_SLOT_ARRAY_OFFSET   0x18u
#define CHARACTER_POOL_LINK_SIZE           0x04u
#define CHARACTER_POOL_FREE_LINK           0xFFFFFFFFu

/* The payload is a character record. The spawn path at 0x00437250 zeroes 0x81 dwords of it, so a
 * real one is 0x204 bytes; these bounds are wider than that on purpose, because the point of
 * checking is to refuse a pointer that is not a pool at all rather than to assert a size. */
#define CHARACTER_POOL_ELEMENT_SIZE_MIN 0x80u
#define CHARACTER_POOL_ELEMENT_SIZE_MAX 0x1000u
#define CHARACTER_POOL_CAPACITY_MAX     4096u

/* Anything slower than this in world units per simulation step reads as standing still. The engine
 * holds a character's previous position itself, so a stationary one gives exactly zero and this
 * only has to survive rounding rather than pick a threshold out of the air. */
#define CHARACTER_MOTION_EPSILON 0.001f

typedef enum character_motion {
    CHARACTER_MOTION_STILL = 0,
    CHARACTER_MOTION_RISING,
    CHARACTER_MOTION_SINKING
} character_motion_t;

/* Slot stride and the byte offset of slot `index` from the start of the pool header. Both return 0
 * when the arithmetic would overflow or the pool does not describe itself sensibly, which the
 * caller treats as "stop walking" rather than as a valid offset. */
uint32_t character_scan_slot_stride(uint32_t element_size);
uint32_t character_scan_slot_offset(uint32_t element_size, uint32_t index);

/* A slot holds a live character when its link word is not the free marker. */
bool character_scan_slot_is_live(uint32_t link_word);

/* Whether a pool header's own size and capacity are within the bounds above. A header that fails
 * this is not walked, because the alternative is walking whatever the pointer happened to hit. */
bool character_scan_pool_is_sane(uint32_t element_size, uint32_t capacity);

/* Straight line distance between two world positions, and the radius test built on it. The test
 * compares squared values, so it never takes a square root and never rejects on rounding at the
 * boundary the way a compared-after-sqrt version can. A negative radius matches nothing. */
float character_scan_distance(const float position[3], const float other[3]);
bool  character_scan_within_radius(const float position[3], const float other[3], float radius);

/* Height gained or lost over the last simulation step. Z is up in this engine: the five recorded
 * lift placements sit within a tenth of a unit of each other in Z while their X and Y differ by
 * whole units. A negative result means the character went down. */
float              character_scan_vertical_delta(const float position[3], const float previous[3]);
character_motion_t character_scan_classify(float vertical_delta);
const char        *character_scan_motion_text(character_motion_t motion);

/* Remembering a character's height between reports.
 *
 * The engine's own previous position field cannot answer "did this character move", because both
 * it and the current position are read at the same instant at frame end, by which point the
 * simulation has already copied one into the other. Measured in the game, that difference was
 * exactly zero on every line of a whole session, including a character that visibly descended
 * nearly a unit during it. The height has to be remembered here and differenced against the next
 * report instead.
 *
 * The table is keyed by the record's own address, which is stable while a character is alive
 * because the pool never moves a live slot. A record of 0 marks a free entry. */
#define CHARACTER_TRACK_MAX 64u

typedef struct character_track {
    uint32_t record;
    float    z;
} character_track_t;

/* Writes the change in height since this record was last seen and returns true. Returns false the
 * first time a record is seen, when there is nothing yet to compare against, and when the table is
 * NULL or empty. A full table replaces an entry rather than refusing, so a level with more
 * characters than the table holds still reports, just with a gap where one was evicted. */
bool character_scan_track(character_track_t *table, size_t count, uint32_t record, float z,
                          float *out_change);

/* Forgets everything. Called when the pool pointer changes, since slot addresses from one level
 * say nothing about the next. */
void character_scan_track_reset(character_track_t *table, size_t count);

/* Turn the record's fixed width name field into a printable, terminated string. The field is a
 * char[12] the spawn path fills with a byte copy from the placement, so it is not guaranteed to
 * carry a terminator and it is not guaranteed to be text at all if the pointer was wrong. Every
 * byte outside printable ASCII becomes a full stop, which keeps a bad read visible in the log
 * instead of writing control characters into it. Always terminates when out_size is at least 1. */
void character_scan_copy_name(const char *raw, size_t raw_size, char *out, size_t out_size);

#endif /* DIAGNOSTICS_CHARACTER_SCAN_H */
