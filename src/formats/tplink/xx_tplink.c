/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/tplink/xx_tplink.h"

#include "xxfclib/algo/hash/xx_hash.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the file-type constant resolves to UNKNOWN until the enumerator
 * lands.  Delete this block once XX_FILE_TYPE_TPLINK exists in the enum. */
#ifdef TPLINK
#define XX_TPLINK_FILE_TYPE XX_FILE_TYPE_TPLINK
#else
#define XX_TPLINK_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

/** Streaming buffer for the MD5 pass. */
#define XX_TPLINK_STAGING_SIZE 65536U
/** Above this the advisory MD5 pass is skipped; it buys nothing on a blob
 *  that large and the walk is not free. */
#define XX_TPLINK_MD5_LIMIT (256 * 1024 * 1024)
/** Length of vendor_name and fw_version as stored. */
#define XX_TPLINK_VENDOR_FIELD_SIZE 24U
#define XX_TPLINK_FWVERSION_FIELD_SIZE 36U

/*
 * The two salts of OpenWrt's firmware-utils mktplinkfw.c (the v1, vendor-
 * string header this reader parses): md5salt_normal for an image without a
 * bootloader, md5salt_boot for one with.
 *
 * These are the one thing in this reader that could not be checked against a
 * local copy of their source - no copy of firmware-utils was available in the
 * offline reference tree, and binwalk's tplink module does not verify the
 * digest at all, so it carries no salt to compare against.  They are therefore
 * used for an ADVISORY check only: a match is reported, a mismatch changes
 * nothing.  If one of these constants is wrong, the cost is a missing
 * "md5 verified" flag, never a rejected image - and vendor-rebuilt firmware
 * uses its own salts anyway, so the check could never have been mandatory.
 * The digest is computed only by handle_base_info, never by the detection
 * probe (check_is_valid), so it costs nothing at detection time.
 */
#define XX_TPLINK_SALT_COUNT 2
static const uint8_t xx_tplink_md5_salts[XX_TPLINK_SALT_COUNT]
                                        [XX_TPLINK_MD5_SIZE] = {
    {0xDCU, 0xD7U, 0x3AU, 0xA5U, 0xC3U, 0x95U, 0x98U, 0xFBU, 0xDDU, 0xF9U,
     0xE7U, 0xF4U, 0x0EU, 0xAEU, 0x47U, 0x38U},
    {0x8CU, 0xEFU, 0x33U, 0x5BU, 0xD5U, 0xC5U, 0xCEU, 0xFAU, 0xA7U, 0x9CU,
     0x28U, 0xDAU, 0xB2U, 0xE9U, 0x0FU, 0x42U}};

/** region_code at +0x48 is a small enumerator (mktplinkfw: US = 1, the
 *  universal/EU/BR builds 0); anything past this is not a TP-Link header. */
#define XX_TPLINK_MAX_REGION_CODE 0xFFFFU

typedef struct xx_tplink_region_s {
    const char *name; /**< A literal chosen here, never from the file. */
    int64_t data_offset;
    int64_t data_size;
} xx_tplink_region;

typedef struct xx_tplink_private_s {
    xx_tplink_region regions[XX_TPLINK_MAX_RECORDS];
    size_t count;
    int64_t input_size;
    int64_t archive_end;
    xx_tplink_variant_t variant;
    uint32_t header_size;
    uint32_t image_size;
    uint32_t hardware_id;
    uint32_t hardware_revision;
    uint32_t kernel_load_address;
    uint32_t kernel_entry_point;
    uint32_t kernel_offset;
    uint32_t kernel_length;
    uint32_t rootfs_offset;
    uint32_t rootfs_length;
    uint32_t bootloader_offset;
    uint32_t bootloader_length;
    uint16_t version_major;
    uint16_t version_minor;
    uint16_t version_patch;
    uint16_t model_number;
    uint8_t hardware_rev_major;
    uint8_t hardware_rev_minor;
    uint8_t md5sum1[XX_TPLINK_MD5_SIZE];
    bool md5_checked;
    bool md5_valid;
    int md5_salt_index;
    bool header_big_endian;
    char *vendor_name;
    char *firmware_version;
} xx_tplink_private;

