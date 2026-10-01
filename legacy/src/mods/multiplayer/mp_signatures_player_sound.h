/* mp_signatures_player_sound.h: the places a player's own body is made heard, and the shield it
 * wears, as byte patterns.
 *
 * Layer 2. Eight sites, every one inside a function of the player module and every one ended on a
 * call. Four of those calls are repointed while a session is armed, so that the moment the engine
 * plays the sound is the moment this machine tells the others: the burning ground, the key, the
 * plunge into water and the end of the shield's time. The other four are only read, for what the
 * far side needs to play the same thing at a puppet: the pickup's name, the shield pickup's own
 * shield, and the two voices of a death with the tables they are read from, the burst of a burning
 * death and the entry both voices are started through.
 *
 * A table of its own because the main table's file is at its size limit, the way the contacts and
 * the push blocks have theirs.
 *
 * Every call's target is read once, when the table is resolved and before anything is repointed,
 * so what is handed out below is always the function the engine's own call reached.
 */
#ifndef MULTIPLAYER_MP_SIGNATURES_PLAYER_SOUND_H
#define MULTIPLAYER_MP_SIGNATURES_PLAYER_SOUND_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum mp_player_sound_site {
    MP_PLAYER_SOUND_SITE_GROUND,       /* 0x004492D2  the burning ground, call 0x004492EA */
    MP_PLAYER_SOUND_SITE_KEY,          /* 0x0044CA5F  a key used on a lock, call 0x0044CA70 */
    MP_PLAYER_SOUND_SITE_WATER,        /* 0x0044D260  into the water, call 0x0044D26E */
    MP_PLAYER_SOUND_SITE_SHIELD_DOWN,  /* 0x00448E62  the shield's time is up, call 0x00448E82 */
    MP_PLAYER_SOUND_SITE_PICKUP,       /* 0x00448973  a pickup taken; read only */
    MP_PLAYER_SOUND_SITE_SHIELD_UP,    /* 0x00448936  the shield pickup's shield; read only */
    MP_PLAYER_SOUND_SITE_DEATH,        /* 0x00450153  the death cry; read only */
    MP_PLAYER_SOUND_SITE_BURN,         /* 0x0045027E  the burst and the burning cry; read only */
    MP_PLAYER_SOUND_SITE_COUNT
} mp_player_sound_site_t;

/* Everything the sites say, read once. An address is 0 and a value 0 where its site did not
 * resolve. */
typedef struct mp_player_sound_engine {
    size_t    resolved;          /* how many of the eight sites resolved */

    /* The four repointed calls, the E8 of each, and what they reached when they were found. */
    uintptr_t ground_call;
    uintptr_t key_call;
    uintptr_t water_call;
    uintptr_t shield_down_call;
    uintptr_t play_name;         /* bapsound_playName: the ground's, the key's and the water's
                                  * call, which have to agree, and the pickup's */
    uintptr_t shield_release;    /* fxshield_release */

    /* The sound names the four read, as the absolute operand each reads its name through. */
    uint32_t  ground_name;
    uint32_t  key_name;
    uint32_t  water_name;
    uint32_t  pickup_name;

    /* The far side's half. */
    uint32_t  names;             /* g_soundName, named by both cries, which have to agree */
    uint32_t  death_voices;      /* g_heroDeathVoice */
    uint32_t  burn_voices;       /* g_heroBurnVoice */
    uintptr_t play_by_name;      /* bapsound_playByName, reached by both cries alike */
    uintptr_t burst;             /* shot_shatterThing, as the burning death calls it */
    float     burst_speed;       /* the speed the burning death bursts the body at */
    uintptr_t shield_create;     /* fxshield_create, as the shield pickup calls it */
    float     shield_timer;      /* the seconds the shield pickup sets the timer to */
    bool      agree;             /* every pair above that has to agree does */
} mp_player_sound_engine_t;

/* Resolves the eight sites once, with a line for each and one for the whole, reads everything
 * above and answers it; afterwards it answers the same reading. Called from the installation, so
 * the targets are read before any call is repointed. */
const mp_player_sound_engine_t *mp_signatures_player_sound_engine(void);

#endif /* MULTIPLAYER_MP_SIGNATURES_PLAYER_SOUND_H */
