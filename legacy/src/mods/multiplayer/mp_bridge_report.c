/* mp_bridge_report.c: the far body's half of the bridge report.
 *
 * It was pulled out of mp_bridge.c the moment that file reached its limit, and it is the right
 * cut rather than the convenient one: everything here READS and nothing steers. What it prints
 * comes from the interpolator, the puppet, the body module and the effect layer, each through
 * that module's own counters, plus the handful of the bridge's own numbers that arrive as an
 * argument. The bridge keeps no state on this file's behalf and this file holds none of its own,
 * so the two can be read apart.
 *
 * The map reports itself, at the end, because what it has to say is a variable number of lines:
 * one per mover type that was actually compared.
 *
 * The session line joined it on 2026-09-04 for the same reason: it describes a session rather
 * than the bridge, it reads two of them and steers neither, and leaving it in the caller meant
 * every counter split there pushed that file back over its limit.
 *
 * SIZE NOTE: over 600 lines, and every one of them prints. The file has no branch of its own
 * beyond the two shapes of run, so length here buys nothing back the way it does in a module
 * that decides something: it is a list. The seam, when it is wanted, is the far body: the body,
 * the puppet and the effects on one side and the bridge's own traffic on the other, which is the
 * same cut that made this file in the first place.
 */
#include "mp_bridge_report.h"

#include "mp_entropy.h"

#include "mp_bridge_appearance.h"
#include "mp_bridge_drain.h"
#include "mp_bridge_far.h"
#include "mp_bridge_relay.h"
#include "mp_bridge_lobby.h"
#include "mp_bridge_lobby_late.h"
#include "mp_bridge_roster.h"
#include "mp_bridge_seats.h"
#include "mp_bridge_world.h"
#include "mp_puppet.h"
#include "mp_puppet_shot.h"
#include "mp_puppet_starter.h"
#include "mp_text.h"

#include "mp_content.h"
#include "mp_bank.h"
#include "mp_blade.h"
#include "mp_blade_draw.h"
#include "mp_body.h"
#include "mp_body_wear.h"
#include "mp_cadence.h"
#include "mp_effects.h"
#include "mp_enemy.h"
#include "mp_memory_watch.h"
#include "mp_menu.h"
#include "mp_movie_gate.h"
#include "mp_phases.h"
#include "mp_interp.h"
#include "mp_puppet.h"
#include "mp_session.h"
#include "mp_state_note_rule.h"
#include "mp_timeline.h"
#include "mp_twist.h"
#include "mp_enemy_relay.h"
#include "mp_range_gate.h"
#include "mp_level_switch.h"
#include "mp_stopwatch.h"
#include "mp_npc_copies_bridge.h"
#include "mp_npc_shot_relay.h"
#include "mp_enemy_sync.h"
#include "mp_pickup_relay.h"
#include "mp_dialog_relay.h"
#include "mp_quest_relay.h"
#include "mp_scratch_wire.h"
#include "mp_hit_relay.h"
#include "mp_use_latch.h"
#include "mp_world.h"
#include "mp_world_anchor.h"

#include "common/logging.h"
#include "common/text.h"

#include <windows.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

void mp_bridge_report_round(const mp_bridge_report_round_t *round)
{
    if (round == NULL) {
        return;
    }
    log_info("  the round: %s on world change %u, %u substep(s) gone, outcome %u winner %u; "
             "deaths %u scored, %u refused, %u after the end, %u heard with no round running; "
             "tables %u sent, %u unsent, %u taken, %u torn, %u adopted, %u refused at the "
             "authority; %u round(s) decided",
             round->running ? "running" : "none", (unsigned)round->generation,
             (unsigned)round->elapsed, (unsigned)round->outcome, (unsigned)round->winner,
             (unsigned)round->deaths_taken, (unsigned)round->deaths_refused,
             (unsigned)round->deaths_after_end, (unsigned)round->deaths_outside,
             (unsigned)round->boards_sent, (unsigned)round->boards_unsent,
             (unsigned)round->boards_taken, (unsigned)round->boards_torn,
             (unsigned)round->boards_adopted, (unsigned)round->boards_refused,
             (unsigned)round->rounds_ended);
}

/* The kinds in a mask as numbers, for a line that has to stay readable when a session sees
 * three of them and when it sees twenty. The engine's kind numbers are the names here:
 * there is no shipped table of words for them, and inventing one would be a second
 * vocabulary for something the wire and the log already agree on. */
static void shot_kinds_text(uint64_t mask, char *out, size_t size)
{
    size_t   used = 0;
    uint32_t kind;

    out[0] = '\0';
    for (kind = 0; kind < 64u && used + 4u < size; ++kind) {
        if ((mask & ((uint64_t)1u << kind)) != 0u) {
            used += used == 0u ? text_format(out + used, size - used, "%u", (unsigned)kind)
                               : text_format(out + used, size - used, " %u", (unsigned)kind);
        }
    }
    if (used == 0u) {
        text_format(out, size, "none");
    }
}

/* The far body's stream: what arrived, how the buffer and the timeline took it, and the pumps
 * that drained it. */
