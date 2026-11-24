/* SPDX-License-Identifier: GPL-3.0-only */
#include "../shared/legacy.h"
#include "ext.h"
#include "night_shift.h"
#include "schedule.h"
#include "rv_menus.h"
#include "rv_sites.h"
#include "rv_ui.h"

const struct rv_api *api;
rv_extension_handle self;

enum { MODE = 1 };
static struct rv_timer *timer;
static int has_zone;

static void tick(void *unused)
{
    (void)unused;
    struct schedule s;
    schedule_read(&s);
    night_shift_follow(&s);
    int known = s.state != SCHEDULE_NO_ZONE;
    if (known != has_zone) {
        has_zone = known;
        api->ui_pane_refresh(self);
    }
}

static int first_load = 1;

static void changed(uint16_t key)
{
    if (key == RV_SETTING_KEY_ALL) {
        int import = first_load;
        first_load = 0;
        migrate_switch(api, self, import, MODE, 0x5448474e, 0x31544e53, NIGHT_SHIFT_ON,
                       NIGHT_SHIFT_OFF, RV_SETTING_TYPE_INT);
    }
    int mode = api->setting_get_int(self, MODE);
    night_shift_set(mode);
    if (mode != NIGHT_SHIFT_AUTO) {
        rv_timer_cancel(api, self, &timer);
    } else if (!timer) {
        timer = api->timer_start(self, 60000, 1, tick, NULL);
        /* The time zone service can become available after preferences load. */
        api->timer_start(self, 1000, 0, tick, NULL);
    }
}

static const char *caption(const struct rv_context *ctx)
{
    (void)ctx;
    return night_shift_caption();
}

static const struct rv_setting settings[] = {
    RV_SETTING_CHOICE(MODE, "Night Shift", NIGHT_SHIFT_OFF, night_shift_labels),
};
static const struct rv_screen screens[] = {
    RV_SCREEN_CHOICE("mode", "Night Shift", MODE, night_shift_labels,
                     .bitmap = RV_BITMAP("settings-night-shift")),
};
static const struct rv_menu_entry entries[] = {
    RV_MENU_ENTRY(RV_MENU_SETTINGS, RV_SETTINGS_BRIGHTNESS,
                  RV_ROW_PUSH("Night Shift", "mode"), .caption_setting = MODE,
                  .pane = RV_PANE(.caption_text = caption,
                                  .bitmap = RV_BITMAP("settings-night-shift"))),
};
static const struct rv_hook hooks[] = {
    RV_HOOK_FILTER(RV_SITE_DisplayPipe_Init_SetGammaTables_call,
                   night_shift_init_gamma),
};
static const struct rv_descriptor extension = {
    .size = sizeof extension,
    RV_SETTINGS(settings),
    RV_SCREENS(screens),
    RV_ENTRIES(entries),
    RV_HOOKS(hooks),
    .setting_changed = changed,
    .unload = night_shift_stop,
};

const struct rv_descriptor *rv_module_entry(const struct rv_api *core,
                                            rv_extension_handle handle)
{
    api = core;
    self = handle;
    return &extension;
}
