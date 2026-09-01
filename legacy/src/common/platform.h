/* platform.h: which implementation of Windows this is running on.
 *
 * The project targets Windows and everything here is a Windows binary, but a growing number of
 * people play this under Wine, on a Steam Deck through Proton or on a desktop Linux through Lutris.
 * Wine is a faithful enough Windows for almost all of this code, and where it is not, the
 * difference has been a real defect rather than a cosmetic one. This exists so a fix for one of
 * those can be applied where it is needed and NOWHERE ELSE: the Windows path stays exactly as it
 * was tested, and Wine gets the different answer it needs.
 */
#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdbool.h>

/* True when this process is running under Wine, which includes Proton and Lutris.
 *
 * Asks ntdll for `wine_get_version`, which is Wine's own documented way of being recognised and is
 * the check Wine itself suggests. It is a fact about the loaded ntdll rather than a guess from the
 * environment: real Windows does not export it, and unlike the HKCU\Software\Wine registry key it
 * cannot be left behind by something else or hidden by a prefix that trims the registry.
 *
 * The answer is settled once and remembered, because it cannot change while the process lives. */
bool platform_is_wine(void);

#endif /* PLATFORM_H */
