/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * JBOOT SCH2 / STAG / ARM firmware headers.  The field layouts and the
 * validation rules follow binwalk's src/structures/jboot.rs plus the three
 * parsers in src/signatures/jboot.rs and the SCH2 carver in
 * src/extractors/jboot.rs; the per-field notes live in xx_jboot.h.
 *
 * All three headers are thin wrappers: what follows them is an LZMA or gzip
 * kernel, or a squashfs section, which other readers in this library already
 * decode.  So the job here is to validate the header, verify every checksum
 * that can be verified, and publish the payload as one archive record whose
 * bytes a caller can carve out and feed back into the detector.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/jboot/xx_jboot.h"

#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the alias macro defined next to the enumerator is tested instead;
 * this picks up the real file type as soon as JBOOT is registered there. */
#ifdef JBOOT
#define XX_JBOOT_FILE_TYPE XX_FILE_TYPE_JBOOT
#else
#define XX_JBOOT_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

/** Streaming buffer for the SCH2 kernel CRC pass. */
#define XX_JBOOT_STAGING_SIZE 65536U

/** Offsets of the SCH2 header_crc field, which is zeroed for its own CRC. */
#define XX_JBOOT_SCH2_CRC_START 32U
#define XX_JBOOT_SCH2_CRC_END 36U

/** Largest header this reader ever reads in one go. */
#define XX_JBOOT_MAX_HEADER XX_JBOOT_ARM_HEADER_SIZE

typedef struct xx_jboot_private_s {
    int64_t input_size;
    int64_t data_offset;
    int64_t data_size;
    int64_t archive_end;
    char *name;
    uint32_t variant;
    uint32_t header_size;
    uint32_t payload_size;
    uint32_t compression;
    uint32_t kernel_size;
    uint32_t kernel_crc;
    uint32_t kernel_entry_point;
    uint32_t rootfs_flash_address;
    uint32_t rootfs_size;
    uint32_t rootfs_crc;
    uint32_t cmd_line_size;
    uint32_t stag_cmark;
    uint32_t stag_id;
    uint32_t erase_start;
    uint32_t erase_size;
    uint32_t data_start;
    uint32_t header_version;
    uint32_t section_id;
    uint32_t family;
    uint32_t timestamp;
    char rom_id[XX_JBOOT_ARM_ROM_ID_SIZE + 1U];
    bool is_factory_image;
    bool is_sysupgrade_image;
    bool consumed;
} xx_jboot_private;

static void xx_jboot_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

/* All positioning goes through seek64: a JBOOT header is bounded by 32-bit
 * length fields but its base address inside a larger flash dump is not, and
 * long is 32-bit on Win64. */
static bool xx_jboot_read_at(xx_io_device *device, int64_t offset, void *data,
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

static bool xx_jboot_add(int64_t left, uint64_t right, int64_t *result) {
    if (!result || left < 0 || right > (uint64_t)(INT64_MAX - left)) {
        return false;
    }
    *result = left + (int64_t)right;
    return true;
}

/* True when [offset, offset + size) lies inside [0, total_size). */
static bool xx_jboot_range_within(int64_t total_size, int64_t offset,
                                  int64_t size) {
    return (total_size >= 0) && (offset >= 0) && (size >= 0) &&
           (offset <= total_size) && (size <= total_size - offset);
}

static void xx_jboot_private_cleanup(xx_jboot_private *parsed) {
    if (!parsed) return;
    if (parsed->name) xx_str_free(parsed->name);
    xx_mem_zero(parsed, sizeof(*parsed));
    parsed->input_size = -1;
    parsed->archive_end = -1;
}

static void xx_jboot_private_free(void *pointer) {
    xx_jboot_private *parsed = (xx_jboot_private *)pointer;

    if (!parsed) return;
    xx_jboot_private_cleanup(parsed);
    xx_mem_free(parsed);
}

/* The CRC used by SCH2 is the ordinary ISO-HDLC CRC-32, the one
 * xx_crc32_calc(0, ...) produces.  It is NOT the JAMCRC variant that TRX
 * uses, so no final complement is applied here.  xx_crc32_calc is composable
 * across chunks, which is what lets the kernel be streamed rather than
 * buffered whole. */
static bool xx_jboot_crc_range(xx_io_device *device, int64_t offset,
                               int64_t size, uint32_t *out_crc,
                               xx_pd_struct *pd) {
    uint8_t staging[XX_JBOOT_STAGING_SIZE];
    uint32_t crc = 0U;

    if (!device || !out_crc || offset < 0 || size < 0) return false;
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
        crc = xx_crc32_calc(crc, staging, step);
        size -= (int64_t)step;
    }
    *out_crc = crc;
    return true;
}

