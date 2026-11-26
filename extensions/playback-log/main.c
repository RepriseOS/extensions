/* SPDX-License-Identifier: GPL-3.0-only */
#include "../album-artists/service.h"
#include "../shared/legacy.h"
#include "native.h"
#include "playback_log_config.h"
#include "rv_menus.h"
#include "rv_sites.h"
#include "rv_ui.h"

static const struct rv_api *api;
static rv_extension_handle self;

enum { ENABLED = 1 };

#define LOG_PATH "playback-v3.tsv"
#define LOG_VERSION "3"
/* Native track session states: 5 = counted at the natural end, 6 = finished. */
#define SESSION_PLAYED 5
#define SESSION_FINISHED 6
/* Song record field: iTunes persistent ID (64-bit). */
#define RECORD_PERSISTENT_ID_OFFSET 0x110
/* Native file object: size in bytes. */
#define FILE_SIZE_OFFSET 0x20
#define MAC_EPOCH_OFFSET 2082844800u

static char detailed_header[CFW_LOG_HEADER_MAX];
static uint32_t detailed_header_size;

struct track_session {
    uint8_t state;
    uint8_t reserved[3];
    struct osos_media_item item;
};

enum ending { ENDED_END, ENDED_SKIP, ENDED_CHANGE };

static char log_name[32] = LOG_PATH;
static const char scrobbler_path[] = ".scrobbler.log";
static const char old_config_path[] = "iPod_Control\\Device\\playback-log.cfg";
static const char scrobbler_header[] =
    "#AUDIOSCROBBLER/1.1\n#TZ/UTC\n#CLIENT/RepriseOS\n";
static struct cfw_log_config config;

static struct {
    struct track_session *object;
    struct osos_media_record *record;
    uint32_t last_tick, listened_ms, remainder, start_local, start_utc;
    unsigned running, started, finalized;
} listening;

static unsigned int enabled;
/* Set while MediaPlayer_AdvanceTrack (next/previous) switches tracks. */
static int advancing;
static char session[16];
static unsigned failed_outputs;

static struct {
    char data[2048];
    unsigned size;
} line;

static void append(const char *text)
{
    /* Six tag fields must leave room for the remaining columns and newline. */
    unsigned count = 0;
    while (count < 255 && text[count])
        count++;
    if (count == 255)
        while (count && ((unsigned char)text[count] & 0xc0) == 0x80)
            count--;
    for (unsigned i = 0; i < count && line.size + 1 < sizeof(line.data); i++) {
        char c = text[i];
        /* Keep each row on one line with tab-separated fields. */
        line.data[line.size++] = (c == '\t' || c == '\n' || c == '\r') ? ' ' : c;
    }
}

static void raw(char c)
{
    if (line.size + 1 < sizeof(line.data))
        line.data[line.size++] = c;
}

static void decimal(uint32_t value, unsigned width)
{
    char digits[11];
    unsigned start = sizeof(digits) - 1;
    digits[start] = 0;
    do {
        digits[--start] = '0' + value % 10;
        value /= 10;
    } while (value || sizeof(digits) - 1 - start < width);
    append(digits + start);
}

/* Eight uppercase hex digits, as iTunes writes persistent IDs. */
static void hex(uint32_t value)
{
    for (int shift = 28; shift >= 0; shift -= 4)
        raw("0123456789ABCDEF"[(value >> shift) & 15]);
}

static void field_number(uint32_t value)
{
    decimal(value, 1);
    raw('\t');
}

static void field_text(const char *text)
{
    append(text);
    raw('\t');
}

static void field_media_text(struct osos_media_item *item, enum osos_media_text field)
{
    struct osos_native_string string;
    osos_native_string_init(&string);
    if (field) {
        osos_media_item_text(item, field, &string);
    } else {
        const struct album_artists_service *albums =
            api->extension_service(ALBUM_ARTISTS_ID);
        uint16_t text[255];
        uint32_t units = albums ? albums->read(item->record, text, 255) : 0;
        osos_native_string_assign_utf16(&string, text, units);
    }
    const char *text = osos_native_string_data(&string);
    if (text)
        append(text);
    osos_native_string_destroy(&string);
    raw('\t');
}

