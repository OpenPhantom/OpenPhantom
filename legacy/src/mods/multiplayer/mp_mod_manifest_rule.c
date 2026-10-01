/* mp_mod_manifest_rule.c: the statement a join is judged by, its codec, the judgement, the list of
 * DLLs outside this release behind it, and the table of this release's mods. See the header.
 */
#include "mp_mod_manifest_rule.h"

#include "mp_wire.h"

#include "common/mod_identity.h"
#include "common/text.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The one required mod of this release. Every other mod of the release either changes nothing a
 * client runs for the host, or is a setting the host hands over for the session. */
static const mp_mod_required_t REQUIRED[] = {
    { MP_WIRE_MOD_MULTIPLAYER, "multiplayer.dll" },
};

#define REQUIRED_COUNT (sizeof REQUIRED / sizeof REQUIRED[0])

/* Every mod of the release, one decision each. A client parks its activation scan, runs no script
 * for a parked replica and replays the host's events rather than deciding them, so what is left for
 * a client's own mods to change is its picture, its sound, its own body and how it replays. */
static const mp_mod_known_t KNOWN[] = {
    { "camera_handback_fix", MP_MOD_CLASS_LOCAL, false,
      "gives the camera back after a dialogue, on this machine" },
    { "controller_input", MP_MOD_CLASS_LOCAL, false, "turns a pad into this machine's input" },
    { "crash_report", MP_MOD_CLASS_LOCAL, false, "writes this machine's crash reports" },
    { "crt_copy_fix", MP_MOD_CLASS_LOCAL, false, "copies faster with the same result" },
    { "decal_fix", MP_MOD_CLASS_LOCAL, false, "draws decals and footprints" },
    { "dev_overlay", MP_MOD_CLASS_LOCAL, true,
      "the panel: its world changes go through the session's own rules and notes" },
    { "diagnostics", MP_MOD_CLASS_LOCAL, false, "observes and writes the log" },
    { "dialogue_anim_fix", MP_MOD_CLASS_HOST_ONLY, false,
      "the speaker's animation and a dialogue's pace, which only the host's scripts run" },
    { "dialogue_menu_fix", MP_MOD_CLASS_HOST_ONLY, false,
      "keeps a branching dialogue open, and only the host has the menu" },
    { "dismemberment", MP_MOD_CLASS_HOST_SETTINGS, false,
      "the host decides which limb flies; its Mode is the host's in a session" },
    { "effect_clock", MP_MOD_CLASS_HOST_ONLY, false,
      "the effects' random stream, which counts only where the host's world runs" },
    { "enhanced_input", MP_MOD_CLASS_LOCAL, false, "this player's own body, whose state travels" },
    { "enhanced_resolution", MP_MOD_CLASS_LOCAL, false, "resolution, window and menu scale" },
    { "fmv_player", MP_MOD_CLASS_LOCAL, true,
      "plays the movies; without it the host's movie does not hold this machine's" },
    { "framerate_fix", MP_MOD_CLASS_LOCAL, false,
      "frame rate, camera and clocks of this machine; the substep ladder is held in a session" },
    { "ground_clip_fix", MP_MOD_CLASS_HOST_ONLY, false,
      "contacts and crushers, which only the host's world pushes" },
    { "hud_ratio_scaling", MP_MOD_CLASS_LOCAL, false, "the HUD's aspect" },
    { "imuse_fix", MP_MOD_CLASS_LOCAL, false, "the music" },
    { "large_textures", MP_MOD_CLASS_LOCAL, false, "texture limits" },
    { "multiplayer", MP_MOD_CLASS_REQUIRED, false,
      "reads every message, so both sides run the same build" },
    { "render_guard", MP_MOD_CLASS_LOCAL, false, "limits in the draw path" },
    { "sfx_volume_save_fix", MP_MOD_CLASS_LOCAL, false, "the effects volume" },
    { "sound_lifetime_fix", MP_MOD_CLASS_LOCAL, false, "keeps a level load from crashing" },
    { "variable_fov", MP_MOD_CLASS_LOCAL, false, "the field of view" },
    { "view_distance_fix", MP_MOD_CLASS_HOST_SETTINGS, false,
      "the draw distance and the fog band are the host's in a session" },
};