/* The ROM ID is a NUL-padded board string straight out of the file.  It is
 * reported, never used as a path component, but it is still clamped to
 * printable ASCII so that a hostile image cannot inject control characters
 * into a caller's log. */
static void xx_jboot_copy_rom_id(char *destination, const uint8_t *source) {
    size_t index;

    for (index = 0U; index < XX_JBOOT_ARM_ROM_ID_SIZE; ++index) {
        uint8_t character = source[index];
        if (character == 0U) break;
        if (character < 0x20U || character > 0x7eU) character = '?';
        destination[index] = (char)character;
    }
    destination[index] = '\0';
}

/* --------------------------------------------------------------- SCH2 --- */

static bool xx_jboot_parse_sch2(Abstractformat *self, xx_jboot_private *parsed,
                                const uint8_t *header, xx_pd_struct *pd) {
    uint32_t stored_header_crc;
    uint32_t computed;
    uint8_t scratch[XX_JBOOT_SCH2_HEADER_SIZE];
    size_t index;

    parsed->header_size = XX_JBOOT_SCH2_HEADER_SIZE;
    parsed->compression = xx_data_get_u8(header, XX_JBOOT_SCH2_HEADER_SIZE, 2U);
    parsed->kernel_entry_point =
        xx_data_get_u32(header, XX_JBOOT_SCH2_HEADER_SIZE, 4U, false);
    parsed->kernel_size =
        xx_data_get_u32(header, XX_JBOOT_SCH2_HEADER_SIZE, 8U, false);
    parsed->kernel_crc =
        xx_data_get_u32(header, XX_JBOOT_SCH2_HEADER_SIZE, 12U, false);
    parsed->rootfs_flash_address =
        xx_data_get_u32(header, XX_JBOOT_SCH2_HEADER_SIZE, 20U, false);
    parsed->rootfs_size =
        xx_data_get_u32(header, XX_JBOOT_SCH2_HEADER_SIZE, 24U, false);
    parsed->rootfs_crc =
        xx_data_get_u32(header, XX_JBOOT_SCH2_HEADER_SIZE, 28U, false);
    stored_header_crc =
        xx_data_get_u32(header, XX_JBOOT_SCH2_HEADER_SIZE, 32U, false);
    parsed->cmd_line_size =
        xx_data_get_u16(header, XX_JBOOT_SCH2_HEADER_SIZE, 38U, false);

    /* version, the self-declared header size and the compression selector all
     * have to agree with the one published layout; a header that disagrees is
     * not a header whose remaining fields can be trusted. */
    if (xx_data_get_u8(header, XX_JBOOT_SCH2_HEADER_SIZE, 3U) != 2U) {
        return false;
    }
    if (xx_data_get_u16(header, XX_JBOOT_SCH2_HEADER_SIZE, 36U, false) !=
        XX_JBOOT_SCH2_HEADER_SIZE) {
        return false;
    }
    if (parsed->compression > (uint32_t)XX_JBOOT_COMPRESSION_LZMA) return false;

    /* Header CRC: the whole header with its own CRC field zeroed. */
    for (index = 0U; index < XX_JBOOT_SCH2_HEADER_SIZE; ++index) {
        scratch[index] =
            (index >= XX_JBOOT_SCH2_CRC_START && index < XX_JBOOT_SCH2_CRC_END)
                ? (uint8_t)0U
                : header[index];
    }
    if (xx_crc32_calc(0U, scratch, XX_JBOOT_SCH2_HEADER_SIZE) !=
        stored_header_crc) {
        return false;
    }

    /* The kernel size is attacker-controlled.  Bound it against the device
     * HERE, at parse time, before the CRC pass is allowed to stream it and
     * long before anything tries to carve it: a 4 GB declaration in front of
     * a 200-byte file must not turn into a 4 GB read. */
    if (!xx_jboot_add(self->base_address, parsed->header_size,
                      &parsed->data_offset)) {
        return false;
    }
    parsed->data_size = (int64_t)parsed->kernel_size;
    if (!xx_jboot_range_within(parsed->input_size, parsed->data_offset,
                               parsed->data_size)) {
        return false;
    }
    if (!xx_jboot_add(parsed->data_offset, parsed->kernel_size,
                      &parsed->archive_end)) {
        return false;
    }
    parsed->payload_size = parsed->kernel_size;

    /* The kernel CRC is what the bootloader itself checks before jumping, so
     * a mismatch is a parse failure here too. */
    if (!xx_jboot_crc_range(self->device, parsed->data_offset,
                            parsed->data_size, &computed, pd) ||
        computed != parsed->kernel_crc) {
        return false;
    }
    parsed->name = xx_str_create("kernel.bin");
    return parsed->name != NULL;
}

