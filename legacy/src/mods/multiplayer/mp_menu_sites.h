/* mp_menu_sites.h: the frontend patterns, declared for the one table that carries them.
 *
 * The engine's menu toolkit is a set of ordinary functions this feature calls, and hulls one of,
 * the navigation code. What these patterns buy is an address to call through or to hull and, in
 * one case, four data addresses that fall out of a single site.
 */
#ifndef MULTIPLAYER_MP_MENU_SITES_H
#define MULTIPLAYER_MP_MENU_SITES_H

#include <stdint.h>

extern const uint8_t SIG_MP_SWMENU_BUILD[24];
extern const uint8_t MSK_MP_SWMENU_BUILD[24];
extern const uint8_t SIG_MP_SWMENU_OPEN[23];
extern const uint8_t MSK_MP_SWMENU_OPEN[23];
extern const uint8_t SIG_MP_SWMENU_CLOSE[32];
extern const uint8_t MSK_MP_SWMENU_CLOSE[32];
extern const uint8_t SIG_MP_SWMENU_PUMP_FRAME[21];
extern const uint8_t MSK_MP_SWMENU_PUMP_FRAME[21];
extern const uint8_t SIG_MP_SWMENU_TAKE_NAV[22];
extern const uint8_t MSK_MP_SWMENU_TAKE_NAV[22];
extern const uint8_t SIG_MP_SWMENU_FOCUS_ID[20];
extern const uint8_t MSK_MP_SWMENU_FOCUS_ID[20];
extern const uint8_t SIG_MP_SWMENU_WIDGET_STATE[32];
extern const uint8_t MSK_MP_SWMENU_WIDGET_STATE[32];
extern const uint8_t SIG_MP_SWMENU_SET_EDIT_TEXT[20];
extern const uint8_t MSK_MP_SWMENU_SET_EDIT_TEXT[20];
extern const uint8_t SIG_MP_SWMENU_GET_EDIT_TEXT[21];
extern const uint8_t MSK_MP_SWMENU_GET_EDIT_TEXT[21];
extern const uint8_t SIG_MP_TITLE_MAIN_MENU[73];
extern const uint8_t MSK_MP_TITLE_MAIN_MENU[73];
extern const uint8_t SIG_MP_SWMENU_LAST_FOCUS[20];
extern const uint8_t MSK_MP_SWMENU_LAST_FOCUS[20];
extern const uint8_t SIG_MP_SWWIDGET_FOCUS_BY_ID[24];
extern const uint8_t MSK_MP_SWWIDGET_FOCUS_BY_ID[24];

#endif /* MULTIPLAYER_MP_MENU_SITES_H */
