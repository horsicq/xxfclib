/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* UEFI capsule reader.
 *
 * Implemented: the EFI_CAPSULE_HEADER, the body of a capsule whose GUID is
 * not one this reader knows how to take apart (published whole, so that a
 * caller can hand it to another reader), and the
 * EFI_FIRMWARE_MANAGEMENT_CAPSULE_HEADER item array, whose embedded drivers,
 * payload images and per-payload vendor code blocks are each published as
 * their own record. Image headers of version 1, 2 and 3 are all handled.
 *
 * NOT implemented: authentication. A capsule may be wrapped in an
 * EFI_FIRMWARE_IMAGE_AUTHENTICATION / WIN_CERTIFICATE_UEFI_GUID block, and
 * no signature is verified here - the bytes are published as they are found
 * and a caller must not read a successful parse as a valid signature.
 * Neither is the capsule split across scatter-gather blocks reassembled:
 * that list lives in the caller's memory map, not in the file, so it is not
 * something a file reader can see. Nothing is decompressed: a capsule body
 * is not compressed by the capsule format itself.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/uefi_capsule/xx_uefi_capsule.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

/* Registration placeholder. xxfc_defs.h is shared and is not edited from
 * here, so the file-type constant is supplied locally until the enumerator
 * lands. Delete this block once XX_FILE_TYPE_UEFI_CAPSULE exists in the
 * enum. */
#ifdef UEFI_CAPSULE
#define XX_UEFI_CAPSULE_FILE_TYPE XX_FILE_TYPE_UEFI_CAPSULE
#else
#define XX_UEFI_CAPSULE_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define XX_UEFI_CAPSULE_GUID_SIZE 16U
#define XX_UEFI_CAPSULE_HEADER_SIZE 28U
#define XX_UEFI_CAPSULE_HEADER_MAX UINT32_C(0x10000)
#define XX_UEFI_CAPSULE_FMP_HEADER_SIZE 8U
#define XX_UEFI_CAPSULE_IMAGE_HEADER_V1 32U
#define XX_UEFI_CAPSULE_IMAGE_HEADER_V2 40U
#define XX_UEFI_CAPSULE_IMAGE_HEADER_V3 48U

/* Budgets. An item list is a u16 count, so the list itself is bounded by the
 * format; the entry cap is the backstop for everything else. */
#define XX_UEFI_CAPSULE_MAX_ITEMS 4096U
#define XX_UEFI_CAPSULE_MAX_ENTRIES 16384U
#define XX_UEFI_CAPSULE_MAX_NAME 256U

typedef struct xx_uefi_capsule_entry_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t data_size;
    bool is_folder;
} xx_uefi_capsule_entry;

typedef struct xx_uefi_capsule_private_s {
    xx_uefi_capsule_entry *entries;
    size_t count;
    size_t capacity;
    int64_t input_size;
    int64_t archive_end;
    uint32_t header_size;
    uint32_t flags;
    uint32_t capsule_image_size;
    uint32_t driver_count;
    uint32_t payload_count;
    bool is_firmware_management;
} xx_uefi_capsule_private;

typedef struct xx_uefi_capsule_archive_stream_s {
    xx_uefi_capsule_private parsed;
    size_t index;
} xx_uefi_capsule_archive_stream;

/* A bounded name builder; the CRT string functions are unavailable, so the
 * pieces of a record name are appended into a fixed buffer that refuses to
 * overflow. */
typedef struct xx_uefi_capsule_name_s {
    char data[XX_UEFI_CAPSULE_MAX_NAME];
    size_t used;
    bool overflow;
} xx_uefi_capsule_name;

typedef struct xx_uefi_capsule_known_guid_s {
    uint8_t guid[XX_UEFI_CAPSULE_GUID_SIZE];
    const char *name;
    bool is_firmware_management;
} xx_uefi_capsule_known_guid;

/* On-disk mixed-endian layout: the first three fields are little endian and
 * the trailing eight bytes are in order. */
