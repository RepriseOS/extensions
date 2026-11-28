/* SPDX-License-Identifier: GPL-3.0-only */

/* Album Artists: a row in Music and on the main menu that lists the library by
 * Album Artist, falling back to Artist, on the stock Artists screen. The
 * import hooks keep the Album Artist, Sort Artist and Sort Album Artist tags
 * Apple discards (fields.c); the list is a browser of the extension's own
 * (browser.c).
 *
 * Based on RepriseOS payload/album_artists.c (bfedd72) by lgreitis. */

#include "browser.h"
#include "fields.h"
#include "service.h"
#include "shared.h"
#include "../shared/menu_visibility.h"
#include "osos-2.0.5.h"
#include "rv_api.h"
#include "rv_menus.h"
#include "rv_sites.h"
#include "rv_ui.h"
#include <stddef.h>

const struct rv_api *api;
rv_extension_handle self;

/* The reader of the dataset parse in progress, for the mhod hooks. */
static void *importing;

/* ITunesDB_ImportTrackDataset: the table of this TrackData is built here. */
static uint32_t import_track_dataset(uint32_t reader, const void *next)
{
    fields_import_begin((void *)reader);
    importing = (void *)reader;
    uint32_t status = api->hook_next(next, reader, 0, 0, 0, 0, 0);
    importing = NULL;
    fields_import_end((int)status);
    return status;
}

/* The two discard calls of the mhod switch: (file or reader, bytes), with the
 * record and the mhod type copied from Apple's registers. */
static uint32_t discard_mhod(uint32_t target, uint32_t bytes, uint32_t record,
                             uint32_t type, const void *next)
{
    if (importing && (type == MHOD_ALBUM_ARTIST || type == MHOD_SORT_ARTIST ||
                      type == MHOD_SORT_ALBUM_ARTIST))
        return (uint32_t)fields_import_mhod(importing, bytes, (void *)record, type);
    return api->hook_next(next, target, bytes, record, type, 0, 0);
}

static uint32_t destroy_storage(uint32_t track_data, const void *next)
{
    browser_invalidate((void *)track_data);
    fields_destroy((void *)track_data);
    return api->hook_next(next, track_data, 0, 0, 0, 0, 0);
}

static void unload(void)
{
    browser_unload();
}

static const struct rv_hook hooks[] = {
    RV_HOOK_FILTER(RV_SITE_ITunesDB_ImportTrackDataset_call, import_track_dataset),
    RV_HOOK_FILTER(RV_SITE_ITunesDB_DiscardMhod_call, discard_mhod),
    RV_HOOK_FILTER(RV_SITE_ITunesDB_SkipMhod_call, discard_mhod),
    RV_HOOK_FILTER(RV_SITE_TrackData_DestroyStorage_call, destroy_storage),
    RV_HOOK_FILTER(RV_SITE_LibraryBrowser_BuildArtists_slot, build_artists_slot),
    RV_HOOK_FILTER(RV_SITE_MediaBrowser_BuildArtists_RefreshCounts_call, build_artists),
    RV_HOOK_FILTER(RV_SITE_MediaBrowser_BuildArtists_SecondaryInit_call, build_artists),
    RV_HOOK_FILTER(RV_SITE_MediaBrowser_ClearArtists_entry, clear_artists),
    RV_HOOK_FILTER(RV_SITE_MediaBrowser_ReadArtistName_entry, read_artist_name),
    RV_HOOK_FILTER(RV_SITE_MediaBrowser_ReadArtistJumpLabel_entry,
                   read_artist_jump_label),
    RV_HOOK_FILTER(RV_SITE_MediaBrowser_ArtistFilterCompare_call,
                   artist_filter_compare),
    RV_HOOK_POINTER(RV_SITE_AlbumGroup_literal, album_group),
    RV_HOOK_FILTER(RV_SITE_AlbumSubtitle_slot, album_subtitle),
    RV_HOOK_FILTER(RV_SITE_MediaListSelection_SelectArtworkSource_call,
                   select_artwork_source),
    RV_HOOK_FILTER(RV_SITE_AlbumArtworkProvider_Create_call, create_artwork_provider),
    RV_HOOK_FILTER(RV_SITE_LibraryBrowser_Destroy_entry, destroy_model),
};

static const struct rv_menu_entry entries[] = {
    RV_MENU_ENTRY(RV_MENU_MUSIC, RV_MUSIC_ARTISTS,
                  RV_ROW_ACTION("Album Artists", browser_open),
                  .shown_by = KEY_IN_MUSIC),
    RV_MENU_ENTRY(RV_MENU_MAIN, RV_MAIN_ARTISTS,
                  RV_ROW_ACTION("Album Artists", browser_open), .shown_by = KEY_IN_MAIN,
                  .preview = RV_PREVIEW_MUSIC),
};

static const struct rv_setting settings[] = {
    RV_SETTING_BOOL(KEY_IN_MUSIC, "Album Artists in Music", 1),
    RV_SETTING_BOOL(KEY_IN_MAIN, "Album Artists in Main Menu", 0),
};

static const struct album_artists_service service = {fields_read};

/* The old core's built-in Album Artists row in its menu visibility record. */
#define OLD_MENU_KEY 0xdf951f53

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

static const struct rv_descriptor extension = {
    .size = sizeof extension,
    RV_HOOKS(hooks),
    .unload = unload,
    RV_SETTINGS(settings),
    .setting_changed = changed,
    RV_ENTRIES(entries),
    .service = &service,
};

const struct rv_descriptor *rv_module_entry(const struct rv_api *core,
                                            rv_extension_handle handle)
{
    api = core;
    self = handle;
    return &extension;
}
