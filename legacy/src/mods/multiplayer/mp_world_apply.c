/* mp_world_apply.c: building the host's map note, and honouring one that arrives.
 *
 * The header carries what may be written and why it runs where it runs. What is here is the walk
 * over the world's mover table in both directions and the counters that say what happened, and the
 * counter this file exists for is the first one in the report: how many movers the host has away
 * from home that this side has at home. That number is what nobody has today, and it is the
 * difference between a defect somebody can see and a defect somebody can size.
 */
#include "mp_world_apply.h"

#include "mp_scratch_bind.h"
#include "mp_signatures.h"
#include "mp_world.h"
#include "mp_world_state.h"

#include "common/logging.h"
#include "common/memory.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* How far past the world clock the settling integration is asked for.
 *
 * The integrator returns without doing anything at all when the time it is given equals the
 * mover's own time base, so the two have to differ. The engine's own level prime and its savegame
 * restore both add a flat 1e-05 for this, and a flat constant is not usable here: the world clock
 * is a 32 bit float counting seconds since the level began, its spacing at 256 seconds is already
 * 3.05e-05, and past that point adding 1e-05 rounds straight back to the value it started from.
 * Both engine callers run within a second or two of a level opening and never meet that; a
 * correction arrives whenever it arrives, and in a level somebody has been playing for five
 * minutes the flat nudge would silently do nothing and the corrected mover would hold a pose with
 * no geometry, no faces and no sound behind it.
 *
 * So the nudge carries a relative part as well. A float's spacing is at most the value times
 * 2^-23, about 1.2e-07, so a part per million is eight times the spacing at every magnitude and
 * the sum is always the next representable value or beyond. It costs no travel at all in the two
 * types this module writes: every direction it will write is an arm that assigns the pose a
 * constant, either zero or the whole travel, rather than integrating it. What the nudge does buy
 * is the pose application, which is the entire reason for making the call. */
#define MOVER_SETTLE_ABSOLUTE 1e-05f
#define MOVER_SETTLE_RELATIVE 1e-06f

static float settled_at(float now)
{
    return now + MOVER_SETTLE_ABSOLUTE + now * MOVER_SETTLE_RELATIVE;
}

typedef void(__cdecl *mover_tick_fn_t)(uint32_t mover, float now);

typedef struct world_apply {
    bool            installed;
    bool            can_write;    /* the integrator resolved, so a correction can be settled */
    bool            host;         /* this side owns the map and describes it */
    mover_tick_fn_t tick;

    bool                  held;         /* a note is in hand and has not been honoured yet */
    mp_world_state_note_t note;
    uint32_t              newest_generation;
    bool                  generation_logged;

    /* Sending. */
    uint32_t notes_out;
    uint32_t described;
    uint32_t largest;        /* the largest note this level has cost, in bytes */
    bool     oversize_logged;
    uint32_t capped;
    uint32_t refused;
    uint32_t send_refused;

    /* Receiving. */
    uint32_t notes_in;
    uint32_t torn;
    uint32_t elsewhere;      /* described a level this side is not in */
    uint32_t stale;          /* from before a transition this side has already been told about */
    uint32_t wrong_role;     /* arrived at the side that owns the map */
    uint32_t entries;

    /* Judging. */
    uint32_t at_home;        /* THE NUMBER: away from home there, at home here */
    uint32_t agreed;
    uint32_t other_type;     /* a type this module does not correct */
    uint32_t missing;        /* an id this side's level has no mover for */
    uint32_t unreadable;
    uint32_t type_disagreement;
    uint32_t wire_moving;    /* the host's mover was in the middle of its travel */
    uint32_t local_moving;   /* this side's was */

    /* Writing. */
    uint32_t applied;
    uint32_t one_shots;   /* of those, elevators and lifts: the arm that pulls the pose back */
    uint32_t gate_refused;
    uint32_t write_faults;
} world_apply_t;

