/* stand_in.c: see stand_in.h. */
#include "stand_in.h"
#include "stand_in_watch.h"

#include "common/logging.h"
#include "common/memory.h"
#include "common/signature.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* --- DLG_Find 0x00431D1A, called, never detoured --------------------------------------------- *
 * The binary search over the level's dialogue table, id to record. Its two loads of the level
 * pointer are wildcarded and read back: both must name the same cell, inside the image.
 *
 *   55 8B EC 83 EC 14            push ebp / mov ebp,esp / sub esp,0x14
 *   A1 <g_level>                 mov eax,[g_level]                the operand at +0x07
 *   8B 88 D4 0C 00 00            mov ecx,[eax+0xCD4]              the record count
 *   83 E9 01 89 4D EC            hi = count - 1
 *   C7 45 F8 00 00 00 00         lo = 0
 *   8B 15 <g_level>              mov edx,[g_level]                the operand at +0x20
 *   8B 82 D8 0C 00 00 89 45 F0   mov eax,[edx+0xCD8]              the record base */
static const uint8_t SIG_DLG_FIND[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x14,
    0xA1, 0x00, 0x00, 0x00, 0x00,
    0x8B, 0x88, 0xD4, 0x0C, 0x00, 0x00,
    0x83, 0xE9, 0x01, 0x89, 0x4D, 0xEC,
    0xC7, 0x45, 0xF8, 0x00, 0x00, 0x00, 0x00,
    0x8B, 0x15, 0x00, 0x00, 0x00, 0x00,
    0x8B, 0x82, 0xD8, 0x0C, 0x00, 0x00, 0x89, 0x45, 0xF0
};
static const uint8_t MSK_DLG_FIND[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
_Static_assert(sizeof SIG_DLG_FIND == sizeof MSK_DLG_FIND,
               "the DLG_Find pattern and its mask are different lengths");
#define DLG_FIND_LEVEL_OPERAND_A  0x07u
#define DLG_FIND_LEVEL_OPERAND_B  0x20u
#define DLG_RECORD_KEY_OFFSET     0x04u    /* char[8], "QGm3218": speaker, class, id */
#define DLG_KEY_SIZE              8u

/* --- the character pool, out of the same site the diagnostics census reads it from ----------- *
 * enemy_tickAll's head at 0x00431FF3 tests the pool pointer and then loads it; the address is
 * the operand at +0x08. The pattern runs on to the second load because the first twenty bytes
 * alone match a second function against another global. */
static const uint8_t SIG_CHARACTER_POOL[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x08, 0x83, 0x3D, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x75, 0x0A, 0xB8, 0x01, 0x00, 0x00, 0x00, 0xE9, 0x00, 0x00, 0x00,
    0x00, 0xA1, 0x00, 0x00, 0x00, 0x00
};
static const uint8_t MSK_CHARACTER_POOL[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00,
    0x00, 0xFF, 0x00, 0x00, 0x00, 0x00
};
_Static_assert(sizeof SIG_CHARACTER_POOL == sizeof MSK_CHARACTER_POOL,
               "the character pool pattern and its mask are different lengths");
#define CHARACTER_POOL_OPERAND        0x08u

/* --- bapobj_playClip 0x0041263F, called, never detoured ------------------------------------- *
 * The same head speaker_gesture.c resolves; its mode 4 is the crossfade the script
 * interpreter itself puts a clip on with. */
static const uint8_t SIG_PLAY_CLIP[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x10,
    0x8B, 0x45, 0x08, 0x89, 0x45, 0xF0,
    0xC7, 0x45, 0xF8, 0x00, 0x00, 0x00, 0x00,
    0x8B, 0x4D, 0xF0, 0x83, 0x79, 0x14, 0x00, 0x75, 0x1B
};

/* The pool: a six dword header and then the slots, each a link word and a character record.
 * The link word reads -1 in a free slot. The same layout diagnostics/character_scan.h has. */
#define POOL_ELEMENT_SIZE_OFFSET      0x0Cu
#define POOL_CAPACITY_OFFSET          0x10u
#define POOL_SLOTS_OFFSET             0x18u
#define POOL_LINK_SIZE                0x04u
#define POOL_FREE_LINK                0xFFFFFFFFu
#define POOL_ELEMENT_SIZE_MIN         0x80u
#define POOL_ELEMENT_SIZE_MAX         0x1000u
#define POOL_CAPACITY_MAX             4096u

/* The character record, the offsets the rest of this DLL reads. The anchor bit is the one the
 * spawner sets for the inviso.baf template and the tick tests to skip the body. */
#define CHARACTER_FLAGS_OFFSET        0x14u
#define CHARACTER_STATE_OFFSET        0x20u
#define CHARACTER_TEMPLATE_OFFSET     0x30u
#define CHARACTER_BODY_OFFSET         0x34u
#define CHARACTER_HEALTH_OFFSET       0x38u
#define CHARACTER_POSITION_OFFSET     0xD0u
#define CHARACTER_FLAG_ANCHOR         0x10000000u
#define CHARACTER_STATE_ACTIVE        1
#define TEMPLATE_NAME_OFFSET          0x08u    /* the .baf file name, "quiweap.baf" */
#define TEMPLATE_NAME_SIZE            0x18u

/* How far from the anchor its face may stand. The census: the first level's double is 8 units
 * from his anchor, the podrace's stand-ins 2 to 6 from theirs; the player's own companion in
 * the first level is 25 from it and is not the body meant. */
#define FACE_RADIUS                   20.0f

/* The speaker codes with a face, from the census of every anchor's lines: the first two
 * characters of the line's key, and the start of the model file the character is on. Qui-Gon
 * is three models, quigon, quigung and quiweap. The announcers, the consoles and the crowd
 * have no row and are left alone. */
static const struct {
    char        code[3];
    const char *model;
} FACES[] = {
    { "QG", "qui" },
    { "JJ", "jarjar" },
    { "SH", "shmi" },
    { "PA", "padme" },
    { "WA", "watto" },
    { "3P", "c3po" }
};
#define FACE_COUNT (sizeof FACES / sizeof FACES[0])

enum {
    SITE_DLG_FIND,
    SITE_CHARACTER_POOL,
    SITE_PLAY_CLIP,
    SITE_COUNT
};

static signature_t sites[SITE_COUNT] = {
    SIGNATURE_ENTRY_MASKED("DLG_Find", SIG_DLG_FIND, MSK_DLG_FIND),
    SIGNATURE_ENTRY_MASKED("character_pool", SIG_CHARACTER_POOL, MSK_CHARACTER_POOL),
    SIGNATURE_ENTRY("bapobj_playClip", SIG_PLAY_CLIP)
};

typedef uint32_t (__cdecl *dlg_find_fn_t)(int32_t line);

static struct {
    dlg_find_fn_t             dlg_find;
    const volatile uint32_t  *pool_cell;
} stand_in;

/* The model row for the line, or NULL when its key has no face. `key` receives the key. */
static const char *face_of_line(int32_t line, char key[DLG_KEY_SIZE + 1])
{
    uint32_t record;
    size_t   i;

    memset(key, 0, DLG_KEY_SIZE + 1);
    record = stand_in.dlg_find(line);
    if (record == 0 ||
        !memory_try_read((uintptr_t)record + DLG_RECORD_KEY_OFFSET, key, DLG_KEY_SIZE)) {
        return NULL;
    }
    key[DLG_KEY_SIZE] = '\0';
    for (i = 0; i < FACE_COUNT; ++i) {
        if (key[0] == FACES[i].code[0] && key[1] == FACES[i].code[1]) {
            return FACES[i].model;
        }
    }
    return NULL;
}

/* Whether the record at `character` is a live actor on the model `face`, with its body on a
 * clip its script is playing one pass at a time, or on the stand the watch parked it on.
 * `distance` receives how far it stands from `from`. */
static bool candidate(uintptr_t character, const char *face, const float from[3],
                      float *distance, uintptr_t *record_out, uint32_t *body_out)
{
    int32_t   state = 0;
    int32_t   health = 0;
    uint32_t  template_record = 0;
    uint32_t  body = 0;
    uint32_t  mode = 0;
    uintptr_t track;
    float     position[3];
    char      name[TEMPLATE_NAME_SIZE + 1] = {0};
    float     dx, dy, dz;

    if (!memory_try_read(character + CHARACTER_STATE_OFFSET, &state, sizeof state) ||
        state != CHARACTER_STATE_ACTIVE ||
        !memory_try_read(character + CHARACTER_HEALTH_OFFSET, &health, sizeof health) ||
        health <= 0 ||
        !memory_try_read(character + CHARACTER_TEMPLATE_OFFSET, &template_record,
                         sizeof template_record) ||
        template_record == 0 ||
        !memory_try_read((uintptr_t)template_record + TEMPLATE_NAME_OFFSET, name,
                         TEMPLATE_NAME_SIZE) ||
        _strnicmp(name, face, strlen(face)) != 0 ||
        !memory_try_read(character + CHARACTER_BODY_OFFSET, &body, sizeof body) || body == 0 ||
        !memory_try_read(character + CHARACTER_POSITION_OFFSET, position, sizeof position)) {
        return false;
    }
    dx = position[0] - from[0];
    dy = position[1] - from[1];
    dz = position[2] - from[2];
    *distance = (float)sqrt((double)(dx * dx + dy * dy + dz * dz));
    if (*distance > FACE_RADIUS) {
        return false;
    }
    track = stand_in_base_track(body);
    if (track == 0 || !memory_try_read(track + TRACK_MODE_OFFSET, &mode, sizeof mode) ||
        ((mode & TRACK_MODE_PLAY_ONCE) == 0 && !stand_in_watch_is_parked(body))) {
        return false;
    }
    *record_out = character;
    *body_out = body;
    return true;
}

/* The nearest candidate to `from` in the pool, 0 when there is none. `record` receives its
 * character record. */
static uint32_t nearest_face(const char *face, const float from[3], float *distance,
                            uintptr_t *record)
{
    uint32_t pool = 0;
    uint32_t element_size = 0;
    uint32_t capacity = 0;
    uint32_t stride;
    uint32_t index;
    uint32_t best = 0;

    *record = 0;

    *distance = 0.0f;
    if (!memory_try_read((uintptr_t)stand_in.pool_cell, &pool, sizeof pool) || pool == 0 ||
        !memory_try_read((uintptr_t)pool + POOL_ELEMENT_SIZE_OFFSET, &element_size,
                         sizeof element_size) ||
        !memory_try_read((uintptr_t)pool + POOL_CAPACITY_OFFSET, &capacity, sizeof capacity) ||
        element_size < POOL_ELEMENT_SIZE_MIN || element_size > POOL_ELEMENT_SIZE_MAX ||
        capacity == 0 || capacity > POOL_CAPACITY_MAX) {
        return 0;
    }
    stride = POOL_LINK_SIZE + element_size;
    for (index = 0; index < capacity; ++index) {
        uintptr_t slot = (uintptr_t)pool + POOL_SLOTS_OFFSET + index * stride;
        uint32_t  link = 0;
        uint32_t  body = 0;
        uintptr_t here_record = 0;
        float     here = 0.0f;

        if (!memory_try_read(slot, &link, sizeof link)) {
            break;
        }
        if (link == POOL_FREE_LINK) {
            continue;
        }
        if (candidate(slot + POOL_LINK_SIZE, face, from, &here, &here_record, &body) &&
            (best == 0 || here < *distance)) {
            best = body;
            *record = here_record;
            *distance = here;
        }
    }
    return best;
}

void stand_in_note_line(int32_t actor_record, int32_t line)
{
    uint32_t    flags = 0;
    float       from[3];
    float       distance = 0.0f;
    char        key[DLG_KEY_SIZE + 1];
    const char *face;
    uint32_t    body;
    uint32_t    anchor_body = 0;
    uintptr_t   record = 0;

    if (stand_in.dlg_find == NULL || actor_record == 0 ||
        !memory_try_read((uintptr_t)actor_record + CHARACTER_FLAGS_OFFSET, &flags,
                         sizeof flags) ||
        (flags & CHARACTER_FLAG_ANCHOR) == 0 ||
        !memory_try_read((uintptr_t)actor_record + CHARACTER_POSITION_OFFSET, from,
                         sizeof from) ||
        !memory_try_read((uintptr_t)actor_record + CHARACTER_BODY_OFFSET, &anchor_body,
                         sizeof anchor_body)) {
        return;
    }
    face = face_of_line(line, key);
    if (face == NULL) {
        log_info("line %d (%s) is an anchor's, and its speaker has no face in the table: left "
                 "as it is", (int)line, key[0] != '\0' ? key : "no key");
        return;
    }
    body = nearest_face(face, from, &distance, &record);
    if (body == 0) {
        log_info("line %s is an anchor's, and no live %s* body within %.0f units of it is on a "
                 "clip in the script's play-once mode: left as it is", key, face,
                 (double)FACE_RADIUS);
        return;
    }
    stand_in_watch_line(record, body, anchor_body, key, distance);
}

void stand_in_level_changed(void)
{
    stand_in_watch_forget();
}

bool stand_in_install(const volatile uint32_t *speaker_lock)
{
    uint32_t level_a = 0;
    uint32_t level_b = 0;
    uint32_t pool = 0;

    signature_resolve_table(sites, SITE_COUNT);
    if (sites[SITE_DLG_FIND].address == 0 || sites[SITE_CHARACTER_POOL].address == 0 ||
        sites[SITE_PLAY_CLIP].address == 0) {
        log_warning("DLG_Find, the character pool site or bapobj_playClip did not resolve, so "
                    "an anchor's line leaves its stand-in as the scripts shipped");
        return false;
    }
    if (!memory_read_u32(sites[SITE_DLG_FIND].address + DLG_FIND_LEVEL_OPERAND_A, &level_a) ||
        !memory_read_u32(sites[SITE_DLG_FIND].address + DLG_FIND_LEVEL_OPERAND_B, &level_b) ||
        level_a != level_b || !memory_is_inside_image(level_a, sizeof(uint32_t))) {
        log_warning("DLG_Find's two loads of the level pointer read as %08X and %08X, not one "
                    "cell inside the image, so an anchor's line leaves its stand-in as the "
                    "scripts shipped", (unsigned)level_a, (unsigned)level_b);
        return false;
    }
    if (!memory_read_u32(sites[SITE_CHARACTER_POOL].address + CHARACTER_POOL_OPERAND, &pool) ||
        !memory_is_inside_image(pool, sizeof(uint32_t))) {
        log_warning("the character pool operand read as %08X, outside the image, so an "
                    "anchor's line leaves its stand-in as the scripts shipped", (unsigned)pool);
        return false;
    }
    if (!stand_in_watch_install((stand_in_play_clip_fn_t)sites[SITE_PLAY_CLIP].address,
                                speaker_lock)) {
        log_warning("the per-frame hook could not be installed, so an anchor's line leaves "
                    "its stand-in as the scripts shipped");
        return false;
    }
    stand_in.pool_cell = (const volatile uint32_t *)(uintptr_t)pool;
    stand_in.dlg_find  = (dlg_find_fn_t)sites[SITE_DLG_FIND].address;
    log_info("a line spoken by a script anchor with no body brings in the body it stands for: "
             "the nearest live actor on the speaker's model within %.0f units whose script is "
             "playing a clip one pass at a time has that pass started again, or its stand "
             "ended, as the voice starts, unless its script answers the line itself within "
             "two ticks; a repeat of that clip with nothing said is parked on the stand "
             "(DLG_Find %08X, level cell %08X, pool cell %08X, "
             "bapobj_playClip %08X)", (double)FACE_RADIUS,
             (unsigned)sites[SITE_DLG_FIND].address, (unsigned)level_a, (unsigned)pool,
             (unsigned)sites[SITE_PLAY_CLIP].address);
    return true;
}