typedef struct xx_tplink_archive_stream_s {
    xx_tplink_private parsed;
    size_t index;
} xx_tplink_archive_stream;

static void xx_tplink_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------------------ */
/* Device helpers                                                            */
/* ------------------------------------------------------------------------ */

/* All positioning goes through seek64: the header fields are 32-bit but the
 * base address inside a larger carrier is not, and long is 32-bit on Win64. */
static bool xx_tplink_read_at(xx_io_device *device, int64_t offset, void *data,
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

static bool xx_tplink_add(int64_t left, uint64_t right, int64_t *result) {
    if (!result || left < 0 || right > (uint64_t)(INT64_MAX - left)) {
        return false;
    }
    *result = left + (int64_t)right;
    return true;
}

/* True when [offset, offset + size) lies inside [0, total_size). */
static bool xx_tplink_range_within(int64_t total_size, int64_t offset,
                                   int64_t size) {
    return (total_size >= 0) && (offset >= 0) && (size >= 0) &&
           (offset <= total_size) && (size <= total_size - offset);
}

static void xx_tplink_private_cleanup(xx_tplink_private *parsed) {
    if (!parsed) return;
    if (parsed->vendor_name) xx_str_free(parsed->vendor_name);
    if (parsed->firmware_version) xx_str_free(parsed->firmware_version);
    xx_mem_zero(parsed, sizeof(*parsed));
    parsed->input_size = -1;
    parsed->archive_end = -1;
    parsed->md5_salt_index = -1;
}

static void xx_tplink_push(xx_tplink_private *parsed, const char *name,
                           int64_t offset, int64_t size) {
    xx_tplink_region *region;
    if (!parsed || parsed->count >= XX_TPLINK_MAX_RECORDS || size <= 0) return;
    region = &parsed->regions[parsed->count++];
    region->name = name;
    region->data_offset = offset;
    region->data_size = size;
}

/* Copy a fixed-width, NUL-padded character field out of the header. */
static char *xx_tplink_copy_field(const uint8_t *header, size_t offset,
                                  size_t length) {
    char *result = xx_str_create_len(length);
    size_t index;
    if (!result) return NULL;
    for (index = 0U; index < length; ++index) {
        uint8_t byte = header[offset + index];
        if (byte == 0U) break;
        /* Vendor strings are plain ASCII; anything else is not something to
         * hand on to a caller that may print it. */
        result[index] = (byte >= 0x20U && byte < 0x7FU) ? (char)byte : '?';
    }
    result[index] = '\0';
    return result;
}

/* ------------------------------------------------------------------------ */
/* MD5 over the image with the md5sum1 field replaced by a salt               */
/* ------------------------------------------------------------------------ */

/*
 * mktplinkfw computes the digest over the finished image after writing the
 * salt into the md5sum1 field, then writes the digest back over the salt.  So
 * the verification is: stream the image, substituting the salt for the sixteen
 * bytes at XX_TPLINK_MD5SUM1_OFFSET.  Streaming rather than buffering keeps
 * the memory cost constant no matter how big the declared image is.
 */
static bool xx_tplink_image_md5(xx_io_device *device, int64_t offset,
                                int64_t size, const uint8_t *salt,
                                uint8_t out[XX_TPLINK_MD5_SIZE],
                                xx_pd_struct *pd) {
    uint8_t staging[XX_TPLINK_STAGING_SIZE];
    xx_hash_context ctx;
    int64_t position = 0;
    if (!device || !salt || !out || offset < 0 || size < 0) return false;
    if (!xx_hash_init(&ctx, XX_HASH_MD5)) return false;
    if (size != 0 && xx_io_seek64(device, offset, SEEK_SET) != 0) return false;
    while (position < size) {
        int64_t remaining = size - position;
        size_t step = (remaining < (int64_t)sizeof(staging))
                          ? (size_t)remaining
                          : sizeof(staging);
        size_t done = 0U;
        size_t index;
        if (pd && xx_pd_is_stopped(pd)) return false;
        while (done < step) {
            ssize_t got = xx_io_read(device, staging + done, step - done);
            if (got <= 0 || (size_t)got > step - done) return false;
            done += (size_t)got;
        }
        /* Overwrite whatever part of the md5sum1 field falls in this chunk. */
        for (index = 0U; index < XX_TPLINK_MD5_SIZE; ++index) {
            int64_t absolute = (int64_t)XX_TPLINK_MD5SUM1_OFFSET + (int64_t)index;
            if (absolute >= position && absolute < position + (int64_t)step) {
                staging[(size_t)(absolute - position)] = salt[index];
            }
        }
        xx_hash_update(&ctx, staging, step);
        position += (int64_t)step;
    }
    return xx_hash_final(&ctx, out, XX_TPLINK_MD5_SIZE);
}

/* ------------------------------------------------------------------------ */
/* Linux (mktplinkfw) variant                                                */
/* ------------------------------------------------------------------------ */

/* Decode the binary field block at +0x40 using one byte order and report
 * whether it is self-consistent: the two reserved words that binwalk checks
 * (unk2 at +0x5C, unk3 at +0x70) must be zero, region_code at +0x48 must be a
 * small enumerator (mktplinkfw writes 1 for US builds, so it is NOT required
 * to be zero), and fw_length must cover the header and fit the device. */
static bool xx_tplink_decode_linux(const uint8_t *header, int64_t input_size,
                                   int64_t base_address, bool big_endian,
                                   xx_tplink_private *out) {
    const size_t size = XX_TPLINK_HEADER_SIZE;
    const size_t at = XX_TPLINK_STRUCTURE_OFFSET;
    uint32_t region_code, reserved2, reserved3;
    uint32_t image_size;
    int64_t archive_end;
    region_code = xx_data_get_u32(header, size, at + 0x08U, big_endian);
    reserved2 = xx_data_get_u32(header, size, at + 0x1CU, big_endian);
    reserved3 = xx_data_get_u32(header, size, at + 0x30U, big_endian);
    if (region_code > XX_TPLINK_MAX_REGION_CODE || reserved2 != 0U ||
        reserved3 != 0U) {
        return false;
    }
    image_size = xx_data_get_u32(header, size, at + 0x3CU, big_endian);
    /* fw_length counts the header, so an image cannot be shorter than one,
     * and it has to be physically present.  This is the expansion lever in
     * the format: bound it here, at parse. */
    if (image_size < XX_TPLINK_HEADER_SIZE) return false;
    if (!xx_tplink_add(base_address, image_size, &archive_end) ||
        archive_end > input_size) {
        return false;
    }

    xx_mem_zero(out, sizeof(*out));
    out->input_size = input_size;
    out->md5_salt_index = -1;
    out->variant = XX_TPLINK_VARIANT_LINUX;
    out->header_size = XX_TPLINK_HEADER_SIZE;
    out->header_big_endian = big_endian;
    out->image_size = image_size;
    out->archive_end = archive_end;
    out->hardware_id = xx_data_get_u32(header, size, at + 0x00U, big_endian);
    out->hardware_revision =
        xx_data_get_u32(header, size, at + 0x04U, big_endian);
    out->kernel_load_address =
        xx_data_get_u32(header, size, at + 0x34U, big_endian);
    out->kernel_entry_point =
        xx_data_get_u32(header, size, at + 0x38U, big_endian);
    out->kernel_offset = xx_data_get_u32(header, size, at + 0x40U, big_endian);
    out->kernel_length = xx_data_get_u32(header, size, at + 0x44U, big_endian);
    out->rootfs_offset = xx_data_get_u32(header, size, at + 0x48U, big_endian);
    out->rootfs_length = xx_data_get_u32(header, size, at + 0x4CU, big_endian);
    out->bootloader_offset =
        xx_data_get_u32(header, size, at + 0x50U, big_endian);
    out->bootloader_length =
        xx_data_get_u32(header, size, at + 0x54U, big_endian);
    out->version_major = xx_data_get_u16(header, size, at + 0x58U, big_endian);
    out->version_minor = xx_data_get_u16(header, size, at + 0x5AU, big_endian);
    out->version_patch = xx_data_get_u16(header, size, at + 0x5CU, big_endian);
    xx_rt_memcpy(out->md5sum1, header + XX_TPLINK_MD5SUM1_OFFSET,
                 XX_TPLINK_MD5_SIZE);
    return true;
}

/* True when a declared region [offset, offset + length) lies after the header,
 * inside the declared image AND inside the device.  Both fields come straight
 * out of the file. */
static bool xx_tplink_region_fits(const xx_tplink_private *parsed,
                                  int64_t base_address, uint32_t offset,
                                  uint32_t length, int64_t *absolute) {
    if (length == 0U || offset < XX_TPLINK_HEADER_SIZE ||
        offset > parsed->image_size || length > parsed->image_size - offset) {
        return false;
    }
    return xx_tplink_add(base_address, offset, absolute) &&
           xx_tplink_range_within(parsed->input_size, *absolute,
                                  (int64_t)length);
}

/* Publish the kernel, rootfs and bootloader regions of a decoded header.
 *
 * kernel and rootfs are strict: when one is declared (non-zero length) it must
 * fit, otherwise the decode is rejected - that is what lets a wrong byte-order
 * guess fall through to the other one.  The bootloader span is lenient: it is
 * published when it fits and skipped when it does not.  At least one of
 * kernel/rootfs must be present: an empty table describes no payload, which
 * means the layout was misread.
 *
 * Every offset is taken as a FILE offset from the start of this header.  That
 * is unverified for stock TP-Link "_up_boot" images; see "Unverified: stock
 * _up_boot images" in xx_tplink.h for what goes wrong if they use flash
 * offsets instead (kernel/rootfs silently shifted, bootloader skipped). */
static bool xx_tplink_publish_regions(xx_tplink_private *parsed,
                                      int64_t base_address) {
    int64_t absolute = 0;
    parsed->count = 0U;
    if (parsed->kernel_length == 0U && parsed->rootfs_length == 0U) {
        return false;
    }
    if (parsed->kernel_length != 0U) {
        if (!xx_tplink_region_fits(parsed, base_address, parsed->kernel_offset,
                                   parsed->kernel_length, &absolute)) {
            return false;
        }
        xx_tplink_push(parsed, "kernel.bin", absolute,
                       (int64_t)parsed->kernel_length);
    }
    if (parsed->rootfs_length != 0U) {
        if (!xx_tplink_region_fits(parsed, base_address, parsed->rootfs_offset,
                                   parsed->rootfs_length, &absolute)) {
            return false;
        }
        xx_tplink_push(parsed, "rootfs.bin", absolute,
                       (int64_t)parsed->rootfs_length);
    }
    if (parsed->bootloader_length != 0U &&
        xx_tplink_region_fits(parsed, base_address, parsed->bootloader_offset,
                              parsed->bootloader_length, &absolute)) {
        xx_tplink_push(parsed, "bootloader.bin", absolute,
                       (int64_t)parsed->bootloader_length);
    }
    return parsed->count != 0U;
}

static bool xx_tplink_parse_linux(Abstractformat *self, const uint8_t *header,
                                  int64_t input_size,
                                  xx_tplink_private *parsed, bool want_md5,
                                  xx_pd_struct *pd) {
    xx_tplink_private candidate;
    int attempt;
    int salt;
    bool have = false;

    /* Only read after a successful decode fills it; zeroed so that is
     * obvious to the compiler too (MSVC /W4 C4701). */
    xx_mem_zero(&candidate, sizeof(candidate));

    /*
     * mktplinkfw.c writes every field with htonl()/htons(), i.e. big endian;
     * its version word HEADER_VERSION_V1 = 0x01000000 is what puts the bytes
     * 01 00 00 00 at +0, which binwalk matches as a literal.  Big endian is
     * therefore tried first.  A header whose big-endian decode does not yield
     * a consistent image AND offset table falls back to little endian, the
     * order binwalk's structure parser uses.  The whole decode, including the
     * offset table, is redone per byte order: a fw_length can happen to fit
     * the device in the wrong order (00 3C 00 00 reads as 0x3C00 little
     * endian) while the offsets then cannot.
     */
    for (attempt = 0; attempt < 2 && !have; ++attempt) {
        bool big_endian = (attempt == 0);
        if (xx_tplink_decode_linux(header, input_size, self->base_address,
                                   big_endian, &candidate) &&
            xx_tplink_publish_regions(&candidate, self->base_address)) {
            have = true;
        }
    }
    if (!have) return false;
    *parsed = candidate;

    parsed->vendor_name = xx_tplink_copy_field(header, XX_TPLINK_VENDOR_OFFSET,
                                               XX_TPLINK_VENDOR_FIELD_SIZE);
    parsed->firmware_version = xx_tplink_copy_field(
        header, XX_TPLINK_VENDOR_OFFSET + XX_TPLINK_VENDOR_FIELD_SIZE,
        XX_TPLINK_FWVERSION_FIELD_SIZE);
    if (!parsed->vendor_name || !parsed->firmware_version) return false;

    /* Advisory digest check; see the salt table comment.  Never fatal, and
     * never run from the detection probe. */
    if (want_md5 &&
        (int64_t)parsed->image_size <= (int64_t)XX_TPLINK_MD5_LIMIT) {
        for (salt = 0; salt < XX_TPLINK_SALT_COUNT; ++salt) {
            uint8_t computed[XX_TPLINK_MD5_SIZE];
            if (!xx_tplink_image_md5(self->device, self->base_address,
                                     (int64_t)parsed->image_size,
                                     xx_tplink_md5_salts[salt], computed, pd)) {
                break;
            }
            parsed->md5_checked = true;
            if (xx_hash_equal(computed, parsed->md5sum1,
                              XX_TPLINK_MD5_SIZE)) {
                parsed->md5_valid = true;
                parsed->md5_salt_index = salt;
                break;
            }
        }
    }
    return true;
}

/* ------------------------------------------------------------------------ */
/* RTOS variant                                                              */
/* ------------------------------------------------------------------------ */

static bool xx_tplink_parse_rtos(Abstractformat *self, const uint8_t *header,
                                 int64_t input_size,
                                 xx_tplink_private *parsed) {
    const size_t size = XX_TPLINK_RTOS_HEADER_SIZE;
    uint32_t data_size;
    int64_t payload_offset;
    if (xx_data_get_u32(header, size, 0U, true) != XX_TPLINK_RTOS_MAGIC1 ||
        xx_data_get_u32(header, size, 20U, true) != XX_TPLINK_RTOS_MAGIC2) {
        return false;
    }
    data_size = xx_data_get_u32(header, size, 24U, true);
    if (data_size < XX_TPLINK_RTOS_HEADER_SIZE - XX_TPLINK_RTOS_SIZE_BIAS) {
        return false;
    }

    xx_mem_zero(parsed, sizeof(*parsed));
    parsed->input_size = input_size;
    parsed->md5_salt_index = -1;
    parsed->variant = XX_TPLINK_VARIANT_RTOS;
    parsed->header_size = XX_TPLINK_RTOS_HEADER_SIZE;
    parsed->header_big_endian = true;
    parsed->model_number = xx_data_get_u16(header, size, 28U, true);
    parsed->hardware_rev_major = xx_data_get_u8(header, size, 30U);
    parsed->hardware_rev_minor = xx_data_get_u8(header, size, 31U);

    /* binwalk reports data_size + 20 as the image size without ever checking
     * it against the file.  Bound it here instead. */
    if (data_size > UINT32_MAX - XX_TPLINK_RTOS_SIZE_BIAS) return false;
    parsed->image_size = data_size + XX_TPLINK_RTOS_SIZE_BIAS;
    if (!xx_tplink_add(self->base_address, parsed->image_size,
                       &parsed->archive_end) ||
        parsed->archive_end > input_size) {
        return false;
    }
    if (!xx_tplink_add(self->base_address, XX_TPLINK_RTOS_HEADER_SIZE,
                       &payload_offset)) {
        return false;
    }
    xx_tplink_push(parsed, "payload.bin", payload_offset,
                   parsed->archive_end - payload_offset);
    return parsed->count != 0U;
}

/* ------------------------------------------------------------------------ */
/* Parse                                                                     */
/* ------------------------------------------------------------------------ */

/* want_md5 runs the advisory image digest; only handle_base_info asks for it,
 * so the detection probe never streams the whole image. */
static bool xx_tplink_parse(Abstractformat *self, xx_tplink_private *parsed,
                            bool want_md5, xx_pd_struct *pd) {
    uint8_t header[XX_TPLINK_HEADER_SIZE];
    int64_t input_size;
    if (parsed) {
        xx_mem_zero(parsed, sizeof(*parsed));
        parsed->input_size = -1;
        parsed->archive_end = -1;
        parsed->md5_salt_index = -1;
    }
    if (!self || !self->device || !parsed || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    input_size = xx_io_total_size(self->device);

    /* The Linux variant is tried first: its header is longer, so a file that
     * is too short for it can still be an RTOS image. */
    if (xx_tplink_range_within(input_size, self->base_address,
                               XX_TPLINK_HEADER_SIZE) &&
        xx_tplink_read_at(self->device, self->base_address, header,
                          XX_TPLINK_HEADER_SIZE) &&
        /* The vendor string is the strongest fixed evidence in the header.
         * The fw_version field that binwalk folds into its magic is NOT fixed
         * - "ver. 1.0" is only the common case - so only the vendor name is
         * required here. */
        xx_rt_memcmp(header + XX_TPLINK_VENDOR_OFFSET, XX_TPLINK_VENDOR_STRING,
                     XX_TPLINK_VENDOR_LENGTH) == 0) {
        if (xx_tplink_parse_linux(self, header, input_size, parsed, want_md5,
                                  pd)) {
            return true;
        }
        xx_tplink_private_cleanup(parsed);
    }

    if (xx_tplink_range_within(input_size, self->base_address,
                               XX_TPLINK_RTOS_HEADER_SIZE) &&
        xx_tplink_read_at(self->device, self->base_address, header,
                          XX_TPLINK_RTOS_HEADER_SIZE) &&
        xx_tplink_parse_rtos(self, header, input_size, parsed)) {
        return true;
    }
    xx_tplink_private_cleanup(parsed);
    return false;
}

/* ------------------------------------------------------------------------ */
/* Record plumbing                                                           */
/* ------------------------------------------------------------------------ */

static bool xx_tplink_copy_options(xx_list_s *destination,
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

static const xx_var *xx_tplink_find_option(const xx_list_s *options,
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

static bool xx_tplink_populate_record(xx_archive_record *record,
                                      const xx_tplink_private *parsed,
                                      const xx_tplink_region *region) {
    if (!record || !parsed || !region || !region->name) return false;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = parsed->archive_end - (int64_t)parsed->image_size;
    record->header_size = (int64_t)parsed->header_size;
    record->data_offset = region->data_offset;
    record->compressed_size = region->data_size;
    /* The regions are stored verbatim - the kernel is usually LZMA and the
     * rootfs usually squashfs, but that is the payload's own business - so the
     * two sizes agree and the compression method is "none". */
    return xx_archive_record_set_original_name(record, region->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)region->data_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)region->data_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          0U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

static void xx_tplink_archive_stream_free(void *pointer) {
    xx_tplink_archive_stream *stream = (xx_tplink_archive_stream *)pointer;
    if (!stream) return;
    xx_tplink_private_cleanup(&stream->parsed);
    xx_mem_free(stream);
}

/* ------------------------------------------------------------------------ */
/* Public interface                                                          */
/* ------------------------------------------------------------------------ */

void xx_tplink_init(xx_tplink *tplink, xx_io_device *dev,
                    int64_t base_address) {
    if (!tplink) return;
    xx_mem_zero(tplink, sizeof(*tplink));
    xx_format_init(&tplink->format, dev, base_address);
    /* The Linux header's byte order is decided per file; this is only the
     * default reported before a successful parse. */
    tplink->format.endian = XX_ENDIAN_LITTLE;
    tplink->format.file_type = XX_TPLINK_FILE_TYPE;
    tplink->format.format_type = XX_TYPE_ARCHIVE;
    tplink->format.is_archive = true;
    xx_format_set_mime_type(&tplink->format, "application/x-tplink-firmware");
    xx_format_set_extension(&tplink->format, "bin");
    tplink->format.check_is_valid = xx_tplink_check_is_valid;
    tplink->format.handle_base_info = xx_tplink_handle_base_info;
    tplink->format.get_format_size = xx_tplink_get_format_size;
    tplink->format.get_number_of_archive_records =
        xx_tplink_get_number_of_archive_records;
    tplink->format.create_archive_records_reading =
        xx_tplink_create_archive_records_reading;
    tplink->format.get_current_archive_record =
        xx_tplink_get_current_archive_record;
    tplink->format.unpack_current_archive_record =
        xx_tplink_unpack_current_archive_record;
    tplink->format.archive_record_move_to_next =
        xx_tplink_archive_record_move_to_next;
    tplink->format.free_archive_records_reading =
        xx_tplink_free_archive_records_reading;
    tplink->format.destroy = xx_tplink_vtable_destroy;
    tplink->variant = XX_TPLINK_VARIANT_NONE;
    tplink->md5_salt_index = -1;
    tplink->archive_end = -1;
}

xx_tplink *xx_tplink_create(xx_io_device *dev, int64_t base_address) {
    xx_tplink *tplink = (xx_tplink *)xx_mem_alloc(sizeof(*tplink));
    if (tplink) xx_tplink_init(tplink, dev, base_address);
    return tplink;
}

void xx_tplink_destroy(xx_tplink *tplink) {
    if (!tplink) return;
    if (tplink->internal) {
        xx_tplink_private_cleanup((xx_tplink_private *)tplink->internal);
        xx_mem_free(tplink->internal);
        tplink->internal = NULL;
    }
    xx_format_cleanup_extra_parameters(&tplink->format);
}

static void xx_tplink_vtable_destroy(Abstractformat *self) {
    xx_tplink_destroy((xx_tplink *)self);
}

void xx_tplink_free(xx_tplink *tplink) {
    if (!tplink) return;
    xx_tplink_destroy(tplink);
    xx_mem_free(tplink);
}

bool xx_tplink_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_tplink_private parsed;
    bool result = xx_tplink_parse(self, &parsed, false, pd);
    xx_tplink_private_cleanup(&parsed);
    return result;
}

bool xx_tplink_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_tplink_private *parsed;
    xx_tplink *tplink = (xx_tplink *)self;
    int64_t total_size;
    if (!self || !tplink) return false;
    parsed = (xx_tplink_private *)xx_mem_alloc(sizeof(*parsed));
    if (!parsed || !xx_tplink_parse(self, parsed, true, pd)) {
        if (parsed) xx_mem_free(parsed);
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    if (tplink->internal) {
        xx_tplink_private_cleanup((xx_tplink_private *)tplink->internal);
        xx_mem_free(tplink->internal);
    }
    tplink->internal = parsed;
    tplink->variant = parsed->variant;
    tplink->number_of_records = parsed->count;
    tplink->header_size = parsed->header_size;
    tplink->image_size = parsed->image_size;
    tplink->hardware_id = parsed->hardware_id;
    tplink->hardware_revision = parsed->hardware_revision;
    tplink->kernel_load_address = parsed->kernel_load_address;
    tplink->kernel_entry_point = parsed->kernel_entry_point;
    tplink->kernel_offset = parsed->kernel_offset;
    tplink->kernel_length = parsed->kernel_length;
    tplink->rootfs_offset = parsed->rootfs_offset;
    tplink->rootfs_length = parsed->rootfs_length;
    tplink->bootloader_offset = parsed->bootloader_offset;
    tplink->bootloader_length = parsed->bootloader_length;
    tplink->version_major = parsed->version_major;
    tplink->version_minor = parsed->version_minor;
    tplink->version_patch = parsed->version_patch;
    tplink->model_number = parsed->model_number;
    tplink->hardware_rev_major = parsed->hardware_rev_major;
    tplink->hardware_rev_minor = parsed->hardware_rev_minor;
    xx_rt_memcpy(tplink->md5sum1, parsed->md5sum1, XX_TPLINK_MD5_SIZE);
    tplink->md5_checked = parsed->md5_checked;
    tplink->md5_valid = parsed->md5_valid;
    tplink->md5_salt_index = parsed->md5_salt_index;
    tplink->header_big_endian = parsed->header_big_endian;
    tplink->archive_end = parsed->archive_end;
    tplink->format.endian =
        parsed->header_big_endian ? XX_ENDIAN_BIG : XX_ENDIAN_LITTLE;
    if (parsed->firmware_version) {
        xx_format_set_version(&tplink->format, parsed->firmware_version);
    }
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

int64_t xx_tplink_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return -1;
    }
    return self->format_size;
}

uint64_t xx_tplink_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return ((xx_tplink *)self)->number_of_records;
}

xx_archive_record_state *xx_tplink_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_record_state *state;
    xx_tplink_archive_stream *stream;
    if (!self || !self->device ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    stream = (xx_tplink_archive_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!state || !stream) {
        if (state) xx_mem_free(state);
        if (stream) xx_mem_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    if (!xx_tplink_copy_options(&state->options, options) ||
        !xx_tplink_parse(self, &stream->parsed, false, pd)) {
        xx_tplink_archive_stream_free(stream);
        xx_archive_record_state_free(state);
        return NULL;
    }
    stream->index = 0U;
    state->internal_state = stream;
    state->free_internal = xx_tplink_archive_stream_free;
    state->total_records = (int64_t)stream->parsed.count;
    if (stream->parsed.count != 0U &&
        xx_tplink_populate_record(&state->current_record, &stream->parsed,
                                  &stream->parsed.regions[0])) {
        state->has_record = true;
        state->current_index = 0;
    }
    return state;
}

const xx_archive_record *xx_tplink_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_tplink_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_tplink_archive_stream *stream;
    if (!self || !state || state->format != self || !state->has_record ||
        !state->internal_state || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_tplink_archive_stream *)state->internal_state;
    ++stream->index;
    if (stream->index >= stream->parsed.count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    if (!xx_tplink_populate_record(&state->current_record, &stream->parsed,
                                   &stream->parsed.regions[stream->index])) {
        state->has_record = false;
        return false;
    }
    ++state->current_index;
    return true;
}

bool xx_tplink_unpack_current_archive_record(Abstractformat *self,
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
    option = xx_tplink_find_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!option) {
        /* No destination: report whether the region's span is addressable. */
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

void xx_tplink_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

uint64_t xx_tplink_get_number_of_records(const xx_tplink *tplink) {
    return tplink ? tplink->number_of_records : 0U;
}
xx_tplink_variant_t xx_tplink_get_variant(const xx_tplink *tplink) {
    return tplink ? tplink->variant : XX_TPLINK_VARIANT_NONE;
}
uint32_t xx_tplink_get_image_size(const xx_tplink *tplink) {
    return tplink ? tplink->image_size : 0U;
}
uint32_t xx_tplink_get_hardware_id(const xx_tplink *tplink) {
    return tplink ? tplink->hardware_id : 0U;
}
const char *xx_tplink_get_firmware_version(const xx_tplink *tplink) {
    const xx_tplink_private *parsed =
        tplink ? (const xx_tplink_private *)tplink->internal : NULL;
    return parsed ? parsed->firmware_version : NULL;
}
const char *xx_tplink_get_vendor_name(const xx_tplink *tplink) {
    const xx_tplink_private *parsed =
        tplink ? (const xx_tplink_private *)tplink->internal : NULL;
    return parsed ? parsed->vendor_name : NULL;
}
bool xx_tplink_get_md5_valid(const xx_tplink *tplink) {
    return tplink ? (tplink->md5_checked && tplink->md5_valid) : false;
}
int xx_tplink_get_md5_salt_index(const xx_tplink *tplink) {
    return tplink ? tplink->md5_salt_index : -1;
}
bool xx_tplink_get_header_big_endian(const xx_tplink *tplink) {
    return tplink ? tplink->header_big_endian : false;
}
