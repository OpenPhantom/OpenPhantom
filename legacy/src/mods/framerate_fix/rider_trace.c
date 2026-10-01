/* rider_trace.c: see rider_trace.h. */
#include "rider_trace.h"

#include "common/ini.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/numeric.h"

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define FRAMERATE_SECTION   "framerate_fix"
#define OBJECT_ACTOR_OFFSET 0x14u    /* bapObj -> bapActor, the template */
#define ACTOR_NAME_OFFSET   0x08u    /* the .baf file name, 0x18 bytes */
#define ACTOR_NAME_SIZE     0x18u
#define RING_SIZE           64u      /* per frame; a level draws far fewer of one model */
#define MODEL_FILTER_MAX    16u

/* The x87 status word: bits 11 to 13 are TOP, the stack pointer, 0 when nothing is pushed; the
 * low six bits are the sticky exception flags, IE DE ZE OE UE PE. */
#define X87_STATUS_TOP_MASK       0x3800u
#define X87_STATUS_EXCEPTION_MASK 0x003Fu

typedef struct trace_record {
    uint32_t frame;
    uint32_t object;
    char     name[ACTOR_NAME_SIZE + 1];
    uint32_t step;
    uint32_t gap;
    float    alpha;
    float    weight;
    float    mine[3];
    float    engine[3];
    float    current[3];
    float    drawn[3];
    bool     from_tracker;
    bool     refused;
    bool     odd;             /* recorded for its own sake, whatever the model */
    uint16_t status;          /* the x87 words at the hook's entry */
    uint16_t control;
} trace_record_t;

static struct {
    bool           on;
    char           model[MODEL_FILTER_MAX];
    size_t         model_length;
    uint32_t       frame;
    trace_record_t ring[RING_SIZE];
    uint32_t       count;
    uint32_t       dropped;
    uint8_t        key_down[256];
    uint16_t       last_end_status;    /* the frame-end sample, for the change */
    bool           end_sampled;
} trace;

static bool finite3(const float *v)
{
    return numeric_is_finite(v[0]) && numeric_is_finite(v[1]) && numeric_is_finite(v[2]);
}

/* The template file name behind the object, "" when the chain does not read. */
static void model_name_of(const char *object, char name[ACTOR_NAME_SIZE + 1])
{
    uint32_t actor = 0;

    memset(name, 0, ACTOR_NAME_SIZE + 1);
    if (!memory_try_read((uintptr_t)object + OBJECT_ACTOR_OFFSET, &actor, sizeof actor) ||
        actor == 0 ||
        !memory_try_read((uintptr_t)actor + ACTOR_NAME_OFFSET, name, ACTOR_NAME_SIZE)) {
        name[0] = '\0';
    }
    name[ACTOR_NAME_SIZE] = '\0';
}

void rider_trace_install(void)
{
    trace.on = ini_read_bool(FRAMERATE_SECTION, "RiderTrace", false);
    if (!trace.on) {
        return;
    }
    (void)ini_read_string(FRAMERATE_SECTION, "RiderTraceModel", "obi", trace.model,
                          sizeof trace.model);
    trace.model_length = strlen(trace.model);
    log_info("RiderTrace=1: every blend of a model whose file starts with \"%s\" is written out "
             "frame by frame, with the keys, and any object's blend that was refused, not finite "
             "or entered with the x87 stack in use. A measurement, and a large one; switch it off "
             "when the run is done", trace.model);
}

bool rider_trace_on(void)
{
    return trace.on;
}

void rider_trace_blend(const char *object, uint32_t step, uint32_t gap, float alpha, float weight,
                       const float *mine, bool from_tracker, const float *engine,
                       const float *current, const float *drawn, bool refused)
{
    trace_record_t *r;
    uint16_t        status = 0;
    uint16_t        control = 0;
    char            name[ACTOR_NAME_SIZE + 1];
    bool            odd;
    bool            wanted;

    __asm {
        fnstsw status
        fnstcw control
    }
    odd = refused || (status & X87_STATUS_TOP_MASK) != 0 || !finite3(mine) ||
          !finite3(current) || !finite3(drawn) || !numeric_is_finite(alpha) || gap > 1u;
    model_name_of(object, name);
    wanted = trace.model_length != 0 && _strnicmp(name, trace.model, trace.model_length) == 0;
    if (!odd && !wanted) {
        return;
    }
    if (trace.count >= RING_SIZE) {
        ++trace.dropped;
        return;
    }
    r = &trace.ring[trace.count++];
    r->frame        = trace.frame;
    r->object       = (uint32_t)(uintptr_t)object;
    memcpy(r->name, name, sizeof r->name);
    r->step         = step;
    r->gap          = gap;
    r->alpha        = alpha;
    r->weight       = weight;
    memcpy(r->mine, mine, sizeof r->mine);
    memcpy(r->engine, engine, sizeof r->engine);
    memcpy(r->current, current, sizeof r->current);
    memcpy(r->drawn, drawn, sizeof r->drawn);
    r->from_tracker = from_tracker;
    r->refused      = refused;
    r->odd          = odd;
    r->status       = status;
    r->control      = control;
}

static void note_keys(void)
{
    int vk;

    for (vk = 1; vk < 256; ++vk) {
        bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;

        if (down != (trace.key_down[vk] != 0)) {
            trace.key_down[vk] = down ? 1u : 0u;
            log_info("trace: frame %u key %02X %s", (unsigned)trace.frame, (unsigned)vk,
                     down ? "down" : "up");
        }
    }
}

void rider_trace_frame(void)
{
    uint32_t i;

    if (!trace.on) {
        return;
    }
    for (i = 0; i < trace.count; ++i) {
        const trace_record_t *r = &trace.ring[i];

        log_info("trace: frame %u obj %08X %s step %u gap %u alpha %.4f weight %.4f %s "
                 "mine(%.3f %.3f %.3f) engine(%.3f %.3f %.3f) cur(%.3f %.3f %.3f) "
                 "drawn(%.3f %.3f %.3f)%s x87 status %04X control %04X%s",
                 (unsigned)r->frame, (unsigned)r->object, r->name[0] ? r->name : "?",
                 (unsigned)r->step, (unsigned)r->gap, (double)r->alpha, (double)r->weight,
                 r->from_tracker ? "tracker" : "engine-pair",
                 (double)r->mine[0], (double)r->mine[1], (double)r->mine[2],
                 (double)r->engine[0], (double)r->engine[1], (double)r->engine[2],
                 (double)r->current[0], (double)r->current[1], (double)r->current[2],
                 (double)r->drawn[0], (double)r->drawn[1], (double)r->drawn[2],
                 r->refused ? " REFUSED" : "", (unsigned)r->status, (unsigned)r->control,
                 r->odd ? " ODD" : "");
    }
    if (trace.dropped != 0) {
        log_info("trace: frame %u, %u more blends than the ring holds were dropped",
                 (unsigned)trace.frame, (unsigned)trace.dropped);
    }
    trace.count   = 0;
    trace.dropped = 0;
    note_keys();
    {
        uint16_t status = 0;

        __asm {
            fnstsw status
        }
        if (!trace.end_sampled ||
            ((status ^ trace.last_end_status) & X87_STATUS_TOP_MASK) != 0) {
            log_info("trace: frame %u ends with x87 status %04X, stack pointer %u",
                     (unsigned)trace.frame, (unsigned)status,
                     (unsigned)((status & X87_STATUS_TOP_MASK) >> 11));
        }
        trace.end_sampled     = true;
        trace.last_end_status = status;
    }
    ++trace.frame;
}
