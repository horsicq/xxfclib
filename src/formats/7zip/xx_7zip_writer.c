/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Native non-solid 7z writer. Header framing follows the official
 * ip7z/7zip DOC/7zFormat.txt; coder IDs follow DOC/Methods.txt.
 */
#include "xx_7zip_writer.h"
#include "xx_7zip_defs.h"
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/lzma/xx_lzma.h"
#include "xxfclib/algo/bzip2/xx_bzip2.h"
#include "xxfclib/algo/ppmd7/xx_ppmd7.h"
#include "../../algo/ppmd7/xx_ppmd7_internal.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/zstd/xx_zstd.h"
#include "xxfclib/algo/aes/xx_aes.h"
#include <limits.h>

#define X7W_MAX_HEADER (64U * 1024U * 1024U)
#define X7W_MAX_FILES 262144U

typedef struct x7w_buffer {
    uint8_t *data;
    size_t size, capacity;
} x7w_buffer;

typedef struct x7w_entry {
    x7w_buffer name;
    uint64_t method, size, packed, codec_size, mtime;
    uint32_t crc, packed_crc, attributes;
    uint8_t properties[5], aes_properties[34];
    size_t properties_size, aes_properties_size;
    bool directory, encrypted, mtime_defined;
} x7w_entry;

typedef struct x7w_state {
    x7w_entry *entries;
    size_t count, capacity, folders, names_size;
    int64_t offset;
    bool finalized, failed;
} x7w_state;

typedef struct x7w_output {
    xx_io_device device;
    xx_io_device *target;
    xx_pd_struct *pd;
    uint64_t written, limit;
    uint32_t crc;
    bool failed;
} x7w_output;

typedef struct x7w_source {
    xx_io_device device;
    xx_io_device *target;
    xx_pd_struct *pd;
    int64_t size, position;
    uint32_t crc;
} x7w_source;

static bool x7w_error(xx_pd_struct *pd, const char *message) {
    xx_pd_set_error(pd, XXFC_ERR_INVALID_ARG, message);
    return false;
}

static void x7w_buffer_free(x7w_buffer *buffer) {
    xx_mem_free(buffer->data);
    xx_mem_zero(buffer, sizeof(*buffer));
}

static void x7w_secret_free(x7w_buffer *buffer) {
    volatile uint8_t *bytes = buffer->data;
    size_t size = buffer->capacity;
    if (bytes) while (size--) *bytes++ = 0U;
    x7w_buffer_free(buffer);
}

static bool x7w_append(x7w_buffer *buffer, const void *data, size_t size) {
    size_t needed, capacity;
    uint8_t *replacement;
    if (size > X7W_MAX_HEADER - buffer->size || (!data && size)) return false;
    needed = buffer->size + size;
    if (needed > buffer->capacity) {
        capacity = buffer->capacity ? buffer->capacity : 256U;
        while (capacity < needed) {
            if (capacity > X7W_MAX_HEADER / 2U) {
                capacity = X7W_MAX_HEADER;
                break;
            }
            capacity *= 2U;
        }
        replacement = (uint8_t *)xx_mem_realloc(buffer->data, capacity);
        if (!replacement) return false;
        buffer->data = replacement;
        buffer->capacity = capacity;
    }
    if (size) xx_rt_memcpy(buffer->data + buffer->size, data, size);
    buffer->size = needed;
    return true;
}

static bool x7w_byte(x7w_buffer *buffer, uint8_t value) {
    return x7w_append(buffer, &value, 1U);
}

static void x7w_put_le(uint8_t *data, uint64_t value, size_t bytes) {
    size_t index;
    for (index = 0U; index < bytes; ++index) {
        data[index] = (uint8_t)value;
        value >>= 8U;
    }
}

static bool x7w_le(x7w_buffer *buffer, uint64_t value, size_t bytes) {
    uint8_t data[8];
    x7w_put_le(data, value, bytes);
    return x7w_append(buffer, data, bytes);
}

/* 7z UINT64 is a prefix byte followed by zero to eight LE bytes. */
static bool x7w_number(x7w_buffer *buffer, uint64_t value) {
    uint8_t data[9], first = 0U, mask = 0x80U;
    size_t extra;
    for (extra = 0U; extra < 8U; ++extra) {
        if (value < (UINT64_C(1) << (7U * (extra + 1U)))) {
            first |= (uint8_t)(value >> (8U * extra));
            break;
        }
        first |= mask;
        mask >>= 1U;
    }
    data[0] = first;
    x7w_put_le(data + 1U, value, extra);
    return x7w_append(buffer, data, extra + 1U);
}

static bool x7w_property(x7w_buffer *header, uint8_t id,
                         const x7w_buffer *data) {
    return x7w_byte(header, id) && x7w_number(header, data->size) &&
           x7w_append(header, data->data, data->size);
}

static const xx_var *x7w_option(Abstractformat *format,
                                const xx_archive_write_state *state,
                                const xx_archive_record *record, uint32_t id) {
    const xx_var *value = record ? xx_archive_record_find_meta(record, id) : NULL;
    return value ? value : xx_format_resolve_extra_parameter(format,
                                                   &state->options, id);
}

