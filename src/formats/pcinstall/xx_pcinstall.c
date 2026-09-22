/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * PC-Install installer data file (*.SHR) -- the compact single-volume shape of
 * the mid-1990s PC-Install setup builder.  This is the installer's DATA file,
 * not a self-extracting setup program.  The XArchive reference module
 * installers/xpcinstall.cpp covers the builder's other shape, the multi-volume
 * "[20/20]" .BND disk set with its 0x114-byte record headers; the .SHR files
 * in the reference corpus are this much smaller sibling, whose layout was
 * derived from the corpus and validated by exact chain tiling on every sample.
 *
 * Volume header (0x38 bytes, little endian):
 *   +0x00  13 zero bytes
 *   +0x0d  uint8  0x74          record tag, repeated at +0x10
 *   +0x0e  uint16 count         number of members in the chain
 *   +0x10  uint32 0x74
 *   +0x14  0x24 zero bytes
 *
 * Then `count` records, back to back, each:
 *   +0x00  char   name[0x14]    NUL terminated, DOS 8.3, no path separators
 *   +0x14  uint32 packed_size   length of the payload
 *   +0x18  uint16 dos_date
 *   +0x1a  uint16 dos_time
 *   +0x1c  0x14 zero bytes
 *   +0x30  payload              raw PKWARE DCL implode stream
 *
 * There is no index, no trailer and no per-record link field: the chain is
 * required to tile the file exactly from 0x38 to end of file and to hold
 * exactly the declared number of members.  Together with the zero-filled
 * header that is a far stronger test than any two-byte magic, which is just
 * as well because the container has no magic at all.
 *
 * The container stores no plaintext length, so it is discovered by measuring
 * the DCL stream.  That costs a decode and is therefore kept out of
 * check_is_valid().
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/pcinstall/xx_pcinstall.h"

#include "xxfclib/algo/dcl/xx_dcl.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>

#ifdef PCINSTALL
#define XX_PCINSTALL_FILE_TYPE XX_FILE_TYPE_PCINSTALL
#else
#define XX_PCINSTALL_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define PCI_VOLUME_HEADER_SIZE 0x38
#define PCI_RECORD_SIZE 0x30
#define PCI_NAME_FIELD_SIZE 0x14
#define PCI_COUNT_OFFSET 0x0e
#define PCI_TAG_OFFSET 0x0d
#define PCI_TAG UINT8_C(0x74)
#define PCI_SIZE_OFFSET 0x14
#define PCI_DATE_OFFSET 0x18
#define PCI_TIME_OFFSET 0x1a
/* The count is a uint16, so it cannot exceed this; the constant is spelled
 * out anyway so the bound survives a future widening of the field. */
#define PCI_MAX_RECORDS 65535U
/* This family is a floppy disk set, so no member can plausibly be huge.  The
 * cap exists only so a malformed header can never drive an allocation. */
#define PCI_MAX_RAW_SIZE (256 * 1024 * 1024)

typedef struct pci_member_s {
    char *name;
    int64_t header_offset;
    int64_t data_offset;
    int64_t packed_size;
    int64_t unpacked_size; /**< -1 while unresolved. */
    uint16_t dos_date;
    uint16_t dos_time;
} pci_member;

typedef struct pci_stream_s {
    pci_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
    uint16_t declared_count;
} pci_stream;

static uint16_t pci_le16(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U));
}

static uint32_t pci_le32(const uint8_t *bytes) {
    return (uint32_t)pci_le16(bytes) | ((uint32_t)pci_le16(bytes + 2U) << 16U);
}

static bool pci_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