static const xx_uefi_capsule_known_guid xx_uefi_capsule_known_guids[] = {
    {{0xED, 0xD5, 0xCB, 0x6D, 0x2D, 0xE8, 0x44, 0x4C, 0xBD, 0xA1, 0x71, 0x94,
      0x19, 0x9A, 0xD9, 0x2A},
     "FIRMWARE_MANAGEMENT_CAPSULE", true},
    {{0x62, 0x81, 0x8C, 0x3B, 0x8C, 0x18, 0xA4, 0x46, 0xAE, 0xC9, 0xBE, 0x43,
      0xF1, 0xD6, 0x56, 0x97},
     "WINDOWS_UX_CAPSULE", false},
    {{0x46, 0x8C, 0xB6, 0x39, 0xFB, 0xF7, 0x1B, 0x44, 0xB6, 0xEC, 0x16, 0xB0,
      0xF6, 0x98, 0x21, 0xF3},
     "CAPSULE_REPORT", false},
    {{0x9D, 0xD2, 0xAF, 0x4A, 0xDF, 0x68, 0xEE, 0x49, 0x8A, 0xA9, 0x34, 0x7D,
      0x37, 0x56, 0x65, 0xA7},
     "FIRMWARE_CONTENTS_SIGNED", false}};

static void xx_uefi_capsule_vtable_destroy(Abstractformat *self);

/* ---------------------------------------------------------------- helpers */

