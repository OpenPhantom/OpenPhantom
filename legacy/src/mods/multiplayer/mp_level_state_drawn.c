/* mp_level_state_drawn.c: the fog as the device draws it, read after a fog command. See the
 * header. */
#include "mp_level_state_drawn.h"

#include "mp_level_state_bind.h"

#include "common/logging.h"
#include "common/memory.h"
#include "common/signature.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The first sixteen commands of a session get a line; later ones are counted. */
#define DRAWN_LINES 16u

/* A second of substeps between the two readings of one line. */
#define SECOND_LATER 32u

/* The level's render flag word, whose bit 0 is its fog, and the band the per vertex ramp reads. */
#define WORLD_RENDER_FLAGS 0x210u
#define WORLD_FOG_BIT      0x1u
#define WORLD_FOG_START    0x218u
#define WORLD_FOG_END      0x21Cu

#define COMMAND_START 6
#define COMMAND_END   7
#define COMMAND_GREEN 11

#define BRANCH_OPCODE 0xE9u

/* Commands 6 and 7: push the float argument, call, pop; the call returns to +9. Masked: the
 * call. */
#define ARM_EDGE_RETURN 0x09u
static const uint8_t SIG_ARM_FOG_START[13] = {
    0x8B, 0x55, 0x14, 0x52, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x83, 0xC4, 0x04, 0xE9,
};
static const uint8_t MSK_ARM_FOG_START[13] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF,
};
static const uint8_t SIG_ARM_FOG_END[13] = {
    0x8B, 0x45, 0x14, 0x50, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x83, 0xC4, 0x04, 0xE9,
};
static const uint8_t MSK_ARM_FOG_END[13] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF,
};

/* Command 11: pale green and a zero for the level's own fog pushed, then the two arguments and the
 * call to the ramp, which returns to +0x1E. */
#define ARM_GREEN_RETURN 0x1Eu
static const uint8_t SIG_ARM_FOG_GREEN[26] = {
    0x6A, 0x00, 0x68, 0xAA, 0x00, 0x00, 0x00, 0x68, 0xFF, 0x00, 0x00, 0x00, 0x68,
    0xAA, 0x00, 0x00, 0x00, 0x8B, 0x4D, 0x14, 0x51, 0x8B, 0x55, 0x10, 0x52, 0xE8,
};

/* The two edge functions share their head, push ebp, mov ebp esp, fld [ebp+8], six bytes on an
 * instruction boundary. Behind it: the test against 0.0, the call that writes the device's cell,
 * which returns to +0x1C, and for the start the shadow the ramp works from. Masked: the constant,
 * the call and the shadow. */
#define EDGE_HEAD_BYTES  6u
#define EDGE_CALL_RETURN 0x1Cu
static const uint8_t EDGE_HEAD[EDGE_HEAD_BYTES] = { 0x55, 0x8B, 0xEC, 0xD9, 0x45, 0x08 };
static const uint8_t SIG_FOG_START_TAIL[36] = {
    0xD8, 0x1D, 0x00, 0x00, 0x00, 0x00, 0xDF, 0xE0, 0xF6, 0xC4, 0x01, 0x75,
    0x15, 0x8B, 0x45, 0x08, 0x50, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x83, 0xC4,
    0x04, 0x8B, 0x4D, 0x08, 0x89, 0x0D, 0x00, 0x00, 0x00, 0x00, 0x5D, 0xC3,
};
static const uint8_t MSK_FOG_START_TAIL[36] = {
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF,
};
static const uint8_t SIG_FOG_END_TAIL[27] = {
    0xD8, 0x1D, 0x00, 0x00, 0x00, 0x00, 0xDF, 0xE0, 0xF6, 0xC4, 0x01, 0x75, 0x0C, 0x8B,
    0x45, 0x08, 0x50, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x83, 0xC4, 0x04, 0x5D, 0xC3,
};
static const uint8_t MSK_FOG_END_TAIL[27] = {
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
};

/* The device's two edge setters are a load and a store: `mov eax, [esp+4]; mov [cell], eax; ret`.
 * Four functions of the image have that shape, so it is checked where the call leads rather than
 * searched for. */
static const uint8_t LEAF_STORE_HEAD[5] = { 0x8B, 0x44, 0x24, 0x04, 0xA3 };
#define LEAF_STORE_CELL 5u
#define LEAF_STORE_RET  9u

