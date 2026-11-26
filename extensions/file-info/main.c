/* SPDX-License-Identifier: GPL-3.0-only */

/* File Info in the context menus of songs, podcast and iTunes U episodes,
 * audiobooks, movies, TV episodes, video playlists and Now Playing: the item's
 * tags and file properties as a list of label and value rows.
 *
 * Based on RepriseOS payload/file_info.c (acbb928) by lgreitis and paviro. */

#include "info.h"
#include "service.h"
#include "osos-2.0.5.h"
#include "rv_api.h"
#include "rv_ui.h"
#include <stddef.h>

static const struct rv_api *api;
static rv_extension_handle self;

/* The item File Info shows when another extension opened it, held until the
 * screen closes. */
static uint32_t opened[MediaItem_bytes / 4];
static int holding;

/* The menu's item, or the one the service opened. */
static const void *item_of(const struct rv_context *ctx)
{
    return ctx->arg ? ctx->arg : ctx->item;
}

static int has_item(const struct rv_context *ctx)
{
    return ctx->item != NULL;
}

static uint32_t count(const struct rv_context *ctx)
{
    return info_rows(item_of(ctx));
}

static const char *row(const struct rv_context *ctx, uint32_t index,
                       const char **detail)
{
    return info_row(item_of(ctx), index, detail);
}

static uint32_t first(const struct rv_context *ctx)
{
    (void)ctx;
    return 0;
}

static void release(void)
{
    if (holding)
        MediaItem_Destroy((struct MediaItem *)opened);
    holding = 0;
}

static void closed(const struct rv_context *ctx)
{
    (void)ctx;
    info_release();
    release();
}

/* closed is not called when the extension unloads under the screen. */
static void unload(void)
{
    info_release();
    release();
}

static int open(void *record)
{
    release();
    info_release();
    MediaItem_InitFromRecord((struct MediaItem *)opened, record, 0);
    holding = 1;
    int status = api->ui_push(self, "info", opened);
    if (status != RV_OK)
        release();
    return status;
}

static const struct file_info_service service = {open};

static const struct rv_screen screens[] = {
    RV_SCREEN_DYNAMIC_LIST("info", "File Info", count, row, .layout = RV_LAYOUT_FULL,
                           .dynamic.detail = RV_DETAIL_BELOW, .dynamic.initial = first,
                           .closed = closed),
};

/* Every context menu that hands over an item. */
#define ITEM_MENUS                                                                     \
    (RV_MENU_BIT(RV_MENU_SONG_CONTEXT) | RV_MENU_BIT(RV_MENU_NOW_PLAYING_CONTEXT) |    \
     RV_MENU_BIT(RV_MENU_PODCAST_EPISODE_CONTEXT) |                                    \
     RV_MENU_BIT(RV_MENU_ITUNES_U_EPISODE_CONTEXT) |                                   \
     RV_MENU_BIT(RV_MENU_AUDIOBOOK_CONTEXT) | RV_MENU_BIT(RV_MENU_MOVIE_CONTEXT) |     \
     RV_MENU_BIT(RV_MENU_TV_EPISODE_CONTEXT) |                                         \
     RV_MENU_BIT(RV_MENU_VIDEO_PLAYLIST_CONTEXT))

static const struct rv_menu_entry entries[] = {
    RV_MENUS_ENTRY(ITEM_MENUS, 0, RV_ROW_PUSH("File Info", "info"),
                   .available = has_item),
};

static const struct rv_descriptor extension = {
    .size = sizeof extension,
    RV_SCREENS(screens),
    RV_ENTRIES(entries),
    .unload = unload,
    .service = &service,
};

const struct rv_descriptor *rv_module_entry(const struct rv_api *table,
                                            rv_extension_handle handle)
{
    api = table;
    self = handle;
    info_init(api);
    return &extension;
}
