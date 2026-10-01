/* npc_spawn_block.c: the panel's savegame block as bytes, checked without the game.
 *
 * What would be silent if it were wrong: a field written at one offset and read at another,
 * which raises every copy of a savegame in the wrong place with no complaint; a torn block that
 * gives half its copies; an entry from a later build that stops the ones behind it; a name or a
 * number the builder would take on trust. The round trip is fuzzed as well, because a codec's
 * asymmetries are found by the round trip and not by the cases one thought of.
 */
#include "unittest.h"

#include "common/text.h"

#include "npc_spawn_block.h"

#include <math.h>
#include <string.h>

static uint8_t s_block[NPC_SPAWN_BLOCK_MAX_BYTES + 64u];
static npc_spawn_saved_t s_in[NPC_SPAWN_BLOCK_COPIES_MAX + 2u];
static npc_spawn_saved_t s_out[NPC_SPAWN_BLOCK_COPIES_MAX + 2u];

static uint32_t s_seed = 0x2659A1B3u;

static uint32_t next_random(void)
{
    s_seed = s_seed * 1664525u + 1013904223u;
    return s_seed >> 8;
}

static float random_float(float span)
{
    return ((float)(next_random() & 0xFFFFu) / 65535.0f - 0.5f) * span;
}

static void sample(npc_spawn_saved_t *copy, uint32_t n)
{
    memset(copy, 0, sizeof *copy);
    copy->desc.source      = (uint8_t)(n % 255u);
    copy->desc.behaviour   = (uint8_t)(n % 4u);
    copy->desc.archive     = (n % 3u) == 0u;
    text_format(copy->desc.file, sizeof copy->desc.file, "kind%u.baf", n % 100u);
    copy->desc.position[0] = 10.0f + (float)n;
    copy->desc.position[1] = -20.5f;
    copy->desc.position[2] = 3.25f;
    copy->desc.facing      = 90.0f;
    copy->position[0]      = 11.0f + (float)n;
    copy->position[1]      = -19.5f;
    copy->position[2]      = 3.5f;
    copy->yaw              = 45.0f;
    copy->health           = (int32_t)n - 5;
}

static bool same(const npc_spawn_saved_t *a, const npc_spawn_saved_t *b)
{
    return a->desc.source == b->desc.source && a->desc.behaviour == b->desc.behaviour &&
           a->desc.archive == b->desc.archive && strcmp(a->desc.file, b->desc.file) == 0 &&
           memcmp(a->desc.position, b->desc.position, sizeof a->desc.position) == 0 &&
           memcmp(&a->desc.facing, &b->desc.facing, sizeof a->desc.facing) == 0 &&
           memcmp(a->position, b->position, sizeof a->position) == 0 &&
           memcmp(&a->yaw, &b->yaw, sizeof a->yaw) == 0 && a->health == b->health;
}

/* A block whose entries the test writes by hand: the length word is set at the end. */
static size_t s_at;

static void hand_begin(void)
{
    memset(s_block, 0, sizeof s_block);
    s_at = NPC_SPAWN_BLOCK_LENGTH_BYTES;
}

static void hand_entry(uint8_t type, uint8_t version, const uint8_t *body, uint16_t length)
{
    s_block[s_at]     = type;
    s_block[s_at + 1] = version;
    s_block[s_at + 2] = (uint8_t)(length & 0xFFu);
    s_block[s_at + 3] = (uint8_t)(length >> 8);
    memcpy(s_block + s_at + 4, body, length);
    s_at += 4u + length;
}

/* The 52 bytes of one copy, as the encoder lays them out. */
static void copy_bytes(const npc_spawn_saved_t *copy, uint8_t *out)
{
    size_t bytes = 0;

    (void)npc_spawn_block_encode(copy, 1u, s_block, sizeof s_block, &bytes);
    memcpy(out, s_block + NPC_SPAWN_BLOCK_LENGTH_BYTES + NPC_SPAWN_BLOCK_ENTRY_HEAD,
           NPC_SPAWN_BLOCK_COPY_BYTES);
}

static size_t hand_end(void)
{
    uint32_t length = (uint32_t)(s_at - NPC_SPAWN_BLOCK_LENGTH_BYTES);

    s_block[0] = (uint8_t)(length & 0xFFu);
    s_block[1] = (uint8_t)((length >> 8) & 0xFFu);
    s_block[2] = (uint8_t)((length >> 16) & 0xFFu);
    s_block[3] = (uint8_t)(length >> 24);
    return s_at;
}

