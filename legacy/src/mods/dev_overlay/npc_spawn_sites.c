/* npc_spawn_sites.c: see npc_spawn_sites.h. The VAs in the comments are retail WMAIN.EXE's. */
#include "npc_spawn_sites.h"

#include "player_slot.h"

#include "common/logging.h"
#include "common/memory.h"
#include "common/patch.h"
#include "common/signature.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define X 0x00   /* a wildcard byte in a pattern; its mask byte is 0x00 */

/* enemy_saveBlock 0x004320A2 through its first two walks of the pool: the pool cell twice. */
static const uint8_t SIG_ENEMY_SAVE_BLOCK[] = {
    0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x34, 0x01, 0x00, 0x00,   /* frame, sub esp,0x134           */
    0xC7, 0x85, 0xE0, 0xFE, 0xFF, 0xFF, 0xFC, 0x09, 0x00, 0x00,
    0xA1, X, X, X, X, 0x50, 0xE8, X, X, X, X, 0x83, 0xC4, 0x04,   /* list_rewind(pool)     */
    0xC7, 0x85, 0xDC, 0xFE, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0x8B, 0x0D, X, X, X, X, 0x51, 0xE8, X, X, X, X, 0x83, 0xC4, 0x04   /* list_next(pool)  */
};
static const uint8_t MSK_ENEMY_SAVE_BLOCK[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0, 0, 0, 0, 0xFF, 0xFF, 0, 0, 0, 0, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0, 0, 0, 0, 0xFF, 0xFF, 0, 0, 0, 0, 0xFF, 0xFF, 0xFF
};
_Static_assert(sizeof SIG_ENEMY_SAVE_BLOCK == 58u, "the length measured against the bytes");
_Static_assert(sizeof SIG_ENEMY_SAVE_BLOCK == sizeof MSK_ENEMY_SAVE_BLOCK, "mask length");
#define ENEMY_SAVE_POOL_A 0x14u
#define ENEMY_SAVE_POOL_B 0x2Du

/* The pool being created, 0x00431F45: 0x204 bytes an element and 0x80 of them are required
 * bytes, so a pool of another shape does not resolve. The multiplayer reads the same cell here. */
static const uint8_t SIG_ENEMY_POOL_NEW[] = {
    0x83, 0xC4, 0x04, 0xA3, X, X, X, X, 0x68, 0x80, 0x00, 0x00,
    0x00, 0x68, 0x04, 0x02, 0x00, 0x00, 0xE8, X, X, X, X, 0x83,
    0xC4, 0x08, 0xA3, X, X, X, X, 0xC7, 0x05, X, X, X,
    X, X, X, X, X
};
static const uint8_t MSK_ENEMY_POOL_NEW[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 0, 0, 0xFF,
    0xFF, 0xFF, 0xFF, 0, 0, 0, 0, 0xFF, 0xFF, 0, 0, 0,
    0, 0, 0, 0, 0
};
_Static_assert(sizeof SIG_ENEMY_POOL_NEW == sizeof MSK_ENEMY_POOL_NEW, "mask length");
#define POOL_NEW_CELL 0x1Bu

/* save_saveGame 0x00451370 through the engine's own refusal for want of disk space, which is the
 * call to the message box this file uses as well. */
