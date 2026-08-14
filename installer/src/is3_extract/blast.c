/* PKWARE Data Compression Library "implode" decompressor.
 *
 * The three Huffman tables below are the ones the format defines; they are fixed, not carried in
 * the stream. Each byte is a run: the high nibble plus one is how many consecutive symbols share
 * the code length in the low nibble. That is the format's own compact notation, kept rather than
 * expanded so the tables stay checkable against the specification by eye.
 *
 * The one thing worth knowing before reading the decoder: codes are stored INVERTED, most
 * significant bit first, while everything else in the stream is least significant bit first. That
 * is why decode() complements each bit it takes.
 */
#include "blast.h"

#include <stdbool.h>
#include <string.h>

/* Literal codes, 256 symbols. Only consulted when the header says literals are coded. */
static const uint8_t LITERAL_LENGTHS[] = {
    11, 124, 8, 7, 28, 7, 188, 13, 76, 4, 10, 8, 12, 10, 12, 10, 8, 23, 8,
    9, 7, 6, 7, 8, 7, 6, 55, 8, 23, 24, 12, 11, 7, 9, 11, 12, 6, 7, 22, 5,
    7, 24, 6, 11, 9, 6, 7, 22, 7, 11, 38, 7, 9, 8, 25, 11, 8, 11, 9, 12,
    8, 12, 5, 38, 5, 38, 5, 11, 7, 5, 6, 21, 6, 10, 53, 8, 7, 24, 10, 27,
    44, 253, 253, 253, 252, 252, 252, 13, 12, 45, 12, 45, 12, 61, 12, 45,
    44, 173
};

/* Length codes, 16 symbols, each selecting a base and a number of extra bits. */
static const uint8_t LENGTH_LENGTHS[] = { 2, 35, 36, 53, 38, 23 };
static const uint16_t LENGTH_BASE[]   = { 3, 2, 4, 5, 6, 7, 8, 9, 10, 12, 16, 24, 40, 72, 136, 264 };
static const uint8_t  LENGTH_EXTRA[]  = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8 };

/* Distance codes, 64 symbols carrying the high bits of the distance. */
static const uint8_t DISTANCE_LENGTHS[] = { 2, 20, 53, 230, 247, 151, 248 };

/* A copy never reaches further back than the dictionary, and the dictionary tops out at 4096. */
#define WINDOW_SIZE 4096

/* 519 is the length that means "no more data". It cannot occur as a real match length. */
#define END_OF_STREAM_LENGTH 519

typedef struct huffman {
    int16_t count[16];   /* how many codes have each bit length */
    int16_t symbol[256]; /* symbols ordered by code */
} huffman_t;

typedef struct blast_state {
    const uint8_t *input;
    size_t         input_size;
    size_t         input_pos;

    uint32_t bit_buffer;
    int      bit_count;
    bool     out_of_input;

    uint8_t window[WINDOW_SIZE];
    size_t  window_used;

    blast_write_fn write;
    void          *user;
    bool           write_failed;
} blast_state_t;

static void build_huffman(huffman_t *table, const uint8_t *runs, size_t run_count)
{
    uint8_t lengths[256];
    int16_t offsets[16];
    size_t  symbol_count = 0;
    size_t  i;
    int     length;

    for (i = 0; i < run_count; ++i) {
        int repeat = (runs[i] >> 4) + 1;
        int value  = runs[i] & 15;
        while (repeat-- > 0) {
            lengths[symbol_count++] = (uint8_t)value;
        }
    }

    memset(table->count, 0, sizeof(table->count));
    for (i = 0; i < symbol_count; ++i) {
        table->count[lengths[i]]++;
    }

    offsets[1] = 0;
    for (length = 1; length < 15; ++length) {
        offsets[length + 1] = (int16_t)(offsets[length] + table->count[length]);
    }
    for (i = 0; i < symbol_count; ++i) {
        if (lengths[i] != 0) {
            table->symbol[offsets[lengths[i]]++] = (int16_t)i;
        }
    }
}

/* Least significant bit first. Past the end of the input the stream is treated as exhausted rather
 * than reading whatever follows it in memory; the caller sees BLAST_ERR_TRUNCATED. */
static int take_bits(blast_state_t *s, int need)
{
    uint32_t value = s->bit_buffer;

    while (s->bit_count < need) {
        if (s->input_pos >= s->input_size) {
            s->out_of_input = true;
            return 0;
        }
        value |= (uint32_t)s->input[s->input_pos++] << s->bit_count;
        s->bit_count += 8;
    }

    s->bit_buffer = value >> need;
    s->bit_count -= need;
    return (int)(value & (((uint32_t)1 << need) - 1));
}

/* Walks the code lengths shortest first. Returns -1 when the input runs out mid-code. */
static int decode_symbol(blast_state_t *s, const huffman_t *table)
{
    int code = 0;
    int first = 0;
    int index = 0;
    int length = 1;

    for (;;) {
        while (s->bit_count > 0) {
            int count;

            code |= (int)(s->bit_buffer & 1u) ^ 1;  /* the inversion noted in the file header */
            s->bit_buffer >>= 1;
            s->bit_count--;

            count = table->count[length];
            if (code < first + count) {
                return table->symbol[index + (code - first)];
            }
            index += count;
            first = (first + count) << 1;
            code <<= 1;
            length++;
            if (length > 15) {
                return -1;
            }
        }

        if (s->input_pos >= s->input_size) {
            s->out_of_input = true;
            return -1;
        }
        s->bit_buffer = s->input[s->input_pos++];
        s->bit_count = 8;
    }
}

