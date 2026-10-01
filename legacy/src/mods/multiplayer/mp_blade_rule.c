/* mp_blade_rule.c: which sabre vectors a Jedi body keeps, what a puppet's blade does, and the
 * vertices it is drawn from. See the header.
 */
#include "mp_blade_rule.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* A blade shorter than this is none, and a hilt narrower than this is a point: squared lengths of
 * 0.01 and 0.001. A blade longer than 10 is none the engine grew (the shipped ones are under half
 * a unit), and a square that overflows would otherwise read as the longest. */
#define DELTA_MIN_SQUARED 1.0e-4f
#define DELTA_MAX_SQUARED 1.0e2f
#define HILT_MIN_SQUARED  1.0e-6f

static char lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

bool mp_blade_rule_jedi(uint32_t hero)
{
    return hero == 0u || hero == 1u;
}

bool mp_blade_rule_same_asset(const char *a, const char *b)
{
    size_t i;

    if (a == NULL || b == NULL || a[0] == '\0' || b[0] == '\0') {
        return false;
    }
    for (i = 0; i + 1u < MP_BLADE_NAME_BYTES; ++i) {
        if (lower(a[i]) != lower(b[i])) {
            return false;
        }
        if (a[i] == '\0') {
            return true;
        }
    }
    return true;
}

static float squared(const float v[3])
{
    return v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
}

static bool finite3(const float v[3])
{
    return isfinite(v[0]) && isfinite(v[1]) && isfinite(v[2]);
}

bool mp_blade_rule_usable(const mp_blade_vectors_t *vectors)
{
    float width[3];
    float d0;
    float d1;
    float w;
    int   axis;

    if (vectors == NULL || !finite3(vectors->vert[0]) || !finite3(vectors->vert[1]) ||
        !finite3(vectors->delta[0]) || !finite3(vectors->delta[1])) {
        return false;
    }
    for (axis = 0; axis < 3; ++axis) {
        width[axis] = vectors->vert[1][axis] - vectors->vert[0][axis];
    }
    d0 = squared(vectors->delta[0]);
    d1 = squared(vectors->delta[1]);
    w  = squared(width);
    return d0 >= DELTA_MIN_SQUARED && d0 <= DELTA_MAX_SQUARED && d1 >= DELTA_MIN_SQUARED &&
           d1 <= DELTA_MAX_SQUARED && w >= HILT_MIN_SQUARED && isfinite(w);
}

bool mp_blade_rule_writes(mp_blade_source_t source)
{
    return source == MP_BLADE_BOOK || source == MP_BLADE_DONOR;
}

/* The two hilt vertices, bit for bit. */
static bool same_hilt(const mp_blade_vectors_t *a, const mp_blade_vectors_t *b)
{
    return memcmp(a->vert, b->vert, 2u * sizeof a->vert[0]) == 0;
}

/* Both deltas of `a` longer than those of `b`. */
static bool longer(const mp_blade_vectors_t *a, const mp_blade_vectors_t *b)
{
    return squared(a->delta[0]) > squared(b->delta[0]) &&
           squared(a->delta[1]) > squared(b->delta[1]);
}

/* The book's entry for `name`, MP_BLADE_BOOK_SIZE for none. */
static size_t entry_of(const mp_blade_book_t *book, const char *name)
{
    size_t i;

    for (i = 0; book != NULL && i < book->known && i < MP_BLADE_BOOK_SIZE; ++i) {
        if (mp_blade_rule_same_asset(book->entry[i].name, name)) {
            return i;
        }
    }
    return MP_BLADE_BOOK_SIZE;
}

const mp_blade_vectors_t *mp_blade_book_find(const mp_blade_book_t *book, const char *name)
{
    size_t index = entry_of(book, name);

    return index < MP_BLADE_BOOK_SIZE ? &book->entry[index].vectors : NULL;
}

mp_blade_learned_t mp_blade_book_learn(mp_blade_book_t *book, const char *name,
                                       const mp_blade_vectors_t *vectors)
{
    mp_blade_book_entry_t *entry;
    size_t                 index;

    if (book == NULL || name == NULL || name[0] == '\0' || !mp_blade_rule_usable(vectors)) {
        return MP_BLADE_LEARNED_REFUSED;
    }
    index = entry_of(book, name);
    if (index < MP_BLADE_BOOK_SIZE) {
        entry = &book->entry[index];
        if (!same_hilt(vectors, &entry->vectors)) {
            return MP_BLADE_LEARNED_FOREIGN;
        }
        if (!longer(vectors, &entry->vectors)) {
            return MP_BLADE_LEARNED_KEPT;
        }
        entry->vectors = *vectors;
        ++book->raised;
        return MP_BLADE_LEARNED_RAISED;
    }
    if (book->known >= MP_BLADE_BOOK_SIZE) {
        ++book->refused;
        return MP_BLADE_LEARNED_REFUSED;
    }
    entry = &book->entry[book->known++];
    memset(entry, 0, sizeof *entry);
    for (index = 0; index + 1u < MP_BLADE_NAME_BYTES && name[index] != '\0'; ++index) {
        entry->name[index] = lower(name[index]);
    }
    entry->vectors = *vectors;
    return MP_BLADE_LEARNED_NEW;
}

