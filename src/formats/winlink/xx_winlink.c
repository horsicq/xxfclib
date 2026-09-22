/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * WinLink, the single-file packer whose output is found on the mid-1990s
 * Windows communications-suite install disks (FTP Software / IBM "WinLink"
 * media: PCTCP.DLL, EEHLLAPI.DLL, GENMAP.EXE, TD????I.FON and friends).  As
 * with MS-DOS era packers the packed copy keeps the original name with the
 * first character of the extension replaced by '_'.
 *
 * There is no published description of the container, but U3 recognises it
 * and both halves are recovered in F:\utils\U3\src (FORMAT_INDEX.md
 * "archive / 357 WinLink", class vxa, VMT 0x005d6418; slot 0 ->
 * FUN_005d6490, slot 1 -> FUN_005d64e0).  The layout below is FUN_005d6490
 * transcribed, and it holds for all 140 samples in F:\ARC\ARC\WinLink:
 *
 *   0x00  u16   2                 - format constant (U3 tests the word)
 *   0x02  u8    0                 - and this byte separately
 *   0x03  u16   MS-DOS packed time
 *   0x05  u16   MS-DOS packed date
 *   0x07  char[13]  the original 8.3 name, NUL terminated and NUL padded to
 *                   the full 13 bytes (U3's FUN_00425390)
 *   0x14  u32   0xFFFFFFFF sentinel
 *   0x18  ...   the packed payload, running to the end of the file
 *
 * THE CODEC IS LZW, and it was read out of U3's decoder FUN_0053fdf0 (the
 * unpack path is FUN_005d64e0 -> FUN_0053fdf0 over the extent from 0x18 to
 * end-of-file).  It is the TIFF dialect widened by two bits:
 *
 *   - codes are MSB-first, taken from the top of a 32-bit accumulator that
 *     is refilled one byte at a time (FUN_0053fc80);
 *   - widths run 9 to 14 bits, and the width changes EARLY: before every
 *     code, if (1 << width) - 1 <= next-assignable-code the width grows by
 *     one (FUN_0053fd20).  A decoder that widens one code later, as GIF and
 *     Unix compress do, desynchronises immediately;
 *   - 0x100 is CLEAR, 0x101 is END, and the first assignable code is 0x102,
 *     so 0x101 is never assigned;
 *   - the table stops growing at 0x4000 entries and then simply stalls --
 *     it does not self-restart;
 *   - a CLEAR resets the width and the next-assignable code but NOT the bit
 *     accumulator, and the code that follows a CLEAR is a bare literal that
 *     adds no table entry.
 *
 * The container records no plaintext length anywhere, so the size is
 * obtained by running the stream once with no output buffer and then
 * decoding into a buffer of exactly that size.  Reaching the explicit END
 * code is required for both passes; all 140 samples do.
 *
 * Verified byte for byte against U3's own output for all 140 samples.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/winlink/xx_winlink.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

/* The enumerator is added by the coordinator, not by this file.  Until it
 * exists the reader still compiles and simply reports UNKNOWN. */
#ifdef WINLINK
#define XX_WINLINK_FILE_TYPE XX_FILE_TYPE_WINLINK
#else
#define XX_WINLINK_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define XX_WINLINK_HEADER_SIZE 24
/* Fixed name field at 0x07: "12345678.123" plus its NUL. */
#define XX_WINLINK_NAME_OFFSET 7
#define XX_WINLINK_NAME_FIELD 13U
#define XX_WINLINK_SENTINEL_OFFSET 20

/* LZW constants, all read out of FUN_0053fdf0. */
#define XX_WINLINK_LZW_CLEAR 0x100U
#define XX_WINLINK_LZW_END 0x101U
#define XX_WINLINK_LZW_FIRST 0x102U
#define XX_WINLINK_LZW_TABLE 0x4000U
#define XX_WINLINK_LZW_MIN_BITS 9U
#define XX_WINLINK_LZW_MAX_BITS 14U

/* Bound the payload before anything is read or allocated for it.  A WinLink
 * member is one file off an install disk; these limits refuse a container
 * claiming an extent no such disk ever held. */
