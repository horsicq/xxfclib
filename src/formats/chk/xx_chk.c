/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/chk/xx_chk.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the file-type constant resolves to UNKNOWN until the enumerator
 * lands.  Delete this block once XX_FILE_TYPE_CHK exists in the enum. */
#ifdef CHK
#define XX_CHK_FILE_TYPE XX_FILE_TYPE_CHK
#else
#define XX_CHK_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

/** Streaming buffer for the checksum passes. */
#define XX_CHK_STAGING_SIZE 65536U
#define XX_CHK_HEADER_CHKSUM_OFFSET 0x24U
/** Two payloads at most: the kernel and, optionally, the rootfs. */
#define XX_CHK_MAX_MEMBERS 2U

typedef struct xx_chk_member_s {
    const char *name; /**< A static literal; never freed. */
    int64_t data_offset;
    int64_t data_size;
} xx_chk_member;

typedef struct xx_chk_private_s {
    xx_chk_member members[XX_CHK_MAX_MEMBERS];
    size_t count;
    int64_t input_size;
    int64_t header_offset;
    int64_t archive_end;
    uint32_t header_size;
    uint32_t kernel_size;
    uint32_t rootfs_size;
    uint32_t kernel_checksum;
    uint32_t rootfs_checksum;
    uint32_t image_checksum;
    uint32_t header_checksum;
    char board_id[XX_CHK_MAX_BOARD_ID + 1U];
} xx_chk_private;

typedef struct xx_chk_archive_stream_s {
    xx_chk_private parsed;
    size_t index;
} xx_chk_archive_stream;

static void xx_chk_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------------------ */
/* NETGEAR checksum                                                          */
/* ------------------------------------------------------------------------ */

/*
 * Transcribed from netgear_checksum_add() / netgear_checksum_fini() in
 * OpenWrt's tools/firmware-utils/src/mkchkimg.c.  c0 sums the bytes and c1
 * sums c0; each accumulator is folded to sixteen bits TWICE - one fold can
 * still leave a seventeenth bit set - and the halves are packed c1 over c0.
 */

typedef struct xx_chk_checksum_s {
    uint32_t c0;
    uint32_t c1;
} xx_chk_checksum;

static void xx_chk_checksum_init(xx_chk_checksum *sum) {
    sum->c0 = 0U;
    sum->c1 = 0U;
}

static void xx_chk_checksum_add(xx_chk_checksum *sum, const uint8_t *data,
                                size_t size) {
    size_t index;
    for (index = 0U; index < size; ++index) {
        sum->c0 += data[index];
        sum->c1 += sum->c0;
    }
}

static uint32_t xx_chk_checksum_final(const xx_chk_checksum *sum) {
    uint32_t folded;
    uint32_t c0;
    uint32_t c1;
    folded = (sum->c0 & 65535U) + ((sum->c0 >> 16U) & 65535U);
    c0 = ((folded >> 16U) + folded) & 65535U;
    folded = (sum->c1 & 65535U) + ((sum->c1 >> 16U) & 65535U);
    c1 = ((folded >> 16U) + folded) & 65535U;
    return (c1 << 16U) | c0;
}

/* ------------------------------------------------------------------------ */
/* Bounds helpers                                                            */
/* ------------------------------------------------------------------------ */

/* All positioning goes through seek64: long is 32-bit on Win64 and a CHK
 * image can sit anywhere inside a flash dump. */
static bool xx_chk_read_at(xx_io_device *device, int64_t offset, void *data,
                           size_t size) {
    uint8_t *out = (uint8_t *)data;
    size_t done = 0U;
    if (!device || (!data && size != 0U) || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0) {
        return false;
    }
    while (done < size) {
        ssize_t got = xx_io_read(device, out + done, size - done);
        if (got <= 0 || (size_t)got > size - done) return false;
        done += (size_t)got;
    }
    return true;
}

static bool xx_chk_add(int64_t left, uint64_t right, int64_t *result) {
    if (!result || left < 0 || right > (uint64_t)(INT64_MAX - left)) {
        return false;
    }
    *result = left + (int64_t)right;
    return true;
}

/* True when [offset, offset + size) lies inside [0, total_size). */
static bool xx_chk_range_within(int64_t total_size, int64_t offset,
                                int64_t size) {
    return (total_size >= 0) && (offset >= 0) && (size >= 0) &&
           (offset <= total_size) && (size <= total_size - offset);
}

