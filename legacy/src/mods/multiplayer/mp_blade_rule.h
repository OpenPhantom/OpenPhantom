/* mp_blade_rule.h: which sabre vectors a Jedi body keeps after its spawn, how a puppet's blade
 * length moves, and the four vertices a far body's blade is drawn from, pure.
 *
 * The blade is a node of the model, and the model is the asset's: two bodies wearing one asset
 * draw one blade mesh. A spawn of hero 0 or 1 reads that mesh as it stands into its block, four
 * vertices and two tip deltas, and every length after that is the hilt plus the deltas times the
 * length. A mesh another body has folded reads as a blade of no length, and a mesh another body
 * has half grown reads as a short one; either stays so until the level ends. So a Jedi body keeps
 * the longest reading anybody has of its asset: its own, another standing body's, or the book's,
 * which holds the longest reading of each asset name for the life of the process. Nothing writes
 * a blade longer than full, and an asset's hilt is the same bytes in every reading of it, so the
 * longest reading with that hilt is the full blade. A reading with another hilt under the same
 * name is not taken.
 *
 * The same module answers what a puppet's blade does each substep. No far body calls the engine's
 * length tick: its setter writes the four vertices into the mesh of the model the body draws, and
 * that mesh is the asset's, so a far body's length written there is every body's length. A far
 * Jedi's length is stepped in its own block instead, and its blade is drawn from four vertices
 * worked out of that block for the length of its own draw.
 */
#ifndef MULTIPLAYER_MP_BLADE_RULE_H
#define MULTIPLAYER_MP_BLADE_RULE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The block's own layout at +0x1C8: vertices 0 and 1 are the hilt, 2 and 3 the tip, then the two
 * tip deltas, vertex 2 minus 0 and vertex 3 minus 1. */
typedef struct mp_blade_vectors {
    float vert[4][3];
    float delta[2][3];
} mp_blade_vectors_t;

#define MP_BLADE_VECTOR_BYTES 0x48u
_Static_assert(sizeof(mp_blade_vectors_t) == MP_BLADE_VECTOR_BYTES,
               "the block's layout at +0x1C8: four vertices, then two deltas");

/* An asset name as the actor header carries it, terminator included. */
#define MP_BLADE_NAME_BYTES 32u

/* How many asset names the book holds. Two heroes carry a blade, and every other asset with one is
 * a character riding a hero's slot; eight is room for a session's worth of them. */
#define MP_BLADE_BOOK_SIZE 8u

/* No hero: what a block that did not read is taken for. Not 0 or 1, so never a Jedi. */
#define MP_BLADE_NO_HERO 0xFFFFFFFFu

typedef struct mp_blade_book_entry {
    char               name[MP_BLADE_NAME_BYTES];
    mp_blade_vectors_t vectors;
} mp_blade_book_entry_t;

/* The longest reading of each asset name, never forgotten: a name names a file, and the file is
 * the same in every level. */
typedef struct mp_blade_book {
    mp_blade_book_entry_t entry[MP_BLADE_BOOK_SIZE];
    size_t                known;
    uint32_t              raised;    /* entries a longer reading with the same hilt replaced */
    uint32_t              refused;   /* names the full book had no room for */
} mp_blade_book_t;

/* A block as the rule sees it. */
typedef struct mp_blade_body {
    const char        *name;    /* its actor's file name; empty or NULL when it did not read */
    uint32_t           hero;    /* +0x6C, the hero index its spawn was given */
    mp_blade_vectors_t vectors;
} mp_blade_body_t;

typedef enum mp_blade_source {
    MP_BLADE_NONE,     /* not a Jedi block: its spawn read nothing, and nothing is given */
    MP_BLADE_BOOK,     /* the book's reading of the asset */
    MP_BLADE_DONOR,    /* another block of the same asset had the longest reading */
    MP_BLADE_OWN,      /* its own reading was the longest */
    MP_BLADE_FOLDED    /* nobody has a reading with a blade in it: its own is kept */
} mp_blade_source_t;

typedef enum mp_blade_learned {
    MP_BLADE_LEARNED_NEW,      /* a name the book did not know */
    MP_BLADE_LEARNED_RAISED,   /* a longer reading with the same hilt */
    MP_BLADE_LEARNED_KEPT,     /* the book's reading is as long, or nothing was offered */
    MP_BLADE_LEARNED_FOREIGN,  /* another hilt under a name the book knows: not taken */
    MP_BLADE_LEARNED_REFUSED   /* no name, no blade in the reading, or no room */
} mp_blade_learned_t;

typedef struct mp_blade_choice {
    mp_blade_source_t  source;
    mp_blade_learned_t learned;   /* what the book did with the choice */
    size_t             donor;     /* the index into `others` for MP_BLADE_DONOR */
    uint32_t           foreign;   /* readings of the asset with another hilt, not taken */
    mp_blade_vectors_t vectors;   /* what the block carries after the choice */
} mp_blade_choice_t;

/* Whether hero index `hero` is one whose spawn reads the blade mesh into the block: 0 or 1. */
bool mp_blade_rule_jedi(uint32_t hero);

