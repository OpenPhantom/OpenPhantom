/* hud_layout.c: the HUD arithmetic, checked without the game.
 *
 * The important cases are the identities, because this is an authentic-first remaster and the
 * original look has to stay exactly reachable:
 *
 *   * with squaring off and scale 1.0, NOTHING may move by a single bit, at ANY resolution;
 *   * on a 4:3 screen at scale 1.0 that holds with squaring on as well;
 *   * a rectangle that is not one of the four HUD blocks must come back untouched, because the
 *     sprite blitter draws far more than the HUD and another DLL can push its own rectangles
 *     through the same gate.
 *
 * Everything is exercised over the whole display-mode list the game offers, not over one
 * resolution, because three of the four blocks were wrong at every mode and not only at 1080p.
 */
#include "unittest.h"

#include "hud_layout.h"

#include <math.h>
#include <stdbool.h>
#include <stdio.h>

/* Every mode the game's own display list offers, smallest to largest. */
typedef struct display_mode {
    float width;
    float height;
} display_mode_t;

static const display_mode_t MODES[] = {
    { 640.0f,  480.0f}, { 720.0f,  480.0f}, { 720.0f,  576.0f}, { 800.0f,  600.0f},
    {1024.0f,  768.0f}, {1152.0f,  864.0f}, {1176.0f,  664.0f}, {1280.0f,  720.0f},
    {1280.0f,  768.0f}, {1280.0f,  800.0f}, {1280.0f,  960.0f}, {1280.0f, 1024.0f},
    {1360.0f,  768.0f}, {1366.0f,  768.0f}, {1400.0f, 1050.0f}, {1440.0f,  900.0f},
    {1440.0f, 1080.0f}, {1600.0f,  900.0f}, {1600.0f, 1024.0f}, {1600.0f, 1200.0f},
    {1680.0f, 1050.0f}, {1920.0f, 1080.0f}, {1920.0f, 1200.0f}, {1920.0f, 1440.0f},
    {2560.0f, 1440.0f}, {3620.0f, 2036.0f}
};
#define MODE_COUNT (sizeof(MODES) / sizeof(MODES[0]))

/* The tolerance the classifier works with. Repeated here on purpose: a test that imported the
 * value could not notice it being widened. */
#define CLASSIFIER_TOLERANCE 2.0f

static bool is_authored_aspect(display_mode_t mode)
{
    return 4.0f * mode.height == 3.0f * mode.width;
}

/* ---------------------------------------------------------------------------------------------
 * The four blocks exactly as the engine builds them, in the engine's own operation order so the
 * float rounding matches.
 * ------------------------------------------------------------------------------------------- */
static hud_rect_t health_bar(float w, float h)
{
    hud_rect_t r;

    r.left   = 1.0f;
    r.right  = w * 0.2f;
    r.top    = h * 0.93333334f;
    r.bottom = h - 1.0f;
    return r;
}

/* Both edges are "the matching health edge, lifted by one bar height and by one more pixel". The
 * second subtraction of 1.0 is the engine's, and forgetting it puts the recogniser a pixel out. */
static hud_rect_t force_bar(float w, float h)
{
    hud_rect_t health = health_bar(w, h);
    hud_rect_t r;

    r.left   = health.left;
    r.right  = health.right;
    r.top    = (health.top    - h * 0.06666667f) - 1.0f;
    r.bottom = (health.bottom - h * 0.06666667f) - 1.0f;
    return r;
}

static hud_rect_t weapon_icon(float w, float h)
{
    hud_rect_t r;

    r.top    = h * 0.93333334f - 1.0f;
    r.bottom = h - 1.0f;
    r.left   = w * 0.2f + 1.0f;
    r.right  = h * 0.2f + r.left;
    return r;
}

static hud_rect_t escort_bar(float w, float h)
{
    hud_rect_t r;
    float bar = w * 0.2f;

    r.top    = 0.0f;
    r.bottom = h * 0.06666667f;
    r.left   = (w - bar) / 2.0f;
    r.right  = r.left + bar;
    return r;
}