static void report_stream(const mp_interp_t *interp, const mp_timeline_t *timeline,
                          const mp_bridge_report_run_t *run,
                          const mp_puppet_counters_t *counters)
{
    log_info("  %u substep(s) with a peer, which is how many states this side sent and how many it "
             "could have received; a gap to what the world line reports is either the wire or this "
             "side's own ring, and the session line's stale count tells them apart",
             (unsigned)run->joined_substeps);
    log_info("  the buffer: target lag %u tick(s), %s, deepest dip in the window %u, %u change(s) "
             "to the target",
             (unsigned)mp_timeline_target_lag(timeline),
             mp_timeline_auto_lag(timeline) ? "measured" : "held",
             (unsigned)mp_timeline_dip(timeline),
             (unsigned)mp_timeline_target_changes(timeline));
    log_info("  the timeline: %u inserted, %u skipped, %u resync(s), %u halt(s), render tick %u "
             "against the newest %u; %u underrun(s); events late %u / forced %u",
             (unsigned)mp_timeline_inserted(timeline), (unsigned)mp_timeline_skipped(timeline),
             (unsigned)mp_timeline_resyncs(timeline), (unsigned)mp_timeline_halts(timeline),
             (unsigned)mp_timeline_render_tick(timeline),
             (unsigned)mp_timeline_newest_tick(timeline),
             (unsigned)mp_interp_underruns(interp),
             (unsigned)counters->events_late, (unsigned)counters->events_forced);
    {
        uint32_t stretches = 0, total = 0, worst = 0;

        mp_bridge_drain_still_counts(&stretches, &total, &worst);
        if (stretches != 0u) {
            log_info("  the host's world stood still %u time(s) while its packets kept arriving, "
                     "%u ms in all, the longest %u ms", (unsigned)stretches, (unsigned)total,
                     (unsigned)worst);
        }
    }
    if (mp_bridge_world_bodies_off_the_model() + mp_bridge_world_off_the_model_in(3u) +
            mp_bridge_world_off_the_model_in(4u) != 0u) {
        log_info("  this side's own body was read off its model on %u substep(s), because a "
                 "script had stopped the player module for a scene: %u idle, %u quitting, "
                 "%u dying, %u respawning; %u sample(s) taken off the block instead because the "
                 "module was dying or respawning",
                 (unsigned)mp_bridge_world_bodies_off_the_model(),
                 (unsigned)mp_bridge_world_off_the_model_in(0u),
                 (unsigned)mp_bridge_world_off_the_model_in(2u),
                 (unsigned)mp_bridge_world_off_the_model_in(3u),
                 (unsigned)mp_bridge_world_off_the_model_in(4u),
                 (unsigned)(mp_bridge_world_off_the_model_in(3u) +
                            mp_bridge_world_off_the_model_in(4u)));
    }
    log_info("  the pumps: timer pump %u / frame pump %u / refused inside a tick %u; %u of them "
             "drained %u payload(s) between substeps; %u substep end(s) reached, which is where "
             "this side SENDS (%u of them found no task half and sent nothing)",
             (unsigned)run->timer_pumps, (unsigned)run->frame_pumps,
             (unsigned)run->pumps_refused, (unsigned)run->idle_drains,
             (unsigned)run->idle_payloads, (unsigned)run->substep_ends,
             (unsigned)run->ends_without_tick);
}

/* The puppet as a body: its hero, its clips, its hurt, its ground, its weapon and the engine
 * starters behind them. */
