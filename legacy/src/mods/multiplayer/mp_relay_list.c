/* mp_relay_list.c: the relay's public list over its own sockets. See the header. */
#include "mp_relay_list.h"

#include "mp_relay_wire.h"

#include "common/logging.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static bool reached(uint32_t now, uint32_t at)
{
    return now - at < 0x80000000u;
}

bool mp_relay_list_open(mp_relay_list_t *list, const mp_relay_services_t *services)
{
    memset(list, 0, sizeof *list);
    list->services = services;
    list->family   = -1;
    if (!mp_relay_socket_open(&list->socket)) {
        log_warning("the public list cannot ask the relay: no UDP socket would open (error %d)",
                    list->socket.last_error);
        return false;
    }
    mp_relay_listing_init(&list->listing, services->random);
    list->opened_at     = services->now_ms();
    list->next_fetch_at = list->opened_at;
    list->up            = true;
    return true;
}

static void take_addresses(mp_relay_list_t *list, const mp_relay_fetch_result_t *result,
                           uint32_t now)
{
    if (result->addresses == 0u) {
        if (!list->addresses_known) {
            log_warning("the public list cannot ask the relay: %s; the next try in %u s",
                        mp_relay_fetch_error_text(result->error),
                        (unsigned)(MP_RELAY_LOOKUP_RETRY_MS / 1000u));
            list->next_fetch_at = now + MP_RELAY_LOOKUP_RETRY_MS;
        }
        return;
    }
    mp_relay_socket_set_relay(&list->socket, result->address, result->addresses);
    list->addresses_known = true;
}

static void lookup(mp_relay_list_t *list, uint32_t now)
{
    mp_relay_fetch_result_t result;

    if (list->fetching) {
        /* Asked before the take: a fetch that finishes between the two is still in its slot. */
        bool running = list->services->fetch_running();

        if (list->services->fetch_take(&result)) {
            list->fetching = false;
            take_addresses(list, &result, now);
        } else if (!running) {
            list->fetching = false;   /* it finished, and another part of the program took it */
        }
        return;
    }
    if (list->addresses_known || !reached(now, list->next_fetch_at)) {
        return;
    }
    if (list->services->fetch_begin(false)) {
        list->fetching = true;
    } else if (!list->services->fetch_running() && list->services->fetch_take(&result)) {
        take_addresses(list, &result, now);   /* one nobody took: its addresses are as good */
    } else {
        list->next_fetch_at = now + MP_RELAY_FETCH_BUSY_MS;
    }
}

void mp_relay_list_pump(mp_relay_list_t *list)
{
    uint32_t now;
    int      family;
    size_t   bytes;

    if (list == NULL || !list->up) {
        return;
    }
    now = list->services->now_ms();
    lookup(list, now);
    if (!list->addresses_known) {
        return;
    }
    for (family = 0; family < (int)MP_RELAY_FAMILIES; ++family) {
        while ((bytes = mp_relay_socket_recv(&list->socket, (mp_relay_family_t)family,
                                             list->datagram, sizeof list->datagram)) != 0u) {
            if (mp_relay_listing_receive(&list->listing, now, list->datagram, bytes)) {
                list->family = family;
            }
        }
    }
    while ((bytes = mp_relay_listing_tick(&list->listing, now, list->datagram,
                                          sizeof list->datagram)) != 0u) {
        if (list->datagram[0] == MP_RELAY_TYPE_HELLO || list->family < 0) {
            for (family = 0; family < (int)MP_RELAY_FAMILIES; ++family) {
                (void)mp_relay_socket_send(&list->socket, (mp_relay_family_t)family,
                                           list->datagram, bytes);
            }
        } else {
            (void)mp_relay_socket_send(&list->socket, (mp_relay_family_t)list->family,
                                       list->datagram, bytes);
        }
    }
}

mp_relay_listing_state_t mp_relay_list_state(const mp_relay_list_t *list)
{
    uint32_t now;

    if (list == NULL || !list->up) {
        return MP_RELAY_LISTING_SILENT;
    }
    now = list->services->now_ms();
    if (!list->listing.started) {
        return reached(now, list->opened_at + MP_RELAY_LIST_SILENT_MS) ? MP_RELAY_LISTING_SILENT
                                                                       : MP_RELAY_LISTING_ASKING;
    }
    return mp_relay_listing_state(&list->listing, now);
}

size_t mp_relay_list_count(const mp_relay_list_t *list)
{
    return list != NULL && list->up ? list->listing.count : 0u;
}

const mp_relay_list_row_t *mp_relay_list_row(const mp_relay_list_t *list, size_t index)
{
    return index < mp_relay_list_count(list) ? &list->listing.row[index] : NULL;
}

void mp_relay_list_close(mp_relay_list_t *list)
{
    if (list == NULL || !list->up) {
        return;
    }
    log_info("the public list closes: %u round(s), %u page(s), %u row(s) shown last, %u "
             "unreadable, %u nack(s)", (unsigned)list->listing.rounds,
             (unsigned)list->listing.pages, (unsigned)list->listing.count,
             (unsigned)list->listing.unreadable, (unsigned)list->listing.nacks);
    mp_relay_socket_close(&list->socket);
    list->up = false;
}
