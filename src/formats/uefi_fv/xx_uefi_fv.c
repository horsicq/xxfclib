/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* UEFI PI firmware volume reader.
 *
 * Implemented: the EFI_FIRMWARE_VOLUME_HEADER including its extended header
 * and its 16-bit header checksum, the FFS file walk (both the 24-bit
 * EFI_FFS_FILE_HEADER and the EFI_FFS_FILE_HEADER2 large-file form), the
 * section walk (both EFI_COMMON_SECTION_HEADER and the extended
 * EFI_COMMON_SECTION_HEADER2 form), recursion into
 * EFI_SECTION_FIRMWARE_VOLUME_IMAGE and into the section streams of an
 * uncompressed EFI_SECTION_COMPRESSION or a GUID-defined section that does
 * not require processing, and LZMA extraction of GUID-defined sections
 * carrying the LZMA_CUSTOM_DECOMPRESS GUID.
 *
 * NOT implemented, and therefore published as unsupported records rather
 * than silently dropped: the EFI/Tiano custom compression used by
 * EFI_SECTION_COMPRESSION type 1 and by TIANO_CUSTOM_DECOMPRESS, the
 * Brotli and LZMAF86 (LZMA plus an x86 branch filter) custom decompressors,
 * and any other GUID-defined section whose PROCESSING_REQUIRED attribute is
 * set. No codec for those exists in this library and this reader does not
 * write one. The FFS IntegrityCheck field and the FFS State byte (and with
 * it the erase-polarity rules for marked-for-update and deleted files) are
 * read but not acted upon: a file is published whatever its state says.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/uefi_fv/xx_uefi_fv.h"

#include "xxfclib/algo/lzma/xx_lzma.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

/* Registration placeholder. xxfc_defs.h is shared and is not edited from
 * here, so the file-type constant is supplied locally until the enumerator
 * lands. Delete this block once XX_FILE_TYPE_UEFI_FV exists in the enum. */
#ifdef UEFI_FV
#define XX_UEFI_FV_FILE_TYPE XX_FILE_TYPE_UEFI_FV
#else
#define XX_UEFI_FV_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define XX_UEFI_FV_SIGNATURE UINT32_C(0x4856465F) /* "_FVH" */
#define XX_UEFI_FV_SIGNATURE_OFFSET 40U
#define XX_UEFI_FV_HEADER_MIN 56U
#define XX_UEFI_FV_HEADER_MAX 4096U
#define XX_UEFI_FV_EXT_HEADER_SIZE 20U
#define XX_UEFI_FV_FFS_HEADER_SIZE 24U
#define XX_UEFI_FV_FFS_HEADER2_SIZE 32U
#define XX_UEFI_FV_SECTION_HEADER_SIZE 4U
#define XX_UEFI_FV_SECTION_HEADER2_SIZE 8U
#define XX_UEFI_FV_GUID_SIZE 16U

#define XX_UEFI_FV_FILE_ALIGNMENT 8
#define XX_UEFI_FV_SECTION_ALIGNMENT 4

/* Budgets. Every one of them ends a walk rather than the whole parse, so an
 * image that trips a budget still yields the records found before it. */
#define XX_UEFI_FV_MAX_ENTRIES 200000U
#define XX_UEFI_FV_MAX_FILES 50000U
#define XX_UEFI_FV_MAX_SECTIONS 8192U
#define XX_UEFI_FV_MAX_VOLUMES 1024U
#define XX_UEFI_FV_MAX_DEPTH 12U
#define XX_UEFI_FV_MAX_NAME 512U

/* Attributes bit that turns EFI_FFS_FILE_HEADER into EFI_FFS_FILE_HEADER2. */
#define XX_UEFI_FV_FFS_ATTRIB_LARGE_FILE 0x01U

/* FFS file types this reader names; anything else is reported by number. */
#define XX_UEFI_FV_FILETYPE_RAW 0x01U
#define XX_UEFI_FV_FILETYPE_FFS_PAD 0xF0U

/* Section types, from the PI specification's EFI_SECTION_* set. */
#define XX_UEFI_FV_SECTION_COMPRESSION 0x01U
#define XX_UEFI_FV_SECTION_GUID_DEFINED 0x02U
#define XX_UEFI_FV_SECTION_USER_INTERFACE 0x15U
#define XX_UEFI_FV_SECTION_FIRMWARE_VOLUME_IMAGE 0x17U

/* EFI_GUIDED_SECTION_PROCESSING_REQUIRED. When clear the payload is plain. */
#define XX_UEFI_FV_GUIDED_PROCESSING_REQUIRED 0x01U

/* EFI_SECTION_COMPRESSION CompressionType values. */
#define XX_UEFI_FV_COMPRESSION_NONE 0x00U
#define XX_UEFI_FV_COMPRESSION_STANDARD 0x01U

typedef struct xx_uefi_fv_entry_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;   /**< First payload byte on the device. */
    int64_t data_size;     /**< Payload bytes on the device. */
    int64_t unpacked_size; /**< Expanded size, or data_size when stored. */
    int64_t codec_offset;  /**< Codec input start, past any codec preamble. */
    int64_t codec_size;    /**< Codec input length. */
    uint8_t props[5];      /**< LZMA property block, when method is LZMA. */
    uint32_t method;       /**< One of XX_UEFI_FV_METHOD_*. */
    bool is_folder;
} xx_uefi_fv_entry;

/* Volume offsets already entered, so that a section claiming to hold the
 * volume that contains it cannot be followed round for ever. The count is
 * small - it is capped by XX_UEFI_FV_MAX_DEPTH - so a linear scan is used. */
typedef struct xx_uefi_fv_chain_s {
    int64_t offsets[XX_UEFI_FV_MAX_DEPTH + 1U];
    unsigned count;
} xx_uefi_fv_chain;

typedef struct xx_uefi_fv_private_s {
    xx_uefi_fv_entry *entries;
    size_t count;
    size_t capacity;
    xx_uefi_fv_chain chain;
    size_t volumes;
    size_t files;
    int64_t input_size;
    int64_t archive_end;
    uint64_t fv_length;
    uint16_t header_length;
    uint16_t checksum;
    uint8_t revision;
} xx_uefi_fv_private;

typedef struct xx_uefi_fv_archive_stream_s {
    xx_uefi_fv_private parsed;
    size_t index;
} xx_uefi_fv_archive_stream;

/* A bounded name builder. Records are named by a path that is assembled from
 * several pieces, and the CRT string functions are unavailable, so the pieces
 * are appended into a fixed buffer that refuses to overflow. */
typedef struct xx_uefi_fv_name_s {
    char data[XX_UEFI_FV_MAX_NAME];
    size_t used;
    bool overflow;
} xx_uefi_fv_name;

/* Identifiers for the GUIDs this reader has to act on rather than merely
 * name, so that the decision is made on the GUID bytes and never on the
 * printable name next to them. */
#define XX_UEFI_FV_GUID_OTHER 0U
#define XX_UEFI_FV_GUID_LZMA 1U
#define XX_UEFI_FV_GUID_TIANO 2U

