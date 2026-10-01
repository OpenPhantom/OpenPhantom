/* limb_mode_pick.h: which dismemberment mode the poll runs, the host's or this machine's own.
 *
 * In a multiplayer session the host decides the mode for every machine: a client throws the pieces
 * the host's blows sever, and they fly the way this mode says. The host's value reaches this DLL
 * through common/host_settings_note and is held in memory only, so engine_fixes.ini keeps this
 * machine's own mode for the whole session. The choice is pure so a test can hold it, and it sits
 * apart from dismemberment.c because that file hulls the engine.
 */
#ifndef LIMB_MODE_PICK_H
#define LIMB_MODE_PICK_H

#include <stdbool.h>

typedef struct limb_mode_choice {
    int  mode;        /* the mode to run */
    int  own;         /* this machine's own, after this read of the ini */
    bool from_host;
} limb_mode_choice_t;

/* Whether `mode` is one of the three this DLL knows. */
bool limb_mode_is_known(int mode);

/* `ini_mode` is what the ini holds now; a mode this DLL does not know leaves `own_before`
 * standing, as the poll always has. The host's value is taken while the host names one and it is a
 * whole number this DLL knows as a mode. */
limb_mode_choice_t limb_mode_pick(bool host_named, float host_value, int ini_mode,
                                  int own_before);

#endif /* LIMB_MODE_PICK_H */