static void check_the_shape(void)
{
    ut_section("the shape");
    ut_check(npc_spawn_block_bytes(0u) == 4u && npc_spawn_block_bytes(1u) == 60u,
             "an empty block is its length word, and a copy is 4 bytes of head and 52 of body");
    ut_check(npc_spawn_block_bytes(NPC_SPAWN_BLOCK_COPIES_MAX) == NPC_SPAWN_BLOCK_MAX_BYTES,
             "a full block is 7172 bytes");
}

static void check_one_copy(void)
{
    npc_spawn_block_read_t read;
    size_t                 bytes = 0;
    const uint8_t         *e = s_block + NPC_SPAWN_BLOCK_LENGTH_BYTES;
    float                  f = 0.0f;

    ut_section("one copy, byte by byte");
    sample(&s_in[0], 7u);
    ut_check(npc_spawn_block_encode(s_in, 1u, s_block, sizeof s_block, &bytes) && bytes == 60u,
             "one copy encodes to 60 bytes");
    ut_check(s_block[0] == 56u && s_block[1] == 0u && s_block[2] == 0u && s_block[3] == 0u,
             "the length word counts what follows it, low byte first");
    ut_check(e[0] == 1u && e[1] == 1u && e[2] == 52u && e[3] == 0u,
             "the entry says type 1, version 1, 52 bytes");
    ut_check(e[4] == 7u && e[5] == 3u && e[6] == 0u && e[7] == 0u,
             "then the source, the behaviour, no archive flag and a zero");
    ut_check(memcmp(e + 8, "kind7.baf\0\0\0", 12) == 0, "then the file, NUL padded to 12");
    memcpy(&f, e + 20, sizeof f);
    ut_check(f == 17.0f, "the described position starts at body offset 16");
    memcpy(&f, e + 36, sizeof f);
    ut_check(f == 18.0f, "where it stands starts at body offset 32");
    ut_check(e[52] == 2u && e[53] == 0u && e[54] == 0u && e[55] == 0u,
             "and its health, 2, is the last word");
    ut_check(npc_spawn_block_decode(s_block, bytes, s_out, 4u, &read) && read.copies == 1u &&
                 same(&s_in[0], &s_out[0]),
             "and it reads back as it was");
}

static void check_every_field(void)
{
    static const float F[10] = { 1.5f, 2.5f, 3.5f, 4.5f, 5.5f, 6.5f, 7.5f, 8.5f, 9.5f, 10.5f };
    const uint8_t     *body = s_block + NPC_SPAWN_BLOCK_LENGTH_BYTES + NPC_SPAWN_BLOCK_ENTRY_HEAD;
    npc_spawn_saved_t  c;
    size_t             bytes = 0;
    uint32_t           k;
    bool               all = true;

    ut_section("every field at its own offset");
    memset(&c, 0, sizeof c);
    c.desc.source    = 11u;
    c.desc.behaviour = 2u;
    c.desc.archive   = true;
    memcpy(c.desc.file, "abc.baf", 8);
    memcpy(c.desc.position, F, sizeof c.desc.position);
    c.desc.facing = F[3];
    memcpy(c.position, F + 4, sizeof c.position);
    c.yaw    = F[7];
    c.health = 0x01020304;
    (void)npc_spawn_block_encode(&c, 1u, s_block, sizeof s_block, &bytes);
    for (k = 0; k < 8u; ++k) {
        float f;

        memcpy(&f, body + 16u + 4u * k, sizeof f);
        all = all && f == F[k];
    }
    ut_check(body[0] == 11u && body[1] == 2u && body[2] == 1u && body[3] == 0u,
             "the source, the behaviour, the archive flag, a zero");
    ut_check(all, "the eight floats in the order the header names: the described position, "
                  "its facing, where it stands, its yaw");
    ut_check(body[48] == 4u && body[49] == 3u && body[50] == 2u && body[51] == 1u,
             "and the health last, low byte first");
}