static void xx_chk_private_cleanup(xx_chk_private *parsed) {
    if (!parsed) return;
    /* Member names are string literals, so nothing here owns heap memory;
     * the cleanup exists for symmetry with the other readers and so that a
     * failed parse leaves the caller's stack copy in a defined state. */
    xx_mem_zero(parsed, sizeof(*parsed));
    parsed->input_size = -1;
    parsed->header_offset = -1;
    parsed->archive_end = -1;
}

/* Feed a device range into a running checksum, streamed so a large payload is
 * never resident. */
static bool xx_chk_checksum_range(xx_io_device *device, int64_t offset,
                                  int64_t size, xx_chk_checksum *sum,
                                  xx_pd_struct *pd) {
    uint8_t staging[XX_CHK_STAGING_SIZE];
    if (!device || !sum || offset < 0 || size < 0) return false;
    if (size != 0 && xx_io_seek64(device, offset, SEEK_SET) != 0) return false;
    while (size > 0) {
        size_t step =
            (size < (int64_t)sizeof(staging)) ? (size_t)size : sizeof(staging);
        size_t done = 0U;
        if (pd && xx_pd_is_stopped(pd)) return false;
        while (done < step) {
            ssize_t got = xx_io_read(device, staging + done, step - done);
            if (got <= 0 || (size_t)got > step - done) return false;
            done += (size_t)got;
        }
        xx_chk_checksum_add(sum, staging, step);
        size -= (int64_t)step;
    }
    return true;
}

static bool xx_chk_checksum_equals(xx_io_device *device, int64_t offset,
                                   int64_t size, uint32_t expected,
                                   xx_pd_struct *pd) {
    xx_chk_checksum sum;
    xx_chk_checksum_init(&sum);
    if (!xx_chk_checksum_range(device, offset, size, &sum, pd)) return false;
    return xx_chk_checksum_final(&sum) == expected;
}

