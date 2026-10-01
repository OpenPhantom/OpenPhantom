/* spawn_ghost.c: the preview's render handle, bound, drawn and let go over a pretend engine.
 *
 * The one way the placement mode can crash the game is a handle drawn after its model is gone. So
 * these checks are about order: a handle bound in one world is let go before anything is drawn in
 * the next, a new model is bound only after the old one was let go, a model the engine refuses is
 * never drawn, and a handle let go twice is let go once.
 */
#include "unittest.h"

#include "spawn_ghost.h"

#include <string.h>

/* The pretend engine: what it was asked, in order, and what it answers. */
typedef struct engine {
    uint32_t  inits;
    uint32_t  binds;
    uint32_t  frees;
    uint32_t  draws;
    uintptr_t bound_model;     /* what the handle holds as far as the engine knows */
    bool      refuse_bind;
    bool      cull;
    uint32_t  stamp_at_draw;
    float     root_at_draw[12];
    uintptr_t model_at_draw;
    char      order[64];
} engine_t;

static engine_t eng;

static void note(char c)
{
    size_t n = strlen(eng.order);

    if (n + 1u < sizeof eng.order) {
        eng.order[n] = c;
        eng.order[n + 1u] = '\0';
    }
}

static int32_t __cdecl fake_init(void *thing, void *owner)
{
    (void)thing;
    (void)owner;
    ++eng.inits;
    note('i');
    return 1;
}

static int32_t __cdecl fake_set_model(void *thing, void *model3)
{
    (void)thing;
    ++eng.binds;
    note('b');
    if (eng.refuse_bind) {
        return 0;
    }
    eng.bound_model = (uintptr_t)model3;
    return 1;
}

static void __cdecl fake_free(void *thing)
{
    (void)thing;
    ++eng.frees;
    note('f');
    eng.bound_model = 0;
}

static void *__cdecl fake_draw(void *thing, const float *root)
{
    ++eng.draws;
    note('d');
    eng.stamp_at_draw = ((const uint32_t *)thing)[0x1Cu / 4u];
    eng.model_at_draw = eng.bound_model;
    memcpy(eng.root_at_draw, root, sizeof eng.root_at_draw);
    return eng.cull ? NULL : thing;
}

static const spawn_ghost_ops_t OPS = { &fake_init, &fake_set_model, &fake_free, &fake_draw };

static void reset(spawn_ghost_t *g)
{
    memset(&eng, 0, sizeof eng);
    memset(g, 0, sizeof *g);
}

int main(void)
{
    static spawn_ghost_t g;
    float                root[12];
    const float          at[3] = { 1.0f, 2.0f, 3.0f };

    spawn_ghost_matrix(root, at, 90.0f, 2.0f);

    ut_section("the root matrix");
    ut_near(root[0], 0.0, 1e-5, "turned 90 degrees, the right row is (0, 1, 0), scaled");
    ut_near(root[1], 2.0, 1e-5, "by the scale");
    ut_near(root[3], -2.0, 1e-5, "and the forward row is (-1, 0, 0): the engine's (-sin, cos)");
    ut_near(root[8], 2.0, 1e-5, "up keeps the scale");
    ut_check(root[9] == 1.0f && root[10] == 2.0f && root[11] == 3.0f,
             "and the translation is the place");

    ut_section("bound, drawn, let go");
    reset(&g);
    ut_check(!spawn_ghost_draw(&g, &OPS, 1u) && eng.inits == 0u,
             "nothing wanted, nothing bound and nothing drawn");
    spawn_ghost_want(&g, 0x1000u, 1u, root);
    ut_check(spawn_ghost_draw(&g, &OPS, 1u) && strcmp(eng.order, "ibd") == 0,
             "wanted: initialised, bound, drawn, in that order");
    ut_check(eng.stamp_at_draw == 0x80000000u,
             "the pose stamp is set to what the counter cannot hold before the draw");
    ut_check(eng.model_at_draw == 0x1000u && eng.root_at_draw[9] == 1.0f,
             "drawn with the wanted model and matrix");
    ut_check(spawn_ghost_draw(&g, &OPS, 1u) && strcmp(eng.order, "ibdd") == 0,
             "the next frame draws again without binding again");

    ut_section("a new world lets it go first");
    ut_check(!spawn_ghost_draw(&g, &OPS, 2u) && strcmp(eng.order, "ibddf") == 0 && !g.bound,
             "a draw in another world lets the handle go and draws nothing: the model may be gone");
    spawn_ghost_want(&g, 0x2000u, 2u, root);
    ut_check(spawn_ghost_draw(&g, &OPS, 2u) && strcmp(eng.order, "ibddfibd") == 0 &&
                 eng.model_at_draw == 0x2000u,
             "wanted in the new world, it is bound afresh and drawn with the new model");

    ut_section("another model lets the old one go before the new one is bound");
    spawn_ghost_want(&g, 0x3000u, 2u, root);
    memset(eng.order, 0, sizeof eng.order);
    ut_check(spawn_ghost_draw(&g, &OPS, 2u) && strcmp(eng.order, "fibd") == 0,
             "freed, then initialised, bound and drawn");

    ut_section("not wanted any more");
    spawn_ghost_want_none(&g);
    memset(eng.order, 0, sizeof eng.order);
    ut_check(!spawn_ghost_draw(&g, &OPS, 2u) && strcmp(eng.order, "f") == 0 && !g.bound,
             "a frame that wants none lets the handle go");

    ut_section("let go twice is let go once");
    reset(&g);
    spawn_ghost_want(&g, 0x1000u, 5u, root);
    (void)spawn_ghost_draw(&g, &OPS, 5u);
    spawn_ghost_release(&g, &OPS);
    spawn_ghost_release(&g, &OPS);
    ut_check(eng.frees == 1u && g.counters.releases == 1u, "one free for two releases");
    spawn_ghost_release(&g, NULL);
    ut_check(eng.frees == 1u, "a release with no engine calls frees nothing more");

    ut_section("a model the engine refuses");
    reset(&g);
    eng.refuse_bind = true;
    spawn_ghost_want(&g, 0x1000u, 1u, root);
    ut_check(!spawn_ghost_draw(&g, &OPS, 1u) && eng.draws == 0u && strcmp(eng.order, "ibf") == 0,
             "refused: what the bind took is freed and nothing is drawn");
    ut_check(!g.bound && g.counters.refused == 1u && !g.wanted,
             "the handle stays unbound, the refusal is counted and not asked again this frame");

    ut_section("wanted for another world");
    reset(&g);
    spawn_ghost_want(&g, 0x1000u, 3u, root);
    ut_check(!spawn_ghost_draw(&g, &OPS, 4u) && eng.inits == 0u,
             "a want of an older world binds nothing in this one");

    ut_section("culled");
    reset(&g);
    eng.cull = true;
    spawn_ghost_want(&g, 0x1000u, 1u, root);
    ut_check(!spawn_ghost_draw(&g, &OPS, 1u) && g.counters.culled == 1u && g.bound,
             "the engine's own visibility test said no: counted, and the handle kept");

    ut_section("without engine calls");
    reset(&g);
    spawn_ghost_want(&g, 0x1000u, 1u, root);
    ut_check(!spawn_ghost_draw(&g, NULL, 1u) && eng.inits == 0u, "no engine, no draw");
    spawn_ghost_want(&g, 0u, 1u, root);
    ut_check(!g.wanted, "a want of no model is no want");

    return ut_summary("spawn ghost");
}