/* "YYYY-MM-DD HH:MM:SS" from seconds since 1970 (Howard Hinnant's civil date). */
static void datetime(uint32_t seconds, char date_separator, char middle)
{
    uint32_t days = seconds / 86400, rest = seconds % 86400;
    uint32_t z = days + 719468, era = z / 146097, doe = z - era * 146097;
    uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
    uint32_t day = doy - (153 * mp + 2) / 5 + 1, month = mp < 10 ? mp + 3 : mp - 9;
    decimal(yoe + era * 400 + (month <= 2), 4);
    if (date_separator)
        raw(date_separator);
    decimal(month, 2);
    if (date_separator)
        raw(date_separator);
    decimal(day, 2);
    raw(middle);
    decimal(rest / 3600, 2);
    if (date_separator)
        raw(':');
    decimal(rest / 60 % 60, 2);
    if (date_separator)
        raw(':');
    decimal(rest % 60, 2);
}

/* Local clock and UTC as seconds since 1970, from one clock reading. */
static void clock_now(uint32_t *local, uint32_t *utc)
{
    /* The native clock record is at most 12 bytes; no libc memset in the payload. */
    uint32_t clock[4];
    for (unsigned i = 0; i < 4; i++)
        clock[i] = 0;
    osos_clock_read(clock);
    *local = osos_clock_seconds(clock);
    /* Mac-epoch seconds; the flag subtracts the configured time zone and DST. */
    *utc = osos_clock_mac_seconds(clock) - MAC_EPOCH_OFFSET;
}

static void utc_offset(uint32_t local, uint32_t utc)
{
    int32_t minutes = ((int32_t)(local - utc)) / 60;
    raw(minutes < 0 ? '-' : '+');
    if (minutes < 0)
        minutes = -minutes;
    decimal((uint32_t)minutes / 60, 2);
    raw(':');
    decimal((uint32_t)minutes % 60, 2);
    raw('\t');
}

static void configure_output(void)
{
    line.size = 0;
    uint32_t hash = 2166136261u;
    int custom = config.column_count != CFW_LOG_COLUMN_COUNT;
    for (unsigned i = 0; i < config.column_count; i++) {
        unsigned column = config.columns[i];
        custom |= column != i;
        field_text(cfw_log_column_names[column]);
    }
    line.data[line.size - 1] = '\n';
    detailed_header_size = line.size;
    for (unsigned i = 0; i < line.size; i++) {
        detailed_header[i] = line.data[i];
        hash = (hash ^ (uint8_t)line.data[i]) * 16777619u;
    }
    line.size = 0;
    append("playback-v3");
    if (custom) {
        raw('-');
        hex(hash);
    }
    append(".tsv");
    for (unsigned i = 0; i < line.size; i++)
        log_name[i] = line.data[i];
    log_name[line.size] = 0;
    line.size = 0;
}

/* A layout-specific filename keeps old reports; checking the header also
 * protects against collisions or a file replaced while connected over USB. */
static int detailed_header_matches(const struct osos_path *path)
{
    void *handle;
    if (osos_file_open(path, 1, &handle) != 0)
        return 0;
    if (!*(uint32_t *)((uint8_t *)handle + FILE_SIZE_OFFSET)) {
        osos_file_close(handle);
        return 0;
    }
    char data[CFW_LOG_HEADER_MAX];
    uint32_t bytes = 0;
    int valid = osos_file_read(handle, data, detailed_header_size, &bytes) == 0 &&
                bytes == detailed_header_size;
    osos_file_close(handle);
    for (unsigned i = 0; valid && i < bytes; i++)
        valid = data[i] == detailed_header[i];
    return valid ? 1 : -1;
}

