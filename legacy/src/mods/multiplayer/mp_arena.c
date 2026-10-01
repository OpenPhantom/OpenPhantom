/* mp_arena.c: in a team deathmatch no NPC comes into being.
 *
 * Not a cleanup. Removing actors once they exist would mean fighting the campaign every substep,
 * would leak the effects and sounds their removal fires, and would still show the first frame of
 * every one of them. The level is emptied instead, before it is played, by writing the state the
 * engine's own activation scan already treats as "do not wake this".
 *
 * There are two ways an actor gets into a level and they need different answers:
 *
 *   the activation scan   reads each placement's spawn state and skips anything that is not zero.
 *                         Writing 2 over the enemy placements is therefore enough, and nothing in
 *                         the engine writes that back to zero for a placement that never lived.
 *
 *   the script spawner    calls the spawner straight out of the AI machine and never looks at the
 *                         spawn state. Only a gate on the spawner catches it.
 *
 * What is buried and what is not. The class the placement authored decides, with one exception
 * that decides first: a placement whose flags carry the player-host bit is never buried, whatever
 * its class says. The spawner builds no body for such a placement and parks it in state 16 for the
 * player module to take over, and the removal path calls back into the player module to resume it.
 * Some of them do carry an enemy class, so a filter reading the class alone would bury the body
 * the player is about to stand up in.
 *
 * Two side effects, known and not fixed here.
 *
 *   Some enemy placements carry a mover slot and their script drives a door or a lift with it:
 *   82 of the 1112 enemy class placements across the eleven shipped levels do. Burying such a
 *   placement leaves that door shut, and a team deathmatch on a campaign level can therefore
 *   have parts of it walled off. Fixing it means driving those movers from somewhere else, which
 *   is a mode's job and not this module's.
 *
 *   Reveal lists run dry. A placement can carry a list of other placements to make scan-eligible
 *   when it dies (133 of the 1112 do, 242 targets between them), and a placement that never
 *   lives never dies, so the followers are never revealed. The reveal runs inside the engine's
 *   actor removal and only for removal reasons 1 and 0xE, where it writes spawn state 2 over the
 *   dying placement and sets bit 0 of each follower's flags, the bit the activation scan tests
 *   first. In a team deathmatch that is the wanted outcome, since the followers are enemies too;
 *   it is written down because it is the mechanism by which burying one placement quietly
 *   reaches others.
 */
#include "mp_arena.h"

#include "mp_cells.h"
#include "mp_placements.h"
#include "mp_signatures.h"
#include "mp_wire.h"

#include "common/detour.h"
#include "common/host_image.h"
#include "common/logging.h"
#include "common/memory.h"

#include <intrin.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* __cdecl, three arguments, the actor in EAX. The same shape the receiver's own spawner calls. */
typedef uintptr_t(__cdecl *spawn_actor_fn_t)(uintptr_t placement, int32_t index,
                                             int32_t script_override);

typedef struct arena_state {
    bool             installed;
    bool             active;
    bool             mode_logged;
    detour_t         spawn_hull;
    spawn_actor_fn_t original;

    /* The pass. */
    uint32_t passes;
    uint32_t buried;            /* placements this module wrote the buried state into */
    uint32_t already_buried;    /* enemy placements that were buried before it looked */
    uint32_t buried_while_live; /* buried with an actor already standing on them, so the pass ran
                                 * after the level was populated rather than as it opened */
    uint32_t kept;              /* every placement the rule left alone */
    uint32_t buried_player_host;  /* of the BURIED, the ones the handover flag put under */
    uint32_t unreadable;
    uint32_t write_faults;
    uint32_t last_buried;
    uint32_t last_kept;
    bool     no_world_logged;

    /* The gate. */
    uint32_t gate_refused;
    uint32_t gate_let_engine;   /* engine calls for something the rule does not bury */
    uint32_t gate_let_outside;  /* calls from outside the engine: this DLL's replica spawner and
                                 * second scan, and the developer overlay's copies */
    uint32_t gate_let_idle;     /* calls seen while the arena is off, which is cooperative play */
    uint32_t gate_unreadable;
    bool     refusal_logged;

    mp_arena_spawn_listener_t spawn_listener;
    uint32_t                  copies_heard;   /* copies handed to the listener as they were made */

    mp_arena_placement_listener_t placement_listener;
} arena_state_t;