static world_apply_t apply;

bool mp_world_apply_install(void)
{
    uintptr_t address;

    if (apply.installed) {
        return apply.can_write;
    }
    apply.installed = true;

    /* The engine's own integrator, entered the way its own callers enter it. Two other fixes in
     * this tree detour the same function, so what stands at this address may be a branch into one
     * of their hooks; that is the point of entering here rather than past the prologue, because
     * their work belongs to every integration and not only to the engine's own, and one of them
     * takes the sub node snapshot the render interpolation reads. The site was already resolved
     * for the map's own module, so settling a mover cost no new pattern:
     *
     *     bapmap_tickMover at 0x00409170, void __cdecl (mover, float now)
     *       55 8B EC 83 EC 2C        push ebp; mov ebp, esp; sub esp, 0x2C
     *       83 3D CC 5F 5B 00 00     cmp  dword [005B5FCC], 0        ; the freeze cell
     *       0F 85 C7 04 00 00        jne  +0x4C7                     ; the whole body skipped
     *
     * with a declared prologue of six and the freeze cell and the branch distance masked. */
    address = mp_signatures_address(MP_SITE_BAPMAP_TICK_MOVER);
    if (address == 0) {
        log_warning("the map's state is described and compared but never applied: the mover "
                    "integrator did not resolve, and a corrected mover that is not settled would "
                    "hold a pose with no geometry, no faces and no sound behind it");
        return false;
    }
    apply.tick      = (mover_tick_fn_t)address;
    apply.can_write = true;
    return true;
}

void mp_world_apply_set_authority(bool host)
{
    apply.host = host;
    apply.held = false;
}

void mp_world_apply_forget(void)
{
    apply.held              = false;
    apply.newest_generation = 0u;
}

/* How many bank transitions this machine has seen. It is read rather than counted here because
 * the scratchpad binding already answers every one of the module messages that move a bank, and a
 * second counter fed from a second place would be a second thing to keep true. A build whose
 * binding did not resolve answers zero for good on both sides, and the guard then falls back to
 * the level identity alone, which is weaker but not wrong. */
uint32_t mp_world_apply_generation(void)
{
    uint32_t transitions = 0;
    uint32_t bank_bytes  = 0;
    uint32_t ai_writes   = 0;

    mp_scratch_bind_counters(&transitions, &bank_bytes, &ai_writes, NULL);
    return transitions;
}

/* ==============================================================================================
 * The sending side. Only the host, once a second, and only what is away from home.
 * ============================================================================================ */

size_t mp_world_apply_build(uint32_t tick, uint8_t *buffer, size_t capacity)
{
    mp_world_state_note_t note;
    uint32_t              world;
    uint32_t              count = 0;
    uint32_t              index;

    if (!apply.host || (tick % MP_WORLD_STATE_TICKS) != 0u) {
        return 0u;
    }
    world = mp_world_pointer();
    if (!mp_world_mover_count(world, &count)) {
        return 0u;
    }
    /* A level with more movers than the cap cannot be described whole, and the sender would then
     * quietly drop the tail of its own map every second. Said once, because it is a property of
     * the level and not of the moment; no shipped level reaches it. */
    if (!mp_world_state_fits(count) && !apply.oversize_logged) {
        apply.oversize_logged = true;
        log_warning("this level holds %u movers and one state note carries %u, so the movers past "
                    "that are described to nobody",
                    (unsigned)count, (unsigned)MP_WORLD_STATE_MAX_ENTRIES);
    }
    mp_world_state_note_init(&note, tick, (uint16_t)count, mp_world_apply_generation());
    for (index = 0; index < count; ++index) {
        mp_world_mover_t read;

        if (!mp_world_mover_read(world, index, &read)) {
            continue;
        }
        if (!mp_world_state_disturbed(read.type, read.active, read.dir)) {
            continue;
        }
        switch (mp_world_state_note_add(&note, index, read.type, read.dir, read.active, read.pose,
                                        read.length, read.dwell)) {
        case MP_WORLD_ADD_FULL:
            ++apply.capped;
            break;
        case MP_WORLD_ADD_REFUSED:
            ++apply.refused;
            break;
        case MP_WORLD_ADD_OK:
        default:
            break;
        }
    }
    if (note.count == 0u) {
        return 0u;   /* a level in which nothing has been touched yet; there is nothing to say */
    }
    ++apply.notes_out;
    apply.described += note.count;
    if (mp_world_state_bytes(note.count) > apply.largest) {
        apply.largest = (uint32_t)mp_world_state_bytes(note.count);
    }
    return mp_world_state_encode(&note, buffer, capacity);
}

