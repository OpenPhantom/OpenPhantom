/* actor_catalog.c: see actor_catalog.h.
 *
 * The archive is LABN: a sixteen byte head (the tag, a version, the entry count and the size of
 * the name table), then one sixteen byte record per entry (the name's offset into the name
 * table, the data's offset into the file, its size and a four byte type), then the name table.
 * That is the layout the game's own loader walks and the one every tool this project has used
 * on the archive reads.
 *
 * An actor file is 0x120 bytes of header and four blocks. The three header words read here are
 * proven by the loader: +0xC0 the geometry block's size, +0xC8 the clip count (the assert string
 * "track <= pBapObj->pActor->numTracks" is against this word once loaded), and +0xD4 the size
 * of the animation block that ends the file. The node table is the tail of the geometry block:
 * the geometry starts at size - animation - geometry, its node count is the dword at +0x54 of
 * that (0x00401949), and the nodes, 0xB4 bytes each with the name first, run up to where the
 * animation block begins. Each node's name is what bapobj_findNodeByNameId compares against the
 * engine's own table of names, "waist", "head", "chest", "lhand", "rhand", "weapon", "sabre",
 * "sabreblad01" and so on, with strcmp, so case matters to the engine and is kept here.
 */
#include "actor_catalog.h"

#include "common/host_image.h"
#include "common/logging.h"
#include "common/text.h"

#include <windows.h>

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define LAB_HEAD           16u
#define LAB_ENTRY          16u
#define BAF_HEADER         0x120u
#define BAF_GEO_SIZE       0xC0u
#define BAF_CLIP_COUNT     0xC8u
#define BAF_ANIM_SIZE      0xD4u
#define BAF_ANIM_DESC      0x40u    /* a clip's descriptor, ahead of its key block */
#define DESC_TOTAL_SIZE    0x00u    /* descriptor and key together */
#define DESC_TRACK_FLAGS   0x14u
#define DESC_USAGE         0x18u
#define GEO_NODE_COUNT     0x54u
#define NODE_RECORD        0xB4u
#define NODE_NAME_MAX      0x44u
#define NODES_MAX          400u     /* the tools' own plausibility bound; the heroes have 48 */

static struct {
    bool                  tried;
    uint32_t              count;
    actor_catalog_entry_t entry[ACTOR_CATALOG_MAX];
    uint32_t              data[ACTOR_CATALOG_MAX];   /* each entry's offset in the archive */
    uint32_t              size[ACTOR_CATALOG_MAX];
    char                  path[MAX_PATH];
} catalog;