static arena_state_t arena;

/* ==============================================================================================
 * The rule.
 * ============================================================================================ */

bool mp_arena_buries(uint32_t placement_flags, int32_t class_id, bool drives_a_mover)
{
    /* THE CUTSCENE HANDOVER, and it is buried FIRST, whatever its class says.
     *
     * This bit does not mean "a spawn point for the player", which is what it was read as while
     * this module was first written. It means the placement TAKES the player: in the actor tick a
     * placement carrying it calls player_suspend, which parks the player module at state 0, and
     * the removal path calls player_resume to give him back. That is how a level runs its opening
     * sequence, and there are twenty five of them across the eleven levels, named obistart,
     * obiexit, obi-end and so on.
     *
     * In an arena that is exactly what must not happen, and for a reason larger than the sequence
     * itself: a parked player module reads 0, and every respawn, every hero swap and every placed
     * pose is gated on it reading 1. One of these waking mid-round would freeze a player out of
     * his own match with no message anywhere.
     *
     * Burying them costs nothing that an arena wants. The spawner builds no body for such a
     * placement in the first place, and the player's own body does not come from here: the level
     * begin message spawns him at the world's authored start, with no reference to any placement.
     * Three of the twenty five also carry an enemy class, so a filter reading the class alone
     * would have half of them either way.
     *
     * The bit is read out of the spawner itself, which copies the placement's whole flags word
     * into the actor and then branches on bit 13: clear, and it builds the body, binds the model
     * and plays clip 0; set, and it only parks the record in state 0x10. The removal path
     * confirms it from the other side, opening with the same test and calling the player resume
     * when it holds. The twenty five are authored with six flag words, 0x2000, 0x2020, 0x2080,
     * 0x2800, 0x3000 and 0xA000, which is why the test is of the bit and not of the word. The
     * three that also carry an enemy class are two on the assault level and one on the
     * federation ship; the first count said four and named two. */
    if ((placement_flags & MP_PLACEMENT_F_HOSTS_PLAYER) != 0u) {
        return true;
    }
    /* Somebody who shoots goes under whatever they drive, and this arm has to come before the
     * mover one rather than after it.
     *
     * Without it the mover arm rescued eighty one armed placements across the eleven levels that
     * the old class rule had always buried: thirteen in Theed alone, riflemen and two mercenaries
     * of fifty and sixty health, every one of them kept only because its script may open a door.
     * That is the complaint this whole change was made to answer, arriving through the fix for
     * it. The lifts those thirteen drive were already dead under the old rule, so nothing that
     * used to work stops working. Counted over all eleven levels: the old class rule buried 1134
     * and left 1116; the mover first order buried 1919 and left 331, 81 of them armed; this order
     * buries 2000 and leaves 250.
     *
     * The tank is named beside the enemy because the spawner maps class 8 onto class 2 before it
     * builds anything, reading the class out of the placement at +0x34; five records in the
     * shipped levels, all the same tank file, arrive as riflemen do. */
    if (class_id == MP_PLACEMENT_CLASS_ENEMY || class_id == MP_PLACEMENT_CLASS_TANK) {
        return true;
    }
    /* An arena keeps its weapons and its health. The band is closed at both ends because the
     * engine's own contact handler is: above it the classes are damage arms, not pickups. The
     * pickup handler tests the bands 0x0A, 0x0D to 0x10 and 0x11 to 0x1B; 177 shipped placements
     * fall in them, every one a power up file. */
    if (class_id >= (int32_t)MP_PLACEMENT_CLASS_FIRST_PICKUP &&
        class_id <= (int32_t)MP_PLACEMENT_CLASS_LAST_PICKUP) {
        return false;
    }
    /* And it keeps whatever drives level geometry, whatever that thing is otherwise. A lift with
     * a civilian's class is still the only way to the upper floor. */
    if (drives_a_mover) {
        return false;
    }
    return true;
}