/* The health number's x, and the ammo count's x, as the engine centres them. */
static float health_number_x(float w, float h)
{
    hud_rect_t r = health_bar(w, h);

    return (r.right - r.left) * 0.5f + r.left;
}

static float ammo_number_x(float w, float h)
{
    hud_rect_t r = weapon_icon(w, h);

    return (r.right - r.left) * 0.5f + r.left;
}

/* ---------------------------------------------------------------------------------------------
 * Classification
 * ------------------------------------------------------------------------------------------- */
static void test_classification_at_every_mode(void)
{
    size_t i;
    int wrong = 0;
    int too_close = 0;

    for (i = 0; i < MODE_COUNT; ++i) {
        float w = MODES[i].width;
        float h = MODES[i].height;
        hud_rect_t health = health_bar(w, h);
        hud_rect_t force  = force_bar(w, h);
        hud_rect_t weapon = weapon_icon(w, h);
        hud_rect_t escort = escort_bar(w, h);

        if (hud_classify(&health, w, h) != HUD_BLOCK_HEALTH ||
            hud_classify(&force,  w, h) != HUD_BLOCK_FORCE  ||
            hud_classify(&weapon, w, h) != HUD_BLOCK_WEAPON ||
            hud_classify(&escort, w, h) != HUD_BLOCK_ESCORT) {
            ++wrong;
            printf("      %.0fx%.0f: %d %d %d %d\n", (double)w, (double)h,
                   (int)hud_classify(&health, w, h), (int)hud_classify(&force, w, h),
                   (int)hud_classify(&weapon, w, h), (int)hud_classify(&escort, w, h));
        }

        /* No two of the four tests may be reachable by one rectangle. The pairs that share an
         * edge are health/force (which differ only in the bottom edge) and health/weapon (which
         * differ only in the left edge), so both separations have to exceed twice the tolerance. */
        if (fabsf(health.bottom - force.bottom) <= 2.0f * CLASSIFIER_TOLERANCE ||
            weapon.left <= 2.0f * CLASSIFIER_TOLERANCE ||
            escort.bottom >= health.bottom - 2.0f * CLASSIFIER_TOLERANCE) {
            ++too_close;
        }
    }

    ut_check(wrong == 0, "all four blocks are classified correctly at every display mode");
    ut_check(too_close == 0, "no two of the four tests can be satisfied by one rectangle");
}

static void test_classification_refusals(void)
{
    hud_rect_t r = health_bar(1920.0f, 1080.0f);

    ut_check(hud_classify(&(hud_rect_t){0.0f, 1920.0f, 0.0f, 1080.0f}, 1920.0f, 1080.0f)
          == HUD_BLOCK_NONE, "a full-screen blit is not a HUD block");
    ut_check(hud_classify(&(hud_rect_t){700.0f, 900.0f, 400.0f, 500.0f}, 1920.0f, 1080.0f)
          == HUD_BLOCK_NONE, "a blit in the middle of the screen is not a HUD block");
    ut_check(hud_classify(&(hud_rect_t){1.0f, 384.0f, 100.0f, 160.0f}, 1920.0f, 1080.0f)
          == HUD_BLOCK_NONE, "a bar-shaped blit that is not on a HUD row is not a HUD block");
    /* A crosshair arm: on the bottom row by chance, but neither at the left edge nor icon-sized. */
    ut_check(hud_classify(&(hud_rect_t){940.0f, 980.0f, 1075.0f, 1079.0f}, 1920.0f, 1080.0f)
          == HUD_BLOCK_NONE, "a foreign blit on the bottom row is not a HUD block");
    ut_check(hud_classify(NULL, 1920.0f, 1080.0f) == HUD_BLOCK_NONE, "a null rectangle is refused");
    ut_check(hud_classify(&r, 0.0f, 0.0f) == HUD_BLOCK_NONE, "a zero screen is refused");
    ut_check(hud_classify(&r, -1.0f, -1.0f) == HUD_BLOCK_NONE,
          "a negative screen size is refused, which is what the engine leaves behind on shutdown");
}