#define KNOWN_COUNT (sizeof KNOWN / sizeof KNOWN[0])

/* ======================================== The statement ======================================= */

size_t mp_mod_manifest_encode(const mp_mod_manifest_t *manifest, uint8_t *out, size_t capacity)
{
    mp_wire_writer_t w;
    uint8_t          i;

    if (manifest == NULL || out == NULL || manifest->count > MP_MOD_MANIFEST_MAX_MODS ||
        capacity < MP_MOD_MANIFEST_HEAD_BYTES +
                       (size_t)manifest->count * MP_MOD_MANIFEST_MOD_BYTES) {
        return 0u;
    }
    mp_wire_writer_init(&w, out, capacity);
    mp_wire_put_u32(&w, manifest->damage);
    mp_wire_put_u32(&w, manifest->roster);
    mp_wire_put_u8(&w, manifest->count);
    for (i = 0; i < manifest->count; ++i) {
        mp_wire_put_u8(&w, manifest->mods[i].id);
        mp_wire_put_u32(&w, manifest->mods[i].stamp);
        mp_wire_put_u32(&w, manifest->mods[i].image);
    }
    return w.overflowed ? 0u : w.at;
}

bool mp_mod_manifest_decode(const uint8_t *bytes, size_t length, mp_mod_manifest_t *out)
{
    mp_wire_reader_t r;
    uint8_t          i;

    if (bytes == NULL || out == NULL) {
        return false;
    }
    memset(out, 0, sizeof *out);
    mp_wire_reader_init(&r, bytes, length);
    mp_wire_get_u32(&r, &out->damage);
    mp_wire_get_u32(&r, &out->roster);
    mp_wire_get_u8(&r, &out->count);
    if (r.overran || out->count > MP_MOD_MANIFEST_MAX_MODS ||
        length < MP_MOD_MANIFEST_HEAD_BYTES + (size_t)out->count * MP_MOD_MANIFEST_MOD_BYTES) {
        memset(out, 0, sizeof *out);
        return false;
    }
    for (i = 0; i < out->count; ++i) {
        mp_wire_get_u8(&r, &out->mods[i].id);
        mp_wire_get_u32(&r, &out->mods[i].stamp);
        mp_wire_get_u32(&r, &out->mods[i].image);
    }
    if (r.overran) {
        memset(out, 0, sizeof *out);
        return false;
    }
    return true;
}

static uint32_t fold(uint32_t hash, uint32_t value)
{
    int byte;

    for (byte = 0; byte < 4; ++byte) {
        hash ^= (value >> (8 * byte)) & 0xFFu;
        hash *= 16777619u;
    }
    return hash;
}

/* ======================================== The judgement ======================================= */

const mp_mod_required_t *mp_mod_manifest_required(size_t *count)
{
    if (count != NULL) {
        *count = REQUIRED_COUNT;
    }
    return REQUIRED;
}

const char *mp_mod_manifest_required_name(const mp_mod_required_t *table, size_t count,
                                          uint8_t id)
{
    size_t i;

    for (i = 0; table != NULL && i < count; ++i) {
        if (table[i].id == id) {
            return table[i].dll;
        }
    }
    return NULL;
}

static const mp_mod_build_t *find_build(const mp_mod_manifest_t *manifest, uint8_t id)
{
    uint8_t i;

    for (i = 0; i < manifest->count && i < MP_MOD_MANIFEST_MAX_MODS; ++i) {
        if (manifest->mods[i].id == id) {
            return &manifest->mods[i];
        }
    }
    return NULL;
}

/* The judge's one test for a data file, so the fingerprint below and the judgement cannot come to
 * mean two things. A 0 is no exception: a side without the file differs from a side with it. */
static bool data_differs(uint32_t host, uint32_t joiner)
{
    return host != joiner;
}