/* ==============================================================================================
 * The pass over the placement table.
 * ============================================================================================ */

/* Reads the three fields the rule needs plus the state it may overwrite. False means the record
 * could not be read at all, which is counted and skipped rather than guessed at.
 *
 * The gate this walks through is the engine's own. The activation scan at 0x00437161 loops over
 * the placement count at world+0x204 and the pointer table at world+0x20C, and takes a placement
 * only when bit 0 of its flags is set and the dword at +0xC8, the spawn state, is zero; those
 * are the operands the count, table and state offsets were read out of. Nothing writes the 2
 * back to 0 for a placement that never lived: the only writer of state 0 is the actor removal
 * at 0x00437850, in the arm that runs for an actor that exists, and it writes 2 instead when
 * that actor is already a corpse. A buried placement has no actor, so no removal ever runs for
 * it, and the corpse cull likewise only reaches actors. */
uint32_t mp_arena_clear_enemies(void)
{
    uint32_t count = 0;
    uint32_t table = 0;
    uint32_t buried = 0;
    uint32_t kept = 0;
    uint32_t hosts = 0;
    uint32_t i;

    /* An arena that is off empties nothing, and this gate was missing from the day the module was
     * written. The pass ran at the beginning of every level, in a campaign as readily as in a
     * deathmatch, and only the SPAWNER was ever asked whether the arena was on.
     *
     * While the rule buried two classes that was a co-operative level short of its riflemen,
     * which is wrong and survivable. When the rule grew to bury everything that neither is a
     * pickup nor drives a mover, the same ungated pass took four hundred placements out of a
     * campaign level: no enemies, no civilians, no scripted actors, no story. That is a broken
     * game, and it is the one thing this module must never do to a co-op session. */
    if (!arena.active) {
        return 0u;
    }
    if (!mp_placements_table(NULL, &count, &table)) {
        if (!arena.no_world_logged) {
            arena.no_world_logged = true;
            log_warning("the arena could not be emptied: no world stands, or its placement table "
                        "did not read. The level keeps the enemies the campaign authored");
        }
        return 0u;
    }

    ++arena.passes;
    for (i = 0; i < count; ++i) {
        mp_placement_t placement;
        uint32_t       put = MP_PLACEMENT_STATE_BURIED;

        if (!mp_placements_read(table, i, &placement)) {
            ++arena.unreadable;
            continue;
        }
        if (!mp_arena_buries(placement.flags, placement.class_id, placement.drives_mover)) {
            ++kept;
            continue;
        }
        if ((placement.flags & MP_PLACEMENT_F_HOSTS_PLAYER) != 0u) {
            ++hosts;   /* counted apart: these are buried for a different reason than the rest */
        }
        if (placement.state == MP_PLACEMENT_STATE_BURIED) {
            ++arena.already_buried;
            continue;
        }
        if (!memory_try_write(placement.address + MP_PLACEMENT_SPAWN_STATE, &put, sizeof put)) {
            ++arena.write_faults;
            continue;
        }
        /* A live one is still written, because the intent is that it never comes back. It is
         * counted apart because it means the pass ran late: an actor already stands on it, and
         * the engine's own removal of that actor may put the state back to zero. */
        if (placement.state == MP_PLACEMENT_STATE_LIVE) {
            ++arena.buried_while_live;
        }
        ++buried;
    }

    arena.buried           += buried;
    arena.kept             += kept;
    /* Counted among the BURIED, not among the kept: `hosts` is incremented inside the burying
     * branch. The report used to read "N left standing (M of them the player's own body)", which
     * put it on the wrong side of the split and had a reader looking for a cutscene handover
     * among the survivors. */
    arena.buried_player_host += hosts;
    arena.last_buried       = buried;
    arena.last_kept         = kept;
    log_info("the arena is emptied: %u placement(s) buried (%u of them a cutscene handover), "
             "%u left standing, %u already buried, %u unreadable, %u refused the write",
             (unsigned)buried, (unsigned)hosts, (unsigned)kept, (unsigned)arena.already_buried,
             (unsigned)arena.unreadable, (unsigned)arena.write_faults);
    return buried;
}

