/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "osos-2.0.5.h"
#include "rv_api.h"
#include "../fields.c"

/* The host doubles as the core: counted blocks from alloc and free. */
static size_t live_blocks, live_bytes, peak_bytes;
static int fail_alloc;

union block {
    max_align_t align;
    size_t bytes;
};

static void *host_alloc(rv_extension_handle self, uint32_t bytes)
{
    (void)self;
    if (fail_alloc)
        return NULL;
    union block *block = calloc(1, sizeof *block + bytes);
    assert(block);
    block->bytes = bytes;
    live_blocks++;
    live_bytes += bytes;
    if (live_bytes > peak_bytes)
        peak_bytes = live_bytes;
    return block + 1;
}

static void host_free(rv_extension_handle self, void *memory)
{
    (void)self;
    union block *block = (union block *)memory - 1;
    assert(live_blocks);
    live_blocks--;
    live_bytes -= block->bytes;
    free(block);
}
static const struct rv_api host_api = {.alloc = host_alloc, .free = host_free};
const struct rv_api *api = &host_api;
rv_extension_handle self;

/* The artist cache: a string the cache holds comes back as its token with
 * no further reference, so the table never releases one. */
static struct {
    const uint16_t *text;
    uint32_t units;
} tokens[512];

static uint32_t token_count = 1;
static uint8_t track_data[TrackData_artist_cache + 0x58];
static void *cache = track_data + TrackData_artist_cache;

int StringCache_ReadToken(void *c, uint32_t token, void *out, uint32_t *bytes,
                          uint32_t maximum)
{
    assert(c == cache && token && token < token_count);
    *bytes = tokens[token].units * 2 < maximum ? tokens[token].units * 2 : maximum;
    memcpy(out, tokens[token].text, *bytes);
    return 0;
}

static uint32_t intern(const uint16_t *text, uint32_t units)
{
    for (uint32_t i = 1; i < token_count; i++)
        if (tokens[i].units == units && !memcmp(tokens[i].text, text, units * 2))
            return i;
    assert(token_count < sizeof tokens / sizeof tokens[0]);
    tokens[token_count].text = text;
    tokens[token_count].units = units;
    return token_count++;
}

/* The reader: a file cursor and the mhod body under it. */
static struct {
    uint64_t offset;
    const uint16_t *text;
    uint32_t units;
    int decode_error, skip_error, position_error;
} file;

static _Alignas(16) uint8_t reader[ITunesReader_owner + 8];

int ITunesDB_ImportCachedString(void *r, uint32_t encoding, uint32_t type, void *c,
                                uint32_t *token, uint32_t glyph_field, int collect)
{
    assert(r == reader && encoding == 1 && (type == 22 || type == 23 || type == 29) &&
           c == cache && glyph_field == 1 && collect == 1);
    if (file.decode_error) {
        file.offset += 8;
        return file.decode_error;
    }
    file.offset += 16 + file.units * 2;
    *token = file.units ? intern(file.text, file.units) : 0;
    return 0;
}

int ITunesDB_SkipImportBytes(void *r, int bytes)
{
    assert(r == reader);
    if (file.skip_error)
        return file.skip_error;
    assert(bytes >= 0);
    file.offset += (uint32_t)bytes;
    return 0;
}

int BufferedFile_GetPosition(void *f, uint64_t *position)
{
    assert(f == RV_FIELD(reader, ITunesReader_file, void *));
    if (file.position_error)
        return file.position_error;
    *position = file.offset;
    return 0;
}

static int invalidated;

void browser_invalidate(void *owner)
{
    assert(owner == track_data);
    invalidated++;
}

/* Records: the owner word, the Artist token and the persistent id. */
static uint8_t records[64][TrackData_dbid + 8];

static void *record(unsigned i, uint32_t artist, uint32_t id_low, uint32_t id_high)
{
    uint8_t *r = records[i];
    memset(r, 0, sizeof records[i]);
    RV_FIELD(r, 0, void *) = track_data;
    RV_FIELD(r, MediaRecord_artist, uint32_t) = artist;
    RV_FIELD(r, TrackData_dbid, uint32_t) = id_low;
    RV_FIELD(r, TrackData_dbid + 4, uint32_t) = id_high;
    return r;
}