static void test_number_split(void)
{
    size_t i;
    int wrong = 0;

    for (i = 0; i < MODE_COUNT; ++i) {
        float w = MODES[i].width;
        float h = MODES[i].height;

        if (hud_block_for_number(health_number_x(w, h), w) != HUD_BLOCK_HEALTH ||
            hud_block_for_number(ammo_number_x(w, h), w) != HUD_BLOCK_WEAPON) {
            ++wrong;
        }
    }

    ut_check(wrong == 0, "each HUD number is assigned to its own block at every display mode");
    ut_check(hud_block_for_number(100.0f, 0.0f) == HUD_BLOCK_NONE,
          "a number is left alone when the screen size is unusable");
}

/* ---------------------------------------------------------------------------------------------
 * The identities. These are exact: not "within a pixel", bit for bit.
 * ------------------------------------------------------------------------------------------- */
static float rect_deviation(hud_rect_t a, hud_rect_t b)
{
    float d = fabsf(a.left - b.left);

    if (fabsf(a.right - b.right) > d) {
        d = fabsf(a.right - b.right);
    }
    if (fabsf(a.top - b.top) > d) {
        d = fabsf(a.top - b.top);
    }
    if (fabsf(a.bottom - b.bottom) > d) {
        d = fabsf(a.bottom - b.bottom);
    }
    return d;
}

static float identity_deviation(float w, float h, bool square)
{
    struct { hud_rect_t rect; hud_block_t block; } blocks[4];
    float worst = 0.0f;
    float x;
    float y;
    float glyph_x = 0.8f;
    float glyph_y = 0.8f;
    int i;

    blocks[0].rect = health_bar(w, h);  blocks[0].block = HUD_BLOCK_HEALTH;
    blocks[1].rect = force_bar(w, h);   blocks[1].block = HUD_BLOCK_FORCE;
    blocks[2].rect = weapon_icon(w, h); blocks[2].block = HUD_BLOCK_WEAPON;
    blocks[3].rect = escort_bar(w, h);  blocks[3].block = HUD_BLOCK_ESCORT;

    for (i = 0; i < 4; ++i) {
        hud_rect_t after = hud_transform(&blocks[i].rect, blocks[i].block, w, h, 1.0f, square);
        float deviation = rect_deviation(after, blocks[i].rect);

        if (deviation > worst) {
            worst = deviation;
        }
    }

    x = health_number_x(w, h);
    y = health_bar(w, h).bottom;
    {
        float before_x = x;
        float before_y = y;

        hud_transform_point(&x, &y, HUD_BLOCK_HEALTH, w, h, 1.0f, square);
        if (fabsf(x - before_x) > worst) {
            worst = fabsf(x - before_x);
        }
        if (fabsf(y - before_y) > worst) {
            worst = fabsf(y - before_y);
        }
    }

    x = ammo_number_x(w, h);
    y = weapon_icon(w, h).bottom;
    {
        float before_x = x;
        float before_y = y;

        hud_transform_point(&x, &y, HUD_BLOCK_WEAPON, w, h, 1.0f, square);
        if (fabsf(x - before_x) > worst) {
            worst = fabsf(x - before_x);
        }
        if (fabsf(y - before_y) > worst) {
            worst = fabsf(y - before_y);
        }
    }

    hud_glyph_scale(&glyph_x, &glyph_y, w, h, 1.0f, square);
    if (fabsf(glyph_x - 0.8f) > worst) {
        worst = fabsf(glyph_x - 0.8f);
    }
    if (fabsf(glyph_y - 0.8f) > worst) {
        worst = fabsf(glyph_y - 0.8f);
    }

    return worst;
}