static void report_puppet_motion(const mp_puppet_counters_t *counters)
{
    {
        uint32_t agreed = 0, apart = 0, worst = 0;

        mp_puppet_hero_counts(&agreed, &apart, &worst);
        log_info("  the far players' heroes: %u body(s) agreed with the wire within %u sample(s) "
                 "at worst, %u still apart after the grace", (unsigned)agreed, (unsigned)worst,
                 (unsigned)apart);
    }
    log_info("  the puppet: base starts %u cut / %u fade, %u seed(s), %u seek(s) (%u across a "
             "marker), overlay starts %u / stops %u, %u start(s) withheld for the aux, guard "
             "refusals %u, ordinal refusals %u, track write faults %u; %u shot(s) unplaced, %u "
             "push(es) dropped, %u event(s) dropped, %u placement write fault(s)",
             (unsigned)counters->anim.base_starts_cut, (unsigned)counters->anim.base_starts_fade,
             (unsigned)counters->anim.seeds, (unsigned)counters->anim.seeks,
             (unsigned)counters->anim.seeks_across_marker,
             (unsigned)counters->anim.overlay_starts, (unsigned)counters->anim.overlay_stops,
             (unsigned)counters->anim.overlay_starts_withheld,
             (unsigned)counters->anim.guard_refusals, (unsigned)counters->anim.refused_ordinals,
             (unsigned)counters->anim.write_faults, (unsigned)counters->shots_unplaced,
             (unsigned)counters->pushes_dropped, (unsigned)counters->events_dropped,
             (unsigned)counters->write_faults);
    {
        uint32_t offered = 0, in_band = 0, voices = 0, swallowed = 0;
        uint32_t blood = 0, no_node = 0, no_sphere = 0, refused = 0;

        mp_effects_hurt_counters(&offered, &in_band, &voices, &swallowed, &blood, &no_node,
                                 &no_sphere, &refused);
        /* Eight numbers, and each one answers a different `nothing happened, because`. The
         * proof that the voice has a PLACE is not here but in `sounds given a place`, which
         * has to rise with this: a pain voice falls through the engine's own funnel with no
         * position, and only the anchor the puppet window holds open gives it one. */
        log_info("  a hurt far body: %u damage contact(s) offered, %u in the codes the "
                 "engine would take, %u pain voice(s) played, %u swallowed by the drop "
                 "timer | blood: %u drawn, %u with no node named, %u on a node with no "
                 "sphere, %u refused for a site that did not resolve",
                 (unsigned)offered, (unsigned)in_band, (unsigned)voices, (unsigned)swallowed,
                 (unsigned)blood, (unsigned)no_node, (unsigned)no_sphere, (unsigned)refused);
    }
    {
        uint32_t on_floor = 0;
        uint32_t airborne = 0;
        uint32_t ticked = 0;

        mp_phases_ground_counts(&on_floor, &airborne, &ticked);
        /* A sentence of its own, so the line above keeps the words it is found by when two runs'
         * reports are compared. This is a counter check and not a switch any more: a puppet's floor
         * polygon is zero for the life of a session, decided at the code rather than in a run, so
         * the left number is expected to stay at zero and anything there means the reading is
         * wrong. The footfalls come from a floor probed under the reported position instead; see
         * mp_footstep.h. The third number is the substeps this machine ticked itself, where the
         * engine filled the cell in phase one and the other two would say nothing at all about the
         * wire. */
        log_info("  the far bodies on the ground: %u substep(s) standing on a floor polygon, "
                 "%u with none", (unsigned)on_floor, (unsigned)airborne);
        if (ticked != 0u) {
            log_info("    and %u substep(s) not asked, because this machine ticked the body "
                     "itself and the engine set the cell", (unsigned)ticked);
        }
    }
    if (counters->anim.seeks != 0u) {
        /* The count alone cannot say whether the head crept past the tolerance or lost its place,
         * and those two want opposite fixes. The buckets are in frames against a tolerance of one
         * and a half: everything in the first two is a drift, a spread into the last two is a
         * track that had to find its way back. */
        log_info("    the puppet's seeks by distance: under 2 frames %u, under 4 %u, under 8 %u, "
                 "under 16 %u, beyond %u; %u of them backwards",
                 (unsigned)counters->anim.seek_bucket[0], (unsigned)counters->anim.seek_bucket[1],
                 (unsigned)counters->anim.seek_bucket[2], (unsigned)counters->anim.seek_bucket[3],
                 (unsigned)counters->anim.seek_bucket[4], (unsigned)counters->anim.seeks_back);
    }
    log_info("  the puppet's weapon: %u change(s) from an event, %u dropped waiting to start, %u "
             "refused by the setter, %u withheld from the state for a busy track, %u never "
             "confirmed by the wire",
             (unsigned)counters->weapon_events, (unsigned)counters->weapon_dropped,
             (unsigned)counters->weapon_refused, (unsigned)counters->weapon_withheld,
             (unsigned)counters->weapon_expired);
    {
        mp_puppet_starter_counters_t starters;
        mp_body_wear_counters_t      blades;

        mp_puppet_starter_counters(&starters);
        mp_body_wear_get_counters(&blades);
        log_info("  the puppet's engine starters: %u weapon change(s), %u push(es) and %u midair "
                 "swing(s) not started because the body's actor lacks the clip (fewest clips seen "
                 "%u), %u change(s) to a slot past the weapon table, %u wait(s) on an actor that "
                 "did not read; %u blade length tick(s) withheld from a body whose spawn read no "
                 "blade, %u light tick(s) run only to take its light away; %u weapon change(s) "
                 "set straight into a worn body's block, %u of those writes refused",
                 (unsigned)starters.weapon_missing, (unsigned)starters.push_missing,
                 (unsigned)starters.midair_missing, (unsigned)starters.fewest_clips,
                 (unsigned)starters.slot_past_table, (unsigned)starters.unread,
                 (unsigned)blades.blade_ticks_bladeless,
                 (unsigned)blades.light_releases_bladeless,
                 (unsigned)starters.weapon_set_worn, (unsigned)starters.set_writes_refused);
    }
}

/* The puppet's blade and model: the sabre and its effects, the node rotations, the worn model,
 * the sweep against the level, and the health the wire wrote. */