typedef struct xx_uefi_fv_known_guid_s {
    uint8_t guid[XX_UEFI_FV_GUID_SIZE];
    const char *name;
    uint32_t id;
} xx_uefi_fv_known_guid;

/* The GUIDs are stored in their on-disk mixed-endian layout: the first three
 * fields are little endian, the last eight bytes are in order. */
static const xx_uefi_fv_known_guid xx_uefi_fv_known_guids[] = {
    {{0x98, 0x58, 0x4E, 0xEE, 0x14, 0x39, 0x59, 0x42, 0x9D, 0x6E, 0xDC, 0x7B,
      0xD7, 0x94, 0x03, 0xCF},
     "LZMA_CUSTOM_DECOMPRESS", XX_UEFI_FV_GUID_LZMA},
    {{0xBD, 0xE6, 0x2A, 0xD4, 0x52, 0x13, 0xFB, 0x4B, 0x90, 0x9A, 0xCA, 0x72,
      0xA6, 0xEA, 0xE8, 0x89},
     "LZMAF86_CUSTOM_DECOMPRESS", XX_UEFI_FV_GUID_OTHER},
    {{0x50, 0x20, 0x53, 0x3D, 0xDA, 0x5C, 0xD0, 0x4F, 0x87, 0x9E, 0x0F, 0x7F,
      0x63, 0x0D, 0x5A, 0xFB},
     "BROTLI_CUSTOM_DECOMPRESS", XX_UEFI_FV_GUID_OTHER},
    {{0xAD, 0x80, 0x12, 0xA3, 0x1E, 0x48, 0xB6, 0x41, 0x95, 0xE8, 0x12, 0x7F,
      0x4C, 0x98, 0x47, 0x79},
     "TIANO_CUSTOM_DECOMPRESS", XX_UEFI_FV_GUID_TIANO},
    {{0xB0, 0xCD, 0x1B, 0xFC, 0x31, 0x7D, 0xAA, 0x49, 0x93, 0x6A, 0xA4, 0x60,
      0x0D, 0x9D, 0xD0, 0x83},
     "CRC32_GUIDED_SECTION", XX_UEFI_FV_GUID_OTHER},
    {{0x78, 0xE5, 0x8C, 0x8C, 0x3D, 0x8A, 0x1C, 0x4F, 0x99, 0x35, 0x89, 0x61,
      0x85, 0xC3, 0x2D, 0xD3},
     "FFS2", XX_UEFI_FV_GUID_OTHER},
    {{0x7A, 0xC0, 0x73, 0x54, 0xCB, 0x3D, 0xCA, 0x4D, 0xBD, 0x6F, 0x1E, 0x96,
      0x89, 0xE7, 0x34, 0x9A},
     "FFS3", XX_UEFI_FV_GUID_OTHER},
    {{0xD9, 0x54, 0x93, 0x7A, 0x68, 0x04, 0x4A, 0x44, 0x81, 0xCE, 0x0B, 0xF6,
      0x17, 0xD8, 0x90, 0xDF},
     "SYSTEM_NV_DATA_FV", XX_UEFI_FV_GUID_OTHER},
    {{0x2E, 0x06, 0xA0, 0x1B, 0x79, 0xC7, 0x82, 0x45, 0x85, 0x66, 0x33, 0x6A,
      0xE8, 0xF7, 0x8F, 0x09},
     "VOLUME_TOP_FILE", XX_UEFI_FV_GUID_OTHER},
    {{0xE7, 0x0E, 0x51, 0xFC, 0xDC, 0xFF, 0xD4, 0x11, 0xBD, 0x41, 0x00, 0x80,
      0xC7, 0x3C, 0x88, 0x81},
     "DXE_APRIORI", XX_UEFI_FV_GUID_OTHER},
    {{0x0A, 0xCC, 0x45, 0x1B, 0x6A, 0x15, 0x8A, 0x42, 0xAF, 0x62, 0x49, 0x86,
      0x4D, 0xA0, 0xE6, 0xE6},
     "PEI_APRIORI", XX_UEFI_FV_GUID_OTHER}};

static void xx_uefi_fv_vtable_destroy(Abstractformat *self);
static bool xx_uefi_fv_walk_volume(Abstractformat *self,
                                   xx_uefi_fv_private *parsed, int64_t offset,
                                   int64_t limit, const xx_uefi_fv_name *prefix,
                                   unsigned depth, xx_pd_struct *pd);
static void xx_uefi_fv_walk_sections(Abstractformat *self,
                                     xx_uefi_fv_private *parsed, int64_t start,
                                     int64_t end, const xx_uefi_fv_name *prefix,
                                     unsigned depth, xx_pd_struct *pd);

/* ---------------------------------------------------------------- helpers */