static bool x7w_unsigned(const xx_var *value, uint64_t *number) {
    if (!value || value->type < XX_VAR_TYPE_INT8 ||
        value->type > XX_VAR_TYPE_UINT64) return false;
    if (value->type <= XX_VAR_TYPE_INT64 && xx_var_get_i64(value) < 0) return false;
    *number = xx_var_get_u64(value);
    return true;
}

static bool x7w_method_supported(uint64_t method) {
    return method == XX_7ZIP_METHOD_COPY || method == XX_7ZIP_METHOD_LZMA ||
           method == XX_7ZIP_METHOD_LZMA2 || method == XX_7ZIP_METHOD_BZIP2 ||
           method == XX_7ZIP_METHOD_PPMD7 || method == XX_7ZIP_METHOD_DEFLATE ||
           method == XX_7ZIP_METHOD_DEFLATE64 || method == XX_7ZIP_METHOD_ZSTD;
}

static bool x7w_settings(Abstractformat *format, xx_archive_write_state *state,
                          const xx_archive_record *record, uint64_t *method,
                          int *level, const xx_var **password,
                          bool *encrypted, xx_pd_struct *pd) {
    const xx_var *value;
    uint64_t encryption = 0U;
    *method = XX_7ZIP_METHOD_LZMA2;
    value = x7w_option(format, state, record, XX_META_ID_COMPRESSION_METHOD);
    if (value && !x7w_unsigned(value, method)) return x7w_error(pd, "Invalid 7z method ID");
    if (!x7w_method_supported(*method)) return x7w_error(pd, "Unsupported 7z writer method");
    *level = *method == XX_7ZIP_METHOD_PPMD7 ? 8 :
             *method == XX_7ZIP_METHOD_BZIP2 ? XX_BZIP2_LEVEL_DEFAULT :
             *method == XX_7ZIP_METHOD_ZSTD ? XX_ZSTD_LEVEL_DEFAULT :
             *method == XX_7ZIP_METHOD_LZMA || *method == XX_7ZIP_METHOD_LZMA2
                  ? XX_LZMA_LEVEL_DEFAULT : XX_DEFLATE_LEVEL_DEFAULT;
    value = x7w_option(format, state, record, XX_META_ID_COMPRESSION_LEVEL);
    if (value) {
        int64_t signed_level;
        if (value->type < XX_VAR_TYPE_INT8 || value->type > XX_VAR_TYPE_UINT64 ||
            (value->type >= XX_VAR_TYPE_UINT8 && xx_var_get_u64(value) > INT_MAX))
            return x7w_error(pd, "Invalid 7z compression level");
        signed_level = xx_var_get_i64(value);
        if (signed_level < INT_MIN || signed_level > INT_MAX)
            return x7w_error(pd, "Invalid 7z compression level");
        *level = (int)signed_level;
    }
    *password = x7w_option(format, state, record, XX_META_ID_OPT_PASSWORD);
    value = x7w_option(format, state, record, XX_META_ID_ENCRYPTION_METHOD);
    if (value && (!x7w_unsigned(value, &encryption) ||
                  (encryption != 0U && encryption != XX_7ZIP_METHOD_AES)))
        return x7w_error(pd, "Unsupported 7z encryption method");
    if (value && encryption == 0U && *password)
        return x7w_error(pd, "7z password conflicts with disabled encryption");
    *encrypted = *password != NULL || encryption != 0U ||
        (record && xx_archive_record_get_meta_bool(record, XX_META_ID_IS_ENCRYPTED, false));
    if (*encrypted && !*password) return x7w_error(pd, "7z encryption requires a password");
    return true;
}

/* Convert the variant's exact extent, rejecting malformed Unicode and NULs.
 * wchar_t may be UTF-16 (Windows) or UTF-32 (POSIX). */
