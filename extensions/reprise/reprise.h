/* SPDX-License-Identifier: GPL-3.0-only */

/* What the RepriseOS core exports as the library `reprise`. There is no
 * reprise.rvx: the core answers the id itself. List it under
 * `[libraries] optional`, so the extension also runs on other cores, where
 * these stay NULL; call them through RV_OPTIONAL or test them first. */

#ifndef REPRISE_H
#define REPRISE_H
#include <stdint.h>

/* Copies the stored value of an old firmware preference `key` into `data`
 * and returns its size, 0 when absent; at most `capacity` bytes. */
uint32_t (reprise_legacy_preference)(uint32_t key, void *data, uint32_t capacity);

#endif
