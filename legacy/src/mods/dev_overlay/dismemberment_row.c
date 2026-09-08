/* dismemberment_row.c: see dismemberment_row.h. */
#include "dismemberment_row.h"

#include "common/ini.h"

#define DISMEMBERMENT_SECTION "dismemberment"
#define DISMEMBERMENT_KEY     "Mode"

/* The three values the key takes. Named here rather than shared with dismemberment.dll, because
 * feature DLLs in this tree do not include each other's headers; the numbers are the settings
 * file's own contract and both sides read them from it. */
#define DISMEMBERMENT_OFF      0
#define DISMEMBERMENT_ON_DEATH 2

bool dismemberment_row_get(void)
{
    /* The default is the shipped default in dismemberment, and the two have to stay in step: a row
     * that reads OFF while the feature is ON would be worse than no row at all. */
    return ini_read_int(DISMEMBERMENT_SECTION, DISMEMBERMENT_KEY, DISMEMBERMENT_OFF) !=
           DISMEMBERMENT_OFF;
}

bool dismemberment_row_set(bool on)
{
    return ini_write_int(DISMEMBERMENT_SECTION, DISMEMBERMENT_KEY,
                         on ? DISMEMBERMENT_ON_DEATH : DISMEMBERMENT_OFF);
}
