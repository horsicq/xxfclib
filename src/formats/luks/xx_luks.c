/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * LUKS1 and LUKS2 containers. The layouts followed here are the LUKS1
 * On-Disk Format Specification and the LUKS2 format specification, both
 * published by the cryptsetup project; the per-field notes live in xx_luks.h.
 *
 * LUKS1 AES-CBC/plain/plain64/ESSIV-SHA256 and AES-XTS/plain64 payloads
 * are decoded with an explicitly supplied password after PBKDF2/AF keyslot
 * recovery and master-key digest verification. LUKS2 remains metadata-only;
 * unsupported ciphers/KDFs are refused without publishing ciphertext.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/luks/xx_luks.h"

#include "xxfclib/data/xx_data.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>
#include "xxfclib/algo/store/xx_store.h"
#include "../xx_disk_crypto_private.h"

/* Registration placeholder. xxfc_defs.h is shared and is not edited from
 * here, so the alias macro defined next to the enumerator is tested instead;
 * this picks up the real file type as soon as LUKS is registered there. */
#ifdef LUKS
#define XX_LUKS_FILE_TYPE XX_FILE_TYPE_LUKS
#else
#define XX_LUKS_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define XX_LUKS_MAGIC_SIZE 6U
#define XX_LUKS1_HEADER_SIZE 592
#define XX_LUKS2_BINARY_HEADER_SIZE 4096

#define XX_LUKS1_SLOT_BASE 208U
#define XX_LUKS1_SLOT_SIZE 48U
#define XX_LUKS1_SLOT_ENABLED UINT32_C(0x00ac71f3)
#define XX_LUKS1_SLOT_DISABLED UINT32_C(0x0000dead)

/* LUKS2 permits a header of 16 KB up to 4 MB. Anything outside that is not a
 * header this reader will trust. */
#define XX_LUKS2_MIN_HEADER_SIZE UINT64_C(0x4000)
#define XX_LUKS2_MAX_HEADER_SIZE UINT64_C(0x400000)

/* How much of the LUKS2 JSON area is scanned for the data segment offset. */
#define XX_LUKS2_JSON_SCAN 65536U

#define XX_LUKS_MEMBER_NAME "payload.img"

typedef struct xx_luks_private_s {
    int64_t input_size;
    int64_t base_address;
    uint64_t payload_offset;  /**< Absolute device offset. */
    uint64_t payload_size;
    uint64_t header_size;
    uint64_t seqid;
    uint32_t version;
    uint32_t key_bytes;
    uint32_t mk_digest_iter;
    uint32_t active_slots;
    char cipher_name[33];
    char cipher_mode[33];
    char hash_spec[33];
    char uuid[41];
    char label[49];
    char subsystem[49];
    bool payload_offset_exact;
    bool consumed;
} xx_luks_private;

static void xx_luks_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

/* xx_io_seek64, never xx_io_seek: a LUKS volume is usually a whole disk and
 * long is 32 bits on Win64. */
