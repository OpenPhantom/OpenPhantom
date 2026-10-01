/* spawn_mode.c: the placement mode's own frame, run against a stand in for the engine.
 *
 * spawn_mode.c is where the mode meets the game, so everything it reaches beyond the mode's own
 * pure modules is stood up below, each as the one function the mode calls: the spawner, the
 * session link, the panel's module node, the player's record, the camera and the look. The mode's
 * state, the ghost's handle, the keys and the owner of the pointer are the real modules. A frame
 * here is what the frame hook does: the mode's frame, then the owner's pause.
 *
 * What would be silent if it were wrong:
 *
 *   a world that runs under the mode in a single player game, where the panel holds it still,
 *   or one that never runs after a copy is placed, which draws the copy between the world's
 *   origin and its place;
 *
 *   a mode that stays on over a dead player;
 *
 *   a refusal that outlives the click after it, or a raise that fails without a word in the row;
 *
 *   a right click that deletes on a client, deletes a ridden gun, or acts on a copy a click of the
 *   same frame may have culled;
 *
 *   wheel notches the game was given before the mode taken for a turn.
 */
#include "unittest.h"

#include "common/text.h"

#include "character_prop.h"
#include "character_prop_body.h"
#include "cheats_openphantom.h"
#include "input_owner.h"
#include "npc_spawn_describe.h"
#include "npc_spawn_link.h"
#include "npc_spawn_node.h"
#include "npc_spawner.h"
#include "overlay_input.h"
#include "player_slot.h"
#include "session_lock.h"
#include "sim_pause.h"
#include "spawn_look.h"
#include "spawn_marks.h"
#include "spawn_mode.h"
#include "spawn_place.h"
#include "world_camera.h"
#include "world_probe.h"

#include <string.h>

#define MSG_LEFT_BUTTON  0x0201
#define MSG_RIGHT_BUTTON 0x0204
#define MSG_MOUSE_WHEEL  0x020A
#define PLAYER_DEAD      0x394u
#define HOVERED_ACTOR    0x12340000u

/* ============================================================================================
 * The stand in.
 * ============================================================================================ */

static struct {
    bool         open;
    bool         session;         /* the multiplayer runs the copies */
    bool         session_running; /* ... and, apart from that, a session is running at all */
    bool         client;
    bool         may_pause;
    bool         paused;
    bool         raise_answers;
    uint32_t     substeps;
    uint32_t     epoch;
    uint8_t      player[0x400];
    bool         no_player;
    const char  *refusal;
    char         refusal_text[64];
    spawn_look_t look;
    uint32_t     raised;
    uint32_t     wished;
    uint32_t     deleted;
    uintptr_t    deleted_actor;
    uint32_t     kinds;
    uint32_t     alive;
    int32_t      chosen;
    bool         can_raise;
    char         order[8];
    uint32_t     order_count;
} st;

static void note_order(char what)
{
    if (st.order_count + 1u < sizeof st.order) {
        st.order[st.order_count++] = what;
        st.order[st.order_count]   = '\0';
    }
}

bool overlay_input_is_open(void)                { return st.open; }
bool overlay_input_opens_on(int32_t virtual_key)
{
    (void)virtual_key;
    return false;
}
bool cheats_openphantom_is_on(cheats_own_id_t id)
{
    (void)id;
    return false;
}
int32_t cheats_openphantom_freecam_hotkey(void)  { return 0; }
bool session_lock_panel_may_pause(void)         { return st.may_pause; }
bool session_lock_running(void)                 { return st.session_running; }
/* The group's lock, which is what the mode has to read: a session that runs no copies. */
bool session_lock_holds_the_spawner(void)       { return st.session_running && !st.session; }
void sim_pause_hold(sim_pause_holder_t who, bool held)
{
    if (who == SIM_PAUSE_PANEL) {
        st.paused = held;
    }
}
void *player_slot_current(void)                 { return st.no_player ? NULL : st.player; }

bool world_camera_available(void)               { return true; }
void world_camera_resolve(void)                 { }
void world_probe_resolve(void)                  { }

uint32_t npc_spawn_node_epoch(void)             { return st.epoch; }
bool npc_spawn_node_standing(void)              { return true; }
uint32_t npc_spawn_node_substeps(void)          { return st.substeps; }