/* The ramp's head, push ebp, mov ebp esp, cmp [ebp+8] 0, seven bytes on an instruction boundary,
 * and its whole body behind it: the two refusals, the store of the keep word, the colour, the
 * level's fog bit, and the stores of the target, the length and what is left, in that order.
 * Masked: the constant, the six calls, the keep word twice, the level twice and the three ramp
 * words. The colour's setter is the call that returns to +0x66; the target is the operand at
 * +0xA6 and what is left at +0xBB. */
#define RAMP_HEAD_BYTES     7u
#define RAMP_COLOUR_RETURN  0x66u
#define RAMP_TARGET_OPERAND 0xA6u
#define RAMP_LEFT_OPERAND   0xBBu
static const uint8_t RAMP_HEAD[RAMP_HEAD_BYTES] = { 0x55, 0x8B, 0xEC, 0x83, 0x7D, 0x08, 0x00 };
static const uint8_t SIG_RAMP_FOG_TAIL[184] = {
    0x7F, 0x05, 0xE9, 0xB1, 0x00, 0x00, 0x00, 0xD9, 0x45, 0x0C, 0xD8, 0x1D,
    0x00, 0x00, 0x00, 0x00, 0xDF, 0xE0, 0xF6, 0xC4, 0x01, 0x74, 0x05, 0xE9,
    0x9C, 0x00, 0x00, 0x00, 0x8B, 0x45, 0x1C, 0xA3, 0x00, 0x00, 0x00, 0x00,
    0x83, 0x3D, 0x00, 0x00, 0x00, 0x00, 0x00, 0x75, 0x6D, 0xE8, 0x00, 0x00,
    0x00, 0x00, 0x24, 0xBF, 0x50, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x83, 0xC4,
    0x04, 0x8B, 0x4D, 0x18, 0x81, 0xE1, 0xFF, 0x00, 0x00, 0x00, 0x51, 0x8B,
    0x55, 0x14, 0x81, 0xE2, 0xFF, 0x00, 0x00, 0x00, 0x52, 0x8B, 0x45, 0x10,
    0x25, 0xFF, 0x00, 0x00, 0x00, 0x50, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x83,
    0xC4, 0x0C, 0x6A, 0x00, 0x6A, 0x00, 0x6A, 0x00, 0xE8, 0x00, 0x00, 0x00,
    0x00, 0x83, 0xC4, 0x0C, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x40, 0x50,
    0xE8, 0x00, 0x00, 0x00, 0x00, 0x83, 0xC4, 0x04, 0x8B, 0x0D, 0x00, 0x00,
    0x00, 0x00, 0x8B, 0x91, 0x10, 0x02, 0x00, 0x00, 0x83, 0xCA, 0x01, 0xA1,
    0x00, 0x00, 0x00, 0x00, 0x89, 0x90, 0x10, 0x02, 0x00, 0x00, 0x8B, 0x4D,
    0x0C, 0x89, 0x0D, 0x00, 0x00, 0x00, 0x00, 0xDB, 0x45, 0x08, 0xD9, 0x1D,
    0x00, 0x00, 0x00, 0x00, 0x8B, 0x15, 0x00, 0x00, 0x00, 0x00, 0x89, 0x15,
    0x00, 0x00, 0x00, 0x00,
};
static const uint8_t MSK_RAMP_FOG_TAIL[184] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00,
    0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00,
    0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00,
    0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF,
    0x00, 0x00, 0x00, 0x00,
};

/* The device's colour setter: three arguments into three cells, red at +0x0D, green at +0x13 and
 * blue at +0x19. Masked: the cells. */
#define COLOUR_RED   0x0Du
#define COLOUR_GREEN 0x13u
#define COLOUR_BLUE  0x19u
static const uint8_t SIG_FOG_COLOUR[29] = {
    0x8B, 0x44, 0x24, 0x04, 0x8B, 0x4C, 0x24, 0x08, 0x8B, 0x54, 0x24, 0x0C, 0xA3, 0x00, 0x00,
    0x00, 0x00, 0x89, 0x0D, 0x00, 0x00, 0x00, 0x00, 0x89, 0x15, 0x00, 0x00, 0x00, 0x00,
};
static const uint8_t MSK_FOG_COLOUR[29] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00,
    0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
};

/* The frame setup's one site that switches the per vertex ramp on: the device has no table fog,
 * so the flag becomes 1 and the level's band is copied for the vertices. The flag is the operand
 * at +0x0B. Masked: the call, the flag, the level and the copy. */
