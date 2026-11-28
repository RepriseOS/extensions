/* SPDX-License-Identifier: GPL-3.0-only */

/* Apple's importer discards the Album Artist (mhod 22), Sort Artist (23) and
 * Sort Album Artist (29) strings. These hooks intern them in the TrackData's
 * artist cache instead and keep one 20-byte row per song that needs one: a
 * song whose Album Artist is its Artist and that carries no Sort As stores
 * nothing, because the browser resolves a missing Album Artist to the Artist.
 * While the import runs the rows collect in fixed chunks; when it ends they
 * move into one exact-size block, each chunk freed as it is copied, and the
 * block is heapsorted by persistent id for binary search.
 *
 * The mhods of one mhit are parsed before the next mhit begins, so one
 * pending record collects a song's values and is flushed when another record
 * arrives and when the dataset parse returns; by then the record's own Artist
 * token is final. No token is ever released: a string the cache already holds
 * comes back as the same token without a further reference, so one release by
 * anyone frees it for every record that names it. The strings live as long as
 * the TrackData, whose destruction drops them all.
 *
 * Based on RepriseOS payload/album_artist.c (bfedd72) by lgreitis. */

#include "fields.h"
#include "browser.h"
#include "shared.h"
#include "osos-2.0.5.h"
#include "rv_api.h"
#include <stdint.h>

enum { TOKEN, SORT_ALBUM_ARTIST, SORT_ARTIST, TOKENS };

#define CHUNK_ROWS 256

struct row {
    uint32_t id_low, id_high;
    uint32_t tokens[TOKENS];
};

/* The rows of an import in progress, newest chunk first. */
struct chunk {
    struct chunk *next;
    uint32_t count;
    struct row rows[CHUNK_ROWS];
};

static struct {
    void *owner; /* the TrackData of the import, NULL before the first */
    void *cache; /* its artist string cache */
    struct chunk *chunks;
    struct row *rows; /* the sorted table once sealed */
    uint32_t count;
    uint8_t sealed; /* a later mhod only skips its bytes */

    struct {
        const void *record;
        uint32_t tokens[TOKENS];
    } pending;
} table;

static void drop_chunks(void)
{
    while (table.chunks) {
        struct chunk *next = table.chunks->next;
        api->free(self, table.chunks);
        table.chunks = next;
    }
}

static void clear(void)
{
    if (table.owner)
        browser_invalidate(table.owner);
    drop_chunks();
    if (table.rows)
        api->free(self, table.rows);
    table.rows = NULL;
    table.count = 0;
    table.sealed = 0;
    table.pending.record = NULL;
    for (unsigned i = 0; i < TOKENS; i++)
        table.pending.tokens[i] = 0;
}

static const uint32_t *persistent_id(const void *record)
{
    return &RV_FIELD(record, TrackData_dbid, const uint32_t);
}

static int before(const struct row *row, const uint32_t *id)
{
    return row->id_high < id[1] || (row->id_high == id[1] && row->id_low < id[0]);
}

static void append(const void *record, const uint32_t *tokens)
{
    struct chunk *chunk = table.chunks;
    if (!chunk || chunk->count == CHUNK_ROWS) {
        chunk = api->alloc(self, sizeof *chunk);
        if (!chunk)
            return;
        chunk->next = table.chunks;
        table.chunks = chunk;
    }
    struct row *row = &chunk->rows[chunk->count++];
    table.count++;
    const uint32_t *id = persistent_id(record);
    row->id_low = id[0];
    row->id_high = id[1];
    for (unsigned i = 0; i < TOKENS; i++)
        row->tokens[i] = tokens[i];
}

static void flush(void)
{
    const void *record = table.pending.record;
    uint32_t *tokens = table.pending.tokens;
    if (!record)
        return;
    table.pending.record = NULL;
    /* Nothing to keep when the Album Artist is the Artist or empty and no
     * Sort As came with it. */
    uint32_t token = tokens[TOKEN];
    int same = (!token || token == RV_FIELD(record, MediaRecord_artist, uint32_t)) &&
               !tokens[SORT_ALBUM_ARTIST] && !tokens[SORT_ARTIST];
    if (!same)
        append(record, tokens);
    for (unsigned i = 0; i < TOKENS; i++)
        tokens[i] = 0;
}

static void sift(struct row *rows, uint32_t root, uint32_t count)
{
    while (root < count / 2) {
        uint32_t child = root * 2 + 1;
        if (child + 1 < count && before(&rows[child], &rows[child + 1].id_low))
            child++;
        if (!before(&rows[root], &rows[child].id_low))
            break;
        struct row swap = rows[root];
        rows[root] = rows[child];
        rows[child] = swap;
        root = child;
    }
}

/* The chunks into one exact-size block, heapsorted by persistent id in place.
 * Without memory for the block the import's rows are dropped. */