static void check_the_edges_of_a_name(void)
{
    npc_spawn_block_read_t read;
    size_t                 bytes;
    uint8_t                body[NPC_SPAWN_BLOCK_COPY_BYTES];

    ut_section("the edges of a name");
    sample(&s_in[0], 3u);
    copy_bytes(&s_in[0], body);
    body[4 + 2] = 0x7Fu;
    hand_begin();
    hand_entry(1u, 1u, body, NPC_SPAWN_BLOCK_COPY_BYTES);
    bytes = hand_end();
    ut_check(npc_spawn_block_decode(s_block, bytes, s_out, 4u, &read) && read.invalid == 1u,
             "DEL in a name is not a printable character");
    copy_bytes(&s_in[0], body);
    body[3] = 1u;
    hand_begin();
    hand_entry(1u, 1u, body, NPC_SPAWN_BLOCK_COPY_BYTES);
    bytes = hand_end();
    ut_check(npc_spawn_block_decode(s_block, bytes, s_out, 4u, &read) && read.invalid == 1u,
             "and the zero byte beside the flags has to be zero");
}

static void check_a_torn_block_writes_nothing(void)
{
    npc_spawn_block_read_t read;
    size_t                 bytes = 0;
    npc_spawn_saved_t      sentinel;

    ut_section("a torn block writes nothing into the caller's array");
    sample(&s_in[0], 4u);
    sample(&s_in[1], 5u);
    (void)npc_spawn_block_encode(s_in, 2u, s_block, sizeof s_block, &bytes);
    s_block[NPC_SPAWN_BLOCK_LENGTH_BYTES + 56u + 2u] = 200u;   /* the second entry runs out */
    memset(&sentinel, 0x5A, sizeof sentinel);
    s_out[0] = sentinel;
    ut_check(!npc_spawn_block_decode(s_block, bytes, s_out, 4u, &read) &&
                 memcmp(&s_out[0], &sentinel, sizeof sentinel) == 0,
             "the good first copy is not taken either: the block is checked whole first");
}

static void check_empty_and_full(void)
{
    npc_spawn_block_read_t read;
    size_t                 bytes = 0;
    uint32_t               i;

    ut_section("empty and full");
    ut_check(npc_spawn_block_encode(NULL, 0u, s_block, sizeof s_block, &bytes) && bytes == 4u &&
                 npc_spawn_block_decode(s_block, bytes, s_out, 4u, &read) && read.copies == 0u,
             "a block with no copy is legitimate");
    for (i = 0; i < NPC_SPAWN_BLOCK_COPIES_MAX + 1u; ++i) {
        sample(&s_in[i], i);
    }
    ut_check(!npc_spawn_block_encode(s_in, NPC_SPAWN_BLOCK_COPIES_MAX + 1u, s_block,
                                     sizeof s_block, &bytes),
             "129 copies are refused: the pool holds 128");
    ut_check(npc_spawn_block_encode(s_in, NPC_SPAWN_BLOCK_COPIES_MAX, s_block, sizeof s_block,
                                    &bytes) &&
                 bytes == NPC_SPAWN_BLOCK_MAX_BYTES,
             "128 copies fill a full block");
    ut_check(npc_spawn_block_decode(s_block, bytes, s_out, NPC_SPAWN_BLOCK_COPIES_MAX, &read) &&
                 read.copies == NPC_SPAWN_BLOCK_COPIES_MAX && same(&s_in[127], &s_out[127]),
             "and all of them read back");
    ut_check(npc_spawn_block_decode(s_block, bytes, s_out, 100u, &read) && read.copies == 100u &&
                 read.over == 28u,
             "a smaller array takes what it holds and counts the rest");
    ut_check(!npc_spawn_block_encode(s_in, 2u, s_block, npc_spawn_block_bytes(2u) - 1u, &bytes),
             "a room one byte short is refused");
}