static void flush_window(blast_state_t *s)
{
    if (s->window_used == 0 || s->write_failed) {
        return;
    }
    if (!s->write(s->user, s->window, s->window_used)) {
        s->write_failed = true;
    }
    s->window_used = 0;
}

static void emit_byte(blast_state_t *s, uint8_t byte)
{
    s->window[s->window_used++] = byte;
    if (s->window_used == WINDOW_SIZE) {
        /* The window is handed over whole, but the last WINDOW_SIZE bytes have to stay readable
         * for the next back-reference, so the buffer is not cleared. window_used going to zero
         * is what makes the old contents unreachable as output while keeping them as history. */
        if (!s->write_failed && !s->write(s->user, s->window, WINDOW_SIZE)) {
            s->write_failed = true;
        }
        s->window_used = 0;
    }
}

blast_result_t blast_decompress(
    const uint8_t *input,
    size_t         input_size,
    blast_write_fn write,
    void          *user)
{
    /* Rebuilt per call rather than cached in file scope: it costs a few hundred iterations once
     * per member, and it keeps this translation unit free of mutable global state. */
    huffman_t literal_table;
    huffman_t length_table;
    huffman_t distance_table;

    blast_state_t s;
    int   literal_mode;
    int   dictionary_bits;
    int64_t total_out = 0;

    build_huffman(&literal_table, LITERAL_LENGTHS, sizeof(LITERAL_LENGTHS));
    build_huffman(&length_table, LENGTH_LENGTHS, sizeof(LENGTH_LENGTHS));
    build_huffman(&distance_table, DISTANCE_LENGTHS, sizeof(DISTANCE_LENGTHS));

    memset(&s, 0, sizeof(s));
    s.input = input;
    s.input_size = input_size;
    s.write = write;
    s.user = user;

    literal_mode = take_bits(&s, 8);
    if (s.out_of_input) {
        return BLAST_ERR_TRUNCATED;
    }
    if (literal_mode > 1) {
        return BLAST_ERR_LITERAL_MODE;
    }

    dictionary_bits = take_bits(&s, 8);
    if (s.out_of_input) {
        return BLAST_ERR_TRUNCATED;
    }
    if (dictionary_bits < 4 || dictionary_bits > 6) {
        return BLAST_ERR_DICTIONARY_SIZE;
    }

    for (;;) {
        int is_pair = take_bits(&s, 1);
        if (s.out_of_input) {
            return BLAST_ERR_TRUNCATED;
        }

        if (is_pair) {
            int symbol = decode_symbol(&s, &length_table);
            int length;
            int distance_shift;
            int distance;
            int i;
            size_t from;

            if (symbol < 0) {
                return BLAST_ERR_TRUNCATED;
            }
            length = LENGTH_BASE[symbol] + take_bits(&s, LENGTH_EXTRA[symbol]);
            if (s.out_of_input) {
                return BLAST_ERR_TRUNCATED;
            }
            if (length == END_OF_STREAM_LENGTH) {
                break;
            }

            /* A two-byte match always uses a 2-bit low field regardless of dictionary size; every
             * longer one uses the dictionary's own width. */
            distance_shift = (length == 2) ? 2 : dictionary_bits;
            symbol = decode_symbol(&s, &distance_table);
            if (symbol < 0) {
                return BLAST_ERR_TRUNCATED;
            }
            distance = (symbol << distance_shift) + take_bits(&s, distance_shift) + 1;
            if (s.out_of_input) {
                return BLAST_ERR_TRUNCATED;
            }
            if ((int64_t)distance > total_out) {
                return BLAST_ERR_DISTANCE;
            }

            /* The window is circular in effect: window_used counts unwritten bytes, and the bytes
             * before it are the history a back-reference reads. */
            from = (s.window_used + WINDOW_SIZE - (size_t)distance) % WINDOW_SIZE;
            for (i = 0; i < length; ++i) {
                emit_byte(&s, s.window[from]);
                from = (from + 1) % WINDOW_SIZE;
            }
            total_out += length;
        } else {
            int byte = literal_mode ? decode_symbol(&s, &literal_table) : take_bits(&s, 8);
            if (byte < 0 || s.out_of_input) {
                return BLAST_ERR_TRUNCATED;
            }
            emit_byte(&s, (uint8_t)byte);
            total_out++;
        }

        if (s.write_failed) {
            return BLAST_ERR_WRITE;
        }
    }

    flush_window(&s);
    return s.write_failed ? BLAST_ERR_WRITE : BLAST_OK;
}

const char *blast_result_text(blast_result_t result)
{
    switch (result) {
    case BLAST_OK:                    return "ok";
    case BLAST_ERR_LITERAL_MODE:      return "the stream header does not name a valid literal mode";
    case BLAST_ERR_DICTIONARY_SIZE:   return "the stream header does not name a valid dictionary size";
    case BLAST_ERR_TRUNCATED:         return "the compressed stream ends before its end marker";
    case BLAST_ERR_DISTANCE:          return "a back-reference points before the start of the data";
    case BLAST_ERR_WRITE:             return "the output could not be written";
    default:                          return "unknown error";
    }
}
