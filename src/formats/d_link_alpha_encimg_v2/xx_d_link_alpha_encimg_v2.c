/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * D-Link / Alpha Networks encrypted firmware, "encimg v2": a plain WRGG03
 * header (0xA0 bytes, OpenWrt mkwrggimg.c) and an AES-256-CBC payload.
 * xx_d_link_alpha_encimg_v2.h carries the field table.
 *
 * Ported from unblob's handlers/archive/dlink/alpha_encimg.py
 * (AlphaEncimgV2Handler / AlphaEncimgExtractorV2 / EncParamsV2), unblob
 * 26.6.4, MIT licence: the fixed V2 key and IV, the signature mangling of
 * both, the "smaller fsize wins" byte-order rule, the fsize checks and the
 * "first payload block must look encrypted" test (known plaintext magics,
 * Shannon entropy of the 16 bytes >= 3 bits).
 *
 * Differences from unblob, all on the strict side except the first:
 *  - the magic is accepted in either byte order (unblob's search pattern
 *    only has the little-endian bytes, but its header check admits both
 *    values, and mkwrggimg writes big-endian headers too);
 *  - both magic words must be identical;
 *  - the signature must be printable ASCII, start with "wap" (unblob's
 *    pattern), end with NUL padding only, and be non-empty;
 *  - fsize must be at least one AES block.
 *
 * The decrypted payload is written as "<signature>.bin" with any character
 * that is unsafe in a file name replaced by '_'.  There is no checksum over
 * the plaintext to verify the key against: the header "digest" field's
 * coverage is not documented for encrypted images, so it is not used.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/d_link_alpha_encimg_v2/xx_d_link_alpha_encimg_v2.h"

#include "xxfclib/algo/aes/xx_aes.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>
#include "xxfclib/data/xx_data.h"

#ifdef D_LINK_ALPHA_ENCIMG_V2
#define XX_D_LINK_ALPHA_ENCIMG_V2_FILE_TYPE XX_FILE_TYPE_D_LINK_ALPHA_ENCIMG_V2
#else
#define XX_D_LINK_ALPHA_ENCIMG_V2_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define ENCIMG2_HEADER XX_D_LINK_ALPHA_ENCIMG_V2_HEADER_SIZE
#define ENCIMG2_SIG_SIZE XX_D_LINK_ALPHA_ENCIMG_V2_SIGNATURE_SIZE
#define ENCIMG2_BLOCK 16U
#define ENCIMG2_CHUNK (64U * 1024U)
#define ENCIMG2_XOR_RANGE 0xFCU
#define ENCIMG2_NAME_MAX (ENCIMG2_SIG_SIZE + 5U)

/* EncParamsV2 (unblob, MIT). */
static const char encimg2_key[] = "oVhq0hvXHdfaGFLdubM4/QvuVHdKee7v";
static const char encimg2_iv[] = "0BO5nlYankuVBe4s";

/* unblob KNOWN_UNENCRYPTED: LZMA, FDT, and the DAP X2810/X2850 prefix. */
static const uint8_t encimg2_plain_magics[3][4] = {{0x5D, 0x00, 0x00, 0x80}, {0xD0, 0x0D, 0xFE, 0xED}, {0x10, 0xEC, 0x7A, 0x0D}};

/* round(c * log2(c) * 1e6) for c = 0..16.  For 16 bytes the Shannon entropy
 * is 4 - sum(c log2 c) / 16, so "entropy < 3" is "sum > 16e6". */
static const uint32_t encimg2_clog2c[17] = {0U,        0U,        2000000U,  4754888U,  8000000U,  11609640U, 15509775U, 19651484U, 24000000U,
                                            28529325U, 33219281U, 38053748U, 43019550U, 48105716U, 53302969U, 58603359U, 64000000U};

typedef struct encimg2_context_s {
    int64_t payload_offset;
    uint32_t payload_size;
    uint32_t offset_field;
    bool big_endian;
    size_t signature_length;
    uint8_t signature[ENCIMG2_SIG_SIZE + 1U];
    char version[17];
    char model[17];
    char buildno[17];
    char devname[33];
    char name[ENCIMG2_NAME_MAX + 1U];
} encimg2_context;

typedef struct encimg2_stream_s {
    encimg2_context context;
    size_t index;
    size_t count;
} encimg2_stream;

