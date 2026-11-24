/* SPDX-License-Identifier: GPL-3.0-only */

/* The dsp library has no hooks, settings or screens; its exports are the
 * whole of it. */

#include "rv_api.h"
#include <stddef.h>

const struct rv_descriptor *rv_module_entry(const struct rv_api *api,
                                            rv_extension_handle self)
{
    (void)api;
    (void)self;
    return NULL;
}