#define U(...) ((const uint16_t[]){__VA_ARGS__})
static const uint16_t artist_a[] = {'A', 'r', 't', 'i', 's', 't', ' ', 'A'};
static const uint16_t various[] = {'V', 'a', 'r', 'i', 'o', 'u', 's'};
static const uint16_t sort_x[] = {'X'};

/* One mhod of `type` with `text` for `record`, through the discard hook. */
static int mhod(void *r, uint32_t type, const uint16_t *text, uint32_t units)
{
    file.text = text;
    file.units = units;
    uint64_t before = file.offset;
    uint32_t body = 16 + units * 2 + 4; /* with retained padding */
    int status = fields_import_mhod(reader, body, r, type);
    if (!status)
        assert(file.offset == before + body);
    return status;
}

static void begin(void)
{
    RV_FIELD(reader, ITunesReader_file, void *) = &file;
    RV_FIELD(reader, ITunesReader_owner, void *) = track_data;
    memset(&file, 0, sizeof file);
    fields_import_begin(reader);
}

static void expect_text(const void *r, const uint16_t *text, uint32_t units)
{
    uint16_t out[64];
    uint32_t got = fields_read(r, out, 64);
    assert(got == units && !memcmp(out, text, units * 2));
}

static void test_flush_rules(void)
{
    begin();
    uint32_t a = intern(artist_a, 8); /* the record's own Artist token */
    void *same = record(0, a, 1, 0);
    void *differs = record(1, a, 2, 0);
    void *sort_only = record(2, a, 3, 0);
    void *same_sorted = record(3, a, 4, 0);
    void *untagged = record(4, a, 5, 0);
    void *no_artist = record(5, 0, 6, 0);
    void *empty = record(6, a, 7, 0);
    assert(!mhod(same, 22, artist_a, 8));
    assert(!mhod(empty, 22, various, 0)); /* an empty tag */
    assert(!mhod(differs, 22, various, 7));
    assert(!mhod(sort_only, 23, sort_x, 1));
    assert(!mhod(same_sorted, 22, artist_a, 8));
    assert(!mhod(same_sorted, 29, sort_x, 1));
    assert(!mhod(no_artist, 22, various, 7));
    /* The pending record flushes when another arrives, and once at the end. */
    assert(fields_count() == 3);
    fields_import_end(0);
    assert(fields_count() == 4 && fields_imported());
    /* Same-as-Artist stores nothing. */
    assert(!fields_token(same) && !fields_sort_token(same, 1) &&
           !fields_sort_token(same, 0));
    assert(fields_read(same, NULL, 0) == 0);
    assert(fields_token(differs) && fields_token(differs) != a);
    expect_text(differs, various, 7);
    assert(!fields_token(sort_only) && fields_sort_token(sort_only, 0) &&
           !fields_sort_token(sort_only, 1));
    assert(fields_token(same_sorted) == a && fields_sort_token(same_sorted, 1));
    uint16_t text[8];
    assert(fields_read(same_sorted, text, 8) == 0);
    assert(!fields_token(untagged) && !fields_token(empty));
    assert(fields_token(no_artist) == fields_token(differs));
    /* A record of another TrackData, or a stale one, answers nothing. */
    uint8_t other[TrackData_dbid + 8];
    memcpy(other, differs, sizeof other);
    RV_FIELD(other, 0, void *) = other;
    assert(!fields_token(other) && !fields_token(NULL));
    /* After the import a late mhod only skips. */
    uint32_t count = token_count;
    assert(!mhod(untagged, 22, various, 7) && token_count == count &&
           !fields_token(untagged));
    /* The table is exact: 4 rows of 20 bytes, the import chunk gone. */
    assert(live_blocks == 1 && live_bytes == 4 * 20);
    fields_destroy(track_data);
    assert(!live_blocks && invalidated == 1 && !fields_imported());
    /* Destroying another TrackData's storage does nothing to the next table. */
    begin();
    fields_destroy(reader);
    assert(fields_imported());
    fields_import_end(0);
    assert(!live_blocks);
    fields_destroy(track_data);
}

