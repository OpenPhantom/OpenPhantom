/* mp_crate_play.h: what the two push block machines need from the engine, as a table of calls.
 *
 * Layer 1, declarations only. The host's machine and the client's machine run the protocol and
 * the rules; they never read a record or call the engine themselves. mp_crate.c hands them this
 * table filled with the engine's own functions, and the unit tests hand them one filled with a
 * small world of their own, which is how both machines are driven with no game in the process.
 */
#ifndef MULTIPLAYER_MP_CRATE_PLAY_H
#define MULTIPLAYER_MP_CRATE_PLAY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* One push block as the machines read it. */
typedef struct mp_crate_body {
    int32_t kind;
    int32_t rig_flags;
    uint8_t flags;
    uint8_t carrier;          /* the mover it rides, while carried */
    uint8_t carrier_part;
    uint8_t carrying;         /* the block riding it, while carrying */
    float   position[3];
    float   carry_offset[3];
    float   home[3];          /* where the level put it */
    float   radius;
    int32_t reveals;          /* the placement its sinking reveals, below 0 for none */
} mp_crate_body_t;

typedef struct mp_crate_engine {
    void *context;

    /* One mover by index. False for an index with no record; a record that is no push block is
     * read all the same, and the caller decides. */
    bool (*read)(void *context, uint32_t id, mp_crate_body_t *out);

    /* The host: one step of a far player's push through the engine's own push, with that
     * player's body as the one pushing, so that the body test sees the right actor. The engine's
     * answer, or -1 when the slot has no body here. `step` may be written back, as the engine
     * does when it slides a block along a wall. */
    int32_t (*push)(void *context, uint8_t slot, uint32_t id, float step[3]);

    /* A client: the tail of the engine's push, which is how a block is put somewhere from outside
     * with its floor, its carrier and its cells. The flag byte written is the merge of this side's
     * with the host's. */
    bool (*place)(void *context, uint32_t id, const float position[3], uint8_t host_flags);

    /* A client: the engine's drop with the host's input, then the tail of the push. The drop's
     * own answer: 1 is a fall, 2 a step too small to fall, 0 a refusal. */
    int32_t (*fall)(void *context, uint32_t id, const float position[3],
                    const float direction[2]);

    /* A client: the engine's sink, as a savegame runs it, with the crush left to the host. */
    bool (*sink)(void *context, uint32_t id);
} mp_crate_engine_t;

#endif /* MULTIPLAYER_MP_CRATE_PLAY_H */
