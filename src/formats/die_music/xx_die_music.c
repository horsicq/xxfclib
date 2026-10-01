/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * The 408 per-family modules use DIE's original ordered rule for exact
 * identification. This shared adapter lists/extracts the encoded file itself;
 * it does not claim to split patterns, instruments, or decode samples.
 */
#include "xxfclib/formats/die_music/xx_die_music.h"
#include "xxfclib/die_engine/die_engine.h"
#include "../xx_payload_members.h"
#include <string.h>

typedef struct xx_die_music_reader {
    Abstractformat format;
    const xx_die_music_descriptor *descriptor;
} xx_die_music_reader;

typedef struct xx_die_music_type_entry {
    const char *format_id;
    xx_file_type_t file_type;
} xx_die_music_type_entry;

#define XX_DIE_MUSIC_ENTRY(ID, slug, numeric_id, display_name) \
    { #ID, XX_FILE_TYPE_DIE_MUSIC_##ID },
static const xx_die_music_type_entry xx_die_music_types[] = {
#include "xx_die_music_registry.inc"
};
#undef XX_DIE_MUSIC_ENTRY

static xx_file_type_t xx_die_music_type_for_id(const char *id)
{
    size_t i;
    if (!id || !id[0]) return XX_FILE_TYPE_UNKNOWN;
    for (i = 0; i < sizeof(xx_die_music_types) / sizeof(xx_die_music_types[0]); ++i) {
        if (strcmp(id, xx_die_music_types[i].format_id) == 0)
            return xx_die_music_types[i].file_type;
    }
    return XX_FILE_TYPE_UNKNOWN;
}

static xx_file_type_t xx_die_music_scan(xx_io_device *device,
                                         const char *expected_id)
{
    DBase db = {0};
    ScanOptions options;
    ScanResult result = {0};
    xx_file_type_t strong = XX_FILE_TYPE_UNKNOWN;
    xx_file_type_t heuristic = XX_FILE_TYPE_UNKNOWN;
    int pass;
    int i;

    if (!device || xx_io_size(device) <= 0) return XX_FILE_TYPE_UNKNOWN;
    scan_options_init(&options);
    if (db_load_builtin_music(&db, DB_MAIN)) {
        /* Keep ordinary signatures ahead of deep and heuristic rules. A
         * forced reader still gets all three passes when its family needs
         * one of the optional DIE scan modes. */
        for (pass = 0; pass < 3; ++pass) {
            options.bDeepScan = (pass >= 1);
            options.bHeuristicScan = (pass >= 2);
            if (die_engine_scan_device(device, &db, &options, &result)) {
                for (i = 0; i < result.nCount; ++i) {
                    const ScanRecord *record = &result.pRecords[i];
                    xx_file_type_t type;
                    if (!record->pFormatId || !record->pFormatId[0] ||
                        (expected_id && strcmp(expected_id, record->pFormatId) != 0))
                        continue;
                    type = xx_die_music_type_for_id(record->pFormatId);
                    if (type == XX_FILE_TYPE_UNKNOWN) continue;
                    if (record->bIsHeuristic || record->bIsAHeuristic) {
                        if (heuristic == XX_FILE_TYPE_UNKNOWN) heuristic = type;
                    } else if (strong == XX_FILE_TYPE_UNKNOWN) {
                        strong = type;
                    }
                }
            }
            scan_result_free(&result);
            if (strong != XX_FILE_TYPE_UNKNOWN ||
                (expected_id && heuristic != XX_FILE_TYPE_UNKNOWN)) break;
        }
    }
    db_free(&db);
    scan_options_free(&options);
    return strong != XX_FILE_TYPE_UNKNOWN ? strong : heuristic;
}

xx_file_type_t xx_die_music_detect_device(xx_io_device *device)
{
    return xx_die_music_scan(device, NULL);
}

static bool pm_parse(Abstractformat *format, pm_stream *stream, xx_pd_struct *pd)
{
    xx_die_music_reader *reader = (xx_die_music_reader *)format;
    xx_io_volume volume;
    xx_io_device *view;
    xx_file_type_t actual;
    int64_t available = pm_available(format);
    char name[80];

    if (!reader->descriptor || available <= 0 ||
        (pd && xx_pd_is_stopped(pd))) return false;
    volume.device = format->device;
    volume.offset = format->base_address;
    volume.size = available;
    view = xx_io_multivolume_open(&volume, 1U, false);
    if (!view) return false;
    actual = xx_die_music_scan(view, reader->descriptor->format_id);
    (void)xx_io_close(view);
    if (actual != reader->descriptor->file_type ||
        (pd && xx_pd_is_stopped(pd))) return false;

    (void)xx_rt_snprintf(name, sizeof(name), "%s.music",
                         reader->descriptor->format_id);
    if (!pm_add(format, stream, name, 0, available)) return false;
    stream->size = available;
    return true;
}

Abstractformat *xx_die_music_reader_create(
    const xx_die_music_descriptor *descriptor, xx_io_device *device,
    int64_t base_address)
{
    xx_die_music_reader *reader;
    if (!descriptor) return NULL;
    reader = (xx_die_music_reader *)xx_mem_alloc(sizeof(*reader));
    if (!reader) return NULL;
    xx_mem_zero(reader, sizeof(*reader));
    pm_init(&reader->format, device, base_address, descriptor->file_type,
            "music");
    reader->descriptor = descriptor;
    return &reader->format;
}

void xx_die_music_reader_free(void *p)
{
    xx_die_music_reader *reader = (xx_die_music_reader *)p;
    if (!reader) return;
    xx_format_cleanup_extra_parameters(&reader->format);
    xx_mem_free(reader);
}