static void write_line(unsigned output, const char *header, uint32_t bytes)
{
    if (failed_outputs & output)
        return;
    struct osos_path path;
    osos_path_init(&path, output == CFW_LOG_SCROBBLER ? scrobbler_path : log_name);
    int header_matches =
        output == CFW_LOG_DETAILED ? detailed_header_matches(&path) : 1;
    if (header_matches < 0) {
        failed_outputs |= output;
        return;
    }
    void *handle;
    uint32_t written = 0;
    if (osos_file_open(&path, 2, &handle) != 0)
        return;
    if (!header_matches && *(uint32_t *)((uint8_t *)handle + FILE_SIZE_OFFSET)) {
        failed_outputs |= output;
        osos_file_close(handle);
        return;
    }
    if (osos_file_seek(handle, 0, 2) == 0) {
        if (!*(uint32_t *)((uint8_t *)handle + FILE_SIZE_OFFSET) &&
            (osos_file_write(handle, header, bytes, &written) != 0 ||
             written != bytes)) {
            failed_outputs |= output;
            osos_file_close(handle);
            return;
        }
        /* A partial row cannot be retried without duplicating its prefix. */
        if (osos_file_write(handle, line.data, line.size, &written) != 0 ||
            written != line.size)
            failed_outputs |= output;
    }
    osos_file_close(handle);
}

/* Read on existing player events and transitions; subtraction tolerates one
 * wrap of the native 32-bit microsecond clock (about 71 minutes). */
static void tick(uint32_t now)
{
    if (listening.running) {
        uint32_t delta = now - listening.last_tick;
        uint32_t ms = delta / 1000;
        listening.remainder += delta % 1000;
        ms += listening.remainder / 1000;
        listening.remainder %= 1000;
        listening.listened_ms = UINT32_MAX - listening.listened_ms < ms
                                    ? UINT32_MAX
                                    : listening.listened_ms + ms;
    }
    listening.last_tick = now;
}

static void observe(struct osos_player *player)
{
    if (!enabled)
        return;
    tick(osos_playback_clock());
    struct track_session *current = player->current;
    if (current != listening.object ||
        (current && current->item.record != listening.record) ||
        (current && listening.finalized &&
         (current->state == 3 || current->state == 4))) {
        listening.object = current;
        listening.record = current ? current->item.record : NULL;
        listening.listened_ms = listening.remainder = 0;
        listening.started = listening.finalized = 0;
    }
    const uint8_t *state = (const uint8_t *)player;
    listening.running = current && !listening.finalized && state[0x93d] == 1 &&
                        !state[0x93e] && (current->state == 3 || current->state == 4);
    if (listening.running && !listening.started) {
        clock_now(&listening.start_local, &listening.start_utc);
        listening.started = 1;
    }
}

static int qualified(uint32_t length, uint32_t listened)
{
    uint32_t threshold = length / 2 + length % 2;
    if (threshold > 240000)
        threshold = 240000;
    return length > 30000 && listened >= threshold;
}

static void scrobble(struct osos_media_item *item, uint32_t length, uint32_t elapsed)
{
    line.size = 0;
    field_media_text(item, OSOS_MEDIA_ARTIST);
    field_media_text(item, OSOS_MEDIA_ALBUM);
    field_media_text(item, OSOS_MEDIA_TITLE);
    field_number(osos_media_item_track(item));
    field_number(length / 1000);
    field_text(qualified(length, elapsed) ? "L" : "S");
    field_number(listening.start_utc);
    raw('\n');
    write_line(CFW_LOG_SCROBBLER, scrobbler_header, sizeof(scrobbler_header) - 1);
}

struct log_record {
    struct osos_player *player;
    struct osos_media_item *item;
    uint32_t length, position, local, utc, plays, skips, plays_before, skips_before;
    enum ending ending;
    uint8_t state;
};