/* --------------------------------------------------------------- STAG --- */

static bool xx_jboot_parse_stag(Abstractformat *self, xx_jboot_private *parsed,
                                const uint8_t *header) {
    parsed->header_size = XX_JBOOT_STAG_HEADER_SIZE;
    parsed->stag_cmark = xx_data_get_u8(header, XX_JBOOT_STAG_HEADER_SIZE, 0U);
    parsed->stag_id = xx_data_get_u8(header, XX_JBOOT_STAG_HEADER_SIZE, 1U);
    parsed->timestamp =
        xx_data_get_u32(header, XX_JBOOT_STAG_HEADER_SIZE, 4U, false);
    parsed->payload_size =
        xx_data_get_u32(header, XX_JBOOT_STAG_HEADER_SIZE, 8U, false);

    if (xx_data_get_u16(header, XX_JBOOT_STAG_HEADER_SIZE, 2U, false) !=
        XX_JBOOT_STAG_MAGIC) {
        return false;
    }
    /* id is fixed at 0x04 by every known producer and by both of binwalk's
     * STAG signatures; with no verifiable checksum it is part of the magic.
     * 0xFF marks a factory image; otherwise cmark must repeat id.  The
     * reference requires image_size to exceed the header size, which is the
     * only other thing standing between this 4-byte magic and every run of
     * 04 04 24 2B in a random binary, so it is kept. */
    if (parsed->stag_id != XX_JBOOT_STAG_ID) return false;
    parsed->is_factory_image = parsed->stag_cmark == XX_JBOOT_STAG_FACTORY_CMARK;
    parsed->is_sysupgrade_image = parsed->stag_cmark == parsed->stag_id;
    if (!parsed->is_factory_image && !parsed->is_sysupgrade_image) return false;
    if (parsed->payload_size <= XX_JBOOT_STAG_HEADER_SIZE) return false;

    if (!xx_jboot_add(self->base_address, parsed->header_size,
                      &parsed->data_offset)) {
        return false;
    }
    parsed->data_size = (int64_t)parsed->payload_size;
    /* Bounded at PARSE.  binwalk is stricter still and demands the payload end
     * STRICTLY before end of file, on the grounds that a STAG header describes
     * a kernel and should not consume the whole image.  That rule rejects a
     * correctly carved standalone STAG image, so the bound used here is
     * "inside the device" and the extra strictness is left to the dispatcher's
     * ordering.  See the port report. */
    if (!xx_jboot_range_within(parsed->input_size, parsed->data_offset,
                               parsed->data_size)) {
        return false;
    }
    if (!xx_jboot_add(parsed->data_offset, parsed->payload_size,
                      &parsed->archive_end)) {
        return false;
    }
    parsed->name = xx_str_create("kernel.bin");
    return parsed->name != NULL;
}

/* ---------------------------------------------------------------- ARM --- */