static bool xx_luks_read_at(xx_io_device *device, int64_t offset, void *data,
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

static bool xx_luks_range_within(int64_t total_size, int64_t offset,
                                 int64_t size) {
    return (total_size >= 0) && (offset >= 0) && (size >= 0) &&
           (offset <= total_size) && (size <= total_size - offset);
}

/* Copy a fixed-width, NUL-padded header string into a NUL-terminated buffer,
 * stopping at the first NUL and replacing anything unprintable with '?' so
 * that a hostile header cannot smuggle control bytes into a caller's log. */
static void xx_luks_copy_field(char *destination, size_t destination_size,
                               const uint8_t *source, size_t source_size) {
    size_t index;
    size_t limit = destination_size - 1U;

    if (source_size < limit) limit = source_size;
    for (index = 0U; index < limit; ++index) {
        uint8_t character = source[index];
        if (character == 0U) break;
        destination[index] =
            (character < 32U || character > 126U) ? '?' : (char)character;
    }
    destination[index] = '\0';
}

static void xx_luks_private_free(void *pointer) {
    if (pointer) xx_mem_free(pointer);
}

/* A two-call text builder for the record comment. The CRT is off limits, so
 * sprintf() is not available; these two keep *used a running length and
 * silently stop at the buffer's end rather than truncating mid-write. */
static void xx_luks_append_text(char *buffer, size_t capacity, size_t *used,
                                const char *text) {
    size_t index = 0U;

    if (!text) return;
    while (text[index] != '\0' && *used + 1U < capacity) {
        buffer[*used] = text[index];
        ++(*used);
        ++index;
    }
    buffer[*used] = '\0';
}

static void xx_luks_append_u64(char *buffer, size_t capacity, size_t *used,
                               uint64_t value) {
    char digits[21];
    size_t count = 0U;

    do {
        digits[count++] = (char)('0' + (value % 10U));
        value /= 10U;
    } while (value != 0U && count < sizeof(digits));
    while (count != 0U && *used + 1U < capacity) {
        buffer[*used] = digits[--count];
        ++(*used);
    }
    buffer[*used] = '\0';
}

/* ---------------------------------------------------------- LUKS2 JSON -- */

/* The data segment's offset is a JSON string under "segments", not a binary
 * field, and xxfclib carries no JSON parser. Rather than add one for a single
 * number this does a narrow, bounded scan: find "segments", then the first
 * "offset":"<digits>" after it. It is deliberately conservative - anything
 * unexpected leaves the offset inferred from hdr_size instead, and
 * payload_offset_exact records which of the two happened. */
static bool xx_luks2_scan_segment_offset(const uint8_t *json, size_t size,
                                         uint64_t *out_offset) {
    static const char key_segments[] = "\"segments\"";
    static const char key_offset[] = "\"offset\"";
    size_t index;
    size_t start = 0U;
    bool found_segments = false;

    if (!json || !out_offset || size < sizeof(key_offset)) return false;
    for (index = 0U; index + sizeof(key_segments) - 1U <= size; ++index) {
        if (xx_rt_memcmp(json + index, key_segments,
                         sizeof(key_segments) - 1U) == 0) {
            start = index + sizeof(key_segments) - 1U;
            found_segments = true;
            break;
        }
    }
    if (!found_segments) return false;
    for (index = start; index + sizeof(key_offset) - 1U <= size; ++index) {
        size_t cursor;
        uint64_t value = 0U;
        unsigned digits = 0U;

        if (xx_rt_memcmp(json + index, key_offset, sizeof(key_offset) - 1U) !=
            0) {
            continue;
        }
        cursor = index + sizeof(key_offset) - 1U;
        while (cursor < size && (json[cursor] == ' ' || json[cursor] == ':' ||
                                 json[cursor] == '"')) {
            ++cursor;
        }
        while (cursor < size && json[cursor] >= '0' && json[cursor] <= '9') {
            if (digits >= 19U) return false; /* Not a plausible byte offset. */
            value = value * 10U + (uint64_t)(json[cursor] - '0');
            ++digits;
            ++cursor;
        }
        if (digits == 0U) return false;
        *out_offset = value;
        return true;
    }
    return false;
}

/* ---------------------------------------------------------------- parse -- */

static bool xx_luks_parse_v1(xx_io_device *device, xx_luks_private *parsed) {
    uint8_t header[XX_LUKS1_HEADER_SIZE];
    uint64_t payload;
    unsigned slot;

    if (!xx_luks_range_within(parsed->input_size, parsed->base_address,
                              XX_LUKS1_HEADER_SIZE) ||
        !xx_luks_read_at(device, parsed->base_address, header,
                         sizeof(header))) {
        return false;
    }
    xx_luks_copy_field(parsed->cipher_name, sizeof(parsed->cipher_name),
                       header + 8, 32U);
    xx_luks_copy_field(parsed->cipher_mode, sizeof(parsed->cipher_mode),
                       header + 40, 32U);
    xx_luks_copy_field(parsed->hash_spec, sizeof(parsed->hash_spec),
                       header + 72, 32U);
    xx_luks_copy_field(parsed->uuid, sizeof(parsed->uuid), header + 168, 40U);
    /* The payload offset is in 512-byte sectors and must clear the header. */
    payload = (uint64_t)xx_data_get_u32(header, sizeof(header), 104U, true) *
              512U;
    parsed->key_bytes = xx_data_get_u32(header, sizeof(header), 108U, true);
    parsed->mk_digest_iter = xx_data_get_u32(header, sizeof(header), 164U, true);
    if (payload < (uint64_t)XX_LUKS1_HEADER_SIZE) return false;
    /* The master key is at most 64 bytes for every cipher LUKS1 defines;
     * a wilder value means this is not a LUKS1 header. */
    if (parsed->key_bytes == 0U || parsed->key_bytes > 1024U) return false;
    if (parsed->cipher_name[0] == '\0') return false;

    for (slot = 0U; slot < XX_LUKS_KEY_SLOTS; ++slot) {
        size_t at = XX_LUKS1_SLOT_BASE + (size_t)slot * XX_LUKS1_SLOT_SIZE;
        uint32_t state = xx_data_get_u32(header, sizeof(header), at, true);
        if (state == XX_LUKS1_SLOT_ENABLED) {
            parsed->active_slots |= (UINT32_C(1) << slot);
        } else if (state != XX_LUKS1_SLOT_DISABLED) {
            /* Neither marker: the header is not a LUKS1 phdr. */
            return false;
        }
    }
    parsed->header_size = XX_LUKS1_HEADER_SIZE;
    parsed->payload_offset = (uint64_t)parsed->base_address + payload;
    parsed->payload_offset_exact = true;
    return true;
}

static bool xx_luks_parse_v2(xx_io_device *device, xx_luks_private *parsed) {
    uint8_t header[512];
    uint8_t *json = NULL;
    uint64_t hdr_size;
    uint64_t json_size;
    uint64_t payload = 0U;
    size_t scan;

    if (!xx_luks_range_within(parsed->input_size, parsed->base_address,
                              XX_LUKS2_BINARY_HEADER_SIZE) ||
        !xx_luks_read_at(device, parsed->base_address, header,
                         sizeof(header))) {
        return false;
    }
    hdr_size = xx_data_get_u64(header, sizeof(header), 8U, true);
    if (hdr_size < XX_LUKS2_MIN_HEADER_SIZE ||
        hdr_size > XX_LUKS2_MAX_HEADER_SIZE) {
        return false;
    }
    if (!xx_luks_range_within(parsed->input_size, parsed->base_address,
                              (int64_t)hdr_size)) {
        return false;
    }
    parsed->header_size = hdr_size;
    parsed->seqid = xx_data_get_u64(header, sizeof(header), 16U, true);
    xx_luks_copy_field(parsed->label, sizeof(parsed->label), header + 24, 48U);
    xx_luks_copy_field(parsed->hash_spec, sizeof(parsed->hash_spec),
                       header + 72, 32U);
    xx_luks_copy_field(parsed->uuid, sizeof(parsed->uuid), header + 168, 40U);
    xx_luks_copy_field(parsed->subsystem, sizeof(parsed->subsystem),
                       header + 208, 48U);

    /* The JSON area runs from the end of the 4096-byte binary header to
     * hdr_size. Only the first chunk of it is scanned. */
    json_size = hdr_size - (uint64_t)XX_LUKS2_BINARY_HEADER_SIZE;
    scan = json_size > XX_LUKS2_JSON_SCAN ? XX_LUKS2_JSON_SCAN
                                          : (size_t)json_size;
    if (scan != 0U) {
        json = (uint8_t *)xx_mem_alloc(scan);
        if (json &&
            xx_luks_read_at(device,
                            parsed->base_address + XX_LUKS2_BINARY_HEADER_SIZE,
                            json, scan)) {
            if (xx_luks2_scan_segment_offset(json, scan, &payload) &&
                payload >= hdr_size &&
                payload <= (uint64_t)parsed->input_size) {
                parsed->payload_offset =
                    (uint64_t)parsed->base_address + payload;
                parsed->payload_offset_exact = true;
            }
        }
        if (json) xx_mem_free(json);
    }
    if (!parsed->payload_offset_exact) {
        /* Fall back to the layout cryptsetup always writes: the primary
         * header at 0 and its secondary copy immediately after, with the data
         * segment starting after both. Flagged as inferred, not read. */
        uint64_t inferred = hdr_size * 2U;
        if (inferred > (uint64_t)parsed->input_size) return false;
        parsed->payload_offset = (uint64_t)parsed->base_address + inferred;
    }
    /* LUKS2 names its cipher inside the JSON segment description, which this
     * reader does not parse; leaving the fields empty is honest. */
    return true;
}

static bool xx_luks_parse_impl(Abstractformat *self, xx_luks_private *parsed,
                          xx_pd_struct *pd) {
    uint8_t magic[8];
    int64_t total_size;

    if (parsed) {
        xx_mem_zero(parsed, sizeof(*parsed));
        parsed->input_size = -1;
    }
    if (!self || !self->device || !parsed || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    total_size = xx_io_total_size(self->device);
    if (!xx_luks_range_within(total_size, self->base_address, 8) ||
        !xx_luks_read_at(self->device, self->base_address, magic,
                         sizeof(magic))) {
        return false;
    }
    if (xx_rt_memcmp(magic, "LUKS\xba\xbe", XX_LUKS_MAGIC_SIZE) != 0) {
        return false;
    }
    parsed->input_size = total_size;
    parsed->base_address = self->base_address;
    parsed->version = xx_data_get_u16(magic, sizeof(magic), 6U, true);
    if (parsed->version == 1U) {
        if (!xx_luks_parse_v1(self->device, parsed)) return false;
    } else if (parsed->version == 2U) {
        if (!xx_luks_parse_v2(self->device, parsed)) return false;
    } else {
        /* A LUKS version this reader has never seen. Refused rather than
         * guessed at: the two known versions share nothing but the magic. */
        return false;
    }
    if (parsed->payload_offset > (uint64_t)total_size) return false;
    parsed->payload_size = (uint64_t)total_size - parsed->payload_offset;
    return true;
}

static bool xx_luks_parse(Abstractformat *self, xx_luks_private *parsed, xx_pd_struct *pd) {
    int64_t cursor=self&&self->device?xx_io_tell(self->device):-1; bool ok=xx_luks_parse_impl(self,parsed,pd);
    if(cursor>=0&&xx_io_seek64(self->device,cursor,SEEK_SET)!=0) { ok=false; } return ok;
}

/* ------------------------------------------------------------ lifecycle -- */

void xx_luks_init(xx_luks *luks, xx_io_device *dev, int64_t base_address) {
    if (!luks) return;
    xx_mem_zero(luks, sizeof(*luks));
    xx_format_init(&luks->format, dev, base_address);
    luks->format.endian = XX_ENDIAN_BIG;
    luks->format.file_type = XX_LUKS_FILE_TYPE;
    luks->format.format_type = XX_TYPE_ARCHIVE;
    luks->format.is_archive = true;
    xx_format_set_mime_type(&luks->format, "application/x-luks-volume");
    xx_format_set_extension(&luks->format, "luks");
    luks->format.check_is_valid = xx_luks_check_is_valid;
    luks->format.handle_base_info = xx_luks_handle_base_info;
    luks->format.get_format_size = xx_luks_get_format_size;
    luks->format.get_number_of_archive_records =
        xx_luks_get_number_of_archive_records;
    luks->format.create_archive_records_reading =
        xx_luks_create_archive_records_reading;
    luks->format.get_current_archive_record = xx_luks_get_current_archive_record;
    luks->format.unpack_current_archive_record =
        xx_luks_unpack_current_archive_record;
    luks->format.archive_record_move_to_next = xx_luks_archive_record_move_to_next;
    luks->format.free_archive_records_reading =
        xx_luks_free_archive_records_reading;
    luks->format.destroy = xx_luks_vtable_destroy;
}

xx_luks *xx_luks_create(xx_io_device *dev, int64_t base_address) {
    xx_luks *luks = (xx_luks *)xx_mem_alloc(sizeof(*luks));

    if (luks) xx_luks_init(luks, dev, base_address);
    return luks;
}

void xx_luks_destroy(xx_luks *luks) {
    if (!luks) return;
    if (luks->internal) {
        xx_luks_private_free(luks->internal);
        luks->internal = NULL;
    }
    xx_format_cleanup_extra_parameters(&luks->format);
}

static void xx_luks_vtable_destroy(Abstractformat *self) {
    xx_luks_destroy((xx_luks *)self);
}

void xx_luks_free(xx_luks *luks) {
    if (!luks) return;
    xx_luks_destroy(luks);
    xx_mem_free(luks);
}

/* --------------------------------------------------------------- format -- */

bool xx_luks_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_luks_private parsed;

    return xx_luks_parse(self, &parsed, pd);
}

bool xx_luks_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_luks *luks = (xx_luks *)self;
    xx_luks_private *parsed;

    if (!self || !luks) return false;
    parsed = (xx_luks_private *)xx_mem_alloc(sizeof(*parsed));
    if (!parsed || !xx_luks_parse(self, parsed, pd)) {
        if (parsed) xx_mem_free(parsed);
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    if (luks->internal) xx_luks_private_free(luks->internal);
    luks->internal = parsed;
    luks->number_of_records = 1U;
    luks->payload_offset = parsed->payload_offset;
    luks->payload_size = parsed->payload_size;
    luks->header_size = parsed->header_size;
    luks->seqid = parsed->seqid;
    luks->version = parsed->version;
    luks->key_bytes = parsed->key_bytes;
    luks->mk_digest_iter = parsed->mk_digest_iter;
    luks->active_slots = parsed->active_slots;
    luks->payload_offset_exact = parsed->payload_offset_exact;
    xx_mem_copy(luks->cipher_name, parsed->cipher_name,
                sizeof(luks->cipher_name));
    xx_mem_copy(luks->cipher_mode, parsed->cipher_mode,
                sizeof(luks->cipher_mode));
    xx_mem_copy(luks->hash_spec, parsed->hash_spec, sizeof(luks->hash_spec));
    xx_mem_copy(luks->uuid, parsed->uuid, sizeof(luks->uuid));
    xx_mem_copy(luks->label, parsed->label, sizeof(luks->label));
    xx_mem_copy(luks->subsystem, parsed->subsystem, sizeof(luks->subsystem));
    /* Header plus payload is the whole volume, so there is never an overlay. */
    self->format_size = parsed->input_size - self->base_address;
    self->overlay_offset = -1;
    self->overlay_size = 0;
    self->number_of_archive_records = 1U;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_luks_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return -1;
    }
    return self->format_size;
}