/* Candidate `index` of a choice, the others in their order and then the body itself, or NULL
 * when that one is no Jedi block of the same asset or has no blade in its reading. */
static const mp_blade_body_t *candidate(const mp_blade_body_t *self,
                                        const mp_blade_body_t *others, size_t count,
                                        size_t index)
{
    const mp_blade_body_t *body = index < count ? &others[index] : self;

    if (body != self && (!mp_blade_rule_jedi(body->hero) ||
                         !mp_blade_rule_same_asset(self->name, body->name))) {
        return NULL;
    }
    return mp_blade_rule_usable(&body->vectors) ? body : NULL;
}

/* The longest candidate, the first of equals, or NULL with none: the hilt the candidates are held
 * against when the book does not know the asset. */
static const mp_blade_vectors_t *longest(const mp_blade_body_t *self,
                                         const mp_blade_body_t *others, size_t count)
{
    const mp_blade_vectors_t *best = NULL;
    size_t                    i;

    for (i = 0; i <= count; ++i) {
        const mp_blade_body_t *body = candidate(self, others, count, i);

        if (body != NULL && (best == NULL || longer(&body->vectors, best))) {
            best = &body->vectors;
        }
    }
    return best;
}

void mp_blade_rule_choose(const mp_blade_body_t *self, const mp_blade_body_t *others,
                          size_t count, mp_blade_book_t *book, mp_blade_choice_t *out)
{
    const mp_blade_vectors_t *hilt;
    const mp_blade_vectors_t *best;
    size_t                    i;

    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof *out);
    out->source  = MP_BLADE_NONE;
    out->learned = MP_BLADE_LEARNED_KEPT;
    if (self == NULL) {
        return;
    }
    out->vectors = self->vectors;
    if (!mp_blade_rule_jedi(self->hero)) {
        return;
    }
    if (others == NULL) {
        count = 0u;
    }
    best        = mp_blade_book_find(book, self->name);
    hilt        = best != NULL ? best : longest(self, others, count);
    out->source = best != NULL ? MP_BLADE_BOOK : MP_BLADE_FOLDED;
    for (i = 0; i <= count; ++i) {
        const mp_blade_body_t *body = candidate(self, others, count, i);

        if (body == NULL) {
            continue;
        }
        if (!same_hilt(&body->vectors, hilt)) {
            ++out->foreign;
            continue;
        }
        if (best == NULL || longer(&body->vectors, best)) {
            best        = &body->vectors;
            out->source = i < count ? MP_BLADE_DONOR : MP_BLADE_OWN;
            out->donor  = i;
        }
    }
    if (best == NULL) {
        return;
    }
    out->vectors = *best;
    if (out->source == MP_BLADE_DONOR || out->source == MP_BLADE_OWN) {
        out->learned = mp_blade_book_learn(book, self->name, &out->vectors);
    }
}

mp_blade_tick_t mp_blade_rule_tick(const mp_blade_tick_facts_t *facts)
{
    if (facts == NULL) {
        return MP_BLADE_TICK_BLADELESS;
    }
    if (facts->worn) {
        return mp_blade_rule_jedi(facts->hero) ? MP_BLADE_TICK_WORN_STEP : MP_BLADE_TICK_WORN;
    }
    if (!mp_blade_rule_jedi(facts->hero) || facts->node == 0u) {
        return MP_BLADE_TICK_BLADELESS;
    }
    return MP_BLADE_TICK_STEP;
}

bool mp_blade_rule_light_whole(mp_blade_tick_t verdict)
{
    return verdict == MP_BLADE_TICK_STEP;
}

bool mp_blade_rule_verts(const mp_blade_vectors_t *vectors, float size,
                         float out[MP_BLADE_VERT_FLOATS])
{
    float made[MP_BLADE_VERT_FLOATS];
    int   edge;
    int   axis;

    if (vectors == NULL || out == NULL || !isfinite(size) || !finite3(vectors->vert[0]) ||
        !finite3(vectors->vert[1]) || !finite3(vectors->delta[0]) ||
        !finite3(vectors->delta[1])) {
        return false;
    }
    for (edge = 0; edge < 2; ++edge) {
        for (axis = 0; axis < 3; ++axis) {
            made[3 * edge + axis] = vectors->vert[edge][axis];
            made[6 + 3 * edge + axis] =
                vectors->delta[edge][axis] * size + vectors->vert[edge][axis];
        }
    }
    for (axis = 0; axis < (int)MP_BLADE_VERT_FLOATS; ++axis) {
        if (!isfinite(made[axis])) {
            return false;
        }
    }
    memcpy(out, made, sizeof made);
    return true;
}

bool mp_blade_rule_draws_own(bool worn, bool jedi, uint32_t mesh_verts)
{
    return !worn && jedi && mesh_verts == 4u;
}