static bool pci_range_within(int64_t total, int64_t offset, int64_t size) {
    return total >= 0 && offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Every name field is a fixed-width buffer whose tail is uninitialised
 * builder heap, so only the bytes before the first NUL may be surfaced.  A
 * field with no terminator, an empty name, a path separator or a byte outside
 * printable ASCII is not a record and ends the walk. */
static char *pci_fixed_name(const uint8_t *field, size_t size) {
    size_t length = 0U;
    char *name;
    size_t index;
    while (length < size && field[length] != 0U) ++length;
    if (length == 0U || length == size) return NULL;
    for (index = 0U; index < length; ++index) {
        uint8_t c = field[index];
        if (c < 0x20U || c > 0x7eU || c == '/' || c == '\\') return NULL;
    }
    name = (char *)xx_mem_alloc(length + 1U);
    if (!name) return NULL;
    for (index = 0U; index < length; ++index) {
        uint8_t c = field[index];
        name[index] = (c == ':' || c == '*' || c == '?' || c == '"' ||
                       c == '<' || c == '>' || c == '|')
                          ? '_'
                          : (char)c;
    }
    name[length] = 0;
    return name;
}

static bool pci_safe_output_name(const char *name) {
    if (!name || !name[0] || name[1] == ':') return false;
    if (name[0] == '.' && (name[1] == 0 || (name[1] == '.' && name[2] == 0)))
        return false;
    return true;
}

static void pci_stream_free(void *opaque) {
    pci_stream *stream = (pci_stream *)opaque;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index)
        if (stream->items[index].name) xx_mem_free(stream->items[index].name);
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool pci_add_member(pci_stream *stream, const pci_member *member) {
    pci_member *grown;
    if (!stream || !member || stream->count >= PCI_MAX_RECORDS ||
        stream->count > SIZE_MAX / sizeof(*grown) - 1U)
        return false;
    grown = (pci_member *)xx_mem_realloc(stream->items,
                                         (stream->count + 1U) * sizeof(*grown));
    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}

static uint8_t *pci_read_packed(Abstractformat *format,
                                const pci_member *member) {
    uint8_t *packed;
    if (member->packed_size <= 0 || (uint64_t)member->packed_size > SIZE_MAX)
        return NULL;
    packed = (uint8_t *)xx_mem_alloc((size_t)member->packed_size);
    if (!packed) return NULL;
    if (!pci_read_at(format->device, member->data_offset, packed,
                     (size_t)member->packed_size)) {
        xx_mem_free(packed);
        return NULL;
    }
    return packed;
}

/* Decodes one member's DCL stream.  @p output may be NULL to measure only. */
static bool pci_decode_member(Abstractformat *format, const pci_member *member,
                              uint8_t **output, size_t *produced) {
    uint8_t *packed;
    size_t consumed = 0U;
    size_t plain = 0U;
    bool result = false;
    if (output) *output = NULL;
    if (produced) *produced = 0U;
    if (member->packed_size == 0) return true;
    packed = pci_read_packed(format, member);
    if (!packed) return false;
    if (xx_dcl_scan_memory(packed, (size_t)member->packed_size,
                           (size_t)PCI_MAX_RAW_SIZE, &consumed, &plain)) {
        if (!output) {
            if (produced) *produced = plain;
            result = true;
        } else {
            uint8_t *out = (uint8_t *)xx_mem_alloc(plain ? plain : 1U);
            size_t written = 0U;
            if (out &&
                xx_dcl_decode_memory(packed, (size_t)member->packed_size, out,
                                     plain, &written) &&
                written == plain) {
                *output = out;
                if (produced) *produced = written;
                result = true;
            } else if (out) {
                xx_mem_free(out);
            }
        }
    }
    xx_mem_free(packed);
    return result;
}

static bool pci_parse(Abstractformat *format, pci_stream **result,
                      bool resolve_sizes) {
    uint8_t header[PCI_VOLUME_HEADER_SIZE];
    uint8_t record[PCI_RECORD_SIZE];
    pci_stream *stream = NULL;
    int64_t total, available, cursor, base;
    uint32_t declared;
    size_t index;
    if (!format || !format->device || !result || format->base_address < 0)
        return false;
    base = format->base_address;
    total = xx_io_total_size(format->device);
    if (total < base) return false;
    available = total - base;
    if (available < PCI_VOLUME_HEADER_SIZE + PCI_RECORD_SIZE) return false;
    if (!pci_read_at(format->device, base, header, sizeof(header)))
        return false;
    /* The header has no magic; what it has is a very particular shape. */
    for (index = 0U; index < PCI_TAG_OFFSET; ++index)
        if (header[index] != 0U) return false;
    if (header[PCI_TAG_OFFSET] != PCI_TAG) return false;
    if (pci_le32(header + 0x10) != (uint32_t)PCI_TAG) return false;
    for (index = 0x14U; index < PCI_VOLUME_HEADER_SIZE; ++index)
        if (header[index] != 0U) return false;
    declared = pci_le16(header + PCI_COUNT_OFFSET);
    if (declared == 0U || declared > PCI_MAX_RECORDS) return false;
    /* Each member costs at least its descriptor, so a count that could not
     * fit is rejected before a single allocation happens. */
    if ((int64_t)declared >
        (available - PCI_VOLUME_HEADER_SIZE) / PCI_RECORD_SIZE)
        return false;

    stream = (pci_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return false;
    stream->declared_count = (uint16_t)declared;

    cursor = PCI_VOLUME_HEADER_SIZE;
    for (index = 0U; index < declared; ++index) {
        pci_member member;
        int64_t packed_size;
        size_t zero;
        if (!pci_range_within(available, cursor, PCI_RECORD_SIZE) ||
            !pci_read_at(format->device, base + cursor, record,
                         sizeof(record)))
            goto fail;
        for (zero = 0x1cU; zero < PCI_RECORD_SIZE; ++zero)
            if (record[zero] != 0U) goto fail;
        packed_size = (int64_t)pci_le32(record + PCI_SIZE_OFFSET);
        if (!pci_range_within(available, cursor + PCI_RECORD_SIZE,
                              packed_size))
            goto fail;
        xx_mem_zero(&member, sizeof(member));
        member.name = pci_fixed_name(record, PCI_NAME_FIELD_SIZE);
        if (!member.name) goto fail;
        member.header_offset = base + cursor;
        member.data_offset = base + cursor + PCI_RECORD_SIZE;
        member.packed_size = packed_size;
        member.unpacked_size = -1;
        member.dos_date = pci_le16(record + PCI_DATE_OFFSET);
        member.dos_time = pci_le16(record + PCI_TIME_OFFSET);
        if (!pci_add_member(stream, &member)) {
            xx_mem_free(member.name);
            goto fail;
        }
        cursor += PCI_RECORD_SIZE + packed_size;
    }

    /* The chain must tile the volume exactly.  Without this the format would
     * have no detector at all. */
    if (cursor != available) goto fail;
    stream->archive_size = available;

    if (resolve_sizes) {
        for (index = 0U; index < stream->count; ++index) {
            size_t produced = 0U;
            if (pci_decode_member(format, &stream->items[index], NULL,
                                  &produced))
                stream->items[index].unpacked_size = (int64_t)produced;
        }
    }
    *result = stream;
    return true;
fail:
    pci_stream_free(stream);
    return false;
}

static bool pci_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *pci_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool pci_set_record(xx_archive_record *record,
                           const pci_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = PCI_RECORD_SIZE;
    record->data_offset = member->data_offset;
    record->compressed_size = member->packed_size;
    if (!xx_archive_record_set_original_name(record, member->name) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                        (uint64_t)member->packed_size) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_LAST_MOD_DATE,
                                        member->dos_date) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_LAST_MOD_TIME,
                                        member->dos_time) ||
        !xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                         false) ||
        !xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false))
        return false;
    /* The plaintext length is nowhere in the container, so it is published
     * only when a decode actually produced it. */
    if (member->unpacked_size >= 0 &&
        !xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                        (uint64_t)member->unpacked_size))
        return false;
    return true;
}

