/* SPDX-License-Identifier: GPL-3.0-only */

/* The Album Artist, Sort Artist and Sort Album Artist of each song, kept by
 * persistent id through the iTunesDB import. Tokens name strings in the
 * TrackData's artist cache; 0 means absent. */

#ifndef ALBUM_ARTISTS_FIELDS_H
#define ALBUM_ARTISTS_FIELDS_H
#include <stdint.h>

/* The iTunesDB mhod types Apple discards. */
enum { MHOD_ALBUM_ARTIST = 22, MHOD_SORT_ARTIST = 23, MHOD_SORT_ALBUM_ARTIST = 29 };

/* The import hooks: the parse of a track dataset begins and ends, and one of
 * the mhod types Apple discards is reached with the record being filled. The
 * mhod call returns Apple's status for the import loop. */
void fields_import_begin(void *reader);
void fields_import_end(int status);
int fields_import_mhod(void *reader, uint32_t body_bytes, void *record,
                       uint32_t mhod_type);

/* Apple destroys a TrackData's storage: the table of that TrackData goes. */
void fields_destroy(void *track_data);

/* Whether an import has run on the current TrackData since the module loaded. */
int fields_imported(void);

uint32_t fields_token(const void *record);
/* The Sort Album Artist when `album_artist` is set, else the Sort Artist. */
uint32_t fields_sort_token(const void *record, int album_artist);
uint32_t fields_read(const void *record, uint16_t *text, uint32_t max_units);

/* For the host tests. */
uint32_t fields_count(void);

#endif