/* Whether two asset names name one asset: both non empty, compared lower case up to the first
 * zero byte and within MP_BLADE_NAME_BYTES - 1 characters. The book's key and the donor's test. */
bool mp_blade_rule_same_asset(const char *a, const char *b);

/* Whether a reading has a blade in it: the hilt and both deltas finite, both deltas at least 0.01
 * long and the two hilt vertices at least 0.001 apart. The heroes' blades are 0.388 to 0.427 long
 * and their hilts 0.022 to 0.024 wide; a folded blade has deltas of exactly 0, and a block no
 * spawn read is 0 everywhere. A blade one substep into growing passes: that is why the book and
 * the other bodies are asked for a longer one. */
bool mp_blade_rule_usable(const mp_blade_vectors_t *vectors);

/* Whether a choice puts other vectors into the block than its spawn read. */
bool mp_blade_rule_writes(mp_blade_source_t source);

/* The book's reading of `name`, or NULL. */
const mp_blade_vectors_t *mp_blade_book_find(const mp_blade_book_t *book, const char *name);

/* Offer the book a reading of `name`. A new name is kept while there is room; a known one is
 * replaced only by a reading with the same hilt, bit for bit, and both deltas longer. It never
 * shortens, and a reading with another hilt is never taken. */
mp_blade_learned_t mp_blade_book_learn(mp_blade_book_t *book, const char *name,
                                       const mp_blade_vectors_t *vectors);

/* The vectors `self` keeps after its spawn. The candidates are the book's reading of its asset,
 * every block in `others` that is a Jedi block of the same asset, and its own, each only with a
 * blade in it. The hilt they are held against is the book's, or, with none, that of the longest
 * candidate; a candidate with another hilt is counted and passed over. Of the rest the longest
 * wins, both deltas longer than the one before it, so on equal length the book comes first, then
 * the others in their order, then its own. What won is offered to the book. */
void mp_blade_rule_choose(const mp_blade_body_t *self, const mp_blade_body_t *others,
                          size_t count, mp_blade_book_t *book, mp_blade_choice_t *out);

/* What a puppet's blade does this substep. Neither answer that steps calls the engine's setter:
 * it writes the mesh the body's render handle draws, which is shared by every body of that model.
 * The two stepping answers are kept apart because the report counts a worn body's steps and a
 * body in its hero's own model separately. */
typedef enum mp_blade_tick {
    MP_BLADE_TICK_STEP,        /* a Jedi body in its hero's own model: its length is stepped in its
                                * block, and its blade is drawn from vertices of its own */
    MP_BLADE_TICK_WORN,        /* a worn body whose hero carries no blade: withheld whole */
    MP_BLADE_TICK_BLADELESS,   /* its spawn read no blade, or its rig has no blade node */
    MP_BLADE_TICK_WORN_STEP    /* a Jedi body in a borrowed model: stepped in its block the same
                                * way, and drawn by whoever draws the borrowed model */
} mp_blade_tick_t;

typedef struct mp_blade_tick_facts {
    bool     worn;   /* the body's render handle draws another model than its actor */
    uint32_t hero;   /* the record's +0x6C */
    uint32_t node;   /* the record's +0x4C, the blade node its spawn found, 0 for none */
} mp_blade_tick_facts_t;

/* The one answer, in this order: a worn body, which steps its own length when its hero carries a
 * blade at all and is withheld whole when it does not; a body without a blade; and otherwise a
 * Jedi body whose length is stepped in its block. Whether the local player holds a sabre of the
 * same model is not asked: no far body writes the mesh any more, so the mesh has no owner to ask
 * about.
 *
 * A worn body's blade node is not asked for. The overlay puts that word back to 0 when it dresses
 * a body, because the node it named belongs to the hero's rig, so the hero index is the only
 * thing left that says whether the block carries a blade at all. */
mp_blade_tick_t mp_blade_rule_tick(const mp_blade_tick_facts_t *facts);

/* Whether the light tick runs whole for that answer rather than only as far as its release: only
 * for a Jedi body in its hero's own model, whose blade node has the sphere the light is placed at.
 * A worn body's node names a joint of the hero's rig, which the borrowed model does not draw. */
bool mp_blade_rule_light_whole(mp_blade_tick_t verdict);

/* The four vertices of a blade mesh, twelve floats, in the order the mesh holds them. */
#define MP_BLADE_VERT_FLOATS 12u

/* The vertices the engine's length setter writes for `size`, out of the block's vectors: the two
 * hilt vertices as they stand, the first tip the first hilt vertex plus the first delta times the
 * length, the second tip the same from the second. False, with nothing written, when an argument
 * is missing or a number going in or coming out is not finite. */
bool mp_blade_rule_verts(const mp_blade_vectors_t *vectors, float size,
                         float out[MP_BLADE_VERT_FLOATS]);

/* Whether a far body's blade is drawn from vertices of its own: a body in its hero's own model,
 * the hero a Jedi, and the blade mesh the four vertices the setter writes. A worn body is drawn
 * by whoever put the borrowed model on it. */
bool mp_blade_rule_draws_own(bool worn, bool jedi, uint32_t mesh_verts);

#endif /* MULTIPLAYER_MP_BLADE_RULE_H */