static bool xx_uefi_capsule_read_at(xx_io_device *device, int64_t offset,
                                    void *data, size_t size) {
    uint8_t *out = (uint8_t *)data;
    size_t done = 0U;
    /* The 64-bit seek is the only correct one: xx_io_seek() takes a long,
     * and a long is 32 bits on Win64 while a capsule can be larger. */
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

/* True when [offset, offset + size) lies inside [0, total_size). */
static bool xx_uefi_capsule_range_within(int64_t total_size, int64_t offset,
                                         int64_t size) {
    return (total_size >= 0) && (offset >= 0) && (size >= 0) &&
           (offset <= total_size) && (size <= total_size - offset);
}

static void xx_uefi_capsule_name_reset(xx_uefi_capsule_name *name) {
    if (!name) return;
    name->used = 0U;
    name->overflow = false;
    name->data[0] = '\0';
}

static void xx_uefi_capsule_name_add_char(xx_uefi_capsule_name *name,
                                          char ch) {
    if (!name || name->overflow) return;
    if (name->used + 1U >= sizeof(name->data)) {
        name->overflow = true;
        return;
    }
    name->data[name->used++] = ch;
    name->data[name->used] = '\0';
}

static void xx_uefi_capsule_name_add(xx_uefi_capsule_name *name,
                                     const char *text) {
    size_t index;
    if (!name || !text) return;
    for (index = 0U; text[index] != '\0'; ++index) {
        xx_uefi_capsule_name_add_char(name, text[index]);
    }
}

/* Append a decimal number, zero padded to at least two digits so that the
 * record names of one item list sort in item order. */
static void xx_uefi_capsule_name_add_index(xx_uefi_capsule_name *name,
                                           uint32_t value) {
    char digits[12];
    size_t used = 0U;
    if (!name) return;
    do {
        digits[used++] = (char)('0' + (value % 10U));
        value /= 10U;
    } while (value != 0U && used < sizeof(digits));
    if (used < 2U) xx_uefi_capsule_name_add_char(name, '0');
    while (used != 0U) xx_uefi_capsule_name_add_char(name, digits[--used]);
}

static void xx_uefi_capsule_name_add_hex8(xx_uefi_capsule_name *name,
                                          uint8_t value) {
    static const char digits[] = "0123456789abcdef";
    xx_uefi_capsule_name_add_char(name, digits[(value >> 4U) & 0x0FU]);
    xx_uefi_capsule_name_add_char(name, digits[value & 0x0FU]);
}

/* Render a GUID in its canonical text form; the first three fields are
 * little endian on disk and the last eight bytes are printed in order. */
static void xx_uefi_capsule_name_add_guid(xx_uefi_capsule_name *name,
                                          const uint8_t *guid) {
    static const int order[XX_UEFI_CAPSULE_GUID_SIZE] = {
        3, 2, 1, 0, 5, 4, 7, 6, 8, 9, 10, 11, 12, 13, 14, 15};
    size_t index;
    if (!name || !guid) return;
    for (index = 0U; index < XX_UEFI_CAPSULE_GUID_SIZE; ++index) {
        if (index == 4U || index == 6U || index == 8U || index == 10U) {
            xx_uefi_capsule_name_add_char(name, '-');
        }
        xx_uefi_capsule_name_add_hex8(name, guid[order[index]]);
    }
}

static char *xx_uefi_capsule_name_dup(const xx_uefi_capsule_name *name) {
    char *copy;
    if (!name || name->overflow || name->used == 0U) return NULL;
    copy = (char *)xx_mem_alloc(name->used + 1U);
    if (!copy) return NULL;
    xx_mem_copy(copy, name->data, name->used);
    copy[name->used] = '\0';
    return copy;
}

static const xx_uefi_capsule_known_guid *xx_uefi_capsule_lookup_guid(
    const uint8_t *guid) {
    size_t index;
    if (!guid) return NULL;
    for (index = 0U;
         index < sizeof(xx_uefi_capsule_known_guids) /
                     sizeof(xx_uefi_capsule_known_guids[0]);
         ++index) {
        if (xx_rt_memcmp(xx_uefi_capsule_known_guids[index].guid, guid,
                         XX_UEFI_CAPSULE_GUID_SIZE) == 0) {
            return &xx_uefi_capsule_known_guids[index];
        }
    }
    return NULL;
}

static bool xx_uefi_capsule_all_bytes(const uint8_t *data, size_t size,
                                      uint8_t value) {
    size_t index;
    for (index = 0U; index < size; ++index) {
        if (data[index] != value) return false;
    }
    return true;
}

/* ------------------------------------------------------------- collection */

static void xx_uefi_capsule_private_cleanup(xx_uefi_capsule_private *parsed) {
    size_t index;
    if (!parsed) return;
    for (index = 0U; index < parsed->count; ++index) {
        if (parsed->entries[index].name) {
            xx_str_free(parsed->entries[index].name);
        }
    }
    if (parsed->entries) xx_mem_free(parsed->entries);
    xx_mem_zero(parsed, sizeof(*parsed));
    parsed->input_size = -1;
    parsed->archive_end = -1;
}

static bool xx_uefi_capsule_append_entry(xx_uefi_capsule_private *parsed,
                                         xx_uefi_capsule_entry *entry) {
    xx_uefi_capsule_entry *grown;
    size_t capacity;
    if (!parsed || !entry || !entry->name ||
        parsed->count >= XX_UEFI_CAPSULE_MAX_ENTRIES) {
        return false;
    }
    if (parsed->count == parsed->capacity) {
        capacity = parsed->capacity ? parsed->capacity * 2U : 16U;
        if (capacity < parsed->count ||
            capacity > SIZE_MAX / sizeof(*parsed->entries)) return false;
        grown = (xx_uefi_capsule_entry *)xx_mem_realloc(
            parsed->entries, capacity * sizeof(*parsed->entries));
        if (!grown) return false;
        parsed->entries = grown;
        parsed->capacity = capacity;
    }
    parsed->entries[parsed->count++] = *entry;
    xx_mem_zero(entry, sizeof(*entry));
    return true;
}

/* Publish one record. The name is consumed either way. */
static bool xx_uefi_capsule_publish(xx_uefi_capsule_private *parsed,
                                    char *name, int64_t header_offset,
                                    int64_t header_size, int64_t data_offset,
                                    int64_t data_size, bool is_folder) {
    xx_uefi_capsule_entry entry;
    if (!name) return false;
    xx_mem_zero(&entry, sizeof(entry));
    entry.name = name;
    entry.header_offset = header_offset;
    entry.header_size = header_size;
    entry.data_offset = data_offset;
    entry.data_size = data_size;
    entry.is_folder = is_folder;
    if (!xx_uefi_capsule_append_entry(parsed, &entry)) {
        xx_str_free(name);
        return false;
    }
    return true;
}

/* ---------------------------------------------------------------- the walk */

/* Walk one EFI_FIRMWARE_MANAGEMENT_CAPSULE_IMAGE_HEADER at offset, which
 * must lie inside [body, end). Publishes the image and, when present, the
 * vendor code block that follows it. */
static void xx_uefi_capsule_walk_image(Abstractformat *self,
                                       xx_uefi_capsule_private *parsed,
                                       int64_t offset, int64_t end,
                                       uint32_t index,
                                       const xx_uefi_capsule_name *prefix) {
    uint8_t header[XX_UEFI_CAPSULE_IMAGE_HEADER_V3];
    uint32_t version;
    uint32_t image_size;
    uint32_t vendor_size;
    int64_t header_size = XX_UEFI_CAPSULE_IMAGE_HEADER_V1;
    int64_t data;
    xx_uefi_capsule_name name;

    if (!xx_uefi_capsule_range_within(end, offset,
                                      XX_UEFI_CAPSULE_IMAGE_HEADER_V1) ||
        !xx_uefi_capsule_read_at(self->device, offset, header,
                                 XX_UEFI_CAPSULE_IMAGE_HEADER_V1)) {
        return;
    }
    version = xx_data_get_u32(header, XX_UEFI_CAPSULE_IMAGE_HEADER_V1, 0U,
                              false);
    if (version == 0U || version > 16U) return;
    if (version >= 2U) header_size = XX_UEFI_CAPSULE_IMAGE_HEADER_V2;
    if (version >= 3U) header_size = XX_UEFI_CAPSULE_IMAGE_HEADER_V3;
    if (!xx_uefi_capsule_range_within(end, offset, header_size)) return;
    image_size = xx_data_get_u32(header, XX_UEFI_CAPSULE_IMAGE_HEADER_V1, 24U,
                                 false);
    vendor_size = xx_data_get_u32(header, XX_UEFI_CAPSULE_IMAGE_HEADER_V1, 28U,
                                  false);
    data = offset + header_size;
    /* Both sizes are attacker-controlled u32s, so they are checked against
     * the remaining capsule rather than trusted. */
    if (!xx_uefi_capsule_range_within(end, data, (int64_t)image_size)) return;
    if (!xx_uefi_capsule_range_within(end, data + (int64_t)image_size,
                                      (int64_t)vendor_size)) {
        return;
    }

    name = *prefix;
    xx_uefi_capsule_name_add(&name, "/image");
    xx_uefi_capsule_name_add_index(&name, index);
    xx_uefi_capsule_name_add_char(&name, '_');
    /* UpdateImageTypeId sits at +4 and names what the image updates. */
    xx_uefi_capsule_name_add_guid(&name, header + 4U);
    if (!xx_uefi_capsule_publish(parsed, xx_uefi_capsule_name_dup(&name),
                                 offset, header_size, data,
                                 (int64_t)image_size, false)) {
        return;
    }
    if (vendor_size != 0U) {
        xx_uefi_capsule_name_add(&name, ".vendor");
        (void)xx_uefi_capsule_publish(parsed, xx_uefi_capsule_name_dup(&name),
                                      offset, header_size,
                                      data + (int64_t)image_size,
                                      (int64_t)vendor_size, false);
    }
}

/* Walk the EFI_FIRMWARE_MANAGEMENT_CAPSULE_HEADER item array in
 * [body, end). Returns false when the body is not a usable FMP header, so
 * that the caller can fall back to publishing the body whole. */
static bool xx_uefi_capsule_walk_fmp(Abstractformat *self,
                                     xx_uefi_capsule_private *parsed,
                                     int64_t body, int64_t end,
                                     const xx_uefi_capsule_name *prefix,
                                     xx_pd_struct *pd) {
    uint8_t header[XX_UEFI_CAPSULE_FMP_HEADER_SIZE];
    uint32_t version;
    uint32_t driver_count;
    uint32_t payload_count;
    uint32_t total;
    uint32_t index;
    int64_t list_offset;

    if (!xx_uefi_capsule_range_within(end, body, sizeof(header)) ||
        !xx_uefi_capsule_read_at(self->device, body, header, sizeof(header))) {
        return false;
    }
    version = xx_data_get_u32(header, sizeof(header), 0U, false);
    driver_count = xx_data_get_u16(header, sizeof(header), 4U, false);
    payload_count = xx_data_get_u16(header, sizeof(header), 6U, false);
    total = driver_count + payload_count;
    if (version == 0U || version > 16U || total == 0U ||
        total > XX_UEFI_CAPSULE_MAX_ITEMS) {
        return false;
    }
    list_offset = body + (int64_t)sizeof(header);
    if (!xx_uefi_capsule_range_within(end, list_offset, (int64_t)total * 8)) {
        return false;
    }
    parsed->driver_count = driver_count;
    parsed->payload_count = payload_count;

    for (index = 0U; index < total; ++index) {
        uint8_t raw[8];
        uint64_t item;
        int64_t item_offset;
        int64_t item_end;
        if (pd && xx_pd_is_stopped(pd)) return true;
        if (parsed->count >= XX_UEFI_CAPSULE_MAX_ENTRIES) return true;
        if (!xx_uefi_capsule_read_at(self->device,
                                     list_offset + (int64_t)index * 8, raw,
                                     sizeof(raw))) {
            return true;
        }
        /* Item offsets are relative to the start of the FMP header and are
         * unordered in principle, so each one is bounds-checked on its own
         * instead of being assumed to follow the previous. */
        item = xx_data_get_u64(raw, sizeof(raw), 0U, false);
        if (item > (uint64_t)INT64_MAX) continue;
        item_offset = body + (int64_t)item;
        if (item_offset < list_offset || item_offset >= end) continue;
        if (index < driver_count) {
            /* An embedded driver runs from its offset to the next item, or
             * to the end of the capsule when it is the last one. */
            uint8_t next_raw[8];
            uint64_t next_item;
            xx_uefi_capsule_name name;
            item_end = end;
            if (index + 1U < total &&
                xx_uefi_capsule_read_at(self->device,
                                        list_offset + (int64_t)(index + 1U) * 8,
                                        next_raw, sizeof(next_raw))) {
                next_item = xx_data_get_u64(next_raw, sizeof(next_raw), 0U,
                                            false);
                if (next_item <= (uint64_t)INT64_MAX &&
                    body + (int64_t)next_item > item_offset &&
                    body + (int64_t)next_item <= end) {
                    item_end = body + (int64_t)next_item;
                }
            }
            name = *prefix;
            xx_uefi_capsule_name_add(&name, "/driver");
            xx_uefi_capsule_name_add_index(&name, index);
            if (!xx_uefi_capsule_publish(parsed,
                                         xx_uefi_capsule_name_dup(&name),
                                         item_offset, 0, item_offset,
                                         item_end - item_offset, false)) {
                return true;
            }
        } else {
            xx_uefi_capsule_walk_image(self, parsed, item_offset, end,
                                       index - driver_count, prefix);
        }
    }
    return true;
}

static bool xx_uefi_capsule_parse(Abstractformat *self,
                                  xx_uefi_capsule_private *parsed,
                                  xx_pd_struct *pd) {
    uint8_t header[XX_UEFI_CAPSULE_HEADER_SIZE];
    const xx_uefi_capsule_known_guid *known;
    int64_t total_size;
    int64_t body;
    int64_t end;
    uint32_t header_size;
    uint32_t image_size;
    uint32_t flags;
    xx_uefi_capsule_name root;
    /* Initialise before the guard clause: callers such as
     * xx_uefi_capsule_check_is_valid() run the cleanup on their stack copy
     * whatever this returns. */
    if (parsed) {
        xx_mem_zero(parsed, sizeof(*parsed));
        parsed->input_size = -1;
        parsed->archive_end = -1;
    }
    if (!self || !self->device || !parsed || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd))) return false;
    total_size = xx_io_total_size(self->device);
    if (!xx_uefi_capsule_range_within(total_size, self->base_address,
                                      XX_UEFI_CAPSULE_HEADER_SIZE) ||
        !xx_uefi_capsule_read_at(self->device, self->base_address, header,
                                 sizeof(header))) {
        return false;
    }
    header_size = xx_data_get_u32(header, sizeof(header), 16U, false);
    flags = xx_data_get_u32(header, sizeof(header), 20U, false);
    image_size = xx_data_get_u32(header, sizeof(header), 24U, false);
    if (header_size < XX_UEFI_CAPSULE_HEADER_SIZE ||
        header_size > XX_UEFI_CAPSULE_HEADER_MAX || image_size < header_size ||
        !xx_uefi_capsule_range_within(total_size, self->base_address,
                                      (int64_t)image_size)) {
        return false;
    }
    /* A zero or erased GUID is not a capsule, and neither is an arbitrary
     * blob whose bytes at +16 happen to read as plausible sizes. Detection
     * therefore needs one positive signal beyond the size arithmetic: a
     * capsule GUID this reader knows, one of the three flags the
     * specification defines, or a capsule that fills the input exactly. */
    known = xx_uefi_capsule_lookup_guid(header);
    if (xx_uefi_capsule_all_bytes(header, XX_UEFI_CAPSULE_GUID_SIZE, 0x00U) ||
        xx_uefi_capsule_all_bytes(header, XX_UEFI_CAPSULE_GUID_SIZE, 0xFFU)) {
        return false;
    }
    if (!known &&
        (flags & (XX_UEFI_CAPSULE_FLAG_PERSIST_ACROSS_RESET |
                  XX_UEFI_CAPSULE_FLAG_POPULATE_SYSTEM_TABLE |
                  XX_UEFI_CAPSULE_FLAG_INITIATE_RESET)) == 0U &&
        self->base_address + (int64_t)image_size != total_size) {
        return false;
    }

    parsed->input_size = total_size;
    parsed->header_size = header_size;
    parsed->flags = flags;
    parsed->capsule_image_size = image_size;
    parsed->archive_end = self->base_address + (int64_t)image_size;
    body = self->base_address + (int64_t)header_size;
    end = parsed->archive_end;

    xx_uefi_capsule_name_reset(&root);
    xx_uefi_capsule_name_add(&root, "capsule_");
    xx_uefi_capsule_name_add_guid(&root, header);
    if (!xx_uefi_capsule_publish(parsed, xx_uefi_capsule_name_dup(&root),
                                 self->base_address, (int64_t)header_size,
                                 self->base_address, (int64_t)image_size,
                                 true)) {
        goto fail;
    }
    if (known && known->is_firmware_management) {
        parsed->is_firmware_management =
            xx_uefi_capsule_walk_fmp(self, parsed, body, end, &root, pd);
    }
    if (!parsed->is_firmware_management && end > body) {
        /* Whatever else the body is - a firmware volume, a signed image, a
         * vendor blob - it is published whole so that a caller can recurse
         * into it with another reader. */
        xx_uefi_capsule_name payload = root;
        xx_uefi_capsule_name_add(&payload, "/payload");
        if (!xx_uefi_capsule_publish(parsed,
                                     xx_uefi_capsule_name_dup(&payload),
                                     self->base_address,
                                     (int64_t)header_size, body, end - body,
                                     false)) {
            goto fail;
        }
    }
    if (parsed->count == 0U) goto fail;
    return true;