static void finish(void)
{
    uint32_t count = table.count;
    struct row *rows = count ? api->alloc(self, count * sizeof *rows) : NULL;
    uint32_t at = 0;
    while (table.chunks) {
        struct chunk *chunk = table.chunks;
        if (rows)
            for (uint32_t i = 0; i < chunk->count; i++)
                rows[at++] = chunk->rows[i];
        table.chunks = chunk->next;
        api->free(self, chunk);
    }
    table.rows = rows;
    table.count = rows ? count : 0;
    table.sealed = 1;
    if (!rows)
        return;
    for (uint32_t i = count / 2; i; i--)
        sift(rows, i - 1, count);
    for (uint32_t i = count; i > 1; i--) {
        struct row swap = rows[0];
        rows[0] = rows[i - 1];
        rows[i - 1] = swap;
        sift(rows, 0, i - 1);
    }
}

void fields_import_begin(void *reader)
{
    clear();
    table.owner = RV_FIELD(reader, ITunesReader_owner, void *);
    table.cache = (uint8_t *)table.owner + TrackData_artist_cache;
}

void fields_import_end(int status)
{
    flush();
    if (status)
        clear();
    else
        finish();
}

int fields_import_mhod(void *reader, uint32_t body_bytes, void *record,
                       uint32_t mhod_type)
{
    /* Apple's skip treats a negative length as a seek back. */
    if (body_bytes > INT32_MAX)
        return -0xd0;
    if (!table.owner || table.sealed || body_bytes < 16)
        return ITunesDB_SkipImportBytes(reader, (int32_t)body_bytes);
    void *file = RV_FIELD(reader, ITunesReader_file, void *);
    uint64_t start, end;
    if (BufferedFile_GetPosition(file, &start))
        return ITunesDB_SkipImportBytes(reader, (int32_t)body_bytes);
    /* Apple's own arguments for an artist string: UTF-16 in the file, the
     * artist cache's glyph collection. */
    uint32_t token = 0;
    int status =
        ITunesDB_ImportCachedString(reader, 1, mhod_type, table.cache, &token, 1, 1);
    int cursor = BufferedFile_GetPosition(file, &end);
    if (cursor || end < start || end - start > body_bytes)
        return cursor ? cursor : -0xd0;
    /* The padding the decode left, also after a failed optional field. */
    int trailing = ITunesDB_SkipImportBytes(
        reader, (int32_t)(body_bytes - (uint32_t)(end - start)));
    if (trailing)
        return trailing;
    if (status)
        token = 0;
    if (table.pending.record != record)
        flush();
    table.pending.record = record;
    unsigned which = mhod_type == MHOD_SORT_ALBUM_ARTIST ? SORT_ALBUM_ARTIST
                     : mhod_type == MHOD_SORT_ARTIST     ? SORT_ARTIST
                                                         : TOKEN;
    table.pending.tokens[which] = token;
    return 0;
}

void fields_destroy(void *track_data)
{
    if (track_data == table.owner) {
        clear();
        table.owner = table.cache = NULL;
    }
}

int fields_imported(void)
{
    return table.owner != NULL;
}

static const struct row *lookup(const void *record)
{
    if (!record || !table.sealed || !table.count ||
        *(void *const *)record != table.owner)
        return NULL;
    const uint32_t *id = persistent_id(record);
    uint32_t low = 0, high = table.count;
    while (low < high) {
        uint32_t middle = low + (high - low) / 2;
        if (before(&table.rows[middle], id))
            low = middle + 1;
        else
            high = middle;
    }
    const struct row *row = &table.rows[low];
    return low < table.count && row->id_high == id[1] && row->id_low == id[0] ? row
                                                                              : NULL;
}

uint32_t fields_token(const void *record)
{
    const struct row *row = lookup(record);
    return row ? row->tokens[TOKEN] : 0;
}

uint32_t fields_sort_token(const void *record, int album_artist)
{
    const struct row *row = lookup(record);
    return row ? row->tokens[album_artist ? SORT_ALBUM_ARTIST : SORT_ARTIST] : 0;
}

uint32_t fields_read(const void *record, uint16_t *text, uint32_t max_units)
{
    uint32_t token = fields_token(record);
    if (!token || token == RV_FIELD(record, MediaRecord_artist, uint32_t) ||
        !max_units || max_units > INT32_MAX / 2)
        return 0;
    uint32_t bytes = 0;
    if (StringCache_ReadToken(table.cache, token, text, &bytes, max_units * 2))
        return 0;
    uint32_t units = bytes / 2;
    if (units && text[units - 1] >= 0xd800 && text[units - 1] <= 0xdbff)
        units--;
    return units;
}

uint32_t fields_count(void)
{
    return table.count;
}
