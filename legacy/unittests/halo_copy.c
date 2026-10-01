/* halo_copy.c: the node vertex copy halo_draw is given, checked against structures of our own.
 *
 * The engine's own routine writes nothing on either of its failure arms and halo_draw then reads
 * a buffer nothing wrote, which was a saber-coloured bar out of Obi-Wan's hand. So the claim that
 * matters here is that every arm that does not copy leaves twelve zeros behind, and that a mesh
 * with more vertices than the buffer holds does not write past its fourth.
 */
#include "unittest.h"

#include "halo_copy.h"

#include <stdint.h>
#include <string.h>

/* The engine's structures, laid out by the offsets in halo_copy.h and nothing else. */
static uint8_t obj_bytes[HALO_OBJ_THING + 4];
static uint8_t thing_bytes[HALO_THING_MODEL + 4];
static uint8_t model_bytes[HALO_MODEL_NODES + 4];
static uint8_t node_bytes[2 * HALO_NODE_STRIDE];
static uint8_t mesh_bytes[2 * HALO_MESH_STRIDE];
static float   verts[6 * 3];

static void put32(uint8_t *at, uint32_t value)
{
    memcpy(at, &value, sizeof value);
}

static void build(uint32_t node_count, int32_t mesh_index_of_node_0, uint32_t vert_count)
{
    unsigned i;

    memset(obj_bytes, 0, sizeof obj_bytes);
    memset(thing_bytes, 0, sizeof thing_bytes);
    memset(model_bytes, 0, sizeof model_bytes);
    memset(node_bytes, 0, sizeof node_bytes);
    memset(mesh_bytes, 0, sizeof mesh_bytes);
    for (i = 0; i < 18; ++i) {
        verts[i] = (float)(i + 1);
    }
    put32(obj_bytes + HALO_OBJ_THING, (uint32_t)(uintptr_t)thing_bytes);
    put32(thing_bytes + HALO_THING_MODEL, (uint32_t)(uintptr_t)model_bytes);
    put32(model_bytes + HALO_MODEL_NODE_COUNT, node_count);
    put32(model_bytes + HALO_MODEL_NODES, (uint32_t)(uintptr_t)node_bytes);
    put32(model_bytes + HALO_MODEL_MESHES, (uint32_t)(uintptr_t)mesh_bytes);
    put32(node_bytes + HALO_NODE_MESH_INDEX, (uint32_t)mesh_index_of_node_0);
    put32(mesh_bytes + HALO_MESH_VERT_COUNT, vert_count);
    put32(mesh_bytes + HALO_MESH_VERTS, (uint32_t)(uintptr_t)verts);
}

static int all_zero(const float *out)
{
    unsigned i;

    for (i = 0; i < HALO_VERTS_FLOATS; ++i) {
        if (out[i] != 0.0f) {
            return 0;
        }
    }
    return 1;
}

static void test_copy(void)
{
    float     out[HALO_VERTS_FLOATS + 3];
    uintptr_t model = 0;

    ut_section("the copy");

    build(1u, 0, 4u);
    memset(out, 0x55, sizeof out);
    ut_check(halo_copy_node_verts((uintptr_t)obj_bytes, 0u, out, &model) == HALO_COPY_OK,
             "a node with a four vertex mesh is copied");
    ut_check(out[0] == 1.0f && out[6] == 7.0f && out[11] == 12.0f,
             "the four vertices arrive in order, twelve floats");
    ut_check(model == (uintptr_t)model_bytes, "the model's address is handed back for the log");

    build(1u, 0, 6u);
    memset(out, 0, sizeof out);
    out[12] = 99.0f;
    ut_check(halo_copy_node_verts((uintptr_t)obj_bytes, 0u, out, NULL) == HALO_COPY_OK,
             "a six vertex mesh is accepted");
    ut_check(out[11] == 12.0f && out[12] == 99.0f,
             "only the four vertices the buffer holds are written, the thirteenth float is not");

    build(1u, 0, 2u);
    memset(out, 0x55, sizeof out);
    ut_check(halo_copy_node_verts((uintptr_t)obj_bytes, 0u, out, NULL) == HALO_COPY_OK,
             "a two vertex mesh is copied");
    ut_check(out[5] == 6.0f && out[6] == 0.0f && out[11] == 0.0f,
             "the floats past a short mesh are zero, not whatever the buffer held");
}

static void test_failure_arms(void)
{
    float     out[HALO_VERTS_FLOATS];
    uintptr_t model = 1;

    ut_section("the routine's failure arms");

    build(1u, 0, 4u);
    memset(out, 0x55, sizeof out);
    ut_check(halo_copy_node_verts((uintptr_t)obj_bytes, 1u, out, &model) ==
                 HALO_COPY_NODE_PAST_COUNT,
             "a node index equal to the count is past it, the routine's first arm");
    ut_check(all_zero(out), "and the twelve floats are zeroed");
    ut_check(model == (uintptr_t)model_bytes, "the model is still named");

    build(1u, -1, 4u);
    memset(out, 0x55, sizeof out);
    ut_check(halo_copy_node_verts((uintptr_t)obj_bytes, 0u, out, NULL) == HALO_COPY_NO_MESH,
             "a negative mesh index is the routine's second arm");
    ut_check(all_zero(out), "and the twelve floats are zeroed");

    build(1u, 0, 0u);
    memset(out, 0x55, sizeof out);
    ut_check(halo_copy_node_verts((uintptr_t)obj_bytes, 0u, out, NULL) == HALO_COPY_NO_VERTICES,
             "a mesh with no vertices is refused");
    ut_check(all_zero(out), "and the twelve floats are zeroed");

    build(1u, 0, 4u);
    put32(obj_bytes + HALO_OBJ_THING, 0u);
    memset(out, 0x55, sizeof out);
    model = 1;
    ut_check(halo_copy_node_verts((uintptr_t)obj_bytes, 0u, out, &model) == HALO_COPY_NO_MODEL,
             "an object with no thing is refused");
    ut_check(all_zero(out) && model == 0, "zeroed, and no model is named");

    memset(out, 0x55, sizeof out);
    ut_check(halo_copy_node_verts(0x10u, 0u, out, NULL) == HALO_COPY_NO_MODEL,
             "an object pointer nothing can read is refused, not faulted on");
    ut_check(all_zero(out), "and the twelve floats are zeroed");
}

int main(void)
{
    test_copy();
    test_failure_arms();
    return ut_summary("halo copy");
}