static void test_identities(void)
{
    size_t i;
    float worst_off = 0.0f;
    float worst_43 = 0.0f;
    int authored_modes = 0;

    for (i = 0; i < MODE_COUNT; ++i) {
        float deviation = identity_deviation(MODES[i].width, MODES[i].height, false);

        if (deviation > worst_off) {
            worst_off = deviation;
        }

        if (is_authored_aspect(MODES[i])) {
            ++authored_modes;
            deviation = identity_deviation(MODES[i].width, MODES[i].height, true);
            if (deviation > worst_43) {
                worst_43 = deviation;
            }
        }
    }

    ut_check(authored_modes > 0, "the mode list contains 4:3 modes to check the second identity on");
    ut_check(worst_off == 0.0f,
          "SquareHud=0 with HudScale=1.0 is bit-exact at every one of the 26 display modes");
    ut_check(worst_43 == 0.0f,
          "at 4:3 with HudScale=1.0 both SquareHud settings are bit-exact");

    /* And a rectangle nobody classified is never touched, whatever the knobs say. */
    {
        hud_rect_t before = health_bar(1920.0f, 1080.0f);
        hud_rect_t after  = hud_transform(&before, HUD_BLOCK_NONE, 1920.0f, 1080.0f, 2.0f, true);

        ut_check(rect_deviation(after, before) == 0.0f,
              "a rectangle classified as NONE is never transformed");
    }
}

/* ---------------------------------------------------------------------------------------------
 * The corrected widescreen layout
 * ------------------------------------------------------------------------------------------- */
static void test_squaring_at_16_9(void)
{
    const float w = 1920.0f;
    const float h = 1080.0f;
    hud_rect_t health = hud_transform(&(hud_rect_t){1.0f, 384.0f, 1008.0f, 1079.0f},
                                      HUD_BLOCK_HEALTH, w, h, 1.0f, true);
    hud_rect_t force_before = force_bar(w, h);
    hud_rect_t force = hud_transform(&force_before, HUD_BLOCK_FORCE, w, h, 1.0f, true);
    hud_rect_t icon_before = weapon_icon(w, h);
    hud_rect_t icon = hud_transform(&icon_before, HUD_BLOCK_WEAPON, w, h, 1.0f, true);
    hud_rect_t escort_before = escort_bar(w, h);
    hud_rect_t escort = hud_transform(&escort_before, HUD_BLOCK_ESCORT, w, h, 1.0f, true);

    ut_near(health.left, 0.75f, 0.001f, "the squared health bar starts at 0.75");
    ut_near(health.right, 288.0f, 0.001f, "the squared health bar ends at 288");

    /* The defect this whole change exists for: the force bar used to keep its full 384 px and
     * overhang the health bar underneath it by 96. */
    ut_near(force.right, 288.0f, 0.001f, "the squared force bar ends at 288, like the health "
                                             "bar under it");
    ut_near(force.bottom, 1006.0f, 0.001f, "the force bar keeps its own bottom edge");

    ut_near(icon.left, 288.75f, 0.001f, "the weapon icon follows the bar in, to 288.75");
    ut_near(icon.right - icon.left, 216.0f, 0.001f, "the weapon icon keeps its square width");
    ut_near(icon.left - health.right, 0.75f, 0.001f,
                "the authored one-pixel gap between bar and icon is scaled, not left as a hole");

    ut_near(escort.left, 816.0f, 0.001f, "the squared escort bar starts at 816");
    ut_near(escort.right, 1104.0f, 0.001f, "the squared escort bar ends at 1104");
    ut_near((escort.left + escort.right) * 0.5f, 960.0f, 0.001f,
                "the squared escort bar is still centred");
    ut_near(escort.top, 0.0f, 0.001f, "the squared escort bar still hugs the top edge");

    /* The sprite is 128x32, so a squared bar has to come out 4:1 again. */
    ut_near((health.right - health.left) / (health.bottom - health.top), 4.0f, 0.05f,
                "the squared health bar is 4:1 again");
}