/* ==============================================================================================
 * The receiving side.
 * ============================================================================================ */

bool mp_world_apply_take(const uint8_t *note, size_t bytes)
{
    mp_world_state_note_t arrived;

    if (!mp_world_state_is_note(note, bytes)) {
        return false;
    }
    /* Ours by tag from here on, whatever becomes of it: handing it back would only have the event
     * decoder try to read a description of a map as a body's action. */
    if (!mp_world_state_decode(note, bytes, &arrived)) {
        ++apply.torn;
        return true;
    }
    ++apply.notes_in;

    /* The map has one owner. A note arriving at the owner is a peer describing a map it does not
     * own, and refusing it is what stops a mistaken mode from letting a client move everybody's
     * doors. */
    if (apply.host) {
        ++apply.wrong_role;
        return true;
    }
    if (!mp_world_state_generation_current(arrived.generation, apply.newest_generation)) {
        ++apply.stale;
        if (!apply.generation_logged) {
            apply.generation_logged = true;
            log_warning("a map note describes transition %u and this side has already been told "
                        "of %u, so it was refused: it was encoded before a level change and "
                        "arrived after one, and its mover ids name a map that is gone",
                        (unsigned)arrived.generation, (unsigned)apply.newest_generation);
        }
        return true;
    }
    apply.newest_generation = arrived.generation;
    apply.note              = arrived;
    apply.held              = true;
    return true;
}

/* One entry against the mover this side has under that id.
 *
 * Everything before the write is counting, and it is counted even on a build that cannot write,
 * because the count is what says whether the write is worth having. */
/* An elevator, a lift or a drawbridge, put where the host has it.
 *
 * It is not the door path with a different type, and the two differences are both load bearing.
 *
 * FIRST, the pose is pulled back short of the end (mp_world_state_one_shot_pose). The engine
 * composes a mover's geometry on the TAIL of its integrator, and the integrator turns back in its
 * opening lines for a mover that is already latched:
 *
 *     case 3:  if (dir != 1) { if (dir != 2) return; *mover = 0; disableMoverFaces(); return; }
 *     case 4:  if (dir != 1) return;
 *
 * and the face disabler at 0x00408F12 only rewrites face flags and composes no matrices. That is
 * also what the engine's own savegame restore does to these two types: it writes the pose and
 * nothing composes it, so a lift loaded out of a save stands at the bottom while its record says
 * it is at the top. Only type 5 has a restore arm that forces it running with a zero time base,
 * so the integration runs the whole travel in one step and the pose application does run; that
 * reads like somebody having met the defect and fixed the case in front of them. None of the
 * three has been seen in the field, and they predict a single player defect rather than a
 * multiplayer one. Writing the end pose straight in produces a
 * record that reads "at the top" and a lift that is still at the bottom, the record correct, the
 * world wrong, and no way to tell from either side. Left a hair short and set running, the one
 * integration below reaches the tail, composes the sub node matrices, evaluates the keyframe
 * flags against the latch and clamps to the end itself.
 *
 * SECOND, the direction is not written back afterwards. That is the exact opposite of what the
 * door path does, and getting it wrong would be silent: the mover has to be left standing in the
 * RUNNING arm so that the engine's own next tick, which comes within a thirty-second of a
 * second, for every mover, unconditionally, carries it the last of the way and latches it with
 * its own hand. Written back, the next tick would find a latched mover, turn back in its first
 * line, and leave the geometry exactly where this function found it.
 *
 * Type 5 is given the engine's own restore arm when the wire says it is finished: direction one
 * with a zero time base is what mp_world's own savegame restore writes for it, and it makes the
 * clamp inside the integrator do the pose, the direction and the active flag in one pass. */
