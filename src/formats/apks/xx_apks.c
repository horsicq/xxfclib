/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Identity and limits follow Formats/archives/xapks.cpp: a bundletool APK Set
 * contains an authenticated nonempty toc.pb and a complete authenticated APK.
 * No archive member is published or executed by this probe.
 */
#include "xxfclib/formats/apks/xx_apks.h"
#include "xxfclib/formats/apk/xx_apk.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"

#define XX_APKS_RECORD_LIMIT 20000U
#define XX_APKS_CANDIDATE_LIMIT 8U
#define XX_APKS_TOC_LIMIT (UINT64_C(16) * 1024U * 1024U)
#define XX_APKS_DEFLATED_LIMIT (UINT64_C(64) * 1024U * 1024U)

typedef struct xx_apks_sink {
    uint8_t *data;
    uint64_t position;
    uint64_t limit;
} xx_apks_sink;

static ssize_t xx_apks_sink_write(xx_io_device *device, const void *data,
                                   size_t size) {
    xx_apks_sink *sink = device ? (xx_apks_sink *)device->priv : NULL;
    if (!sink || (!data && size) || size > (size_t)PTRDIFF_MAX ||
        sink->position > sink->limit ||
        (uint64_t)size > sink->limit - sink->position) return -1;
    if (sink->data && size) {
        xx_rt_memcpy(sink->data + (size_t)sink->position, data, size);
    }
    sink->position += (uint64_t)size;
    return (ssize_t)size;
}

static void xx_apks_init_sink(xx_io_device *device, xx_apks_sink *sink,
                               uint8_t *data, uint64_t limit) {
    xx_mem_zero(device, sizeof(*device));
    xx_mem_zero(sink, sizeof(*sink));
    sink->data = data;
    sink->limit = limit;
    device->priv = sink;
    device->write = xx_apks_sink_write;
}

static bool xx_apks_name_is_apk(const char *name) {
    size_t length;
    const char *suffix;
    if (!name) return false;
    length = xx_rt_strlen(name);
    if (length < 4U) return false;
    suffix = name + length - 4U;
    return suffix[0] == '.' && (suffix[1] == 'a' || suffix[1] == 'A') &&
           (suffix[2] == 'p' || suffix[2] == 'P') &&
           (suffix[3] == 'k' || suffix[3] == 'K');
}

static bool xx_apks_complete_apk(xx_io_device *device, xx_pd_struct *pd) {
    xx_apk apk;
    bool valid;
    xx_apk_init(&apk, device, 0);
    valid = xx_apk_check_is_valid(&apk.zip.format, pd);
    xx_apk_destroy(&apk);
    return valid;
}

