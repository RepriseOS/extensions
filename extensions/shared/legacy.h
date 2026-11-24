/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include "rv_api.h"

/* Only the initial load imports the old firmware switch. A later reset records
 * the extension default so rebooting cannot resurrect the old preference. */
static inline void migrate_switch(const struct rv_api *api, rv_extension_handle self,
                                  int first_load, uint16_t key, uint32_t old_key,
                                  uint32_t old_on, int on, int off, uint8_t type)
{
    uint32_t value;
    if (!rv_has_reprise_services(api) ||
        api->setting_read(self, key, &value, sizeof value) == sizeof value)
        return;
    int32_t mode = off;
    if (first_load &&
        api->legacy_preference(old_key, &value, sizeof value) == sizeof value &&
        value == old_on)
        mode = on;
    api->setting_write(self, key, type, &mode, sizeof mode);
}