fail:
    xx_uefi_capsule_private_cleanup(parsed);
    return false;
}

/* --------------------------------------------------------------- plumbing */

static bool xx_uefi_capsule_copy_options(xx_list_s *destination,
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

static const xx_var *xx_uefi_capsule_find_option(const xx_list_s *options,
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

static bool xx_uefi_capsule_populate_record(
    xx_archive_record *record, const xx_uefi_capsule_entry *entry) {
    if (!record || !entry || !entry->name) return false;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = entry->header_offset;
    record->header_size = entry->header_size;
    record->data_offset = entry->data_offset;
    record->compressed_size = entry->data_size;
    return xx_archive_record_set_original_name(record, entry->name) &&
           xx_archive_record_set_meta_u64(record,
                                          XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)entry->data_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)entry->data_size) &&
           xx_archive_record_set_meta_u64(record,
                                          XX_META_ID_COMPRESSION_METHOD, 0U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           entry->is_folder);
}

static void xx_uefi_capsule_archive_stream_free(void *pointer) {
    xx_uefi_capsule_archive_stream *stream =
        (xx_uefi_capsule_archive_stream *)pointer;
    if (!stream) return;
    xx_uefi_capsule_private_cleanup(&stream->parsed);
    xx_mem_free(stream);
}

/* Extraction-time check: the names are assembled here from fixed words,
 * indices and GUIDs, so they cannot normally be unsafe - but the name is
 * used as a relative path, so it is checked anyway. */
