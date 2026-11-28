/* SPDX-License-Identifier: GPL-3.0-only */

/* The core table and handle every file of the module calls through. */

#ifndef ALBUM_ARTISTS_SHARED_H
#define ALBUM_ARTISTS_SHARED_H
#include "rv_api.h"
#include <stdint.h>

#define KEY_IN_MUSIC 1 /* Settings > Music Menu and Main Menu flip these two */
#define KEY_IN_MAIN 2

extern const struct rv_api *api;
extern rv_extension_handle self;

/* A hook argument as the pointer it carries. */
static inline void *ptr(uint32_t word)
{
    return (void *)(uintptr_t)word;
}

#endif