static void test_numbers_land_on_their_block(void)
{
    size_t i;
    float worst = 0.0f;

    for (i = 0; i < MODE_COUNT; ++i) {
        float w = MODES[i].width;
        float h = MODES[i].height;
        hud_rect_t bar_before = health_bar(w, h);
        hud_rect_t icon_before = weapon_icon(w, h);
        hud_rect_t bar  = hud_transform(&bar_before, HUD_BLOCK_HEALTH, w, h, 1.0f, true);
        hud_rect_t icon = hud_transform(&icon_before, HUD_BLOCK_WEAPON, w, h, 1.0f, true);
        float x = health_number_x(w, h);
        float y = bar_before.bottom;
        float deviation;

        hud_transform_point(&x, &y, HUD_BLOCK_HEALTH, w, h, 1.0f, true);
        deviation = fabsf(x - (bar.left + bar.right) * 0.5f);
        if (deviation > worst) {
            worst = deviation;
        }

        x = ammo_number_x(w, h);
        y = icon_before.bottom;
        hud_transform_point(&x, &y, HUD_BLOCK_WEAPON, w, h, 1.0f, true);
        deviation = fabsf(x - (icon.left + icon.right) * 0.5f);
        if (deviation > worst) {
            worst = deviation;
        }
    }

    ut_near(worst, 0.0f, 0.002f,
                "both numbers land on the centre of their own block at every display mode");

    /* The two worked numbers, so a regression names itself. */
    {
        float x = 192.5f;
        float y = 1043.5f;

        hud_transform_point(&x, &y, HUD_BLOCK_HEALTH, 1920.0f, 1080.0f, 1.0f, true);
        ut_near(x, 144.375f, 0.001f, "the health number moves from 192.5 to 144.375");

        x = 493.0f;
        y = 1043.0f;
        hud_transform_point(&x, &y, HUD_BLOCK_WEAPON, 1920.0f, 1080.0f, 1.0f, true);
        ut_near(x, 396.75f, 0.001f, "the ammo number moves from 493 to 396.75");
    }
}

static void test_scaling(void)
{
    hud_rect_t before = health_bar(1920.0f, 1080.0f);
    hud_rect_t after  = hud_transform(&before, HUD_BLOCK_HEALTH, 1920.0f, 1080.0f, 2.0f, false);

    ut_near(after.right - after.left, (before.right - before.left) * 2.0f, 0.1f,
                "scale 2.0 doubles the bar width");
    ut_near(after.bottom - after.top, (before.bottom - before.top) * 2.0f, 0.1f,
                "scale 2.0 doubles the bar height");
    ut_near(after.bottom, before.bottom, 2.0f, "a scaled bar stays on the bottom edge");

    before = weapon_icon(1920.0f, 1080.0f);
    after  = hud_transform(&before, HUD_BLOCK_WEAPON, 1920.0f, 1080.0f, 2.0f, false);
    ut_near(after.right - after.left, (before.right - before.left) * 2.0f, 0.1f,
                "scale 2.0 doubles the icon width even with squaring off");

    before = escort_bar(1920.0f, 1080.0f);
    after  = hud_transform(&before, HUD_BLOCK_ESCORT, 1920.0f, 1080.0f, 2.0f, false);
    ut_near((after.left + after.right) * 0.5f, 960.0f, 0.5f,
                "a scaled escort bar stays centred");
    ut_near(after.top, 0.0f, 0.01f, "a scaled escort bar stays on the top edge");

    /* Half size, so the other direction is covered too. */
    before = health_bar(1920.0f, 1080.0f);
    after  = hud_transform(&before, HUD_BLOCK_HEALTH, 1920.0f, 1080.0f, 0.5f, false);
    ut_near(after.right - after.left, (before.right - before.left) * 0.5f, 0.1f,
                "scale 0.5 halves the bar width");
    ut_near(after.bottom, before.bottom, 2.0f, "a halved bar stays on the bottom edge");

    /* A 5:4 screen is the one place squaring makes the bars WIDER, and nothing may leave it. */
    before = weapon_icon(1280.0f, 1024.0f);
    after  = hud_transform(&before, HUD_BLOCK_WEAPON, 1280.0f, 1024.0f, 1.0f, true);
    ut_check(after.right < 1280.0f, "on a 5:4 screen the widened weapon icon still fits");
}

