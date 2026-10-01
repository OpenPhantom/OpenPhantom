/* mp_arm_hulls.c: the hulls a session puts on the engine when it is armed. See the header.
 *
 * Split from multiplayer.c along the seam its size note named, the arming, when that file stood
 * two lines short of the hard limit with more hulls still to be installed from it. What moved
 * keeps its order and its lines; the one callback of that file's it needs, the death listener, is
 * handed in, and the two note takers the arming sets between these runs stayed where they were.
 */
#include "mp_arm_hulls.h"

#include "mp_actions.h"
#include "mp_bank.h"
#include "mp_body.h"
#include "mp_body_death.h"
#include "mp_bridge_lobby.h"
#include "mp_enemy_bind.h"
#include "mp_enemy_blast.h"
#include "mp_enemy_fx.h"
#include "mp_enemy_limb.h"
#include "mp_enemy_nodes.h"
#include "mp_enemy_pose.h"
#include "mp_enemy_relay.h"
#include "mp_enemy_spawn.h"
#include "mp_follow.h"
#include "mp_footstep.h"
#include "mp_hit_relay.h"
#include "mp_level_state.h"
#include "mp_level_switch.h"
#include "mp_node_map.h"
#include "mp_npc_shot_relay.h"
#include "mp_phases.h"
#include "mp_pickup_relay.h"
#include "mp_player_sound.h"
#include "mp_range_gate.h"
#include "mp_script_sound.h"
#include "mp_session_now.h"
#include "mp_sound.h"
#include "mp_target.h"
#include "mp_world_door.h"

#include "common/logging.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void mp_arm_hulls_hits(bool as_client, mp_hit_relay_death_fn_t on_death)
{
    /* The hits. Without them the host never learns that a client's shot connected, so its
     * enemies are not sometimes invulnerable to a client but entirely so. */
    if (mp_hit_relay_install()) {
        mp_hit_relay_set_client(as_client);
        /* And the other direction: what the host drops for a far player is reported to that
         * player's own machine, the only place it can honestly land. */
        mp_body_set_puppet_hit_listener(&mp_hit_relay_note_puppet_hit);
        /* And who fired each player side shot: a far player's copy is not this one's hit. */
        mp_body_set_shot_object_listener(&mp_hit_relay_note_own_shot);
        /* And an ally's: an actor of the player's side hurts no far player. */
        mp_body_set_ally_shot_listener(&mp_hit_relay_note_ally_shot);
        /* And which sender is a bolt, so the gate between players reads a bolt's side. */
        mp_body_death_set_shot_test(&mp_hit_relay_is_a_shot);
        mp_body_death_set_ally_test(&mp_hit_relay_sender_is_an_ally);
        mp_body_death_set_far_shot_test(&mp_hit_relay_far_shot_bank);
        /* And the death, which is the victim's to report: only the machine a player died on sees
         * both that he died and which bank's contact ended it. */
        mp_body_set_death_listener(&mp_hit_relay_note_death);
        /* And the score, which is what a death is FOR in a deathmatch. A death this machine
         * reported and one off the wire both come through this one listener, so the table is fed
         * exactly once per death on every machine. */
        mp_hit_relay_set_death_listener(on_death);
        log_info("a death of this player is reported to the far side with the slot that caused "
                 "it, and both sides hand it to the round's table and to the re-entry rules");
    }

    /* And the NPCs' bolts, which a client's parked replicas never fire. A hit by one is
     * decided where its victim sits, and the hit relay asks which bolts those are. */
    if (mp_npc_shot_relay_install()) {
        mp_npc_shot_relay_set_host(!as_client);
        mp_hit_relay_set_npc_owned(&mp_npc_shot_relay_npc_owned);
    }

    /* The team byte finally has a consumer. A contact between two players is judged by the game
     * and the two teams before it is allowed to hurt, which is also what stops one co-op player
     * from killing the other. */
    mp_body_set_damage_gate(&mp_bridge_may_damage_peer);
}

/* The director's weapon commands and its blast, each by the module whose state or event carries
 * it; the question whose side this is belongs to the session and is asked here. */
static bool nodes_hand(void *actor, int32_t command, int32_t a1, int32_t a2)
{
    (void)command;
    (void)a1;
    (void)a2;
    return mp_enemy_nodes_director((uintptr_t)actor,
                                   mp_session_now_client_of_a_started_session(NULL, NULL));
}

static bool blast_hand(void *actor, int32_t command, int32_t a1, int32_t a2)
{
    (void)command;
    (void)a1;
    (void)a2;
    return mp_enemy_blast_director((uintptr_t)actor,
                                   mp_session_now_client_of_a_started_session(NULL, NULL));
}

void mp_arm_hulls_world(const multiplayer_config_t *config, bool as_client)
{
    (void)mp_node_map_install();
    (void)mp_enemy_pose_install();
    if (!mp_enemy_bind_install()) {
        log_warning("the enemy pool did not bind, so nothing about enemies can travel on this "
                    "build even once the sender exists");
    }
    /* And the spawner, so a client gets bodies for the enemies the host has and it has not: the
     * activation scan measures the distance to the LOCAL player, so two machines standing apart
     * hold different actor sets and the host's state for the difference would land on nothing. */
    if (!mp_enemy_spawn_install()) {
        log_warning("the receiver spawn did not bind, so an enemy the host wakes and this "
                    "machine's own scan never does stays missing here");
    }
    /* Whom an NPC fights. Only a host resolves anything: a client's NPCs are parked replicas. */
    if (mp_target_install()) {
        mp_target_set_host(!as_client);
    }
    /* Where a sound is. Both sides install it: a client hears the host's puppet and a host hears
     * the client's, and neither of those two bodies is the one its machine listens from. */
    (void)mp_sound_install();
    (void)mp_script_sound_install();   /* and what an actor's script plays, on every machine */
    (void)mp_footstep_install();
    (void)mp_enemy_fx_install(&mp_session_now_client_of_a_started_session);
    (void)mp_enemy_limb_install();
    (void)mp_enemy_blast_install();
    mp_phases_set_bodies_are_ticked(config->bank_tick);
    /* And the removals, with their reason, and the host's level seeing every player. */
    if (mp_enemy_relay_install()) {
        mp_enemy_relay_set_host(!as_client);
    }
    /* And the one range test the engine keeps and wakes enemies by. Only the host widens it: a
     * client that woke its own would hold a second set beside the one the host sends it. */
    mp_range_gate_set_host(!as_client);
    (void)mp_range_gate_install();
    /* And what a script switches on the level, counted on both sides and changed on
     * neither: whether the client runs these arms itself decides what the repair is. */
    mp_level_switch_set_host(!as_client);
    (void)mp_level_switch_install();
    /* And the pickups: a client claims, the host grants, the effect lands on the claimant. */
    if (mp_pickup_relay_install()) {
        mp_pickup_relay_set_host(!as_client);
        mp_pickup_relay_set_perform(&mp_hit_relay_perform);
    }
    /* And the sounds and the shield of this player's body, told as moments of it. */
    (void)mp_player_sound_install(&mp_actions_note_moment);
    /* And the director's commands by class, handed in before the hull that asks them stands. */
    mp_level_state_arm(&mp_world_door_hand, &mp_session_now_client_of_a_started_session,
                       &mp_bank_active);
    (void)mp_world_door_hand(MP_DIRECTOR_NODES, &nodes_hand, "the node masks");
    (void)mp_world_door_hand(MP_DIRECTOR_BLAST, &blast_hand, "the blast at a node");
    /* And the world changes after the start: the host names each, the clients follow. */
    (void)mp_follow_install();
}