bool npc_spawner_is_available(void)             { return true; }
bool npc_spawner_can_raise(void)                { return st.can_raise; }
uint32_t npc_spawner_kind_count(void)           { return st.kinds; }
uint32_t npc_spawner_alive(void)                { return st.alive; }
int32_t npc_spawner_chosen(void)                { return st.chosen; }
bool npc_spawner_kind(uint32_t index, npc_spawner_kind_t *out)
{
    (void)index;
    memset(out, 0, sizeof *out);
    text_format(out->name, sizeof out->name, "%s", "tusken");
    text_format(out->file, sizeof out->file, "%s", "tusken.baf");
    out->placements = 3u;
    return true;
}
bool npc_spawner_raise_described(const npc_spawn_desc_t *desc)
{
    (void)desc;
    note_order('p');
    ++st.raised;
    return st.raise_answers;
}
bool npc_spawner_delete(uintptr_t actor)
{
    note_order('r');
    ++st.deleted;
    st.deleted_actor = actor;
    return true;
}

bool npc_spawn_link_active(void)                { return st.session; }
bool npc_spawn_link_is_client(void)             { return st.session && st.client; }
void npc_spawn_link_facts(npc_spawn_link_facts_t *out)
{
    memset(out, 0, sizeof *out);
    out->read     = st.session_running;
    out->active   = st.session;
    out->client   = st.session && st.client;
    out->cap      = (st.session && !st.client) ? 16u : 0u;
    out->own_slot = (st.session && st.client) ? 1u : 0u;
}
const char *npc_spawn_link_refusal(void)        { return st.refusal; }
void npc_spawn_link_refuse_here(const char *why)
{
    text_format(st.refusal_text, sizeof st.refusal_text, "%s", why);
    st.refusal = st.refusal_text;
}
void npc_spawn_link_forget_refusal(void)        { st.refusal = NULL; }
uint32_t npc_spawn_link_cap(void)               { return (st.session && !st.client) ? 16u : 0u; }
bool npc_spawn_link_spawn_described(const npc_spawn_desc_t *desc)
{
    (void)desc;
    note_order('w');
    ++st.wished;
    return true;
}

bool npc_spawn_describe_at(const float *position, float facing, npc_spawn_desc_t *out)
{
    memset(out, 0, sizeof *out);
    text_format(out->file, sizeof out->file, "%s", "tusken.baf");
    memcpy(out->position, position, sizeof out->position);
    out->facing = facing;
    return true;
}

void spawn_look_at(spawn_look_t *out, const npc_spawner_kind_t *kind, int32_t chosen)
{
    (void)kind;
    (void)chosen;
    *out = st.look;
}
void spawn_look_label(const npc_spawner_kind_t *kind, char *out, uint32_t out_size)
{
    text_format(out, out_size, "%s", kind->name);
}
void spawn_marks_draw(const spawn_marks_scene_t *scene)   { (void)scene; }
/* The band at the top of the picture: it needs a screen, and this program has none. What it
 * would have said is spawn_banner.c's, which has a program of its own. */
void spawn_marks_banner(const char *text)                 { (void)text; }

const character_prop_sites_t *character_prop_sites(void) { return NULL; }
bool character_prop_draw_install(void)                    { return false; }
void character_prop_draw_set_extra(character_prop_draw_extra_t extra) { (void)extra; }

/* ============================================================================================ */

/* What the frame hook does, in its order. */
static void frame(void)
{
    spawn_mode_frame();
    (void)input_owner_sync();
}

static void fresh(void)
{
    spawn_place_t *place = spawn_place_state();

    spawn_mode_ask(false);
    st.open = true;
    frame();
    memset(&st, 0, sizeof st);
    memset(place, 0, sizeof *place);
    st.open              = true;
    st.may_pause         = true;
    st.raise_answers     = true;
    st.can_raise         = true;
    st.kinds             = 204u;
    st.chosen            = 0;
    st.epoch             = 1u;
    st.look.camera       = true;
    st.look.verdict      = SPAWN_SPOT_OK;
    st.look.strike       = 7.5f;
    (void)input_owner_take_wheel();
}

