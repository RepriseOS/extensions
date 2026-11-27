/* SPDX-License-Identifier: GPL-3.0-only */

/* Play Next and Play Last in music and audio podcast episode context menus,
 * and Up Next: pending items in Music, the Main menu and the
 * Now Playing context menu.
 *
 * The Play Last flag around Add to On-The-Go is adapted from RepriseOS
 * payload/queue_selection.c (11e5ba7, c997a51) by brilliant-alien and lgreitis.
 * Up Next is based on RepriseOS payload/up_next.c (e501359) by Alex Ball. */

#include "queue.h"
#include "../file-info/service.h"
#include "../shared/menu_visibility.h"
#include "osos-2.0.5.h"
#include "rv_api.h"
#include "rv_media.h"
#include "rv_menus.h"
#include "rv_sites.h"
#include "rv_ui.h"
#include <stddef.h>

#define KEY_IN_MUSIC 1 /* Settings > Music Menu and Main Menu flip these two */
#define KEY_IN_MAIN 2

static const struct rv_api *api;
static rv_extension_handle self;

/* Play Last flag of the Add to On-The-Go in progress, -1 when none is. */
static int32_t adding = -1;

/* Album and artist rows reuse Add to On-The-Go: it filters the library to the
 * selection and hands it to the On-The-Go playlist here. */
static uint32_t on_the_go_add(uint32_t otg, uint32_t row, uint32_t browser,
                              uint32_t save, const void *next)
{
    if (adding < 0)
        return api->hook_next(next, otg, row, browser, save, 0, 0);
    if ((int32_t)row == -1)
        queue_add_browser((void *)browser, adding);
    return 1;
}

static void add_selection(const struct rv_context *ctx, int last)
{
    adding = last;
    MediaListController_AddSelectionToOnTheGo(ctx->controller);
    adding = -1;
}

static void song_next(const struct rv_context *ctx)
{
    queue_add_item(ctx->item, 0);
}

static void song_last(const struct rv_context *ctx)
{
    queue_add_item(ctx->item, 1);
}

static void selection_next(const struct rv_context *ctx)
{
    add_selection(ctx, 0);
}

static void selection_last(const struct rv_context *ctx)
{
    add_selection(ctx, 1);
}

static int available(const struct rv_context *ctx)
{
    (void)ctx;
    return queue_available();
}

static int can_add(const struct rv_context *ctx)
{
    (void)ctx;
    return queue_can_add();
}

static int can_add_item(const struct rv_context *ctx)
{
    return queue_can_add_item(ctx->item);
}

static uint32_t toggle_stopped(uint32_t player, const void *next)
{
    if (queue_start_idle())
        return 1;
    return api->hook_next(next, player, 0, 0, 0, 0, 0);
}

static int upcoming(const struct rv_context *ctx)
{
    (void)ctx;
    return queue_rows() != 0;
}

/* Up Next */

static char title[128], artist[128];
static struct rv_timer *refresh_timer;
static uint32_t queue_views;

static void refresh_queue(void *arg)
{
    (void)arg;
    if (queue_changed())
        api->ui_refresh(self, "queue");
}

static void queue_opened(const struct rv_context *ctx)
{
    (void)ctx;
    if (queue_views++)
        return;
    queue_changed();
    refresh_timer = api->timer_start(self, 500, 1, refresh_queue, NULL);
}

static void queue_closed(const struct rv_context *ctx)
{
    (void)ctx;
    if (--queue_views)
        return;
    rv_timer_cancel(api, self, &refresh_timer);
}

static uint32_t song_count(const struct rv_context *ctx)
{
    (void)ctx;
    return queue_rows();
}

static const char *song_row(const struct rv_context *ctx, uint32_t index,
                            const char **detail)
{
    (void)ctx;
    queue_text(index, 0, title, sizeof title);
    queue_text(index, 1, artist, sizeof artist);
    *detail = artist;
    return title;
}

