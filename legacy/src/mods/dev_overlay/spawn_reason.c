/* spawn_reason.c: see spawn_reason.h. */
#include "spawn_reason.h"

#include <stddef.h>
#include <string.h>

void spawn_reason_group_facts(spawn_reason_facts_t *out, bool session_holds, bool session,
                              bool level, uint32_t kinds)
{
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof *out);
    out->session_holds = session_holds;
    out->session       = session;
    out->level         = level;
    out->kinds         = kinds;
}

const char *spawn_reason_why(const spawn_reason_facts_t *facts, spawn_reason_need_t need)
{
    if (facts == NULL) {
        return "nothing is known about the spawner";
    }
    /* First, because it is the one nothing on this machine can answer: in a session the copies
     * belong to the multiplayer, and one that does not run them hands out no keys for them. The
     * fact behind it is the group's lock, so that the rows and the mode cannot disagree. */
    if (facts->session_holds) {
        return "the session runs no copies";
    }
    if (!facts->level) {
        return "no level, or no player in it";
    }
    if (facts->kinds == 0u) {
        return "this level offers nothing to spawn";
    }
    if (need == SPAWN_REASON_GROUP) {
        return NULL;
    }
    if (!facts->chosen) {
        return "nothing is chosen to place";
    }
    if (!facts->camera) {
        return "the camera cells did not resolve";
    }
    /* In a session the host raises the copy, so what this machine could build on its own says
     * nothing about whether the wish can be made. */
    if (!facts->session && !facts->builder) {
        return "the spawner cannot raise a copy here";
    }
    return NULL;
}