static void test_point_refusals(void)
{
    float x = 100.0f;
    float y = 200.0f;

    hud_transform_point(&x, &y, HUD_BLOCK_NONE, 1920.0f, 1080.0f, 2.0f, true);
    ut_near(x, 100.0f, 0.0f, "a point classified as NONE is not moved");
    ut_near(y, 200.0f, 0.0f, "a point classified as NONE is not moved vertically");

    hud_transform_point(NULL, &y, HUD_BLOCK_HEALTH, 1920.0f, 1080.0f, 2.0f, true);
    ut_near(y, 200.0f, 0.0f, "a null x leaves y alone rather than half-transforming");
}

/* ---------------------------------------------------------------------------------------------
 * The two glyph rules
 * ------------------------------------------------------------------------------------------- */
static void test_hud_glyph_rule(void)
{
    float sx = 0.8f;
    float sy = 0.8f;

    /* The renderer draws at (sx*W/640, sy*H/480). Under squaring the bars grow by H/480, so the
     * digits have to grow by exactly that too, which is what the corrected pair produces. */
    hud_glyph_scale(&sx, &sy, 1920.0f, 1080.0f, 1.0f, true);
    ut_near(sx, 0.6f, 0.0005f, "the HUD glyph horizontal drops to 0.6 at 16:9");
    ut_near(sy, 0.8f, 0.0005f, "the HUD glyph vertical is unchanged at scale 1.0");

    ut_near(sx * (1920.0f / 640.0f), 1.8f, 0.001f, "the drawn glyph is 1.80 px wide");
    ut_near(sy * (1080.0f / 480.0f), 1.8f, 0.001f, "the drawn glyph is 1.80 px tall, square");
    ut_near((sx * (1920.0f / 640.0f)) / (0.8f * (640.0f / 640.0f)), 1080.0f / 480.0f, 0.001f,
                "the glyph grows by H/480, the same factor the squared bars grow by");

    sx = 0.8f;
    sy = 0.8f;
    hud_glyph_scale(&sx, &sy, 1920.0f, 1080.0f, 2.0f, false);
    ut_near(sx, 1.6f, 0.0005f, "HudScale alone doubles the HUD glyph horizontally");
    ut_near(sy, 1.6f, 0.0005f, "HudScale alone doubles the HUD glyph vertically");

    sx = 0.8f;
    sy = 0.8f;
    hud_glyph_scale(&sx, &sy, 0.0f, 0.0f, 1.0f, true);
    ut_near(sx, 0.8f, 0.0f, "an unusable screen size leaves the HUD glyph pair alone");
    ut_near(sy, 0.8f, 0.0f, "an unusable screen size leaves the HUD glyph vertical alone");
}

static void test_square_text_rule(void)
{
    ut_near(hud_square_glyph_scale(1.0f, 640, 480), 1.0f, 0.001f,
                "the general glyph correction is the identity at 4:3");
    ut_near(hud_square_glyph_scale(1.0f, 1920, 1080), 4.0f / 3.0f, 0.001f,
                "the general glyph correction is 4/3 at 16:9");
    ut_near(hud_square_glyph_scale(0.8f, 1920, 1080), 0.8f * 4.0f / 3.0f, 0.001f,
                "the general glyph correction scales with the caller's value");

    /* The level-intro crawl passes (640/W, 480/H) and must survive bit for bit. */
    ut_near(hud_square_glyph_scale(640.0f / 1920.0f, 1920, 1080), 480.0f / 1080.0f, 0.0001f,
                "the crawl's hand-corrected pair is left exactly as it was");

    ut_near(hud_square_glyph_scale(1.0f, 0, 0), 1.0f, 0.001f,
                "an unknown display size leaves the scale alone");
}

int main(void)
{
    printf("checking %d display modes\n\n", (int)MODE_COUNT);

    test_classification_at_every_mode();
    test_classification_refusals();
    test_number_split();
    test_identities();
    test_squaring_at_16_9();
    test_numbers_land_on_their_block();
    test_scaling();
    test_point_refusals();
    test_hud_glyph_rule();
    test_square_text_rule();

    return ut_summary("hud_layout");
}