static bool xx_jboot_parse_arm(Abstractformat *self, xx_jboot_private *parsed,
                               const uint8_t *header) {
    parsed->header_size = XX_JBOOT_ARM_HEADER_SIZE;

    /* Sixteen must-be-zero reserved bytes at +48, two more reserved fields,
     * lpvs == 1, mbz == 0 and the "BH" header id are what make this header
     * identifiable at all: there is no magic in the first bytes, only a board
     * string. */
    if (xx_data_get_u32(header, XX_JBOOT_ARM_HEADER_SIZE, 20U, false) != 0U ||
        xx_data_get_u16(header, XX_JBOOT_ARM_HEADER_SIZE, 24U, false) != 0U ||
        xx_data_get_u32(header, XX_JBOOT_ARM_HEADER_SIZE, 48U, false) != 0U ||
        xx_data_get_u32(header, XX_JBOOT_ARM_HEADER_SIZE, 52U, false) != 0U ||
        xx_data_get_u32(header, XX_JBOOT_ARM_HEADER_SIZE, 56U, false) != 0U ||
        xx_data_get_u32(header, XX_JBOOT_ARM_HEADER_SIZE, 60U, false) != 0U ||
        xx_data_get_u16(header, XX_JBOOT_ARM_HEADER_SIZE, 68U, false) != 0U) {
        return false;
    }
    if (xx_data_get_u8(header, XX_JBOOT_ARM_HEADER_SIZE, 26U) != 1U ||
        xx_data_get_u8(header, XX_JBOOT_ARM_HEADER_SIZE, 27U) != 0U) {
        return false;
    }
    if (xx_data_get_u16(header, XX_JBOOT_ARM_HEADER_SIZE, 64U, false) !=
        XX_JBOOT_ARM_MAGIC) {
        return false;
    }
    if (xx_data_get_u16(header, XX_JBOOT_ARM_HEADER_SIZE, 66U, false) > 4U) {
        return false;
    }

    xx_jboot_copy_rom_id(parsed->rom_id, header);
    parsed->timestamp =
        xx_data_get_u32(header, XX_JBOOT_ARM_HEADER_SIZE, 28U, false);
    parsed->erase_start =
        xx_data_get_u32(header, XX_JBOOT_ARM_HEADER_SIZE, 32U, false);
    parsed->erase_size =
        xx_data_get_u32(header, XX_JBOOT_ARM_HEADER_SIZE, 36U, false);
    parsed->data_start =
        xx_data_get_u32(header, XX_JBOOT_ARM_HEADER_SIZE, 40U, false);
    parsed->payload_size =
        xx_data_get_u32(header, XX_JBOOT_ARM_HEADER_SIZE, 44U, false);
    parsed->header_version =
        xx_data_get_u16(header, XX_JBOOT_ARM_HEADER_SIZE, 66U, false);
    parsed->section_id = xx_data_get_u8(header, XX_JBOOT_ARM_HEADER_SIZE, 70U);
    parsed->family =
        xx_data_get_u16(header, XX_JBOOT_ARM_HEADER_SIZE, 76U, false);

    if (!xx_jboot_add(self->base_address, parsed->header_size,
                      &parsed->data_offset)) {
        return false;
    }
    parsed->data_size = (int64_t)parsed->payload_size;
    /* data_size is a bare 32-bit field; bound it at parse. */
    if (!xx_jboot_range_within(parsed->input_size, parsed->data_offset,
                               parsed->data_size)) {
        return false;
    }
    if (!xx_jboot_add(parsed->data_offset, parsed->payload_size,
                      &parsed->archive_end)) {
        return false;
    }
    parsed->name = xx_str_create("data.bin");
    return parsed->name != NULL;
}

/* -------------------------------------------------------------- parse --- */