static uint32_t song_first(const struct rv_context *ctx)
{
    (void)ctx;
    return 0;
}

static int song_select(const struct rv_context *ctx, uint32_t index)
{
    queue_play(index);
    void *created = ControllerRegistry_InvokeControllerFactory(
        UI_GetControllerRegistry(), "TMediaNowPlayingCntlr", "TMediaNowPlayingCntlr");
    if (created)
        rv_controller_push(ctx->controller, created);
    api->ui_select(self, "queue", 0);
    return 0;
}

static int can_remove(const struct rv_context *ctx)
{
    return ctx->row >= 0 && queue_capture(ctx->row);
}

static int can_move_up(const struct rv_context *ctx)
{
    return ctx->row > 0 && can_remove(ctx);
}

static int can_move_down(const struct rv_context *ctx)
{
    return can_remove(ctx) && (uint32_t)ctx->row + 1 < queue_rows();
}

static void move_top(const struct rv_context *ctx)
{
    queue_move(ctx->row, 0);
    api->ui_refresh(self, "queue");
}

static void move_up(const struct rv_context *ctx)
{
    if (ctx->row > 0)
        queue_move(ctx->row, ctx->row - 1);
    api->ui_refresh(self, "queue");
}

static void move_down(const struct rv_context *ctx)
{
    if (ctx->row >= 0)
        queue_move(ctx->row, ctx->row + 1);
    api->ui_refresh(self, "queue");
}

static void move_bottom(const struct rv_context *ctx)
{
    uint32_t count = queue_rows();
    if (count)
        queue_move(ctx->row, count - 1);
    api->ui_refresh(self, "queue");
}

static void remove_song(const struct rv_context *ctx)
{
    queue_remove(ctx->row);
    api->ui_refresh(self, "queue");
    api->ui_select(self, "queue", ctx->row > 0 ? (uint32_t)ctx->row - 1 : 0);
}

/* Browse Album, Artist and Compilation as the stock song list offers them:
 * not for audiobooks, videos, iTunes U or records with flag 8F01, and a
 * compilation with preference 0x60bc set gets Browse Compilation in place
 * of the other two. The name goes to the navigator as
 * the pending selection; the stock menus follow it down from the main
 * screen. */
enum browse { BROWSE_NONE, BROWSE_ALBUM_ARTIST, BROWSE_COMPILATION };

static enum browse browse_kind(const struct rv_context *ctx)
{
    void *record =
        ctx->row >= 0 && queue_capture(ctx->row) ? queue_record(ctx->row) : NULL;
    if (!record)
        return BROWSE_NONE;
    _Alignas(uint32_t) uint8_t storage[MediaItem_bytes];
    struct MediaItem *item =
        MediaItem_InitFromRecord((struct MediaItem *)storage, record, 0);
    enum browse kind = BROWSE_NONE;
    if (!MediaItem_HasRecordFlag8F01(item) && !MediaItem_HasKind200000(item) &&
        !MediaItem_HasKind8(item) && !MediaItem_HasKind8062(item)) {
        void *preferences = PreferencesService_Get();
        kind = MediaItem_IsCompilation(item) && preferences &&
                       PreferencesService_Get60BC(preferences)
                   ? BROWSE_COMPILATION
                   : BROWSE_ALBUM_ARTIST;
    }
    MediaItem_Destroy(item);
    return kind;
}

static int can_browse_pair(const struct rv_context *ctx)
{
    return browse_kind(ctx) == BROWSE_ALBUM_ARTIST;
}

static int can_browse_compilation(const struct rv_context *ctx)
{
    return browse_kind(ctx) == BROWSE_COMPILATION;
}

/* ARM: Runtime_OperatorNew returns with mov pc, lr. The screen that takes
 * the selection deletes the copy. */
