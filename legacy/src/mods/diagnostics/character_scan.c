/* character_scan.c: see character_scan.h. Numbers only, no engine, no memory reads. */
#include "character_scan.h"

#include <math.h>

uint32_t character_scan_slot_stride(uint32_t element_size)
{
    if (element_size == 0u || element_size > CHARACTER_POOL_ELEMENT_SIZE_MAX) {
        return 0u;
    }
    return element_size + CHARACTER_POOL_LINK_SIZE;
}

uint32_t character_scan_slot_offset(uint32_t element_size, uint32_t index)
{
    uint32_t stride = character_scan_slot_stride(element_size);
    uint64_t offset;

    if (stride == 0u) {
        return 0u;
    }
    /* Widened before multiplying rather than after, so a capacity that is itself nonsense cannot
       wrap the product back into a plausible looking offset. */
    offset = (uint64_t)CHARACTER_POOL_SLOT_ARRAY_OFFSET + (uint64_t)stride * (uint64_t)index;
    if (offset > 0xFFFFFFFFull) {
        return 0u;
    }
    return (uint32_t)offset;
}

bool character_scan_slot_is_live(uint32_t link_word)
{
    return link_word != CHARACTER_POOL_FREE_LINK;
}

bool character_scan_pool_is_sane(uint32_t element_size, uint32_t capacity)
{
    if (element_size < CHARACTER_POOL_ELEMENT_SIZE_MIN ||
        element_size > CHARACTER_POOL_ELEMENT_SIZE_MAX) {
        return false;
    }
    return capacity != 0u && capacity <= CHARACTER_POOL_CAPACITY_MAX;
}

/* Squared separation, shared by the distance and the radius test so the two can never disagree. */
static float separation_squared(const float position[3], const float other[3])
{
    float dx = position[0] - other[0];
    float dy = position[1] - other[1];
    float dz = position[2] - other[2];

    return (dx * dx) + (dy * dy) + (dz * dz);
}

float character_scan_distance(const float position[3], const float other[3])
{
    float squared = separation_squared(position, other);

    /* Written as a positive test so a NaN separation answers zero here rather than propagating
       through the square root and into the log. */
    if (!(squared > 0.0f)) {
        return 0.0f;
    }
    return sqrtf(squared);
}

bool character_scan_within_radius(const float position[3], const float other[3], float radius)
{
    float squared;

    if (!(radius > 0.0f)) {
        return false;
    }
    squared = separation_squared(position, other);
    /* Written as a positive test so a NaN separation falls through to false. */
    return squared <= (radius * radius);
}

float character_scan_vertical_delta(const float position[3], const float previous[3])
{
    return position[2] - previous[2];
}

character_motion_t character_scan_classify(float vertical_delta)
{
    if (vertical_delta <= -CHARACTER_MOTION_EPSILON) {
        return CHARACTER_MOTION_SINKING;
    }
    if (vertical_delta >= CHARACTER_MOTION_EPSILON) {
        return CHARACTER_MOTION_RISING;
    }
    /* A NaN delta reaches here, because neither comparison above is true of it, and reads as still.
       That is the honest answer: a NaN says the two positions cannot be compared, not that the
       character is descending. */
    return CHARACTER_MOTION_STILL;
}

const char *character_scan_motion_text(character_motion_t motion)
{
    switch (motion) {
    case CHARACTER_MOTION_SINKING:
        return "sinking";
    case CHARACTER_MOTION_RISING:
        return "rising";
    case CHARACTER_MOTION_STILL:
    default:
        return "still";
    }
}

void character_scan_copy_name(const char *raw, size_t raw_size, char *out, size_t out_size)
{
    size_t limit;
    size_t index;

    if (out == NULL || out_size == 0u) {
        return;
    }
    out[0] = '\0';
    if (raw == NULL || raw_size == 0u) {
        return;
    }

    limit = raw_size < (out_size - 1u) ? raw_size : (out_size - 1u);
    for (index = 0u; index < limit; ++index) {
        char byte = raw[index];

        if (byte == '\0') {
            break;
        }
        out[index] = (byte >= 0x20 && byte < 0x7F) ? byte : '.';
    }
    out[index] = '\0';
}

void character_scan_track_reset(character_track_t *table, size_t count)
{
    size_t index;

    if (table == NULL) {
        return;
    }
    for (index = 0u; index < count; ++index) {
        table[index].record = 0u;
        table[index].z = 0.0f;
    }
}

bool character_scan_track(character_track_t *table, size_t count, uint32_t record, float z,
                          float *out_change)
{
    size_t index;
    size_t free_slot = count;

    if (table == NULL || count == 0u || record == 0u) {
        return false;
    }
    for (index = 0u; index < count; ++index) {
        if (table[index].record == record) {
            if (out_change != NULL) {
                *out_change = z - table[index].z;
            }
            table[index].z = z;
            return true;
        }
        if (free_slot == count && table[index].record == 0u) {
            free_slot = index;
        }
    }

    /* Not seen before. Take a free entry, or evict a deterministic one so a crowded level degrades
       predictably rather than by whichever character happened to be walked first. */
    index = (free_slot < count) ? free_slot : (size_t)(record % (uint32_t)count);
    table[index].record = record;
    table[index].z = z;
    return false;
}
