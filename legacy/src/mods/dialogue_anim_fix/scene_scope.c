/* scene_scope.c: see scene_scope.h. */
#include "scene_scope.h"

#include "common/memory.h"

#include <string.h>

/* The three links from an actor record to its model's name. The body and thing offsets are the
 * ones dismemberment.c and the diagnostics census read; model3's own first bytes ARE a short
 * name string, the same technique retail's own giant-model special case in rdThing_Draw uses. */
#define ACTOR_PLACEMENT_OFFSET 0x04u  /* char[12], the placement label the spawn path copies in */
#define ACTOR_PLACEMENT_SIZE     12u
#define ACTOR_OWN_BODY_OFFSET  0x34u  /* actor record -> its own body pointer */
#define BODY_THING_OFFSET      0x9Cu  /* body -> rdThing* */
#define THING_MODEL3_OFFSET    0x04u  /* rdThing -> model3* */

static const scene_scope_t SCOPES[] = {
    { "espa.b3d",  NULL,       0, false, true,  { "obinpc", "pquigon", NULL } },
    { "queen.b3d", "enemy031", 0, true,  false, { "nabcit2", NULL, NULL } }
};
#define SCOPE_COUNT (sizeof SCOPES / sizeof SCOPES[0])

const scene_scope_t *scene_scope_for_level(const char *path)
{
    size_t i;

    if (path == NULL) {
        return NULL;
    }
    for (i = 0; i < SCOPE_COUNT; ++i) {
        if (strstr(path, SCOPES[i].level_file) != NULL) {
            return &SCOPES[i];
        }
    }
    return NULL;
}

size_t scene_scope_count(void)
{
    return SCOPE_COUNT;
}

const scene_scope_t *scene_scope_at(size_t index)
{
    return (index < SCOPE_COUNT) ? &SCOPES[index] : NULL;
}

/* The reads are the faulting kind, not the asking kind: this runs inside the engine's own
 * dialogue opcodes, on pointers it handed over a moment ago. */
static bool actor_name_starts_with(int32_t actor_record, const char *prefix)
{
    void  *body = NULL;
    void  *thing = NULL;
    void  *model3 = NULL;
    char   name[9] = {0};
    size_t prefix_len = strlen(prefix);

    if (!memory_try_read((uintptr_t)actor_record + ACTOR_OWN_BODY_OFFSET, &body, sizeof(body)) ||
        body == NULL) {
        return false;
    }
    if (!memory_try_read((uintptr_t)body + BODY_THING_OFFSET, &thing, sizeof(thing)) ||
        thing == NULL) {
        return false;
    }
    if (!memory_try_read((uintptr_t)thing + THING_MODEL3_OFFSET, &model3, sizeof(model3)) ||
        model3 == NULL) {
        return false;
    }
    if (prefix_len >= sizeof(name) ||
        !memory_try_read((uintptr_t)model3, name, prefix_len)) {
        return false;
    }
    return memcmp(name, prefix, prefix_len) == 0;
}

/* The placement label is not unique across the game, so a row never relies on it alone: it
 * narrows a model match to one placement within the one level the row names. */
static bool actor_placement_is(int32_t actor_record, const char *placement)
{
    char name[ACTOR_PLACEMENT_SIZE + 1] = {0};

    return memory_try_read((uintptr_t)actor_record + ACTOR_PLACEMENT_OFFSET, name,
                           ACTOR_PLACEMENT_SIZE) &&
           strcmp(name, placement) == 0;
}

bool scene_scope_actor_matches(const scene_scope_t *scope, int32_t actor_record)
{
    size_t i;

    if (scope == NULL || actor_record == 0) {
        return false;
    }
    if (scope->placement != NULL && !actor_placement_is(actor_record, scope->placement)) {
        return false;
    }
    for (i = 0; scope->prefixes[i] != NULL; ++i) {
        if (actor_name_starts_with(actor_record, scope->prefixes[i])) {
            return true;
        }
    }
    return false;
}
