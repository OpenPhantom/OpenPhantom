/* mp_lobby_sites.h: the byte patterns the lobby needs from the game's own level flow.
 *
 * They are their own file for the reason mp_menu_sites.c gives: patterns are data, they are long,
 * and the file that holds them says nothing else. What is here is the campaign round (which level
 * the next one loads), the level loader's own branch (the five cells it names), the hero swap and
 * the two ways a savegame is restored.
 */
#ifndef MULTIPLAYER_MP_LOBBY_SITES_H
#define MULTIPLAYER_MP_LOBBY_SITES_H

#include <stdint.h>

extern const uint8_t SIG_MP_CAMPAIGN_ROUND[50];
extern const uint8_t MSK_MP_CAMPAIGN_ROUND[50];

extern const uint8_t SIG_MP_CAMPAIGN_LOAD[56];
extern const uint8_t MSK_MP_CAMPAIGN_LOAD[56];

extern const uint8_t SIG_MP_LEVEL_HANDOVER[35];
extern const uint8_t MSK_MP_LEVEL_HANDOVER[35];

extern const uint8_t SIG_MP_PATH_PREFIX[11];
extern const uint8_t MSK_MP_PATH_PREFIX[11];

extern const uint8_t SIG_MP_HERO_SWAP[24];
extern const uint8_t MSK_MP_HERO_SWAP[24];

extern const uint8_t SIG_MP_SAVE_LOAD_NAMED[24];
extern const uint8_t MSK_MP_SAVE_LOAD_NAMED[24];

extern const uint8_t SIG_MP_LOAD_GAME_SCREEN[32];
extern const uint8_t MSK_MP_LOAD_GAME_SCREEN[32];
/* `push ebp; mov ebp,esp; sub esp,0xDC`: one, two and six bytes, the first instruction boundary
 * past the five a jump needs, and no relative operand inside them. */
#define LOAD_GAME_SCREEN_PROLOGUE 9u

#endif /* MULTIPLAYER_MP_LOBBY_SITES_H */