#define VERTEX_FLAG_OPERAND 0x0Bu
static const uint8_t SIG_VERTEX_FOG_ON[32] = {
    0xE8, 0x00, 0x00, 0x00, 0x00, 0x85, 0xC0, 0x75, 0x7A, 0xC7, 0x05, 0x00, 0x00, 0x00, 0x00, 0x01,
    0x00, 0x00, 0x00, 0x8B, 0x15, 0x00, 0x00, 0x00, 0x00, 0x8B, 0x82, 0x18, 0x02, 0x00, 0x00, 0xA3,
};
static const uint8_t MSK_VERTEX_FOG_ON[32] = {
    0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
};

typedef struct drawn_cells {
    uint32_t start;    /* f32, the device's band */
    uint32_t end;
    uint32_t red;      /* u32 each, 0..255 */
    uint32_t green;
    uint32_t blue;
    uint32_t target;   /* f32, the ramp's */
    uint32_t left;
    uint32_t vertex;   /* i32, the per vertex ramp is on */
} drawn_cells_t;

typedef struct drawn_sample {
    bool     read;
    bool     vertex;
    float    start;
    float    end;
    uint32_t colour[3];
    uint32_t fog_bit;
    float    left;
    float    target;
} drawn_sample_t;

typedef struct drawn_line {
    bool           used;
    bool           first_taken;
    const char    *whose;
    int32_t        command;
    uint16_t       sequence;
    uint32_t       first_at;
    drawn_sample_t first;
} drawn_line_t;

typedef struct drawn_state {
    bool          attempted;
    bool          bound;
    const char   *why;
    drawn_cells_t cells;
    uint32_t      substep;
    uint32_t      noted;
    uint32_t      started;
    uint32_t      written;
    drawn_line_t  line[DRAWN_LINES];
} drawn_state_t;

static drawn_state_t drawn;

/* A function a call named, proved where it stands: its head the one the engine shipped or a
 * branch another module put there, and the bytes behind it as shipped wherever the mask says so. */
static bool proved_behind(uintptr_t address, const uint8_t *head, size_t head_size,
                          const uint8_t *tail, const uint8_t *mask, size_t tail_size)
{
    uint8_t seen[RAMP_HEAD_BYTES + sizeof SIG_RAMP_FOG_TAIL];
    size_t  i;

    if (address == 0u || head_size + tail_size > sizeof seen ||
        !memory_try_read(address, seen, head_size + tail_size)) {
        return false;
    }
    if (seen[0] != BRANCH_OPCODE && memcmp(seen, head, head_size) != 0) {
        return false;
    }
    for (i = 0; i < tail_size; ++i) {
        if ((mask == NULL || mask[i] != 0u) && seen[head_size + i] != tail[i]) {
            return false;
        }
    }
    return true;
}

static bool masked_equal(uintptr_t address, const uint8_t *sig, const uint8_t *mask, size_t size)
{
    uint8_t seen[sizeof SIG_FOG_COLOUR + sizeof SIG_ARM_FOG_START];
    size_t  i;

    if (address == 0u || size > sizeof seen || !memory_try_read(address, seen, size)) {
        return false;
    }
    for (i = 0; i < size; ++i) {
        if ((mask == NULL || mask[i] != 0u) && seen[i] != sig[i]) {
            return false;
        }
    }
    return true;
}

/* The device's cell an edge command ends in: arm, edge function, leaf setter, its store. */
static bool edge_cell(int32_t command, const uint8_t *arm_sig, const uint8_t *arm_mask,
                      const uint8_t *tail, const uint8_t *tail_mask, size_t tail_size,
                      uint32_t *cell)
{
    uintptr_t arm = mp_level_state_bind_director_arm(command);
    uintptr_t edge;
    uintptr_t leaf;
    uint8_t   shape[LEAF_STORE_RET + 1u];

    if (!masked_equal(arm, arm_sig, arm_mask, sizeof SIG_ARM_FOG_START)) {
        return false;
    }
    edge = mp_level_state_bind_callee(arm + ARM_EDGE_RETURN);
    if (!proved_behind(edge, EDGE_HEAD, EDGE_HEAD_BYTES, tail, tail_mask, tail_size)) {
        return false;
    }
    leaf = mp_level_state_bind_callee(edge + EDGE_CALL_RETURN);
    if (leaf == 0u || !memory_try_read(leaf, shape, sizeof shape) ||
        memcmp(shape, LEAF_STORE_HEAD, sizeof LEAF_STORE_HEAD) != 0 ||
        shape[LEAF_STORE_RET] != 0xC3u || !memory_read_u32(leaf + LEAF_STORE_CELL, cell)) {
        return false;
    }
    return memory_is_inside_image((uintptr_t)*cell, sizeof(float));
}

