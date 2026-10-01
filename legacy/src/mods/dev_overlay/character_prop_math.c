/* character_prop_math.c: where the borrowed weapon lands, as arithmetic and nothing else.
 *
 * This is the half of character_prop.c that reads no engine memory, holds no state and can be
 * driven without a game, which is why it is a file of its own and why the unit test binds to it.
 * Three of the routines mirror an engine one exactly, so a reader can check them against the
 * disassembly rather than against an intention; the inverse and the sphere merge are ours, because
 * the engine has no routine for either.
 *
 * A matrix is twelve floats: three basis rows and then the translation. Row vectors throughout, so
 * `compose(out, parent, local)` reads as "apply local, then parent".
 */
#include "character_prop.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#define MATRIX_FLOATS   12u

/* How far a rest chain may depart from a rotation before its transpose stops being its inverse.
 * A hero rig measures zero: every bind rotation between its root and its right hand is zero. */
#define SKEW_TOLERANCE  0.001f

/* Both compute into a local first, so an output that is also an input is defined. The engine's own
 * two routines are not written that way. */
void character_prop_mat_compose(float out[12], const float parent[12], const float local[12])
{
    float    result[MATRIX_FLOATS];
    uint32_t row;
    uint32_t k;

    for (row = 0; row < 4u; ++row) {
        for (k = 0; k < 3u; ++k) {
            float sum = local[row * 3u + 0u] * parent[0u + k] +
                        local[row * 3u + 1u] * parent[3u + k] +
                        local[row * 3u + 2u] * parent[6u + k];

            result[row * 3u + k] = (row == 3u) ? (sum + parent[9u + k]) : sum;
        }
    }
    memcpy(out, result, sizeof result);
}

void character_prop_mat_point(float out[3], const float point[3], const float matrix[12])
{
    float    result[3];
    uint32_t k;

    for (k = 0; k < 3u; ++k) {
        result[k] = point[0] * matrix[0u + k] + point[1] * matrix[3u + k] +
                    point[2] * matrix[6u + k] + matrix[9u + k];
    }
    memcpy(out, result, sizeof result);
}

float character_prop_mat_skew(const float matrix[12])
{
    float    worst = 0.0f;
    uint32_t i;
    uint32_t j;

    for (i = 0; i < 3u; ++i) {
        for (j = 0; j < 3u; ++j) {
            float dot = matrix[i * 3u + 0u] * matrix[j * 3u + 0u] +
                        matrix[i * 3u + 1u] * matrix[j * 3u + 1u] +
                        matrix[i * 3u + 2u] * matrix[j * 3u + 2u];
            float want = (i == j) ? 1.0f : 0.0f;
            float off = (dot > want) ? (dot - want) : (want - dot);

            if (off > worst) {
                worst = off;
            }
        }
    }
    return worst;
}

bool character_prop_mat_invert(float out[12], const float matrix[12])
{
    uint32_t j;
    uint32_t k;

    if (character_prop_mat_skew(matrix) > SKEW_TOLERANCE) {
        return false;
    }
    for (j = 0; j < 3u; ++j) {
        for (k = 0; k < 3u; ++k) {
            out[j * 3u + k] = matrix[k * 3u + j];
        }
    }
    for (k = 0; k < 3u; ++k) {
        out[9u + k] = -(matrix[9u] * matrix[k * 3u + 0u] + matrix[10u] * matrix[k * 3u + 1u] +
                        matrix[11u] * matrix[k * 3u + 2u]);
    }
    return true;
}

/* The engine's own re-parenting factor. Three additions, and they are the whole placement.
 *
 * `rdThing_buildWorldMatrices` builds a node's local transform out of three fields and its parent's
 * one, in this order and no other:
 *
 *     mat34_identityAt(L, &node->pivot)        L  = translate(pivot)
 *     mat34_postMultiply(L, &mat[node->idx])   L  = L * restMatrix
 *     if (node->parent) L.t -= parent->pivot
 *     bapmap_matMul3(&mat[node->idx], W_parent, L)
 *
 * so `W_node = translate(pivot) * rest * translate(-parent->pivot) * W_parent`. The last factor is
 * the parent's, not the node's: a node's matrix carries points out of that node's MESH space, and
 * `-parent->pivot` is what steps from the parent's joint back to the parent mesh's own zero.
 *
 * Draw the reference model against `out * H` and its weapon subtree comes out as
 *
 *     translate(pivot) * rest * translate(-reference_pivot) * C * out * H
 *
 * with `C` the reference rest chain to its hand. Setting `out = inverse(C) * translate(reference
 * pivot - target_pivot)` cancels `C` and leaves `translate(-target_pivot) * H`, which is the
 * matrix the borrowed rig itself would build for that node. Every other term is the weapon's own
 * and none of them is touched, which is why the same matrix serves all twelve weapon rows.
 *
 * A translation applied AFTER a matrix is one addition into its translation row, which is all
 * this is. */
void character_prop_mat_reparent(float out[12], const float reference_inverse[12],
                                 const float reference_pivot[3], const float target_pivot[3])
{
    uint32_t k;

    for (k = 0; k < 9u; ++k) {
        out[k] = reference_inverse[k];
    }
    for (k = 0; k < 3u; ++k) {
        out[9u + k] = reference_inverse[9u + k] + reference_pivot[k] - target_pivot[k];
    }
}

float character_prop_mat_scale(const float matrix[12])
{
    return (float)sqrt((double)(matrix[0] * matrix[0] + matrix[1] * matrix[1] +
                                matrix[2] * matrix[2]));
}

/* The smallest sphere containing two, grown one part at a time. A sphere already inside the other
 * changes nothing; otherwise the answer spans both along the line between the centres, so its
 * radius is half that span and its centre slides onto the line.
 *
 * The division is safe without a guard: at zero distance one of the two containment tests is always
 * true, because one radius is then not smaller than the other. */
void character_prop_sphere_merge(float centre[3], float *radius, const float other[3],
                                 float other_radius)
{
    float    delta[3];
    float    distance = 0.0f;
    float    merged;
    float    step;
    uint32_t k;

    for (k = 0; k < 3u; ++k) {
        delta[k] = other[k] - centre[k];
        distance += delta[k] * delta[k];
    }
    distance = (float)sqrt((double)distance);

    if (distance + other_radius <= *radius) {
        return;
    }
    if (distance + *radius <= other_radius) {
        memcpy(centre, other, 3u * sizeof centre[0]);
        *radius = other_radius;
        return;
    }
    merged = (distance + *radius + other_radius) * 0.5f;
    step = (merged - *radius) / distance;
    for (k = 0; k < 3u; ++k) {
        centre[k] += delta[k] * step;
    }
    *radius = merged;
}