static void test_sort_and_lookup(void)
{
    begin();
    /* Ids in both halves, out of order; more than one chunk's worth comes
     * from the repeats below. */
    static const uint32_t ids[][2] = {
        {5, 1},          {1, 0},  {0xffffffff, 0}, {2, 1},  {9, 9},  {4, 0},
        {3, 0},          {7, 2},  {8, 2},          {6, 2},  {1, 1},  {0, 1},
        {0xfffffffe, 0}, {11, 0}, {10, 0},         {12, 0}, {13, 0}, {14, 0},
    };

    enum { N = sizeof ids / sizeof ids[0] };

    for (unsigned i = 0; i < N; i++)
        assert(!mhod(record(i, 0, ids[i][0], ids[i][1]), 22, various, 7));
    /* Chunk growth: 300 more ids fill a second chunk; the peak is the chunks
     * plus the exact block while it is filled. */
    for (unsigned i = 0; i < 300; i++)
        assert(!mhod(record(40 + i % 20, 0, 100 + i, 3), 22, various, 7));
    uint32_t total = N + 300;
    fields_import_end(0);
    assert(fields_count() == total && live_bytes == total * 20 && live_blocks == 1);
    assert(peak_bytes <= total * 20 + 2 * (8 + 256 * 20) + 16);
    uint32_t token = fields_token(records[0]);
    for (unsigned i = 0; i < N; i++)
        assert(fields_token(records[i]) == token);
    assert(fields_token(record(59, 0, 399, 3)) == token &&
           !fields_token(record(59, 0, 400, 3)));
    assert(!fields_token(record(60, 0, 5, 0)) && !fields_token(record(61, 0, 15, 0)) &&
           !fields_token(record(62, 0, 0, 0)));
    fields_destroy(track_data);
    assert(!live_blocks);
}

static void test_failures(void)
{
    /* A failed import drops the table. */
    begin();
    assert(!mhod(record(0, 0, 1, 0), 22, various, 7));
    assert(!mhod(record(1, 0, 2, 0), 22, various, 7));
    fields_import_end(-1);
    assert(!live_blocks && !fields_count());
    /* A body over INT32_MAX is refused; a short body, a cursor error, a decode
     * past the body and a skip error all store nothing. */
    begin();
    void *r = record(0, 0, 1, 0);
    assert(fields_import_mhod(reader, 0x80000000u, r, 22) == -0xd0);
    assert(!fields_import_mhod(reader, 8, r, 22));
    file.position_error = -3;
    assert(!mhod(r, 22, various, 7));
    file.position_error = 0;
    file.text = various;
    file.units = 7;
    assert(fields_import_mhod(reader, 16, r, 22) == -0xd0);
    file.skip_error = -4;
    assert(fields_import_mhod(reader, 16 + 14 + 2, r, 22) == -4);
    file.skip_error = 0;
    /* A decode error keeps the row's slot empty but consumes the body. */
    file.decode_error = -5;
    assert(!mhod(r, 22, various, 7));
    file.decode_error = 0;
    fields_import_end(0);
    assert(!fields_count() && !live_blocks);
    fields_destroy(track_data);
    /* Allocation failure drops the row; the table stays usable. Without
     * memory for the final block the import's rows go. */
    begin();
    fail_alloc = 1;
    assert(!mhod(record(0, 0, 1, 0), 22, various, 7));
    assert(!mhod(record(1, 0, 2, 0), 22, various, 7)); /* flushes record 0 */
    fail_alloc = 0;
    assert(!mhod(record(2, 0, 3, 0), 22, various, 7));
    fields_import_end(0);
    assert(fields_count() == 2 && fields_token(records[1]) &&
           fields_token(records[2]) && !fields_token(records[0]));
    fields_destroy(track_data);
    assert(!live_blocks);
    begin();
    assert(!mhod(record(0, 0, 1, 0), 22, various, 7));
    assert(!mhod(record(1, 0, 2, 0), 22, various, 7));
    assert(!mhod(record(2, 0, 3, 0), 22, various, 7));
    fail_alloc = 1;
    fields_import_end(0);
    fail_alloc = 0;
    assert(!fields_count() && !live_blocks && fields_imported());
    fields_destroy(track_data);
}

int main(void)
{
    test_flush_rules();
    test_sort_and_lookup();
    test_failures();
    assert(!live_blocks);
    return 0;
}