static void detailed_column(unsigned column, const struct log_record *record)
{
    struct osos_media_item *item = record->item;
    struct osos_player *player = record->player;
    switch (column) {
    case CFW_LOG_COLUMN_VERSION:
        field_text(LOG_VERSION);
        break;
    case CFW_LOG_COLUMN_SESSION:
        field_text(session);
        break;
    case CFW_LOG_COLUMN_END_UTC:
        field_number(record->utc);
        break;
    case CFW_LOG_COLUMN_UTC_OFFSET:
        utc_offset(record->local, record->utc);
        break;
    case CFW_LOG_COLUMN_START_LOCAL:
        datetime(listening.start_local, '-', ' ');
        raw('\t');
        break;
    case CFW_LOG_COLUMN_END_LOCAL:
        datetime(record->local, '-', ' ');
        raw('\t');
        break;
    case CFW_LOG_COLUMN_LISTENED_MS:
        field_number(listening.listened_ms);
        break;
    case CFW_LOG_COLUMN_LENGTH_MS:
        field_number(record->length);
        break;
    case CFW_LOG_COLUMN_PERCENT:
        field_number(record->length ? (uint32_t)((uint64_t)listening.listened_ms * 100 /
                                                 record->length)
                                    : 0);
        break;
    case CFW_LOG_COLUMN_ENDED:
        field_text(record->ending == ENDED_END    ? "end"
                   : record->ending == ENDED_SKIP ? "skip"
                                                  : "change");
        break;
    case CFW_LOG_COLUMN_APPLE:
        field_text(record->state == SESSION_PLAYED ||
                           record->plays > record->plays_before
                       ? "played"
                   : record->skips > record->skips_before ? "skipped"
                                                          : "none");
        break;
    case CFW_LOG_COLUMN_ARTIST:
        field_media_text(item, OSOS_MEDIA_ARTIST);
        break;
    case CFW_LOG_COLUMN_ALBUM_ARTIST:
        field_media_text(item, 0);
        break;
    case CFW_LOG_COLUMN_ALBUM:
        field_media_text(item, OSOS_MEDIA_ALBUM);
        break;
    case CFW_LOG_COLUMN_TITLE:
        field_media_text(item, OSOS_MEDIA_TITLE);
        break;
    case CFW_LOG_COLUMN_TRACK:
        field_number(osos_media_item_track(item));
        break;
    case CFW_LOG_COLUMN_DISC:
        field_number(osos_media_item_disc(item));
        break;
    case CFW_LOG_COLUMN_YEAR:
        field_number(osos_media_item_year(item));
        break;
    case CFW_LOG_COLUMN_GENRE:
        field_media_text(item, OSOS_MEDIA_GENRE);
        break;
    case CFW_LOG_COLUMN_COMPOSER:
        field_media_text(item, OSOS_MEDIA_COMPOSER);
        break;
    case CFW_LOG_COLUMN_BITRATE:
        field_number(osos_media_item_bitrate(item));
        break;
    case CFW_LOG_COLUMN_SAMPLE_RATE:
        field_number(osos_media_item_sample_rate(item));
        break;
    case CFW_LOG_COLUMN_RATING:
        field_number((uint32_t)osos_media_item_rating(item) / 20);
        break;
    case CFW_LOG_COLUMN_PLAY_COUNT:
        field_number(record->plays);
        break;
    case CFW_LOG_COLUMN_SKIP_COUNT:
        field_number(record->skips);
        break;
    case CFW_LOG_COLUMN_SHUFFLE:
        field_text(osos_player_is_shuffled(player) ? "on" : "off");
        break;
    case CFW_LOG_COLUMN_REPEAT:
        field_text(osos_player_is_repeat_one(player)   ? "one"
                   : osos_player_is_repeat_all(player) ? "all"
                                                       : "off");
        break;
    case CFW_LOG_COLUMN_QUEUE_POS:
        field_number(osos_playback_list_valid(&player->playback)
                         ? (uint32_t)player->current_index + 1
                         : 0);
        break;
    case CFW_LOG_COLUMN_QUEUE_LEN:
        field_number(osos_playback_list_valid(&player->playback)
                         ? osos_list_root(player->playback.list)->tracks
                         : 0);
        break;
    case CFW_LOG_COLUMN_PERSISTENT_ID:
        if (item->record) {
            const uint32_t *id = (const uint32_t *)((uint8_t *)item->record +
                                                    RECORD_PERSISTENT_ID_OFFSET);
            hex(id[1]);
            hex(id[0]);
        }
        raw('\t');
        break;
    case CFW_LOG_COLUMN_START_UTC:
        field_number(listening.start_utc);
        break;
    case CFW_LOG_COLUMN_POSITION_MS:
        field_number(record->position);
        break;
    }
}