uint64_t xx_luks_get_number_of_archive_records(Abstractformat *self,
                                               xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return ((xx_luks *)self)->number_of_records;
}

/* -------------------------------------------------------------- records -- */

static bool xx_luks_copy_options(xx_list_s *destination,
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

static bool xx_luks_populate_record(xx_archive_record *record,
                                    const xx_luks_private *parsed) {
    char detail[192];
    size_t used = 0U;

    if (!record || !parsed) return false;
    detail[0] = '\0';
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = parsed->base_address;
    record->header_size = (int64_t)parsed->header_size;
    record->data_offset = (int64_t)parsed->payload_offset;
    record->compressed_size = (int64_t)parsed->payload_size;
    if (parsed->version == 1U) {
        xx_luks_append_text(detail, sizeof(detail), &used, "LUKS1 cipher=");
        xx_luks_append_text(detail, sizeof(detail), &used, parsed->cipher_name);
        xx_luks_append_text(detail, sizeof(detail), &used, "-");
        xx_luks_append_text(detail, sizeof(detail), &used, parsed->cipher_mode);
        xx_luks_append_text(detail, sizeof(detail), &used, " hash=");
        xx_luks_append_text(detail, sizeof(detail), &used, parsed->hash_spec);
        xx_luks_append_text(detail, sizeof(detail), &used, " key_bytes=");
        xx_luks_append_u64(detail, sizeof(detail), &used, parsed->key_bytes);
        xx_luks_append_text(detail, sizeof(detail), &used, " active_slots=");
        xx_luks_append_u64(detail, sizeof(detail), &used, parsed->active_slots);
    } else {
        xx_luks_append_text(detail, sizeof(detail), &used, "LUKS2 hdr_size=");
        xx_luks_append_u64(detail, sizeof(detail), &used, parsed->header_size);
        xx_luks_append_text(detail, sizeof(detail), &used, " csum_alg=");
        xx_luks_append_text(detail, sizeof(detail), &used, parsed->hash_spec);
        xx_luks_append_text(detail, sizeof(detail), &used, " label=");
        xx_luks_append_text(detail, sizeof(detail), &used, parsed->label);
        xx_luks_append_text(detail, sizeof(detail), &used, " payload=");
        xx_luks_append_text(detail, sizeof(detail), &used,
                            parsed->payload_offset_exact ? "from-json"
                                                         : "inferred");
    }
    xx_luks_append_text(detail, sizeof(detail), &used, " ENCRYPTED");
    return xx_archive_record_set_original_name(record, XX_LUKS_MEMBER_NAME) &&
           /* Supported sector ciphers preserve the declared payload length. */
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          parsed->payload_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          parsed->payload_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          0U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false) &&
           /* The point of this reader. */
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           true) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_ENCRYPTION_METHOD,
                                          parsed->version) &&
           xx_archive_record_set_meta_str(record, XX_META_ID_COMMENT, detail);
}

