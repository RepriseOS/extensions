/* SPDX-License-Identifier: GPL-3.0-only */

/* What Album Artists offers other extensions through api->extension_service. */

#ifndef ALBUM_ARTISTS_SERVICE_H
#define ALBUM_ARTISTS_SERVICE_H
#include <stdint.h>

#define ALBUM_ARTISTS_ID "album-artists"

struct album_artists_service {
    /* UTF-16 units of the record's Album Artist into `text`, when it differs
     * from the record's Artist; 0 otherwise, or while no library is imported. */
    uint32_t (*read)(const void *record, uint16_t *text, uint32_t max_units);
};

#endif
