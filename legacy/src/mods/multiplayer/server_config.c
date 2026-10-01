/* server_config.c: the server's settings, read from and written to the shared ini. See the header
 * for why the level list here is a convenience rather than a source of truth.
 */
#include "server_config.h"

#include "mp_rules.h"
#include "mp_settings.h"
#include "mp_server_log.h"

#include "common/ini.h"

#include <stdio.h>
#include <string.h>

/* The eleven the game shipped, as the relative path the loader takes and the name a person knows
 * them by. The folder is the engine's own and the extension is compared without regard to case,
 * so the spelling below is the one the files carry. */
static const struct {
    const char *path;
    const char *title;
} SHIPPED[] = {
    { "level\\ASSAULT.B3D", "Assault"     },
    { "level\\BIGCITY.B3D", "Theed"       },
    { "level\\ESPA.B3D",    "Escape"      },
    { "level\\FEDSHIP.B3D", "Federation"  },
    { "level\\FINAL.B3D",   "Final"       },
    { "level\\GARDEN.B3D",  "Garden"      },
    { "level\\GUNGA.B3D",   "Gungan"      },
    { "level\\MAUL.B3D",    "Maul"        },
    { "level\\QUEEN.B3D",   "Queen"       },
    { "level\\RACE.B3D",    "Race"        },
    { "level\\SWAMP.B3D",   "Swamp"       }
};

size_t server_config_level_count(void)
{
    return sizeof SHIPPED / sizeof SHIPPED[0];
}

const char *server_config_level_path(size_t index)
{
    return index < server_config_level_count() ? SHIPPED[index].path : "";
}

const char *server_config_level_title(size_t index)
{
    return index < server_config_level_count() ? SHIPPED[index].title : "";
}

static void copy_into(char *out, size_t capacity, const char *text)
{
    size_t i;

    for (i = 0; i + 1u < capacity && text != NULL && text[i] != '\0'; ++i) {
        out[i] = text[i];
    }
    out[i] = '\0';
    while (++i < capacity) {
        out[i - 1u] = '\0';   /* the rest zeroed, because the setup note encodes the whole field */
    }
}

void server_config_default(server_config_t *config)
{
    if (config == NULL) {
        return;
    }
    memset(config, 0, sizeof *config);
    config->port  = 27960u;
    config->slots = (uint8_t)MP_SETTINGS_SLOTS_MAX;
    copy_into(config->name, sizeof config->name, "Obi Server");

    config->setup.mode        = (uint8_t)MP_LOBBY_MODE_TDM;
    config->setup.flags       = (uint8_t)MP_LOBBY_F_STARTED;
    config->setup.level_index = 0u;
    copy_into(config->setup.level, sizeof config->setup.level, server_config_level_path(0));
    copy_into(config->setup.title, sizeof config->setup.title, server_config_level_title(0));
    mp_rules_default(&config->setup.rules);

    config->log_categories = (uint32_t)MP_SERVER_LOG_DEFAULT;
    config->log_ring       = 8192u;
    copy_into(config->log_path, sizeof config->log_path, "obi_dedicated.log");
}

bool server_config_clamp(server_config_t *config)
{
    bool moved = false;

    if (config == NULL) {
        return false;
    }
    if (config->port == 0u) {
        config->port = 27960u;
        moved = true;
    }
    if (config->slots < 2u) {
        config->slots = 2u;
        moved = true;
    }
    /* Four, not the fifteen the session could hold: a server has no body of its own, and every
     * client shows its own body and three far ones. A fifth client would play in nobody else's
     * world. */
    if (config->slots > (uint8_t)MP_SETTINGS_SLOTS_MAX) {
        config->slots = (uint8_t)MP_SETTINGS_SLOTS_MAX;
        moved = true;
    }
    /* A dedicated server plays one game. Co-op is a campaign out of somebody's savegame and this
     * machine has neither a savegame nor a campaign, so the mode is not an operator's choice. */
    if (config->setup.mode != (uint8_t)MP_LOBBY_MODE_TDM) {
        config->setup.mode = (uint8_t)MP_LOBBY_MODE_TDM;
        moved = true;
    }
    config->setup.flags |= (uint8_t)MP_LOBBY_F_STARTED;
    if (config->setup.level[0] == '\0') {
        copy_into(config->setup.level, sizeof config->setup.level, server_config_level_path(0));
        moved = true;
    }
    if (mp_rules_clamp(&config->setup.rules)) {
        moved = true;
    }
    if ((config->log_categories & ~(uint32_t)MP_SERVER_LOG_ALL) != 0u) {
        config->log_categories &= (uint32_t)MP_SERVER_LOG_ALL;
        moved = true;
    }
    if (config->log_ring < 256u) {
        config->log_ring = 256u;
        moved = true;
    }
    return moved;
}