static bool xx_uefi_fv_read_at(xx_io_device *device, int64_t offset, void *data,
                               size_t size) {
    uint8_t *out = (uint8_t *)data;
    size_t done = 0U;
    /* xx_io_seek() takes a long, and a long is 32 bits on Win64 while a
     * firmware image can easily pass 2 GiB, so the 64-bit seek is the only
     * correct one here. */
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
static bool xx_uefi_fv_range_within(int64_t total_size, int64_t offset,
                                    int64_t size) {
    return (total_size >= 0) && (offset >= 0) && (size >= 0) &&
           (offset <= total_size) && (size <= total_size - offset);
}

/* Round value up to the next alignment boundary measured from base, refusing
 * to overflow. The alignment is always a power of two.
 *
 * The specification aligns an FFS file to eight bytes from the start of its
 * volume and a section to four bytes from the start of its section stream,
 * not from the start of the device - and a volume nested inside a section
 * begins at an offset that need not be eight-byte aligned itself, so
 * aligning the absolute offset would walk straight past the first file. */
static bool xx_uefi_fv_align(int64_t value, int64_t base, int64_t alignment,
                             int64_t *result) {
    int64_t relative;
    if (!result || value < base || base < 0 || alignment <= 0) return false;
    relative = value - base;
    if (relative > INT64_MAX - (alignment - 1)) return false;
    relative = (relative + (alignment - 1)) & ~(alignment - 1);
    if (relative > INT64_MAX - base) return false;
    *result = base + relative;
    return true;
}

/* Read a 24-bit little-endian size out of a three-byte field. */
static uint32_t xx_uefi_fv_get_u24(const uint8_t *data, size_t data_size,
                                   size_t offset) {
    return (uint32_t)xx_data_get_u8(data, data_size, offset) |
           ((uint32_t)xx_data_get_u8(data, data_size, offset + 1U) << 8U) |
           ((uint32_t)xx_data_get_u8(data, data_size, offset + 2U) << 16U);
}

static bool xx_uefi_fv_all_bytes(const uint8_t *data, size_t size,
                                 uint8_t value) {
    size_t index;
    for (index = 0U; index < size; ++index) {
        if (data[index] != value) return false;
    }
    return true;
}

static void xx_uefi_fv_name_reset(xx_uefi_fv_name *name) {
    if (!name) return;
    name->used = 0U;
    name->overflow = false;
    name->data[0] = '\0';
}

static void xx_uefi_fv_name_add_char(xx_uefi_fv_name *name, char ch) {
    if (!name || name->overflow) return;
    if (name->used + 1U >= sizeof(name->data)) {
        name->overflow = true;
        return;
    }
    name->data[name->used++] = ch;
    name->data[name->used] = '\0';
}

static void xx_uefi_fv_name_add(xx_uefi_fv_name *name, const char *text) {
    size_t index;
    if (!name || !text) return;
    for (index = 0U; text[index] != '\0'; ++index) {
        xx_uefi_fv_name_add_char(name, text[index]);
    }
}

/* Append a decimal number, zero padded to at least two digits so that the
 * record names of one container sort in walk order. */
static void xx_uefi_fv_name_add_index(xx_uefi_fv_name *name, uint32_t value) {
    char digits[12];
    size_t used = 0U;
    if (!name) return;
    do {
        digits[used++] = (char)('0' + (value % 10U));
        value /= 10U;
    } while (value != 0U && used < sizeof(digits));
    if (used < 2U) xx_uefi_fv_name_add_char(name, '0');
    while (used != 0U) xx_uefi_fv_name_add_char(name, digits[--used]);
}

static void xx_uefi_fv_name_add_hex8(xx_uefi_fv_name *name, uint8_t value) {
    static const char digits[] = "0123456789abcdef";
    xx_uefi_fv_name_add_char(name, digits[(value >> 4U) & 0x0FU]);
    xx_uefi_fv_name_add_char(name, digits[value & 0x0FU]);
}

/* Render a GUID in its canonical text form. The first three fields are
 * little endian on disk, the trailing eight bytes are printed in order. */
static void xx_uefi_fv_name_add_guid(xx_uefi_fv_name *name,
                                     const uint8_t *guid) {
    static const int order[XX_UEFI_FV_GUID_SIZE] = {3,  2,  1, 0, 5, 4, 7, 6,
                                                    8,  9,  10, 11, 12, 13,
                                                    14, 15};
    size_t index;
    if (!name || !guid) return;
    for (index = 0U; index < XX_UEFI_FV_GUID_SIZE; ++index) {
        if (index == 4U || index == 6U || index == 8U || index == 10U) {
            xx_uefi_fv_name_add_char(name, '-');
        }
        xx_uefi_fv_name_add_hex8(name, guid[order[index]]);
    }
}

/* Append free-form text, mapping everything outside a conservative portable
 * set onto '_' so that a record name stays usable as a relative path. */
static void xx_uefi_fv_name_add_sanitised(xx_uefi_fv_name *name,
                                          const char *text, size_t length) {
    size_t index;
    if (!name || !text) return;
    for (index = 0U; index < length; ++index) {
        unsigned char ch = (unsigned char)text[index];
        bool plain = (ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z') ||
                     (ch >= 'a' && ch <= 'z') || ch == '.' || ch == '-' ||
                     ch == '_';
        xx_uefi_fv_name_add_char(name, plain ? (char)ch : '_');
    }
}

static char *xx_uefi_fv_name_dup(const xx_uefi_fv_name *name) {
    char *copy;
    if (!name || name->overflow || name->used == 0U) return NULL;
    copy = (char *)xx_mem_alloc(name->used + 1U);
    if (!copy) return NULL;
    xx_mem_copy(copy, name->data, name->used);
    copy[name->used] = '\0';
    return copy;
}

static const xx_uefi_fv_known_guid *xx_uefi_fv_lookup_guid(
    const uint8_t *guid) {
    size_t index;
    if (!guid) return NULL;
    for (index = 0U;
         index < sizeof(xx_uefi_fv_known_guids) /
                     sizeof(xx_uefi_fv_known_guids[0]);
         ++index) {
        if (xx_rt_memcmp(xx_uefi_fv_known_guids[index].guid, guid,
                         XX_UEFI_FV_GUID_SIZE) == 0) {
            return &xx_uefi_fv_known_guids[index];
        }
    }
    return NULL;
}

static const char *xx_uefi_fv_known_guid_name(const uint8_t *guid) {
    const xx_uefi_fv_known_guid *known = xx_uefi_fv_lookup_guid(guid);
    return known ? known->name : NULL;
}

static const char *xx_uefi_fv_file_type_name(uint8_t type) {
    switch (type) {
        case 0x01U: return "RAW";
        case 0x02U: return "FREEFORM";
        case 0x03U: return "SECURITY_CORE";
        case 0x04U: return "PEI_CORE";
        case 0x05U: return "DXE_CORE";
        case 0x06U: return "PEIM";
        case 0x07U: return "DRIVER";
        case 0x08U: return "COMBINED_PEIM_DRIVER";
        case 0x09U: return "APPLICATION";
        case 0x0AU: return "MM";
        case 0x0BU: return "FIRMWARE_VOLUME_IMAGE";
        case 0x0CU: return "COMBINED_MM_DXE";
        case 0x0DU: return "MM_CORE";
        case 0x0EU: return "MM_STANDALONE";
        case 0x0FU: return "MM_CORE_STANDALONE";
        case 0xF0U: return "FFS_PAD";
        default: return "TYPE";
    }
}

static const char *xx_uefi_fv_section_type_name(uint8_t type) {
    switch (type) {
        case 0x01U: return "COMPRESSION";
        case 0x02U: return "GUID_DEFINED";
        case 0x03U: return "DISPOSABLE";
        case 0x10U: return "PE32";
        case 0x11U: return "PIC";
        case 0x12U: return "TE";
        case 0x13U: return "DXE_DEPEX";
        case 0x14U: return "VERSION";
        case 0x15U: return "USER_INTERFACE";
        case 0x16U: return "COMPATIBILITY16";
        case 0x17U: return "FIRMWARE_VOLUME_IMAGE";
        case 0x18U: return "FREEFORM_SUBTYPE_GUID";
        case 0x19U: return "RAW";
        case 0x1BU: return "PEI_DEPEX";
        case 0x1CU: return "MM_DEPEX";
        default: return "SECTION";
    }
}

/* ------------------------------------------------------------- collection */

static void xx_uefi_fv_private_cleanup(xx_uefi_fv_private *parsed) {
    size_t index;
    if (!parsed) return;
    for (index = 0U; index < parsed->count; ++index) {
        if (parsed->entries[index].name) xx_str_free(parsed->entries[index].name);
    }
    if (parsed->entries) xx_mem_free(parsed->entries);
    xx_mem_zero(parsed, sizeof(*parsed));
    parsed->input_size = -1;
    parsed->archive_end = -1;
}

static bool xx_uefi_fv_append_entry(xx_uefi_fv_private *parsed,
                                    xx_uefi_fv_entry *entry) {
    xx_uefi_fv_entry *grown;
    size_t capacity;
    if (!parsed || !entry || !entry->name ||
        parsed->count >= XX_UEFI_FV_MAX_ENTRIES) {
        return false;
    }
    if (parsed->count == parsed->capacity) {
        capacity = parsed->capacity ? parsed->capacity * 2U : 32U;
        if (capacity < parsed->count ||
            capacity > SIZE_MAX / sizeof(*parsed->entries)) return false;
        grown = (xx_uefi_fv_entry *)xx_mem_realloc(
            parsed->entries, capacity * sizeof(*parsed->entries));
        if (!grown) return false;
        parsed->entries = grown;
        parsed->capacity = capacity;
    }
    parsed->entries[parsed->count++] = *entry;
    xx_mem_zero(entry, sizeof(*entry));
    return true;
}

/* Publish one record. The name is consumed either way, so a caller that has
 * built a name can hand it over and forget about it. */
static bool xx_uefi_fv_publish(xx_uefi_fv_private *parsed, char *name,
                               int64_t header_offset, int64_t header_size,
                               int64_t data_offset, int64_t data_size,
                               uint32_t method, bool is_folder) {
    xx_uefi_fv_entry entry;
    if (!name) return false;
    xx_mem_zero(&entry, sizeof(entry));
    entry.name = name;
    entry.header_offset = header_offset;
    entry.header_size = header_size;
    entry.data_offset = data_offset;
    entry.data_size = data_size;
    entry.unpacked_size = data_size;
    entry.codec_offset = data_offset;
    entry.codec_size = data_size;
    entry.method = method;
    entry.is_folder = is_folder;
    if (!xx_uefi_fv_append_entry(parsed, &entry)) {
        xx_str_free(name);
        return false;
    }
    return true;
}

/* -------------------------------------------------------------- the walks */

/* Decode the GUID-defined section at [start, end) whose payload begins at
 * payload. Returns true when the entry was filled in and should be published
 * as a codec record, false when the caller should keep walking the payload
 * as a plain section stream instead. */
static bool xx_uefi_fv_guided_entry(Abstractformat *self,
                                    xx_uefi_fv_entry *entry,
                                    const uint8_t *guid, uint16_t attributes,
                                    int64_t payload, int64_t payload_size) {
    const xx_uefi_fv_known_guid *known = xx_uefi_fv_lookup_guid(guid);
    uint32_t id = known ? known->id : XX_UEFI_FV_GUID_OTHER;
    entry->method = XX_UEFI_FV_METHOD_UNKNOWN;
    entry->data_offset = payload;
    entry->data_size = payload_size;
    entry->unpacked_size = payload_size;
    entry->codec_offset = payload;
    entry->codec_size = payload_size;
    if (id == XX_UEFI_FV_GUID_LZMA) {
        /* EDK2 writes an LZMA-Alone preamble: five property bytes and a
         * little-endian u64 expanded size. The size is 0xFFFF... when the
         * writer did not know it, which the codec takes as -1. */
        uint8_t preamble[13];
        uint64_t expanded;
        if (payload_size < (int64_t)sizeof(preamble) ||
            !xx_uefi_fv_read_at(self->device, payload, preamble,
                                sizeof(preamble))) {
            return true;
        }
        expanded = xx_data_get_u64(preamble, sizeof(preamble), 5U, false);
        xx_mem_copy(entry->props, preamble, sizeof(entry->props));
        entry->method = XX_UEFI_FV_METHOD_LZMA;
        entry->codec_offset = payload + (int64_t)sizeof(preamble);
        entry->codec_size = payload_size - (int64_t)sizeof(preamble);
        entry->unpacked_size =
            (expanded <= (uint64_t)INT64_MAX) ? (int64_t)expanded : -1;
        return true;
    }
    if (id == XX_UEFI_FV_GUID_TIANO) {
        entry->method = XX_UEFI_FV_METHOD_TIANO;
        return true;
    }
    if ((attributes & XX_UEFI_FV_GUIDED_PROCESSING_REQUIRED) == 0U) {
        /* No processing required: the payload is a plain section stream. */
        return false;
    }
    return true;
}

/* Walk the section stream in [start, end). Every failure ends this stream
 * only, so a file with one damaged section still yields the sections before
 * it. Sections are four-byte aligned and the walk refuses a step that would
 * not move forward, which is what keeps a zero size from spinning. */
static void xx_uefi_fv_walk_sections(Abstractformat *self,
                                     xx_uefi_fv_private *parsed, int64_t start,
                                     int64_t end, const xx_uefi_fv_name *prefix,
                                     unsigned depth, xx_pd_struct *pd) {
    int64_t offset = start;
    uint32_t index;
    if (!self || !parsed || !prefix || depth > XX_UEFI_FV_MAX_DEPTH) return;
    for (index = 0U; index < XX_UEFI_FV_MAX_SECTIONS; ++index) {
        uint8_t header[XX_UEFI_FV_SECTION_HEADER2_SIZE];
        uint32_t raw_size;
        int64_t size;
        int64_t header_size = XX_UEFI_FV_SECTION_HEADER_SIZE;
        int64_t body;
        int64_t body_size;
        int64_t next;
        uint8_t type;
        xx_uefi_fv_name name;
        xx_uefi_fv_entry entry;
        bool publish_plain = true;

        if (pd && xx_pd_is_stopped(pd)) return;
        if (parsed->count >= XX_UEFI_FV_MAX_ENTRIES) return;
        if (end - offset < XX_UEFI_FV_SECTION_HEADER_SIZE) return;
        if (!xx_uefi_fv_read_at(self->device, offset, header,
                                XX_UEFI_FV_SECTION_HEADER_SIZE)) {
            return;
        }
        raw_size = xx_uefi_fv_get_u24(header, XX_UEFI_FV_SECTION_HEADER_SIZE,
                                      0U);
        type = xx_data_get_u8(header, XX_UEFI_FV_SECTION_HEADER_SIZE, 3U);
        if (raw_size == 0x00FFFFFFU) {
            /* EFI_COMMON_SECTION_HEADER2: the real size is a u32 at +4. */
            if (end - offset < XX_UEFI_FV_SECTION_HEADER2_SIZE ||
                !xx_uefi_fv_read_at(self->device, offset, header,
                                    XX_UEFI_FV_SECTION_HEADER2_SIZE)) {
                return;
            }
            size = (int64_t)xx_data_get_u32(header,
                                            XX_UEFI_FV_SECTION_HEADER2_SIZE, 4U,
                                            false);
            header_size = XX_UEFI_FV_SECTION_HEADER2_SIZE;
        } else {
            size = (int64_t)raw_size;
        }
        /* A size below the header, or past the end of the stream, means the
         * stream cannot be trusted any further. */
        if (size < header_size || size > end - offset) return;
        body = offset + header_size;
        body_size = size - header_size;

        name = *prefix;
        xx_uefi_fv_name_add(&name, "/s");
        xx_uefi_fv_name_add_index(&name, index);
        xx_uefi_fv_name_add_char(&name, '_');
        xx_uefi_fv_name_add(&name, xx_uefi_fv_section_type_name(type));
        if (xx_rt_memcmp(xx_uefi_fv_section_type_name(type), "SECTION", 7U) ==
            0) {
            xx_uefi_fv_name_add_char(&name, '_');
            xx_uefi_fv_name_add_hex8(&name, type);
        }

        xx_mem_zero(&entry, sizeof(entry));
        entry.header_offset = offset;
        entry.header_size = header_size;
        entry.data_offset = body;
        entry.data_size = body_size;
        entry.unpacked_size = body_size;
        entry.codec_offset = body;
        entry.codec_size = body_size;
        entry.method = XX_UEFI_FV_METHOD_STORE;

        if (type == XX_UEFI_FV_SECTION_GUID_DEFINED) {
            uint8_t guided[XX_UEFI_FV_GUID_SIZE + 4U];
            uint16_t data_offset;
            uint16_t attributes;
            const char *known;
            if (body_size < (int64_t)sizeof(guided) ||
                !xx_uefi_fv_read_at(self->device, body, guided,
                                    sizeof(guided))) {
                return;
            }
            data_offset = xx_data_get_u16(guided, sizeof(guided),
                                          XX_UEFI_FV_GUID_SIZE, false);
            attributes = xx_data_get_u16(guided, sizeof(guided),
                                         XX_UEFI_FV_GUID_SIZE + 2U, false);
            /* DataOffset counts from the start of the section header. */
            if ((int64_t)data_offset < header_size + (int64_t)sizeof(guided) ||
                (int64_t)data_offset > size) {
                return;
            }
            known = xx_uefi_fv_known_guid_name(guided);
            xx_uefi_fv_name_add_char(&name, '_');
            if (known) {
                xx_uefi_fv_name_add(&name, known);
            } else {
                xx_uefi_fv_name_add_guid(&name, guided);
            }
            publish_plain = xx_uefi_fv_guided_entry(
                self, &entry, guided, attributes, offset + data_offset,
                size - (int64_t)data_offset);
            if (!publish_plain) {
                xx_uefi_fv_walk_sections(self, parsed, offset + data_offset,
                                         offset + size, &name, depth + 1U, pd);
            }
        } else if (type == XX_UEFI_FV_SECTION_COMPRESSION) {
            uint8_t compression[5];
            uint8_t compression_type;
            if (body_size < (int64_t)sizeof(compression) ||
                !xx_uefi_fv_read_at(self->device, body, compression,
                                    sizeof(compression))) {
                return;
            }
            entry.unpacked_size = (int64_t)xx_data_get_u32(
                compression, sizeof(compression), 0U, false);
            compression_type =
                xx_data_get_u8(compression, sizeof(compression), 4U);
            entry.data_offset = body + (int64_t)sizeof(compression);
            entry.data_size = body_size - (int64_t)sizeof(compression);
            entry.codec_offset = entry.data_offset;
            entry.codec_size = entry.data_size;
            if (compression_type == XX_UEFI_FV_COMPRESSION_NONE) {
                /* Not compressed after all: the payload is a plain section
                 * stream and is walked rather than published whole. */
                entry.unpacked_size = entry.data_size;
                publish_plain = false;
                xx_uefi_fv_walk_sections(self, parsed, entry.data_offset,
                                         offset + size, &name, depth + 1U, pd);
            } else if (compression_type == XX_UEFI_FV_COMPRESSION_STANDARD) {
                /* EFI/Tiano compression. No codec for it exists here, so the
                 * section is published as an unsupported record. */
                entry.method = XX_UEFI_FV_METHOD_TIANO;
            } else {
                entry.method = XX_UEFI_FV_METHOD_UNKNOWN;
            }
        } else if (type == XX_UEFI_FV_SECTION_FIRMWARE_VOLUME_IMAGE) {
            /* A whole volume lives in the payload. It is published as a
             * record in its own right and then walked. */
            xx_uefi_fv_name leaf = name;
            xx_uefi_fv_name_add(&leaf, ".sec");
            if (xx_uefi_fv_publish(parsed, xx_uefi_fv_name_dup(&leaf), offset,
                                   header_size, body, body_size,
                                   XX_UEFI_FV_METHOD_STORE, false)) {
                /* The nested volume gets a name of its own below the
                 * section's, so that it does not collide with the record
                 * just published for the section itself. */
                xx_uefi_fv_name nested = name;
                xx_uefi_fv_name_add(&nested, "/fv");
                (void)xx_uefi_fv_walk_volume(self, parsed, body,
                                             body + body_size, &nested,
                                             depth + 1U, pd);
            }
            publish_plain = false;
        } else if (type == XX_UEFI_FV_SECTION_USER_INTERFACE) {
            /* The payload is a NUL-terminated UCS-2 name. Only the low byte
             * of each unit is taken, which covers the ASCII names EDK2
             * actually writes and degrades the rest to '_'. */
            uint8_t text[128];
            size_t available =
                (body_size > 0 && body_size < (int64_t)sizeof(text) * 2)
                    ? (size_t)body_size
                    : sizeof(text) * 2U;
            size_t units = available / 2U;
            size_t unit;
            char decoded[128];
            if (units > sizeof(decoded)) units = sizeof(decoded);
            if (units != 0U &&
                xx_uefi_fv_read_at(self->device, body, text, units * 2U)) {
                for (unit = 0U; unit < units; ++unit) {
                    if (text[unit * 2U] == 0U && text[unit * 2U + 1U] == 0U) {
                        break;
                    }
                    decoded[unit] = (char)text[unit * 2U];
                }
                if (unit != 0U) {
                    xx_uefi_fv_name_add_char(&name, '_');
                    xx_uefi_fv_name_add_sanitised(&name, decoded, unit);
                }
            }
        }

        if (publish_plain) {
            /* The record name carries an extension while the name without it
             * is the directory the section's own children go in, so that a
             * container's bytes and its children never claim the same path
             * when a caller extracts the lot. */
            xx_uefi_fv_name leaf = name;
            xx_uefi_fv_name_add(&leaf, ".sec");
            entry.name = xx_uefi_fv_name_dup(&leaf);
            if (!entry.name || !xx_uefi_fv_append_entry(parsed, &entry)) {
                if (entry.name) xx_str_free(entry.name);
                return;
            }
        }

        if (!xx_uefi_fv_align(offset + size, start,
                              XX_UEFI_FV_SECTION_ALIGNMENT, &next) ||
            next <= offset) {
            return;
        }
        offset = next;
    }
}

/* Walk one firmware volume. The header is validated - signature, header
 * length, the 16-bit header checksum and FvLength - before any file is
 * touched; a volume that fails validation contributes nothing. */
static bool xx_uefi_fv_walk_volume(Abstractformat *self,
                                   xx_uefi_fv_private *parsed, int64_t offset,
                                   int64_t limit, const xx_uefi_fv_name *prefix,
                                   unsigned depth, xx_pd_struct *pd) {
    uint8_t header[XX_UEFI_FV_HEADER_MAX];
    uint16_t header_length;
    uint16_t ext_header_offset;
    uint32_t sum = 0U;
    uint32_t word;
    uint64_t fv_length;
    int64_t fv_end;
    int64_t file_offset;
    uint32_t index;
    unsigned chain_index;
    xx_uefi_fv_name volume_name;

    if (!self || !self->device || !parsed || !prefix) return false;
    if (depth > XX_UEFI_FV_MAX_DEPTH) return false;
    if (parsed->volumes >= XX_UEFI_FV_MAX_VOLUMES) return false;
    /* A volume image section can name a volume already on the stack; that
     * would recurse until the depth cap, so the cycle is caught directly. */
    for (chain_index = 0U; chain_index < parsed->chain.count; ++chain_index) {
        if (parsed->chain.offsets[chain_index] == offset) return false;
    }
    if (!xx_uefi_fv_range_within(limit, offset, XX_UEFI_FV_HEADER_MIN) ||
        !xx_uefi_fv_read_at(self->device, offset, header,
                            XX_UEFI_FV_HEADER_MIN)) {
        return false;
    }
    if (xx_data_get_u32(header, XX_UEFI_FV_HEADER_MIN,
                        XX_UEFI_FV_SIGNATURE_OFFSET,
                        false) != XX_UEFI_FV_SIGNATURE) {
        return false;
    }
    fv_length = xx_data_get_u64(header, XX_UEFI_FV_HEADER_MIN, 32U, false);
    header_length = xx_data_get_u16(header, XX_UEFI_FV_HEADER_MIN, 48U, false);
    ext_header_offset =
        xx_data_get_u16(header, XX_UEFI_FV_HEADER_MIN, 52U, false);
    if (header_length < XX_UEFI_FV_HEADER_MIN ||
        header_length > XX_UEFI_FV_HEADER_MAX || (header_length & 1U) != 0U ||
        fv_length < header_length || fv_length > (uint64_t)INT64_MAX) {
        return false;
    }
    fv_end = offset + (int64_t)fv_length;
    if (fv_end > limit) return false;
    if (!xx_uefi_fv_read_at(self->device, offset, header, header_length)) {
        return false;
    }
    /* The header checksum is a 16-bit sum of the header's u16 words; a valid
     * header sums to zero. */
    for (word = 0U; word < header_length; word += 2U) {
        sum += xx_data_get_u16(header, header_length, word, false);
    }
    if ((sum & 0xFFFFU) != 0U) return false;

    if (parsed->volumes == 0U) {
        parsed->fv_length = fv_length;
        parsed->header_length = header_length;
        parsed->checksum =
            xx_data_get_u16(header, header_length, 50U, false);
        parsed->revision = xx_data_get_u8(header, header_length, 55U);
    }
    ++parsed->volumes;
    if (parsed->chain.count <
        sizeof(parsed->chain.offsets) / sizeof(parsed->chain.offsets[0])) {
        parsed->chain.offsets[parsed->chain.count++] = offset;
    }

    /* Files start after the header, or after the extended header when one is
     * present, in either case realigned to eight bytes. */
    file_offset = offset + header_length;
    if (ext_header_offset != 0U) {
        uint8_t ext[XX_UEFI_FV_EXT_HEADER_SIZE];
        uint32_t ext_size;
        int64_t ext_offset = offset + (int64_t)ext_header_offset;
        if ((int64_t)ext_header_offset < header_length ||
            !xx_uefi_fv_range_within(fv_end, ext_offset, sizeof(ext)) ||
            !xx_uefi_fv_read_at(self->device, ext_offset, ext, sizeof(ext))) {
            goto done;
        }
        ext_size = xx_data_get_u32(ext, sizeof(ext), XX_UEFI_FV_GUID_SIZE,
                                   false);
        if (ext_size < XX_UEFI_FV_EXT_HEADER_SIZE ||
            (int64_t)ext_size > fv_end - ext_offset) {
            goto done;
        }
        file_offset = ext_offset + (int64_t)ext_size;
    }
    if (!xx_uefi_fv_align(file_offset, offset, XX_UEFI_FV_FILE_ALIGNMENT,
                          &file_offset)) {
        goto done;
    }

    volume_name = *prefix;
    if (!xx_uefi_fv_publish(parsed, xx_uefi_fv_name_dup(&volume_name), offset,
                            header_length, offset, (int64_t)fv_length,
                            XX_UEFI_FV_METHOD_STORE, true)) {
        goto done;
    }

    for (index = 0U; index < XX_UEFI_FV_MAX_FILES; ++index) {
        uint8_t ffs[XX_UEFI_FV_FFS_HEADER2_SIZE];
        uint32_t raw_size;
        int64_t size;
        int64_t header_size = XX_UEFI_FV_FFS_HEADER_SIZE;
        int64_t next;
        uint8_t type;
        uint8_t attributes;
        xx_uefi_fv_name name;

        if (pd && xx_pd_is_stopped(pd)) goto done;
        if (parsed->count >= XX_UEFI_FV_MAX_ENTRIES) goto done;
        if (fv_end - file_offset < XX_UEFI_FV_FFS_HEADER_SIZE) goto done;
        if (!xx_uefi_fv_read_at(self->device, file_offset, ffs,
                                XX_UEFI_FV_FFS_HEADER_SIZE)) {
            goto done;
        }
        /* Erased space reads as all ones and marks the end of the files. */
        if (xx_uefi_fv_all_bytes(ffs, XX_UEFI_FV_FFS_HEADER_SIZE, 0xFFU)) {
            goto done;
        }
        raw_size = xx_uefi_fv_get_u24(ffs, XX_UEFI_FV_FFS_HEADER_SIZE, 20U);
        type = xx_data_get_u8(ffs, XX_UEFI_FV_FFS_HEADER_SIZE, 18U);
        attributes = xx_data_get_u8(ffs, XX_UEFI_FV_FFS_HEADER_SIZE, 19U);
        if (raw_size == 0x00FFFFFFU &&
            (attributes & XX_UEFI_FV_FFS_ATTRIB_LARGE_FILE) != 0U) {
            /* EFI_FFS_FILE_HEADER2 carries a u64 ExtendedSize at +24. */
            uint64_t extended;
            if (fv_end - file_offset < XX_UEFI_FV_FFS_HEADER2_SIZE ||
                !xx_uefi_fv_read_at(self->device, file_offset, ffs,
                                    XX_UEFI_FV_FFS_HEADER2_SIZE)) {
                goto done;
            }
            extended = xx_data_get_u64(ffs, XX_UEFI_FV_FFS_HEADER2_SIZE,
                                       XX_UEFI_FV_FFS_HEADER_SIZE, false);
            if (extended > (uint64_t)INT64_MAX) goto done;
            size = (int64_t)extended;
            header_size = XX_UEFI_FV_FFS_HEADER2_SIZE;
        } else {
            size = (int64_t)raw_size;
        }
        /* A size below the header cannot describe a file, and one past the
         * volume cannot be read; both end the walk instead of being clamped,
         * because a wrong size makes every following offset meaningless. */
        if (size < header_size || size > fv_end - file_offset) goto done;

        ++parsed->files;
        if (type != XX_UEFI_FV_FILETYPE_FFS_PAD) {
            name = *prefix;
            xx_uefi_fv_name_add(&name, "/f");
            xx_uefi_fv_name_add_index(&name, index);
            xx_uefi_fv_name_add_char(&name, '_');
            xx_uefi_fv_name_add(&name, xx_uefi_fv_file_type_name(type));
            if (xx_rt_memcmp(xx_uefi_fv_file_type_name(type), "TYPE", 4U) ==
                0) {
                xx_uefi_fv_name_add_hex8(&name, type);
            }
            xx_uefi_fv_name_add_char(&name, '_');
            {
                const char *known = xx_uefi_fv_known_guid_name(ffs);
                if (known) {
                    xx_uefi_fv_name_add(&name, known);
                    xx_uefi_fv_name_add_char(&name, '_');
                }
            }
            xx_uefi_fv_name_add_guid(&name, ffs);
            {
                /* ".ffs" for the file's own bytes; its sections live in the
                 * directory named by the unsuffixed path. */
                xx_uefi_fv_name leaf = name;
                xx_uefi_fv_name_add(&leaf, ".ffs");
                if (!xx_uefi_fv_publish(parsed, xx_uefi_fv_name_dup(&leaf),
                                        file_offset, header_size,
                                        file_offset + header_size,
                                        size - header_size,
                                        XX_UEFI_FV_METHOD_STORE, false)) {
                    goto done;
                }
            }
            /* A raw file has no section stream; anything else is tried as
             * one, and a stream that does not parse simply adds nothing. */
            if (type != XX_UEFI_FV_FILETYPE_RAW) {
                xx_uefi_fv_walk_sections(self, parsed, file_offset + header_size,
                                         file_offset + size, &name, depth + 1U,
                                         pd);
            }
        }

        if (!xx_uefi_fv_align(file_offset + size, offset,
                              XX_UEFI_FV_FILE_ALIGNMENT, &next) ||
            next <= file_offset || next > fv_end) {
            goto done;
        }
        file_offset = next;
    }
done:
    if (parsed->chain.count != 0U) --parsed->chain.count;
    return true;
}

static bool xx_uefi_fv_parse(Abstractformat *self, xx_uefi_fv_private *parsed,
                             xx_pd_struct *pd) {
    int64_t total_size;
    xx_uefi_fv_name root;
    /* Initialise before the guard clause: callers such as
     * xx_uefi_fv_check_is_valid() run the cleanup on their stack copy
     * whatever this returns. */
    if (parsed) {
        xx_mem_zero(parsed, sizeof(*parsed));
        parsed->input_size = -1;
        parsed->archive_end = -1;
    }
    if (!self || !self->device || !parsed || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd))) return false;
    total_size = xx_io_total_size(self->device);
    parsed->input_size = total_size;
    xx_uefi_fv_name_reset(&root);
    xx_uefi_fv_name_add(&root, "fv");
    if (!xx_uefi_fv_walk_volume(self, parsed, self->base_address, total_size,
                                &root, 0U, pd) ||
        parsed->count == 0U) {
        xx_uefi_fv_private_cleanup(parsed);
        return false;
    }
    parsed->archive_end = self->base_address + (int64_t)parsed->fv_length;
    return true;
}