static bool xx_jboot_parse(Abstractformat *self, xx_jboot_private *parsed,
                           xx_pd_struct *pd) {
    uint8_t header[XX_JBOOT_MAX_HEADER];
    uint32_t sch2_magic;
    bool ok = false;

    if (parsed) {
        xx_mem_zero(parsed, sizeof(*parsed));
        parsed->input_size = -1;
        parsed->archive_end = -1;
    }
    if (!self || !self->device || !parsed || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    parsed->input_size = xx_io_total_size(self->device);

    /* SCH2 first: it is the only one of the three whose header carries a CRC
     * over itself, so it is both the cheapest to be sure about and the least
     * likely to be a false positive. */
    if (xx_jboot_range_within(parsed->input_size, self->base_address,
                              XX_JBOOT_SCH2_HEADER_SIZE) &&
        xx_jboot_read_at(self->device, self->base_address, header,
                         XX_JBOOT_SCH2_HEADER_SIZE)) {
        sch2_magic =
            xx_data_get_u16(header, XX_JBOOT_SCH2_HEADER_SIZE, 0U, false);
        if (sch2_magic == XX_JBOOT_SCH2_MAGIC) {
            if (xx_jboot_parse_sch2(self, parsed, header, pd)) {
                parsed->variant = (uint32_t)XX_JBOOT_VARIANT_SCH2;
                ok = true;
            } else {
                /* Reset before the next attempt: a half-filled parse must
                 * not leak fields into the variant that finally matches. */
                int64_t remembered = parsed->input_size;
                xx_jboot_private_cleanup(parsed);
                parsed->input_size = remembered;
            }
        }
    }

    if (!ok && xx_jboot_range_within(parsed->input_size, self->base_address,
                                     XX_JBOOT_ARM_HEADER_SIZE) &&
        xx_jboot_read_at(self->device, self->base_address, header,
                         XX_JBOOT_ARM_HEADER_SIZE)) {
        if (xx_data_get_u16(header, XX_JBOOT_ARM_HEADER_SIZE, 64U, false) ==
            XX_JBOOT_ARM_MAGIC) {
            if (xx_jboot_parse_arm(self, parsed, header)) {
                parsed->variant = (uint32_t)XX_JBOOT_VARIANT_ARM;
                ok = true;
            } else {
                int64_t remembered = parsed->input_size;
                xx_jboot_private_cleanup(parsed);
                parsed->input_size = remembered;
            }
        }
    }

    /* STAG last: its 4-byte magic is the weakest of the three and its header
     * carries nothing this reader can verify, so it must not get first refusal
     * on a file one of the others would have claimed. */
    if (!ok && xx_jboot_range_within(parsed->input_size, self->base_address,
                                     XX_JBOOT_STAG_HEADER_SIZE) &&
        xx_jboot_read_at(self->device, self->base_address, header,
                         XX_JBOOT_STAG_HEADER_SIZE)) {
        if (xx_data_get_u16(header, XX_JBOOT_STAG_HEADER_SIZE, 2U, false) ==
                XX_JBOOT_STAG_MAGIC &&
            xx_jboot_parse_stag(self, parsed, header)) {
            parsed->variant = (uint32_t)XX_JBOOT_VARIANT_STAG;
            ok = true;
        }
    }

    if (!ok) goto fail;
    return true;
fail:
    xx_jboot_private_cleanup(parsed);
    return false;
}

/* ------------------------------------------------------------- records -- */

static bool xx_jboot_copy_options(xx_list_s *destination,
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

static const xx_var *xx_jboot_find_option(const xx_list_s *options,
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

/* The STAG/ARM time_stamp is not a Unix time.  OpenWrt firmware-utils
 * (mkdlinkfw-lib.c jboot_timestamp) writes it as
 *     (((uint32_t)unix_seconds) - TIMESTAMP_MAGIC) >> 2
 * with TIMESTAMP_MAGIC 0x35016f00 (mkdlinkfw-lib.h), i.e. 4-second ticks since 1998-03-07 16:00:00 UTC.  XX_META_ID_TIMESTAMP
 * carries Unix seconds in this library (tar, cpio, igf1, bwcf, miz; the tar
 * and cpio writers read it back as an mtime), so the field is converted.
 * 0 is "no timestamp", and a value with either of the top two bits set cannot
 * come out of the >> 2, so neither is published. */
static bool xx_jboot_timestamp_to_unix(uint32_t field, uint64_t *unix_time) {
    if (!unix_time || field == 0U || field > XX_JBOOT_TIMESTAMP_MAX) {
        return false;
    }
    *unix_time = (uint64_t)field * XX_JBOOT_TIMESTAMP_TICK +
                 XX_JBOOT_TIMESTAMP_EPOCH;
    return true;
}

static bool xx_jboot_populate_record(xx_archive_record *record,
                                     const xx_jboot_private *parsed) {
    uint64_t unix_time = 0U;

    if (!record || !parsed || !parsed->name) return false;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = parsed->data_offset - (int64_t)parsed->header_size;
    record->header_size = (int64_t)parsed->header_size;
    record->data_offset = parsed->data_offset;
    record->compressed_size = parsed->data_size;
    /* The payload is stored verbatim behind the header.  Whether it is itself
     * gzip or LZMA is the payload's business, not the container's, so the
     * container reports "none" and the caller recurses. */
    if (!xx_archive_record_set_original_name(record, parsed->name) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                        (uint64_t)parsed->data_size) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                        (uint64_t)parsed->data_size) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                        0U) ||
        !xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false) ||
        !xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                         false)) {
        return false;
    }
    if (xx_jboot_timestamp_to_unix(parsed->timestamp, &unix_time) &&
        !xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP,
                                        unix_time)) {
        return false;
    }
    return true;
}

