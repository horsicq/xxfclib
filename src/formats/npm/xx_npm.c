/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/npm/xx_npm.h"

#include "../tar_common/xx_tar_common.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/json/xx_json.h"
#include "xxfclib/strings/xx_string.h"

#include <string.h>

#define XX_NPM_PACKAGE_JSON "package/package.json"
#define XX_NPM_PACKAGE_JSON_LIMIT ((size_t)1024U * (size_t)1024U)
#define XX_NPM_RECORD_LIMIT 20000U
#define XX_NPM_JSON_DEPTH_LIMIT 64U

static void xx_npm_apply_identity(Abstractformat *format) {
    if (!format) {
        return;
    }
    format->file_type = XX_FILE_TYPE_NPM;
    format->format_type = XX_TYPE_PACKAGE;
    format->os = XX_OS_GENERIC;
    format->is_archive = true;
    format->is_executable = false;
    xx_format_set_mime_type(format, "application/x-npm");
    xx_format_set_extension(format, "tgz");
}

/*
 * A package.json is accepted when it is one well-formed JSON object, consumed
 * to the last byte, carrying top-level "name" and "version" keys whose values
 * are strings with at least one non-whitespace character.
 *
 * The strictness is the point: this predicate is what separates an npm
 * package tarball from any other tarball that happens to contain a file at
 * package/package.json, so a lenient parse would weaken the identification
 * rather than just the validation.
 */
static bool xx_npm_json_string_has_content(const char *text) {
    size_t i;

    if (!text) return false;
    for (i = 0U; text[i]; ++i) {
        char c = text[i];
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r') return true;
    }
    return false;
}

static bool xx_npm_package_json_is_valid(const uint8_t *data, size_t size) {
    xx_json json;
    bool name_valid = false;
    bool version_valid = false;

    if (!data || size == 0U || size > XX_NPM_PACKAGE_JSON_LIMIT) {
        return false;
    }
    xx_json_init(&json, data, size);
    if (!xx_json_object_begin(&json)) return false;
    if (!xx_json_object_empty(&json)) {
        for (;;) {
            char *key = NULL;
            bool ok;

            if (!xx_json_object_key(&json, &key)) return false;
            if (xx_str_cmp(key, "name") == 0 ||
                xx_str_cmp(key, "version") == 0) {
                bool is_name = xx_str_cmp(key, "name") == 0;
                /* The value must be a string; anything else leaves the flag
                 * false, which fails the check below rather than the parse. */
                if (xx_json_peek(&json) == XX_JSON_TYPE_STRING) {
                    char *value = NULL;
                    ok = xx_json_string(&json, &value);
                    if (ok && xx_npm_json_string_has_content(value)) {
                        if (is_name) {
                            name_valid = true;
                        } else {
                            version_valid = true;
                        }
                    }
                    xx_str_free(value);
                } else {
                    ok = xx_json_skip(&json);
                }
            } else {
                ok = xx_json_skip(&json);
            }
            xx_str_free(key);
            if (!ok) return false;
            if (xx_json_more(&json)) continue;
            break;
        }
    }
    if (!xx_json_object_end(&json)) return false;
    /* Nothing may follow the object: trailing bytes mean this is not a
     * package.json, even if the prefix parsed. */
    while (json.position < json.size) {
        uint8_t c = json.data[json.position];
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r') return false;
        ++json.position;
    }
    return name_valid && version_valid;
}

static bool xx_npm_record_name_matches(const xx_archive_record *record) {
    const char *name;
    const wchar_t *name_w;
    char *owned_name = NULL;
    bool result;
    if (!record) {
        return false;
    }
    name = xx_archive_record_get_original_name(record);
    if (name) {
        return xx_rt_strcmp(name, XX_NPM_PACKAGE_JSON) == 0;
    }
    name_w = xx_archive_record_get_original_name_w(record);
    if (name_w) {
        owned_name = xx_str_unicode_to_utf8(name_w);
    }
    result = owned_name && xx_rt_strcmp(owned_name, XX_NPM_PACKAGE_JSON) == 0;
    if (owned_name) {
        xx_str_free(owned_name);
    }
    return result;
}