static bool xx_chk_parse(Abstractformat *self, xx_chk_private *parsed,
                         xx_pd_struct *pd) {
    uint8_t header[XX_CHK_MAX_HEADER_SIZE];
    uint32_t computed;
    size_t board_length;
    size_t index;
    int64_t kernel_offset;
    int64_t rootfs_offset;
    xx_chk_checksum sum;
    if (parsed) {
        xx_mem_zero(parsed, sizeof(*parsed));
        parsed->input_size = -1;
        parsed->header_offset = -1;
        parsed->archive_end = -1;
    }
    if (!self || !self->device || !parsed || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    parsed->input_size = xx_io_total_size(self->device);
    parsed->header_offset = self->base_address;
    if (!xx_chk_range_within(parsed->input_size, self->base_address,
                             XX_CHK_FIXED_HEADER_SIZE) ||
        !xx_chk_read_at(self->device, self->base_address, header,
                        XX_CHK_FIXED_HEADER_SIZE) ||
        xx_data_get_u32(header, XX_CHK_FIXED_HEADER_SIZE, 0U, true) !=
            XX_CHK_MAGIC) {
        goto fail;
    }
    parsed->header_size =
        xx_data_get_u32(header, XX_CHK_FIXED_HEADER_SIZE, 4U, true);
    /* header_len is 40 plus the board id, and a board id is a short model
     * string.  Bounding it here is what keeps the fixed header buffer above
     * from being an attacker-chosen length. */
    if (parsed->header_size <= XX_CHK_FIXED_HEADER_SIZE ||
        parsed->header_size > XX_CHK_MAX_HEADER_SIZE ||
        !xx_chk_range_within(parsed->input_size, self->base_address,
                             (int64_t)parsed->header_size) ||
        !xx_chk_read_at(self->device, self->base_address, header,
                        parsed->header_size)) {
        goto fail;
    }
    parsed->kernel_checksum =
        xx_data_get_u32(header, parsed->header_size, 0x10U, true);
    parsed->rootfs_checksum =
        xx_data_get_u32(header, parsed->header_size, 0x14U, true);
    parsed->kernel_size =
        xx_data_get_u32(header, parsed->header_size, 0x18U, true);
    parsed->rootfs_size =
        xx_data_get_u32(header, parsed->header_size, 0x1CU, true);
    parsed->image_checksum =
        xx_data_get_u32(header, parsed->header_size, 0x20U, true);
    parsed->header_checksum =
        xx_data_get_u32(header, parsed->header_size, 0x24U, true);

    /* The board id runs to the end of the header and is not NUL terminated,
     * though a few producers pad it out; trailing NULs are trimmed and the
     * remainder has to read as a printable model string. */
    board_length = parsed->header_size - XX_CHK_FIXED_HEADER_SIZE;
    while (board_length != 0U &&
           header[XX_CHK_FIXED_HEADER_SIZE + board_length - 1U] == 0U) {
        --board_length;
    }
    if (board_length == 0U || board_length > XX_CHK_MAX_BOARD_ID) goto fail;
    for (index = 0U; index < board_length; ++index) {
        uint8_t ch = header[XX_CHK_FIXED_HEADER_SIZE + index];
        if (ch < 0x20U || ch > 0x7EU) goto fail;
    }
    xx_rt_memcpy(parsed->board_id, header + XX_CHK_FIXED_HEADER_SIZE,
                 board_length);
    parsed->board_id[board_length] = '\0';

    /* The header checksum covers header_len bytes with its own field read as
     * zero.  mkchkimg computes it last, over a struct whose checksum field is
     * still zero, so the zeroing is done on the buffer rather than by
     * subtracting the field's contribution. */
    xx_rt_memset(header + XX_CHK_HEADER_CHKSUM_OFFSET, 0, 4U);
    xx_chk_checksum_init(&sum);
    xx_chk_checksum_add(&sum, header, parsed->header_size);
    computed = xx_chk_checksum_final(&sum);
    if (computed != parsed->header_checksum) goto fail;

    /* mkchkimg refuses to build an image without a kernel. */
    if (parsed->kernel_size == 0U) goto fail;
    if (!xx_chk_add(self->base_address, parsed->header_size, &kernel_offset) ||
        !xx_chk_range_within(parsed->input_size, kernel_offset,
                             (int64_t)parsed->kernel_size) ||
        !xx_chk_add(kernel_offset, parsed->kernel_size, &rootfs_offset) ||
        !xx_chk_range_within(parsed->input_size, rootfs_offset,
                             (int64_t)parsed->rootfs_size) ||
        !xx_chk_add(rootfs_offset, parsed->rootfs_size, &parsed->archive_end)) {
        goto fail;
    }

    /* All three payload checksums are verified.  A NETGEAR bootloader will
     * not flash an image whose checksums disagree, so a mismatch is a parse
     * failure here as well. */
    if (!xx_chk_checksum_equals(self->device, kernel_offset,
                                (int64_t)parsed->kernel_size,
                                parsed->kernel_checksum, pd)) {
        goto fail;
    }
    if (parsed->rootfs_size != 0U &&
        !xx_chk_checksum_equals(self->device, rootfs_offset,
                                (int64_t)parsed->rootfs_size,
                                parsed->rootfs_checksum, pd)) {
        goto fail;
    }
    xx_chk_checksum_init(&sum);
    if (!xx_chk_checksum_range(self->device, kernel_offset,
                               (int64_t)parsed->kernel_size, &sum, pd) ||
        !xx_chk_checksum_range(self->device, rootfs_offset,
                               (int64_t)parsed->rootfs_size, &sum, pd) ||
        xx_chk_checksum_final(&sum) != parsed->image_checksum) {
        goto fail;
    }

    /* The names are literals chosen here, never taken from the file, so they
     * need no sanitising before use as destination path components. */
    parsed->members[parsed->count].name = "kernel";
    parsed->members[parsed->count].data_offset = kernel_offset;
    parsed->members[parsed->count].data_size = (int64_t)parsed->kernel_size;
    ++parsed->count;
    if (parsed->rootfs_size != 0U) {
        parsed->members[parsed->count].name = "rootfs";
        parsed->members[parsed->count].data_offset = rootfs_offset;
        parsed->members[parsed->count].data_size = (int64_t)parsed->rootfs_size;
        ++parsed->count;
    }
    return true;
fail:
    xx_chk_private_cleanup(parsed);
    return false;
}

/* ------------------------------------------------------------------------ */
/* Record plumbing                                                           */
/* ------------------------------------------------------------------------ */

static bool xx_chk_copy_options(xx_list_s *destination,
                                const xx_list_s *source) {
    size_t index;
    if (!destination || !source) return source == NULL;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *item =
            (const xx_meta *)xx_list_at((const xx_list_t *)source, index);
        xx_meta copy;
        if (!item) continue;
        xx_meta_init(&copy, item->meta_id);
        if (!xx_var_copy(&copy.var, &item->var) ||
            !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

static const xx_var *xx_chk_find_option(const xx_list_s *options,
                                        uint32_t meta_id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *item =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (item && item->meta_id == meta_id) return &item->var;
    }
    return NULL;
}

static bool xx_chk_populate_record(xx_archive_record *record,
                                   const xx_chk_private *parsed,
                                   const xx_chk_member *member) {
    if (!record || !parsed || !member || !member->name) return false;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = parsed->header_offset;
    record->header_size = (int64_t)parsed->header_size;
    record->data_offset = member->data_offset;
    record->compressed_size = member->data_size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->data_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)member->data_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          0U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

static void xx_chk_archive_stream_free(void *pointer) {
    xx_chk_archive_stream *stream = (xx_chk_archive_stream *)pointer;
    if (!stream) return;
    xx_chk_private_cleanup(&stream->parsed);
    xx_mem_free(stream);
}

/* ------------------------------------------------------------------------ */
/* Public interface                                                          */
/* ------------------------------------------------------------------------ */

void xx_chk_init(xx_chk *chk, xx_io_device *dev, int64_t base_address) {
    if (!chk) return;
    xx_mem_zero(chk, sizeof(*chk));
    xx_format_init(&chk->format, dev, base_address);
    chk->format.endian = XX_ENDIAN_BIG;
    chk->format.file_type = XX_CHK_FILE_TYPE;
    chk->format.format_type = XX_TYPE_ARCHIVE;
    chk->format.is_archive = true;
    xx_format_set_mime_type(&chk->format, "application/x-chk-firmware");
    xx_format_set_extension(&chk->format, "chk");
    chk->format.check_is_valid = xx_chk_check_is_valid;
    chk->format.handle_base_info = xx_chk_handle_base_info;
    chk->format.get_format_size = xx_chk_get_format_size;
    chk->format.get_number_of_archive_records =
        xx_chk_get_number_of_archive_records;
    chk->format.create_archive_records_reading =
        xx_chk_create_archive_records_reading;
    chk->format.get_current_archive_record = xx_chk_get_current_archive_record;
    chk->format.unpack_current_archive_record =
        xx_chk_unpack_current_archive_record;
    chk->format.archive_record_move_to_next = xx_chk_archive_record_move_to_next;
    chk->format.free_archive_records_reading =
        xx_chk_free_archive_records_reading;
    chk->format.destroy = xx_chk_vtable_destroy;
    chk->archive_end = -1;
}

xx_chk *xx_chk_create(xx_io_device *dev, int64_t base_address) {
    xx_chk *chk = (xx_chk *)xx_mem_alloc(sizeof(*chk));
    if (chk) xx_chk_init(chk, dev, base_address);
    return chk;
}

void xx_chk_destroy(xx_chk *chk) {
    if (!chk) return;
    if (chk->internal) {
        xx_chk_private_cleanup((xx_chk_private *)chk->internal);
        xx_mem_free(chk->internal);
        chk->internal = NULL;
    }
    xx_format_cleanup_extra_parameters(&chk->format);
}

static void xx_chk_vtable_destroy(Abstractformat *self) {
    xx_chk_destroy((xx_chk *)self);
}

void xx_chk_free(xx_chk *chk) {
    if (!chk) return;
    xx_chk_destroy(chk);
    xx_mem_free(chk);
}

bool xx_chk_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_chk_private parsed;
    bool result = xx_chk_parse(self, &parsed, pd);
    xx_chk_private_cleanup(&parsed);
    return result;
}

bool xx_chk_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_chk_private *parsed;
    xx_chk *chk = (xx_chk *)self;
    int64_t total_size;
    if (!self || !chk) return false;
    parsed = (xx_chk_private *)xx_mem_alloc(sizeof(*parsed));
    if (!parsed || !xx_chk_parse(self, parsed, pd)) {
        if (parsed) xx_mem_free(parsed);
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    if (chk->internal) {
        xx_chk_private_cleanup((xx_chk_private *)chk->internal);
        xx_mem_free(chk->internal);
    }
    chk->internal = parsed;
    chk->number_of_records = parsed->count;
    chk->number_of_members = parsed->count;
    chk->header_size = parsed->header_size;
    chk->kernel_size = parsed->kernel_size;
    chk->rootfs_size = parsed->rootfs_size;
    chk->kernel_checksum = parsed->kernel_checksum;
    chk->rootfs_checksum = parsed->rootfs_checksum;
    chk->image_checksum = parsed->image_checksum;
    chk->header_checksum = parsed->header_checksum;
    xx_rt_memcpy(chk->board_id, parsed->board_id, sizeof(chk->board_id));
    chk->archive_end = parsed->archive_end;
    self->format_size = parsed->archive_end - self->base_address;
    total_size = xx_io_total_size(self->device);
    if (total_size > parsed->archive_end) {
        self->overlay_offset = parsed->archive_end;
        self->overlay_size = total_size - parsed->archive_end;
    } else {
        self->overlay_offset = -1;
        self->overlay_size = 0;
    }
    self->number_of_archive_records = parsed->count;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_chk_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return -1;
    }
    return self->format_size;
}