void xx_pcinstall_init(xx_pcinstall *archive, xx_io_device *device,
                       int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_PCINSTALL_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-pcinstall");
    xx_format_set_extension(&archive->format, "shr");
    archive->format.check_is_valid = xx_pcinstall_check_is_valid;
    archive->format.handle_base_info = xx_pcinstall_handle_base_info;
    archive->format.get_format_size = xx_pcinstall_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_pcinstall_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_pcinstall_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_pcinstall_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_pcinstall_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_pcinstall_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_pcinstall_free_archive_records_reading;
}

xx_pcinstall *xx_pcinstall_create(xx_io_device *device, int64_t base_address) {
    xx_pcinstall *archive = (xx_pcinstall *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_pcinstall_init(archive, device, base_address);
    return archive;
}

void xx_pcinstall_destroy(xx_pcinstall *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_pcinstall_free(xx_pcinstall *archive) {
    if (!archive) return;
    xx_pcinstall_destroy(archive);
    xx_mem_free(archive);
}

bool xx_pcinstall_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    pci_stream *stream;
    (void)pd;
    if (!pci_parse(format, &stream, false)) return false;
    pci_stream_free(stream);
    return true;
}

bool xx_pcinstall_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    pci_stream *stream;
    xx_pcinstall *archive;
    (void)pd;
    if (!format) return false;
    if (!pci_parse(format, &stream, false)) {
        format->is_valid = false;
        format->base_info_handled = false;
        format->format_size = -1;
        format->number_of_archive_records = 0U;
        return false;
    }
    archive = (xx_pcinstall *)format;
    archive->number_of_records = stream->count;
    archive->declared_count = stream->declared_count;
    format->number_of_archive_records = stream->count;
    format->format_size = stream->archive_size;
    format->overlay_offset = -1;
    format->overlay_size = 0;
    format->file_type = XX_PCINSTALL_FILE_TYPE;
    format->format_type = XX_TYPE_ARCHIVE;
    format->is_archive = true;
    format->is_valid = true;
    format->base_info_handled = true;
    pci_stream_free(stream);
    return true;
}