/* A mod the list requires, as the fingerprint folds it: its number and its build, or its number
 * and a marker when this side does not have it, which no build's stamp and size can equal. */
#define ABSENT_BUILD 0xFFFFFFFFu

uint32_t mp_mod_manifest_fingerprint(const mp_mod_manifest_t *manifest)
{
    uint32_t hash = 2166136261u;
    bool     any = false;
    size_t   i;

    if (manifest == NULL) {
        return 0u;
    }
    /* "Nothing at all" is judged by what is folded, not by the count: a statement that names only
     * numbers this build does not know holds as little as one that names none. */
    for (i = 0; i < REQUIRED_COUNT; ++i) {
        any = any || find_build(manifest, REQUIRED[i].id) != NULL;
    }
    if (!any && manifest->damage == 0u && manifest->roster == 0u) {
        return 0u;
    }
    hash = fold(hash, manifest->damage);
    hash = fold(hash, manifest->roster);
    for (i = 0; i < REQUIRED_COUNT; ++i) {
        const mp_mod_build_t *build = find_build(manifest, REQUIRED[i].id);

        hash = fold(hash, REQUIRED[i].id);
        hash = fold(hash, build != NULL ? build->stamp : ABSENT_BUILD);
        hash = fold(hash, build != NULL ? build->image : ABSENT_BUILD);
    }
    return hash != 0u ? hash : 1u;
}

static void refuse(mp_mod_verdict_t *out, uint8_t reason, uint8_t sub, uint8_t mod,
                   uint32_t host_stamp, uint32_t host_image, uint32_t joiner_stamp,
                   uint32_t joiner_image)
{
    out->reason       = reason;
    out->sub          = sub;
    out->mod          = mod;
    out->host_stamp   = host_stamp;
    out->host_image   = host_image;
    out->joiner_stamp = joiner_stamp;
    out->joiner_image = joiner_image;
}

void mp_mod_manifest_judge(const mp_mod_manifest_t *host, const mp_mod_manifest_t *joiner,
                           const mp_mod_required_t *table, size_t table_count,
                           mp_mod_verdict_t *out)
{
    uint8_t i;

    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof *out);
    out->mod = MP_MOD_NO_MOD;
    if (host == NULL || joiner == NULL) {
        return;
    }
    if (data_differs(host->damage, joiner->damage)) {
        refuse(out, MP_MOD_REFUSE_GAME_DATA, MP_MOD_SUB_DAMAGE, MP_MOD_NO_MOD, host->damage, 0u,
               joiner->damage, 0u);
        return;
    }
    if (data_differs(host->roster, joiner->roster)) {
        refuse(out, MP_MOD_REFUSE_GAME_DATA, MP_MOD_SUB_ROSTER, MP_MOD_NO_MOD, host->roster, 0u,
               joiner->roster, 0u);
        return;
    }
    for (i = 0; i < host->count && i < MP_MOD_MANIFEST_MAX_MODS; ++i) {
        const mp_mod_build_t *mine = &host->mods[i];
        const mp_mod_build_t *theirs = find_build(joiner, mine->id);

        if (theirs == NULL) {
            refuse(out, MP_MOD_REFUSE_MODS, MP_MOD_SUB_MISSING_AT_JOINER, mine->id, mine->stamp,
                   mine->image, 0u, 0u);
            return;
        }
        if (theirs->stamp != mine->stamp || theirs->image != mine->image) {
            refuse(out, MP_MOD_REFUSE_MODS, MP_MOD_SUB_OTHER_BUILD, mine->id, mine->stamp,
                   mine->image, theirs->stamp, theirs->image);
            return;
        }
    }
    for (i = 0; i < joiner->count && i < MP_MOD_MANIFEST_MAX_MODS; ++i) {
        const mp_mod_build_t *theirs = &joiner->mods[i];

        if (find_build(host, theirs->id) == NULL &&
            mp_mod_manifest_required_name(table, table_count, theirs->id) != NULL) {
            refuse(out, MP_MOD_REFUSE_MODS, MP_MOD_SUB_MISSING_AT_HOST, theirs->id, 0u, 0u,
                   theirs->stamp, theirs->image);
            return;
        }
    }
}

