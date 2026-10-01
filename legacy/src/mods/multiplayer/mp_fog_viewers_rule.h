/* mp_fog_viewers_rule.h: the fog of a room, seen only by the player who stands in it.
 *
 * Layer 1, pure. One script of the shipped levels, the gas room of FEDSHIP, sets the fog for the
 * room it guards: green while the player is inside, the level's own fog once he walks out, green
 * again when he comes back. The fog is the device's and every machine has one, so in a session the
 * host playing that script for "the nearest player" turned every screen green whenever anybody
 * stood in the room. Here each machine plays the room's fog for its own player, and the host only
 * says that the script runs, that it has ended, and where the actor that runs it stands. Source's
 * env_fog_controller is the same idea: the fog is a property of the viewer, set per player.
 *
 * Which scripts. A table, not a structural rule: the gas room's first green is unconditional and
 * the level wide fog of the swamp stands behind a distance test, so no shape of a script tells the
 * two apart. A script is known by a fingerprint of its fog, taken on the level as loaded: its entry
 * count and every director entry that sets fog with its two arguments. A script the table does not
 * name is shared, which is what every fog was before this, so a shipped script that sets fog and is
 * missing from the table stays shared.
 *
 * The values are the script's. A per viewer row names the entries it reads: the two distance
 * tests, the flag that ends the script and the state it ends in, and the director entries of each
 * of the four transitions. Every machine reads them out of its own copy of the level and checks
 * each one's opcode, and the command or the target, before it believes it; a row whose entries
 * are not what it says does not bind, and that script's fog stays shared.
 *
 * The four transitions, as the script makes them and as each machine replays them:
 *
 *   start   when the host says the script runs: the room's green, in two halves a substep apart
 *           as the script's first two states give them. A viewer who is already past the leave
 *           distance when it starts sees nothing: in single player the player is always in the room
 *           when it starts, and a player across the level should not get a second of green.
 *   leave   at or past the leave distance: the ramp back, then the level's band.
 *   enter   at or inside the enter distance: the band, then the green ramp.
 *   end     when the host says the script has ended: the band the script sets at its end, once.
 *           A viewer inside the room keeps the green, as in single player, where that script's end
 *           leaves the colour alone.
 *
 * A dead player keeps his state, as does one whose position cannot be read, and each such substep
 * is counted.
 */
#ifndef MULTIPLAYER_MP_FOG_VIEWERS_RULE_H
#define MULTIPLAYER_MP_FOG_VIEWERS_RULE_H

#include "mp_level_state_fog_rule.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The director's opcode, and the bit that says an operand sits in the entry itself rather than in
 * the script's pool. The rows name their other opcodes as the export lists them: 0x107 a distance
 * test, 0x400 a change of state, 0x602 a flag test. */
#define MP_FOG_OP_DIRECTOR 0x606
#define MP_FOG_OP_DISTANCE 0x107
#define MP_FOG_OP_INLINE   0x4000
#define MP_FOG_OP_MASK     0x0FFF

/* Two opcodes whose operand is a name rather than a pool index: the head of every script carries
 * the script's four letter name, and every state label the state's. The engine forms a pool
 * address out of them as it does for any entry, and never reads it. */
#define MP_FOG_OP_HEAD  0x000
#define MP_FOG_OP_STATE 0x001

/* What one entry's operand is to this side's own copy of a script, and so whether three words of
 * the pool are read for it. Only the director's entries and the distance tests have words a row or
 * a fingerprint reads, and they are read only where all three lie inside the script's own pool:
 * the pool runs from behind the entries to the next script, and an index past it reads another
 * script's bytes or memory the level never held. */
typedef enum mp_fog_operand {
    MP_FOG_OPERAND_INLINE = 0,   /* the operand is the entry's own word */
    MP_FOG_OPERAND_POOL,         /* three words of the script's pool are read */
    MP_FOG_OPERAND_NAME,         /* a script's head or a state's label: four letters, no index */
    MP_FOG_OPERAND_UNUSED,       /* an opcode whose words no row and no fingerprint reads */
    MP_FOG_OPERAND_PAST_POOL     /* an index whose three words are not all inside the pool */
} mp_fog_operand_t;

mp_fog_operand_t mp_fog_viewers_operand(int32_t opcode, int32_t operand, uint32_t pool_words);

/* The words of a script's pool: from `pool` to `end`, the next script's record or, for the last,
 * the end of the level's script buffer. 0 when `end` does not lie past `pool`. */
uint32_t mp_fog_viewers_pool_words(uint32_t pool, uint32_t end);

/* The distance tests' comparisons, as the engine numbers them. */
#define MP_FOG_COMPARISONS 6

typedef enum mp_fog_class {
    MP_FOG_CLASS_SHARED = 0,
    MP_FOG_CLASS_PER_VIEWER
} mp_fog_class_t;

typedef enum mp_fog_role {
    MP_FOG_ROLE_END_FLAG = 0,   /* the flag test that sends the script to its end */
    MP_FOG_ROLE_END_STATE,      /* the state it sends it to */
    MP_FOG_ROLE_LEAVE_TEST,
    MP_FOG_ROLE_ENTER_TEST,
    MP_FOG_ROLE_START_FIRST,
    MP_FOG_ROLE_START_SECOND,
    MP_FOG_ROLE_LEAVE_FIRST,
    MP_FOG_ROLE_LEAVE_SECOND,
    MP_FOG_ROLE_ENTER_FIRST,
    MP_FOG_ROLE_ENTER_SECOND,
    MP_FOG_ROLE_END_FIRST,
    MP_FOG_ROLES
} mp_fog_role_t;