static void check_a_torn_block_gives_nothing(void)
{
    npc_spawn_block_read_t read;
    size_t                 bytes = 0;
    size_t                 cut;
    uint32_t               taken = 0;

    ut_section("a torn block gives nothing");
    sample(&s_in[0], 1u);
    sample(&s_in[1], 2u);
    (void)npc_spawn_block_encode(s_in, 2u, s_block, sizeof s_block, &bytes);
    for (cut = 0; cut < bytes; ++cut) {
        s_block[0] = (uint8_t)(bytes - 4u);      /* the word of the whole block */
        taken += npc_spawn_block_decode(s_block, cut, s_out, 4u, &read) ? 1u : 0u;
        if (cut >= 4u && (cut - 4u) % 56u != 0u) {
            s_block[0] = (uint8_t)(cut - 4u);    /* a word that agrees, cut inside an entry */
            taken += npc_spawn_block_decode(s_block, cut, s_out, 4u, &read) ? 1u : 0u;
        }
    }
    s_block[0] = (uint8_t)(bytes - 4u);
    ut_checkf(taken == 0u,
              "every cut short of the end is refused, the length word agreeing or not, "
              "unless it falls between two entries (%u taken)", (unsigned)taken);
    s_block[0] = (uint8_t)(bytes - 4u);
    ut_check(!npc_spawn_block_decode(s_block, bytes + 1u, s_out, 4u, &read),
             "a length word one short of the bytes given is refused");
    s_block[NPC_SPAWN_BLOCK_LENGTH_BYTES + 2] = 200u;
    ut_check(!npc_spawn_block_decode(s_block, bytes, s_out, 4u, &read) && read.copies == 0u,
             "an entry that claims more than is left is refused, and nothing is taken");
}

/* A block laid out by hand: two entries a later build could write, then seven copies no build
 * could raise around the one legitimate name. */
static void check_what_later_builds_write(void)
{
    npc_spawn_block_read_t read;
    size_t                 bytes;
    uint8_t                body[NPC_SPAWN_BLOCK_COPY_BYTES];
    uint8_t                bad[NPC_SPAWN_BLOCK_COPY_BYTES];
    float                  nan_value = NAN;

    ut_section("what a later build writes, and what no build would");
    sample(&s_in[0], 3u);
    copy_bytes(&s_in[0], body);

    hand_begin();
    hand_entry(9u, 1u, body, 20u);
    hand_entry(1u, 2u, body, NPC_SPAWN_BLOCK_COPY_BYTES);
    hand_entry(1u, 1u, body, NPC_SPAWN_BLOCK_COPY_BYTES);
    bytes = hand_end();
    ut_check(npc_spawn_block_decode(s_block, bytes, s_out, 4u, &read) && read.copies == 1u &&
                 read.unknown == 2u && same(&s_in[0], &s_out[0]),
             "an unknown type and an unknown version are stepped over, and the copy behind "
             "them is taken");

    hand_begin();
    memcpy(bad, body, sizeof bad);
    memset(bad + 4, 0, NPC_SPAWN_FILE_MAX);
    hand_entry(1u, 1u, bad, NPC_SPAWN_BLOCK_COPY_BYTES);      /* no name */
    memcpy(bad, body, sizeof bad);
    memcpy(bad + 4, "abcdefghijkl", NPC_SPAWN_FILE_MAX);
    hand_entry(1u, 1u, bad, NPC_SPAWN_BLOCK_COPY_BYTES);      /* twelve, legitimate */
    memcpy(bad, body, sizeof bad);
    bad[4 + 3] = '\t';
    hand_entry(1u, 1u, bad, NPC_SPAWN_BLOCK_COPY_BYTES);      /* a control character */
    memcpy(bad, body, sizeof bad);
    bad[4 + 10] = 'x';
    hand_entry(1u, 1u, bad, NPC_SPAWN_BLOCK_COPY_BYTES);      /* a byte behind the name */
    memcpy(bad, body, sizeof bad);
    memcpy(bad + 36, &nan_value, sizeof nan_value);
    hand_entry(1u, 1u, bad, NPC_SPAWN_BLOCK_COPY_BYTES);      /* where it stands is NaN */
    memcpy(bad, body, sizeof bad);
    bad[0] = NPC_SPAWN_NO_SOURCE;
    bad[2] = 0u;
    hand_entry(1u, 1u, bad, NPC_SPAWN_BLOCK_COPY_BYTES);      /* a level kind, no source */
    memcpy(bad, body, sizeof bad);
    bad[2] = 0x02u;
    hand_entry(1u, 1u, bad, NPC_SPAWN_BLOCK_COPY_BYTES);      /* a flag nobody knows */
    hand_entry(1u, 1u, body, 40u);                            /* a copy of another length */
    bytes = hand_end();
    ut_checkf(npc_spawn_block_decode(s_block, bytes, s_out, 8u, &read) && read.copies == 1u &&
                  read.invalid == 7u && strcmp(s_out[0].desc.file, "abcdefghijkl") == 0,
              "seven copies no build could raise are counted (%u) and stepped over; a name "
              "of twelve characters is taken", (unsigned)read.invalid);
}