static bool read_at(HANDLE file, uint32_t offset, void *out, uint32_t size)
{
    DWORD got = 0;

    if (SetFilePointer(file, (LONG)offset, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER) {
        return false;
    }
    return ReadFile(file, out, size, &got, NULL) && got == size;
}

static uint32_t u32_at(const uint8_t *bytes, uint32_t offset)
{
    uint32_t value;

    memcpy(&value, bytes + offset, sizeof value);
    return value;
}

/* One actor file's clip count and the three node facts, from its header and its node table. */
static bool read_actor(HANDLE file, uint32_t data, uint32_t size, uint8_t *nodes,
                       actor_catalog_entry_t *out)
{
    uint8_t  header[BAF_HEADER];
    uint32_t geo_size;
    uint32_t anim_size;
    uint32_t geo;
    uint32_t anim_start;
    uint32_t node_count;
    uint32_t i;

    if (size < BAF_HEADER || !read_at(file, data, header, sizeof header)) {
        return false;
    }
    geo_size  = u32_at(header, BAF_GEO_SIZE);
    anim_size = u32_at(header, BAF_ANIM_SIZE);
    out->clips = u32_at(header, BAF_CLIP_COUNT);
    if (anim_size > size || geo_size > size - anim_size) {
        return false;
    }
    anim_start = size - anim_size;
    geo        = anim_start - geo_size;
    if (geo < BAF_HEADER || geo + GEO_NODE_COUNT + 4u > size ||
        !read_at(file, data + geo + GEO_NODE_COUNT, &node_count, sizeof node_count) ||
        node_count == 0 || node_count > NODES_MAX || node_count * NODE_RECORD > anim_start - geo ||
        !read_at(file, data + anim_start - node_count * NODE_RECORD, nodes,
                 node_count * NODE_RECORD)) {
        return false;
    }
    for (i = 0; i < node_count; ++i) {
        const char *name = (const char *)(nodes + i * NODE_RECORD);
        uint32_t    n;

        for (n = 0; n < NODE_NAME_MAX && name[n] != '\0'; ++n) {
        }
        if (n == NODE_NAME_MAX) {
            continue;   /* not a name */
        }
        if (strcmp(name, "head") == 0 || strcmp(name, "chest") == 0) {
            out->has_body = true;
        }
        if (strncmp(name, "sabre", 5) == 0) {
            out->weapon = ACTOR_WEAPON_SABRE;
        } else if (strcmp(name, "weapon") == 0 && out->weapon == ACTOR_WEAPON_NONE) {
            out->weapon = ACTOR_WEAPON_MOUNT;
        }
    }
    return true;
}

static HANDLE open_archive(char *path, size_t path_size)
{
    HANDLE file;

    text_format(path, path_size, "%sbig.lab", host_directory());
    file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (file != INVALID_HANDLE_VALUE) {
        return file;
    }
    text_format(path, path_size, "big.lab");
    return CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
}

bool actor_catalog_load(void)
{
    char     path[MAX_PATH];
    HANDLE   file;
    uint8_t  head[LAB_HEAD];
    uint32_t entries;
    uint32_t names_size;
    uint8_t *directory = NULL;
    char    *names = NULL;
    uint8_t *nodes = NULL;
    uint32_t i;
    uint32_t unreadable = 0;

    if (catalog.tried) {
        return catalog.count != 0;
    }
    catalog.tried = true;

    file = open_archive(path, sizeof path);
    if (file == INVALID_HANDLE_VALUE) {
        log_warning("actor catalogue: big.lab was not found beside the game or in its working "
                    "folder, so the spawner offers only the level's own actor files");
        return false;
    }
    if (!read_at(file, 0, head, sizeof head) || memcmp(head, "LABN", 4) != 0) {
        log_warning("actor catalogue: %s does not begin with LABN", path);
        CloseHandle(file);
        return false;
    }
    entries    = u32_at(head, 8);
    names_size = u32_at(head, 12);
    if (entries == 0 || entries > 100000u || names_size > (16u << 20)) {
        log_warning("actor catalogue: %s claims %u entries and %u bytes of names, which is not "
                    "an archive this reads", path, entries, names_size);
        CloseHandle(file);
        return false;
    }
    directory = (uint8_t *)malloc(entries * LAB_ENTRY);
    names     = (char *)malloc(names_size + 1u);
    nodes     = (uint8_t *)malloc(NODES_MAX * NODE_RECORD);
    if (directory == NULL || names == NULL || nodes == NULL ||
        !read_at(file, LAB_HEAD, directory, entries * LAB_ENTRY) ||
        !read_at(file, LAB_HEAD + entries * LAB_ENTRY, names, names_size)) {
        log_warning("actor catalogue: the directory of %s could not be read", path);
        free(directory);
        free(names);
        free(nodes);
        CloseHandle(file);
        return false;
    }
    names[names_size] = '\0';

    for (i = 0; i < entries && catalog.count < ACTOR_CATALOG_MAX; ++i) {
        const uint8_t         *record = directory + i * LAB_ENTRY;
        uint32_t               name_offset = u32_at(record, 0);
        const char            *name;
        size_t                 length;
        actor_catalog_entry_t *entry;

        if (name_offset >= names_size) {
            continue;
        }
        name   = names + name_offset;
        length = strlen(name);
        if (length < 5u || length >= ACTOR_CATALOG_NAME_MAX ||
            _stricmp(name + length - 4u, ".baf") != 0) {
            continue;
        }
        entry = &catalog.entry[catalog.count];
        memset(entry, 0, sizeof *entry);
        memcpy(entry->name, name, length + 1u);
        if (!read_actor(file, u32_at(record, 4), u32_at(record, 8), nodes, entry)) {
            unreadable++;
            continue;
        }
        catalog.data[catalog.count] = u32_at(record, 4);
        catalog.size[catalog.count] = u32_at(record, 8);
        catalog.count++;
    }
    free(directory);
    free(names);
    free(nodes);
    CloseHandle(file);
    memcpy(catalog.path, path, sizeof catalog.path);

    log_info("actor catalogue: %u actor files read from %s (%u unreadable), with their clip "
             "counts, whether each has a body and what it holds",
             catalog.count, path, unreadable);
    return catalog.count != 0;
}

uint32_t actor_catalog_count(void)
{
    return catalog.count;
}

const actor_catalog_entry_t *actor_catalog_at(uint32_t index)
{
    return (index < catalog.count) ? &catalog.entry[index] : NULL;
}

const actor_catalog_entry_t *actor_catalog_find(const char *file)
{
    uint32_t i;

    if (file == NULL) {
        return NULL;
    }
    for (i = 0; i < catalog.count; ++i) {
        if (_stricmp(catalog.entry[i].name, file) == 0) {
            return &catalog.entry[i];
        }
    }
    return NULL;
}

/* The animation block is the tail of the file: one descriptor and key per clip, back to back,
 * the descriptor's first word the size of the pair, the key's name its first 0x24 bytes. The
 * walk above proved the sum lands on the sound event table for every file in the archive. */
bool actor_catalog_clips(const char *file, actor_clip_t *out, uint32_t max, uint32_t *count)
{
    HANDLE   archive;
    uint32_t i;
    uint32_t at;
    uint32_t end;
    uint32_t anim_size;
    uint32_t clips;
    uint8_t  header[BAF_HEADER];

    *count = 0;
    if (!actor_catalog_load()) {
        return false;
    }
    for (i = 0; i < catalog.count; ++i) {
        if (_stricmp(catalog.entry[i].name, file) == 0) {
            break;
        }
    }
    if (i == catalog.count) {
        return false;
    }
    archive = CreateFileA(catalog.path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0,
                          NULL);
    if (archive == INVALID_HANDLE_VALUE || !read_at(archive, catalog.data[i], header,
                                                     sizeof header)) {
        if (archive != INVALID_HANDLE_VALUE) {
            CloseHandle(archive);
        }
        return false;
    }
    anim_size = u32_at(header, BAF_ANIM_SIZE);
    clips     = u32_at(header, BAF_CLIP_COUNT);
    end       = catalog.data[i] + catalog.size[i];
    at        = end - anim_size;
    if (anim_size > catalog.size[i] || clips > max) {
        CloseHandle(archive);
        return false;
    }
    for (i = 0; i < clips; ++i) {
        uint8_t  descriptor[BAF_ANIM_DESC + ACTOR_CLIP_NAME_MAX];
        uint32_t size;
        uint32_t n;

        if (at + sizeof descriptor > end || !read_at(archive, at, descriptor, sizeof descriptor)) {
            break;
        }
        size = u32_at(descriptor, DESC_TOTAL_SIZE);
        out[i].flags = u32_at(descriptor, DESC_TRACK_FLAGS);
        out[i].usage = u32_at(descriptor, DESC_USAGE);
        for (n = 0; n + 1u < ACTOR_CLIP_NAME_MAX; ++n) {
            char c = (char)descriptor[BAF_ANIM_DESC + n];

            if (c == '\0' || c == '.') {
                break;
            }
            out[i].name[n] = (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
        }
        out[i].name[n] = '\0';
        if (size < BAF_ANIM_DESC || size > end - at) {
            break;
        }
        at += size;
    }
    CloseHandle(archive);
    *count = i;
    return i == clips;
}