/* One entry a row reads: its index, the opcode it must hold, and the first operand it must hold:
 * the command of a director entry, the target of a distance test, the flag of a flag test. -1
 * checks nothing, for the state an end sends the script to, which is read. */
typedef struct mp_fog_entry {
    uint16_t entry;
    uint16_t opcode;
    int32_t  word;
    uint8_t  role;
} mp_fog_entry_t;

typedef struct mp_fog_script_row {
    const char           *name;
    uint32_t              fingerprint;
    uint32_t              entry_count;
    mp_fog_class_t        cls;
    const mp_fog_entry_t *entries;   /* NULL for a shared row */
    size_t                entry_rows;
} mp_fog_script_row_t;

size_t                     mp_fog_viewers_rows(void);
const mp_fog_script_row_t *mp_fog_viewers_row(size_t index);

/* The row a fingerprint names, or NULL. */
const mp_fog_script_row_t *mp_fog_viewers_row_of(uint32_t fingerprint);

/* One entry of a script as a side reads it: the opcode as it stands, and the words its handler
 * reads, the entry's own operand for an inline one and three words of the pool otherwise. */
typedef struct mp_fog_script_entry {
    int32_t opcode;
    int32_t word[3];
} mp_fog_script_entry_t;

typedef bool (*mp_fog_read_fn_t)(const void *script, uint32_t index, mp_fog_script_entry_t *out);

/* The fingerprint of one script: FNV-1a over its entry count and every director entry that sets
 * fog, index, command and both arguments, four little endian bytes each. 0 and `fog_commands` 0
 * for a script that sets none; false when an entry did not read. */
bool mp_fog_viewers_fingerprint(uint32_t entry_count, mp_fog_read_fn_t read, const void *script,
                                uint32_t *fingerprint, uint32_t *fog_commands);

#define MP_FOG_HALF_MAX 3u

typedef enum mp_fog_transition {
    MP_FOG_START = 0,
    MP_FOG_LEAVE,
    MP_FOG_ENTER,
    MP_FOG_END,
    MP_FOG_TRANSITIONS
} mp_fog_transition_t;

typedef struct mp_fog_half {
    uint8_t                count;
    mp_level_fog_command_t command[MP_FOG_HALF_MAX];
} mp_fog_half_t;

typedef struct mp_fog_test {
    int32_t mode;
    float   distance;
} mp_fog_test_t;

/* A per viewer row with its values read out of one side's own script. */
typedef struct mp_fog_bound {
    const mp_fog_script_row_t *row;
    int32_t                    end_state;
    mp_fog_test_t              leave;
    mp_fog_test_t              enter;
    mp_fog_half_t              half[MP_FOG_TRANSITIONS][2];
} mp_fog_bound_t;

/* Reads a per viewer row's entries out of a script. Returns how many did not hold what the row
 * says; 0 is bound. */
uint32_t mp_fog_viewers_bind(const mp_fog_script_row_t *row, uint32_t entry_count,
                             mp_fog_read_fn_t read, const void *script, mp_fog_bound_t *out);

/* The engine's own float comparison of a distance test. An unknown mode is false. */
bool mp_fog_viewers_compare(float value, int32_t mode, float against);

typedef enum mp_fog_stage {
    MP_FOG_STAGE_NEVER = 0,   /* the script has not run for this viewer */
    MP_FOG_STAGE_IN,          /* in the room's fog */
    MP_FOG_STAGE_OUT,         /* out of it, watched for the way back */
    MP_FOG_STAGE_ENDED
} mp_fog_stage_t;

typedef struct mp_fog_viewer {
    uint8_t stage;     /* mp_fog_stage_t */
    uint8_t pending;   /* the transition whose second half is due, MP_FOG_TRANSITIONS for none */
} mp_fog_viewer_t;

void mp_fog_viewers_viewer_init(mp_fog_viewer_t *viewer);

typedef struct mp_fog_input {
    bool  active;     /* the host says the script runs */
    bool  ended;      /* the host says it has ended */
    bool  dead;       /* this side's player is dead */
    bool  measured;   /* and his distance to the actor could be read */
    float distance;
} mp_fog_input_t;

typedef enum mp_fog_outcome {
    MP_FOG_NOTHING = 0,
    MP_FOG_PLAY,               /* play half `half` of transition `transition` */
    MP_FOG_STARTED_AWAY,       /* the start found the viewer past the leave distance */
    MP_FOG_ENDED_UNSEEN,       /* the end came before the start did */
    MP_FOG_UNDECIDED_DEAD,
    MP_FOG_UNDECIDED_UNMEASURED
} mp_fog_outcome_t;

typedef struct mp_fog_step {
    uint8_t outcome;       /* mp_fog_outcome_t */
    uint8_t transition;    /* mp_fog_transition_t */
    uint8_t half;          /* 0 or 1 */
    bool    green_at_end;  /* the end found the viewer in the room */
} mp_fog_step_t;

/* One substep of one viewer: what to play, and the viewer moved on. */
mp_fog_step_t mp_fog_viewers_decide(mp_fog_viewer_t *viewer, const mp_fog_bound_t *bound,
                                    const mp_fog_input_t *in);

#endif /* MULTIPLAYER_MP_FOG_VIEWERS_RULE_H */