#define XX_WINLINK_MAX_PACKED ((int64_t)128 * 1024 * 1024)
#define XX_WINLINK_MAX_OUTPUT ((size_t)256U * 1024U * 1024U)

typedef struct xx_winlink_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    uint64_t unpacked_size; /* 0 until the measuring pass has run */
    uint32_t timestamp;
} xx_winlink_member;

typedef struct xx_winlink_stream_s {
    xx_winlink_member member;
    size_t count;
    size_t index;
    int64_t archive_size;
    uint32_t timestamp;
} xx_winlink_stream;

/* ------------------------------------------------------------ helpers --- */

static uint16_t xx_winlink_le16(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U));
}

static bool xx_winlink_read_at(Abstractformat *self, int64_t offset,
                               void *buffer, size_t size) {
    size_t done = 0U;

    if (!self || !self->device || (!buffer && size != 0U) || offset < 0 ||
        xx_io_seek64(self->device, offset, SEEK_SET) != 0)
        return false;
    while (done < size) {
        ssize_t amount =
            xx_io_read(self->device, (uint8_t *)buffer + done, size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

/* An MS-DOS packed date whose month or day is out of range is not a date,
 * and a header that carries one is not a WinLink header. */
static bool xx_winlink_date_sane(uint16_t date) {
    unsigned month = (unsigned)((date >> 5U) & 0x0fU);
    unsigned day = (unsigned)(date & 0x1fU);

    return month >= 1U && month <= 12U && day >= 1U && day <= 31U;
}

/* U3's own 8.3 test (FUN_00425390): a printable first character that is not
 * '.', one to eight name characters, a mandatory '.', up to three extension
 * characters, the terminating NUL, and then nothing but NULs to the end of
 * the fixed field.  Requiring the dot is what turns a field of printable
 * bytes into a real signature. */
static bool xx_winlink_name_field_sane(const uint8_t *field, size_t size,
                                       size_t *length) {
    size_t index = 0U;
    size_t run;

    if (size == 0U || field[0] < 0x20U || field[0] == '.') return false;
    for (run = 1U; run <= 8U; ++run) {
        ++index;
        if (index >= size || field[index] < 0x20U) return false;
        if (field[index] == '.') break;
    }
    if (field[index] != '.') return false;
    /* Four steps, not three: the NUL that ends a full three-character
     * extension sits one past the last of them. */
    for (run = 0U; run < 4U; ++run) {
        ++index;
        if (index >= size) return false;
        if (field[index] == 0U) break;
        if (field[index] < 0x20U) return false;
    }
    if (field[index] != 0U) return false;
    *length = index;
    for (; index < size; ++index)
        if (field[index] != 0U) return false;
    return true;
}

/* Rewrite only the filesystem-facing form; the bytes themselves are kept for
 * the caller's code page. */
static char *xx_winlink_normalize_name(const uint8_t *bytes, size_t size) {
    char *name;
    size_t input;
    size_t output = 0U;

    if (!bytes || size == 0U || size > SIZE_MAX - 2U) return NULL;
    name = (char *)xx_mem_alloc(size + 2U);
    if (!name) return NULL;
    for (input = 0U; input < size; ++input) {
        uint8_t c = bytes[input];
        if (c < 0x20U || c == '/' || c == '\\' || c == ':' || c == '"' ||
            c == '*' || c == '<' || c == '>' || c == '?' || c == '|')
            name[output++] = '_';
        else
            name[output++] = (char)c;
    }
    while (output > 0U && (name[output - 1U] == ' ' || name[output - 1U] == '.'))
        --output;
    if (output == 0U) name[output++] = '_';
    name[output] = 0;
    return name;
}

static void xx_winlink_stream_free(void *pointer) {
    xx_winlink_stream *stream = (xx_winlink_stream *)pointer;

    if (!stream) return;
    xx_str_free(stream->member.name);
    xx_mem_free(stream);
}

static xx_winlink_stream *xx_winlink_parse(Abstractformat *self,
                                           xx_pd_struct *pd) {
    uint8_t header[XX_WINLINK_HEADER_SIZE];
    xx_winlink_stream *stream;
    int64_t total;
    int64_t span;
    size_t name_length = 0U;

    if (!self || !self->device || self->base_address < 0) return NULL;
    if (pd && xx_pd_is_stopped(pd)) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    /* A member with no payload at all cannot be a packed file. */
    if (span <= (int64_t)XX_WINLINK_HEADER_SIZE) return NULL;
    if (!xx_winlink_read_at(self, self->base_address, header, sizeof(header)))
        return NULL;
    if (header[0] != 0x02U || header[1] != 0x00U || header[2] != 0x00U)
        return NULL;
    if (header[XX_WINLINK_SENTINEL_OFFSET] != 0xffU ||
        header[XX_WINLINK_SENTINEL_OFFSET + 1] != 0xffU ||
        header[XX_WINLINK_SENTINEL_OFFSET + 2] != 0xffU ||
        header[XX_WINLINK_SENTINEL_OFFSET + 3] != 0xffU)
        return NULL;
    if (!xx_winlink_date_sane(xx_winlink_le16(header + 5))) return NULL;
    if (!xx_winlink_name_field_sane(header + XX_WINLINK_NAME_OFFSET,
                                    XX_WINLINK_NAME_FIELD, &name_length))
        return NULL;

    stream = (xx_winlink_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return NULL;
    stream->member.name =
        xx_winlink_normalize_name(header + XX_WINLINK_NAME_OFFSET, name_length);
    if (!stream->member.name) {
        xx_mem_free(stream);
        return NULL;
    }
    stream->timestamp = ((uint32_t)xx_winlink_le16(header + 5) << 16U) |
                        (uint32_t)xx_winlink_le16(header + 3);
    stream->member.timestamp = stream->timestamp;
    stream->member.header_offset = self->base_address;
    stream->member.header_size = XX_WINLINK_HEADER_SIZE;
    stream->member.data_offset = self->base_address + XX_WINLINK_HEADER_SIZE;
    stream->member.compressed_size = span - (int64_t)XX_WINLINK_HEADER_SIZE;
    stream->member.unpacked_size = 0U;
    stream->count = 1U;
    stream->archive_size = span;
    return stream;
}

/* ---------------------------------------------------------- LZW codec --- */

/* MSB-first bit source: bytes drop into the top of a 32-bit accumulator and
 * codes are taken off the top, exactly as FUN_0053fc80 does it. */
typedef struct xx_winlink_bits_s {
    const uint8_t *data;
    size_t size;
    size_t position;
    uint32_t accumulator;
    unsigned available;
} xx_winlink_bits;

static bool xx_winlink_bits_get(xx_winlink_bits *bits, unsigned width,
                                unsigned *code) {
    while (bits->available < width) {
        if (bits->position >= bits->size) return false;
        bits->accumulator +=
            (uint32_t)bits->data[bits->position++] << (24U - bits->available);
        bits->available += 8U;
    }
    *code = (unsigned)(bits->accumulator >> (32U - width));
    bits->accumulator <<= width;
    bits->available -= width;
    return true;
}

/* The early width change: it is tested before every code, and against the
 * CURRENT mask rather than the next power of two, so the widest code of a
 * width is never used. */
static bool xx_winlink_next_code(xx_winlink_bits *bits, unsigned *width,
                                 unsigned next, unsigned *code) {
    if (((1U << *width) - 1U) <= next && *width < XX_WINLINK_LZW_MAX_BITS)
        ++(*width);
    return xx_winlink_bits_get(bits, *width, code);
}

/* Output sink.  With a NULL buffer it only counts, which is how the member
 * is measured before anything is allocated for it. */
typedef struct xx_winlink_sink_s {
    uint8_t *data;
    size_t limit;
    size_t count;
} xx_winlink_sink;

static bool xx_winlink_sink_put(xx_winlink_sink *sink, uint8_t value) {
    if (sink->count >= sink->limit) return false;
    if (sink->data) sink->data[sink->count] = value;
    ++sink->count;
    return true;
}

typedef struct xx_winlink_lzw_s {
    uint16_t *prefix;
    uint8_t *suffix;
    uint8_t *stack;
} xx_winlink_lzw;

static void xx_winlink_lzw_cleanup(xx_winlink_lzw *lzw) {
    if (!lzw) return;
    xx_mem_free(lzw->prefix);
    xx_mem_free(lzw->suffix);
    xx_mem_free(lzw->stack);
    lzw->prefix = NULL;
    lzw->suffix = NULL;
    lzw->stack = NULL;
}

/* 64 KiB of tables is too much for the stack, so they are heap allocated
 * once per decode rather than per segment. */
static bool xx_winlink_lzw_setup(xx_winlink_lzw *lzw) {
    unsigned index;

    lzw->prefix = (uint16_t *)xx_mem_alloc(XX_WINLINK_LZW_TABLE *
                                           sizeof(*lzw->prefix));
    lzw->suffix = (uint8_t *)xx_mem_alloc(XX_WINLINK_LZW_TABLE);
    lzw->stack = (uint8_t *)xx_mem_alloc(XX_WINLINK_LZW_TABLE);
    if (!lzw->prefix || !lzw->suffix || !lzw->stack) {
        xx_winlink_lzw_cleanup(lzw);
        return false;
    }
    for (index = 0U; index < 0x100U; ++index) {
        lzw->prefix[index] = 0U;
        lzw->suffix[index] = (uint8_t)index;
    }
    return true;
}

/* Run the whole stream.  @p output may be NULL, in which case the run only
 * measures; @p limit bounds it either way.  Succeeds only on an explicit END
 * code, which every sample in the corpus reaches. */
static bool xx_winlink_lzw_run(const uint8_t *input, size_t input_size,
                               uint8_t *output, size_t limit,
                               size_t *produced) {
    xx_winlink_lzw lzw;
    xx_winlink_bits bits;
    xx_winlink_sink sink;
    bool finished = false;

    if (produced) *produced = 0U;
    if (!input || input_size == 0U) return false;
    xx_mem_zero(&lzw, sizeof(lzw));
    if (!xx_winlink_lzw_setup(&lzw)) return false;
    bits.data = input;
    bits.size = input_size;
    bits.position = 0U;
    bits.accumulator = 0U;
    bits.available = 0U;
    sink.data = output;
    sink.limit = limit;
    sink.count = 0U;

    /* One turn of this loop is one segment: the run from the start of the
     * stream, or from a CLEAR, up to the next CLEAR or the END. */
    while (!finished) {
        unsigned width = XX_WINLINK_LZW_MIN_BITS;
        unsigned next = XX_WINLINK_LZW_FIRST;
        unsigned code = 0U;
        unsigned previous;
        unsigned first;

        if (!xx_winlink_next_code(&bits, &width, next, &code)) goto done;
        if (code == XX_WINLINK_LZW_END) {
            finished = true;
            break;
        }
        /* U3 writes the low byte of whatever opens a segment; a code that is
         * not a literal there means the stream is not this codec's, so it is
         * refused instead of guessed at. */
        if (code > 0xffU) goto done;
        if (!xx_winlink_sink_put(&sink, (uint8_t)code)) goto done;
        first = code;
        previous = code;

        for (;;) {
            unsigned current;
            size_t depth = 0U;

            if (!xx_winlink_next_code(&bits, &width, next, &code)) goto done;
            if (code == XX_WINLINK_LZW_CLEAR) break;
            if (code == XX_WINLINK_LZW_END) {
                finished = true;
                break;
            }
            /* A code past the next assignable one cannot be resolved; only
             * exactly-next is the legal self-referential case. */
            if (code > next || code >= XX_WINLINK_LZW_TABLE) goto done;
            current = code;
            if (current == next) {
                lzw.stack[depth++] = (uint8_t)first;
                current = previous;
            }
            while (current > 0xffU) {
                if (current >= XX_WINLINK_LZW_TABLE ||
                    depth >= XX_WINLINK_LZW_TABLE)
                    goto done;
                lzw.stack[depth++] = lzw.suffix[current];
                current = lzw.prefix[current];
            }
            if (depth >= XX_WINLINK_LZW_TABLE) goto done;
            first = current;
            lzw.stack[depth++] = (uint8_t)current;
            while (depth != 0U)
                if (!xx_winlink_sink_put(&sink, lzw.stack[--depth])) goto done;
            if (next < XX_WINLINK_LZW_TABLE) {
                lzw.prefix[next] = (uint16_t)previous;
                lzw.suffix[next] = (uint8_t)current;
                ++next;
            }
            previous = code;
        }
    }
done:
    xx_winlink_lzw_cleanup(&lzw);
    if (finished && produced) *produced = sink.count;
    return finished;
}

static uint8_t *xx_winlink_read_packed(Abstractformat *self,
                                       const xx_winlink_member *member,
                                       size_t *size) {
    uint8_t *packed;

    if (!self || !member || !size) return NULL;
    if (member->compressed_size < 2 ||
        member->compressed_size > XX_WINLINK_MAX_PACKED)
        return NULL;
    packed = (uint8_t *)xx_mem_alloc((size_t)member->compressed_size);
    if (!packed) return NULL;
    if (!xx_winlink_read_at(self, member->data_offset, packed,
                            (size_t)member->compressed_size)) {
        xx_mem_free(packed);
        return NULL;
    }
    *size = (size_t)member->compressed_size;
    return packed;
}

/* The container stores no plaintext length, so it is measured by running the
 * stream once with no output buffer. */
static bool xx_winlink_measure(Abstractformat *self,
                               xx_winlink_member *member) {
    uint8_t *packed = NULL;
    size_t packed_size = 0U;
    size_t produced = 0U;
    bool result;

    if (!self || !member) return false;
    if (member->unpacked_size != 0U) return true;
    packed = xx_winlink_read_packed(self, member, &packed_size);
    if (!packed) return false;
    result = xx_winlink_lzw_run(packed, packed_size, NULL,
                                XX_WINLINK_MAX_OUTPUT, &produced) &&
             produced != 0U;
    xx_mem_free(packed);
    if (!result) return false;
    member->unpacked_size = (uint64_t)produced;
    return true;
}

static bool xx_winlink_decode(Abstractformat *self,
                              const xx_winlink_member *member, uint8_t **plain,
                              size_t *plain_size) {
    uint8_t *packed = NULL;
    uint8_t *output = NULL;
    size_t packed_size = 0U;
    size_t produced = 0U;
    size_t output_size;

    if (!self || !member || !plain || !plain_size) return false;
    if (member->unpacked_size == 0U || member->unpacked_size > SIZE_MAX)
        return false;
    output_size = (size_t)member->unpacked_size;
    packed = xx_winlink_read_packed(self, member, &packed_size);
    if (!packed) return false;
    output = (uint8_t *)xx_mem_alloc(output_size);
    if (!output ||
        !xx_winlink_lzw_run(packed, packed_size, output, output_size,
                            &produced) ||
        produced != output_size) {
        xx_mem_free(packed);
        if (output) xx_mem_free(output);
        return false;
    }
    xx_mem_free(packed);
    *plain = output;
    *plain_size = produced;
    return true;
}

/* --------------------------------------------------------- lifecycle --- */

static void xx_winlink_vtable_destroy(Abstractformat *self);

void xx_winlink_init(xx_winlink *archive, xx_io_device *device,
                     int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_WINLINK_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-winlink");
    xx_format_set_extension(&archive->format, "_xe");
    archive->format.check_is_valid = xx_winlink_check_is_valid;
    archive->format.handle_base_info = xx_winlink_handle_base_info;
    archive->format.get_format_size = xx_winlink_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_winlink_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_winlink_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_winlink_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_winlink_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_winlink_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_winlink_free_archive_records_reading;
    archive->format.destroy = xx_winlink_vtable_destroy;
}

xx_winlink *xx_winlink_create(xx_io_device *device, int64_t base_address) {
    xx_winlink *archive = (xx_winlink *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_winlink_init(archive, device, base_address);
    return archive;
}

void xx_winlink_destroy(xx_winlink *archive) {
    if (!archive) return;
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_winlink_free(xx_winlink *archive) {
    if (!archive) return;
    xx_winlink_destroy(archive);
    xx_mem_free(archive);
}

static void xx_winlink_vtable_destroy(Abstractformat *self) {
    xx_winlink_destroy((xx_winlink *)self);
}

/* ------------------------------------------------------------ format --- */

bool xx_winlink_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_winlink_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_winlink_parse(self, pd);
    if (!stream) return false;
    xx_winlink_stream_free(stream);
    return true;
}

bool xx_winlink_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_winlink *archive = (xx_winlink *)self;
    xx_winlink_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    self->base_info_handled = true;
    stream = xx_winlink_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    archive->timestamp = stream->timestamp;
    xx_winlink_stream_free(stream);
    return true;
}

int64_t xx_winlink_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd)))
        return 0;
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_winlink_get_number_of_archive_records(Abstractformat *self,
                                                  xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd)))
        return 0U;
    return self->is_valid ? ((xx_winlink *)self)->number_of_records : 0U;
}

