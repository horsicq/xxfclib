/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * QNAP NAS (QTS) firmware images: a gzip stream whose first encrypted_len
 * bytes are enciphered, followed by a 74-byte "icpnas" footer.
 * xx_qnap_nas_firmware.h carries the field table.
 *
 * The cipher is QNAP's variant of Alexander Pukall's PC1 byte stream cipher,
 * as documented by unblob 26.6.4 handlers/archive/qnap/_qnap.py (MIT) and the
 * public gist it cites.  It is written here from that description:
 *
 *   The key is read as n = len/2 16-bit words, word i being
 *   (sext(key[2i] ^ acc) << 8) + (key[2i+1] ^ acc), where sext() treats the
 *   byte as a signed char minus one when it is >= 0x80 (a quirk of the
 *   original C).  Chaining x_i = y_{i-1} ^ word_i (y_{-1} = 0) gives
 *   y_i = 0x4E35 * x_i + 1 and z_i = 0x15A * x_i (all mod 2^16).
 *   Per byte, with running state Y, Z and the plaintext accumulator acc:
 *     for i: prev = Y; Y = z_i; Z = Y + prev + 0x4E35 * (Z + i);
 *            res ^= y_i ^ Z
 *     k = (res >> 8) ^ (res & 0xFF);  plain = cipher ^ k;  acc ^= plain.
 *   The (y_i, z_i) pairs depend only on acc, so they are tabulated per acc.
 *
 * Detection needs the enciphered gzip magic at offset 0 and a well-formed
 * footer at the end of the device; both are fixed-size reads, so the probe
 * costs two small reads whatever the file size.  Unpacking streams the
 * payload through a 64 KiB buffer and never holds more than that.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/qnap_nas_firmware/xx_qnap_nas_firmware.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

/* Registration placeholder; picks up the real file type once
 * QNAP_NAS_FIRMWARE is registered in xxfc_defs.h. */
#ifdef QNAP_NAS_FIRMWARE
#define XX_QNAP_NAS_FIRMWARE_FILE_TYPE XX_FILE_TYPE_QNAP_NAS_FIRMWARE
#else
#define XX_QNAP_NAS_FIRMWARE_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define QNAP_FOOTER_SIZE 74
#define QNAP_FIELD_SIZE 16
#define QNAP_KEY_WORDS 7 /* "QNAPNASVERSION<d>" is 15 bytes: 15/2 words. */
#define QNAP_CHUNK ((size_t)64U * 1024U)
#define QNAP_PAYLOAD_NAME "firmware.tgz"

static const uint8_t qnap_head_magic[4] = {0xF5, 0x7B, 0x47, 0x03};
static const uint8_t qnap_footer_magic[6] = {'i', 'c', 'p', 'n', 'a', 's'};
/* Only the first 2 * QNAP_KEY_WORDS bytes of the secret are ever used. */
static const uint8_t qnap_secret[2 * QNAP_KEY_WORDS] = {
    'Q', 'N', 'A', 'P', 'N', 'A', 'S', 'V', 'E', 'R', 'S', 'I', 'O', 'N'};

typedef struct qnap_context_s {
    int64_t payload_offset; /**< Device offset of the payload (base). */
    int64_t payload_size;
    uint32_t encrypted_len;
    char fields[4][QNAP_FIELD_SIZE + 1];
} qnap_context;

typedef struct qnap_stream_s {
    qnap_context context;
    size_t index;
    size_t count;
} qnap_stream;

typedef struct qnap_cipher_s {
    uint16_t y[256][QNAP_KEY_WORDS];
    uint16_t z[256][QNAP_KEY_WORDS];
    uint16_t state_y;
    uint16_t state_z;
    uint8_t acc;
} qnap_cipher;