static void log_session(struct osos_media_item *item, uint8_t state, uint32_t position,
                        enum ending ending, uint32_t plays_before,
                        uint32_t skips_before)
{
    uint32_t length = osos_media_item_duration(item);
    uint32_t elapsed = listening.listened_ms;
    if (!elapsed)
        return;
    uint32_t kind = item->record ? *(uint32_t *)((uint8_t *)item->record + 0xbc) : 0;
    if (!cfw_log_config_includes(&config, kind))
        return;
    if (config.format & CFW_LOG_SCROBBLER)
        scrobble(item, length, elapsed);
    if (!(config.format & CFW_LOG_DETAILED))
        return;
    uint32_t plays = osos_media_item_play_count(item);
    uint32_t skips = osos_media_item_skip_count(item);

    uint32_t local, utc;
    clock_now(&local, &utc);
    line.size = 0;
    if (!session[0]) {
        datetime(local, 0, '-');
        for (unsigned i = 0; i < line.size && i + 1 < sizeof(session); i++)
            session[i] = line.data[i];
        line.size = 0;
    }

    const struct log_record record = {
        .player = osos_player_get(),
        .item = item,
        .length = length,
        .position = position,
        .local = local,
        .utc = utc,
        .plays = plays,
        .skips = skips,
        .plays_before = plays_before,
        .skips_before = skips_before,
        .ending = ending,
        .state = state,
    };
    for (unsigned i = 0; i < config.column_count; i++)
        detailed_column(config.columns[i], &record);
    line.data[line.size - 1] = '\n';
    write_line(CFW_LOG_DETAILED, detailed_header, detailed_header_size);
}

static void finish(struct track_session *session_object, uint32_t position,
                   enum ending ending, const void *next)
{
    struct osos_player *player = osos_player_get();
    struct osos_media_item *item = session_object ? &session_object->item : NULL;
    if (enabled)
        tick(osos_playback_clock());
    int logged = enabled && session_object && session_object == player->current &&
                 session_object == listening.object && listening.started &&
                 !listening.finalized && session_object->state != SESSION_FINISHED &&
                 osos_media_item_valid(item);
    if (logged) {
        listening.running = 0;
        listening.finalized = 1;
    }
    uint8_t state = logged ? session_object->state : 0;
    uint32_t plays = logged ? osos_media_item_play_count(item) : 0;
    uint32_t skips = logged ? osos_media_item_skip_count(item) : 0;
    /* The native finish updates the counters; the item outlives it in the caller. */
    api->hook_next(next, (uintptr_t)session_object, position, 0, 0, 0, 0);
    if (logged)
        log_session(item, state, position, state == SESSION_PLAYED ? ENDED_END : ending,
                    plays, skips);
}

/* Next/previous: MediaPlayer_AdvanceTrack can finish the current track itself. */
static void on_skip(struct track_session *session_object, uint32_t position,
                    const void *next)
{
    finish(session_object, position, ENDED_SKIP, next);
}

/* Natural end, next/previous, a new selection or stop: the current-track release. */
static void on_release(struct track_session *session_object, uint32_t position,
                       const void *next)
{
    finish(session_object, position, advancing ? ENDED_SKIP : ENDED_CHANGE, next);
}

/* MediaPlayer_AdvanceTrack selects the next track, releasing the current one. */
static int on_advance(struct osos_player *player, int index, int flags,
                      const void *next)
{
    advancing = 1;
    int result = api->hook_next(next, (uintptr_t)player, index, flags, 0, 0, 0);
    advancing = 0;
    return result;
}

