/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef BALANCE_SHARED_H
#define BALANCE_SHARED_H

#include "balance.h"

enum {
    KEY_BALANCE = 1, /* int: the position, 0 to BALANCE_STEPS - 1 */
};

/* labels.c: the positions by name, "L only" to "R only". */
extern const char *const balance_labels[BALANCE_STEPS];

#endif
