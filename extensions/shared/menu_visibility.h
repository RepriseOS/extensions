/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include "rv_api.h"
#include "../reprise/reprise.h"

/* The old core kept the Main and Music menu checkboxes of its own rows in one
 * record, "MVIS": {key, visible} pairs, bit 0 Main and bit 1 Music. Only the
 * initial load imports the row's pair; without one, and on a later reset, the
 * extension defaults are recorded so rebooting cannot resurrect the old value.
 * On a core without the reprise library nothing is imported. */
static inline void migrate_menu_visibility(const struct rv_api *api,
                                           rv_extension_handle self, int first_load,
                                           uint32_t old_key, uint16_t music_key,
                                           int music_default, uint16_t main_key,
                                           int main_default)
{
    uint32_t value;
    if (!reprise_legacy_preference ||
        api->setting_read(self, music_key, &value, sizeof value) == sizeof value ||
        api->setting_read(self, main_key, &value, sizeof value) == sizeof value)
        return;
    int32_t music = music_default, main = main_default;
    uint32_t pairs[16];
    uint32_t bytes =
        first_load ? (reprise_legacy_preference)(0x5349564d, pairs, sizeof pairs) : 0;
    for (uint32_t i = 0; i + 1 < bytes / 4; i += 2)
        if (pairs[i] == old_key && pairs[i + 1] <= 3) {
            music = (pairs[i + 1] >> 1) & 1;
            main = pairs[i + 1] & 1;
            break;
        }
    api->setting_write(self, music_key, RV_SETTING_TYPE_BOOL, &music, sizeof music);
    api->setting_write(self, main_key, RV_SETTING_TYPE_BOOL, &main, sizeof main);
}