static int on_notify(struct osos_player *player, uint32_t property, const void *next)
{
    observe(player);
    return api->hook_next(next, (uintptr_t)player, property, 0, 0, 0, 0);
}

static int on_event(struct osos_player *player, void *event, const void *next)
{
    observe(player);
    int result = api->hook_next(next, (uintptr_t)player, (uintptr_t)event, 0, 0, 0, 0);
    observe(player);
    return result;
}

static void on_complete(struct track_session *object, const void *next)
{
    if (enabled)
        tick(osos_playback_clock());
    int logged = enabled && object && object == osos_player_get()->current &&
                 object == listening.object && listening.started &&
                 !listening.finalized && object->state == 4 &&
                 osos_media_item_valid(&object->item);
    api->hook_next(next, (uintptr_t)object, 0, 0, 0, 0, 0);
    if (logged && object->state == SESSION_PLAYED) {
        listening.running = 0;
        listening.finalized = 1;
        log_session(&object->item, SESSION_PLAYED,
                    osos_media_item_duration(&object->item), ENDED_END, 0, 0);
    }
}

static void load_config(void)
{
    struct osos_path path;
    void *file;
    osos_path_init(&path, "/Reprise/playback-log.cfg");
    int error = osos_file_open(&path, 1, &file);
    if (error) {
        osos_path_init(&path, old_config_path);
        error = osos_file_open(&path, 1, &file);
    }
    char data[CFW_LOG_CONFIG_MAX + 1];
    uint32_t bytes = 0;
    if (!error) {
        if (osos_file_read(file, data, sizeof data, &bytes))
            bytes = 0;
        osos_file_close(file);
    }
    cfw_log_config_parse(&config, data, bytes);
    configure_output();
}

static int settings_ready;

static void changed(uint16_t key)
{
    if (key == RV_SETTING_KEY_ALL) {
        int import = !settings_ready;
        settings_ready = 0;
        migrate_switch(api, self, import, ENABLED, 0x474f4c50, 1, 1, 0,
                       RV_SETTING_TYPE_BOOL);
        settings_ready = 1;
    }
    enabled = api->setting_get_int(self, ENABLED);
    listening.object = NULL;
    listening.running = listening.started = listening.finalized = 0;
    /* The player is constructed after boot preferences. Its first event starts timing. */
    if (enabled && settings_ready && key == ENABLED)
        observe(osos_player_get());
}
static const struct rv_setting settings[] = {
    RV_SETTING_BOOL(ENABLED, "Playback Log", 0)};
static const struct rv_menu_entry entries[] = {
    RV_MENU_ENTRY(RV_MENU_SETTINGS, RV_SETTINGS_SOUND_CHECK,
                  RV_ROW_SETTING("Playback Log", ENABLED),
                  .pane = RV_PANE(.caption = "Save Listening History",
                                  .bitmap = RV_BITMAP("settings-playback-log"))),
};
static const struct rv_hook hooks[] = {
    RV_HOOK_FILTER(RV_SITE_MediaPlayer_AdvanceFinish_call, on_skip),
    RV_HOOK_FILTER(RV_SITE_MediaPlayer_ReleaseFinish_call, on_release),
    RV_HOOK_FILTER(RV_SITE_MediaPlayer_AdvanceSelect_call, on_advance),
    RV_HOOK_FILTER(RV_SITE_TrackSession_Complete_entry, on_complete),
    RV_HOOK_FILTER(RV_SITE_MediaPlayer_Notify_entry, on_notify),
    RV_HOOK_FILTER(RV_SITE_MediaPlayer_Event_entry, on_event),
};
static const struct rv_descriptor extension = {
    .size = sizeof extension,
    RV_HOOKS(hooks),
    RV_SETTINGS(settings),
    RV_ENTRIES(entries),
    .setting_changed = changed,
};

const struct rv_descriptor *rv_module_entry(const struct rv_api *core,
                                            rv_extension_handle handle)
{
    api = core;
    self = handle;
    load_config();
    return &extension;
}