/* --------------------------------------------------------------- plumbing */

static bool xx_uefi_fv_copy_options(xx_list_s *destination,
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

static const xx_var *xx_uefi_fv_find_option(const xx_list_s *options,
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

static bool xx_uefi_fv_populate_record(xx_archive_record *record,
                                       const xx_uefi_fv_entry *entry) {
    if (!record || !entry || !entry->name) return false;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = entry->header_offset;
    record->header_size = entry->header_size;
    record->data_offset = entry->data_offset;
    record->compressed_size = entry->data_size;
    return xx_archive_record_set_original_name(record, entry->name) &&
           xx_archive_record_set_meta_u64(
               record, XX_META_ID_UNCOMPRESSED_SIZE,
               entry->unpacked_size >= 0 ? (uint64_t)entry->unpacked_size
                                         : (uint64_t)entry->data_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)entry->data_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          entry->method) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           entry->is_folder);
}

static void xx_uefi_fv_archive_stream_free(void *pointer) {
    xx_uefi_fv_archive_stream *stream = (xx_uefi_fv_archive_stream *)pointer;
    if (!stream) return;
    xx_uefi_fv_private_cleanup(&stream->parsed);
    xx_mem_free(stream);
}

/* Extraction-time check: a record name is assembled here from indices, type
 * names, GUIDs and sanitised UI text, so it cannot normally be unsafe - but
 * it is checked anyway, because the name is used as a relative path. */