static void apply_one_shot(uint32_t world, uint32_t mover, float now,
                           const mp_world_entry_t *wire, const mp_world_mover_t *local)
{
    const float settle  = settled_at(now) - now;
    uint32_t    run_dir = MOVER_DIR_RUNNING;
    float       pose    = mp_world_state_one_shot_pose(wire->pose, local->length, local->speed,
                                                       settle);
    float       base    = now;

    /* The ticking flag first, and through the engine's own opener rather than by writing the
     * field, for the same reason the door path uses it: the opener carries the cell occupancy and
     * the face state with it.
     *
     * Leaving it out was a defect with no symptom at the moment of the write and a permanent one
     * afterwards. A type 4 mover NEVER clears its own flag, its arm sets the latched direction
     * and falls into the next one, which turns back before the line that would clear it, so a
     * host that has run one holds the flag for the rest of the level while a joiner who never ran
     * it holds nothing. The comparison tests the flag BEFORE it measures the pose, so the two
     * sides could never agree however well this function placed the mover, and the correction
     * fired again on every note, once a second, for 142 of the 528 shipped movers.
     *
     * The opener is also exactly the right arm to arrive in: for all three one-shot types it sets
     * the running direction, which is where the pose below wants the mover to be. */
    if (local->active == 0u && wire->active != 0u) {
        if (!mp_world_gate(world, wire->id, true)) {
            ++apply.gate_refused;
            return;
        }
    } else if (local->active != 0u && wire->active == 0u) {
        if (!mp_world_gate(world, wire->id, false)) {
            ++apply.gate_refused;
            return;
        }
    }
    if (mp_world_state_is_restore_armed(local->type) && wire->dir == MOVER_DIR_LATCHED) {
        /* The engine's own arm for this type, and it wants the clock at zero so that the whole
         * travel has notionally already happened. */
        base = 0.0f;
        pose = wire->pose * local->length;
    }
    if (!memory_try_write(mover + MOVER_DIR, &run_dir, sizeof run_dir) ||
        !memory_try_write(mover + MOVER_POSE, &pose, sizeof pose) ||
        !memory_try_write(mover + MOVER_TIMEBASE, &base, sizeof base)) {
        ++apply.write_faults;
        return;
    }
    apply.tick(mover, settled_at(now));
    ++apply.applied;
    ++apply.one_shots;
}