static bool x7w_text(const xx_var *value, bool password, x7w_buffer *result) {
    const uint8_t *bytes = NULL;
    const wchar_t *wide = NULL;
    size_t size = 0U, position = 0U;
    if (!value) return false;
    if (value->type == XX_VAR_TYPE_STRING || value->type == XX_VAR_TYPE_STRING_VIEW) {
        bytes = (const uint8_t *)xx_var_get_str(value);
        size = value->val.str.len;
    } else if (value->type == XX_VAR_TYPE_WSTRING || value->type == XX_VAR_TYPE_WSTRING_VIEW) {
        wide = xx_var_get_wstr(value);
        size = value->val.wstr.len;
    } else if (password && (value->type == XX_VAR_TYPE_BYTES || value->type == XX_VAR_TYPE_BYTES_VIEW)) {
        bytes = (const uint8_t *)xx_var_get_bytes(value, &size);
    } else return false;
    if ((!bytes && !wide && size) || (!password && !size)) return false;
    /* Allocate passwords once so a growing realloc never releases a copy of
     * sensitive text before the explicit wipe. Four bytes per source unit is
     * sufficient for both UTF-8 and wchar_t inputs. */
    if (password && size) {
        if (size > X7W_MAX_HEADER / 4U) return false;
        result->capacity = size * 4U;
        result->data = (uint8_t *)xx_mem_alloc(result->capacity);
        if (!result->data) { result->capacity = 0U; return false; }
    }
    while (position < size) {
        uint32_t code;
        if (wide) {
            code = (uint32_t)wide[position++];
            if (sizeof(wchar_t) == 2U && code >= 0xd800U && code <= 0xdbffU) {
                uint32_t next;
                if (position == size) return false;
                next = (uint32_t)wide[position++];
                if (next < 0xdc00U || next > 0xdfffU) return false;
                code = 0x10000U + ((code - 0xd800U) << 10U) + next - 0xdc00U;
            }
        } else {
            uint32_t minimum = 0U;
            unsigned continuation = 0U;
            code = bytes[position++];
            if (code >= 0x80U) {
                if (code >= 0xc2U && code <= 0xdfU) { code &= 31U; continuation = 1U; minimum = 0x80U; }
                else if (code >= 0xe0U && code <= 0xefU) { code &= 15U; continuation = 2U; minimum = 0x800U; }
                else if (code >= 0xf0U && code <= 0xf4U) { code &= 7U; continuation = 3U; minimum = 0x10000U; }
                else return false;
                if (continuation > size - position) return false;
                while (continuation--) {
                    uint8_t next = bytes[position++];
                    if ((next & 0xc0U) != 0x80U) return false;
                    code = (code << 6U) | (next & 63U);
                }
                if (code < minimum) return false;
            }
        }
        if (code == 0U || code > 0x10ffffU || (code >= 0xd800U && code <= 0xdfffU)) return false;
        if (!password && code == '\\') code = '/';
        if (code > 0xffffU) {
            code -= 0x10000U;
            if (!x7w_le(result, 0xd800U + (code >> 10U), 2U) ||
                !x7w_le(result, 0xdc00U + (code & 1023U), 2U)) return false;
        } else if (!x7w_le(result, code, 2U)) return false;
    }
    return password || x7w_le(result, 0U, 2U);
}

static uint16_t x7w_name_unit(const x7w_buffer *name, size_t index) {
    return (uint16_t)((uint16_t)name->data[index * 2U] |
                     ((uint16_t)name->data[index * 2U + 1U] << 8U));
}

static bool x7w_name(x7w_entry *entry, const xx_archive_record *record) {
    size_t units, start, index;
    const xx_var *value = xx_archive_record_find_meta(record, XX_META_ID_ORIGINAL_NAME);
    if (!x7w_text(value, false, &entry->name)) return false;
    units = entry->name.size / 2U - 1U;
    if (units && x7w_name_unit(&entry->name, units - 1U) == '/') entry->directory = true;
    while (units && x7w_name_unit(&entry->name, units - 1U) == '/') --units;
    if (!units || x7w_name_unit(&entry->name, 0U) == '/') return false;
    start = 0U;
    for (index = 0U; index <= units; ++index) {
        uint16_t unit = index == units ? 0U : x7w_name_unit(&entry->name, index);
        if (unit == ':') return false;
        if (!unit || unit == '/') {
            size_t length = index - start;
            if (!length || (length == 1U && x7w_name_unit(&entry->name, start) == '.') ||
                (length == 2U && x7w_name_unit(&entry->name, start) == '.' &&
                 x7w_name_unit(&entry->name, start + 1U) == '.')) return false;
            start = index + 1U;
        }
    }
    entry->name.size = units * 2U + 2U;
    entry->name.data[units * 2U] = entry->name.data[units * 2U + 1U] = 0U;
    return true;
}

static ssize_t x7w_output_write(xx_io_device *device, const void *data, size_t size) {
    x7w_output *output = (x7w_output *)device->priv;
    const uint8_t *bytes = (const uint8_t *)data;
    size_t done = 0U;
    if (output->failed || (!data && size) || size > (size_t)PTRDIFF_MAX ||
        (uint64_t)size > output->limit - output->written) return -1;
    while (done < size) {
        ssize_t amount;
        if (xx_pd_is_stopped(output->pd)) { output->failed = true; return -1; }
        amount = xx_io_write(output->target, bytes + done, size - done);
        if (amount <= 0 || (size_t)amount > size - done) {
            output->failed = true;
            return -1;
        }
        output->crc = xx_crc32_calc(output->crc, bytes + done, (size_t)amount);
        output->written += (uint64_t)amount;
        done += (size_t)amount;
    }
    return (ssize_t)size;
}

static int64_t x7w_output_size(xx_io_device *device) {
    return (int64_t)((x7w_output *)device->priv)->written;
}

static void x7w_output_init(x7w_output *output, xx_io_device *target,
                            uint64_t limit, xx_pd_struct *pd) {
    xx_mem_zero(output, sizeof(*output));
    output->target = target; output->limit = limit; output->pd = pd;
    output->device.priv = output;
    output->device.write = x7w_output_write;
    output->device.total_size = output->device.get_total_size =
        output->device.size = output->device.tell = x7w_output_size;
}

