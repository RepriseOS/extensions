/* SPDX-License-Identifier: GPL-3.0-only */

/* Night Light: lowers the backlight below Apple's floor of level 6 (the PMU
 * accepts 1..63), optionally dims the picture further through the gamma
 * tables, always or within a daily window that fades in and out.
 *
 * The backlight is written straight to PMU register 0x28 with Apple's own
 * pmu_write and locks (hw.c). Apple's cached level is left alone, so its
 * setter sees no change and does not undo ours; a one-second check re-applies
 * the level if anything else does. Releasing writes Apple's cached level back. */

#include "dim.h"
#include "ext.h"
#include "hw.h"
#include "osos-2.0.5.h"
#include "rv_libc.h"
#include "rv_menus.h"
#include "rv_sites.h"
#include "rv_ui.h"

const struct rv_api *api;
rv_extension_handle self;

/* Key 2 held the 0.1 gamma level; it is retired, not reused. */
enum { MODE = 1, START = 3, END = 4, BACKLIGHT = 5, EXTRA = 6 };

enum { MODE_OFF, MODE_ON, MODE_SCHEDULED };

enum { REG_LEVEL = 0x28 };

static const char *const mode_labels[] = {"Off", "On", "Scheduled"};
/* Index n > 0 is backlight level 6 - n. */
static const char *const backlight_labels[] = {"Stock", "5", "4", "3", "2", "1"};
/* Index n is n * 10 % gamma dimming. */
static const char *const extra_labels[] = {"Off", "10%", "20%", "30%",
                                           "40%", "50%", "60%", "70%"};
static const char *const hour_labels[] = {
    "00:00", "01:00", "02:00", "03:00", "04:00", "05:00", "06:00", "07:00",
    "08:00", "09:00", "10:00", "11:00", "12:00", "13:00", "14:00", "15:00",
    "16:00", "17:00", "18:00", "19:00", "20:00", "21:00", "22:00", "23:00",
};

static struct rv_timer *timer;
static int held; /* the backlight level we hold, 0 when Apple's applies */

static int local_minute(void)
{
    uint8_t now[DateTime_bytes] __attribute__((aligned(4)));
    RTC_ReadDateTime((struct DateTime *)now); /* local time: zone and DST applied */
    return now[DateTime_hour] * 60 + now[DateTime_minute];
}

static unsigned strength(void)
{
    switch (api->setting_get_int(self, MODE)) {
    case MODE_ON:
        return 256;
    case MODE_SCHEDULED:
        return dim_fraction(local_minute(), api->setting_get_int(self, START) * 60,
                            api->setting_get_int(self, END) * 60);
    default:
        return 0;
    }
}

static void release_backlight(void)
{
    if (!held)
        return;
    int stock = hw_cached_level();
    if (stock >= 1 && stock <= hw_stock_max())
        hw_write(REG_LEVEL, (uint8_t)stock);
    held = 0;
}

static void hold_backlight(int level)
{
    if (!hw_check() || level < 1 || level >= STOCK_FLOOR || level > hw_stock_max()) {
        release_backlight();
        return;
    }
    uint8_t now = 0;
    if (hw_read(REG_LEVEL, &now) != 0 || now < 1 || now > 63)
        return; /* the register did not read back sanely: leave it */
    if (now != level)
        hw_write(REG_LEVEL, (uint8_t)level);
    held = level;
}

static void update(void)
{
    unsigned s = strength();
    int choice = api->setting_get_int(self, BACKLIGHT);
    int level = choice ? dim_backlight(hw_cached_level(), 6 - choice, s) : 0;
    if (level)
        hold_backlight(level);
    else
        release_backlight();
    dim_apply((unsigned)api->setting_get_int(self, EXTRA) * 10 * s / 256);
}

static void tick(void *unused)
{
    (void)unused;
    update();
}

static void changed(uint16_t key)
{
    (void)key;
    if (api->setting_get_int(self, MODE) == MODE_OFF)
        rv_timer_cancel(api, self, &timer);
    else if (!timer)
        timer = api->timer_start(self, 1000, 1, tick, NULL);
    update();
    api->ui_pane_refresh(self);
}

static void stop(void)
{
    release_backlight();
    dim_apply(0);
}

static char caption_buffer[48];

static const char *caption(const struct rv_context *ctx)
{
    (void)ctx;
    int mode = api->setting_get_int(self, MODE);
    if (mode == MODE_OFF)
        return "Off";
    if (!hw_check())
        return "Backlight control unsupported";
    char what[24];
    int choice = api->setting_get_int(self, BACKLIGHT);
    int extra = api->setting_get_int(self, EXTRA);
    if (choice && extra)
        rv_format(what, sizeof what, "level %d + %d%%", 6 - choice, extra * 10);
    else if (choice)
        rv_format(what, sizeof what, "level %d", 6 - choice);
    else
        rv_format(what, sizeof what, "%d%% dimmer", extra * 10);
    if (mode == MODE_ON)
        rv_format(caption_buffer, sizeof caption_buffer, "On, %s", what);
    else
        rv_format(caption_buffer, sizeof caption_buffer, "%s-%s, %s",
                  hour_labels[api->setting_get_int(self, START)],
                  hour_labels[api->setting_get_int(self, END)], what);
    return caption_buffer;
}

static const struct rv_setting settings[] = {
    RV_SETTING_LABELLED(MODE, "Night Light", MODE_OFF, mode_labels),
    RV_SETTING_CHOICE(BACKLIGHT, "Backlight", 3, backlight_labels), /* level 3 */
    RV_SETTING_CHOICE(EXTRA, "Extra Dimming", 0, extra_labels),
    RV_SETTING_CHOICE(START, "From", 21, hour_labels),
    RV_SETTING_CHOICE(END, "Until", 7, hour_labels),
};

static const struct rv_row rows[] = {
    RV_ROW_SETTING("Night Light", MODE),    RV_ROW_SETTING("Backlight", BACKLIGHT),
    RV_ROW_SETTING("Extra Dimming", EXTRA), RV_ROW_SETTING("From", START),
    RV_ROW_SETTING("Until", END),
};

static const struct rv_screen screens[] = {
    RV_SCREEN_LIST("night-light", "Night Light", rows),
};

static const struct rv_menu_entry entries[] = {
    RV_MENU_ENTRY(RV_MENU_SETTINGS, RV_SETTINGS_BRIGHTNESS,
                  RV_ROW_PUSH("Night Light", "night-light"),
                  .pane = RV_PANE(.caption_text = caption,
                                  .bitmap = RV_BITMAP("settings-night-light"))),
};

static const struct rv_hook hooks[] = {
    RV_HOOK_FILTER(RV_SITE_DisplayPipe_Init_SetGammaTables_call, dim_init_gamma),
};

static const struct rv_descriptor extension = {
    .size = sizeof extension,
    RV_SETTINGS(settings),
    RV_SCREENS(screens),
    RV_ENTRIES(entries),
    RV_HOOKS(hooks),
    .setting_changed = changed,
    .unload = stop,
};

const struct rv_descriptor *rv_module_entry(const struct rv_api *core,
                                            rv_extension_handle handle)
{
    api = core;
    self = handle;
    return &extension;
}