static bool ramp_cells(drawn_cells_t *cells)
{
    uintptr_t arm = mp_level_state_bind_director_arm(COMMAND_GREEN);
    uintptr_t ramp;
    uintptr_t colour;

    if (!masked_equal(arm, SIG_ARM_FOG_GREEN, NULL, sizeof SIG_ARM_FOG_GREEN)) {
        return false;
    }
    ramp = mp_level_state_bind_callee(arm + ARM_GREEN_RETURN);
    if (!proved_behind(ramp, RAMP_HEAD, RAMP_HEAD_BYTES, SIG_RAMP_FOG_TAIL, MSK_RAMP_FOG_TAIL,
                       sizeof SIG_RAMP_FOG_TAIL) ||
        !memory_read_u32(ramp + RAMP_TARGET_OPERAND, &cells->target) ||
        !memory_read_u32(ramp + RAMP_LEFT_OPERAND, &cells->left)) {
        return false;
    }
    colour = mp_level_state_bind_callee(ramp + RAMP_COLOUR_RETURN);
    return masked_equal(colour, SIG_FOG_COLOUR, MSK_FOG_COLOUR, sizeof SIG_FOG_COLOUR) &&
           memory_read_u32(colour + COLOUR_RED, &cells->red) &&
           memory_read_u32(colour + COLOUR_GREEN, &cells->green) &&
           memory_read_u32(colour + COLOUR_BLUE, &cells->blue) &&
           memory_is_inside_image((uintptr_t)cells->red, 4u) &&
           memory_is_inside_image((uintptr_t)cells->green, 4u) &&
           memory_is_inside_image((uintptr_t)cells->blue, 4u) &&
           memory_is_inside_image((uintptr_t)cells->target, 4u) &&
           memory_is_inside_image((uintptr_t)cells->left, 4u);
}

static bool bound(void)
{
    drawn_cells_t cells;
    uintptr_t     site;

    if (drawn.attempted || mp_level_state_bind_director_entry() == 0u) {
        return drawn.bound;
    }
    drawn.attempted = true;
    memset(&cells, 0, sizeof cells);
    if (!edge_cell(COMMAND_START, SIG_ARM_FOG_START, MSK_ARM_FOG_START, SIG_FOG_START_TAIL,
                   MSK_FOG_START_TAIL, sizeof SIG_FOG_START_TAIL, &cells.start) ||
        !edge_cell(COMMAND_END, SIG_ARM_FOG_END, MSK_ARM_FOG_END, SIG_FOG_END_TAIL,
                   MSK_FOG_END_TAIL, sizeof SIG_FOG_END_TAIL, &cells.end)) {
        drawn.why = "the director's commands 6 and 7 do not lead to the device's band";
    } else if (!ramp_cells(&cells)) {
        drawn.why = "the director's command 11 does not lead to the ramp and the colour";
    } else {
        site = signature_find_unique(SIG_VERTEX_FOG_ON, MSK_VERTEX_FOG_ON,
                                     sizeof SIG_VERTEX_FOG_ON);
        if (site == 0u || !memory_read_u32(site + VERTEX_FLAG_OPERAND, &cells.vertex) ||
            !memory_is_inside_image((uintptr_t)cells.vertex, 4u)) {
            drawn.why = "the frame setup's switch of the per vertex ramp did not resolve";
        }
    }
    if (drawn.why != NULL) {
        log_warning("the fog as drawn is NOT read, so the lines after a fog command say nothing "
                    "of the picture: %s", drawn.why);
        return false;
    }
    drawn.cells = cells;
    drawn.bound = true;
    log_info("the fog as drawn is read from: the device's band at %08X and %08X, the colour at "
             "%08X, %08X and %08X, the ramp's target at %08X and time left at %08X, and the per "
             "vertex flag at %08X",
             (unsigned)cells.start, (unsigned)cells.end, (unsigned)cells.red,
             (unsigned)cells.green, (unsigned)cells.blue, (unsigned)cells.target,
             (unsigned)cells.left, (unsigned)cells.vertex);
    return true;
}