static bool xx_npm_has_valid_package_json(Abstractformat *self,
                                          xx_pd_struct *pd) {
    xx_tar_gz probe;
    xx_tar_common *common;
    xx_archive_record_state *state = NULL;
    uint64_t visited = 0U;
    bool result = false;

    if (!self || !self->device || self->base_address < 0) {
        return false;
    }
    xx_tar_gz_init(&probe, self->device, self->base_address);
    if (!xx_tar_gz_handle_base_info(&probe.format, pd)) {
        xx_tar_gz_destroy(&probe);
        return false;
    }
    common = (xx_tar_common *)probe.internal;
    state = xx_tar_gz_create_archive_records_reading(
        &probe.format, NULL, NULL);
    while (state && state->has_record && visited < XX_NPM_RECORD_LIMIT) {
        const xx_archive_record *record =
            xx_tar_gz_get_current_archive_record(&probe.format, state);
        ++visited;
        if (pd && xx_pd_is_stopped(pd)) {
            break;
        }
        if (xx_npm_record_name_matches(record)) {
            uint64_t unpacked_size = xx_archive_record_get_meta_u64(
                record, XX_META_ID_UNCOMPRESSED_SIZE, 0U);
            bool is_folder = xx_archive_record_get_meta_bool(
                record, XX_META_ID_IS_FOLDER, false);
            if (common && common->decoded_data && !is_folder &&
                unpacked_size > 0U &&
                unpacked_size <= XX_NPM_PACKAGE_JSON_LIMIT &&
                record->compressed_size == (int64_t)unpacked_size &&
                record->data_offset >= 0 &&
                (uint64_t)record->data_offset <=
                    (uint64_t)common->decoded_size &&
                unpacked_size <=
                    (uint64_t)common->decoded_size -
                        (uint64_t)record->data_offset) {
                result = xx_npm_package_json_is_valid(
                    common->decoded_data + (size_t)record->data_offset,
                    (size_t)unpacked_size);
            }
            break;
        }
        if (!xx_tar_gz_archive_record_move_to_next(&probe.format, state,
                                                    NULL)) {
            break;
        }
    }
    if (state) {
        xx_tar_gz_free_archive_records_reading(&probe.format, state);
    }
    xx_tar_gz_destroy(&probe);
    return result;
}

static void xx_npm_vtable_destroy(Abstractformat *self) {
    if (self) {
        xx_npm_destroy((xx_npm *)self);
    }
}

void xx_npm_init(xx_npm *npm, xx_io_device *dev, int64_t base_address) {
    Abstractformat *format;

    if (!npm) {
        return;
    }
    xx_mem_zero(npm, sizeof(*npm));
    xx_tar_gz_init(&npm->tar_gz, dev, base_address);
    format = &npm->tar_gz.format;
    xx_npm_apply_identity(format);

    /* All remaining callbacks stay wired directly to xx_tar_gz.c. */
    format->check_is_valid = xx_npm_check_is_valid;
    format->handle_base_info = xx_npm_handle_base_info;
    format->destroy = xx_npm_vtable_destroy;
}

xx_npm *xx_npm_create(xx_io_device *dev, int64_t base_address) {
    xx_npm *npm = (xx_npm *)xx_mem_alloc(sizeof(*npm));
    if (!npm) {
        return NULL;
    }
    xx_npm_init(npm, dev, base_address);
    return npm;
}

void xx_npm_destroy(xx_npm *npm) {
    if (npm) {
        xx_tar_gz_destroy(&npm->tar_gz);
    }
}

void xx_npm_free(xx_npm *npm) {
    if (!npm) {
        return;
    }
    xx_npm_destroy(npm);
    xx_mem_free(npm);
}

bool xx_npm_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    return xx_npm_has_valid_package_json(self, pd);
}

bool xx_npm_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    bool result;

    if (!self) {
        return false;
    }
    result = xx_tar_gz_handle_base_info(self, pd);
    if (result) {
        result = xx_npm_has_valid_package_json(self, pd);
    }
    xx_npm_apply_identity(self);
    self->is_valid = result;
    return result;
}