static void report_puppet_blade(const mp_bridge_report_run_t *run,
                                const mp_puppet_counters_t *counters,
                                const mp_twist_counters_t *twists)
{
    {
        mp_blade_counters_t     kept;
        mp_body_wear_counters_t steps;

        mp_blade_get_counters(&kept);
        mp_body_wear_get_counters(&steps);
        log_info("  the blades: %u far Jedi spawn(s), %u from the book, %u from the local "
                 "player, %u from another bank, %u their own, %u folded; %u local spawn(s) or "
                 "restore(s), %u put right, %u folded; the book knows %u of %u asset(s), %u "
                 "raised, %u refused, %u reading(s) with a foreign hilt; %u write fault(s)",
                 (unsigned)kept.far_jedi, (unsigned)kept.far_book, (unsigned)kept.far_local,
                 (unsigned)kept.far_bank, (unsigned)kept.far_own, (unsigned)kept.far_folded,
                 (unsigned)kept.local_settled, (unsigned)kept.local_put_right,
                 (unsigned)kept.local_folded, (unsigned)kept.book_known,
                 (unsigned)MP_BLADE_BOOK_SIZE, (unsigned)kept.book_raised,
                 (unsigned)kept.book_refused, (unsigned)kept.foreign_hilts,
                 (unsigned)kept.write_faults);
        mp_blade_draw_report(steps.own_steps, steps.own_step_faults);
    }
    log_info("  %u sabre event(s) performed on the puppet: %u swing(s), %u block(s), %u "
             "parries, %u disarm(s) by event and %u by the fallback; %u refused, %u dropped "
             "waiting for the aux, %u dropped waiting for a track, %u clip(s) unplayed, %u write "
             "fault(s); blade %s",
             (unsigned)(counters->sabre.swings + counters->sabre.blocks + counters->sabre.parries +
                        counters->sabre.disarms),
             (unsigned)counters->sabre.swings, (unsigned)counters->sabre.blocks,
             (unsigned)counters->sabre.parries, (unsigned)counters->sabre.disarms,
             (unsigned)counters->sabre.fallback_disarms, (unsigned)counters->sabre.refused,
             (unsigned)counters->sabre.held_dropped, (unsigned)counters->sabre.track_dropped,
             (unsigned)counters->sabre.overlays_unplayed, (unsigned)counters->sabre.write_faults,
             mp_puppet_sabre_armed(MP_BRIDGE_DRAIN_FAR_BANK) ? "armed" : "at rest");
    log_info("  the puppet's blade effects: %u armed contact(s) offered (%u blade against blade), "
             "%u effect set(s) played (%u of them without a voice), %u swallowed by the lock, "
             "%u refused; passed over: %u body message(s), %u on an unarmed blade",
             (unsigned)mp_effects_contacts(), (unsigned)mp_effects_blade_contacts(),
             (unsigned)mp_effects_played(), (unsigned)mp_effects_without_voice(),
             (unsigned)mp_effects_suppressed(), (unsigned)mp_effects_refused(),
             (unsigned)mp_effects_body_messages(), (unsigned)mp_effects_not_armed());
    log_info("  the puppet's node rotations: %s; %u frame(s) drawn from the substep pair, %u "
             "substep(s) that produced no new value, %u stall(s) the frame half closed itself, %u "
             "frame(s) refused because no object was named by both the window and the bank, %u "
             "claim(s) given up after a run of frames with no substep behind them, %u substep(s) "
             "withheld because the far player wore a model this side does not draw",
             twists->installed ? "drawn once per rendered frame on the engine's own weight"
                              : "left on the simulation clock, one step per substep",
             (unsigned)twists->frames, (unsigned)twists->collapses, (unsigned)twists->stalls,
             (unsigned)twists->refusals, (unsigned)twists->given_up, (unsigned)twists->withheld);
    {
        mp_body_wear_counters_t wear;

        mp_body_wear_get_counters(&wear);
        log_info("  the far models: %u asked, %u worn, %u refused, %u body(ies) never answered, "
                 "%u rebuilt and %u take down(s) for it refused, %u blade tick(s) withheld, %u "
                 "length step(s) without a mesh (%u refused), %u "
                 "swing(s) of a worn body (%u kept their contact, %u contact node(s) put "
                 "back to 0), %u weapon node(s) found by name, "
                 "%u light tick(s) run only to take the light away (%u slot write(s) "
                 "refused), %u substep(s) of rotations withheld; %u size(s) "
                 "written, %u wish(es) published again as a body went down, %u refused by the "
                 "channel",
                 (unsigned)wear.asked, (unsigned)wear.worn, (unsigned)wear.refused,
                 (unsigned)wear.unanswered, (unsigned)wear.rebuilt,
                 (unsigned)wear.rebuilds_refused, (unsigned)wear.blade_ticks_withheld,
                 (unsigned)wear.blade_steps, (unsigned)wear.blade_step_faults,
                 (unsigned)counters->sabre.worn_swings,
                 (unsigned)counters->sabre.swing_contacts_kept,
                 (unsigned)counters->sabre.swing_contacts_withheld,
                 (unsigned)wear.weapon_nodes,
                 (unsigned)wear.light_releases, (unsigned)wear.slot_writes_refused,
                 (unsigned)twists->withheld, (unsigned)wear.sizes_written,
                 (unsigned)wear.notes_down, (unsigned)wear.publish_faults);
    }
    log_info("  the puppet's blade against the level: %u swept substep(s), %u of them on a body "
             "in a borrowed model, %u seeded instead (a fresh swing or a placement jump), %u "
             "withheld because the contact node answered no sphere",
             (unsigned)mp_effects_sweeps(), (unsigned)mp_effects_sweeps_worn(),
             (unsigned)mp_effects_sweeps_seeded(), (unsigned)mp_effects_sweeps_unprobed());
    log_info("  health from the wire: last %u, %u write(s), %u refused; puppet died %u / "
             "revived %u, %u shadow fault(s); contacts suppressed %u; content fingerprint %08X "
             "(%s)",
             (unsigned)counters->last_health, (unsigned)counters->health_writes,
             (unsigned)counters->health_refused, (unsigned)counters->died,
             (unsigned)counters->revived, (unsigned)counters->shadow_faults,
             (unsigned)mp_body_contacts_suppressed(), (unsigned)run->content,
             run->content_decided ? (run->content != 0u ? "named to the session"
                                                             : "unreadable, check skipped")
                                    : "not yet read, no substep has run");
}

/* Every module that says its own part of the far body's report, in the order they are read. */
static void report_modules(const mp_bridge_report_run_t *run)
{
    mp_bridge_appearance_report();
    mp_content_report();
    mp_world_report();
    mp_scratch_wire_report();
    mp_enemy_sync_report();
    mp_enemy_sync_report_let_go();
    mp_enemy_relay_report();
    mp_range_gate_report();
    mp_world_anchor_report();
    mp_level_switch_report();
    mp_stopwatch_report();
    mp_cadence_report(run->is_host);
    mp_npc_copies_bridge_report();
    mp_npc_shot_relay_report();
    mp_pickup_relay_report();
    mp_quest_relay_report();
    mp_dialog_relay_report();
    mp_use_latch_report();
    mp_hit_relay_report();
    mp_enemy_report();
    mp_memory_watch_report();
    mp_menu_report();
}

void mp_bridge_report_far_body(const mp_interp_t *interp, const mp_bridge_report_run_t *run)
{
    const mp_timeline_t  *timeline = mp_interp_timeline(interp);
    mp_puppet_counters_t  counters;
    mp_twist_counters_t   twists;

    mp_puppet_get_counters(&counters);
    mp_twist_get_counters(&twists);
    report_stream(interp, timeline, run, &counters);
    report_puppet_motion(&counters);
    report_puppet_blade(run, &counters, &twists);
    report_modules(run);
}