static bool xx_uefi_capsule_safe_name(const char *name) {
    const char *component;
    const char *cursor;
    if (!name || !name[0] || name[0] == '/' || name[0] == '\\') return false;
    component = name;
    for (cursor = name;; ++cursor) {
        unsigned char ch = (unsigned char)*cursor;
        if (ch == ':' || ch == '<' || ch == '>' || ch == '"' || ch == '|' ||
            ch == '?' || ch == '*' || (ch != 0U && ch < 32U)) return false;
        if (ch == '/' || ch == '\\' || ch == 0U) {
            size_t length = (size_t)(cursor - component);
            if (length == 0U || (length == 1U && component[0] == '.') ||
                (length == 2U && component[0] == '.' && component[1] == '.') ||
                component[length - 1U] == ' ' ||
                component[length - 1U] == '.') {
                return false;
            }
            if (ch == 0U) return true;
            component = cursor + 1;
        }
    }
}

/* --------------------------------------------------------- the public API */

void xx_uefi_capsule_init(xx_uefi_capsule *capsule, xx_io_device *dev,
                          int64_t base_address) {
    if (!capsule) return;
    xx_mem_zero(capsule, sizeof(*capsule));
    xx_format_init(&capsule->format, dev, base_address);
    capsule->format.endian = XX_ENDIAN_LITTLE;
    capsule->format.file_type = XX_UEFI_CAPSULE_FILE_TYPE;
    capsule->format.format_type = XX_TYPE_ARCHIVE;
    capsule->format.is_archive = true;
    xx_format_set_mime_type(&capsule->format, "application/x-uefi-capsule");
    xx_format_set_extension(&capsule->format, "cap");
    capsule->format.check_is_valid = xx_uefi_capsule_check_is_valid;
    capsule->format.handle_base_info = xx_uefi_capsule_handle_base_info;
    capsule->format.get_format_size = xx_uefi_capsule_get_format_size;
    capsule->format.get_number_of_archive_records =
        xx_uefi_capsule_get_number_of_archive_records;
    capsule->format.create_archive_records_reading =
        xx_uefi_capsule_create_archive_records_reading;
    capsule->format.get_current_archive_record =
        xx_uefi_capsule_get_current_archive_record;
    capsule->format.unpack_current_archive_record =
        xx_uefi_capsule_unpack_current_archive_record;
    capsule->format.archive_record_move_to_next =
        xx_uefi_capsule_archive_record_move_to_next;
    capsule->format.free_archive_records_reading =
        xx_uefi_capsule_free_archive_records_reading;
    capsule->format.destroy = xx_uefi_capsule_vtable_destroy;
    capsule->archive_end = -1;
}

