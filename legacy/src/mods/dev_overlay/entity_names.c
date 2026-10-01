/* entity_names.c: see entity_names.h.
 *
 * The names are English, as the panel is. The pickups are named for what the game says they give
 * (its own info text, in the installed language, names them by the same amounts); the characters
 * and creatures by the film. A file whose identity was not certain from its data and its levels is
 * left out on purpose: a wrong name is believed, and a stem only looks unfinished.
 */
#include "entity_names.h"

#include <string.h>

#define VEHICLE 1u

typedef struct entity_name_row {
    const char *stem;
    const char *name;
    uint32_t    marks;
} entity_name_row_t;

/* Sorted by stem, which the test holds. */
static const entity_name_row_t NAMES[] = {
    { "anakin",   "Anakin Skywalker",       0u },
    { "baron",    "Battle droid",           0u },
    { "baronbaz", "Battle droid, bazooka",  0u },
    { "baroncom", "Battle droid commander", 0u },
    { "baronsec", "Security battle droid",  0u },
    { "bith",     "Bith",                   0u },
    { "bossnass", "Boss Nass",              0u },
    { "c3po",     "C-3PO",                  0u },
    { "destroyr", "Droideka",               0u },
    { "drdfitr",  "Droid starfighter",      VEHICLE },
    { "eopie",    "Eopie",                  0u },
    { "gamguard", "Gamorrean guard",        0u },
    { "gonk",     "Gonk droid",             0u },
    { "gonkblk",  "Gonk droid, black",      0u },
    { "gunggrd",  "Gungan guard",           0u },
    { "handmaid", "Handmaiden",             0u },
    { "hovdroid", "Hover cannon",           0u },
    { "jabba",    "Jabba the Hutt",         0u },
    { "jarjar",   "Jar Jar Binks",          0u },
    { "jawa",     "Jawa",                   0u },
    { "jawagun",  "Jawa, armed",            0u },
    { "kaadu",    "Kaadu",                  0u },
    { "mine",     "Mine",                   0u },
    { "mtt",      "MTT transport",          VEHICLE },
    { "nabfitr",  "Naboo starfighter",      VEHICLE },
    { "nguard",   "Naboo guard",            0u },
    { "nikto1",   "Nikto",                  0u },
    { "nimoid",   "Neimoidian",             0u },
    { "nrguard",  "Naboo royal guard",      0u },
    { "nuna",     "Nuna",                   0u },
    { "obiwan",   "Obi-Wan Kenobi",         0u },
    { "padme",    "Padme",                  0u },
    { "palpatin", "Senator Palpatine",      0u },
    { "panaka",   "Captain Panaka",         0u },
    { "pekopeko", "Peko-peko",              0u },
    { "pitdroid", "Pit droid",              0u },
    { "pwrbacta", "Small medpack",          0u },
    { "pwrbiggn", "R-65 blaster, 100",      0u },
    { "pwrblst1", "Blaster, 250 shots",     0u },
    { "pwrchain", "Repeating blaster, 300", 0u },
    { "pwrgball", "Gungan energy balls",    0u },
    { "pwrgrend", "Flash grenades",         0u },
    { "pwrhlth1", "Large medpack",          0u },
    { "pwrrckt1", "Proton launcher, 5",     0u },
    { "pwrshld1", "Shield energy",          0u },
    { "pwrthrm1", "Thermal detonator",      0u },
    { "queen",    "Queen Amidala",          0u },
    { "quiweap",  "Qui-Gon Jinn",           0u },
    { "r2d2",     "R2-D2",                  0u },
    { "ronto",    "Ronto",                  0u },
    { "shmi",     "Shmi Skywalker",         0u },
    { "sithdrd",  "Sith probe droid",       0u },
    { "sithjedi", "Darth Maul",             0u },
    { "stap",     "STAP",                   0u },
    { "tc14",     "TC-14",                  0u },
    { "tripod",   "Tripod gun",             0u },
    { "tusken",   "Tusken Raider",          0u },
    { "tuskgun",  "Tusken Raider, armed",   0u },
    { "watto",    "Watto",                  0u },
    { "whale",    NULL,                     VEHICLE }
};

#define NAME_ROWS (sizeof NAMES / sizeof NAMES[0])

/* The row whose stem is `file` up to its dot, case aside, or NULL. */
static const entity_name_row_t *row_of(const char *file)
{
    uint32_t i;

    if (file == NULL) {
        return NULL;
    }
    for (i = 0; i < NAME_ROWS; ++i) {
        size_t n = strlen(NAMES[i].stem);

        if (_strnicmp(file, NAMES[i].stem, n) == 0 && (file[n] == '.' || file[n] == '\0')) {
            return &NAMES[i];
        }
    }
    return NULL;
}

const char *entity_name_of(const char *file)
{
    const entity_name_row_t *row = row_of(file);

    return row != NULL ? row->name : NULL;
}

bool entity_name_is_vehicle(const char *file)
{
    const entity_name_row_t *row = row_of(file);

    return row != NULL && (row->marks & VEHICLE) != 0u;
}

uint32_t entity_names_count(void)
{
    return (uint32_t)NAME_ROWS;
}

bool entity_names_at(uint32_t index, const char **stem, const char **name)
{
    if (index >= NAME_ROWS || stem == NULL || name == NULL) {
        return false;
    }
    *stem = NAMES[index].stem;
    *name = NAMES[index].name;
    return true;
}