static void qnap_cipher_init(qnap_cipher *cipher) {
    unsigned acc, i;
    for (acc = 0U; acc < 256U; ++acc) {
        uint16_t prev = 0U;
        for (i = 0U; i < QNAP_KEY_WORDS; ++i) {
            unsigned hi = (unsigned)qnap_secret[2U * i] ^ acc;
            unsigned lo = (unsigned)qnap_secret[2U * i + 1U] ^ acc;
            uint16_t word = (uint16_t)((hi << 8U) + lo -
                                       (hi >= 0x80U ? 0x100U : 0U));
            uint16_t x = (uint16_t)(prev ^ word);
            cipher->y[acc][i] = (uint16_t)(0x4E35U * (uint32_t)x + 1U);
            cipher->z[acc][i] = (uint16_t)(0x15AU * (uint32_t)x);
            prev = cipher->y[acc][i];
        }
    }
    cipher->state_y = 0U;
    cipher->state_z = 0U;
    cipher->acc = 0U;
}

static void qnap_cipher_decrypt(qnap_cipher *cipher, uint8_t *data,
                                size_t size) {
    size_t n;
    for (n = 0U; n < size; ++n) {
        const uint16_t *ys = cipher->y[cipher->acc];
        const uint16_t *zs = cipher->z[cipher->acc];
        uint16_t res = 0U, sy = cipher->state_y, sz = cipher->state_z;
        unsigned i;
        uint8_t plain;
        for (i = 0U; i < QNAP_KEY_WORDS; ++i) {
            uint16_t prev = sy;
            sy = zs[i];
            sz = (uint16_t)(sy + prev + 0x4E35U * (uint32_t)(uint16_t)(sz + i));
            res = (uint16_t)(res ^ ys[i] ^ sz);
        }
        cipher->state_y = sy;
        cipher->state_z = sz;
        plain = (uint8_t)(data[n] ^ (uint8_t)((res >> 8U) ^ (res & 0xFFU)));
        data[n] = plain;
        cipher->acc = (uint8_t)(cipher->acc ^ plain);
    }
}