static bool xx_uefi_fv_safe_name(const char *name) {
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

/* ----------------------------------------------------------- the public API */

void xx_uefi_fv_init(xx_uefi_fv *fv, xx_io_device *dev, int64_t base_address) {
    if (!fv) return;
    xx_mem_zero(fv, sizeof(*fv));
    xx_format_init(&fv->format, dev, base_address);
    fv->format.endian = XX_ENDIAN_LITTLE;
    fv->format.file_type = XX_UEFI_FV_FILE_TYPE;
    fv->format.format_type = XX_TYPE_ARCHIVE;
    fv->format.is_archive = true;
    xx_format_set_mime_type(&fv->format, "application/x-uefi-firmware-volume");
    xx_format_set_extension(&fv->format, "fv");
    fv->format.check_is_valid = xx_uefi_fv_check_is_valid;
    fv->format.handle_base_info = xx_uefi_fv_handle_base_info;
    fv->format.get_format_size = xx_uefi_fv_get_format_size;
    fv->format.get_number_of_archive_records =
        xx_uefi_fv_get_number_of_archive_records;
    fv->format.create_archive_records_reading =
        xx_uefi_fv_create_archive_records_reading;
    fv->format.get_current_archive_record =
        xx_uefi_fv_get_current_archive_record;
    fv->format.unpack_current_archive_record =
        xx_uefi_fv_unpack_current_archive_record;
    fv->format.archive_record_move_to_next =
        xx_uefi_fv_archive_record_move_to_next;
    fv->format.free_archive_records_reading =
        xx_uefi_fv_free_archive_records_reading;
    fv->format.destroy = xx_uefi_fv_vtable_destroy;
    fv->archive_end = -1;
}

xx_uefi_fv *xx_uefi_fv_create(xx_io_device *dev, int64_t base_address) {
    xx_uefi_fv *fv = (xx_uefi_fv *)xx_mem_alloc(sizeof(*fv));
    if (fv) xx_uefi_fv_init(fv, dev, base_address);
    return fv;
}

void xx_uefi_fv_destroy(xx_uefi_fv *fv) {
    if (!fv) return;
    if (fv->internal) {
        xx_uefi_fv_private_cleanup((xx_uefi_fv_private *)fv->internal);
        xx_mem_free(fv->internal);
        fv->internal = NULL;
    }
    xx_format_cleanup_extra_parameters(&fv->format);
}

static void xx_uefi_fv_vtable_destroy(Abstractformat *self) {
    xx_uefi_fv_destroy((xx_uefi_fv *)self);
}

void xx_uefi_fv_free(xx_uefi_fv *fv) {
    if (!fv) return;
    xx_uefi_fv_destroy(fv);
    xx_mem_free(fv);
}

bool xx_uefi_fv_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_uefi_fv_private parsed;
    bool result = xx_uefi_fv_parse(self, &parsed, pd);
    xx_uefi_fv_private_cleanup(&parsed);
    return result;
}