xx_uefi_capsule *xx_uefi_capsule_create(xx_io_device *dev,
                                        int64_t base_address) {
    xx_uefi_capsule *capsule =
        (xx_uefi_capsule *)xx_mem_alloc(sizeof(*capsule));
    if (capsule) xx_uefi_capsule_init(capsule, dev, base_address);
    return capsule;
}

void xx_uefi_capsule_destroy(xx_uefi_capsule *capsule) {
    if (!capsule) return;
    if (capsule->internal) {
        xx_uefi_capsule_private_cleanup(
            (xx_uefi_capsule_private *)capsule->internal);
        xx_mem_free(capsule->internal);
        capsule->internal = NULL;
    }
    xx_format_cleanup_extra_parameters(&capsule->format);
}

static void xx_uefi_capsule_vtable_destroy(Abstractformat *self) {
    xx_uefi_capsule_destroy((xx_uefi_capsule *)self);
}

void xx_uefi_capsule_free(xx_uefi_capsule *capsule) {
    if (!capsule) return;
    xx_uefi_capsule_destroy(capsule);
    xx_mem_free(capsule);
}

bool xx_uefi_capsule_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_uefi_capsule_private parsed;
    bool result = xx_uefi_capsule_parse(self, &parsed, pd);
    xx_uefi_capsule_private_cleanup(&parsed);
    return result;
}