uint64_t xx_chk_get_number_of_archive_records(Abstractformat *self,
                                              xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return ((xx_chk *)self)->number_of_records;
}

xx_archive_record_state *xx_chk_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_record_state *state;
    xx_chk_archive_stream *stream;
    if (!self || !self->device ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    stream = (xx_chk_archive_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!state || !stream) {
        if (state) xx_mem_free(state);
        if (stream) xx_mem_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    if (!xx_chk_copy_options(&state->options, options) ||
        !xx_chk_parse(self, &stream->parsed, pd)) {
        xx_chk_archive_stream_free(stream);
        xx_archive_record_state_free(state);
        return NULL;
    }
    stream->index = 0U;
    state->internal_state = stream;
    state->free_internal = xx_chk_archive_stream_free;
    state->total_records = (int64_t)stream->parsed.count;
    if (stream->parsed.count != 0U &&
        xx_chk_populate_record(&state->current_record, &stream->parsed,
                               &stream->parsed.members[0])) {
        state->has_record = true;
        state->current_index = 0;
    }
    return state;
}

const xx_archive_record *xx_chk_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_chk_archive_record_move_to_next(Abstractformat *self,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    xx_chk_archive_stream *stream;
    if (!self || !state || state->format != self || !state->has_record ||
        !state->internal_state || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_chk_archive_stream *)state->internal_state;
    ++stream->index;
    if (stream->index >= stream->parsed.count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    if (!xx_chk_populate_record(&state->current_record, &stream->parsed,
                                &stream->parsed.members[stream->index])) {
        state->has_record = false;
        return false;
    }
    ++state->current_index;
    return true;
}

bool xx_chk_unpack_current_archive_record(Abstractformat *self,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    const xx_archive_record *record;
    const xx_var *option;
    const char *name;
    const char *base = NULL;
    char *owned_base = NULL;
    char *destination = NULL;
    bool result = false;
    if (!self || !self->device || !state || state->format != self ||
        !state->has_record || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    record = &state->current_record;
    name = xx_archive_record_get_original_name(record);
    if (!name || !name[0]) return false;
    option = xx_chk_find_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!option) {
        int64_t total = xx_io_total_size(self->device);
        return record->data_offset >= 0 && record->compressed_size >= 0 &&
               record->data_offset <= total &&
               record->compressed_size <= total - record->data_offset;
    }
    if (option->type == XX_VAR_TYPE_STRING ||
        option->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(option);
    } else if (option->type == XX_VAR_TYPE_WSTRING ||
               option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(option));
        base = owned_base;
    }
    if (!base) goto cleanup;
    if (base[0] && base[xx_str_len(base) - 1U] != '/' &&
        base[xx_str_len(base) - 1U] != '\\') {
        destination = xx_str_concat3(base, "/", name);
    } else {
        destination = xx_str_concat(base, name);
    }
    if (!destination) goto cleanup;
    if (!xx_store_create_dirs_a(destination, false)) goto cleanup;
    result = xx_store_unpack_device_to_file(self->device, record->data_offset,
                                            record->compressed_size,
                                            destination, pd);
    if (!result) xx_rt_remove(destination);
cleanup:
    if (owned_base) xx_str_free(owned_base);
    if (destination) xx_str_free(destination);
    return result;
}

void xx_chk_free_archive_records_reading(Abstractformat *self,
                                         xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

uint64_t xx_chk_get_number_of_records(const xx_chk *chk) {
    return chk ? chk->number_of_records : 0U;
}
uint64_t xx_chk_get_number_of_members(const xx_chk *chk) {
    return chk ? chk->number_of_members : 0U;
}
uint32_t xx_chk_get_kernel_size(const xx_chk *chk) {
    return chk ? chk->kernel_size : 0U;
}
uint32_t xx_chk_get_rootfs_size(const xx_chk *chk) {
    return chk ? chk->rootfs_size : 0U;
}
const char *xx_chk_get_board_id(const xx_chk *chk) {
    return chk ? chk->board_id : "";
}
int64_t xx_chk_get_archive_end(const xx_chk *chk) {
    return chk ? chk->archive_end : -1;
}