/* ================================ The DLLs outside this release =============================== */

/* A stated name comes back whole in a refusal, and three of the longest fit behind the mods. */
_Static_assert(MP_MOD_FOREIGN_NAME_MAX <= MP_MOD_REFUSAL_NAME_MAX,
               "a stated name no longer fits a refusal's detail whole");
_Static_assert(MP_MOD_MANIFEST_HEAD_BYTES + REQUIRED_COUNT * MP_MOD_MANIFEST_MOD_BYTES +
                       MP_MOD_FOREIGN_HEAD_BYTES + 3u * MP_MOD_FOREIGN_NAME_MAX <=
                   MP_MOD_STATEMENT_MAX_BYTES,
               "three names of 31 characters no longer fit behind this release's required mods");

/* Printable ASCII only, because a stranger wrote the name and a log line and a screen print it. */
static bool printable(uint8_t byte)
{
    return byte >= 0x20u && byte < 0x7Fu;
}

/* Whether `name` can be stated, 1 to 31 printable characters, and its length. */
static bool nameable(const char *name, size_t *length)
{
    size_t i;

    for (i = 0; name != NULL && name[i] != '\0'; ++i) {
        if (i + 1u >= MP_MOD_FOREIGN_NAME_MAX || !printable((uint8_t)name[i])) {
            return false;
        }
    }
    *length = i;
    return i != 0u;
}

size_t mp_mod_foreign_encode(const char *const *names, size_t listed, size_t count, bool judged,
                             uint8_t *out, size_t room, bool *stated)
{
    size_t at = MP_MOD_FOREIGN_HEAD_BYTES;
    size_t named = 0u;
    size_t i;

    if (out == NULL || room < MP_MOD_FOREIGN_HEAD_BYTES || (names == NULL && listed != 0u)) {
        return 0u;
    }
    for (i = 0; i < listed; ++i) {
        size_t length = 0u;
        bool   whole = named < MP_MOD_FOREIGN_NAMES_MAX && nameable(names[i], &length) &&
                       length + 1u <= room - at;

        if (whole) {
            out[at] = (uint8_t)length;
            memcpy(out + at + 1u, names[i], length);
            at += 1u + length;
            ++named;
        }
        if (stated != NULL) {
            stated[i] = whole;
        }
    }
    /* Never fewer than the names it was given, so a list never states more than it counts. */
    count = count < listed ? listed : count;
    out[0] = (uint8_t)MP_MOD_FOREIGN_FORM;
    out[1] = judged ? 1u : 0u;
    out[2] = (uint8_t)(count > 0xFFu ? 0xFFu : count);
    out[3] = (uint8_t)named;
    return at;
}

/* One stated name at `*at`, into `name`. False for anything that breaks the form. */
static bool read_name(const uint8_t *bytes, size_t length, size_t *at, char *name)
{
    size_t name_length;
    size_t j;

    if (*at >= length) {
        return false;
    }
    name_length = bytes[(*at)++];
    if (name_length == 0u || name_length >= MP_MOD_FOREIGN_NAME_MAX ||
        name_length > length - *at) {
        return false;
    }
    for (j = 0; j < name_length; ++j) {
        if (!printable(bytes[*at + j])) {
            return false;
        }
        name[j] = (char)bytes[*at + j];
    }
    name[name_length] = '\0';
    *at += name_length;
    return true;
}