static ssize_t x7w_source_read(xx_io_device *device, void *data, size_t size) {
    x7w_source *source = (x7w_source *)device->priv;
    ssize_t amount;
    if (xx_pd_is_stopped(source->pd) || (!data && size)) return -1;
    if ((uint64_t)size > (uint64_t)(source->size - source->position))
        size = (size_t)(source->size - source->position);
    if (!size) return 0;
    amount = xx_io_read(source->target, data, size);
    if (amount <= 0 || (size_t)amount > size) return -1;
    source->crc = xx_crc32_calc(source->crc, data, (size_t)amount);
    source->position += amount;
    return amount;
}

static int64_t x7w_source_size(xx_io_device *device) {
    return ((x7w_source *)device->priv)->size;
}

static int64_t x7w_source_tell(xx_io_device *device) {
    return ((x7w_source *)device->priv)->position;
}

/* Native device encoders seek to zero before reading. Never allow a reread
 * after CRC accounting starts, which would authenticate the wrong bytes. */
static int x7w_source_seek64(xx_io_device *device, int64_t offset, int whence) {
    x7w_source *source = (x7w_source *)device->priv;
    int64_t base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? source->position :
                   whence == SEEK_END ? source->size : -1;
    if (base < 0 || offset < -base || offset > source->size - base ||
        base + offset != source->position) return -1;
    return 0;
}

static int x7w_source_seek(xx_io_device *device, long offset, int whence) {
    return x7w_source_seek64(device, offset, whence);
}

static void x7w_source_init(x7w_source *source, xx_io_device *target,
                            int64_t size, xx_pd_struct *pd) {
    xx_mem_zero(source, sizeof(*source));
    source->target = target; source->size = size; source->pd = pd;
    source->device.priv = source;
    source->device.read = x7w_source_read;
    source->device.seek = x7w_source_seek;
    source->device.seek64 = x7w_source_seek64;
    source->device.tell = x7w_source_tell;
    source->device.total_size = source->device.get_total_size =
        source->device.size = x7w_source_size;
}

static bool x7w_compress(x7w_entry *entry, x7w_source *source,
                          xx_io_device *destination, int level,
                          xx_pd_struct *pd) {
    if (entry->method == XX_7ZIP_METHOD_COPY) {
        uint8_t *buffer = (uint8_t *)xx_mem_alloc(65536U);
        bool okay = buffer != NULL;
        while (okay && source->position < source->size) {
            ssize_t amount = xx_io_read(&source->device, buffer, 65536U);
            okay = amount > 0 && xx_io_write(destination, buffer, (size_t)amount) == amount;
        }
        xx_mem_free(buffer);
        return okay;
    }
    if (entry->method == XX_7ZIP_METHOD_LZMA) {
        entry->properties_size = 5U;
        return xx_lzma_pack_device(&source->device, 0, source->size, destination,
                                   level, entry->properties, &entry->properties_size, pd) &&
               entry->properties_size == 5U;
    }
    if (entry->method == XX_7ZIP_METHOD_LZMA2) {
        entry->properties_size = 1U;
        return xx_lzma2_pack_device(&source->device, 0, source->size, destination,
                                    level, entry->properties, pd);
    }
    if (entry->method == XX_7ZIP_METHOD_BZIP2)
        return xx_bzip2_pack_device(&source->device, 0, source->size, destination, level, pd);
    if (entry->method == XX_7ZIP_METHOD_DEFLATE || entry->method == XX_7ZIP_METHOD_DEFLATE64)
        return xx_deflate_pack_device(&source->device, 0, source->size, destination,
                                      level, entry->method == XX_7ZIP_METHOD_DEFLATE64, pd);
    if (entry->method == XX_7ZIP_METHOD_PPMD7) {
        int order = level < 2 ? 2 : level > 64 ? 64 : level;
        entry->properties_size = 5U;
        entry->properties[0] = (uint8_t)order;
        x7w_put_le(entry->properties + 1U, 16U * 1024U * 1024U, 4U);
        return xx_ppmd7_compress_stream_sized(&source->device, NULL, 0U, 0,
                                             source->size, destination, NULL, 0U,
                                             NULL, order, 16U, pd);
    }
    if (entry->method == XX_7ZIP_METHOD_ZSTD) {
        entry->properties_size = 1U;
        entry->properties[0] = 0U; /* 7-Zip-zstd property: reserved flags */
        return xx_zstd_pack_device(&source->device, 0, source->size, destination, level, pd);
    }
    return false;
}

static void x7w_state_free(void *pointer) {
    x7w_state *writer = (x7w_state *)pointer;
    size_t index;
    if (!writer) return;
    for (index = 0U; index < writer->count; ++index) x7w_buffer_free(&writer->entries[index].name);
    xx_mem_free(writer->entries);
    xx_mem_free(writer);
}