static const uint8_t SIG_SAVE_GAME[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x18, 0x56, 0x57,
    0x68, 0xA0, 0x86, 0x01, 0x00, 0xE8, X, X, X, X, 0x83, 0xC4, 0x04,   /* disk space       */
    0x85, 0xC0, 0x75, 0x22,
    0x68, 0x00, 0x0F, 0x00, 0x00, 0x68, X, X, X, X, 0x6A, 0x00, 0x6A, 0xFF, 0x6A, 0x11,
    0xE8, X, X, X, X, 0x83, 0xC4, 0x14,                                   /* the message box  */
    0xB8, 0x01, 0x00, 0x00, 0x00, 0xE9
};
static const uint8_t MSK_SAVE_GAME[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 0, 0, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0, 0, 0, 0, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
_Static_assert(sizeof SIG_SAVE_GAME == 55u, "the length measured against the bytes");
_Static_assert(sizeof SIG_SAVE_GAME == sizeof MSK_SAVE_GAME, "mask length");
#define SAVE_GAME_BOX_CALL 0x29u

/* swmenu_messageBox 0x0045F24F, the second witness for the call above. */
static const uint8_t SIG_MESSAGE_BOX[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x20, 0xC7, 0x45, 0xEC, 0xFF, 0xFF, 0xFF, 0xFF,
    0x6A, 0x00, 0x68, X, X, X, X, 0x68, X, X, X, X, 0x6A, 0x00, 0x68, X, X, X, X,
    0x68, X, X, X, X, 0xE8
};
static const uint8_t MSK_MESSAGE_BOX[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0, 0, 0, 0, 0xFF, 0, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0, 0, 0, 0,
    0xFF, 0, 0, 0, 0, 0xFF
};
_Static_assert(sizeof SIG_MESSAGE_BOX == sizeof MSK_MESSAGE_BOX, "mask length");
#define MESSAGE_BOX_PROLOGUE 6u

/* swmenu_findWidget 0x0045ECB7, which answers 0 for an id the current menu does not hold; the
 * current menu's cell at +7. */
static const uint8_t SIG_FIND_WIDGET[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x08, 0xA1, X, X, X, X, 0x89, 0x45, 0xF8,
    0x83, 0x7D, 0x08, 0x00, 0x7C, 0x06, 0x83, 0x7D, 0xF8, 0x00, 0x75, 0x04, 0x33, 0xC0, 0xEB, 0x4D
};
static const uint8_t MSK_FIND_WIDGET[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 0, 0, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
_Static_assert(sizeof SIG_FIND_WIDGET == sizeof MSK_FIND_WIDGET, "mask length");
#define FIND_WIDGET_PROLOGUE 6u
#define FIND_WIDGET_MENU     7u

/* pause_saveGame building and opening the save screen, 0x00444F0F: the menu named twice. */
static const uint8_t SIG_SAVE_SCREEN[] = {
    0x6A, 0x00, 0x68, X, X, X, X, 0x68, X, X, X, X, 0x6A, 0x00, 0x68, X, X, X, X,
    0x68, X, X, X, X, 0xE8, X, X, X, X, 0x83, 0xC4, 0x18, 0x68, X, X, X, X,
    0xE8, X, X, X, X, 0x83, 0xC4, 0x04, 0x83, 0xF8, 0x01, 0x75, 0x08, 0x83, 0xC8, 0xFF,
    0xE9, X, X, X, X, 0xC7, 0x05, X, X, X, X, 0x06, 0x00, 0x00, 0x00
};
static const uint8_t MSK_SAVE_SCREEN[] = {
    0xFF, 0xFF, 0xFF, 0, 0, 0, 0, 0xFF, 0, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0, 0, 0, 0,
    0xFF, 0, 0, 0, 0, 0xFF, 0, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 0, 0,
    0xFF, 0, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0, 0, 0, 0, 0xFF, 0xFF, 0, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF
};
_Static_assert(sizeof SIG_SAVE_SCREEN == 68u, "the length measured against the bytes");
_Static_assert(sizeof SIG_SAVE_SCREEN == sizeof MSK_SAVE_SCREEN, "mask length");
#define SAVE_SCREEN_MENU_A 0x14u
#define SAVE_SCREEN_MENU_B 0x21u

/* Plr_EnterTripodGun from +0xB, 0x00450462: the body, the mode descriptor, the player twice. */
static const uint8_t SIG_TRIPOD_ENTER[] = {
    0x8B, 0x4D, 0x08, 0x89, 0x48, 0x10, 0x8B, 0x15, X, X, X, X,
    0xC7, 0x42, 0x60, X, X, X, X, 0xA1, X, X, X, X, 0xC7, 0x40, 0x2C, 0x00, 0x00, 0x80, 0x3F
};
static const uint8_t MSK_TRIPOD_ENTER[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 0, 0,
    0xFF, 0xFF, 0xFF, 0, 0, 0, 0, 0xFF, 0, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
_Static_assert(sizeof SIG_TRIPOD_ENTER == sizeof MSK_TRIPOD_ENTER, "mask length");
#define TRIPOD_ENTER_PLAYER_A 0x08u
#define TRIPOD_ENTER_MODE     0x0Fu
#define TRIPOD_ENTER_PLAYER_B 0x14u
#define TRIPOD_MODE_UPDATE    0x10u   /* the mode's fifth word: its update, Plr_UpdateTripodGun */

/* Plr_UpdateTripodGun from +0xB, 0x004505AA: the second witness for the descriptor. */
static const uint8_t SIG_TRIPOD_UPDATE[] = {
    0x83, 0x78, 0x10, 0x00, 0x75, 0x21, 0x8B, 0x0D, X, X, X, X,
    0x8B, 0x91, 0x8C, 0x03, 0x00, 0x00, 0x52, 0xE8, X, X, X, X, 0x83, 0xC4, 0x04
};
static const uint8_t MSK_TRIPOD_UPDATE[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 0, 0,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 0, 0, 0xFF, 0xFF, 0xFF
};
_Static_assert(sizeof SIG_TRIPOD_UPDATE == sizeof MSK_TRIPOD_UPDATE, "mask length");
#define TRIPOD_UPDATE_HEAD   0x0Bu
#define TRIPOD_UPDATE_PLAYER 0x08u

/* The mount writing the gun's placement index into the player, 0x00450527: the offsets the
 * riding test reads, body +0xA0, actor +0x18, player +0x38C. */
static const uint8_t SIG_TRIPOD_OFFSETS[] = {
    0x8B, 0x91, 0xA0, 0x00, 0x00, 0x00, 0xA1, X, X, X, X, 0x8B, 0x4A, 0x18,
    0x89, 0x88, 0x8C, 0x03, 0x00, 0x00
};
static const uint8_t MSK_TRIPOD_OFFSETS[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 0, 0, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
_Static_assert(sizeof SIG_TRIPOD_OFFSETS == sizeof MSK_TRIPOD_OFFSETS, "mask length");

/* sys_startup 0x0043E613, the pattern the multiplayer hulls it with. */
static const uint8_t SIG_SYS_STARTUP[] = {
    0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x80, 0x00, 0x00, 0x00, 0x68, 0x68, 0xE9,
    0x4A, 0x00, 0x68, 0x64, 0xE9, 0x4A, 0x00, 0xA1, 0xF4, 0xE2, 0x4A, 0x00,
    0x50, 0x68, 0x4C, 0xE9, 0x4A, 0x00, 0x68, 0x80, 0x15, 0x88, 0x00
};
static const uint8_t MSK_SYS_STARTUP[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00,
    0x00, 0x00, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00, 0x00
};
_Static_assert(sizeof SIG_SYS_STARTUP == sizeof MSK_SYS_STARTUP, "mask length");

/* module_install 0x0046ED64, called, and its linking block at +0x7C, which names the tail cell
 * four times. Both the multiplayer's. */
static const uint8_t SIG_MODULE_INSTALL[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x0C, 0xC7, 0x45, 0xFC, 0x00, 0x00, 0x00,
    0x00, 0xC7, 0x45, 0xF8, 0x00, 0x00, 0x00, 0x00, 0x6A
};
static const uint8_t MSK_MODULE_INSTALL[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
_Static_assert(sizeof SIG_MODULE_INSTALL == sizeof MSK_MODULE_INSTALL, "mask length");
#define MODULE_INSTALL_PROLOGUE 6u
#define MODULE_LIST_LINK_AT     0x7Cu

static const uint8_t SIG_MODULE_LIST_LINK[] = {
    0x8B, 0x45, 0xF8, 0x8B, 0x0D, X, X, X, X, 0x89, 0x48, 0x04,
    0x8B, 0x55, 0xF8, 0xC7, 0x02, 0x00, 0x00, 0x00, 0x00, 0x83, 0x3D, X,
    X, X, X, 0x00, 0x74, 0x0A, 0xA1, X, X, X, X, 0x8B,
    0x4D, 0xF8, 0x89, 0x08, 0x8B, 0x55, 0xF8, 0x89, 0x15, X, X, X,
    X, 0x83, 0x3D, X, X, X, X, 0x00, 0x75, 0x08, 0x8B, 0x45,
    0xF8, 0xA3, X, X, X, X
};
static const uint8_t MSK_MODULE_LIST_LINK[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00,
    0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00,
    0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00
};
_Static_assert(sizeof SIG_MODULE_LIST_LINK == 66u, "the multiplayer's length");
_Static_assert(sizeof SIG_MODULE_LIST_LINK == sizeof MSK_MODULE_LIST_LINK, "mask length");
static const size_t MODULE_TAIL_OPERANDS[] = { 0x05u, 0x17u, 0x1Fu, 0x2Du };

/* save_writeChunk 0x00451C6D: header, then the payload, 0 when both were written. */
static const uint8_t SIG_WRITE_CHUNK[] = {
    0x55, 0x8B, 0xEC, 0x66, 0x8B, 0x45, 0x14, 0x50, 0x8B, 0x4D, 0x10, 0x51, 0x8B, 0x55, 0x08, 0x52,
    0xE8, X, X, X, X, 0x83, 0xC4, 0x0C, 0x83, 0xF8, 0x01, 0x75, 0x07, 0xB8, 0x01, 0x00, 0x00,
    0x00, 0xEB, 0x1D, 0x8B, 0x45, 0x10, 0x50, 0x8B, 0x4D, 0x0C, 0x51, 0xE8
};
static const uint8_t MSK_WRITE_CHUNK[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
_Static_assert(sizeof SIG_WRITE_CHUNK == 45u, "the length measured against the bytes");
_Static_assert(sizeof SIG_WRITE_CHUNK == sizeof MSK_WRITE_CHUNK, "mask length");
#define WRITE_CHUNK_PROLOGUE 8u

/* enemy_restoreBlock reading a record, 0x0043264E: the call is save_read. */
static const uint8_t SIG_SAVE_READ_CALL[] = {
    0x68, 0xD0, 0x00, 0x00, 0x00, 0x8D, 0x8D, 0x28, 0xFF, 0xFF, 0xFF, 0x51, 0xE8, X, X, X, X
};
static const uint8_t MSK_SAVE_READ_CALL[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 0, 0
};
_Static_assert(sizeof SIG_SAVE_READ_CALL == sizeof MSK_SAVE_READ_CALL, "mask length");
#define SAVE_READ_CALL 0x0Cu

/* The fread under save_read, 0x00451FB0, which save_read calls at +0xB: the second witness for
 * save_read. Its cell sits in the head, which another module's detour would cover, so only the
 * call is compared. */
static const uint8_t SIG_FREAD_UNDER[] = {
    0x55, 0x8B, 0xEC, 0xA1, X, X, X, X, 0x50, 0x6A, 0x01, 0x8B, 0x4D, 0x0C, 0x51, 0x8B, 0x55,
    0x08, 0x52, 0xE8, X, X, X, X, 0x83, 0xC4, 0x10, 0x5D, 0xC3
};
static const uint8_t MSK_FREAD_UNDER[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
_Static_assert(sizeof SIG_FREAD_UNDER == 29u, "the length measured against the bytes");
_Static_assert(sizeof SIG_FREAD_UNDER == sizeof MSK_FREAD_UNDER, "mask length");
#define FREAD_UNDER_PROLOGUE 9u
#define SAVE_READ_CALLS_FREAD 0x0Bu

/* save_seekRestore 0x00451FCD: fseek on the file being read, its cell at +0xD. */
static const uint8_t SIG_SAVE_SEEK[] = {
    0x55, 0x8B, 0xEC, 0x8B, 0x45, 0x0C, 0x50, 0x8B, 0x4D, 0x08, 0x51, 0x8B, 0x15, X, X, X, X,
    0x52, 0xE8, X, X, X, X, 0x83, 0xC4, 0x0C, 0x5D, 0xC3
};
static const uint8_t MSK_SAVE_SEEK[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 0, 0,
    0xFF, 0xFF, 0, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
_Static_assert(sizeof SIG_SAVE_SEEK == sizeof MSK_SAVE_SEEK, "mask length");
#define SAVE_SEEK_PROLOGUE 7u
#define SAVE_SEEK_FILE     0x0Du

/* The loader stepping over an unknown block, 0x00451846, and seeking past a refused one,
 * 0x004517CB: the block's length and the file, each named in both. */
static const uint8_t SIG_LOAD_SKIP[] = {
    0x6A, 0x01, 0x8B, 0x0D, X, X, X, X, 0x51, 0x8B, 0x15, X, X, X, X, 0x52,
    0xE8, X, X, X, X, 0x83, 0xC4, 0x0C
};
static const uint8_t MSK_LOAD_SKIP[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0, 0, 0, 0, 0xFF,
    0xFF, 0, 0, 0, 0, 0xFF, 0xFF, 0xFF
};
_Static_assert(sizeof SIG_LOAD_SKIP == sizeof MSK_LOAD_SKIP, "mask length");
#define LOAD_SKIP_LENGTH 0x04u
#define LOAD_SKIP_FILE   0x0Bu

static const uint8_t SIG_LOAD_RESEEK[] = {
    0x6A, 0x00, 0xA1, X, X, X, X, 0x8B, 0x4D, 0xD4, 0x8D, 0x54, 0x01, 0x18, 0x52, 0xA1, X, X, X, X,
    0x50, 0xE8
};
static const uint8_t MSK_LOAD_RESEEK[] = {
    0xFF, 0xFF, 0xFF, 0, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 0, 0,
    0xFF, 0xFF
};
_Static_assert(sizeof SIG_LOAD_RESEEK == sizeof MSK_LOAD_RESEEK, "mask length");
#define LOAD_RESEEK_LENGTH 0x03u
#define LOAD_RESEEK_FILE   0x10u

static npc_spawn_sites_t sites;
static bool              resolved;

/* The cell an instruction's 32-bit operand names, or 0 when it names nothing in the image. */
static uintptr_t cell_at(uintptr_t site, size_t offset)
{
    uint32_t cell = 0;

    return (site != 0 && memory_read_image_cell(site + offset, sizeof(uint32_t), &cell))
               ? (uintptr_t)cell
               : 0u;
}

static uintptr_t unique(const uint8_t *bytes, const uint8_t *mask, size_t size)
{
    return signature_find_unique(bytes, mask, size);
}

static void resolve_enemy_block(void)
{
    uintptr_t head = signature_find_detour_target(SIG_ENEMY_SAVE_BLOCK, MSK_ENEMY_SAVE_BLOCK,
                                                  sizeof SIG_ENEMY_SAVE_BLOCK,
                                                  NPC_SPAWN_ENEMY_SAVE_BLOCK_PROLOGUE);
    uintptr_t made = unique(SIG_ENEMY_POOL_NEW, MSK_ENEMY_POOL_NEW, sizeof SIG_ENEMY_POOL_NEW);
    uintptr_t a    = cell_at(head, ENEMY_SAVE_POOL_A);
    uintptr_t b    = cell_at(head, ENEMY_SAVE_POOL_B);
    uintptr_t c    = cell_at(made, POOL_NEW_CELL);

    if (head == 0 || a == 0 || a != b || a != c) {
        log_warning("npc spawner: the enemy block did not resolve (enemy_saveBlock %08X, the pool "
                    "named %08X, %08X and at its creation %08X), so no copy can be kept out of "
                    "a savegame and none is raised", (unsigned)head, (unsigned)a, (unsigned)b,
                    (unsigned)c);
        return;
    }
    sites.enemy_save_block = head;
    sites.pool_cell        = a;
}

static void resolve_refusal(void)
{
    uintptr_t save  = signature_find_detour_target(SIG_SAVE_GAME, MSK_SAVE_GAME,
                                                   sizeof SIG_SAVE_GAME,
                                                   NPC_SPAWN_SAVE_GAME_PROLOGUE);
    uintptr_t box   = signature_find_detour_target(SIG_MESSAGE_BOX, MSK_MESSAGE_BOX,
                                                   sizeof SIG_MESSAGE_BOX, MESSAGE_BOX_PROLOGUE);
    uintptr_t named = 0;
    uintptr_t find  = signature_find_detour_target(SIG_FIND_WIDGET, MSK_FIND_WIDGET,
                                                   sizeof SIG_FIND_WIDGET, FIND_WIDGET_PROLOGUE);
    uintptr_t built = unique(SIG_SAVE_SCREEN, MSK_SAVE_SCREEN, sizeof SIG_SAVE_SCREEN);
    uintptr_t enter = unique(SIG_TRIPOD_ENTER, MSK_TRIPOD_ENTER, sizeof SIG_TRIPOD_ENTER);
    uintptr_t upd   = unique(SIG_TRIPOD_UPDATE, MSK_TRIPOD_UPDATE, sizeof SIG_TRIPOD_UPDATE);
    uintptr_t offs  = unique(SIG_TRIPOD_OFFSETS, MSK_TRIPOD_OFFSETS, sizeof SIG_TRIPOD_OFFSETS);
    uint32_t  mode  = 0;
    uint32_t  mode_update = 0;
    uintptr_t player = player_slot_address();

    if (save != 0) {
        (void)patch_read_call_target(save + SAVE_GAME_BOX_CALL, &named);
    }
    if (enter != 0 && memory_read_u32(enter + TRIPOD_ENTER_MODE, &mode) &&
        memory_is_inside_image((uintptr_t)mode, TRIPOD_MODE_UPDATE + 4u)) {
        (void)memory_read_u32((uintptr_t)mode + TRIPOD_MODE_UPDATE, &mode_update);
    }
    if (save == 0 || box == 0 || named != box || enter == 0 || upd == 0 || offs == 0 ||
        player == 0 || cell_at(enter, TRIPOD_ENTER_PLAYER_A) != player ||
        cell_at(enter, TRIPOD_ENTER_PLAYER_B) != player ||
        cell_at(upd, TRIPOD_UPDATE_PLAYER) != player ||
        (uintptr_t)mode_update != upd - TRIPOD_UPDATE_HEAD) {
        log_warning("npc spawner: the save refusal did not resolve (save_saveGame %08X, its "
                    "message box %08X against %08X, the gun's mode %08X whose update %08X is not "
                    "%08X, the player %08X), so a save cannot be refused on a spawned tripod "
                    "and none is raised", (unsigned)save, (unsigned)named, (unsigned)box,
                    (unsigned)mode, (unsigned)mode_update, (unsigned)(upd - TRIPOD_UPDATE_HEAD),
                    (unsigned)player);
        return;
    }
    sites.save_game   = save;
    sites.message_box = (npc_spawn_message_box_fn)box;
    sites.tripod_mode = (uintptr_t)mode;
    /* Hiding the "saved" line is a nicety: without it the refusal still stands. */
    if (find != 0 && built != 0 && cell_at(built, SAVE_SCREEN_MENU_A) != 0 &&
        cell_at(built, SAVE_SCREEN_MENU_A) == cell_at(built, SAVE_SCREEN_MENU_B) &&
        cell_at(find, FIND_WIDGET_MENU) != 0) {
        sites.find_widget       = (npc_spawn_find_widget_fn)find;
        sites.current_menu_cell = cell_at(find, FIND_WIDGET_MENU);
        sites.save_screen       = cell_at(built, SAVE_SCREEN_MENU_A);
    } else {
        log_info("npc spawner: the save screen did not resolve (find %08X, built %08X), so a "
                 "refused save may still read 'saved' on it", (unsigned)find, (unsigned)built);
    }
}

static void resolve_block(void)
{
    uintptr_t startup = signature_find_detour_target(SIG_SYS_STARTUP, MSK_SYS_STARTUP,
                                                     sizeof SIG_SYS_STARTUP,
                                                     NPC_SPAWN_SYS_STARTUP_PROLOGUE);
    uintptr_t install = signature_find_detour_target(SIG_MODULE_INSTALL, MSK_MODULE_INSTALL,
                                                     sizeof SIG_MODULE_INSTALL,
                                                     MODULE_INSTALL_PROLOGUE);
    uintptr_t link    = unique(SIG_MODULE_LIST_LINK, MSK_MODULE_LIST_LINK,
                               sizeof SIG_MODULE_LIST_LINK);
    uintptr_t write   = signature_find_detour_target(SIG_WRITE_CHUNK, MSK_WRITE_CHUNK,
                                                     sizeof SIG_WRITE_CHUNK, WRITE_CHUNK_PROLOGUE);
    uintptr_t call    = unique(SIG_SAVE_READ_CALL, MSK_SAVE_READ_CALL, sizeof SIG_SAVE_READ_CALL);
    uintptr_t read    = 0;
    uintptr_t fread_under = signature_find_detour_target(SIG_FREAD_UNDER, MSK_FREAD_UNDER,
                                                         sizeof SIG_FREAD_UNDER,
                                                         FREAD_UNDER_PROLOGUE);
    uintptr_t read_calls  = 0;
    uintptr_t seek    = signature_find_detour_target(SIG_SAVE_SEEK, MSK_SAVE_SEEK,
                                                     sizeof SIG_SAVE_SEEK, SAVE_SEEK_PROLOGUE);
    uintptr_t skip    = unique(SIG_LOAD_SKIP, MSK_LOAD_SKIP, sizeof SIG_LOAD_SKIP);
    uintptr_t reseek  = unique(SIG_LOAD_RESEEK, MSK_LOAD_RESEEK, sizeof SIG_LOAD_RESEEK);
    uintptr_t tail    = cell_at(link, MODULE_TAIL_OPERANDS[0]);
    uintptr_t file    = cell_at(skip, LOAD_SKIP_FILE);
    uintptr_t length  = cell_at(skip, LOAD_SKIP_LENGTH);
    bool      agree   = tail != 0 && file != 0 && length != 0;
    size_t    i;

    if (call != 0) {
        (void)patch_read_call_target(call + SAVE_READ_CALL, &read);
    }
    if (read != 0) {
        (void)patch_read_call_target(read + SAVE_READ_CALLS_FREAD, &read_calls);
    }
    for (i = 1; i < sizeof MODULE_TAIL_OPERANDS / sizeof MODULE_TAIL_OPERANDS[0]; ++i) {
        agree = agree && cell_at(link, MODULE_TAIL_OPERANDS[i]) == tail;
    }
    agree = agree && cell_at(reseek, LOAD_RESEEK_FILE) == file &&
            cell_at(reseek, LOAD_RESEEK_LENGTH) == length && cell_at(seek, SAVE_SEEK_FILE) == file;
    if (startup == 0 || install == 0 || link != install + MODULE_LIST_LINK_AT || write == 0 ||
        read == 0 || fread_under == 0 || read_calls != fread_under || seek == 0 || !agree) {
        log_warning("npc spawner: the savegame block did not resolve (sys_startup %08X, "
                    "module_install %08X linking at %08X, save_writeChunk %08X, save_read %08X "
                    "calling %08X where the fread under it is %08X, save_seek %08X, the file "
                    "%08X, the length %08X), so no copy is raised", (unsigned)startup,
                    (unsigned)install, (unsigned)link, (unsigned)write, (unsigned)read,
                    (unsigned)read_calls, (unsigned)fread_under, (unsigned)seek, (unsigned)file,
                    (unsigned)length);
        return;
    }
    sites.sys_startup      = startup;
    sites.module_install   = (npc_spawn_module_install_fn)install;
    sites.module_tail_cell = tail;
    sites.write_chunk      = (npc_spawn_write_chunk_fn)write;
    sites.save_read        = (npc_spawn_save_read_fn)read;
    sites.save_seek        = (npc_spawn_save_seek_fn)seek;
    sites.load_file_cell   = file;
    sites.tag_length_cell  = length;
}

void npc_spawn_sites_resolve(void)
{
    if (resolved) {
        return;
    }
    resolved = true;
    memset(&sites, 0, sizeof sites);
    resolve_enemy_block();
    resolve_refusal();
    resolve_block();
    if (npc_spawn_sites_enemy_block() && npc_spawn_sites_refusal() && npc_spawn_sites_block()) {
        log_info("npc spawner: the savegame sites resolved: enemy_saveBlock %08X over the pool at "
                 "%08X, save_saveGame %08X, the tripod's mode %08X, sys_startup %08X, "
                 "save_writeChunk %08X", (unsigned)sites.enemy_save_block,
                 (unsigned)sites.pool_cell, (unsigned)sites.save_game,
                 (unsigned)sites.tripod_mode, (unsigned)sites.sys_startup,
                 (unsigned)(uintptr_t)sites.write_chunk);
    }
}

const npc_spawn_sites_t *npc_spawn_sites(void)
{
    return &sites;
}

bool npc_spawn_sites_enemy_block(void)
{
    return sites.enemy_save_block != 0 && sites.pool_cell != 0;
}

bool npc_spawn_sites_refusal(void)
{
    return sites.save_game != 0 && sites.message_box != NULL && sites.tripod_mode != 0;
}

bool npc_spawn_sites_block(void)
{
    return sites.sys_startup != 0 && sites.module_install != NULL && sites.write_chunk != NULL &&
           sites.save_read != NULL && sites.save_seek != NULL && sites.load_file_cell != 0 &&
           sites.tag_length_cell != 0 && sites.module_tail_cell != 0;
}