/* ----------------------------------------------------------- records --- */

static bool xx_winlink_set_record(xx_archive_record *record,
                                  const xx_winlink_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = member->header_size;
    record->data_offset = member->data_offset;
    record->compressed_size = member->compressed_size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->compressed_size) &&
           /* Measured by running the stream; 0 when it would not decode. */
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          member->unpacked_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP,
                                          member->timestamp) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

static bool xx_winlink_copy_options(xx_list_s *target,
                                    const xx_list_s *options) {
    size_t index;

    if (!options) return true;
    if (!target) return false;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *source =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        xx_meta copied;
        if (!source) continue;
        xx_meta_init(&copied, source->meta_id);
        if (!xx_var_copy(&copied.var, &source->var) ||
            !xx_list_append(target, &copied)) {
            xx_meta_cleanup(&copied);
            return false;
        }
    }
    return true;
}

static const xx_var *xx_winlink_option(const xx_list_s *options,
                                       uint32_t meta_id) {
    size_t index;

    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == meta_id) return &meta->var;
    }
    return NULL;
}

xx_archive_record_state *xx_winlink_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_winlink_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_winlink_parse(self, pd);
    if (!stream) return NULL;
    /* No plaintext length is stored, so it is measured here.  A stream that
     * will not measure is still listed - with size 0 for "unknown" - and
     * unpacking it later fails closed. */
    (void)xx_winlink_measure(self, &stream->member);
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_winlink_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_winlink_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_winlink_copy_options(&state->options, options) ||
        !xx_winlink_set_record(&state->current_record, &stream->member)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_winlink_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_winlink_archive_record_move_to_next(Abstractformat *self,
                                            xx_archive_record_state *state,
                                            xx_pd_struct *pd) {
    (void)pd;
    if (!self || !state || state->format != self) return false;
    /* A WinLink container holds exactly one member. */
    xx_archive_record_cleanup(&state->current_record);
    xx_archive_record_init(&state->current_record);
    state->has_record = false;
    return false;
}

