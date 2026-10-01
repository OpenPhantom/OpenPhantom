/* mp_puppet_shot.c: a far player's shot, spawned from the puppet's weapon. See the header. */
#include "mp_puppet_shot.h"

#include "mp_hit_relay.h"
#include "mp_puppet_anim.h"
#include "mp_signatures.h"

#include "common/logging.h"
#include "common/memory.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The object's pose as the engine draws and collides it: the position, then pitch, yaw and roll
 * in degrees. The pose commit copies the record into these at the end of the puppet's plan. */
#define OBJECT_POSITION 0x18u
#define OBJECT_ROTATION 0x3Cu

/* The shooter class every player fire site passes; the body module's hull rewrites it for the
 * bank in the window. */
#define SHOOTER_CLASS_PLAYER 1

/* A shot record names the object it flies as here. */
#define SHOT_NODE_OBJECT 0xA0u

typedef void *(__cdecl *shot_spawn_fn_t)(int32_t kind, const void *muzzle, float pitch, float yaw,
                                         int32_t shooter_class);

typedef struct puppet_shot_state {
    shot_spawn_fn_t shot_spawn;
    uint64_t        kinds;   /* which kinds ever flew here, one bit each */
    uint32_t        shots;
    uint32_t        shots_unplaced;
    bool            unplaced_logged;
} puppet_shot_state_t;

static puppet_shot_state_t puppet_shot;

bool mp_puppet_shot_resolve(void)
{
    puppet_shot.shot_spawn = (shot_spawn_fn_t)mp_signatures_address(MP_SITE_SHOT_SPAWN);
    return puppet_shot.shot_spawn != NULL;
}

uint32_t mp_puppet_shot_spawned(void)
{
    return puppet_shot.shots;
}

uint32_t mp_puppet_shot_unplaced(void)
{
    return puppet_shot.shots_unplaced;
}

/* The shot leaves the puppet's weapon: the offset the sender measured in its own body's frame,
 * turned back out by the puppet's pose, and the yaw the sender measured against its heading,
 * added to the puppet's. The object's pose and not the record's, because the plan has just
 * committed the record into the object and the object is what is drawn and collided. A puppet
 * whose object does not read gets no bolt: a bolt at the sender's place would be the fault the
 * offset exists to remove, and a two machine run showed exactly that, bolts leaving the air in
 * front of the puppet, while the muzzle still travelled as a world point. */
uint64_t mp_puppet_shot_kinds(void)
{
    return puppet_shot.kinds;
}

void mp_puppet_shot_perform(size_t bank, uint32_t object, const mp_event_t *event)
{
    float    position[3];
    float    rotation[3];
    float    muzzle[3];
    float    yaw;
    void    *shot;
    uint32_t shot_object = 0;

    if (puppet_shot.shot_spawn == NULL) {
        return;
    }
    if (object == 0u ||
        !memory_try_read(object + OBJECT_POSITION, position, sizeof position) ||
        !memory_try_read(object + OBJECT_ROTATION, rotation, sizeof rotation)) {
        ++puppet_shot.shots_unplaced;
        if (!puppet_shot.unplaced_logged) {
            puppet_shot.unplaced_logged = true;
            log_warning("a far shot could not be placed because the puppet's object did not "
                        "read; it is not spawned, and later ones are counted");
        }
        return;
    }
    mp_puppet_anim_muzzle(position, rotation, event->origin, muzzle);
    yaw = mp_puppet_anim_wrap360(rotation[1] + event->yaw);
    shot = puppet_shot.shot_spawn((int32_t)event->shot_kind, muzzle, event->pitch, yaw,
                                  SHOOTER_CLASS_PLAYER);
    /* Named as the far player's, so its contacts are not reported back as this player's
     * hits, and named with his BANK, because some kinds carry no side of their own: the
     * thermal detonator clears it at birth on purpose, and without this memory the gate
     * that decides whether players may hurt each other never sees the contact at all. */
    if (shot != NULL && memory_read_u32((uintptr_t)shot + SHOT_NODE_OBJECT, &shot_object)) {
        mp_hit_relay_note_far_shot_of(shot_object, bank);
    }
    if (event->shot_kind < 64u) {
        puppet_shot.kinds |= (uint64_t)1u << event->shot_kind;
    }
    ++puppet_shot.shots;
}