static void x7w_write_state_free(xx_archive_write_state *state) {
    size_t index;
    if (!state) return;
    for (index = 0U; index < xx_list_size(&state->options); ++index) {
        xx_meta *item = (xx_meta *)xx_list_at(&state->options, index);
        volatile uint8_t *bytes = NULL;
        size_t size = 0U;
        if (!item || item->meta_id != XX_META_ID_OPT_PASSWORD || !item->var.is_allocated) continue;
        if (item->var.type == XX_VAR_TYPE_STRING) {
            bytes = (uint8_t *)item->var.val.str.ptr;
            size = item->var.val.str.len;
        } else if (item->var.type == XX_VAR_TYPE_WSTRING) {
            bytes = (uint8_t *)item->var.val.wstr.ptr;
            size = item->var.val.wstr.len * sizeof(wchar_t);
        } else if (item->var.type == XX_VAR_TYPE_BYTES) {
            bytes = item->var.val.bytes.data;
            size = item->var.val.bytes.size;
        }
        if (bytes) while (size--) *bytes++ = 0U;
    }
    xx_archive_write_state_free(state);
}

static bool x7w_copy_options(xx_list_s *destination, const xx_list_s *options,
                              xx_pd_struct *pd) {
    size_t index;
    for (index = 0U; options && index < xx_list_size(options); ++index) {
        const xx_meta *item = (const xx_meta *)xx_list_at(options, index);
        xx_meta copy;
        if (!item) return false;
        switch (item->meta_id) {
            case XX_META_ID_COMPRESSION_METHOD: case XX_META_ID_COMPRESSION_LEVEL:
            case XX_META_ID_OPT_PASSWORD: case XX_META_ID_ENCRYPTION_METHOD: break;
            default: return x7w_error(pd, "Unsupported 7z writer option");
        }
        xx_meta_init(&copy, item->meta_id);
        if (!xx_var_copy(&copy.var, &item->var) || !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

xx_archive_write_state *xx_7zip_create_archive_records_writing(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_write_state *state;
    x7w_state *writer;
    x7w_output output;
    uint8_t placeholder[32] = {0};
    uint64_t method;
    int level;
    const xx_var *password;
    bool encrypted;
    x7w_buffer password_check = {0};
    if (!self || !self->device || !self->device->write || self->base_address < 0 ||
        self->base_address > INT64_MAX - 32 || xx_pd_is_stopped(pd)) return NULL;
    state = (xx_archive_write_state *)xx_mem_alloc(sizeof(*state));
    writer = (x7w_state *)xx_mem_calloc(1U, sizeof(*writer));
    if (!state || !writer) { xx_mem_free(state); xx_mem_free(writer); return NULL; }
    xx_archive_write_state_init(state, self);
    state->internal_state = writer;
    state->free_internal = x7w_state_free;
    if (!x7w_copy_options(&state->options, options, pd) ||
        !x7w_settings(self, state, NULL, &method, &level, &password, &encrypted, pd) ||
        (encrypted && !x7w_text(password, true, &password_check)) ||
        xx_io_seek64(self->device, self->base_address, SEEK_SET) != 0) {
        x7w_secret_free(&password_check);
        x7w_write_state_free(state);
        return NULL;
    }
    x7w_secret_free(&password_check);
    /* The next write may overwrite a previously parsed archive even if the
     * destination then fails partway through the placeholder. */
    self->is_valid = false;
    self->is_crypted = false;
    self->base_info_handled = false;
    self->format_size = -1;
    x7w_output_init(&output, self->device, 32U, pd);
    if (xx_io_write(&output.device, placeholder, sizeof(placeholder)) != (ssize_t)sizeof(placeholder)) {
        x7w_write_state_free(state);
        return NULL;
    }
    writer->offset = self->base_address + 32;
    state->total_records = 0;
    return state;
}

bool xx_7zip_pack_archive_record(Abstractformat *self, xx_archive_write_state *state,
                                  const xx_archive_record *record,
                                  xx_io_device *source_dev, xx_pd_struct *pd) {
    x7w_state *writer;
    x7w_entry entry;
    const xx_var *value, *password;
    x7w_buffer password_bytes = {0};
    xx_io_device *owned = NULL, *temporary = NULL, *source = source_dev;
    x7w_source checked_source;
    x7w_output output, compressed;
    int64_t size = 0, encrypted_size = 0;
    int level;
    bool okay = false, started = false;
    char *path = NULL;
    if (!self || !self->device || !state || state->format != self ||
        !state->internal_state || !record || state->has_record) return false;
    writer = (x7w_state *)state->internal_state;
    if (writer->finalized || writer->failed || writer->count >= X7W_MAX_FILES) return false;
    if (xx_pd_is_stopped(pd)) { writer->failed = true; return false; }
    xx_mem_zero(&entry, sizeof(entry));
    entry.directory = xx_archive_record_get_meta_bool(record, XX_META_ID_IS_FOLDER, false);
    if (!x7w_settings(self, state, record, &entry.method, &level,
                      &password, &entry.encrypted, pd) || !x7w_name(&entry, record)) goto cleanup;
    if (entry.encrypted && !x7w_text(password, true, &password_bytes)) goto cleanup;
    if (writer->names_size > X7W_MAX_HEADER - entry.name.size) goto cleanup;
    if (xx_archive_record_find_meta(record, XX_META_ID_LINK_TARGET)) goto cleanup;
    value = xx_archive_record_find_meta(record, XX_META_ID_TIMESTAMP);
    if (value) {
        if (!x7w_unsigned(value, &entry.mtime)) goto cleanup;
        entry.mtime_defined = true;
    }
    value = xx_archive_record_find_meta(record, XX_META_ID_EXTERNAL_ATTRS);
    if (!value) value = xx_archive_record_find_meta(record, XX_META_ID_ATTRIBUTES);
    entry.attributes = entry.directory ? 0x10U : 0x20U;
    if (value) {
        uint64_t attributes;
        if (!x7w_unsigned(value, &attributes) || attributes > UINT32_MAX) goto cleanup;
        entry.attributes = (uint32_t)attributes;
    }
    if (entry.directory) entry.attributes |= 0x10U;
    else entry.attributes &= ~0x10U;
    value = xx_archive_record_find_meta(record, XX_META_ID_UNCOMPRESSED_SIZE);
    if (value) {
        uint64_t declared;
        if (!x7w_unsigned(value, &declared)) goto cleanup;
    }
    if (entry.directory) {
        if (source || (value && xx_var_get_u64(value) != 0U)) goto cleanup;
    } else {
        if (!source && !(value && xx_var_get_u64(value) == 0U)) {
            const char *original = xx_archive_record_get_original_name(record);
            if (original) path = xx_str_dup(original);
            else path = xx_str_unicode_to_utf8(xx_archive_record_get_original_name_w(record));
            if (!path) goto cleanup;
            source = owned = xx_io_file_open(path, "rb");
            if (!source) goto cleanup;
        }
        if (source) {
            if (source == self->device || (size = xx_io_total_size(source)) < 0 ||
                xx_io_seek64(source, 0, SEEK_SET) != 0) goto cleanup;
        }
        if (value) {
            uint64_t declared;
            if (!x7w_unsigned(value, &declared) || declared != (uint64_t)size) goto cleanup;
        }
    }
    entry.size = (uint64_t)size;
    if (writer->count == writer->capacity) {
        size_t capacity = writer->capacity ? writer->capacity * 2U : 16U;
        x7w_entry *entries;
        if (capacity > X7W_MAX_FILES) capacity = X7W_MAX_FILES;
        entries = (x7w_entry *)xx_mem_realloc(writer->entries, capacity * sizeof(*entries));
        if (!entries) goto cleanup;
        writer->entries = entries; writer->capacity = capacity;
    }
    if (entry.size) {
        if (xx_io_seek64(self->device, writer->offset, SEEK_SET) != 0) goto cleanup;
        state->has_record = true; started = true;
        x7w_source_init(&checked_source, source, size, pd);
        x7w_output_init(&output, self->device, (uint64_t)(INT64_MAX - writer->offset), pd);
        if (entry.encrypted) {
            temporary = xx_io_temp_open();
            if (!temporary) goto cleanup;
            x7w_output_init(&compressed, temporary, INT64_MAX, pd);
            if (!x7w_compress(&entry, &checked_source, &compressed.device, level, pd) ||
                compressed.failed || checked_source.position != size || compressed.written == 0U) goto cleanup;
            entry.codec_size = compressed.written;
            entry.aes_properties_size = sizeof(entry.aes_properties);
            if (!xx_7zip_aes_encrypt_device(temporary, 0, (int64_t)compressed.written,
                    password_bytes.data, password_bytes.size,
                    entry.aes_properties, &entry.aes_properties_size,
                    &output.device, &encrypted_size, pd) || encrypted_size < 0 ||
                (uint64_t)encrypted_size != output.written) goto cleanup;
        } else if (!x7w_compress(&entry, &checked_source, &output.device, level, pd)) goto cleanup;
        if (checked_source.position != size || output.failed || !output.written || xx_pd_is_stopped(pd)) goto cleanup;
        entry.crc = checked_source.crc;
        entry.packed = output.written;
        entry.packed_crc = output.crc;
        if (!entry.encrypted) entry.codec_size = entry.packed;
        writer->offset += (int64_t)entry.packed;
        ++writer->folders;
    }
    writer->entries[writer->count++] = entry;
    writer->names_size += entry.name.size;
    xx_mem_zero(&entry.name, sizeof(entry.name));
    state->current_index = (int64_t)writer->count - 1;
    state->total_records = (int64_t)writer->count;
    okay = true;
cleanup:
    state->has_record = false;
    if (!okay && (started || xx_pd_is_stopped(pd))) writer->failed = true;
    if (temporary && xx_io_close(temporary) != 0) { okay = false; writer->failed = true; }
    if (owned) xx_io_close(owned);
    xx_str_free(path);
    x7w_buffer_free(&entry.name);
    x7w_secret_free(&password_bytes);
    return okay;
}

static bool x7w_coder(x7w_buffer *header, uint64_t method,
                       const uint8_t *properties, size_t size) {
    uint8_t id[8];
    size_t bytes = 1U, index;
    uint64_t rest = method;
    while ((rest >>= 8U) != 0U) ++bytes;
    for (index = 0U; index < bytes; ++index)
        id[index] = (uint8_t)(method >> ((bytes - index - 1U) * 8U));
    return x7w_byte(header, (uint8_t)(bytes | (size ? 0x20U : 0U))) &&
           x7w_append(header, id, bytes) &&
           (!size || (x7w_number(header, size) && x7w_append(header, properties, size)));
}

static bool x7w_streams(x7w_buffer *header, const x7w_state *writer) {
    size_t index;
    if (!writer->folders) return true;
    if (!x7w_byte(header, XX_7ZIP_NID_MAIN_STREAMS_INFO) ||
        !x7w_byte(header, XX_7ZIP_NID_PACK_INFO) || !x7w_number(header, 0U) ||
        !x7w_number(header, writer->folders) || !x7w_byte(header, XX_7ZIP_NID_SIZE)) return false;
    for (index = 0U; index < writer->count; ++index)
        if (writer->entries[index].size && !x7w_number(header, writer->entries[index].packed)) return false;
    if (!x7w_byte(header, XX_7ZIP_NID_CRC) || !x7w_byte(header, 1U)) return false;
    for (index = 0U; index < writer->count; ++index)
        if (writer->entries[index].size && !x7w_le(header, writer->entries[index].packed_crc, 4U)) return false;
    if (!x7w_byte(header, XX_7ZIP_NID_END) || !x7w_byte(header, XX_7ZIP_NID_UNPACK_INFO) ||
        !x7w_byte(header, XX_7ZIP_NID_FOLDER) || !x7w_number(header, writer->folders) || !x7w_byte(header, 0U)) return false;
    for (index = 0U; index < writer->count; ++index) {
        const x7w_entry *entry = writer->entries + index;
        if (!entry->size) continue;
        if (!x7w_number(header, entry->encrypted ? 2U : 1U) ||
            (entry->encrypted && !x7w_coder(header, XX_7ZIP_METHOD_AES,
                 entry->aes_properties, entry->aes_properties_size)) ||
            !x7w_coder(header, entry->method, entry->properties, entry->properties_size) ||
            (entry->encrypted && (!x7w_number(header, 1U) || !x7w_number(header, 0U)))) return false;
    }
    if (!x7w_byte(header, XX_7ZIP_NID_CODERS_UNPACK_SIZE)) return false;
    for (index = 0U; index < writer->count; ++index) {
        const x7w_entry *entry = writer->entries + index;
        if (!entry->size) continue;
        if ((entry->encrypted && !x7w_number(header, entry->codec_size)) ||
            !x7w_number(header, entry->size)) return false;
    }
    if (!x7w_byte(header, XX_7ZIP_NID_CRC) || !x7w_byte(header, 1U)) return false;
    for (index = 0U; index < writer->count; ++index)
        if (writer->entries[index].size && !x7w_le(header, writer->entries[index].crc, 4U)) return false;
    return x7w_byte(header, XX_7ZIP_NID_END) &&
           x7w_byte(header, XX_7ZIP_NID_SUBSTREAMS_INFO) && x7w_byte(header, XX_7ZIP_NID_END) &&
           x7w_byte(header, XX_7ZIP_NID_END);
}

static bool x7w_bits(x7w_buffer *data, const x7w_state *writer, unsigned kind) {
    uint8_t byte = 0U, mask = 0x80U;
    size_t index;
    for (index = 0U; index < writer->count; ++index) {
        const x7w_entry *entry = writer->entries + index;
        bool set;
        if (kind == 1U && entry->size) continue;
        set = kind == 0U ? entry->size == 0U : kind == 1U ? !entry->directory : entry->mtime_defined;
        if (set) byte |= mask;
        mask >>= 1U;
        if (!mask) {
            if (!x7w_byte(data, byte)) return false;
            mask = 0x80U; byte = 0U;
        }
    }
    return mask == 0x80U || x7w_byte(data, byte);
}

static bool x7w_header(x7w_buffer *header, const x7w_state *writer) {
    x7w_buffer property = {0};
    size_t index, times = 0U;
    bool okay = false;
    if (!x7w_byte(header, XX_7ZIP_NID_HEADER) || !x7w_streams(header, writer) ||
        !x7w_byte(header, XX_7ZIP_NID_FILES_INFO) || !x7w_number(header, writer->count)) goto cleanup;
    if (writer->count != writer->folders) {
        if (!x7w_bits(&property, writer, 0U) ||
            !x7w_property(header, XX_7ZIP_NID_EMPTY_STREAM, &property)) goto cleanup;
        property.size = 0U;
        if (!x7w_bits(&property, writer, 1U) ||
            !x7w_property(header, XX_7ZIP_NID_EMPTY_FILE, &property)) goto cleanup;
        property.size = 0U;
    }
    if (!x7w_byte(&property, 0U)) goto cleanup;
    for (index = 0U; index < writer->count; ++index) {
        const x7w_entry *entry = writer->entries + index;
        if (!x7w_append(&property, entry->name.data, entry->name.size)) goto cleanup;
        if (entry->mtime_defined) ++times;
    }
    if (!x7w_property(header, XX_7ZIP_NID_NAME, &property)) goto cleanup;
    property.size = 0U;
    if (times) {
        if (!x7w_byte(&property, times == writer->count ? 1U : 0U) ||
            (times != writer->count && !x7w_bits(&property, writer, 2U)) ||
            !x7w_byte(&property, 0U)) goto cleanup;
        for (index = 0U; index < writer->count; ++index)
            if (writer->entries[index].mtime_defined && !x7w_le(&property, writer->entries[index].mtime, 8U)) goto cleanup;
        if (!x7w_property(header, XX_7ZIP_NID_MTIME, &property)) goto cleanup;
        property.size = 0U;
    }
    if (writer->count) {
        if (!x7w_byte(&property, 1U) || !x7w_byte(&property, 0U)) goto cleanup;
        for (index = 0U; index < writer->count; ++index)
            if (!x7w_le(&property, writer->entries[index].attributes, 4U)) goto cleanup;
        if (!x7w_property(header, XX_7ZIP_NID_WIN_ATTRIBUTES, &property)) goto cleanup;
    }
    okay = x7w_byte(header, XX_7ZIP_NID_END) && x7w_byte(header, XX_7ZIP_NID_END);
cleanup:
    x7w_buffer_free(&property);
    return okay;
}

bool xx_7zip_finalize_archive_records_writing(Abstractformat *self,
                                               xx_archive_write_state *state,
                                               xx_pd_struct *pd) {
    x7w_state *writer;
    x7w_buffer header = {0};
    x7w_output output;
    uint8_t signature[32] = {0};
    uint32_t header_crc;
    int64_t end;
    bool okay = false;
    if (!self || !self->device || !state || state->format != self ||
        !state->internal_state || state->has_record) return false;
    writer = (x7w_state *)state->internal_state;
    if (writer->failed) return false;
    if (writer->finalized) return true;
    if (xx_pd_is_stopped(pd)) { writer->failed = true; return false; }
    if (!x7w_header(&header, writer) || header.size > (uint64_t)(INT64_MAX - writer->offset)) goto cleanup;
    end = writer->offset + (int64_t)header.size;
    header_crc = xx_crc32_calc(0U, header.data, header.size);
    if (xx_io_seek64(self->device, writer->offset, SEEK_SET) != 0) goto cleanup;
    x7w_output_init(&output, self->device, header.size, pd);
    if (xx_io_write(&output.device, header.data, header.size) != (ssize_t)header.size) goto cleanup;
    xx_rt_memcpy(signature, XX_7ZIP_SIGNATURE, XX_7ZIP_SIGNATURE_SIZE);
    signature[7] = 4U;
    x7w_put_le(signature + 12U, (uint64_t)(writer->offset - self->base_address - 32), 8U);
    x7w_put_le(signature + 20U, header.size, 8U);
    x7w_put_le(signature + 28U, header_crc, 4U);
    x7w_put_le(signature + 8U, xx_crc32_calc(0U, signature + 12U, 20U), 4U);
    if (xx_pd_is_stopped(pd) || xx_io_seek64(self->device, self->base_address, SEEK_SET) != 0) goto cleanup;
    x7w_output_init(&output, self->device, sizeof(signature), pd);
    if (xx_io_write(&output.device, signature, sizeof(signature)) != (ssize_t)sizeof(signature) ||
        xx_io_seek64(self->device, end, SEEK_SET) != 0 || xx_pd_is_stopped(pd)) goto cleanup;
    writer->finalized = true;
    self->format_size = end - self->base_address;
    self->number_of_archive_records = writer->count;
    self->is_valid = true;
    self->is_crypted = false;
    {
        size_t index;
        int64_t device_size = xx_io_total_size(self->device);
        for (index = 0U; index < writer->count; ++index) {
            if (writer->entries[index].size && writer->entries[index].encrypted) {
                self->is_crypted = true;
                break;
            }
        }
        self->overlay_offset = device_size > end ? end : -1;
        self->overlay_size = device_size > end ? device_size - end : 0;
    }
    self->base_info_handled = false;
    ((xx_7zip *)self)->number_of_records = writer->count;
    ((xx_7zip *)self)->next_header_offset = writer->offset;
    ((xx_7zip *)self)->next_header_size = header.size;
    ((xx_7zip *)self)->next_header_crc = header_crc;
    ((xx_7zip *)self)->is_header_encoded = false;
    okay = true;
cleanup:
    if (!okay) writer->failed = true;
    x7w_buffer_free(&header);
    return okay;
}

void xx_7zip_free_archive_records_writing(Abstractformat *self,
                                           xx_archive_write_state *state) {
    (void)self;
    x7w_write_state_free(state);
}