RV_ARM static void request(const char *key, const struct NativeString *name)
{
    struct NativeString *copy = Runtime_OperatorNew(NativeString_bytes);
    if (!copy)
        return;
    NativeString_InitCopy(copy, name);
    _Alignas(uint32_t) uint8_t storage[CppString_bytes];
    CppString_Init(storage, key);
    NavigatorProperties_SetPointer(NavigatorProperties, storage, copy);
    CppString_Destroy(storage);
}

static void browse(const struct rv_context *ctx, const char *key, int artist)
{
    void *record = ctx->row >= 0 ? queue_record(ctx->row) : NULL;
    if (!record)
        return;
    _Alignas(uint32_t) uint8_t storage[MediaItem_bytes];
    _Alignas(uint32_t) uint8_t text[NativeString_bytes];
    struct MediaItem *item =
        MediaItem_InitFromRecord((struct MediaItem *)storage, record, 0);
    struct NativeString *name = (struct NativeString *)text;
    NativeString_InitEmpty(name);
    if (artist)
        MediaItem_GetArtist(item, name);
    else
        MediaItem_GetAlbum(item, name);
    request(key, name);
    Navigator_PopToMainScreenOnly();
    NativeString_Destroy(name);
    MediaItem_Destroy(item);
}

static void browse_album(const struct rv_context *ctx)
{
    browse(ctx, "SelectAlbum", 0);
}

static void browse_artist(const struct rv_context *ctx)
{
    browse(ctx, "SelectArtist", 1);
}

static void browse_compilation(const struct rv_context *ctx)
{
    browse(ctx, "SelectCompilation", 0);
}

static const struct file_info_service *file_info(void)
{
    return api->extension_service(FILE_INFO_ID);
}

static int can_show_info(const struct rv_context *ctx)
{
    return ctx->row >= 0 && file_info() && queue_capture(ctx->row);
}

static void show_info(const struct rv_context *ctx)
{
    const struct file_info_service *info = file_info();
    void *record = ctx->row >= 0 ? queue_record(ctx->row) : NULL;
    if (info && record)
        info->open(record);
}

static const struct rv_context_row song_menu[] = {
    RV_CONTEXT_ROW(RV_ROW_ACTION("Move to Top", move_top), .available = can_move_up),
    RV_CONTEXT_ROW(RV_ROW_ACTION("Move Up", move_up), .available = can_move_up),
    RV_CONTEXT_ROW(RV_ROW_ACTION("Move Down", move_down), .available = can_move_down),
    RV_CONTEXT_ROW(RV_ROW_ACTION("Move to Bottom", move_bottom),
                   .available = can_move_down),
    RV_CONTEXT_ROW(RV_ROW_ACTION("Remove", remove_song), .available = can_remove),
    RV_CONTEXT_ROW(RV_ROW_ACTION("Browse Album", browse_album),
                   .available = can_browse_pair),
    RV_CONTEXT_ROW(RV_ROW_ACTION("Browse Artist", browse_artist),
                   .available = can_browse_pair),
    RV_CONTEXT_ROW(RV_ROW_ACTION("Browse Compilation", browse_compilation),
                   .available = can_browse_compilation),
    RV_CONTEXT_ROW(RV_ROW_ACTION("File Info", show_info), .available = can_show_info),
    RV_CONTEXT_ROW(RV_ROW_PUSH("Clear Up Next", "clear"), .available = upcoming),
};

/* Clear is button 0. */
static int clear_chosen(const struct rv_context *ctx, uint32_t index)
{
    (void)ctx;
    if (index == 0) {
        queue_clear();
        api->ui_refresh(self, "queue");
    }
    return 1;
}

static const char *const clear_buttons[] = {"Clear", "Cancel"};