bool xx_uefi_fv_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_uefi_fv_private *parsed;
    xx_uefi_fv *fv = (xx_uefi_fv *)self;
    int64_t total_size;
    if (!self || !fv) return false;
    parsed = (xx_uefi_fv_private *)xx_mem_alloc(sizeof(*parsed));
    if (!parsed || !xx_uefi_fv_parse(self, parsed, pd)) {
        if (parsed) xx_mem_free(parsed);
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    if (fv->internal) {
        xx_uefi_fv_private_cleanup((xx_uefi_fv_private *)fv->internal);
        xx_mem_free(fv->internal);
    }
    fv->internal = parsed;
    fv->number_of_records = parsed->count;
    fv->number_of_members = parsed->count;
    fv->number_of_volumes = parsed->volumes;
    fv->number_of_files = parsed->files;
    fv->fv_length = parsed->fv_length;
    fv->header_length = parsed->header_length;
    fv->checksum = parsed->checksum;
    fv->revision = parsed->revision;
    fv->archive_end = parsed->archive_end;
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

int64_t xx_uefi_fv_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self || (!self->base_info_handled &&
                  !xx_format_handle_base_info(self, pd))) return -1;
    return self->format_size;
}

uint64_t xx_uefi_fv_get_number_of_archive_records(Abstractformat *self,
                                                  xx_pd_struct *pd) {
    if (!self || (!self->base_info_handled &&
                  !xx_format_handle_base_info(self, pd))) return 0U;
    return ((xx_uefi_fv *)self)->number_of_records;
}