xx_archive_record_state *xx_luks_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_record_state *state;
    xx_luks_private *parsed;

    if (!self || !self->device ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    parsed = (xx_luks_private *)xx_mem_calloc(1U, sizeof(*parsed));
    if (!state || !parsed) {
        if (state) xx_mem_free(state);
        if (parsed) xx_mem_free(parsed);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    if (!xx_luks_copy_options(&state->options, options) ||
        !xx_luks_parse(self, parsed, pd)) {
        xx_luks_private_free(parsed);
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->internal_state = parsed;
    state->free_internal = xx_luks_private_free;
    state->total_records = 1;
    if (!xx_luks_populate_record(&state->current_record, parsed)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_luks_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_luks_archive_record_move_to_next(Abstractformat *self,
                                         xx_archive_record_state *state,
                                         xx_pd_struct *pd) {
    xx_luks_private *parsed;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    parsed = (xx_luks_private *)state->internal_state;
    if (parsed) parsed->consumed = true;
    xx_archive_record_cleanup(&state->current_record);
    xx_archive_record_init(&state->current_record);
    state->has_record = false;
    return false;
}

static bool xx_luks_decode(Abstractformat *self, const xx_luks_private *parsed, dc_crypto *crypto, xx_io_device *output, xx_pd_struct *pd) {
    uint8_t *buffer=(uint8_t*)xx_mem_alloc(65536); uint64_t done=0; bool result=false; int level;
    if(!buffer) { return false; } level=xx_pd_enter_level(pd,parsed->payload_size,"Decoding LUKS1 payload");
    while(done<parsed->payload_size) {
        size_t n=(size_t)(parsed->payload_size-done),sent=0; if(n>65536) n=65536;
        if(xx_pd_is_stopped(pd) || !dc_read_at(self->device,parsed->payload_offset+done,buffer,n) || !dc_decrypt(crypto,done/512,buffer,n,pd)) goto end;
        while(output && sent<n) { ssize_t z=xx_io_write(output,buffer+sent,n-sent); if(z<=0||(size_t)z>n-sent) goto end; sent+=(size_t)z; }
        done+=n; xx_pd_set_current(pd,level,done);
    } result=!xx_pd_is_stopped(pd);
end: xx_pd_leave_level(pd,level); dc_clear(buffer,65536); xx_mem_free(buffer); return result;
}

bool xx_luks_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    xx_luks_private *parsed; dc_crypto crypto; const uint8_t *pw; size_t pwn; char *owned=NULL,*wide=NULL,*path=NULL,*stage=NULL;
    const xx_var *value; const char *base; xx_io_device *output=NULL; uint64_t memory,member; int64_t cursor; bool result=false,overwrite=false;
    if(!self||!self->device||!state||state->format!=self||!state->has_record||xx_pd_is_stopped(pd)) return false;
    parsed=(xx_luks_private*)state->internal_state; if(!parsed||parsed->consumed) return false;
    xx_mem_zero(&crypto,sizeof(crypto)); cursor=xx_io_tell(self->device);
    if(parsed->version!=1) { xx_pd_set_error(pd,XXFC_ERR_INVALID_ARG,"LUKS2 payload decryption is not supported"); goto done; }
    if((parsed->payload_size&511U) || !dc_limit(self,&state->options,XX_META_ID_OPT_MEMORY_LIMIT,UINT64_MAX,&memory) ||
       !dc_limit(self,&state->options,XX_META_ID_OPT_MAX_MEMBER_SIZE,UINT64_MAX,&member) || memory<65536+16384U || parsed->payload_size>member) goto done;
    if(!dc_password(self,&state->options,&pw,&pwn,&owned,memory-(65536+16384U))) { xx_pd_set_error(pd,XXFC_ERR_INVALID_ARG,"LUKS1 password required"); goto done; }
    if(!dc_luks_unlock(self->device,(uint64_t)parsed->base_address,(uint64_t)(parsed->input_size-parsed->base_address),false,pw,pwn,&crypto,pd)) {
        if(!xx_pd_is_stopped(pd)) { xx_pd_set_error(pd,XXFC_ERR_GENERIC,"LUKS1 keyslot could not be unlocked (wrong password or unsupported parameters)"); } goto done;
    }
    value=xx_format_resolve_extra_parameter(self,&state->options,XX_META_ID_OPT_UNPACK_PATH);
    if(!value) { result=xx_luks_decode(self,parsed,&crypto,NULL,pd); goto done; }
    base=NULL; if(value->type==XX_VAR_TYPE_STRING||value->type==XX_VAR_TYPE_STRING_VIEW) base=xx_var_get_str(value);
    else if(value->type==XX_VAR_TYPE_WSTRING||value->type==XX_VAR_TYPE_WSTRING_VIEW) { wide=xx_str_unicode_to_utf8(xx_var_get_wstr(value)); base=wide; }
    if(!base) goto done;
    path=xx_str_concat3(base,(base[0]&&base[xx_str_len(base)-1]!='/'&&base[xx_str_len(base)-1]!='\\')?"/":"",XX_LUKS_MEMBER_NAME);
    if(!path || !xx_store_create_dirs_a(path,false)) goto done;
    value=xx_format_resolve_extra_parameter(self,&state->options,XX_META_ID_OPT_OVERWRITE);overwrite=value&&xx_var_get_bool(value);
    if(dc_same_path(path,xx_io_source_path(self->device))||(!overwrite&&xx_io_file_exists_a(path))) goto done;
    output=dc_stage(path,&stage);
    result=output && xx_luks_decode(self,parsed,&crypto,output,pd);
    if(output) { if(xx_io_close(output)!=0) result=false; output=NULL; }
    if(result&&stage) result=!xx_pd_is_stopped(pd)&&xx_io_file_replace_a(stage,path,overwrite);
done:
    if(output) { xx_io_close(output); } if(owned) { dc_clear(owned,xx_str_len(owned)); xx_str_free(owned); }
    if(stage) { if(!result) xx_io_file_remove_a(stage); xx_str_free(stage); }
    if(wide) { xx_str_free(wide); } if(path) xx_str_free(path); dc_clear(&crypto,sizeof(crypto));
    if(cursor>=0&&xx_io_seek64(self->device,cursor,SEEK_SET)!=0) { result=false; } return result;
}

void xx_luks_free_archive_records_reading(Abstractformat *self,
                                          xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

/* ------------------------------------------------------------ accessors -- */

uint32_t xx_luks_get_version(const xx_luks *luks) {
    return luks ? luks->version : 0U;
}
uint64_t xx_luks_get_payload_offset(const xx_luks *luks) {
    return luks ? luks->payload_offset : 0U;
}
const char *xx_luks_get_cipher_name(const xx_luks *luks) {
    return luks ? luks->cipher_name : NULL;
}
const char *xx_luks_get_cipher_mode(const xx_luks *luks) {
    return luks ? luks->cipher_mode : NULL;
}
const char *xx_luks_get_uuid(const xx_luks *luks) {
    return luks ? luks->uuid : NULL;
}
bool xx_luks_is_key_slot_active(const xx_luks *luks, unsigned index) {
    if (!luks || index >= XX_LUKS_KEY_SLOTS) return false;
    return (luks->active_slots & (UINT32_C(1) << index)) != 0U;
}