/* ==============================================================================================
 * The gate on the spawner.
 * ============================================================================================ */

/* Whose call is this. The gate has to catch the engine's script spawner and must not catch this
 * DLL's own replica spawner, which reaches the same function through the same address and which
 * lives in another source file that this module may not touch.
 *
 * The return address answers it without either side knowing about the other. The detour replaced
 * the spawner's prologue with a branch, so the call instruction that reached us pushed its own
 * return address and it is still the top of the stack when the hook is entered: an address inside
 * the host executable is one of the engine's three spawn paths, and anything else is somebody who
 * loaded beside it, which here means us.
 *
 * The three engine callers of the spawner, all inside the host image: the save game restore loop
 * at 0x004326AD, the script opcode 0x60F at 0x0043516D, and the activation scan at 0x0043722E.
 * The restore loop is why the gate refuses by the rule rather than with a blanket no: a save
 * being restored rebuilds the player host placement through this function too, and refusing
 * that would abort the whole restore. A team deathmatch has no save game, but the gate does not
 * depend on that being true.
 *
 * It fails open in both directions that matter. If the host geometry never resolved, no caller
 * ever looks like the engine and nothing is refused, so a build this feature does not understand
 * gets its campaign rather than an empty level. And if another DLL were to detour the spawner in
 * front of this hook, its call would arrive from its own image and be let through, which loses the
 * suppression rather than the enemy replica. */
static bool caller_is_the_engine(uintptr_t caller)
{
    uintptr_t base = host_image_base();
    uintptr_t end  = host_image_end();

    return base != 0u && end > base && caller >= base && caller < end;
}

/* Why the pass is not enough. The script opcode 0x60F, an inner branch of the AI dispatch at
 * 0x0043512E, calls the spawner and then writes spawn state 1 when the call answered and the
 * actor does not carry the handover bit; it never reads the state before it calls, so a buried
 * placement is spawned anyway. There are 227 such script nodes across the eleven levels, the
 * heaviest being the race level with 49, the spaceport with 35 and the swamp with 30.
 *
 * Refusal is a plain zero, which is what the spawner itself answers when it has no model or when
 * both pool allocations fail, so every caller in the engine already handles it: all three test
 * the result against zero, the activation scan leaves the spawn state alone and the script
 * spawner does nothing.
 *
 * Nothing is written to the placement here. The pass owns that field, and a gate that wrote it
 * would leave marks behind after the arena is switched off; re-entering a refused script node
 * costs two reads.
 *
 * Every spawn that went through is told to the placement listener with its caller, which is how a
 * host sees a script put the hero on a placement.
 *
 * engine: character *spawn_actor(enmyRecord *rec, int enmyIndex, int scriptOverride) */
static uintptr_t __cdecl hook_spawn_actor(uintptr_t placement, int32_t index,
                                          int32_t script_override)
{
    uintptr_t      caller = (uintptr_t)_ReturnAddress();
    mp_placement_t record;
    uintptr_t      actor;

    if (!arena.active) {
        ++arena.gate_let_idle;
    } else if (!caller_is_the_engine(caller)) {
        ++arena.gate_let_outside;
    } else if (!mp_placements_read_at(placement, &record)) {
        ++arena.gate_unreadable;
    } else if (!mp_arena_buries(record.flags, record.class_id, record.drives_mover)) {
        ++arena.gate_let_engine;
    } else {
        ++arena.gate_refused;
        if (!arena.refusal_logged) {
            arena.refusal_logged = true;
            log_info("the arena refused the spawner for placement %d, class %d: this is the "
                     "scripted path, which does not read the spawn state the pass wrote",
                     (int)index, (int)record.class_id);
        }
        return 0u;
    }
    actor = arena.original(placement, index, script_override);
    if (actor != 0u && arena.spawn_listener != NULL && index >= 0 &&
        mp_wire_key_is_copy((uint32_t)index)) {
        ++arena.copies_heard;
        arena.spawn_listener((uint32_t)index, actor);
    }
    if (actor != 0u && arena.placement_listener != NULL) {
        arena.placement_listener(placement, index, caller);
    }
    return actor;
}