xx_archive_record_state *xx_uefi_fv_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_record_state *state;
    xx_uefi_fv_archive_stream *stream;
    if (!self || !self->device ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    stream = (xx_uefi_fv_archive_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!state || !stream) {
        if (state) xx_mem_free(state);
        if (stream) xx_mem_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    if (!xx_uefi_fv_copy_options(&state->options, options) ||
        !xx_uefi_fv_parse(self, &stream->parsed, pd)) {
        xx_uefi_fv_archive_stream_free(stream);
        xx_archive_record_state_free(state);
        return NULL;
    }
    stream->index = 0U;
    state->internal_state = stream;
    state->free_internal = xx_uefi_fv_archive_stream_free;
    state->total_records = (int64_t)stream->parsed.count;
    if (stream->parsed.count != 0U &&
        xx_uefi_fv_populate_record(&state->current_record,
                                   &stream->parsed.entries[0])) {
        state->has_record = true;
        state->current_index = 0;
    }
    return state;
}

const xx_archive_record *xx_uefi_fv_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record : NULL;
}

bool xx_uefi_fv_archive_record_move_to_next(Abstractformat *self,
                                            xx_archive_record_state *state,
                                            xx_pd_struct *pd) {
    xx_uefi_fv_archive_stream *stream;
    if (!self || !state || state->format != self || !state->has_record ||
        !state->internal_state || (pd && xx_pd_is_stopped(pd))) return false;
    stream = (xx_uefi_fv_archive_stream *)state->internal_state;
    ++stream->index;
    if (stream->index >= stream->parsed.count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    if (!xx_uefi_fv_populate_record(&state->current_record,
                                    &stream->parsed.entries[stream->index])) {
        state->has_record = false;
        return false;
    }
    ++state->current_index;
    return true;
}

bool xx_uefi_fv_unpack_current_archive_record(Abstractformat *self,
                                              xx_archive_record_state *state,
                                              xx_pd_struct *pd) {
    const xx_uefi_fv_archive_stream *stream;
    const xx_uefi_fv_entry *entry;
    const xx_var *option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *destination = NULL;
    bool result = false;
    if (!self || !self->device || !state || state->format != self ||
        !state->has_record || !state->internal_state ||
        (pd && xx_pd_is_stopped(pd))) return false;
    stream = (const xx_uefi_fv_archive_stream *)state->internal_state;
    if (stream->index >= stream->parsed.count) return false;
    entry = &stream->parsed.entries[stream->index];
    if (!xx_uefi_fv_safe_name(entry->name)) return false;
    /* No codec exists for these, so extraction is refused rather than
     * producing the still-compressed bytes under an innocent name. */
    if (entry->method == XX_UEFI_FV_METHOD_TIANO ||
        entry->method == XX_UEFI_FV_METHOD_UNKNOWN) {
        return false;
    }
    option = xx_uefi_fv_find_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!option) {
        int64_t total = xx_io_total_size(self->device);
        return entry->data_offset >= 0 && entry->data_size >= 0 &&
               entry->data_offset <= total &&
               entry->data_size <= total - entry->data_offset;
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
            char *joined = xx_str_concat(destination, entry->name);
            xx_str_free(destination);
            destination = joined;
        }
    } else {
        destination = xx_str_concat(base, entry->name);
    }
    if (!destination) goto cleanup;
    if (entry->is_folder) {
        result = xx_store_create_dirs_a(destination, true);
    } else if (xx_store_create_dirs_a(destination, false)) {
        if (entry->method == XX_UEFI_FV_METHOD_LZMA) {
            result = xx_lzma_unpack_device_to_file(
                self->device, entry->codec_offset, entry->codec_size,
                entry->props, sizeof(entry->props), entry->unpacked_size,
                destination, pd);
        } else {
            result = xx_store_unpack_device_to_file(
                self->device, entry->data_offset, entry->data_size, destination,
                pd);
        }
    }