static void judge_and_apply(uint32_t world, uint32_t count, float now,
                            const mp_world_entry_t *wire)
{
    mp_world_mover_t local;
    mp_world_entry_t here;
    uint32_t         mover = 0;
    uint32_t         want_dir = (uint32_t)wire->dir;
    float            pose;
    float            dwell;
    float            base;

    ++apply.entries;
    if (wire->id >= count) {
        ++apply.missing;
        return;
    }
    if (!mp_world_mover_at(world, wire->id, &mover) ||
        !mp_world_mover_read(world, wire->id, &local)) {
        ++apply.unreadable;
        return;
    }
    /* The measurement this stage exists for, and it is taken before any decision about writing:
     * the host has this mover away from the position the level was authored with, and this side
     * has it exactly where the level put it. That is a door the far player opened and walked
     * through, and a door this player is standing in front of. */
    if (!mp_world_state_disturbed(local.type, local.active, local.dir)) {
        ++apply.at_home;
    }
    if (local.type != wire->type) {
        ++apply.type_disagreement;
        return;   /* two different maps, or a record that did not read; not a mover to correct */
    }
    if (!mp_world_state_carries(wire->type)) {
        ++apply.other_type;
        return;
    }
    mp_world_as_entry(wire->id, &local, &here);
    /* With the threshold this type earns rather than a door's. A one-shot is deliberately left a
     * guard short of its end, so it is permanently that far from what the host reports; measured
     * against a door's thousandth it would never agree, and this side would re-apply the same
     * correction once a second forever, firing the last keyframe edges every time. */
    if (mp_world_state_agrees_within(wire, &here,
                                     mp_world_state_tolerance(wire->type, local.length,
                                                              local.speed,
                                                              settled_at(now) - now))) {
        ++apply.agreed;
        return;
    }
    if (!mp_world_state_at_rest(wire->type, wire->dir)) {
        ++apply.wire_moving;
        return;
    }
    if (!mp_world_state_at_rest(local.type, local.dir)) {
        ++apply.local_moving;
        return;
    }
    if (!apply.can_write) {
        return;
    }

    if (mp_world_state_is_one_shot(wire->type)) {
        apply_one_shot(world, mover, now, wire, &local);
        return;
    }

    /* The shape the engine uses itself when it puts a mover somewhere from outside, which is its
     * savegame restore at 0x0040AD01, read line for line; per record, in this order:
     *
     *     if (mover->active == 0 && rec.active != 0)      bapmap_openMover(world, mover->id);
     *     else if (mover->active == 0 || rec.active != 0) mover->dir = rec.dir;
     *     else                                            bapmap_closeMover(world, mover->id);
     *     mover->pose     = rec.pose;
     *     mover->timeBase = rec.timeBase + (world->worldTime minus rec.savedNow);
     *     mover->dwell    = rec.dwell;
     *     bapmap_tickMover(mover, world->worldTime + 1e-05);
     *     mover->dir      = rec.dir;
     *
     * The time base is written as the world clock itself rather than rebased, because the pose
     * travels too and the next local integration should see a step of nothing.
     *
     * The list membership is flipped through the engine's own opener or closer rather than by
     * writing the field, because those two carry the cell occupancy and the face state with them.
     * When no flip is needed the direction is written here as well as after the integration, which
     * is what the savegame restore does and what makes the settling run in the arm the host is in
     * rather than in the arm this side happened to be in. */
    if (local.active == 0u && wire->active != 0u) {
        if (!mp_world_gate(world, wire->id, true)) {
            ++apply.gate_refused;
            return;
        }
    } else if (local.active != 0u && wire->active == 0u) {
        if (!mp_world_gate(world, wire->id, false)) {
            ++apply.gate_refused;
            return;
        }
    } else if (!memory_try_write(mover + MOVER_DIR, &want_dir, sizeof want_dir)) {
        ++apply.write_faults;
        return;
    }

    pose  = wire->pose * local.length;
    dwell = wire->dwell;
    base  = now;
    if (!memory_try_write(mover + MOVER_POSE, &pose, sizeof pose) ||
        !memory_try_write(mover + MOVER_TIMEBASE, &base, sizeof base) ||
        !memory_try_write(mover + MOVER_DWELL, &dwell, sizeof dwell)) {
        ++apply.write_faults;
        return;
    }

    /* One integration, and it is not a formality. Its tail calls the pose application at
     * 0x00409696, which composes the sub node matrices, so the geometry follows the pose; it
     * evaluates the keyframe flag word of the segment the new pose falls in against the latch at
     * node+0x44, edge wise, so the collision faces, the sounds and the body reveal follow it too;
     * and at the head of its loop it refreshes each sub node's previous world position, node+0x7C
     * to 0x84 from node+0x38 to 0x40, which is what the rider carrier takes its difference from.
     * Without it the jump would be waiting under the feet of whoever is standing on this mover
     * when the next substep asks.
     *
     * It can also cost the dwell that was just written, and that is the engine's own behaviour
     * rather than an oversight here. Opening a door that was closed leaves it in the arm that
     * drives the pose upward, and this integration then finds the pose already at the far end and
     * walks the arm on to the latched one, which starts the dwell over. The result is a door that
     * stays open for its whole authored hold, which is the right answer for a door the far player
     * has just walked through. */
    apply.tick(mover, settled_at(now));

    /* The direction on top of the settled state, because the integration may have advanced the arm
     * it was given. The host's arm is the one that is true. */
    if (!memory_try_write(mover + MOVER_DIR, &want_dir, sizeof want_dir)) {
        ++apply.write_faults;
        return;
    }
    ++apply.applied;
}