int64_t xx_pcinstall_get_format_size(Abstractformat *format,
                                     xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_pcinstall_handle_base_info(format, pd))
               ? format->format_size
               : -1;
}

uint64_t xx_pcinstall_get_number_of_archive_records(Abstractformat *format,
                                                    xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_pcinstall_handle_base_info(format, pd))
               ? ((xx_pcinstall *)format)->number_of_records
               : 0U;
}

xx_archive_record_state *xx_pcinstall_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    pci_stream *stream;
    xx_archive_record_state *state;
    (void)pd;
    if (!pci_parse(format, &stream, true)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        pci_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = pci_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!pci_copy_options(&state->options, options) ||
        !pci_set_record(&state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_pcinstall_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_pcinstall_archive_record_move_to_next(Abstractformat *format,
                                              xx_archive_record_state *state,
                                              xx_pd_struct *pd) {
    pci_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (pci_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = pci_set_record(&state->current_record,
                                       &stream->items[stream->index]);
    return state->has_record;
}

bool xx_pcinstall_unpack_current_archive_record(Abstractformat *format,
                                                xx_archive_record_state *state,
                                                xx_pd_struct *pd) {
    pci_stream *stream;
    pci_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U;
    size_t written = 0U;
    bool result = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (pci_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    if (!pci_safe_output_name(member->name)) return false;
    if (!pci_decode_member(format, member, &plain, &plain_size)) return false;
    path_option = pci_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
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
               ? xx_str_concat3(base, "/", member->name)
               : xx_str_concat(base, member->name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
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
    if (!result && path) xx_io_file_remove_a(path);
    if (plain) xx_mem_free(plain);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_pcinstall_free_archive_records_reading(Abstractformat *format,
                                               xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