cleanup:
    if (owned_base) xx_str_free(owned_base);
    if (destination) xx_str_free(destination);
    return result;
}

void xx_uefi_fv_free_archive_records_reading(Abstractformat *self,
                                             xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

uint64_t xx_uefi_fv_get_number_of_records(const xx_uefi_fv *fv) {
    return fv ? fv->number_of_records : 0U;
}
uint64_t xx_uefi_fv_get_number_of_members(const xx_uefi_fv *fv) {
    return fv ? fv->number_of_members : 0U;
}
uint64_t xx_uefi_fv_get_number_of_volumes(const xx_uefi_fv *fv) {
    return fv ? fv->number_of_volumes : 0U;
}
uint64_t xx_uefi_fv_get_number_of_files(const xx_uefi_fv *fv) {
    return fv ? fv->number_of_files : 0U;
}
uint64_t xx_uefi_fv_get_fv_length(const xx_uefi_fv *fv) {
    return fv ? fv->fv_length : 0U;
}
uint16_t xx_uefi_fv_get_checksum(const xx_uefi_fv *fv) {
    return fv ? fv->checksum : 0U;
}
uint8_t xx_uefi_fv_get_revision(const xx_uefi_fv *fv) {
    return fv ? fv->revision : 0U;
}
int64_t xx_uefi_fv_get_archive_end(const xx_uefi_fv *fv) {
    return fv ? fv->archive_end : -1;
}