/* One peer's channel and payload, two sentences, what the sums above cannot say about four players:
 * which peer the payload crowded, which one answered late, which one went past its rate. The peer
 * and its world slot stand after the colon, so the label the line is found by, when two runs'
 * reports are compared, is the same for every peer. */
static void report_one_peer(const mp_session_t *session, size_t index, uint8_t slot)
{
    const mp_peer_t          *peer    = mp_session_peer(session, index);
    const mp_channel_t       *channel = &peer->channel;
    const mp_session_meter_t *meter   = &peer->meter;

    log_info("  the reliable channel to one peer: peer %u, slot %u, %u packet(s) with a payload, "
             "%u with messages only (%u of them the overflow of a full payload, %u more held "
             "back by the budget); least room beside a payload %u byte(s); %u seat(s) lost to "
             "the payload; longest wait for a seat %u ms; longest unanswered %u ms (tag %02X, "
             "%u byte(s)); deepest queue %u of %u",
             (unsigned)index, (unsigned)slot, (unsigned)peer->packets_with_payload,
             (unsigned)peer->packets_without_payload, (unsigned)peer->overflow_packets,
             (unsigned)peer->held_by_budget, (unsigned)mp_channel_least_room_left(channel),
             (unsigned)mp_channel_seats_lost_to_payload(channel),
             (unsigned)mp_channel_longest_seat_wait_ms(channel),
             (unsigned)mp_channel_longest_unacked_ms(channel),
             (unsigned)mp_channel_longest_unacked_tag(channel),
             (unsigned)mp_channel_longest_unacked_bytes(channel),
             (unsigned)mp_channel_deepest_pending(channel), (unsigned)MP_CHANNEL_SEND_SLOTS);
    log_info("  the payload to one peer: peer %u, slot %u, bytes per packet mean %u, 95th %u, "
             "most %u; of that, bodies at most %u, the enemy block mean %u and most %u, messages "
             "beside it mean %u; the most bytes in one second %u, the most packets %u",
             (unsigned)index, (unsigned)slot,
             (unsigned)(meter->packets != 0u ? meter->bytes_sum / meter->packets : 0u),
             (unsigned)mp_session_meter_percentile(meter, 95u), (unsigned)meter->bytes_most,
             (unsigned)meter->bodies_most,
             (unsigned)(meter->parts != 0u ? meter->enemy_sum / meter->parts : 0u),
             (unsigned)meter->enemy_most,
             (unsigned)(meter->packets != 0u ? meter->messages_sum / meter->packets : 0u),
             (unsigned)meter->second.most_bytes, (unsigned)meter->second.most_packets);
}

/* Every peer slot that carried a packet on its present connection, and the session's upload
 * against the relay's ceilings (mp-relay, internal/config/config.go): a megabyte a second for a
 * session both ways, 1024 packets a second from a host, 256 to one member. */
static void report_peers(const mp_session_t *host, const mp_session_t *client)
{
    uint32_t most_to_one = 0u;
    size_t   index;

    for (index = 0; index < MP_SESSION_MAX_PEERS; ++index) {
        const mp_peer_t *peer = mp_session_peer(host, index);

        if (peer->packets_with_payload + peer->packets_without_payload != 0u) {
            report_one_peer(host, index, mp_session_slot_of_peer(index));
        }
        if (most_to_one < peer->meter.second.most_packets) {
            most_to_one = peer->meter.second.most_packets;
        }
    }
    if (mp_session_peer(client, 0u)->packets_with_payload +
            mp_session_peer(client, 0u)->packets_without_payload != 0u) {
        report_one_peer(client, 0u, 0u);   /* a client's one peer is the host, at slot 0 */
    }
    log_info("  the session's upload: %u byte(s) a second at the most over all peers (the relay "
             "brakes a session at 1048576 both ways), %u packet(s) a second at the most (a host "
             "at 1024), at the most %u to one peer (a member at 256)",
             (unsigned)(host->upload.most_bytes > client->upload.most_bytes
                            ? host->upload.most_bytes : client->upload.most_bytes),
             (unsigned)(host->upload.most_packets > client->upload.most_packets
                            ? host->upload.most_packets : client->upload.most_packets),
             (unsigned)most_to_one);
}

/* The nine state notes, one sentence each, named rather than numbered so that each is its own line
 * when two runs' reports are compared. What a note was spared says whether the newest state is what
 * travels: copies overwritten before they went, copies in flight shrunk to four bytes, repeats left
 * out while an equal copy was on its way, and copies that waited outside a channel behind a shrunk
 * one. The last line is the far side's half: the shrunk copies that arrived empty and were
 * skipped. */
static void report_state_notes(const mp_session_t *host, const mp_session_t *client)
{
    size_t index;

    for (index = 0; index < MP_STATE_NOTE_KINDS; ++index) {
        uint8_t                 kind = mp_state_note_kind_at(index);
        mp_channel_state_kind_t on_host;
        mp_channel_state_kind_t on_client;

        mp_session_state_totals(host, kind, &on_host);
        mp_session_state_totals(client, kind, &on_client);
        log_info("  the state notes, %s: sent %u, replaced before they went out %u, shrunk to "
                 "nothing in flight %u, repeats left out while one was on its way %u, waited "
                 "outside the channel behind a shrunk one %u", mp_state_note_name(kind),
                 (unsigned)(on_host.sent + on_client.sent),
                 (unsigned)(on_host.replaced + on_client.replaced),
                 (unsigned)(on_host.shrunk + on_client.shrunk),
                 (unsigned)(on_host.unchanged + on_client.unchanged),
                 (unsigned)(on_host.waited + on_client.waited));
    }
    log_info("  the reliable notes: %u empty note(s) of a superseded state skipped",
             (unsigned)(mp_session_empty_notes(host) + mp_session_empty_notes(client)));
}

