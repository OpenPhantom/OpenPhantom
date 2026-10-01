/* mp_signatures_pause.h: the pause family's sites, and the cells and calls their bytes name. */
#ifndef MP_SIGNATURES_PAUSE_H
#define MP_SIGNATURES_PAUSE_H

#include <stddef.h>
#include <stdint.h>

typedef enum mp_pause_site {
    MP_PAUSE_SITE_SYS_PAUSE,        /* 0x0043FAB5  the whole of the engine's pause */
    MP_PAUSE_SITE_KEY_ARM,          /* 0x0043F667  the escape arm of the key hook, and its call */
    MP_PAUSE_SITE_PUMP_CHOICE,      /* 0x004430D5  the pause menu's choice of a frame pump */
    MP_PAUSE_SITE_COUNT
} mp_pause_site_t;

typedef enum mp_pause_cell {
    MP_PAUSE_CELL_LATCH,            /* 0x006CCFE0  set while the pause screen is up */
    MP_PAUSE_CELL_SIM_GATE,         /* 0x006CCFD8  set while the substeps are held */
    MP_PAUSE_CELL_OUTCOME,          /* 0x00881368  the level outcome the answer "leave" sets */
    MP_PAUSE_CELL_RESTORE,          /* 0x00881340  the restore flag the same answer sets */
    MP_PAUSE_CELL_BACKDROP,         /* 0x006CFDC4  nought: a menu frame runs the world's frame */
    MP_PAUSE_CELL_COUNT
} mp_pause_cell_t;

/* The calls whose E8 these sites carry. The address answered is the E8 itself. */
typedef enum mp_pause_call {
    MP_PAUSE_CALL_PAUSE,            /* 0x0043F681  the key hook's one call of sys_pause */
    MP_PAUSE_CALL_MENU,             /* 0x0043FAE9  sys_pause's one call of pausemenu_run */
    MP_PAUSE_CALL_WORLD_PUMP,       /* 0x004430DE  the pause menu's call of sys_frame */
    MP_PAUSE_CALL_MENU_PUMP,        /* 0x004430E5  and of swmenu_pumpFrame */
    MP_PAUSE_CALL_COUNT
} mp_pause_call_t;

/* Resolves the engine's pause once and answers the same thing afterwards. Safe to call from
 * anywhere that wants one of its cells, which is what keeps this table out of every caller's
 * installation order. The key arm and the pump choice are left to the next call. */
size_t mp_signatures_pause_resolve(void);

/* Resolves the key arm and the pump choice once, the two sites a session's pause menu is built on,
 * with a line of their own. Answers how many of the two resolved. */
size_t mp_signatures_pause_menu_resolve(void);

/* 0 when the site did not resolve. */
uintptr_t mp_signatures_pause_address(mp_pause_site_t site);

/* 0 when the site did not resolve, or when a cell named twice was named two different ways. */
uintptr_t mp_signatures_pause_cell(mp_pause_cell_t cell);

/* The address of the E8, or 0 when its site did not resolve or the byte there is not an E8. */
uintptr_t mp_signatures_pause_call(mp_pause_call_t call);

#endif /* MP_SIGNATURES_PAUSE_H */