bool xx_winlink_unpack_current_archive_record(Abstractformat *self,
                                              xx_archive_record_state *state,
                                              xx_pd_struct *pd) {
    xx_winlink_stream *stream;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U;
    size_t written = 0U;
    bool result = false;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd)))
        return false;
    stream = (xx_winlink_stream *)state->internal_state;
    if (!stream) return false;
    if (!xx_winlink_measure(self, &stream->member)) return false;
    if (!xx_winlink_decode(self, &stream->member, &plain, &plain_size))
        return false;
    /* With no unpack path the caller only wanted to know the member decodes;
     * it does, so this is a success with nothing written. */
    path_option =
        xx_winlink_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        result = true;
        goto done;
    }
    if (path_option->type == XX_VAR_TYPE_STRING ||
        path_option->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(path_option);
    } else if (path_option->type == XX_VAR_TYPE_WSTRING ||
               path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base = owned_base;
    }
    if (!base) goto done;
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' &&
            base[xx_str_len(base) - 1U] != '\\')
               ? xx_str_concat3(base, "/", stream->member.name)
               : xx_str_concat(base, stream->member.name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        if (!destination) goto done;
        result = true;
        while (written < plain_size) {
            ssize_t amount =
                xx_io_write(destination, plain + written, plain_size - written);
            if (amount <= 0 || (size_t)amount > plain_size - written) {
                result = false;
                break;
            }
            written += (size_t)amount;
        }
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    xx_mem_free(plain);
    xx_str_free(path);
    xx_str_free(owned_base);
    return result;
}

void xx_winlink_free_archive_records_reading(Abstractformat *self,
                                             xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