static void click(int32_t message)
{
    spawn_place_state()->clicked_once = false;   /* each click here is its own, not a double */
    (void)spawn_mode_button(message);
}

static void the_held_world(void)
{
    spawn_place_t *place = spawn_place_state();

    ut_section("the held world, and the settle after a copy");
    fresh();
    spawn_mode_ask(true);
    frame();
    ut_check(place->on && input_owner_now() == INPUT_OWNER_PLACEMENT, "the mode comes on");
    ut_check(st.paused, "in a single player game the world is held under it, as under the panel");
    click(MSG_LEFT_BUTTON);
    frame();
    ut_check(st.raised == 1u, "a click places");
    ut_check(place->settling && !st.paused, "and the world runs for the copy placed");
    st.substeps += 1u;
    frame();
    ut_check(place->settling && !st.paused, "one substep on, it still runs");
    st.substeps += 1u;
    frame();
    ut_check(!place->settling && st.paused, "two substeps on, it is held again");
    frame();
    ut_check(st.paused, "and stays held while nothing is placed");

    fresh();
    st.session   = true;
    st.may_pause = false;
    spawn_mode_ask(true);
    frame();
    click(MSG_LEFT_BUTTON);
    frame();
    ut_check(st.wished == 1u && st.raised == 0u, "in a session the click is a wish to the host");
    ut_check(!place->settling && !st.paused, "and the world runs as it runs, no settle needed");
}

static void the_death(void)
{
    spawn_place_t *place = spawn_place_state();
    uint32_t       dead  = 1u;

    ut_section("the player's death");
    fresh();
    spawn_mode_ask(true);
    frame();
    memcpy(st.player + PLAYER_DEAD, &dead, sizeof dead);
    frame();
    ut_check(!place->on, "the dead flag on the player's record turns the mode off");
    ut_check(input_owner_now() == INPUT_OWNER_PANEL && st.paused,
             "and the panel has the pointer and the held world back");
    spawn_mode_ask(true);
    frame();
    ut_check(!place->on, "over a dead player it does not come on again");
    ut_check(st.refusal != NULL && strcmp(st.refusal, "the player is dead") == 0,
             "and the refusal row names the death as the reason");
    st.no_player = true;
    spawn_mode_ask(true);
    frame();
    ut_check(st.refusal != NULL && strcmp(st.refusal, "no player") == 0,
             "with no player at all it says so instead");
}

static void the_refusal_row(void)
{
    spawn_place_t *place = spawn_place_state();

    ut_section("the refusal row");
    fresh();
    npc_spawn_link_refuse_here("the placement mode could not start");
    spawn_mode_ask(true);
    frame();
    ut_check(place->on && st.refusal == NULL, "a mode that starts clears the old refusal");
    npc_spawn_link_refuse_here("16 are alive, the most at once");
    click(MSG_LEFT_BUTTON);
    frame();
    ut_check(st.raised == 1u && st.refusal == NULL,
             "a click that places clears the refusal of the click before");
    st.raise_answers = false;
    click(MSG_LEFT_BUTTON);
    frame();
    ut_check(st.refusal != NULL && strcmp(st.refusal, "nothing was raised, see the log") == 0,
             "a raise that fails without a word of its own says so in the row");
    st.look.verdict = SPAWN_SPOT_NO_FLOOR;
    npc_spawn_link_refuse_here("stale");
    click(MSG_LEFT_BUTTON);
    frame();
    ut_check(st.refusal == NULL && st.raised == 2u,
             "a click on a refused place raises nothing and leaves no stale refusal behind");
}

