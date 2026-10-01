/* mp_fuzz_input.h: what the fuzz tests feed their decoders.
 *
 * A seeded generator, so a failure reproduces, and the ways a hostile or broken link changes a
 * valid note. Every function here is static, so each test that includes this has its own stream
 * from the same seed; each one uses all four, which the compiler would otherwise say.
 */
#ifndef UNITTESTS_MP_FUZZ_INPUT_H
#define UNITTESTS_MP_FUZZ_INPUT_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define ROUNDS 20000u

static uint32_t s_rng = 0x9E3779B9u;

static uint32_t next_random(void)
{
    s_rng ^= s_rng << 13;
    s_rng ^= s_rng >> 17;
    s_rng ^= s_rng << 5;
    return s_rng;
}

static size_t random_below(size_t n)
{
    return (n == 0) ? 0 : (size_t)(next_random() % (uint32_t)n);
}

static void fill_random(uint8_t *bytes, size_t len)
{
    size_t i;

    for (i = 0; i < len; ++i) {
        bytes[i] = (uint8_t)next_random();
    }
}

/* One of the ways a hostile or broken link changes a valid packet. Returns the new length, which
 * a truncation shortens and a tail of random bytes lengthens up to the capacity. */
static size_t mutate(uint8_t *bytes, size_t len, size_t capacity)
{
    size_t at;
    size_t run;

    switch (next_random() % 6u) {
    case 0:     /* one bit */
        if (len > 0) {
            at = random_below(len);
            bytes[at] ^= (uint8_t)(1u << (next_random() % 8u));
        }
        return len;
    case 1:     /* one byte replaced */
        if (len > 0) {
            bytes[random_below(len)] = (uint8_t)next_random();
        }
        return len;
    case 2:     /* truncated */
        return random_below(len + 1);
    case 3:     /* a run copied over another */
        if (len > 1) {
            run = 1 + random_below(len / 2);
            memmove(bytes + random_below(len - run), bytes + random_below(len - run), run);
        }
        return len;
    case 4:     /* a random tail */
        run = random_below(capacity - len + 1);
        fill_random(bytes + len, run);
        return len + run;
    default:    /* a handful of bytes */
        for (run = 1 + random_below(8); run > 0 && len > 0; --run) {
            bytes[random_below(len)] = (uint8_t)next_random();
        }
        return len;
    }
}

#endif /* UNITTESTS_MP_FUZZ_INPUT_H */