void mp_world_apply_pending(void)
{
    uint32_t world;
    uint32_t count = 0;
    float    now   = 0.0f;
    size_t   index;

    if (!apply.held || apply.host) {
        return;
    }
    world = mp_world_pointer();
    if (!mp_world_mover_count(world, &count)) {
        return;   /* no level open yet; the note waits rather than being spent on nothing */
    }

    /* A mover id is an index into one level's table and a session outlives a level load. The
     * mover count is the identity, and it differs in all eleven shipped levels. */
    if (apply.note.level != (uint16_t)count) {
        ++apply.elsewhere;
        apply.held = false;
        return;
    }
    if (!memory_try_read(world + WORLD_CLOCK, &now, sizeof now) || now != now) {
        return;
    }
    apply.held = false;
    for (index = 0; index < apply.note.count; ++index) {
        judge_and_apply(world, count, now, &apply.note.entry[index]);
    }
}

void mp_world_apply_note_refused(void)
{
    ++apply.send_refused;
}

void mp_world_apply_report(void)
{
    if (!apply.installed) {
        return;
    }
    if (apply.host) {
        log_info("  the map's state note: %u sent describing %u mover(s) away from the position "
                 "the level authored, largest %u byte(s); %u over the cap, %u refused, %u the "
                 "channel would not take; %u note(s) arrived here and were refused because this "
                 "side owns the map",
                 (unsigned)apply.notes_out, (unsigned)apply.described, (unsigned)apply.largest,
                 (unsigned)apply.capped, (unsigned)apply.refused, (unsigned)apply.send_refused,
                 (unsigned)apply.wrong_role);
        return;
    }
    log_info("  the map's state note: %u received, %u torn, %u about another level, %u from "
             "before a level change; %u entry(s) held against this map",
             (unsigned)apply.notes_in, (unsigned)apply.torn, (unsigned)apply.elsewhere,
             (unsigned)apply.stale, (unsigned)apply.entries);
    /* The measurement the whole module is for, over every type and not only the two that are
     * corrected: it is the size of the defect rather than the size of the fix. */
    log_info("    the movers this side was behind on: %u, reported by the host as away from the "
             "position the level authored while this side had them exactly where the level put "
             "them",
             (unsigned)apply.at_home);
    log_info("    of those entries: %u already agreed, %u a type this side does not correct, "
             "%u named a mover this level has not got, %u unreadable, %u a type the two sides "
             "disagreed about",
             (unsigned)apply.agreed, (unsigned)apply.other_type, (unsigned)apply.missing,
             (unsigned)apply.unreadable, (unsigned)apply.type_disagreement);
    log_info("    the doors and buttons: %u applied, %u left alone because the host's was "
             "travelling, %u because this side's was, %u the opener refused, %u write fault(s)%s",
             (unsigned)apply.applied, (unsigned)apply.wire_moving, (unsigned)apply.local_moving,
             (unsigned)apply.gate_refused, (unsigned)apply.write_faults,
             apply.can_write ? "" : "; this build resolved no integrator and wrote nothing");
}
