/* start_level_row.c: see start_level_row.h. */
#include "start_level_row.h"

#include "start_level.h"

#include "common/ini.h"
#include "common/logging.h"
#include "common/text.h"

#define START_SECTION "dev_overlay"
#define START_KEY     "NewGameStartsAt"

void start_level_row_load(void)
{
    int level = ini_read_int(START_SECTION, START_KEY, 0);

    start_level_set(level);
    if (start_level_get() != 0) {
        log_info("%s=%d, so the next new game begins at level %d", START_KEY, level,
                 start_level_get());
    }
}

int start_level_row_get(void)
{
    return start_level_get();
}

bool start_level_row_set(int level)
{
    start_level_set(level);
    return ini_write_int(START_SECTION, START_KEY, start_level_get());
}

void start_level_row_value(char *out, uint32_t out_size)
{
    int  level = start_level_get();
    char stem[16];

    if (level == 0) {
        text_format(out, out_size, "Off");
        return;
    }
    start_level_stem(level, stem, sizeof stem);
    text_format(out, out_size, "%d %s", level, stem);
}
