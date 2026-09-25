/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Native reader for the InstallShield compiled InstallScript file (".inx",
 * and the identical "Binary.InstallScript" stream).
 *
 * The layout was recovered from the 51 samples in F:\ARC\ARC\IS7 INX; no
 * published description of the container was used, because none of the
 * InstallShield tooling documented elsewhere (unshield, Deark's is_z and
 * is_cab modules) covers the compiled script.  Every byte of the file is
 * obfuscated with a position keyed transform:
 *
 *     plain[i] = ROR8(raw[i] ^ 0xF1, 2) - (i % 71)
 *
 * which was fixed by solving it against the plain text banner the header
 * carries, and then confirmed over the whole corpus: it turns files whose
 * raw byte entropy is 7.33 bits into images of 4.76 bits that contain the
 * script's identifier and function name tables.
 *
 * De-obfuscated header:
 *   0x00 u32  tag; the raw bytes are the file's magic, 0x842CC474
 *   0x04 u16  0
 *   0x06 char "Copyright (c) 1990-<year> InstallShield Software Corp. ..."
 *   0x4F      zero padding to 0x68
 *   0x68 u32  0x0000007C in every sample
 *   0x6C u32  \
 *   0x70 u32   | four offsets into the script image, each within the file
 *   0x74 u32   |
 *   0x78 u32  /
 *
 * This is a single stream, not a member container: the file has no member
 * table and nothing in it carries a stored name.  The reader therefore
 * publishes exactly one record, holding the de-obfuscated script, under a
 * fixed name that is never taken from the file.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/is7inx/xx_is7inx.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the file-type constant resolves to UNKNOWN until the enumerator
 * lands.  Delete this block once XX_FILE_TYPE_IS7INX exists in the enum. */
#ifdef IS7INX
#define XX_IS7INX_FILE_TYPE XX_FILE_TYPE_IS7INX
#else
#define XX_IS7INX_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define IS7INX_SIGNATURE UINT32_C(0x842CC474)
#define IS7INX_KEY_XOR 0xF1U
#define IS7INX_KEY_PERIOD 71U
#define IS7INX_HEADER_SIZE 0x7CU
#define IS7INX_BANNER_OFFSET 6U
#define IS7INX_BANNER "Copyright (c) "
#define IS7INX_BANNER_SIZE 14U
#define IS7INX_FIXED_FIELD_OFFSET 0x68U
#define IS7INX_FIXED_FIELD_VALUE UINT32_C(0x0000007C)
#define IS7INX_OFFSET_COUNT 4U
/* Refuse to materialise a script larger than this in one piece. */
#define IS7INX_MAX_SIZE UINT64_C(0x8000000)
/** Name given to the de-obfuscated stream.  Never taken from the file. */
#define IS7INX_PAYLOAD_NAME "installscript.bin"

typedef struct is7inx_parsed_s {
    int64_t script_size;
    uint32_t offsets[IS7INX_OFFSET_COUNT];
    uint32_t fixed_field;
} is7inx_parsed;

typedef struct is7inx_stream_s {
    is7inx_parsed parsed;
    char *name;
    size_t index;
} is7inx_stream;

static uint16_t is7inx_le16(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U));
}

static uint32_t is7inx_le32(const uint8_t *bytes) {
    return (uint32_t)is7inx_le16(bytes) |
           ((uint32_t)is7inx_le16(bytes + 2U) << 16U);
}

static bool is7inx_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

/* Undo the obfuscation in place.  "position" is the offset of buffer[0]
 * within the script, because the key depends on the absolute position. */
static void is7inx_deobfuscate(uint8_t *buffer, size_t size,
                               uint64_t position) {
    size_t index;
    unsigned phase = (unsigned)(position % IS7INX_KEY_PERIOD);
    for (index = 0U; index < size; ++index) {
        unsigned value = (unsigned)(buffer[index] ^ IS7INX_KEY_XOR) & 0xFFU;
        value = ((value >> 2U) | (value << 6U)) & 0xFFU;
        buffer[index] = (uint8_t)((value - phase) & 0xFFU);
        if (++phase == IS7INX_KEY_PERIOD) phase = 0U;
    }
}

