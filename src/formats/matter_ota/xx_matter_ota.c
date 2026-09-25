/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/matter_ota/xx_matter_ota.h"

#include "xxfclib/algo/hash/xx_hash.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the file-type constant resolves to UNKNOWN until the enumerator
 * lands.  Delete this block once XX_FILE_TYPE_MATTER_OTA exists in the enum. */
#ifdef MATTER_OTA
#define XX_MATTER_OTA_FILE_TYPE XX_FILE_TYPE_MATTER_OTA
#else
#define XX_MATTER_OTA_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

/** Deepest TLV container nesting accepted before the walk gives up. */
#define XX_MATTER_OTA_MAX_DEPTH 16U
/** Upper bound on TLV elements in one header; real headers hold ten. */
#define XX_MATTER_OTA_MAX_ELEMENTS 4096U

typedef struct xx_matter_ota_private_s {
    int64_t input_size;
    int64_t archive_end;
    int64_t payload_offset;
    uint64_t total_size;
    uint64_t payload_size;
    uint64_t vendor_id;
    uint64_t product_id;
    uint64_t software_version;
    uint64_t min_applicable_version;
    uint64_t max_applicable_version;
    uint64_t image_digest_type;
    uint32_t header_size;
    uint32_t image_digest_size;
    uint8_t image_digest[64];
    char *version_string;
    char *release_notes_url;
    bool digest_checked;
    bool digest_valid;
} xx_matter_ota_private;

typedef struct xx_matter_ota_archive_stream_s {
    xx_matter_ota_private parsed;
    size_t index;
} xx_matter_ota_archive_stream;

static void xx_matter_ota_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------------------ */
/* Device helpers                                                            */
/* ------------------------------------------------------------------------ */

/* All positioning goes through seek64: total_size is a 64-bit field and long
 * is 32-bit on Win64. */
