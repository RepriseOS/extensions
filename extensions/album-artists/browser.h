/* SPDX-License-Identifier: GPL-3.0-only */

/* The Album Artists list: a browser of the extension's own on the stock
 * Artists screen, grouped by the fields table's Album Artist with the Artist
 * as the fallback. */

#ifndef ALBUM_ARTISTS_BROWSER_H
#define ALBUM_ARTISTS_BROWSER_H
#include "rv_api.h"
#include <stdint.h>

/* Opens the list above the menu of `ctx`. */
void browser_open(const struct rv_context *ctx);

/* The fields table of `track_data` is going: the owned browsers drop their rows. */
void browser_invalidate(void *track_data);

/* On unload: the owned browsers drop their rows and contexts. */
void browser_unload(void);

/* The handlers main.c binds: MediaBrowser_BuildArtists through the model's
 * slot and its two call sites, Apple's clear of the rows, the row readers,
 * the artist filter compare, the album accumulator BuildAlbums loads from
 * its literal, the album subtitle, the artwork source and provider, and
 * LibraryBrowser_Destroy. */
uint32_t build_artists_slot(uint32_t model, const void *next);
uint32_t build_artists(uint32_t browser, const void *next);
uint32_t clear_artists(uint32_t browser, const void *next);
uint32_t read_artist_name(uint32_t browser, uint32_t index, uint32_t out,
                          const void *next);
uint32_t read_artist_jump_label(uint32_t browser, uint32_t index, uint32_t out,
                                const void *next);
uint32_t artist_filter_compare(uint32_t browser, uint32_t left, uint32_t left_units,
                               uint32_t right, const void *next, uint32_t right_units,
                               uint32_t entry);
struct MediaEntry;
int album_group(void *browser, void *callback, uint32_t field_low, uint32_t field_high,
                uint32_t index, struct MediaEntry *entry, void *record, int first);
uint32_t album_subtitle(uint32_t model, uint32_t index, uint32_t out, const void *next);
uint32_t select_artwork_source(uint32_t selection, uint32_t category, const void *next);
uint32_t create_artwork_provider(uint32_t kind, uint32_t selector, const void *next);
uint32_t destroy_model(uint32_t model, const void *next);

#endif