static bool xx_apks_probe(xx_io_device *device, int64_t base_address,
                           xx_pd_struct *pd) {
    xx_zip outer;
    xx_archive_record_state *state = NULL;
    uint64_t visited = 0;
    unsigned candidates = 0;
    bool has_toc = false, has_apk = false, result = false;
    int64_t saved, device_size;

    if (!device || base_address < 0 || xx_pd_is_stopped(pd)) return false;
    saved = xx_io_tell(device);
    device_size = xx_io_total_size(device);
    if (saved < 0 || device_size < base_address) return false;
    xx_zip_init(&outer, device, base_address);
    if (!xx_zip_handle_base_info(&outer.format, pd) ||
        outer.number_of_records > XX_APKS_RECORD_LIMIT) goto done;
    outer.format.base_info_handled = true;
    state = xx_zip_create_archive_records_reading(&outer.format, NULL, NULL);
    while (state && state->has_record && visited++ < XX_APKS_RECORD_LIMIT &&
           !xx_pd_is_stopped(pd)) {
        const xx_archive_record *record =
            xx_zip_get_current_archive_record(&outer.format, state);
        const char *name = xx_archive_record_get_original_name(record);
        char *owned_name = NULL;
        bool is_toc, is_apk;
        uint64_t unpacked, method;
        int64_t packed;
        if (!name) {
            const wchar_t *wide = xx_archive_record_get_original_name_w(record);
            if (wide) owned_name = xx_str_unicode_to_utf8(wide);
            name = owned_name;
        }
        is_toc = name && !xx_rt_strcmp(name, "toc.pb");
        is_apk = xx_apks_name_is_apk(name);
        xx_str_free(owned_name);
        unpacked = xx_archive_record_get_meta_u64(
            record, XX_META_ID_UNCOMPRESSED_SIZE, UINT64_MAX);
        method = xx_archive_record_get_meta_u64(
            record, XX_META_ID_COMPRESSION_METHOD, UINT64_MAX);
        packed = record ? record->compressed_size : -1;
        if ((is_toc || is_apk) && unpacked > 0U && unpacked <= INT64_MAX &&
            packed > 0 && record->data_offset >= base_address &&
            record->data_offset <= device_size &&
            packed <= device_size - record->data_offset &&
            !xx_archive_record_get_meta_bool(record, XX_META_ID_IS_ENCRYPTED, false) &&
            !xx_archive_record_get_meta_bool(record, XX_META_ID_IS_FOLDER, false) &&
            (method == 0U || method == 8U)) {
            if (is_toc && !has_toc && unpacked <= XX_APKS_TOC_LIMIT &&
                (uint64_t)packed <= XX_APKS_TOC_LIMIT) {
                xx_io_device sink_device;
                xx_apks_sink sink;
                xx_apks_init_sink(&sink_device, &sink, NULL, unpacked);
                has_toc = xx_zip_unpack_current_archive_record_to_device(
                    &outer.format, state, &sink_device, pd) &&
                    sink.position == unpacked;
            } else if (is_apk && !has_apk &&
                       candidates++ < XX_APKS_CANDIDATE_LIMIT) {
                if (method == 0U && (uint64_t)packed == unpacked) {
                    xx_io_device sink_device;
                    xx_apks_sink sink;
                    xx_apks_init_sink(&sink_device, &sink, NULL, unpacked);
                    if (xx_zip_unpack_current_archive_record_to_device(
                            &outer.format, state, &sink_device, pd) &&
                        sink.position == unpacked) {
                        xx_io_device *inner = xx_io_sub_open_ro(
                            device, record->data_offset, packed);
                        if (inner) {
                            has_apk = xx_apks_complete_apk(inner, pd);
                            xx_io_close(inner);
                        }
                    }
                } else if (method == 8U && unpacked <= XX_APKS_DEFLATED_LIMIT &&
                           (uint64_t)packed <= XX_APKS_DEFLATED_LIMIT &&
                           unpacked <= SIZE_MAX) {
                    uint8_t *decoded = (uint8_t *)xx_mem_alloc((size_t)unpacked);
                    if (decoded) {
                        xx_io_device sink_device;
                        xx_apks_sink sink;
                        xx_apks_init_sink(&sink_device, &sink, decoded, unpacked);
                        if (xx_zip_unpack_current_archive_record_to_device(
                                &outer.format, state, &sink_device, pd) &&
                            sink.position == unpacked) {
                            xx_io_device *inner = xx_io_mem_open_ro(decoded, (size_t)unpacked);
                            if (inner) {
                                has_apk = xx_apks_complete_apk(inner, pd);
                                xx_io_close(inner);
                            }
                        }
                        xx_mem_free(decoded);
                    }
                }
            }
        }
        if (has_toc && has_apk && !xx_pd_is_stopped(pd)) {
            result = true;
            break;
        }
        if (!xx_zip_archive_record_move_to_next(&outer.format, state, NULL)) break;
    }
done:
    if (state) xx_zip_free_archive_records_reading(&outer.format, state);
    xx_zip_destroy(&outer);
    if (xx_io_seek64(device, saved, SEEK_SET) != 0) result = false;
    return result;
}

static void xx_apks_apply_identity(Abstractformat *format) {
    format->file_type = XX_FILE_TYPE_APKS;
    format->format_type = XX_TYPE_PACKAGE;
    format->os = XX_OS_ANDROID;
    format->is_archive = true;
    format->is_executable = false;
    xx_format_set_mime_type(format, "application/vnd.android.package-archive");
    xx_format_set_extension(format, "apks");
}

static void xx_apks_vtable_destroy(Abstractformat *self) {
    if (self) xx_apks_destroy((xx_apks *)self);
}

void xx_apks_init(xx_apks *apks, xx_io_device *device, int64_t base_address) {
    if (!apks) return;
    xx_mem_zero(apks, sizeof(*apks));
    xx_zip_init(&apks->zip, device, base_address);
    xx_apks_apply_identity(&apks->zip.format);
    apks->zip.format.check_is_valid = xx_apks_check_is_valid;
    apks->zip.format.handle_base_info = xx_apks_handle_base_info;
    apks->zip.format.destroy = xx_apks_vtable_destroy;
}

xx_apks *xx_apks_create(xx_io_device *device, int64_t base_address) {
    xx_apks *apks = (xx_apks *)xx_mem_alloc(sizeof(*apks));
    if (apks) xx_apks_init(apks, device, base_address);
    return apks;
}

void xx_apks_destroy(xx_apks *apks) {
    if (apks) xx_zip_destroy(&apks->zip);
}

void xx_apks_free(xx_apks *apks) {
    if (!apks) return;
    xx_apks_destroy(apks);
    xx_mem_free(apks);
}

bool xx_apks_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    return self && xx_apks_probe(self->device, self->base_address, pd);
}

bool xx_apks_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    bool result;
    if (!self) return false;
    result = xx_zip_handle_base_info(self, pd) && xx_apks_check_is_valid(self, pd);
    xx_apks_apply_identity(self);
    self->is_valid = result;
    return result;
}

xx_file_type_t xx_apks_detect(xx_io_device *device, int64_t base_address) {
    uint8_t magic[4];
    if (!xx_io_read_at(device, base_address, magic, sizeof(magic)) ||
        magic[0] != 'P' || magic[1] != 'K' || magic[2] != 3U || magic[3] != 4U) {
        return XX_FILE_TYPE_UNKNOWN;
    }
    return xx_apks_probe(device, base_address, NULL) ?
        XX_FILE_TYPE_APKS : XX_FILE_TYPE_UNKNOWN;
}