static bool xx_matter_ota_read_at(xx_io_device *device, int64_t offset,
                                  void *data, size_t size) {
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

static bool xx_matter_ota_add(int64_t left, uint64_t right, int64_t *result) {
    if (!result || left < 0 || right > (uint64_t)(INT64_MAX - left)) {
        return false;
    }
    *result = left + (int64_t)right;
    return true;
}

/* True when [offset, offset + size) lies inside [0, total_size). */
static bool xx_matter_ota_range_within(int64_t total_size, int64_t offset,
                                       int64_t size) {
    return (total_size >= 0) && (offset >= 0) && (size >= 0) &&
           (offset <= total_size) && (size <= total_size - offset);
}

static void xx_matter_ota_private_cleanup(xx_matter_ota_private *parsed) {
    if (!parsed) return;
    if (parsed->version_string) xx_str_free(parsed->version_string);
    if (parsed->release_notes_url) xx_str_free(parsed->release_notes_url);
    xx_mem_zero(parsed, sizeof(*parsed));
    parsed->input_size = -1;
    parsed->archive_end = -1;
    parsed->payload_offset = -1;
}

/* ------------------------------------------------------------------------ */
/* Matter TLV                                                                */
/* ------------------------------------------------------------------------ */

/* Context-specific tag numbers defined for the OTA header by the Matter
 * specification.  Anything outside this range is walked over and ignored so a
 * header carrying a future field still parses. */
enum {
    XX_MATTER_OTA_TAG_VENDOR_ID = 0,
    XX_MATTER_OTA_TAG_PRODUCT_ID = 1,
    XX_MATTER_OTA_TAG_SOFTWARE_VERSION = 2,
    XX_MATTER_OTA_TAG_VERSION_STRING = 3,
    XX_MATTER_OTA_TAG_PAYLOAD_SIZE = 4,
    XX_MATTER_OTA_TAG_MIN_VERSION = 5,
    XX_MATTER_OTA_TAG_MAX_VERSION = 6,
    XX_MATTER_OTA_TAG_RELEASE_NOTES = 7,
    XX_MATTER_OTA_TAG_DIGEST_TYPE = 8,
    XX_MATTER_OTA_TAG_DIGEST = 9
};

/* One decoded TLV element.  Values larger than the cases this reader cares
 * about are located but not copied. */
typedef struct xx_matter_ota_tlv_s {
    uint32_t element_type; /**< Control octet, low 5 bits. */
    bool has_tag;
    uint32_t tag;
    uint64_t unsigned_value;
    size_t value_offset; /**< Into the header buffer, for string/octet types. */
    size_t value_length;
} xx_matter_ota_tlv;

/* Assemble a 1/2/4/8 byte little endian value.  xx_data_get_u64() cannot be
 * used for the narrow forms: it returns 0 outright when fewer than eight bytes
 * are available rather than widening what is there. */
static uint64_t xx_matter_ota_le(const uint8_t *data, uint8_t width) {
    uint64_t value = 0U;
    uint8_t index;
    for (index = 0U; index < width; ++index) {
        value |= (uint64_t)data[index] << (8U * index);
    }
    return value;
}

/*
 * Decode one TLV element starting at *cursor, advance *cursor past it.
 *
 * The control octet's low two bits select the width of an integer value or of
 * a string's length field; the top three bits are the tag control.  Only
 * anonymous (0) and one-byte context-specific (1) tags occur in an OTA header,
 * and the wider tag forms would change the element size, so anything else is a
 * parse failure rather than something to skip.
 */
static bool xx_matter_ota_tlv_next(const uint8_t *data, size_t size,
                                   size_t *cursor, xx_matter_ota_tlv *out) {
    static const uint8_t widths[4] = {1U, 2U, 4U, 8U};
    uint8_t control;
    uint32_t element_type;
    uint32_t tag_control;
    uint8_t width;
    size_t at;
    if (!data || !cursor || !out || *cursor >= size) return false;
    at = *cursor;
    control = data[at++];
    element_type = (uint32_t)(control & 0x1FU);
    tag_control = (uint32_t)(control >> 5U);
    width = widths[control & 0x03U];

    xx_mem_zero(out, sizeof(*out));
    out->element_type = element_type;
    if (tag_control == 0U) {
        out->has_tag = false;
    } else if (tag_control == 1U) {
        if (at >= size) return false;
        out->has_tag = true;
        out->tag = data[at++];
    } else {
        return false;
    }

    switch (element_type) {
        case 0x00U: /* signed int, 1/2/4/8 bytes */
        case 0x01U:
        case 0x02U:
        case 0x03U:
        case 0x04U: /* unsigned int, 1/2/4/8 bytes */
        case 0x05U:
        case 0x06U:
        case 0x07U:
            if (width > size - at) return false;
            /* Signed forms share the decode: the header's signed fields are
             * not among the ten this reader models, so no sign extension is
             * needed and the raw bits are kept. */
            out->unsigned_value = xx_matter_ota_le(data + at, width);
            at += width;
            break;
        case 0x08U: /* boolean false */
        case 0x09U: /* boolean true */
            out->unsigned_value = (element_type == 0x09U) ? 1U : 0U;
            break;
        case 0x0AU: /* float32 */
            if (4U > size - at) return false;
            at += 4U;
            break;
        case 0x0BU: /* float64 */
            if (8U > size - at) return false;
            at += 8U;
            break;
        case 0x0CU: /* UTF-8 string, length field 1/2/4/8 bytes */
        case 0x0DU:
        case 0x0EU:
        case 0x0FU:
        case 0x10U: /* octet string, length field 1/2/4/8 bytes */
        case 0x11U:
        case 0x12U:
        case 0x13U: {
            uint64_t length;
            if (width > size - at) return false;
            length = xx_matter_ota_le(data + at, width);
            at += width;
            /* The declared length is attacker controlled: it has to fit in
             * what is left of the header buffer before it is trusted. */
            if (length > (uint64_t)(size - at)) return false;
            out->value_offset = at;
            out->value_length = (size_t)length;
            at += (size_t)length;
            break;
        }
        case 0x14U: /* null */
        case 0x15U: /* structure */
        case 0x16U: /* array */
        case 0x17U: /* list */
        case 0x18U: /* end of container */
            break;
        default:
            return false;
    }
    *cursor = at;
    return true;
}

/* Copy a TLV string value out of the header buffer and NUL-terminate it.  The
 * encoding carries an explicit length and no terminator, and the bytes are not
 * trusted to be free of embedded NULs, so the copy stops at the first one. */
static char *xx_matter_ota_copy_string(const uint8_t *data, size_t offset,
                                       size_t length) {
    char *result;
    size_t index;
    if (length > XX_MATTER_OTA_MAX_STRING) length = XX_MATTER_OTA_MAX_STRING;
    result = xx_str_create_len(length);
    if (!result) return NULL;
    for (index = 0U; index < length; ++index) {
        uint8_t byte = data[offset + index];
        if (byte == 0U) break;
        result[index] = (char)byte;
    }
    result[index] = '\0';
    return result;
}

static bool xx_matter_ota_is_unsigned(const xx_matter_ota_tlv *element) {
    return element->element_type >= 0x04U && element->element_type <= 0x07U;
}

static bool xx_matter_ota_is_utf8(const xx_matter_ota_tlv *element) {
    return element->element_type >= 0x0CU && element->element_type <= 0x0FU;
}

static bool xx_matter_ota_is_octets(const xx_matter_ota_tlv *element) {
    return element->element_type >= 0x10U && element->element_type <= 0x13U;
}

/*
 * The header is ONE anonymous structure (control octet 0x15) and the fields
 * are its direct members.  The walk mirrors connectedhomeip's
 * OTAImageHeaderParser::DecodeTlv:
 *
 *  - the first element must be that anonymous structure, otherwise the bytes
 *    are not an OTA header at all;
 *  - only members at depth 1 are fields.  A nested container belonging to a
 *    future field may carry its own context tag 4, and that must not be
 *    mistaken for PayloadSize;
 *  - the walk stops at the structure's own end-of-container.  HeaderSize
 *    bytes left over after it are ignored, as the reference parser does.
 */
static bool xx_matter_ota_parse_tlv(const uint8_t *data, size_t size,
                                    xx_matter_ota_private *parsed,
                                    bool *saw_payload_size) {
    size_t cursor = 0U;
    size_t elements = 0U;
    unsigned depth = 0U;
    xx_matter_ota_tlv element;
    if (!data || !parsed || !saw_payload_size) return false;
    *saw_payload_size = false;
    if (!xx_matter_ota_tlv_next(data, size, &cursor, &element) ||
        element.element_type != 0x15U || element.has_tag) {
        return false;
    }
    depth = 1U;
    while (depth != 0U) {
        unsigned member_depth = depth;
        /* Running out of header before the structure closes is truncation. */
        if (cursor >= size) return false;
        if (++elements > XX_MATTER_OTA_MAX_ELEMENTS) return false;
        if (!xx_matter_ota_tlv_next(data, size, &cursor, &element)) return false;
        if (element.element_type == 0x15U || element.element_type == 0x16U ||
            element.element_type == 0x17U) {
            if (++depth > XX_MATTER_OTA_MAX_DEPTH) return false;
            continue;
        }
        if (element.element_type == 0x18U) {
            --depth;
            continue;
        }
        if (member_depth != 1U || !element.has_tag) continue;
        switch (element.tag) {
            case XX_MATTER_OTA_TAG_VENDOR_ID:
                if (xx_matter_ota_is_unsigned(&element)) {
                    parsed->vendor_id = element.unsigned_value;
                }
                break;
            case XX_MATTER_OTA_TAG_PRODUCT_ID:
                if (xx_matter_ota_is_unsigned(&element)) {
                    parsed->product_id = element.unsigned_value;
                }
                break;
            case XX_MATTER_OTA_TAG_SOFTWARE_VERSION:
                if (xx_matter_ota_is_unsigned(&element)) {
                    parsed->software_version = element.unsigned_value;
                }
                break;
            case XX_MATTER_OTA_TAG_VERSION_STRING:
                if (xx_matter_ota_is_utf8(&element) &&
                    element.value_length != 0U && !parsed->version_string) {
                    parsed->version_string = xx_matter_ota_copy_string(
                        data, element.value_offset, element.value_length);
                    if (!parsed->version_string) return false;
                }
                break;
            case XX_MATTER_OTA_TAG_PAYLOAD_SIZE:
                /* The consistency check below rests on this value, so a
                 * PayloadSize that is not an unsigned integer is refused
                 * rather than read as zero. */
                if (!xx_matter_ota_is_unsigned(&element)) return false;
                parsed->payload_size = element.unsigned_value;
                *saw_payload_size = true;
                break;
            case XX_MATTER_OTA_TAG_MIN_VERSION:
                if (xx_matter_ota_is_unsigned(&element)) {
                    parsed->min_applicable_version = element.unsigned_value;
                }
                break;
            case XX_MATTER_OTA_TAG_MAX_VERSION:
                if (xx_matter_ota_is_unsigned(&element)) {
                    parsed->max_applicable_version = element.unsigned_value;
                }
                break;
            case XX_MATTER_OTA_TAG_RELEASE_NOTES:
                if (xx_matter_ota_is_utf8(&element) &&
                    element.value_length != 0U && !parsed->release_notes_url) {
                    parsed->release_notes_url = xx_matter_ota_copy_string(
                        data, element.value_offset, element.value_length);
                    if (!parsed->release_notes_url) return false;
                }
                break;
            case XX_MATTER_OTA_TAG_DIGEST_TYPE:
                if (xx_matter_ota_is_unsigned(&element)) {
                    parsed->image_digest_type = element.unsigned_value;
                }
                break;
            case XX_MATTER_OTA_TAG_DIGEST: {
                size_t keep = element.value_length;
                if (!xx_matter_ota_is_octets(&element)) break;
                if (keep > sizeof(parsed->image_digest)) {
                    keep = sizeof(parsed->image_digest);
                }
                if (keep != 0U) {
                    xx_rt_memcpy(parsed->image_digest,
                                 data + element.value_offset, keep);
                }
                parsed->image_digest_size = (uint32_t)keep;
                break;
            }
            default:
                break; /* A field this reader does not model. */
        }
    }
    return true;
}

/* ------------------------------------------------------------------------ */
/* Parse                                                                     */
/* ------------------------------------------------------------------------ */

/* compute_digest: recompute the payload's sha-256 when the header carries
 * one.  Only handle_base_info asks for it: the detector's probe and the record
 * walk need the structure, not a hash of a payload that may be megabytes. */
static bool xx_matter_ota_parse(Abstractformat *self,
                                xx_matter_ota_private *parsed,
                                bool compute_digest, xx_pd_struct *pd) {
    uint8_t preamble[XX_MATTER_OTA_PREAMBLE_SIZE];
    uint8_t *header_data = NULL;
    bool saw_payload_size = false;
    uint64_t consumed;
    if (parsed) {
        xx_mem_zero(parsed, sizeof(*parsed));
        parsed->input_size = -1;
        parsed->archive_end = -1;
        parsed->payload_offset = -1;
    }
    if (!self || !self->device || !parsed || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    parsed->input_size = xx_io_total_size(self->device);
    if (!xx_matter_ota_range_within(parsed->input_size, self->base_address,
                                    XX_MATTER_OTA_PREAMBLE_SIZE) ||
        !xx_matter_ota_read_at(self->device, self->base_address, preamble,
                               XX_MATTER_OTA_PREAMBLE_SIZE) ||
        xx_data_get_u32(preamble, sizeof(preamble), 0U, false) !=
            XX_MATTER_OTA_MAGIC) {
        goto fail;
    }
    parsed->total_size = xx_data_get_u64(preamble, sizeof(preamble), 4U, false);
    parsed->header_size =
        xx_data_get_u32(preamble, sizeof(preamble), 12U, false);

    /* header_size is buffered in full, so it is capped on its own terms as
     * well as against the device: a 4 GiB "header" is an allocation bomb, not
     * a firmware image. */
    if (parsed->header_size == 0U ||
        parsed->header_size > XX_MATTER_OTA_MAX_HEADER_SIZE) {
        goto fail;
    }
    /* The whole image must be physically present before anything is read. */
    if (!xx_matter_ota_add(self->base_address, parsed->total_size,
                           &parsed->archive_end) ||
        parsed->archive_end > parsed->input_size) {
        goto fail;
    }
    consumed = (uint64_t)XX_MATTER_OTA_PREAMBLE_SIZE + parsed->header_size;
    if (consumed > parsed->total_size) goto fail;

    header_data = (uint8_t *)xx_mem_alloc(parsed->header_size);
    if (!header_data ||
        !xx_matter_ota_read_at(self->device,
                               self->base_address +
                                   (int64_t)XX_MATTER_OTA_PREAMBLE_SIZE,
                               header_data, parsed->header_size) ||
        !xx_matter_ota_parse_tlv(header_data, parsed->header_size, parsed,
                                 &saw_payload_size)) {
        goto fail;
    }
    xx_mem_free(header_data);
    header_data = NULL;

    /*
     * The specification's own consistency rule, and the only thing here that
     * separates a real OTA file from four matching magic bytes: the payload
     * declared inside the TLV header has to account for exactly the part of
     * total_size that the preamble and header do not.  A header with no
     * PayloadSize at all cannot be checked and is refused.
     */
    if (!saw_payload_size ||
        parsed->payload_size != parsed->total_size - consumed) {
        goto fail;
    }
    if (!xx_matter_ota_add(self->base_address, consumed,
                           &parsed->payload_offset) ||
        !xx_matter_ota_range_within(parsed->input_size, parsed->payload_offset,
                                    (int64_t)parsed->payload_size)) {
        goto fail;
    }

    /*
     * ImageDigest covers the payload only.  A mismatch is recorded and not
     * treated as a parse failure: the header is still well formed, and a
     * caller inspecting a corrupted or deliberately re-signed image is better
     * served by getting the payload plus a "digest did not match" flag than by
     * getting nothing.  Only sha-256 is recomputed; the other entries in the
     * IANA registry (sha-256-128 and friends) are truncations and full-length
     * SHA-384/512, which this library does not all provide.
     */
    if (compute_digest &&
        parsed->image_digest_type == XX_MATTER_OTA_DIGEST_SHA256 &&
        parsed->image_digest_size == XX_SHA256_DIGEST_SIZE) {
        uint8_t computed[XX_SHA256_DIGEST_SIZE];
        if (xx_hash_device(XX_HASH_SHA256, self->device, parsed->payload_offset,
                           (int64_t)parsed->payload_size, computed,
                           sizeof(computed), pd)) {
            parsed->digest_checked = true;
            parsed->digest_valid = xx_hash_equal(computed, parsed->image_digest,
                                                 XX_SHA256_DIGEST_SIZE);
        }
    }
    return true;
fail:
    if (header_data) xx_mem_free(header_data);
    xx_matter_ota_private_cleanup(parsed);
    return false;
}

/* ------------------------------------------------------------------------ */
/* Record plumbing                                                           */
/* ------------------------------------------------------------------------ */

static bool xx_matter_ota_copy_options(xx_list_s *destination,
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

static const xx_var *xx_matter_ota_find_option(const xx_list_s *options,
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

/* The single record is the OTA payload.  The name is a constant chosen here,
 * never taken from the file, so it needs no sanitising before it is used as a
 * destination path component. */
static bool xx_matter_ota_populate_record(xx_archive_record *record,
                                          const xx_matter_ota_private *parsed) {
    if (!record || !parsed || parsed->payload_offset < 0) return false;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = parsed->archive_end - (int64_t)parsed->total_size;
    record->header_size =
        (int64_t)XX_MATTER_OTA_PREAMBLE_SIZE + (int64_t)parsed->header_size;
    record->data_offset = parsed->payload_offset;
    record->compressed_size = (int64_t)parsed->payload_size;
    /* The payload is stored verbatim, so the two sizes agree and the
     * compression method is "none". */
    return xx_archive_record_set_original_name(record, "payload.bin") &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          parsed->payload_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          parsed->payload_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          0U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

static void xx_matter_ota_archive_stream_free(void *pointer) {
    xx_matter_ota_archive_stream *stream =
        (xx_matter_ota_archive_stream *)pointer;
    if (!stream) return;
    xx_matter_ota_private_cleanup(&stream->parsed);
    xx_mem_free(stream);
}

/* ------------------------------------------------------------------------ */
/* Public interface                                                          */
/* ------------------------------------------------------------------------ */

void xx_matter_ota_init(xx_matter_ota *ota, xx_io_device *dev,
                        int64_t base_address) {
    if (!ota) return;
    xx_mem_zero(ota, sizeof(*ota));
    xx_format_init(&ota->format, dev, base_address);
    ota->format.endian = XX_ENDIAN_LITTLE;
    ota->format.file_type = XX_MATTER_OTA_FILE_TYPE;
    ota->format.format_type = XX_TYPE_ARCHIVE;
    ota->format.is_archive = true;
    xx_format_set_mime_type(&ota->format, "application/x-matter-ota");
    xx_format_set_extension(&ota->format, "ota");
    ota->format.check_is_valid = xx_matter_ota_check_is_valid;
    ota->format.handle_base_info = xx_matter_ota_handle_base_info;
    ota->format.get_format_size = xx_matter_ota_get_format_size;
    ota->format.get_number_of_archive_records =
        xx_matter_ota_get_number_of_archive_records;
    ota->format.create_archive_records_reading =
        xx_matter_ota_create_archive_records_reading;
    ota->format.get_current_archive_record =
        xx_matter_ota_get_current_archive_record;
    ota->format.unpack_current_archive_record =
        xx_matter_ota_unpack_current_archive_record;
    ota->format.archive_record_move_to_next =
        xx_matter_ota_archive_record_move_to_next;
    ota->format.free_archive_records_reading =
        xx_matter_ota_free_archive_records_reading;
    ota->format.destroy = xx_matter_ota_vtable_destroy;
    ota->payload_offset = -1;
    ota->archive_end = -1;
}

xx_matter_ota *xx_matter_ota_create(xx_io_device *dev, int64_t base_address) {
    xx_matter_ota *ota = (xx_matter_ota *)xx_mem_alloc(sizeof(*ota));
    if (ota) xx_matter_ota_init(ota, dev, base_address);
    return ota;
}

void xx_matter_ota_destroy(xx_matter_ota *ota) {
    if (!ota) return;
    if (ota->internal) {
        xx_matter_ota_private_cleanup((xx_matter_ota_private *)ota->internal);
        xx_mem_free(ota->internal);
        ota->internal = NULL;
    }
    xx_format_cleanup_extra_parameters(&ota->format);
}

static void xx_matter_ota_vtable_destroy(Abstractformat *self) {
    xx_matter_ota_destroy((xx_matter_ota *)self);
}

void xx_matter_ota_free(xx_matter_ota *ota) {
    if (!ota) return;
    xx_matter_ota_destroy(ota);
    xx_mem_free(ota);
}

bool xx_matter_ota_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_matter_ota_private parsed;
    bool result = xx_matter_ota_parse(self, &parsed, false, pd);
    xx_matter_ota_private_cleanup(&parsed);
    return result;
}

bool xx_matter_ota_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_matter_ota_private *parsed;
    xx_matter_ota *ota = (xx_matter_ota *)self;
    int64_t total_size;
    if (!self || !ota) return false;
    parsed = (xx_matter_ota_private *)xx_mem_alloc(sizeof(*parsed));
    if (!parsed || !xx_matter_ota_parse(self, parsed, true, pd)) {
        if (parsed) xx_mem_free(parsed);
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    if (ota->internal) {
        xx_matter_ota_private_cleanup((xx_matter_ota_private *)ota->internal);
        xx_mem_free(ota->internal);
    }
    ota->internal = parsed;
    ota->number_of_records = 1U;
    ota->total_size = parsed->total_size;
    ota->header_size = parsed->header_size;
    ota->payload_size = parsed->payload_size;
    ota->vendor_id = parsed->vendor_id;
    ota->product_id = parsed->product_id;
    ota->software_version = parsed->software_version;
    ota->min_applicable_version = parsed->min_applicable_version;
    ota->max_applicable_version = parsed->max_applicable_version;
    ota->image_digest_type = parsed->image_digest_type;
    ota->image_digest_size = parsed->image_digest_size;
    xx_rt_memcpy(ota->image_digest, parsed->image_digest,
                 sizeof(ota->image_digest));
    ota->digest_checked = parsed->digest_checked;
    ota->digest_valid = parsed->digest_valid;
    ota->payload_offset = parsed->payload_offset;
    ota->archive_end = parsed->archive_end;
    if (parsed->version_string) {
        xx_format_set_version(&ota->format, parsed->version_string);
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
    self->number_of_archive_records = 1U;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_matter_ota_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return -1;
    }
    return self->format_size;
}

uint64_t xx_matter_ota_get_number_of_archive_records(Abstractformat *self,
                                                     xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return ((xx_matter_ota *)self)->number_of_records;
}

xx_archive_record_state *xx_matter_ota_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_record_state *state;
    xx_matter_ota_archive_stream *stream;
    if (!self || !self->device ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    stream =
        (xx_matter_ota_archive_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!state || !stream) {
        if (state) xx_mem_free(state);
        if (stream) xx_mem_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    if (!xx_matter_ota_copy_options(&state->options, options) ||
        !xx_matter_ota_parse(self, &stream->parsed, false, pd)) {
        xx_matter_ota_archive_stream_free(stream);
        xx_archive_record_state_free(state);
        return NULL;
    }
    stream->index = 0U;
    state->internal_state = stream;
    state->free_internal = xx_matter_ota_archive_stream_free;
    state->total_records = 1;
    if (xx_matter_ota_populate_record(&state->current_record, &stream->parsed)) {
        state->has_record = true;
        state->current_index = 0;
    }
    return state;
}

const xx_archive_record *xx_matter_ota_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_matter_ota_archive_record_move_to_next(Abstractformat *self,
                                               xx_archive_record_state *state,
                                               xx_pd_struct *pd) {
    xx_matter_ota_archive_stream *stream;
    if (!self || !state || state->format != self || !state->has_record ||
        !state->internal_state || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    /* Exactly one record: the first move_to_next always ends the walk. */
    stream = (xx_matter_ota_archive_stream *)state->internal_state;
    ++stream->index;
    xx_archive_record_cleanup(&state->current_record);
    xx_archive_record_init(&state->current_record);
    state->has_record = false;
    return false;
}

bool xx_matter_ota_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
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
    option =
        xx_matter_ota_find_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
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

void xx_matter_ota_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

uint64_t xx_matter_ota_get_total_size(const xx_matter_ota *ota) {
    return ota ? ota->total_size : 0U;
}
uint32_t xx_matter_ota_get_header_size(const xx_matter_ota *ota) {
    return ota ? ota->header_size : 0U;
}
uint64_t xx_matter_ota_get_payload_size(const xx_matter_ota *ota) {
    return ota ? ota->payload_size : 0U;
}
uint64_t xx_matter_ota_get_vendor_id(const xx_matter_ota *ota) {
    return ota ? ota->vendor_id : 0U;
}
uint64_t xx_matter_ota_get_product_id(const xx_matter_ota *ota) {
    return ota ? ota->product_id : 0U;
}
uint64_t xx_matter_ota_get_software_version(const xx_matter_ota *ota) {
    return ota ? ota->software_version : 0U;
}
const char *xx_matter_ota_get_version_string(const xx_matter_ota *ota) {
    const xx_matter_ota_private *parsed =
        ota ? (const xx_matter_ota_private *)ota->internal : NULL;
    return parsed ? parsed->version_string : NULL;
}
const char *xx_matter_ota_get_release_notes_url(const xx_matter_ota *ota) {
    const xx_matter_ota_private *parsed =
        ota ? (const xx_matter_ota_private *)ota->internal : NULL;
    return parsed ? parsed->release_notes_url : NULL;
}
bool xx_matter_ota_get_digest_valid(const xx_matter_ota *ota) {
    return ota ? (ota->digest_checked && ota->digest_valid) : false;
}
