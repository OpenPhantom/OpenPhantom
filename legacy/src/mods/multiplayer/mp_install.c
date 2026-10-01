/* mp_install.c: the stages of the install that stand on the configuration alone. See the header.
 *
 * Split from multiplayer.c along the seam its size note named, when the one exit pushed that file
 * past the hard limit again. What moved does not register a callback of that file's except the
 * substep-task client, which is handed in.
 */
#include "mp_install.h"

#include "mp_bank.h"
#include "mp_blade_draw.h"
#include "mp_body.h"
#include "mp_body_wear.h"
#include "mp_damage.h"
#include "mp_input.h"
#include "mp_lifecycle.h"
#include "mp_phases.h"
#include "mp_puppet.h"
#include "mp_task.h"

#include "common/logging.h"

#include <stdbool.h>
#include <stddef.h>

/* A far body is about to be rebuilt out of another actor. A clip ordinal is per model, so the
 * puppet's clip, weapon and sabre state describes the skeleton that has just left and would
 * resolve against the new actor's table as something else. The wiring is here because the body
 * module knows nothing about a puppet. The overlay is told at once as well: after a level end or
 * a session end no tick runs to say the body went, and a model it put on must not outlive it. The
 * body's blade row goes here too, the one way out of a standing far body, so the two hulls it
 * stands behind find nothing to open once no far body stands. */
static void on_far_body_rebuilt(size_t bank)
{
    mp_puppet_reset(bank);
    mp_body_wear_note_down(bank);
    mp_blade_draw_forget(bank);
}

/* The bank and its lifecycle hulls back both tick provocations, and the spawn needs the spawn
 * hull's block loan so its absolute clear lands in the bank rather than wiping bank 0. Installed
 * once, behind the bootstrap; with every switch off nothing here holds an engine address. */
void mp_install_bank_and_body(const multiplayer_config_t *config, void (*tick)(void))
{
    if ((config->provoke_bank_swap || config->second_body) &&
        mp_bank_install()) {
        mp_lifecycle_install();
        mp_task_set_tick_client(tick);
        if (config->provoke_bank_swap) {
            log_info("the bank swap provocation is armed: one full swap cycle per substep, and the "
                     "digest of bank 0 must come back bit-identical every time");
        }
        if (config->second_body) {
            log_info("the second body is armed: a session spawns one for every far player, and "
                     "without a session only a body provocation spawns one");
        }
    }

    /* The contact instrument: the count dispatcher and the bank aware shot hull. The second body
     * needs both, so it installs whenever either switch is on. */
    if (config->body_contact || config->second_body) {
        mp_body_install();
        mp_body_set_rebuilt_listener(&on_far_body_rebuilt);
        log_info("a far body that is rebuilt for a new appearance resets the puppet, so no clip, "
                 "weapon or sabre state carries across to the skeleton that replaces it");
    }
}

/* The input split, after the body: its dispatch asks the bank which class is active, and with no
 * command set the second body reads stillness. The death hull after the split, because its
 * provocation rides the same tick client and needs the body and the bank standing first. */
void mp_install_input_and_death(const multiplayer_config_t *config)
{
    if (config->bank_input && mp_input_install() &&
        config->synthetic_spin) {
        mp_input_command_t spin = { 1.0f, 0.0f, 0.0f, 0u };

        mp_input_set_command(&spin);
        log_info("the synthetic spin is armed: bank 1 holds a full right turn, so the second body "
                 "should circle in place while the player stands still");
    }

    /* The death hull installs on every armed run, not only behind BankDeath: it is the one hull
     * that can stop a death from ending the level for everybody, and an ini that turns BankDeath
     * off must not take it away. It costs a run with no session nothing, because its route stays
     * RETAIL until somebody turns the survival on and the pass through is byte for byte the
     * engine's own death. */
    if (mp_damage_install() && config->bank_death) {
        if (config->provoke_second_death) {
            mp_damage_enable_provocation();
            log_info("the death provocation is armed: the second body will be killed on purpose "
                     "after %u substep(s) and revived %u substep(s) later, with the level outcome "
                     "watched in between", (unsigned)MP_DAMAGE_PROVOKE_KILL_AT,
                     (unsigned)MP_DAMAGE_PROVOKE_REVIVE_AFTER);
        }
        /* Bank health, only behind the hull: routed contacts can enter the death path, and the
         * re-enabled death check enters it whenever the banked health runs out. */
        if (config->bank_health) {
            mp_body_enable_contact_routing();
            if (mp_phases_enable_death_check()) {
                log_info("bank health is armed: the second body's contacts are delivered into its "
                         "own bank, and the death check is back in its plan, reading its own "
                         "health");
            } else {
                log_warning("bank health is armed without the death check: the phase loop is "
                            "absent, so the second body takes damage but cannot die from it");
            }
        }
    }
}