static void sample(drawn_sample_t *out)
{
    const drawn_cells_t *c = &drawn.cells;
    uint32_t             world = mp_level_state_bind_world();
    int32_t              vertex = 0;
    uint32_t             flags = 0;
    bool                 read;

    memset(out, 0, sizeof *out);
    if (!bound() || world == 0u) {
        return;
    }
    read = memory_try_read((uintptr_t)c->vertex, &vertex, sizeof vertex) &&
           memory_try_read((uintptr_t)world + WORLD_RENDER_FLAGS, &flags, sizeof flags) &&
           memory_try_read((uintptr_t)c->red, &out->colour[0], sizeof out->colour[0]) &&
           memory_try_read((uintptr_t)c->green, &out->colour[1], sizeof out->colour[1]) &&
           memory_try_read((uintptr_t)c->blue, &out->colour[2], sizeof out->colour[2]) &&
           memory_try_read((uintptr_t)c->left, &out->left, sizeof out->left) &&
           memory_try_read((uintptr_t)c->target, &out->target, sizeof out->target);
    out->vertex = vertex != 0;
    /* The band the picture is drawn with: the device's pair, or the level's in the vertex
     * regime. */
    if (out->vertex) {
        read = read &&
               memory_try_read((uintptr_t)world + WORLD_FOG_START, &out->start,
                               sizeof out->start) &&
               memory_try_read((uintptr_t)world + WORLD_FOG_END, &out->end, sizeof out->end);
    } else {
        read = read && memory_try_read((uintptr_t)c->start, &out->start, sizeof out->start) &&
               memory_try_read((uintptr_t)c->end, &out->end, sizeof out->end);
    }
    out->fog_bit = flags & WORLD_FOG_BIT;
    out->read    = read;
}

void mp_level_state_drawn_note(const char *whose, int32_t command, uint16_t sequence)
{
    size_t i;

    ++drawn.noted;
    if (drawn.started >= DRAWN_LINES) {
        return;
    }
    for (i = 0; i < DRAWN_LINES; ++i) {
        if (!drawn.line[i].used) {
            memset(&drawn.line[i], 0, sizeof drawn.line[i]);
            drawn.line[i].used     = true;
            drawn.line[i].whose    = whose != NULL ? whose : "a";
            drawn.line[i].command  = command;
            drawn.line[i].sequence = sequence;
            ++drawn.started;
            return;
        }
    }
}

static void write_line(const drawn_line_t *line, const drawn_sample_t *later)
{
    const drawn_sample_t *a = &line->first;

    ++drawn.written;
    if (!a->read || !later->read) {
        log_info("the fog as drawn after %s command %d (seq %u): the fog's cells could not be "
                 "read, %s", line->whose, (int)line->command, (unsigned)line->sequence,
                 drawn.why != NULL ? drawn.why : "no level was open");
        return;
    }
    log_info("the fog as drawn after %s command %d (seq %u, %s regime): start %.2f end %.2f "
             "colour %02X%02X%02X fog bit %u ramp left %.2f target %.2f; one second later start "
             "%.2f end %.2f colour %02X%02X%02X fog bit %u",
             line->whose, (int)line->command, (unsigned)line->sequence,
             a->vertex ? "vertex" : "device", (double)a->start, (double)a->end,
             (unsigned)(a->colour[0] & 0xFFu), (unsigned)(a->colour[1] & 0xFFu),
             (unsigned)(a->colour[2] & 0xFFu), (unsigned)a->fog_bit, (double)a->left,
             (double)a->target, (double)later->start, (double)later->end,
             (unsigned)(later->colour[0] & 0xFFu), (unsigned)(later->colour[1] & 0xFFu),
             (unsigned)(later->colour[2] & 0xFFu), (unsigned)later->fog_bit);
}

void mp_level_state_drawn_tick(void)
{
    size_t i;

    ++drawn.substep;
    for (i = 0; i < DRAWN_LINES; ++i) {
        drawn_line_t  *line = &drawn.line[i];
        drawn_sample_t later;

        if (!line->used) {
            continue;
        }
        if (!line->first_taken) {
            sample(&line->first);
            line->first_taken = true;
            line->first_at    = drawn.substep;
            continue;
        }
        if (drawn.substep - line->first_at < SECOND_LATER) {
            continue;
        }
        sample(&later);
        write_line(line, &later);
        line->used = false;
    }
}

void mp_level_state_drawn_report(bool host)
{
    drawn_sample_t now;

    sample(&now);
    log_info("  the fog as drawn (%s): %u fog command(s) noted, %u line(s) written, %u past the "
             "first %u; the cells %s, the regime now %s",
             host ? "host" : "client", (unsigned)drawn.noted, (unsigned)drawn.written,
             (unsigned)(drawn.noted > drawn.started ? drawn.noted - drawn.started : 0u),
             (unsigned)DRAWN_LINES, drawn.bound ? "bound" : "NOT bound",
             !now.read ? "unread" : (now.vertex ? "vertex" : "device"));
}