void mp_mod_foreign_decode(const uint8_t *bytes, size_t length, size_t base_end,
                           mp_mod_foreign_t *out)
{
    const uint8_t *head;
    size_t         at = base_end + MP_MOD_FOREIGN_HEAD_BYTES;
    uint8_t        i;

    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof *out);
    if (base_end == length) {
        return;   /* MP_MOD_FOREIGN_ABSENT */
    }
    out->form = MP_MOD_FOREIGN_MALFORMED;
    if (bytes == NULL || base_end > length || length - base_end < MP_MOD_FOREIGN_HEAD_BYTES) {
        return;
    }
    head = bytes + base_end;
    if (head[0] != MP_MOD_FOREIGN_FORM || head[1] > 1u || head[3] > head[2] ||
        head[3] > MP_MOD_FOREIGN_NAMES_MAX) {
        return;
    }
    for (i = 0; i < head[3]; ++i) {
        if (!read_name(bytes, length, &at, out->names[i])) {
            memset(out->names, 0, sizeof out->names);
            return;
        }
    }
    out->form   = MP_MOD_FOREIGN_SOUND;
    out->judged = head[1];
    out->count  = head[2];
    out->stated = head[3];
}

size_t mp_mod_foreign_first_refused(const char *const *names, size_t count,
                                    const char *allow_list)
{
    size_t i;

    for (i = 0; i < count; ++i) {
        if (names == NULL || !mod_identity_name_listed(allow_list, names[i])) {
            return i;
        }
    }
    return count;
}

void mp_mod_foreign_judge(const mp_mod_foreign_t *foreign, const char *allow_list,
                          mp_mod_verdict_t *out)
{
    const char *names[MP_MOD_FOREIGN_NAMES_MAX];
    size_t      stated;
    size_t      first;

    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof *out);
    out->mod = MP_MOD_NO_MOD;
    /* A build like this one always sends a list, so a missing or broken one is no permission;
     * taken as one, it would let every DLL in unjudged. */
    if (foreign == NULL || foreign->form != MP_MOD_FOREIGN_SOUND) {
        refuse(out, MP_MOD_REFUSE_FOREIGN, MP_MOD_SUB_NO_LIST, MP_MOD_NO_MOD, 0u, 0u, 0u, 0u);
        return;
    }
    if (foreign->judged == 0u) {
        return;
    }
    for (stated = 0; stated < foreign->stated && stated < MP_MOD_FOREIGN_NAMES_MAX; ++stated) {
        names[stated] = foreign->names[stated];
    }
    first = mp_mod_foreign_first_refused(names, stated, allow_list);
    if (first < stated) {
        refuse(out, MP_MOD_REFUSE_FOREIGN, MP_MOD_SUB_NOT_ALLOWED, (uint8_t)first, 0u, 0u, 0u, 0u);
    } else if (foreign->count > foreign->stated) {
        refuse(out, MP_MOD_REFUSE_FOREIGN, MP_MOD_SUB_NOT_NAMED, MP_MOD_NO_MOD, 0u, 0u, 0u, 0u);
    }
}

const char *mp_mod_foreign_list(const char *const *names, size_t count, char *out,
                                size_t capacity)
{
    /* Room kept back for the count of the rest, at the widest it prints. */
    static const size_t tail = sizeof ", and 4294967295 more";
    size_t              used = 0u;
    size_t              shown = 0u;

    if (out == NULL || capacity == 0u) {
        return "";
    }
    (void)text_format(out, capacity, "none");
    while (names != NULL && shown < count && shown < MP_MOD_FOREIGN_NAMES_MAX) {
        const char *name = names[shown] != NULL ? names[shown] : "";
        size_t      length = strlen(name);

        if (length == 0u || used + 2u + length + tail > capacity) {
            break;
        }
        used += text_format(out + used, capacity - used, "%s%s", shown != 0u ? ", " : "", name);
        ++shown;
    }
    if (count != 0u && shown == 0u) {
        (void)text_format(out, capacity, "%u name(s), none short enough to list", (unsigned)count);
    } else if (shown < count) {
        (void)text_format(out + used, capacity - used, ", and %u more", (unsigned)(count - shown));
    }
    return out;
}

/* ==================================== The refusal's detail ==================================== */