static bool encimg2_read_at(xx_io_device *device, int64_t offset, void *buffer, size_t size)
{
    size_t done = 0U;
    if (!device || (!buffer && size != 0U) || offset < 0 || xx_io_seek64(device, offset, SEEK_SET) != 0) return false;
    while (done < size) {
        ssize_t amount = xx_io_read(device, (uint8_t *)buffer + done, size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

/* A fixed text field: printable ASCII up to the first NUL (non-printable
 * bytes become '?'); only used for display. */
static void encimg2_text(char *out, const uint8_t *field, size_t size)
{
    size_t i;
    for (i = 0U; i < size && field[i] != 0U; ++i) out[i] = (field[i] >= 0x20U && field[i] < 0x7FU) ? (char)field[i] : '?';
    out[i] = '\0';
}

static bool encimg2_looks_plain(const uint8_t *block)
{
    uint8_t counts[256];
    uint32_t sum = 0U;
    size_t i;
    for (i = 0U; i < 3U; ++i)
        if (xx_rt_memcmp(block, encimg2_plain_magics[i], 4U) == 0) return true;
    xx_rt_memset(counts, 0, sizeof(counts));
    for (i = 0U; i < ENCIMG2_BLOCK; ++i) ++counts[block[i]];
    for (i = 0U; i < 256U; ++i) sum += encimg2_clog2c[counts[i]];
    return sum > 16000000U;
}

static bool encimg2_parse(Abstractformat *format, encimg2_context *out)
{
    uint8_t header[ENCIMG2_HEADER];
    uint8_t first[ENCIMG2_BLOCK];
    encimg2_context context;
    int64_t total, remaining;
    uint32_t magic_le, magic_be, size_le, size_be, size;
    size_t length, i;
    if (!format || !format->device || !out || format->base_address < 0) return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address || total - format->base_address < (int64_t)(ENCIMG2_HEADER + ENCIMG2_BLOCK)) return false;
    if (!encimg2_read_at(format->device, format->base_address, header, sizeof(header))) return false;
    if (header[0] != 'w' || header[1] != 'a' || header[2] != 'p') return false;
    if (xx_rt_memcmp(header + 32, header + 36, 4U) != 0) return false;
    magic_le = xx_data_get_u32(header + 32, 4, 0, false);
    magic_be = xx_data_get_u32(header + 32, 4, 0, true);
    xx_mem_zero(&context, sizeof(context));
    if (magic_le == XX_D_LINK_ALPHA_ENCIMG_V2_MAGIC) context.big_endian = false;
    else if (magic_be == XX_D_LINK_ALPHA_ENCIMG_V2_MAGIC) context.big_endian = true;
    else return false;

    /* Signature: printable up to the first NUL, NUL padding after it. */
    for (length = 0U; length < ENCIMG2_SIG_SIZE && header[length] != 0U; ++length)
        if (header[length] < 0x20U || header[length] >= 0x7FU) return false;
    for (i = length; i < ENCIMG2_SIG_SIZE; ++i)
        if (header[i] != 0U) return false;
    xx_rt_memcpy(context.signature, header, length);
    context.signature[length] = 0U;
    context.signature_length = length;

    /* unblob: both orders decoded, the smaller size is the right one. */
    size_le = xx_data_get_u32(header + 0x68, 4, 0, false);
    size_be = xx_data_get_u32(header + 0x68, 4, 0, true);
    size = size_le < size_be ? size_le : size_be;
    remaining = total - format->base_address - (int64_t)ENCIMG2_HEADER;
    if (size < ENCIMG2_BLOCK || (size % ENCIMG2_BLOCK) != 0U || (int64_t)size > remaining) return false;
    context.offset_field = (size == size_le) ? xx_data_get_u32(header + 0x6C, 4, 0, false) : xx_data_get_u32(header + 0x6C, 4, 0, true);
    context.payload_size = size;
    context.payload_offset = format->base_address + (int64_t)ENCIMG2_HEADER;

    if (!encimg2_read_at(format->device, context.payload_offset, first, sizeof(first)) || encimg2_looks_plain(first)) return false;

    encimg2_text(context.version, header + 0x28, 16U);
    encimg2_text(context.model, header + 0x38, 16U);
    encimg2_text(context.buildno, header + 0x58, 16U);
    encimg2_text(context.devname, header + 0x70, 32U);

    /* Member name: the signature with file-name-unsafe characters mapped. */
    for (i = 0U; i < length; ++i) {
        char c = (char)context.signature[i];
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|' || c == ' ') c = '_';
        context.name[i] = c;
    }
    xx_rt_memcpy(context.name + length, ".bin", 5U);
    *out = context;
    return true;
}

static void encimg2_derive(const encimg2_context *context, uint8_t key[32], uint8_t iv[16])
{
    size_t i;
    for (i = 0U; i < 32U; ++i) key[i] = (uint8_t)((uint8_t)encimg2_key[i] ^ (uint8_t)((i + 1U) % ENCIMG2_XOR_RANGE) ^ context->signature[i % context->signature_length]);
    for (i = 0U; i < 16U; ++i) iv[i] = (uint8_t)((uint8_t)encimg2_iv[i] ^ (uint8_t)((i + 1U) % ENCIMG2_XOR_RANGE) ^ context->signature[i % context->signature_length]);
}

static void encimg2_stream_free(void *opaque)
{
    if (opaque) xx_mem_free(opaque);
}

static bool encimg2_copy_options(xx_list_s *destination, const xx_list_s *source)
{
    size_t index;
    if (!source) return true;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *original = (const xx_meta *)xx_list_at((const xx_list_t *)source, index);
        xx_meta copy;
        if (!original) continue;
        xx_meta_init(&copy, original->meta_id);
        if (!xx_var_copy(&copy.var, &original->var) || !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

static const xx_var *encimg2_option(const xx_list_s *options, uint32_t id)
{
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta = (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool encimg2_set_record(xx_archive_record *record, const encimg2_context *context)
{
    char comment[160];
    (void)xx_rt_snprintf(comment, sizeof(comment),
                         "AES-256-CBC decrypted; model=%s version=%s build=%s "
                         "device=%s %s",
                         context->model, context->version, context->buildno, context->devname, context->big_endian ? "big-endian" : "little-endian");
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = context->payload_offset - (int64_t)ENCIMG2_HEADER;
    record->header_size = ENCIMG2_HEADER;
    record->data_offset = context->payload_offset;
    record->compressed_size = context->payload_size;
    return xx_archive_record_set_original_name(record, context->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE, (uint64_t)context->payload_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE, (uint64_t)context->payload_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD, 0U) && xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED, false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false) && xx_archive_record_set_meta_str(record, XX_META_ID_COMMENT, comment);
}

void xx_d_link_alpha_encimg_v2_init(xx_d_link_alpha_encimg_v2 *archive, xx_io_device *device, int64_t base_address)
{
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_D_LINK_ALPHA_ENCIMG_V2_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-dlink-encrypted-firmware");
    xx_format_set_extension(&archive->format, "bin");
    archive->format.check_is_valid = xx_d_link_alpha_encimg_v2_check_is_valid;
    archive->format.handle_base_info = xx_d_link_alpha_encimg_v2_handle_base_info;
    archive->format.get_format_size = xx_d_link_alpha_encimg_v2_get_format_size;
    archive->format.get_number_of_archive_records = xx_d_link_alpha_encimg_v2_get_number_of_archive_records;
    archive->format.create_archive_records_reading = xx_d_link_alpha_encimg_v2_create_archive_records_reading;
    archive->format.get_current_archive_record = xx_d_link_alpha_encimg_v2_get_current_archive_record;
    archive->format.unpack_current_archive_record = xx_d_link_alpha_encimg_v2_unpack_current_archive_record;
    archive->format.archive_record_move_to_next = xx_d_link_alpha_encimg_v2_archive_record_move_to_next;
    archive->format.free_archive_records_reading = xx_d_link_alpha_encimg_v2_free_archive_records_reading;
}

xx_d_link_alpha_encimg_v2 *xx_d_link_alpha_encimg_v2_create(xx_io_device *device, int64_t base_address)
{
    xx_d_link_alpha_encimg_v2 *archive = (xx_d_link_alpha_encimg_v2 *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_d_link_alpha_encimg_v2_init(archive, device, base_address);
    return archive;
}

void xx_d_link_alpha_encimg_v2_destroy(xx_d_link_alpha_encimg_v2 *archive)
{
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_d_link_alpha_encimg_v2_free(xx_d_link_alpha_encimg_v2 *archive)
{
    if (!archive) return;
    xx_d_link_alpha_encimg_v2_destroy(archive);
    xx_mem_free(archive);
}

bool xx_d_link_alpha_encimg_v2_check_is_valid(Abstractformat *format, xx_pd_struct *pd)
{
    encimg2_context context;
    (void)pd;
    return encimg2_parse(format, &context);
}

bool xx_d_link_alpha_encimg_v2_handle_base_info(Abstractformat *format, xx_pd_struct *pd)
{
    encimg2_context context;
    xx_d_link_alpha_encimg_v2 *archive;
    (void)pd;
    if (!format || !encimg2_parse(format, &context)) return false;
    archive = (xx_d_link_alpha_encimg_v2 *)format;
    archive->number_of_records = 1U;
    archive->payload_size = context.payload_size;
    archive->offset_field = context.offset_field;
    archive->header_big_endian = context.big_endian;
    xx_rt_memcpy(archive->signature, context.signature, context.signature_length + 1U);
    xx_rt_memcpy(archive->version, context.version, sizeof(archive->version));
    xx_rt_memcpy(archive->model, context.model, sizeof(archive->model));
    xx_rt_memcpy(archive->buildno, context.buildno, sizeof(archive->buildno));
    xx_rt_memcpy(archive->devname, context.devname, sizeof(archive->devname));
    format->endian = context.big_endian ? XX_ENDIAN_BIG : XX_ENDIAN_LITTLE;
    format->number_of_archive_records = 1U;
    format->format_size = (int64_t)ENCIMG2_HEADER + (int64_t)context.payload_size;
    format->is_valid = true;
    format->base_info_handled = true;
    return true;
}

int64_t xx_d_link_alpha_encimg_v2_get_format_size(Abstractformat *format, xx_pd_struct *pd)
{
    return format && (format->base_info_handled || xx_d_link_alpha_encimg_v2_handle_base_info(format, pd)) ? format->format_size : -1;
}

uint64_t xx_d_link_alpha_encimg_v2_get_number_of_archive_records(Abstractformat *format, xx_pd_struct *pd)
{
    return format && (format->base_info_handled || xx_d_link_alpha_encimg_v2_handle_base_info(format, pd)) ? ((xx_d_link_alpha_encimg_v2 *)format)->number_of_records
                                                                                                           : 0U;
}

xx_archive_record_state *xx_d_link_alpha_encimg_v2_create_archive_records_reading(Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd)
{
    encimg2_stream *stream;
    xx_archive_record_state *state;
    encimg2_context context;
    (void)pd;
    if (!encimg2_parse(format, &context)) return NULL;
    stream = (encimg2_stream *)xx_mem_calloc(1U, sizeof(*stream));
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
    state->free_internal = encimg2_stream_free;
    state->total_records = 1U;
    if (!encimg2_copy_options(&state->options, options) || !encimg2_set_record(&state->current_record, &stream->context)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_d_link_alpha_encimg_v2_get_current_archive_record(Abstractformat *format, xx_archive_record_state *state)
{
    return format && state && state->format == format && state->has_record ? &state->current_record : NULL;
}

bool xx_d_link_alpha_encimg_v2_archive_record_move_to_next(Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd)
{
    encimg2_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format || !(stream = (encimg2_stream *)state->internal_state) || ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    return false;
}

/* Decrypt the payload chunk by chunk; the IV of each chunk is the last
 * ciphertext block of the one before.  Memory: one 64 KiB buffer. */
static bool encimg2_decrypt_to_device(Abstractformat *format, const encimg2_context *context, xx_io_device *destination, xx_pd_struct *pd)
{
    uint8_t key[32], iv[16], next_iv[16];
    uint8_t *buffer;
    uint32_t left = context->payload_size;
    int64_t position = context->payload_offset;
    bool result = false;
    buffer = (uint8_t *)xx_mem_alloc(ENCIMG2_CHUNK);
    if (!buffer) return false;
    encimg2_derive(context, key, iv);
    while (left > 0U) {
        size_t amount = left < ENCIMG2_CHUNK ? (size_t)left : ENCIMG2_CHUNK;
        size_t done = 0U;
        if (pd && xx_pd_is_stopped(pd)) goto done;
        if (!encimg2_read_at(format->device, position, buffer, amount)) goto done;
        xx_rt_memcpy(next_iv, buffer + amount - ENCIMG2_BLOCK, ENCIMG2_BLOCK);
        if (!xx_aes_cbc_decrypt(buffer, amount, key, sizeof(key), iv, buffer)) goto done;
        while (done < amount) {
            ssize_t wrote = xx_io_write(destination, buffer + done, amount - done);
            if (wrote <= 0 || (size_t)wrote > amount - done) goto done;
            done += (size_t)wrote;
        }
        xx_rt_memcpy(iv, next_iv, ENCIMG2_BLOCK);
        position += (int64_t)amount;
        left -= (uint32_t)amount;
    }
    result = true;
done:
    xx_rt_memset(key, 0, sizeof(key));
    xx_mem_free(buffer);
    return result;
}

bool xx_d_link_alpha_encimg_v2_unpack_current_archive_record(Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd)
{
    encimg2_stream *stream;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record || !(stream = (encimg2_stream *)state->internal_state) || stream->index >= stream->count ||
        (pd && xx_pd_is_stopped(pd)))
        return false;
    path_option = encimg2_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) return encimg2_parse(format, &stream->context);
    if (path_option->type == XX_VAR_TYPE_STRING || path_option->type == XX_VAR_TYPE_STRING_VIEW) base = xx_var_get_str(path_option);
    else if (path_option->type == XX_VAR_TYPE_WSTRING || path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base = owned_base;
    }
    if (!base) goto done;
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' && base[xx_str_len(base) - 1U] != '\\') ? xx_str_concat3(base, "/", stream->context.name)
                                                                                                  : xx_str_concat(base, stream->context.name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        created = destination != NULL;
        if (!destination) goto done;
        result = encimg2_decrypt_to_device(format, &stream->context, destination, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_d_link_alpha_encimg_v2_free_archive_records_reading(Abstractformat *format, xx_archive_record_state *state)
{
    (void)format;
    xx_archive_record_state_free(state);
}