static void check_what_a_copy_may_be(void)
{
    size_t bytes = 0;

    ut_check(!npc_spawn_block_copy_is_valid(NULL), "no copy is not a valid one");
    sample(&s_in[0], 5u);
    s_in[0].desc.archive = true;
    s_in[0].desc.source  = NPC_SPAWN_NO_SOURCE;
    ut_check(npc_spawn_block_copy_is_valid(&s_in[0]),
             "an archive kind needs no donor: a level with none gives it the defaults");
    s_in[0].desc.behaviour = NPC_SPAWN_BEHAVIOURS;
    ut_check(!npc_spawn_block_copy_is_valid(&s_in[0]),
             "a behaviour past the four is not a copy anybody could raise");
    s_in[0].desc.behaviour = 0u;
    s_in[0].yaw = INFINITY;
    ut_check(!npc_spawn_block_encode(s_in, 1u, s_block, sizeof s_block, &bytes),
             "and the encoder refuses what the decoder would");
}

/* The n copies of one trip, every field drawn from the generator. */
static void random_copies(uint32_t n)
{
    uint32_t j;

    for (j = 0; j < n; ++j) {
        npc_spawn_saved_t *c = &s_in[j];
        uint32_t           len = 1u + next_random() % NPC_SPAWN_FILE_MAX;
        uint32_t           k;

        memset(c, 0, sizeof *c);
        c->desc.archive   = (next_random() & 1u) != 0u;
        c->desc.source    = (uint8_t)(next_random() % (c->desc.archive ? 256u : 255u));
        c->desc.behaviour = (uint8_t)(next_random() % NPC_SPAWN_BEHAVIOURS);
        for (k = 0; k < len; ++k) {
            c->desc.file[k] = (char)('!' + next_random() % 94u);
        }
        c->desc.position[0] = random_float(4000.0f);
        c->desc.position[1] = random_float(4000.0f);
        c->desc.position[2] = random_float(400.0f);
        c->desc.facing      = random_float(720.0f);
        c->position[0]      = random_float(4000.0f);
        c->position[1]      = random_float(4000.0f);
        c->position[2]      = random_float(400.0f);
        c->yaw              = random_float(720.0f);
        c->health           = (int32_t)next_random() - 0x400000;
    }
}

static void check_the_round_trip(void)
{
    npc_spawn_block_read_t read;
    size_t                 bytes = 0;
    uint32_t               trip;
    uint32_t               failed = 0;
    uint32_t               flips_taken = 0;

    ut_section("the round trip, fuzzed");
    for (trip = 0; trip < 2000u; ++trip) {
        uint32_t n = next_random() % 6u;
        uint32_t j;

        random_copies(n);
        if (!npc_spawn_block_encode(s_in, n, s_block, sizeof s_block, &bytes) ||
            !npc_spawn_block_decode(s_block, bytes, s_out, 8u, &read) || read.copies != n) {
            ++failed;
            continue;
        }
        for (j = 0; j < n; ++j) {
            failed += same(&s_in[j], &s_out[j]) ? 0u : 1u;
        }
        /* One byte flipped: never a fault, and whatever it takes is valid. */
        if (bytes > 0u) {
            size_t at = next_random() % bytes;

            s_block[at] ^= (uint8_t)(1u + next_random() % 255u);
            if (npc_spawn_block_decode(s_block, bytes, s_out, 8u, &read)) {
                for (j = 0; j < read.copies; ++j) {
                    failed += npc_spawn_block_copy_is_valid(&s_out[j]) ? 0u : 1u;
                }
                flips_taken += read.copies;
            }
        }
    }
    ut_checkf(failed == 0u, "2000 random blocks read back bit for bit, and a flipped byte "
              "gives only valid copies (%u failures, %u copies taken past a flip)",
              (unsigned)failed, (unsigned)flips_taken);
}

int main(void)
{
    check_the_shape();
    check_one_copy();
    check_every_field();
    check_the_edges_of_a_name();
    check_a_torn_block_writes_nothing();
    check_empty_and_full();
    check_a_torn_block_gives_nothing();
    check_what_later_builds_write();
    check_what_a_copy_may_be();
    check_the_round_trip();

    return ut_summary("npc_spawn_block");
}