static const struct rv_screen screens[] = {
    RV_SCREEN_DYNAMIC_LIST(
        "queue", "Up Next", song_count, song_row, .dynamic.select = song_select,
        .layout = RV_LAYOUT_FULL, .dynamic.detail = RV_DETAIL_BELOW,
        .dynamic.initial = song_first, .dynamic.empty = "No Upcoming Items",
        RV_SCREEN_CONTEXT(song_menu), .opened = queue_opened, .closed = queue_closed),
    RV_SCREEN_DIALOG("clear", "Up Next", "Clear Up Next?",
                     "The items after the current one are removed from the queue.",
                     clear_buttons, clear_chosen, .dialog.initial = 1),
};

/* The menus that hand over one song, and the ones that hand over an album or
 * artist. */
#define ITEM_MENUS                                                                     \
    (RV_MENU_BIT(RV_MENU_SONG_CONTEXT) | RV_MENU_BIT(RV_MENU_PODCAST_EPISODE_CONTEXT))
#define SELECTION_MENUS                                                                \
    (RV_MENU_BIT(RV_MENU_ALBUM_CONTEXT) | RV_MENU_BIT(RV_MENU_ARTIST_CONTEXT))

static const struct rv_menu_entry entries[] = {
    RV_MENUS_ENTRY(ITEM_MENUS, 0, RV_ROW_ACTION("Play Next", song_next),
                   .available = can_add_item),
    RV_MENUS_ENTRY(ITEM_MENUS, 0, RV_ROW_ACTION("Play Last", song_last),
                   .available = can_add_item),
    RV_MENUS_ENTRY(SELECTION_MENUS, 0, RV_ROW_ACTION("Play Next", selection_next),
                   .available = can_add),
    RV_MENUS_ENTRY(SELECTION_MENUS, 0, RV_ROW_ACTION("Play Last", selection_last),
                   .available = can_add),
    RV_MENU_ENTRY(RV_MENU_NOW_PLAYING_CONTEXT, 0, RV_ROW_PUSH("Up Next", "queue"),
                  .available = available),
    RV_MENU_ENTRY(RV_MENU_NOW_PLAYING_CONTEXT, 0, RV_ROW_PUSH("Clear Up Next", "clear"),
                  .available = upcoming),
    RV_MENU_ENTRY(RV_MENU_MUSIC, RV_MUSIC_AUDIOBOOKS, RV_ROW_PUSH("Up Next", "queue"),
                  .shown_by = KEY_IN_MUSIC),
    RV_MENU_ENTRY(RV_MENU_MAIN, RV_MAIN_AUDIOBOOKS, RV_ROW_PUSH("Up Next", "queue"),
                  .shown_by = KEY_IN_MAIN, .preview = RV_PREVIEW_MUSIC),
};

static const struct rv_setting settings[] = {
    RV_SETTING_BOOL(KEY_IN_MUSIC, "Up Next in Music", 1),
    RV_SETTING_BOOL(KEY_IN_MAIN, "Up Next in Main Menu", 0),
};

/* The old core's built-in Up Next row in its menu visibility record. */
#define OLD_MENU_KEY 0x857127e6

static int first_load = 1;

static void changed(uint16_t key)
{
    if (key == RV_SETTING_KEY_ALL) {
        int import = first_load;
        first_load = 0;
        migrate_menu_visibility(api, self, import, OLD_MENU_KEY, KEY_IN_MUSIC, 1,
                                KEY_IN_MAIN, 0);
    }
}

static const struct rv_hook hooks[] = {
    RV_HOOK_FILTER(RV_SITE_MediaPlayer_ToggleStopped_call, toggle_stopped),
    RV_HOOK_FILTER(RV_SITE_MediaList_OnTheGoAdd_call, on_the_go_add),
};

static const struct rv_descriptor extension = {
    .size = sizeof extension,
    RV_HOOKS(hooks),
    RV_SETTINGS(settings),
    .setting_changed = changed,
    RV_SCREENS(screens),
    RV_ENTRIES(entries),
};

const struct rv_descriptor *rv_module_entry(const struct rv_api *core,
                                            rv_extension_handle handle)
{
    api = core;
    self = handle;
    return &extension;
}