/* ----------------------------------------------------------- lifecycle -- */

void xx_jboot_init(xx_jboot *jboot, xx_io_device *dev, int64_t base_address) {
    if (!jboot) return;
    xx_mem_zero(jboot, sizeof(*jboot));
    xx_format_init(&jboot->format, dev, base_address);
    jboot->format.endian = XX_ENDIAN_LITTLE;
    jboot->format.file_type = XX_JBOOT_FILE_TYPE;
    jboot->format.format_type = XX_TYPE_ARCHIVE;
    jboot->format.is_archive = true;
    xx_format_set_mime_type(&jboot->format, "application/x-jboot-firmware");
    xx_format_set_extension(&jboot->format, "bin");
    jboot->format.check_is_valid = xx_jboot_check_is_valid;
    jboot->format.handle_base_info = xx_jboot_handle_base_info;
    jboot->format.get_format_size = xx_jboot_get_format_size;
    jboot->format.get_number_of_archive_records =
        xx_jboot_get_number_of_archive_records;
    jboot->format.create_archive_records_reading =
        xx_jboot_create_archive_records_reading;
    jboot->format.get_current_archive_record =
        xx_jboot_get_current_archive_record;
    jboot->format.unpack_current_archive_record =
        xx_jboot_unpack_current_archive_record;
    jboot->format.archive_record_move_to_next =
        xx_jboot_archive_record_move_to_next;
    jboot->format.free_archive_records_reading =
        xx_jboot_free_archive_records_reading;
    jboot->format.destroy = xx_jboot_vtable_destroy;
    jboot->archive_end = -1;
}

xx_jboot *xx_jboot_create(xx_io_device *dev, int64_t base_address) {
    xx_jboot *jboot = (xx_jboot *)xx_mem_alloc(sizeof(*jboot));

    if (jboot) xx_jboot_init(jboot, dev, base_address);
    return jboot;
}

void xx_jboot_destroy(xx_jboot *jboot) {
    if (!jboot) return;
    if (jboot->internal) {
        xx_jboot_private_free(jboot->internal);
        jboot->internal = NULL;
    }
    xx_format_cleanup_extra_parameters(&jboot->format);
}

static void xx_jboot_vtable_destroy(Abstractformat *self) {
    xx_jboot_destroy((xx_jboot *)self);
}

void xx_jboot_free(xx_jboot *jboot) {
    if (!jboot) return;
    xx_jboot_destroy(jboot);
    xx_mem_free(jboot);
}

/* -------------------------------------------------------------- format -- */

bool xx_jboot_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_jboot_private parsed;
    bool result = xx_jboot_parse(self, &parsed, pd);

    xx_jboot_private_cleanup(&parsed);
    return result;
}