bool xx_uefi_capsule_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_uefi_capsule_private *parsed;
    xx_uefi_capsule *capsule = (xx_uefi_capsule *)self;
    int64_t total_size;
    if (!self || !capsule) return false;
    parsed = (xx_uefi_capsule_private *)xx_mem_alloc(sizeof(*parsed));
    if (!parsed || !xx_uefi_capsule_parse(self, parsed, pd)) {
        if (parsed) xx_mem_free(parsed);
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    if (capsule->internal) {
        xx_uefi_capsule_private_cleanup(
            (xx_uefi_capsule_private *)capsule->internal);
        xx_mem_free(capsule->internal);
    }
    capsule->internal = parsed;
    capsule->number_of_records = parsed->count;
    capsule->number_of_members = parsed->count;
    capsule->header_size = parsed->header_size;
    capsule->flags = parsed->flags;
    capsule->capsule_image_size = parsed->capsule_image_size;
    capsule->driver_count = parsed->driver_count;
    capsule->payload_count = parsed->payload_count;
    capsule->is_firmware_management = parsed->is_firmware_management;
    capsule->archive_end = parsed->archive_end;
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

int64_t xx_uefi_capsule_get_format_size(Abstractformat *self,
                                        xx_pd_struct *pd) {
    if (!self || (!self->base_info_handled &&
                  !xx_format_handle_base_info(self, pd))) return -1;
    return self->format_size;
}

uint64_t xx_uefi_capsule_get_number_of_archive_records(Abstractformat *self,
                                                       xx_pd_struct *pd) {
    if (!self || (!self->base_info_handled &&
                  !xx_format_handle_base_info(self, pd))) return 0U;
    return ((xx_uefi_capsule *)self)->number_of_records;
}

xx_archive_record_state *xx_uefi_capsule_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_record_state *state;
    xx_uefi_capsule_archive_stream *stream;
    if (!self || !self->device ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    stream = (xx_uefi_capsule_archive_stream *)xx_mem_calloc(1U,
                                                             sizeof(*stream));
    if (!state || !stream) {
        if (state) xx_mem_free(state);
        if (stream) xx_mem_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    if (!xx_uefi_capsule_copy_options(&state->options, options) ||
        !xx_uefi_capsule_parse(self, &stream->parsed, pd)) {
        xx_uefi_capsule_archive_stream_free(stream);
        xx_archive_record_state_free(state);
        return NULL;
    }
    stream->index = 0U;
    state->internal_state = stream;
    state->free_internal = xx_uefi_capsule_archive_stream_free;
    state->total_records = (int64_t)stream->parsed.count;
    if (stream->parsed.count != 0U &&
        xx_uefi_capsule_populate_record(&state->current_record,
                                        &stream->parsed.entries[0])) {
        state->has_record = true;
        state->current_index = 0;
    }
    return state;
}

const xx_archive_record *xx_uefi_capsule_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record : NULL;
}

bool xx_uefi_capsule_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    xx_uefi_capsule_archive_stream *stream;
    if (!self || !state || state->format != self || !state->has_record ||
        !state->internal_state || (pd && xx_pd_is_stopped(pd))) return false;
    stream = (xx_uefi_capsule_archive_stream *)state->internal_state;
    ++stream->index;
    if (stream->index >= stream->parsed.count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    if (!xx_uefi_capsule_populate_record(
            &state->current_record, &stream->parsed.entries[stream->index])) {
        state->has_record = false;
        return false;
    }
    ++state->current_index;
    return true;
}

bool xx_uefi_capsule_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    const xx_archive_record *record;
    const xx_var *option;
    const char *name;
    const char *base = NULL;
    char *owned_base = NULL;
    char *destination = NULL;
    bool folder;
    bool result = false;
    if (!self || !self->device || !state || state->format != self ||
        !state->has_record || (pd && xx_pd_is_stopped(pd))) return false;
    record = &state->current_record;
    name = xx_archive_record_get_original_name(record);
    if (!xx_uefi_capsule_safe_name(name)) return false;
    option = xx_uefi_capsule_find_option(&state->options,
                                         XX_META_ID_OPT_UNPACK_PATH);
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
        destination = xx_str_concat(base, "/");
        if (!destination) goto cleanup;
        {
            char *joined = xx_str_concat(destination, name);
            xx_str_free(destination);
            destination = joined;
        }
    } else {
        destination = xx_str_concat(base, name);
    }
    if (!destination) goto cleanup;
    folder = xx_archive_record_get_meta_bool(record, XX_META_ID_IS_FOLDER,
                                             false);
    if (folder) {
        result = xx_store_create_dirs_a(destination, true);
    } else if (xx_store_create_dirs_a(destination, false)) {
        result = xx_store_unpack_device_to_file(self->device,
                                                record->data_offset,
                                                record->compressed_size,
                                                destination, pd);
        if (!result) xx_rt_remove(destination);
    }