static void put_text(mp_wire_writer_t *w, const char *text, size_t field)
{
    size_t i;
    bool   ended = (text == NULL);

    for (i = 0; i < field; ++i) {
        uint8_t byte = 0u;

        if (!ended && i + 1u < field) {
            byte = (uint8_t)text[i];
            ended = (byte == 0u);
        }
        mp_wire_put_u8(w, ended ? 0u : byte);
    }
}

/* Printable ASCII only and always terminated, because a stranger wrote it and a screen draws it. */
static void get_text(mp_wire_reader_t *r, char *out, size_t field)
{
    size_t i;
    bool   ended = false;

    for (i = 0; i < field; ++i) {
        uint8_t byte = 0u;

        mp_wire_get_u8(r, &byte);
        if (byte == 0u) {
            ended = true;
        }
        out[i] = (char)(ended ? 0 : (byte >= 0x20u && byte < 0x7Fu) ? byte : '?');
    }
    out[field - 1u] = '\0';
}

size_t mp_mod_refusal_detail_encode(const mp_mod_verdict_t *verdict, const char *name,
                                    const char *version, uint8_t *out, size_t capacity)
{
    mp_wire_writer_t w;

    if (verdict == NULL || out == NULL) {
        return 0u;
    }
    mp_wire_writer_init(&w, out, capacity);
    mp_wire_put_u8(&w, verdict->sub);
    mp_wire_put_u8(&w, verdict->mod);
    mp_wire_put_u32(&w, verdict->host_stamp);
    mp_wire_put_u32(&w, verdict->host_image);
    put_text(&w, name, MP_MOD_REFUSAL_NAME_MAX);
    put_text(&w, version, MP_MOD_REFUSAL_VERSION_MAX);
    return w.overflowed ? 0u : w.at;
}

bool mp_mod_refusal_detail_decode(const uint8_t *bytes, size_t length, mp_mod_verdict_t *verdict,
                                  char name[MP_MOD_REFUSAL_NAME_MAX],
                                  char version[MP_MOD_REFUSAL_VERSION_MAX])
{
    mp_wire_reader_t r;
    uint8_t          sub = 0u;
    uint8_t          mod = MP_MOD_NO_MOD;
    uint32_t         stamp = 0u;
    uint32_t         image = 0u;

    if (bytes == NULL || verdict == NULL || name == NULL || version == NULL ||
        length < MP_MOD_REFUSAL_DETAIL_BYTES) {
        return false;
    }
    mp_wire_reader_init(&r, bytes, length);
    mp_wire_get_u8(&r, &sub);
    mp_wire_get_u8(&r, &mod);
    mp_wire_get_u32(&r, &stamp);
    mp_wire_get_u32(&r, &image);
    get_text(&r, name, MP_MOD_REFUSAL_NAME_MAX);
    get_text(&r, version, MP_MOD_REFUSAL_VERSION_MAX);
    if (r.overran) {
        return false;
    }
    verdict->sub        = sub;
    verdict->mod        = mod;
    verdict->host_stamp = stamp;
    verdict->host_image = image;
    return true;
}

/* ===================================== The release's mods ===================================== */

const mp_mod_known_t *mp_mod_manifest_known(size_t *count)
{
    if (count != NULL) {
        *count = KNOWN_COUNT;
    }
    return KNOWN;
}

static char ascii_lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

/* Whether the `length` characters at `run` are `name`'s first `name_length`, ignoring case. */
static bool same_run(const char *run, size_t length, const char *name, size_t name_length)
{
    size_t i;

    if (length != name_length) {
        return false;
    }
    for (i = 0; i < length; ++i) {
        if (ascii_lower(run[i]) != ascii_lower(name[i])) {
            return false;
        }
    }
    return true;
}

const mp_mod_known_t *mp_mod_manifest_find_known(const char *stem)
{
    size_t i;

    if (stem == NULL) {
        return NULL;
    }
    for (i = 0; i < KNOWN_COUNT; ++i) {
        if (same_run(stem, strlen(stem), KNOWN[i].name, strlen(KNOWN[i].name))) {
            return &KNOWN[i];
        }
    }
    return NULL;
}
