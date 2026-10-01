/* The dedicated server's leave line says why a player went, as far as the server knows it.
 *
 * The server tells the one departure it decided, a peer the session sent away for falling behind,
 * from every other; a goodbye and a timeout it does not tell apart. The line used to read every
 * departure as a goodbye, the peer the server had sent away included. Driven through the log's own
 * ring and writer thread, with a listener in place of the file.
 */
#include "unittest.h"

#include "mp_server_log.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define LINES 8u

typedef struct heard {
    size_t count;
    char   line[LINES][192];
} heard_t;

static void listen(const char *line, void *context)
{
    heard_t *heard = (heard_t *)context;

    if (heard->count < LINES) {
        strncpy(heard->line[heard->count], line, sizeof heard->line[0] - 1u);
        heard->line[heard->count][sizeof heard->line[0] - 1u] = '\0';
        ++heard->count;
    }
}

static void check_a_leave_names_its_reason(void)
{
    static heard_t heard;

    ut_section("a leave says sent away for the peer the session sent away, gone for any other");
    memset(&heard, 0, sizeof heard);
    ut_check(mp_server_log_open(NULL, MP_SERVER_LOG_SESSION, 16u), "the log opens with no file");
    mp_server_log_set_line_listener(&listen, &heard);
    mp_server_log_leave(2u, "Anna", "sent away for falling behind");
    mp_server_log_leave(3u, "Bert", "gone");
    mp_server_log_close();   /* the writer drains what is left before it stops */

    ut_checkf(heard.count == 2u, "two lines were written (%u)", (unsigned)heard.count);
    ut_checkf(strstr(heard.line[0], "slot 2  Anna (sent away for falling behind)") != NULL,
              "the first names the reason the server had: '%s'", heard.line[0]);
    ut_checkf(strstr(heard.line[1], "slot 3  Bert (gone)") != NULL &&
                  strstr(heard.line[1], "goodbye") == NULL,
              "and the second claims no goodbye the server never heard: '%s'", heard.line[1]);
}

int main(void)
{
    check_a_leave_names_its_reason();
    return ut_summary("mp_server_log");
}