void server_config_load(server_config_t *config)
{
    char text[SERVER_CONFIG_LOG_PATH_MAX];

    if (config == NULL) {
        return;
    }
    server_config_default(config);

    config->port  = (uint16_t)ini_read_int(SERVER_CONFIG_SECTION, "Port", (int32_t)config->port);
    config->slots = (uint8_t)ini_read_int(SERVER_CONFIG_SECTION, "Slots", (int32_t)config->slots);

    if (ini_read_string(SERVER_CONFIG_SECTION, "Name", config->name, text, sizeof text)) {
        copy_into(config->name, sizeof config->name, text);
    }
    if (ini_read_string(SERVER_CONFIG_SECTION, "Password", "", text, sizeof text)) {
        copy_into(config->password, sizeof config->password, text);
    }
    if (ini_read_string(SERVER_CONFIG_SECTION, "Level", config->setup.level, text, sizeof text)) {
        copy_into(config->setup.level, sizeof config->setup.level, text);
    }
    if (ini_read_string(SERVER_CONFIG_SECTION, "Title", config->setup.title, text, sizeof text)) {
        copy_into(config->setup.title, sizeof config->setup.title, text);
    }
    config->setup.level_index =
        (uint8_t)ini_read_int(SERVER_CONFIG_SECTION, "LevelIndex",
                              (int32_t)config->setup.level_index);

    config->setup.rules.score_limit =
        (uint16_t)ini_read_int(SERVER_CONFIG_SECTION, "ScoreLimit",
                               (int32_t)config->setup.rules.score_limit);
    config->setup.rules.time_limit_s =
        (uint16_t)ini_read_int(SERVER_CONFIG_SECTION, "TimeLimit",
                               (int32_t)config->setup.rules.time_limit_s);
    config->setup.rules.team_penalty =
        (int8_t)ini_read_int(SERVER_CONFIG_SECTION, "TeamPenalty",
                             (int32_t)config->setup.rules.team_penalty);
    config->setup.rules.suicide_penalty =
        (int8_t)ini_read_int(SERVER_CONFIG_SECTION, "SuicidePenalty",
                             (int32_t)config->setup.rules.suicide_penalty);
    config->setup.rules.respawn_tenths =
        (uint8_t)ini_read_int(SERVER_CONFIG_SECTION, "RespawnTenths",
                              (int32_t)config->setup.rules.respawn_tenths);

    config->setup.rules.flags = 0u;
    if (ini_read_bool(SERVER_CONFIG_SECTION, "Teams", true)) {
        config->setup.rules.flags |= (uint8_t)MP_RULES_F_TEAMS;
    }
    if (ini_read_bool(SERVER_CONFIG_SECTION, "FriendlyFire", false)) {
        config->setup.rules.flags |= (uint8_t)MP_RULES_F_FRIENDLY_FIRE;
    }

    config->log_categories =
        (uint32_t)ini_read_int(SERVER_CONFIG_SECTION, "LogCategories",
                               (int32_t)config->log_categories);
    config->log_ring =
        (uint32_t)ini_read_int(SERVER_CONFIG_SECTION, "LogRing", (int32_t)config->log_ring);
    if (ini_read_string(SERVER_CONFIG_SECTION, "LogFile", config->log_path, text, sizeof text)) {
        copy_into(config->log_path, sizeof config->log_path, text);
    }

    (void)server_config_clamp(config);
}

bool server_config_save(const server_config_t *config)
{
    bool ok = true;

    if (config == NULL) {
        return false;
    }
    ok = ini_write_int(SERVER_CONFIG_SECTION, "Port", (int32_t)config->port) && ok;
    ok = ini_write_int(SERVER_CONFIG_SECTION, "Slots", (int32_t)config->slots) && ok;
    ok = ini_write_string(SERVER_CONFIG_SECTION, "Name", config->name) && ok;
    ok = ini_write_string(SERVER_CONFIG_SECTION, "Password", config->password) && ok;
    ok = ini_write_string(SERVER_CONFIG_SECTION, "Level", config->setup.level) && ok;
    ok = ini_write_string(SERVER_CONFIG_SECTION, "Title", config->setup.title) && ok;
    ok = ini_write_int(SERVER_CONFIG_SECTION, "LevelIndex",
                       (int32_t)config->setup.level_index) && ok;
    ok = ini_write_int(SERVER_CONFIG_SECTION, "ScoreLimit",
                       (int32_t)config->setup.rules.score_limit) && ok;
    ok = ini_write_int(SERVER_CONFIG_SECTION, "TimeLimit",
                       (int32_t)config->setup.rules.time_limit_s) && ok;
    ok = ini_write_int(SERVER_CONFIG_SECTION, "TeamPenalty",
                       (int32_t)config->setup.rules.team_penalty) && ok;
    ok = ini_write_int(SERVER_CONFIG_SECTION, "SuicidePenalty",
                       (int32_t)config->setup.rules.suicide_penalty) && ok;
    ok = ini_write_int(SERVER_CONFIG_SECTION, "RespawnTenths",
                       (int32_t)config->setup.rules.respawn_tenths) && ok;
    ok = ini_write_int(SERVER_CONFIG_SECTION, "Teams",
                       mp_rules_teams(&config->setup.rules) ? 1 : 0) && ok;
    ok = ini_write_int(SERVER_CONFIG_SECTION, "FriendlyFire",
                       mp_rules_friendly_fire(&config->setup.rules) ? 1 : 0) && ok;
    ok = ini_write_int(SERVER_CONFIG_SECTION, "LogCategories",
                       (int32_t)config->log_categories) && ok;
    ok = ini_write_int(SERVER_CONFIG_SECTION, "LogRing", (int32_t)config->log_ring) && ok;
    ok = ini_write_string(SERVER_CONFIG_SECTION, "LogFile", config->log_path) && ok;
    return ok;
}