/* Both sessions, added together: a listen host runs the host side and a client the client side,
 * and only one of them is ever connected, so a sum reads as the one that was. The two payload
 * counters are printed apart because they are opposite findings. A reordering over a local link is
 * close to impossible and points at the transport; an overrun means arrivals outran the drain,
 * which is a stall on one of the two sides and the same stall the timeline pays for in target lag.
 */
void mp_bridge_report_sessions(const mp_session_t *host, const mp_session_t *client)
{
    uint32_t overrun = mp_session_payloads_overrun(host) + mp_session_payloads_overrun(client);
    uint32_t fresh   = mp_session_payloads_overrun_fresh(host) +
                       mp_session_payloads_overrun_fresh(client);
    uint32_t backlog = mp_session_deepest_backlog(host);
    uint32_t waited  = mp_session_longest_wait_ms(host);

    if (backlog < mp_session_deepest_backlog(client)) {
        backlog = mp_session_deepest_backlog(client);
    }
    if (waited < mp_session_longest_wait_ms(client)) {
        waited = mp_session_longest_wait_ms(client);
    }
    log_info("  the session saw %u join(s), %u drop(s), %u leave(s), %u denial(s), %u payload(s) "
             "reordered and %u overrun, last refusal reason %u (1 full, 2 protocol version, "
             "3 content, 4 game mode, 5 password, 6 behind, 7 a required mod, 8 a DLL not "
             "allowed)",
             (unsigned)(mp_session_joins(host) + mp_session_joins(client)),
             (unsigned)(mp_session_drops(host) + mp_session_drops(client)),
             (unsigned)(mp_session_leaves(host) + mp_session_leaves(client)),
             (unsigned)(mp_session_denied(host) + mp_session_denied(client)),
             (unsigned)(mp_session_payloads_reordered(host) +
                        mp_session_payloads_reordered(client)),
             (unsigned)overrun, (unsigned)mp_session_last_deny(client));
    log_info("  the handshake: %u response(s) with a cookie this host never made, %u that did "
             "not know the password, %u digest(s) the system would not make",
             (unsigned)(host->cookies_refused + client->cookies_refused),
             (unsigned)(host->proofs_refused + client->proofs_refused),
             (unsigned)(host->digest_faults + client->digest_faults));
    /* What the overrun count on its own could not say. A discard that threw away a payload the
     * drain had been awake for is the only one that argues for a deeper ring; the rest were
     * thrown away while no substep ran, and the answer to those is the drain between substeps.
     * The deepest backlog and the longest wait keep their meaning once the discards are gone. */
    log_info("  the payload ring of %u: %u of the %u discard(s) happened with the drain awake "
             "(under %u ms), deepest backlog %u, longest wait %u ms",
             (unsigned)MP_SESSION_PAYLOAD_RING, (unsigned)fresh, (unsigned)overrun,
             (unsigned)MP_SESSION_DRAIN_STALL_MS, (unsigned)backlog, (unsigned)waited);
    /* The reliable channel against the payload, which reserves its room first. The two packet
     * counts say whether a packet without a payload, the only packet a large message is sure of
     * a seat in, was ever built; the seats lost say how often a due message was passed over for
     * the payload alone; and the longest wait says whether "passed over" meant a packet or a
     * stall. A large seats count with a longest wait under the throttle is ordinary; a wait of
     * seconds is a message the payload will never let in, with the ordered queue behind it. The
     * field run this line was written for had a 1043 byte chunk against a payload of 750 to 1000
     * bytes, skipped for good, and nothing in the report that said so; under this line that run
     * reads a seats count in the thousands and a longest wait of the whole level. The wait is a
     * wall clock difference, so the run comparison masks the milliseconds out of the line. */
    {
        size_t   room    = mp_session_least_room_left(host) != 0u
                               ? mp_session_least_room_left(host)
                               : mp_session_least_room_left(client);
        uint32_t unacked = mp_session_longest_unacked_ms(host) >
                                   mp_session_longest_unacked_ms(client)
                               ? mp_session_longest_unacked_ms(host)
                               : mp_session_longest_unacked_ms(client);

        /* The two numbers that say WHICH wait a channel is having. A message that is in every
         * packet and never answered and one that never gets a seat both show up as a wait in the
         * line below, and they want opposite repairs: the first is a far side that is not
         * reading, the second is a payload that leaves no room. */
        log_info("    the least room a payload packet left for messages %u byte(s); the longest "
                 "any message was out unanswered %u ms", (unsigned)room, (unsigned)unacked);
    }
    log_info("  the reliable channel: %u packet(s) left with a payload and %u without; %u seat(s) "
             "lost to the payload, the longest wait for a seat %u ms; %u build(s) held a "
             "message back for the far window, %u packet(s) refused here for a message past "
             "this side's window",
             (unsigned)(mp_session_packets_with_payload(host) +
                        mp_session_packets_with_payload(client)),
             (unsigned)(mp_session_packets_without_payload(host) +
                        mp_session_packets_without_payload(client)),
             (unsigned)(mp_session_seats_lost_to_payload(host) +
                        mp_session_seats_lost_to_payload(client)),
             (unsigned)(mp_session_longest_seat_wait_ms(host) >
                                mp_session_longest_seat_wait_ms(client)
                            ? mp_session_longest_seat_wait_ms(host)
                            : mp_session_longest_seat_wait_ms(client)),
             (unsigned)(mp_session_held_past_window(host) +
                        mp_session_held_past_window(client)),
             (unsigned)(mp_session_refused_past_window(host) +
                        mp_session_refused_past_window(client)));
    /* The inbox the channel's messages wait in for a reader. Over thirty two notes held at once
     * is a backlog the channel alone would have met by refusing packets, payloads and all; a
     * stall is the inbox full with notes left on a channel, which a long fight during a load on
     * this side can reach, and the payloads handed on are the ones that survived it. */
    log_info("  the note inbox: the most notes it held at once %u, the most bytes %u of %u; %u "
             "stall(s) with notes left on the channel for want of room; %u payload(s) handed on "
             "from packet(s) refused for the window",
             (unsigned)(mp_session_inbox_most_notes(host) > mp_session_inbox_most_notes(client)
                            ? mp_session_inbox_most_notes(host)
                            : mp_session_inbox_most_notes(client)),
             (unsigned)(mp_session_inbox_most_bytes(host) > mp_session_inbox_most_bytes(client)
                            ? mp_session_inbox_most_bytes(host)
                            : mp_session_inbox_most_bytes(client)),
             (unsigned)MP_INBOX_BYTES,
             (unsigned)(mp_session_inbox_stalls(host) + mp_session_inbox_stalls(client)),
             (unsigned)(mp_session_payloads_past_window(host) +
                        mp_session_payloads_past_window(client)));
    /* What a host kept for a peer whose channel had no room, and whom it sent away because it
     * could keep no more. The last count is the one that must stay at nought: every peer in it
     * was a player thrown out of the session. */
    {
        mp_session_hold_totals_t on_host;
        mp_session_hold_totals_t on_client;

        mp_session_hold_totals(host, &on_host);
        mp_session_hold_totals(client, &on_client);
        log_info("  the held messages: %u broadcast(s) had to be held for some of the peers and "
                 "%u note(s) sent singly were held, %u message(s) and %u byte(s) in all; %u "
                 "handed on from a hold later; the deepest hold %u message(s) on peer %u, the "
                 "longest a message waited in one %u ms; %u peer(s) sent away for falling behind "
                 "(must be 0), %u late packet(s) of theirs answered with the reason",
                 (unsigned)(on_host.broadcasts_partial + on_client.broadcasts_partial),
                 (unsigned)(on_host.singles_held + on_client.singles_held),
                 (unsigned)(on_host.held + on_client.held),
                 (unsigned)(on_host.held_bytes + on_client.held_bytes),
                 (unsigned)(on_host.delivered + on_client.delivered),
                 (unsigned)on_host.deepest, (unsigned)on_host.deepest_peer,
                 (unsigned)on_host.longest_ms,
                 (unsigned)(on_host.dropped_behind + on_client.dropped_behind),
                 (unsigned)(on_host.answers + on_client.answers));
    }
    report_peers(host, client);
    report_state_notes(host, client);
}