static bool is7inx_parse(Abstractformat *format, is7inx_parsed *parsed) {
    uint8_t header[IS7INX_HEADER_SIZE];
    int64_t total, size;
    uint32_t index;
    if (!format || !format->device || !parsed || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size < (int64_t)IS7INX_HEADER_SIZE ||
        !is7inx_read_at(format->device, format->base_address, header,
                        sizeof(header)) ||
        is7inx_le32(header) != IS7INX_SIGNATURE)
        return false;
    is7inx_deobfuscate(header, sizeof(header), 0U);
    /* Three independent anchors have to agree before the file is accepted:
     * the banner the obfuscation hides, the zero padding that follows it,
     * and the constant field at 0x68. */
    if (header[4] != 0U || header[5] != 0U ||
        xx_rt_memcmp(header + IS7INX_BANNER_OFFSET, IS7INX_BANNER,
                     IS7INX_BANNER_SIZE) != 0)
        return false;
    for (index = 0x4FU; index < IS7INX_FIXED_FIELD_OFFSET; ++index)
        if (header[index] != 0U) return false;
    if (is7inx_le32(header + IS7INX_FIXED_FIELD_OFFSET) !=
        IS7INX_FIXED_FIELD_VALUE)
        return false;
    xx_mem_zero(parsed, sizeof(*parsed));
    parsed->fixed_field = IS7INX_FIXED_FIELD_VALUE;
    for (index = 0U; index < IS7INX_OFFSET_COUNT; ++index) {
        uint32_t value =
            is7inx_le32(header + IS7INX_FIXED_FIELD_OFFSET + 4U + index * 4U);
        /* Every section offset the header publishes must land inside the
         * file; a table that points outside it makes the file invalid
         * rather than truncated. */
        if (value == 0U || (int64_t)value > size) return false;
        parsed->offsets[index] = value;
    }
    parsed->script_size = size;
    return true;
}

static void is7inx_stream_free(void *opaque) {
    is7inx_stream *stream = (is7inx_stream *)opaque;
    if (!stream) return;
    if (stream->name) xx_str_free(stream->name);
    xx_mem_free(stream);
}

static bool is7inx_copy_options(xx_list_s *destination,
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

static const xx_var *is7inx_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool is7inx_set_record(xx_archive_record *record,
                              const is7inx_stream *stream,
                              int64_t base_address) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = base_address;
    record->header_size = IS7INX_HEADER_SIZE;
    record->data_offset = base_address;
    record->compressed_size = stream->parsed.script_size;
    return xx_archive_record_set_original_name(record, stream->name) &&
           xx_archive_record_set_meta_u64(
               record, XX_META_ID_COMPRESSED_SIZE,
               (uint64_t)stream->parsed.script_size) &&
           xx_archive_record_set_meta_u64(
               record, XX_META_ID_UNCOMPRESSED_SIZE,
               (uint64_t)stream->parsed.script_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          0U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

static bool is7inx_decode(Abstractformat *format, const is7inx_parsed *parsed,
                          uint8_t **plain, size_t *plain_size) {
    uint8_t *output;
    size_t size;
    if (!format || !parsed || !plain || !plain_size ||
        parsed->script_size <= 0 ||
        (uint64_t)parsed->script_size > IS7INX_MAX_SIZE ||
        (uint64_t)parsed->script_size > SIZE_MAX)
        return false;
    size = (size_t)parsed->script_size;
    output = (uint8_t *)xx_mem_alloc(size);
    if (!output) return false;
    if (!is7inx_read_at(format->device, format->base_address, output, size)) {
        xx_mem_free(output);
        return false;
    }
    is7inx_deobfuscate(output, size, 0U);
    *plain = output;
    *plain_size = size;
    return true;
}

void xx_is7inx_init(xx_is7inx *script, xx_io_device *device,
                    int64_t base_address) {
    if (!script) return;
    xx_mem_zero(script, sizeof(*script));
    xx_format_init(&script->format, device, base_address);
    script->format.endian = XX_ENDIAN_LITTLE;
    script->format.file_type = XX_IS7INX_FILE_TYPE;
    script->format.format_type = XX_TYPE_ARCHIVE;
    script->format.is_archive = true;
    xx_format_set_mime_type(&script->format,
                            "application/x-installshield-script");
    xx_format_set_extension(&script->format, "inx");
    script->format.check_is_valid = xx_is7inx_check_is_valid;
    script->format.handle_base_info = xx_is7inx_handle_base_info;
    script->format.get_format_size = xx_is7inx_get_format_size;
    script->format.get_number_of_archive_records =
        xx_is7inx_get_number_of_archive_records;
    script->format.create_archive_records_reading =
        xx_is7inx_create_archive_records_reading;
    script->format.get_current_archive_record =
        xx_is7inx_get_current_archive_record;
    script->format.unpack_current_archive_record =
        xx_is7inx_unpack_current_archive_record;
    script->format.archive_record_move_to_next =
        xx_is7inx_archive_record_move_to_next;
    script->format.free_archive_records_reading =
        xx_is7inx_free_archive_records_reading;
    script->archive_end = -1;
}

xx_is7inx *xx_is7inx_create(xx_io_device *device, int64_t base_address) {
    xx_is7inx *script = (xx_is7inx *)xx_mem_alloc(sizeof(*script));
    if (script) xx_is7inx_init(script, device, base_address);
    return script;
}

void xx_is7inx_destroy(xx_is7inx *script) {
    if (script) xx_format_cleanup_extra_parameters(&script->format);
}

void xx_is7inx_free(xx_is7inx *script) {
    if (!script) return;
    xx_is7inx_destroy(script);
    xx_mem_free(script);
}

bool xx_is7inx_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    is7inx_parsed parsed;
    (void)pd;
    return is7inx_parse(format, &parsed);
}

bool xx_is7inx_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    is7inx_parsed parsed;
    xx_is7inx *script;
    (void)pd;
    if (!format || !is7inx_parse(format, &parsed)) return false;
    script = (xx_is7inx *)format;
    script->number_of_records = 1U;
    script->script_version = parsed.fixed_field;
    script->archive_end = format->base_address + parsed.script_size;
    format->number_of_archive_records = 1U;
    format->format_size = parsed.script_size;
    format->is_valid = true;
    format->base_info_handled = true;
    return true;
}

