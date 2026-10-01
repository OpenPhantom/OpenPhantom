/* mp_reports.c: everything this feature prints when a run ends. */
#include "mp_reports.h"

#include "multiplayer.h"
#include "mp_arena.h"
#include "mp_armed.h"
#include "mp_arrival.h"
#include "mp_cutscene.h"
#include "mp_session_over.h"
#include "mp_bank.h"
#include "mp_body.h"
#include "mp_body_death.h"
#include "mp_body_wear.h"
#include "mp_bootstrap.h"
#include "mp_bridge.h"
#include "mp_bridge_drain.h"
#include "mp_bridge_lobby.h"
#include "mp_bridge_savefile.h"
#include "mp_config.h"
#include "mp_chat.h"
#include "mp_chat_draw.h"
#include "mp_chat_input.h"
#include "mp_crate.h"
#include "mp_lobby.h"
#include "mp_damage.h"
#include "mp_input.h"
#include "mp_install.h"
#include "mp_lifecycle.h"
#include "mp_hero_carry.h"
#include "mp_capacity.h"
#include "mp_cells.h"
#include "mp_enemy.h"
#include "mp_enemy_bind.h"
#include "mp_enemy_blast.h"
#include "mp_enemy_block.h"
#include "mp_enemy_burst.h"
#include "mp_enemy_fx.h"
#include "mp_enemy_limb.h"
#include "mp_enemy_shield.h"
#include "mp_enemy_pose.h"
#include "mp_node_map.h"
#include "mp_enemy_relay.h"
#include "mp_follow.h"
#include "mp_enemy_spawn.h"
#include "mp_enemy_sync.h"
#include "mp_hit_relay.h"
#include "mp_host_settings.h"
#include "mp_hud.h"
#include "mp_bridge_public.h"
#include "mp_menu.h"
#include "mp_module.h"
#include "mp_pickup_relay.h"
#include "mp_dialog_relay.h"
#include "mp_quest_relay.h"
#include "mp_npc_copies_bridge.h"
#include "mp_npc_shot_relay.h"
#include "mp_pause.h"
#include "mp_pool.h"
#include "mp_pumps.h"
#include "mp_reentry.h"
#include "mp_respawn.h"
#include "mp_round.h"
#include "mp_scratch_bind.h"
#include "mp_settings.h"
#include "mp_scratch_wire.h"
#include "mp_scene_host.h"
#include "mp_scene_watch.h"
#include "mp_script_sound.h"
#include "mp_signatures.h"
#include "mp_signatures_dialog.h"
#include "mp_spawnpoints.h"
#include "mp_flash.h"
#include "mp_footstep.h"
#include "mp_phases.h"
#include "mp_puppet_anim.h"
#include "mp_player_sound_play.h"
#include "mp_sound.h"
#include "mp_target.h"
#include "mp_trust.h"
#include "mp_use_latch.h"
#include "mp_watchpost.h"
#include "mp_start.h"
#include "mp_world.h"
#include "mp_world_holds.h"
#include "mp_world_values.h"
#include "mp_world_event.h"
#include "common/frame_hook.h"
#include "common/host_image.h"
#include "common/ini.h"
#include "common/logging.h"
#include "common/memory.h"

void mp_reports_run(const char *why)
{
    mp_bank_report(why);
    mp_lifecycle_report(why);
    mp_hero_carry_report();
    mp_body_report(why);
    mp_input_report(why);
    mp_pause_report();
    mp_damage_report(why);
    mp_bridge_report(why);
    mp_round_report();
    mp_hud_report();
    mp_reentry_report();
    mp_respawn_report();
    mp_arrival_report();
    mp_spawnpoints_report();
    mp_start_report();
    mp_bridge_savefile_report();
    mp_arena_report();
    mp_cutscene_report();
    mp_session_over_report();
    mp_follow_report();
    mp_world_values_report();
    mp_host_settings_report();
    mp_world_holds_report();
    mp_enemy_fx_report();
    mp_enemy_limb_report();
    mp_enemy_block_report();
    mp_world_event_report();
    mp_enemy_burst_report();
    mp_enemy_shield_report();
    mp_enemy_blast_report();
    mp_target_report();
    mp_scene_watch_report();
    mp_scene_report();
    mp_crate_report();
    mp_chat_report();
    mp_chat_input_report();
    mp_chat_draw_report();
    mp_sound_report();
    mp_script_sound_report();
    mp_player_sound_report();
    mp_footstep_report();
    mp_puppet_anim_seek_report();
    mp_flash_report();
    mp_watchpost_report();
}