static bool qnap_read_at(xx_io_device *device, int64_t offset, void *buffer,
                         size_t size) {
    size_t done = 0U;
    if (!device || (!buffer && size != 0U) || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0)
        return false;
    while (done < size) {
        ssize_t amount = xx_io_read(device, (uint8_t *)buffer + done,
                                    size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

/* A footer text field: printable text up to the first NUL.  The bytes after
 * the terminator are padding and are not inspected. */
static bool qnap_field(const uint8_t *raw, char *out) {
    size_t i;
    for (i = 0U; i < QNAP_FIELD_SIZE && raw[i]; ++i) {
        if (raw[i] < 0x20U || raw[i] == 0x7FU) return false;
        out[i] = (char)raw[i];
    }
    out[i] = '\0';
    return true;
}

static bool qnap_parse(Abstractformat *format, qnap_context *out) {
    uint8_t head[4];
    uint8_t footer[QNAP_FOOTER_SIZE];
    qnap_context context;
    int64_t total, size;
    unsigned f;
    if (!format || !format->device || !out || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size < QNAP_FOOTER_SIZE + 4 ||
        !qnap_read_at(format->device, format->base_address, head,
                      sizeof(head)) ||
        xx_rt_memcmp(head, qnap_head_magic, sizeof(head)) != 0 ||
        !qnap_read_at(format->device, total - QNAP_FOOTER_SIZE, footer,
                      sizeof(footer)) ||
        xx_rt_memcmp(footer, qnap_footer_magic, sizeof(qnap_footer_magic)) !=
            0)
        return false;
    xx_mem_zero(&context, sizeof(context));
    context.payload_offset = format->base_address;
    context.payload_size = size - QNAP_FOOTER_SIZE;
    context.encrypted_len = (uint32_t)footer[6] | ((uint32_t)footer[7] << 8U) |
                            ((uint32_t)footer[8] << 16U) |
                            ((uint32_t)footer[9] << 24U);
    /* The enciphered prefix has to cover the gzip magic and stay in front of
     * the footer. */
    if (context.encrypted_len < 4U ||
        (int64_t)context.encrypted_len > context.payload_size)
        return false;
    for (f = 0U; f < 4U; ++f)
        if (!qnap_field(footer + 10U + f * QNAP_FIELD_SIZE,
                        context.fields[f]))
            return false;
    *out = context;
    return true;
}

static bool qnap_copy_options(xx_list_s *destination,
                              const xx_list_s *source) {
    size_t index;
    if (!source) return true;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *original =
            (const xx_meta *)xx_list_at((const xx_list_t *)source, index);
        xx_meta copy;
        if (!original) continue;
        xx_meta_init(&copy, original->meta_id);
        if (!xx_var_copy(&copy.var, &original->var) ||
            !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

static const xx_var *qnap_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool qnap_set_record(xx_archive_record *record,
                            const qnap_context *context) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = context->payload_offset + context->payload_size;
    record->header_size = QNAP_FOOTER_SIZE;
    record->data_offset = context->payload_offset;
    record->compressed_size = context->payload_size;
    return xx_archive_record_set_original_name(record, QNAP_PAYLOAD_NAME) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)context->payload_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)context->payload_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          0U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

void xx_qnap_nas_firmware_init(xx_qnap_nas_firmware *archive,
                               xx_io_device *device, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_QNAP_NAS_FIRMWARE_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/octet-stream");
    xx_format_set_extension(&archive->format, "img");
    archive->format.check_is_valid = xx_qnap_nas_firmware_check_is_valid;
    archive->format.handle_base_info = xx_qnap_nas_firmware_handle_base_info;
    archive->format.get_format_size = xx_qnap_nas_firmware_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_qnap_nas_firmware_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_qnap_nas_firmware_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_qnap_nas_firmware_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_qnap_nas_firmware_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_qnap_nas_firmware_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_qnap_nas_firmware_free_archive_records_reading;
}

xx_qnap_nas_firmware *xx_qnap_nas_firmware_create(xx_io_device *device,
                                                  int64_t base_address) {
    xx_qnap_nas_firmware *archive =
        (xx_qnap_nas_firmware *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_qnap_nas_firmware_init(archive, device, base_address);
    return archive;
}

void xx_qnap_nas_firmware_destroy(xx_qnap_nas_firmware *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_qnap_nas_firmware_free(xx_qnap_nas_firmware *archive) {
    if (!archive) return;
    xx_qnap_nas_firmware_destroy(archive);
    xx_mem_free(archive);
}

bool xx_qnap_nas_firmware_check_is_valid(Abstractformat *format,
                                         xx_pd_struct *pd) {
    qnap_context context;
    (void)pd;
    return qnap_parse(format, &context);
}

bool xx_qnap_nas_firmware_handle_base_info(Abstractformat *format,
                                           xx_pd_struct *pd) {
    qnap_context context;
    xx_qnap_nas_firmware *archive;
    (void)pd;
    if (!format || !qnap_parse(format, &context)) return false;
    archive = (xx_qnap_nas_firmware *)format;
    archive->number_of_records = 1U;
    archive->payload_size = (uint64_t)context.payload_size;
    archive->encrypted_len = context.encrypted_len;
    xx_rt_memcpy(archive->device_id, context.fields[0], QNAP_FIELD_SIZE + 1);
    xx_rt_memcpy(archive->file_version, context.fields[1],
                 QNAP_FIELD_SIZE + 1);
    xx_rt_memcpy(archive->firmware_date, context.fields[2],
                 QNAP_FIELD_SIZE + 1);
    xx_rt_memcpy(archive->revision, context.fields[3], QNAP_FIELD_SIZE + 1);
    xx_format_set_version(format, archive->file_version);
    format->number_of_archive_records = 1U;
    format->format_size = context.payload_size + QNAP_FOOTER_SIZE;
    format->is_valid = true;
    format->base_info_handled = true;
    return true;
}

int64_t xx_qnap_nas_firmware_get_format_size(Abstractformat *format,
                                             xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_qnap_nas_firmware_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_qnap_nas_firmware_get_number_of_archive_records(
    Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_qnap_nas_firmware_handle_base_info(format, pd))
               ? ((xx_qnap_nas_firmware *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_qnap_nas_firmware_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    qnap_stream *stream;
    xx_archive_record_state *state;
    qnap_context context;
    (void)pd;
    if (!qnap_parse(format, &context)) return NULL;
    stream = (qnap_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return NULL;
    stream->context = context;
    stream->count = 1U;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_mem_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = xx_mem_free;
    state->total_records = 1U;
    if (!qnap_copy_options(&state->options, options) ||
        !qnap_set_record(&state->current_record, &stream->context)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_qnap_nas_firmware_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_qnap_nas_firmware_archive_record_move_to_next(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    qnap_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (qnap_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    return false;
}

/* Decipher the enciphered prefix and copy the clear remainder, chunk by
 * chunk.  `destination` may be NULL: the payload is then only read through,
 * which is what an unpack without a destination path reports on. */
static bool qnap_unpack_to_device(Abstractformat *format,
                                  const qnap_context *context,
                                  xx_io_device *destination,
                                  xx_pd_struct *pd) {
    qnap_cipher *cipher;
    uint8_t *buffer;
    int64_t position = 0;
    bool result = false;
    cipher = (qnap_cipher *)xx_mem_alloc(sizeof(*cipher));
    buffer = (uint8_t *)xx_mem_alloc(QNAP_CHUNK);
    if (!cipher || !buffer) goto done;
    qnap_cipher_init(cipher);
    while (position < context->payload_size) {
        int64_t left = context->payload_size - position;
        size_t want = left < (int64_t)QNAP_CHUNK ? (size_t)left : QNAP_CHUNK;
        size_t done_bytes = 0U;
        if (pd && xx_pd_is_stopped(pd)) goto done;
        /* Do not let one chunk straddle the enciphered/clear boundary. */
        if (position < (int64_t)context->encrypted_len &&
            (int64_t)want > (int64_t)context->encrypted_len - position)
            want = (size_t)((int64_t)context->encrypted_len - position);
        if (!qnap_read_at(format->device, context->payload_offset + position,
                          buffer, want)) goto done;
        if (position < (int64_t)context->encrypted_len)
            qnap_cipher_decrypt(cipher, buffer, want);
        while (destination && done_bytes < want) {
            ssize_t amount = xx_io_write(destination, buffer + done_bytes,
                                         want - done_bytes);
            if (amount <= 0 || (size_t)amount > want - done_bytes) goto done;
            done_bytes += (size_t)amount;
        }
        position += (int64_t)want;
    }
    result = true;
done:
    if (buffer) xx_mem_free(buffer);
    if (cipher) xx_mem_free(cipher);
    return result;
}

bool xx_qnap_nas_firmware_unpack_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    qnap_stream *stream;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (qnap_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    path_option = qnap_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option)
        return qnap_unpack_to_device(format, &stream->context, NULL, pd);
    if (path_option->type == XX_VAR_TYPE_STRING ||
        path_option->type == XX_VAR_TYPE_STRING_VIEW)
        base = xx_var_get_str(path_option);
    else if (path_option->type == XX_VAR_TYPE_WSTRING ||
             path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base = owned_base;
    }
    if (!base) goto done;
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' &&
            base[xx_str_len(base) - 1U] != '\\')
               ? xx_str_concat3(base, "/", QNAP_PAYLOAD_NAME)
               : xx_str_concat(base, QNAP_PAYLOAD_NAME);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        created = destination != NULL;
        if (!destination) goto done;
        result = qnap_unpack_to_device(format, &stream->context, destination,
                                       pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_qnap_nas_firmware_free_archive_records_reading(
    Abstractformat *format, xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