int64_t xx_is7inx_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format &&
                   (format->base_info_handled ||
                    xx_is7inx_handle_base_info(format, pd))
               ? format->format_size
               : -1;
}

uint64_t xx_is7inx_get_number_of_archive_records(Abstractformat *format,
                                                 xx_pd_struct *pd) {
    return format &&
                   (format->base_info_handled ||
                    xx_is7inx_handle_base_info(format, pd))
               ? ((xx_is7inx *)format)->number_of_records
               : 0U;
}

xx_archive_record_state *xx_is7inx_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    is7inx_stream *stream;
    xx_archive_record_state *state;
    (void)pd;
    stream = (is7inx_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return NULL;
    if (!is7inx_parse(format, &stream->parsed) ||
        !(stream->name = xx_str_dup(IS7INX_PAYLOAD_NAME))) {
        is7inx_stream_free(stream);
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        is7inx_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = is7inx_stream_free;
    state->total_records = 1U;
    if (!is7inx_copy_options(&state->options, options) ||
        !is7inx_set_record(&state->current_record, stream,
                           format->base_address)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_is7inx_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_is7inx_archive_record_move_to_next(Abstractformat *format,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    is7inx_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (is7inx_stream *)state->internal_state) ||
        ++stream->index >= 1U) {
        if (state) state->has_record = false;
        return false;
    }
    return false;
}

bool xx_is7inx_unpack_current_archive_record(Abstractformat *format,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    is7inx_stream *stream;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U, written = 0U;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (is7inx_stream *)state->internal_state) ||
        stream->index != 0U || (pd && xx_pd_is_stopped(pd)))
        return false;
    if (!is7inx_decode(format, &stream->parsed, &plain, &plain_size))
        goto done;
    path_option = is7inx_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        result = true;
        goto done;
    }
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
               ? xx_str_concat3(base, "/", stream->name)
               : xx_str_concat(base, stream->name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        created = destination != NULL;
        if (!destination) goto done;
        result = true;
        while (written < plain_size) {
            ssize_t amount = xx_io_write(destination, plain + written,
                                         plain_size - written);
            if (amount <= 0 || (size_t)amount > plain_size - written) {
                result = false;
                break;
            }
            written += (size_t)amount;
        }
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (plain) xx_mem_free(plain);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_is7inx_free_archive_records_reading(Abstractformat *format,
                                            xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