static void the_right_click(void)
{
    ut_section("the right click");
    fresh();
    spawn_mode_ask(true);
    frame();
    click(MSG_RIGHT_BUTTON);
    frame();
    ut_check(st.deleted == 0u, "nothing under the pointer removes nothing");

    st.look.hover.actor = HOVERED_ACTOR;
    st.look.hover.key   = 257u;
    st.look.hover.mine  = true;
    click(MSG_RIGHT_BUTTON);
    frame();
    ut_check(st.deleted == 1u && st.deleted_actor == HOVERED_ACTOR,
             "in a single player game the copy under the pointer goes");

    st.look.hover.ridden = true;
    click(MSG_RIGHT_BUTTON);
    frame();
    ut_check(st.deleted == 1u, "a copy the player rides is skipped");
    st.look.hover.ridden = false;

    st.session = true;
    st.client  = true;
    click(MSG_RIGHT_BUTTON);
    frame();
    ut_check(st.deleted == 1u, "a client removes no single copy, not even its own");
    st.look.hover.mine = false;
    click(MSG_RIGHT_BUTTON);
    frame();
    ut_check(st.deleted == 1u, "nor another player's");

    st.client = false;
    click(MSG_RIGHT_BUTTON);
    frame();
    ut_check(st.deleted == 2u, "the host removes any copy, another player's too");

    fresh();
    spawn_mode_ask(true);
    frame();
    st.look.hover.actor = HOVERED_ACTOR;
    click(MSG_LEFT_BUTTON);
    click(MSG_RIGHT_BUTTON);
    frame();
    ut_check(strcmp(st.order, "rp") == 0,
             "a right and a left click in one frame remove first and place after, so the removal "
             "never names a copy the placing culled");
}

static void the_wheel(void)
{
    spawn_place_t *place = spawn_place_state();

    ut_section("the wheel at the start");
    fresh();
    input_owner_observe_wheel(MSG_MOUSE_WHEEL, (int32_t)(120u << 16));
    input_owner_observe_wheel(MSG_MOUSE_WHEEL, (int32_t)(120u << 16));
    spawn_mode_ask(true);
    frame();
    ut_check(place->on && !place->fixed,
             "notches the game had before the mode came on are not taken for a turn");
    input_owner_observe_wheel(MSG_MOUSE_WHEEL, (int32_t)(120u << 16));
    frame();
    ut_check(place->fixed, "one given to the mode turns the entity");
}

/* A co-op session that runs the copies: the mode asks whether the copies run, the question the
 * rows beside it ask, and not whether a session is running. Asked the other way, it refused on
 * the host and the client alike in a field run, with "the session runs no copies", while the
 * multiplayer ran them. */
static void the_session(void)
{
    spawn_place_t *place = spawn_place_state();

    ut_section("a session that runs the copies");

    fresh();
    st.session         = true;    /* the host: a cap out of NpcCopiesMax */
    st.session_running = true;
    st.may_pause       = false;
    spawn_mode_ask(true);
    frame();
    ut_check(place->on && place->available,
             "the host of a session that runs the copies enters the placement mode");
    click(MSG_LEFT_BUTTON);
    frame();
    ut_check(st.wished == 1u, "and a click is a wish for a copy");

    fresh();
    st.session         = true;    /* a client: no cap, a world slot of its own */
    st.client          = true;
    st.session_running = true;
    st.may_pause       = false;
    st.can_raise       = false;   /* the host builds it there, so this machine need not */
    spawn_mode_ask(true);
    frame();
    ut_check(place->on && place->available,
             "so does the client, whose copies its host raises");

    fresh();
    st.session         = false;   /* a session that hands out no keys */
    st.session_running = true;
    st.may_pause       = false;
    spawn_mode_ask(true);
    frame();
    ut_check(!place->on, "a session that runs no copies keeps the mode out");
    ut_check(st.refusal != NULL && strcmp(st.refusal, "the session runs no copies") == 0,
             "and the row says exactly that");
    ut_check(place->unavailable != NULL &&
                 strcmp(place->unavailable, "the session runs no copies") == 0,
             "the panel's own row carries the same sentence, decided once");

    fresh();
    st.kinds = 0u;
    spawn_mode_ask(true);
    frame();
    ut_check(!place->on && place->unavailable != NULL &&
                 strcmp(place->unavailable, "this level offers nothing to spawn") == 0,
             "a level with nothing to offer says so rather than blaming a session");

    fresh();
    st.can_raise = false;
    spawn_mode_ask(true);
    frame();
    ut_check(!place->on && place->unavailable != NULL &&
                 strcmp(place->unavailable, "the spawner cannot raise a copy here") == 0,
             "outside a session a machine that cannot raise one still says so");
}

int main(void)
{
    the_held_world();
    the_session();
    the_death();
    the_refusal_row();
    the_right_click();
    the_wheel();
    return ut_summary("spawn mode");
}