bool xx_jboot_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_jboot *jboot = (xx_jboot *)self;
    xx_jboot_private *parsed;
    int64_t total_size;

    if (!self || !jboot) return false;
    parsed = (xx_jboot_private *)xx_mem_alloc(sizeof(*parsed));
    if (!parsed || !xx_jboot_parse(self, parsed, pd)) {
        if (parsed) xx_mem_free(parsed);
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    if (jboot->internal) xx_jboot_private_free(jboot->internal);
    jboot->internal = parsed;
    jboot->number_of_records = 1U;
    jboot->variant = parsed->variant;
    jboot->header_size = parsed->header_size;
    jboot->payload_size = parsed->payload_size;
    jboot->compression = parsed->compression;
    jboot->kernel_size = parsed->kernel_size;
    jboot->kernel_crc = parsed->kernel_crc;
    jboot->kernel_entry_point = parsed->kernel_entry_point;
    jboot->rootfs_flash_address = parsed->rootfs_flash_address;
    jboot->rootfs_size = parsed->rootfs_size;
    jboot->rootfs_crc = parsed->rootfs_crc;
    jboot->cmd_line_size = parsed->cmd_line_size;
    jboot->stag_cmark = parsed->stag_cmark;
    jboot->stag_id = parsed->stag_id;
    jboot->is_factory_image = parsed->is_factory_image;
    jboot->is_sysupgrade_image = parsed->is_sysupgrade_image;
    jboot->erase_start = parsed->erase_start;
    jboot->erase_size = parsed->erase_size;
    jboot->data_start = parsed->data_start;
    jboot->header_version = parsed->header_version;
    jboot->section_id = parsed->section_id;
    jboot->family = parsed->family;
    jboot->timestamp = parsed->timestamp;
    xx_rt_memcpy(jboot->rom_id, parsed->rom_id, sizeof(jboot->rom_id));
    jboot->archive_end = parsed->archive_end;
    self->format_size = parsed->archive_end - self->base_address;
    total_size = xx_io_total_size(self->device);
    if (total_size > parsed->archive_end) {
        self->overlay_offset = parsed->archive_end;
        self->overlay_size = total_size - parsed->archive_end;
    } else {
        self->overlay_offset = -1;
        self->overlay_size = 0;
    }
    self->number_of_archive_records = 1U;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_jboot_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return -1;
    }
    return self->format_size;
}

uint64_t xx_jboot_get_number_of_archive_records(Abstractformat *self,
                                                xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return ((xx_jboot *)self)->number_of_records;
}

/* ------------------------------------------------------ record reading -- */

xx_archive_record_state *xx_jboot_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_record_state *state;
    xx_jboot_private *parsed;

    if (!self || !self->device ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    parsed = (xx_jboot_private *)xx_mem_alloc(sizeof(*parsed));
    if (!state || !parsed) {
        if (state) xx_mem_free(state);
        if (parsed) xx_mem_free(parsed);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    if (!xx_jboot_parse(self, parsed, pd)) {
        xx_jboot_private_free(parsed);
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->internal_state = parsed;
    state->free_internal = xx_jboot_private_free;
    state->total_records = 1;
    if (!xx_jboot_copy_options(&state->options, options) ||
        !xx_jboot_populate_record(&state->current_record, parsed)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_jboot_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_jboot_archive_record_move_to_next(Abstractformat *self,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    xx_jboot_private *parsed;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    /* Every variant describes exactly one payload region, so the first step
     * is always the last. */
    parsed = (xx_jboot_private *)state->internal_state;
    if (parsed) parsed->consumed = true;
    xx_archive_record_cleanup(&state->current_record);
    xx_archive_record_init(&state->current_record);
    state->has_record = false;
    return false;
}

bool xx_jboot_unpack_current_archive_record(Abstractformat *self,
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
    option = xx_jboot_find_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!option) {
        /* No destination: report whether the payload's span is addressable. */
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
cleanup:
    if (owned_base) xx_str_free(owned_base);
    if (destination) xx_str_free(destination);
    return result;
}

void xx_jboot_free_archive_records_reading(Abstractformat *self,
                                           xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

/* ------------------------------------------------------------ accessors -- */

uint32_t xx_jboot_get_variant(const xx_jboot *jboot) {
    return jboot ? jboot->variant : (uint32_t)XX_JBOOT_VARIANT_NONE;
}

const char *xx_jboot_get_variant_name(const xx_jboot *jboot) {
    if (!jboot) return "none";
    switch ((xx_jboot_variant_t)jboot->variant) {
        case XX_JBOOT_VARIANT_SCH2: return "SCH2";
        case XX_JBOOT_VARIANT_STAG: return "STAG";
        case XX_JBOOT_VARIANT_ARM: return "ARM";
        case XX_JBOOT_VARIANT_NONE:
        default: return "none";
    }
}

uint32_t xx_jboot_get_header_size(const xx_jboot *jboot) {
    return jboot ? jboot->header_size : 0U;
}

uint32_t xx_jboot_get_payload_size(const xx_jboot *jboot) {
    return jboot ? jboot->payload_size : 0U;
}

const char *xx_jboot_get_rom_id(const xx_jboot *jboot) {
    return jboot ? jboot->rom_id : "";
}

int64_t xx_jboot_get_archive_end(const xx_jboot *jboot) {
    return jboot ? jboot->archive_end : -1;
}