static BOOL CALLBACK find_unowned_window(HWND hwnd, LPARAM out)
{
    if (IsWindowVisible(hwnd) && GetWindow(hwnd, GW_OWNER) == NULL) {
        *(HWND *)out = hwnd;
        return FALSE;
    }
    return TRUE;
}

void mp_bridge_report_caption(bool is_host)
{
    HWND hwnd = NULL;
    char caption[64];

    EnumThreadWindows(GetCurrentThreadId(), &find_unowned_window, (LPARAM)&hwnd);
    if (hwnd != NULL) {
        text_format(caption, sizeof caption, "The Phantom Menace - %s",
                    mp_text(is_host ? MP_TEXT_CAPTION_HOST : MP_TEXT_CAPTION_CLIENT));
        SetWindowTextA(hwnd, caption);
    }
}

void mp_bridge_report_socket(const mp_udp_t *udp, const mp_session_t *host,
                             const mp_session_t *client)
{
    uint32_t got     = mp_udp_recv_datagrams(udp);
    uint32_t foreign = mp_session_refused_foreign(host) + mp_session_refused_foreign(client);
    uint32_t unheld  = mp_session_refused_unhandled(host) +
                       mp_session_refused_unhandled(client);
    uint32_t ip      = mp_udp_last_source_ip(udp);
    uint32_t sent    = mp_udp_sent_datagrams(udp);
    uint32_t failed  = mp_udp_send_failures(udp);

    if (got == 0u) {
        log_info("  the game socket on port %u was told NOTHING: no datagram of any kind "
                 "arrived while it was up, and it put %u out (%u refused by the local stack, "
                 "last error %d). Nothing from the far side reached this process, so the fault "
                 "is between the two: a filter in front of this port, a different address, or a "
                 "far side that never sent",
                 (unsigned)mp_udp_local_port(udp), (unsigned)sent, (unsigned)failed,
                 mp_udp_last_error(udp));
        return;
    }
    log_info("  the game socket on port %u: %u datagram(s) in, %u byte(s), last from "
             "%u.%u.%u.%u:%u, %u out (%u refused); %u had no endpoint slot left, %u carried a "
             "foreign magic, %u carried ours and no arm of the dispatch took them",
             (unsigned)mp_udp_local_port(udp), (unsigned)got,
             (unsigned)mp_udp_recv_bytes(udp),
             (unsigned)((ip >> 24) & 0xFFu), (unsigned)((ip >> 16) & 0xFFu),
             (unsigned)((ip >> 8) & 0xFFu), (unsigned)(ip & 0xFFu),
             (unsigned)mp_udp_last_source_port(udp), (unsigned)sent, (unsigned)failed,
             (unsigned)mp_udp_recv_unnumbered(udp), (unsigned)foreign, (unsigned)unheld);
    {
        int receive = 0;
        int send    = 0;

        mp_udp_buffer_bytes(udp, &receive, &send);
        log_info("    %u datagram(s) thrown away before the session saw them (too large, empty "
                 "or a stale reset); the socket holds %d byte(s) to receive and %d to send; %u "
                 "salt(s) had to come from the fallback generator",
                 (unsigned)mp_udp_recv_dropped(udp), receive, send,
                 (unsigned)mp_entropy_failures());
    }
}

