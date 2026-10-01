/* mp_mod_allow.h: [multiplayer] AllowMods, and whether this machine may host.
 *
 * Layer 3. A session is played with this release's own DLLs. A DLL out of the mods folder that is
 * not of this release comes into one only by the host's [multiplayer] AllowMods: a machine with one
 * that the list does not name cannot host, and a host refuses a player who has one. Both ask the
 * same rule, mp_mod_foreign_first_refused: this module over the host's whole list from the census,
 * the judge over the names a request states. The list is the host's alone; a player's own list does
 * not count at somebody else's host, so nobody can write an exception for himself.
 *
 * Empty in the shipped ini and in the code. The names are separated by commas, with or without
 * ".dll", in any case, the rule of mod_identity_name_listed; a file name with a comma in it can
 * therefore never be named.
 */
#ifndef MULTIPLAYER_MP_MOD_ALLOW_H
#define MULTIPLAYER_MP_MOD_ALLOW_H

#include <stdbool.h>
#include <stddef.h>

/* Reads [multiplayer] AllowMods now and keeps it for the judge, and says what it names when it is
 * read for the first time or has changed, each name with whether a DLL of that name is loaded
 * here. Answers the list as kept. Asked at every hosting and at every handing of a statement to a
 * host's transport, which covers a host armed from NetRole as well. */
const char *mp_mod_allow_read(void);

/* The list as it was last read, "" before it ever was. The judge holds a request against this
 * rather than reading the ini on every request, since it runs on each repeat of a request and
 * before the cookie has proven its sender. */
const char *mp_mod_allow_list(void);

/* Whether this machine may host: every DLL it has loaded from the mods folder is this release's
 * own or named in [multiplayer] AllowMods, which is read now. A build that judges nothing may, and
 * one with more such DLLs than the census holds may not, since the rest cannot be held against the
 * list. Writes the line that says which, and keeps the answer for mp_mod_allow_hosting_blocked. */
bool mp_mod_allow_may_host(void);

/* For the lobby's band after its arming was refused: true when the last mp_mod_allow_may_host
 * refused, with the file name of the DLL that blocked it in `name`, cut to `capacity` and
 * terminated: the first one the list does not name, or past the census's room the first it could
 * not hold. False, with `name` empty, when hosting was allowed or never asked. */
bool mp_mod_allow_hosting_blocked(char *name, size_t capacity);

/* The report's line: the DLLs outside this release, the names of the list the last hosting was
 * judged by, each list as its first names and how many more, and what became of hosting from the
 * menu. */
void mp_mod_allow_report(void);

#endif /* MULTIPLAYER_MP_MOD_ALLOW_H */