/* The spawner sits at 0x00437250 in the retail image. It is also the function this DLL's own
 * replica spawner calls, by the resolved address, which after this detour is the branch; that is
 * why the hook asks whose call it is before it asks anything else. */
bool mp_arena_install(void)
{
    uintptr_t target;
    size_t    prologue;

    if (arena.installed) {
        return true;
    }
    target   = mp_signatures_address(MP_SITE_SPAWN_ACTOR);
    prologue = mp_signatures_prologue(MP_SITE_SPAWN_ACTOR);
    if (target == 0u || prologue == 0u) {
        log_warning("the spawner site did not resolve, so the scripted spawn path is not gated "
                    "and a team deathmatch level can still be given enemies by its own scripts");
        return false;
    }
    if (host_image_base() == 0u) {
        log_warning("the host image did not resolve, so this DLL's own calls to the spawner "
                    "cannot be told from the engine's. The gate is left off rather than made to "
                    "guess, and the scripted spawn path stays open");
        return false;
    }
    if (!detour_install(&arena.spawn_hull, target, (const void *)&hook_spawn_actor, prologue)) {
        log_error("the spawner at %08X refused the detour, so the scripted spawn path is not "
                  "gated", (unsigned)target);
        return false;
    }
    arena.original  = (spawn_actor_fn_t)arena.spawn_hull.original;
    arena.installed = true;
    log_info("the arena gate is bound at %08X (%s): a scripted spawn of an enemy class is refused "
             "while the arena is on, and this DLL's own spawns are let through by their return "
             "address", (unsigned)target, arena.spawn_hull.chained ? "chained" : "first");
    return true;
}

/* The first call always writes its line, even when it changes nothing. Without that a run that
 * only ever asks for cooperative play leaves no evidence that the mode reached this module at
 * all, which is the failure a report exists to make visible. */
void mp_arena_set_active(bool active)
{
    if (arena.active == active && arena.mode_logged) {
        return;
    }
    arena.active     = active;
    arena.mode_logged = true;
    log_info("the arena is now %s", active ? "on: no enemy is created here"
                                           : "off: the level populates itself as authored");
}

bool mp_arena_installed(void)
{
    return arena.installed;
}

void mp_arena_set_spawn_listener(mp_arena_spawn_listener_t listener)
{
    arena.spawn_listener = listener;
}

bool mp_arena_set_placement_listener(mp_arena_placement_listener_t listener)
{
    arena.placement_listener = listener;
    return arena.installed;
}

void mp_arena_report(void)
{
    log_info("  the arena: %s, gate %s; %u pass(es) buried %u placement(s) (%u of them a "
             "cutscene handover) and left %u standing, %u already buried, %u had an actor on "
             "them already, %u unreadable, %u write fault(s)",
             arena.active ? "on" : "off", arena.installed ? "bound" : "not bound",
             (unsigned)arena.passes, (unsigned)arena.buried,
             (unsigned)arena.buried_player_host, (unsigned)arena.kept,
             (unsigned)arena.already_buried,
             (unsigned)arena.buried_while_live, (unsigned)arena.unreadable,
             (unsigned)arena.write_faults);
    log_info("  the arena gate: %u refused, %u let through for the engine, %u for a caller "
             "outside it, %u while off, %u unreadable; last pass buried %u and kept %u; %u "
             "copies made under the gate went to be parked",
             (unsigned)arena.gate_refused, (unsigned)arena.gate_let_engine,
             (unsigned)arena.gate_let_outside, (unsigned)arena.gate_let_idle,
             (unsigned)arena.gate_unreadable, (unsigned)arena.last_buried,
             (unsigned)arena.last_kept, (unsigned)arena.copies_heard);
}