cleanup:
    if (owned_base) xx_str_free(owned_base);
    if (destination) xx_str_free(destination);
    return result;
}

void xx_uefi_capsule_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

uint64_t xx_uefi_capsule_get_number_of_records(
    const xx_uefi_capsule *capsule) {
    return capsule ? capsule->number_of_records : 0U;
}
uint64_t xx_uefi_capsule_get_number_of_members(
    const xx_uefi_capsule *capsule) {
    return capsule ? capsule->number_of_members : 0U;
}
uint32_t xx_uefi_capsule_get_header_size(const xx_uefi_capsule *capsule) {
    return capsule ? capsule->header_size : 0U;
}
uint32_t xx_uefi_capsule_get_flags(const xx_uefi_capsule *capsule) {
    return capsule ? capsule->flags : 0U;
}
uint32_t xx_uefi_capsule_get_image_size(const xx_uefi_capsule *capsule) {
    return capsule ? capsule->capsule_image_size : 0U;
}
bool xx_uefi_capsule_is_firmware_management(const xx_uefi_capsule *capsule) {
    return capsule ? capsule->is_firmware_management : false;
}
int64_t xx_uefi_capsule_get_archive_end(const xx_uefi_capsule *capsule) {
    return capsule ? capsule->archive_end : -1;
}