/* Each far player's states as the world slot it holds, on a client as well as on a host, and how
 * many banks were described as their player left. */
static void report_far_states(void)
{
    char line[320];

    log_info("  the far players' states: %s",
             mp_bridge_far_states_text(line, sizeof line) != 0u ? line : "none has arrived");
    log_info("  the far players' departures: %u bank(s) described as their player left",
             (unsigned)mp_bridge_far_departures());
}

/* Whose moments this side queued for its puppets: each far bank's count beside the slot it shows
 * now, the moments that named a slot no bank here shows, and on a host the copies it passed on to
 * the other players. A client's acceptance of the host's relay is read here: the bank that shows
 * another client counts that client's shots, which used to reach nobody. */
static void report_moments(const mp_bridge_report_run_t *run)
{
    char     line[160];
    size_t   at = 0;
    size_t   bank;
    uint32_t passed  = 0;
    uint32_t refused = 0;

    mp_bridge_relay_counts(&passed, &refused);
    line[0] = '\0';
    for (bank = 1u; bank <= MP_BANK_FAR_MAX; ++bank) {
        uint32_t moments = mp_bridge_far_moments(bank);
        uint8_t  slot    = 0;

        if (moments == 0u) {
            continue;
        }
        if (mp_bridge_far_slot_of(bank, &slot)) {
            at += text_format(line + at, sizeof line - at, "%sbank %u (slot %u) %u",
                              at == 0u ? "" : ", ", (unsigned)bank, (unsigned)slot,
                              (unsigned)moments);
        } else {
            at += text_format(line + at, sizeof line - at, "%sbank %u (nobody now) %u",
                              at == 0u ? "" : ", ", (unsigned)bank, (unsigned)moments);
        }
        if (at + 1u >= sizeof line) {
            break;   /* the line is full, and what follows would be cut */
        }
    }
    /* A copy is sent or held; one is refused only when the peer's hold was full as well, and that
     * refusal sends the peer away. */
    log_info("  the far players' moments: %s; %u named a slot no bank here shows; %u passed on "
             "to the other players, %u refused by a full channel and a full hold, each sending "
             "that peer away",
             at != 0u ? line : "none has arrived", (unsigned)run->events_unplaced,
             (unsigned)passed, (unsigned)refused);
    log_info("  what the clients said: %u note(s) refused as a host's own or as said in "
             "another player's name", (unsigned)run->notes_refused);
}

void mp_bridge_report_all(const char *why, const mp_bridge_report_run_t *run,
                          const mp_session_t *host, const mp_session_t *client,
                          const mp_udp_t *udp)
{
    if (run == NULL) {
        return;
    }
    log_info("the bridge (%s, game mode %s) at %s: %s, %u command packet(s) sent, %u command(s) "
             "applied through the wire and %u refused, %u client state(s) landed, %u puppet "
             "placement(s), %u event(s) sent (%u reached nobody) and %u received, "
             "%u push(es) and %u shot(s) performed on the puppet",
             run->shape, run->game_mode_name != NULL ? run->game_mode_name : "unnamed", why,
             run->joined ? "joined" : "NOT JOINED", (unsigned)run->commands_sent,
             (unsigned)run->commands_applied, (unsigned)run->commands_refused,
             (unsigned)run->states_in, (unsigned)run->puppet_applies, (unsigned)run->events_sent,
             (unsigned)run->events_unsent, (unsigned)run->events_in,
             (unsigned)mp_puppet_force_pushes(), (unsigned)mp_puppet_shots());
    {
        /* A sentence of its own, so the line above keeps the words it is found by when two runs'
         * reports are compared. The total above says a far player fired and nothing says what:
         * every rule this feature holds about a kind is written against kinds it has never seen fly
         * in a session. It costs no wire byte; the kind is already in the event. */
        char kinds[128];

        shot_kinds_text(mp_puppet_shot_kinds(), kinds, sizeof kinds);
        log_info("  the kinds a far player's shots were spawned as here: %s", kinds);
    }
    mp_bridge_world_report(run->substep);
    if (run->is_loopback) {
        log_info("  the loopback delivered %u and dropped %u", (unsigned)run->loopback_delivered,
                 (unsigned)run->loopback_dropped);
        return;
    }
    /* Every bank first and the first bank's old lines after them: a comparison of two runs' reports
     * keeps the last line of a name, so the comparison against older runs goes on reading bank 1
     * against bank 1, and the other banks stand in the log above it. */
    mp_bridge_far_describe_banks();
    mp_bridge_report_far_body(mp_bridge_far_interp(MP_BRIDGE_DRAIN_FAR_BANK), run);
    report_far_states();
    report_moments(run);
    /* Its own sentence: the pumps line keeps the words it is found by when two runs' reports are
     * compared. */
    log_info("  the timer pump through a stall: %u pump(s) ran while no substep had run for a "
             "second or more, the longest such stretch %u ms", (unsigned)run->stall_pumps,
             (unsigned)run->longest_pumped_stall_ms);
    if (udp != NULL) {
        mp_bridge_report_socket(udp, host, client);
    }
    mp_bridge_report_sessions(host, client);
    mp_bridge_roster_report(client, run->is_host);
    mp_bridge_seats_report(run->is_host, mp_bridge_drain_my_slot());
    mp_bridge_lobby_report(run->is_host, !run->is_host);
    mp_bridge_lobby_late_report(run->is_host);
    mp_movie_gate_report();
}
