/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/gpt/xx_gpt.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

/* Registration placeholder. xxfc_defs.h is shared and is not edited from
 * here, so the file-type constant is resolved locally until the enumerator
 * lands. The GPT macro is the alias xxfc_defs.h defines next to every
 * XX_FILE_TYPE_* value, so this block heals itself the moment the enum grows
 * a GPT member; delete it then. */
#ifdef GPT
#define XX_GPT_FILE_TYPE XX_FILE_TYPE_GPT
#else
#define XX_GPT_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define XX_GPT_SIGNATURE "EFI PART"
#define XX_GPT_HEADER_MIN_SIZE 92U
#define XX_GPT_ENTRY_MIN_SIZE 128U
#define XX_GPT_REVISION_1_0 0x00010000U
#define XX_GPT_NAME_UNITS 36U
#define XX_GPT_GUID_TEXT_SIZE 37U /* 36 characters plus the terminator. */

/* Caps on the attacker-controlled array geometry. The UEFI spec reserves
 * 16384 bytes for the array, i.e. 128 entries of 128 bytes; these bounds are
 * an order of magnitude above that and still keep the product small enough
 * that a hostile header can never ask for a meaningful allocation or a long
 * read. */
#define XX_GPT_MAX_ENTRY_COUNT 4096U
#define XX_GPT_MAX_ENTRY_SIZE 4096U
#define XX_GPT_MAX_ARRAY_BYTES (4U * 1024U * 1024U)

/* Only 512 and 4096 are probed. Those are the only LBA sizes shipped on real
 * media, and probing more would only widen the false-positive surface. */
#define XX_GPT_BLOCK_SIZE_SMALL 512U
#define XX_GPT_BLOCK_SIZE_LARGE 4096U

typedef struct xx_gpt_header_s {
    uint32_t revision;
    uint32_t header_size;
    uint32_t header_crc;
    uint64_t my_lba;
    uint64_t alternate_lba;
    uint64_t first_usable_lba;
    uint64_t last_usable_lba;
    uint64_t partition_entry_lba;
    uint32_t entry_count;
    uint32_t entry_size;
    uint32_t entries_crc;
    char disk_guid[XX_GPT_GUID_TEXT_SIZE];
} xx_gpt_header;

typedef struct xx_gpt_entry_s {
    char *name;  /**< Generated record name, e.g. "partition1". */
    char *label; /**< The decoded UTF-16LE field, UTF-8; never NULL. */
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t data_size;
    uint64_t declared_size;
    uint64_t starting_lba;
    uint64_t ending_lba;
    uint64_t attributes;
    uint32_t entry_index;
    char type_guid[XX_GPT_GUID_TEXT_SIZE];
    char unique_guid[XX_GPT_GUID_TEXT_SIZE];
} xx_gpt_entry;

typedef struct xx_gpt_private_s {
    xx_gpt_entry *entries;
    size_t count;
    size_t capacity;
    int64_t input_size;
    int64_t archive_end;
    xx_gpt_header header;
    uint32_t block_size;
    bool used_backup;
    bool header_crc_valid;
    bool entries_crc_valid;
} xx_gpt_private;

typedef struct xx_gpt_archive_stream_s {
    xx_gpt_private parsed;
    size_t index;
} xx_gpt_archive_stream;

static void xx_gpt_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------------ */
/* Small helpers                                                       */
/* ------------------------------------------------------------------ */

/* Read exactly size bytes at an absolute device offset. xx_io_seek64() is
 * used deliberately: long is 32-bit on Win64 and a GPT disk is routinely
 * larger than 2 GiB - the backup header lives at the very end of it, which
 * the legacy seek could never reach. */
static bool xx_gpt_read_at(xx_io_device *device, int64_t offset, void *data,
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

/* True when [offset, offset + size) lies inside [0, total_size). */
static bool xx_gpt_range_within(int64_t total_size, int64_t offset,
                                int64_t size) {
    return (total_size >= 0) && (offset >= 0) && (size >= 0) &&
           (offset <= total_size) && (size <= total_size - offset);
}

static bool xx_gpt_lba_to_offset(int64_t base_address, uint64_t lba,
                                 uint32_t block_size, int64_t *result) {
    uint64_t bytes;
    if (!result || base_address < 0 || block_size == 0U) return false;
    if (lba > (uint64_t)INT64_MAX / block_size) return false;
    bytes = lba * block_size;
    if (bytes > (uint64_t)(INT64_MAX - base_address)) return false;
    *result = base_address + (int64_t)bytes;
    return true;
}

/* Build a record name of the form "partition12"; see the MBR reader for the
 * same helper and the same reason it is hand rolled. */
static char *xx_gpt_make_name(unsigned index) {
    static const char prefix[] = "partition";
    char digits[16];
    char buffer[sizeof(prefix) + sizeof(digits)];
    size_t used = sizeof(prefix) - 1U;
    size_t count = 0U;
    xx_mem_copy(buffer, prefix, used);
    do {
        digits[count++] = (char)('0' + (index % 10U));
        index /= 10U;
    } while (index != 0U && count < sizeof(digits));
    while (count != 0U) buffer[used++] = digits[--count];
    buffer[used] = '\0';
    return xx_str_create(buffer);
}

/* Render the 16 on-disk bytes of a GUID in canonical uppercase text. The
 * first three fields are stored little endian and the last eight bytes in
 * display order, so only the first three are reversed. */
static void xx_gpt_format_guid(const uint8_t *raw,
                               char out[XX_GPT_GUID_TEXT_SIZE]) {
    static const char digits[] = "0123456789ABCDEF";
    /* Byte index in the on-disk layout for each of the 32 nibble pairs. */
    static const uint8_t order[16] = {3U, 2U,  1U,  0U,  5U,  4U,  7U,  6U,
                                      8U, 9U, 10U, 11U, 12U, 13U, 14U, 15U};
    size_t index;
    size_t used = 0U;
    for (index = 0U; index < 16U; ++index) {
        if (index == 4U || index == 6U || index == 8U || index == 10U) {
            out[used++] = '-';
        }
        out[used++] = digits[(raw[order[index]] >> 4) & 0x0FU];
        out[used++] = digits[raw[order[index]] & 0x0FU];
    }
    out[used] = '\0';
}

static bool xx_gpt_guid_is_zero(const uint8_t *raw) {
    size_t index;
    for (index = 0U; index < 16U; ++index) {
        if (raw[index] != 0U) return false;
    }
    return true;
}

const char *xx_gpt_type_name(const char *type_guid) {
    /* The types worth naming. Everything else is still published, since the
     * caller recurses into the payload and lets the detector decide. */
    static const struct {
        const char *guid;
        const char *name;
    } table[] = {
        {"C12A7328-F81F-11D2-BA4B-00A0C93EC93B", "EFI System"},
        {"024DEE41-33E7-11D3-9D69-0008C781F39F", "MBR partition scheme"},
        {"21686148-6449-6E6F-744E-656564454649", "BIOS boot"},
        {"D3BFE2DE-3DAF-11DF-BA40-E3A556D89593", "Intel Fast Flash"},
        {"F4019732-066E-4E12-8273-346C5641494F", "Sony boot"},
        {"E3C9E316-0B5C-4DB8-817D-F92DF00215AE", "Microsoft reserved"},
        {"EBD0A0A2-B9E5-4433-87C0-68B6B72699C7", "Microsoft basic data"},
        {"5808C8AA-7E8F-42E0-85D2-E1E90434CFB3", "Windows LDM metadata"},
        {"AF9B60A0-1431-4F62-BC68-3311714A69AD", "Windows LDM data"},
        {"DE94BBA4-06D1-4D40-A16A-BFD50179D6AC", "Windows Recovery"},
        {"E75CAF8F-F680-4CEE-AFA3-B001E56EFC2D", "Storage Spaces"},
        {"0FC63DAF-8483-4772-8E79-3D69D8477DE4", "Linux filesystem"},
        {"BC13C2FF-59E6-4262-A352-B275FD6F7172", "Linux extended boot"},
        {"0657FD6D-A4AB-43C4-84E5-0933C84B4F4F", "Linux swap"},
        {"E6D6D379-F507-44C2-A23C-238F2A3DF928", "Linux LVM"},
        {"A19D880F-05FC-4D3B-A006-743F0F84911E", "Linux RAID"},
        {"933AC7E1-2EB4-4F13-B844-0E14E2AEF915", "Linux /home"},
        {"3B8F8425-20E0-4F3B-907F-1A25A76F98E8", "Linux /srv"},
        {"4F68BCE3-E8CD-4DB1-96E7-FBCAF984B709", "Linux root (x86-64)"},
        {"B921B045-1DF0-41C3-AF44-4C6F280D3FAE", "Linux root (arm64)"},
        {"44479540-F297-41B2-9AF7-D131D5F0458A", "Linux root (x86)"},
        {"CA7D7CCB-63ED-4C53-861C-1742536059CC", "Linux LUKS"},
        {"48465300-0000-11AA-AA11-00306543ECAC", "Apple HFS+"},
        {"7C3457EF-0000-11AA-AA11-00306543ECAC", "Apple APFS"},
        {"55465300-0000-11AA-AA11-00306543ECAC", "Apple UFS"},
        {"426F6F74-0000-11AA-AA11-00306543ECAC", "Apple boot (Recovery)"},
        {"53746F72-6167-11AA-AA11-00306543ECAC", "Apple Core Storage"},
        {"516E7CB4-6ECF-11D6-8FF8-00022D09712B", "FreeBSD data"},
        {"516E7CB5-6ECF-11D6-8FF8-00022D09712B", "FreeBSD swap"},
        {"516E7CB6-6ECF-11D6-8FF8-00022D09712B", "FreeBSD UFS"},
        {"83BD6B9D-7F41-11DC-BE0B-001560B84F0F", "FreeBSD boot"},
        {"6A898CC3-1DD2-11B2-99A6-080020736631", "Solaris /usr or Apple ZFS"},
        {"9E1A2D38-C612-4316-AA26-8B49521E5A8B", "PowerPC PReP boot"},
        {"FE3A2A5D-4F32-41A7-B725-ACCC3285A309", "ChromeOS kernel"},
        {"3CB8E202-3B7E-47DD-8A3C-7FF2A13CFCEC", "ChromeOS rootfs"},
        {"2E0A753D-9E48-43B0-8337-B15192CB1B5E", "ChromeOS reserved"},
        {"BFBFAFE7-A34F-448A-9A5B-6213EB736C22", "Lenovo boot"}};
    size_t index;
    if (!type_guid) return "Unknown";
    for (index = 0U; index < sizeof(table) / sizeof(table[0]); ++index) {
        if (xx_str_icmp(type_guid, table[index].guid) == 0) {
            return table[index].name;
        }
    }
    return "Unknown";
}

/* Decode the 36 UTF-16LE code units of the name field into UTF-8. An
 * unpaired surrogate or a control character makes the label implausible, and
 * an empty string is returned instead of a half-decoded one - the record's
 * own name never comes from here, so nothing depends on the result. */
static char *xx_gpt_decode_label(const uint8_t *raw) {
    char buffer[XX_GPT_NAME_UNITS * 3U + 1U];
    size_t used = 0U;
    size_t index = 0U;
    while (index < XX_GPT_NAME_UNITS) {
        uint32_t unit = (uint32_t)raw[index * 2U] |
                        ((uint32_t)raw[index * 2U + 1U] << 8);
        uint32_t code = unit;
        ++index;
        if (unit == 0U) break;
        if (unit >= 0xD800U && unit <= 0xDBFFU) {
            uint32_t low;
            if (index >= XX_GPT_NAME_UNITS) return xx_str_create("");
            low = (uint32_t)raw[index * 2U] |
                  ((uint32_t)raw[index * 2U + 1U] << 8);
            if (low < 0xDC00U || low > 0xDFFFU) return xx_str_create("");
            ++index;
            code = 0x10000U + ((unit - 0xD800U) << 10) + (low - 0xDC00U);
        } else if (unit >= 0xDC00U && unit <= 0xDFFFU) {
            return xx_str_create("");
        }
        if (code < 0x20U || code == 0x7FU) return xx_str_create("");
        if (code < 0x80U) {
            buffer[used++] = (char)code;
        } else if (code < 0x800U) {
            buffer[used++] = (char)(0xC0U | (code >> 6));
            buffer[used++] = (char)(0x80U | (code & 0x3FU));
        } else if (code < 0x10000U) {
            buffer[used++] = (char)(0xE0U | (code >> 12));
            buffer[used++] = (char)(0x80U | ((code >> 6) & 0x3FU));
            buffer[used++] = (char)(0x80U | (code & 0x3FU));
        } else {
            /* Four UTF-8 bytes, but the pair that produced them consumed two
             * of the 36 units, so the three-bytes-per-unit buffer still
             * holds. */
            buffer[used++] = (char)(0xF0U | (code >> 18));
            buffer[used++] = (char)(0x80U | ((code >> 12) & 0x3FU));
            buffer[used++] = (char)(0x80U | ((code >> 6) & 0x3FU));
            buffer[used++] = (char)(0x80U | (code & 0x3FU));
        }
    }
    buffer[used] = '\0';
    return xx_str_create(buffer);
}

/* ------------------------------------------------------------------ */
/* Header and entry array                                              */
/* ------------------------------------------------------------------ */

static void xx_gpt_private_cleanup(xx_gpt_private *parsed) {
    size_t index;
    if (!parsed) return;
    for (index = 0U; index < parsed->count; ++index) {
        if (parsed->entries[index].name) xx_str_free(parsed->entries[index].name);
        if (parsed->entries[index].label) {
            xx_str_free(parsed->entries[index].label);
        }
    }
    if (parsed->entries) xx_mem_free(parsed->entries);
    xx_mem_zero(parsed, sizeof(*parsed));
    parsed->input_size = -1;
    parsed->archive_end = -1;
}

static bool xx_gpt_append_entry(xx_gpt_private *parsed, xx_gpt_entry *entry) {
    xx_gpt_entry *grown;
    size_t capacity;
    if (!parsed || !entry || !entry->name ||
        parsed->count >= XX_GPT_MAX_ENTRY_COUNT) {
        return false;
    }
    if (parsed->count == parsed->capacity) {
        capacity = parsed->capacity ? parsed->capacity * 2U : 16U;
        if (capacity < parsed->count ||
            capacity > SIZE_MAX / sizeof(*parsed->entries)) return false;
        grown = (xx_gpt_entry *)xx_mem_realloc(
            parsed->entries, capacity * sizeof(*parsed->entries));
        if (!grown) return false;
        parsed->entries = grown;
        parsed->capacity = capacity;
    }
    parsed->entries[parsed->count++] = *entry;
    xx_mem_zero(entry, sizeof(*entry));
    return true;
}

/* Read and verify one GPT header. expected_my_lba pins the copy: the primary
 * must say it lives at LBA 1 and the backup at the last LBA, which is what
 * stops a stale or relocated header from being accepted in the wrong place.
 */
static bool xx_gpt_read_header(xx_io_device *device, int64_t offset,
                               uint32_t block_size, int64_t total_size,
                               uint64_t expected_my_lba,
                               xx_gpt_header *header) {
    uint8_t buffer[XX_GPT_BLOCK_SIZE_LARGE];
    uint32_t header_size;
    uint32_t stored_crc;
    uint32_t computed_crc;
    if (!device || !header || block_size > sizeof(buffer)) return false;
    if (!xx_gpt_range_within(total_size, offset, block_size) ||
        !xx_gpt_read_at(device, offset, buffer, block_size)) {
        return false;
    }
    if (xx_rt_memcmp(buffer, XX_GPT_SIGNATURE, 8U) != 0) return false;
    header->revision = xx_data_get_u32(buffer, block_size, 8U, false);
    header_size = xx_data_get_u32(buffer, block_size, 12U, false);
    stored_crc = xx_data_get_u32(buffer, block_size, 16U, false);
    /* The reserved word must be zero; it is the cheapest structural reject
     * available before the CRC is paid for. */
    if (xx_data_get_u32(buffer, block_size, 20U, false) != 0U) return false;
    if (header->revision < XX_GPT_REVISION_1_0) return false;
    if (header_size < XX_GPT_HEADER_MIN_SIZE || header_size > block_size) {
        return false;
    }
    /* The CRC covers header_size bytes with its own field taken as zero. */
    buffer[16] = 0U;
    buffer[17] = 0U;
    buffer[18] = 0U;
    buffer[19] = 0U;
    computed_crc = xx_crc32_calc(0U, buffer, header_size);
    if (computed_crc != stored_crc) return false;
    header->header_size = header_size;
    header->header_crc = stored_crc;
    header->my_lba = xx_data_get_u64(buffer, block_size, 24U, false);
    if (header->my_lba != expected_my_lba) return false;
    header->alternate_lba = xx_data_get_u64(buffer, block_size, 32U, false);
    header->first_usable_lba = xx_data_get_u64(buffer, block_size, 40U, false);
    header->last_usable_lba = xx_data_get_u64(buffer, block_size, 48U, false);
    xx_gpt_format_guid(buffer + 56, header->disk_guid);
    header->partition_entry_lba = xx_data_get_u64(buffer, block_size, 72U,
                                                  false);
    header->entry_count = xx_data_get_u32(buffer, block_size, 80U, false);
    header->entry_size = xx_data_get_u32(buffer, block_size, 84U, false);
    header->entries_crc = xx_data_get_u32(buffer, block_size, 88U, false);
    if (header->first_usable_lba > header->last_usable_lba) return false;
    return true;
}

/* Are the array's declared geometry and position usable at all? This runs
 * before any array byte is read, so a header claiming four billion entries
 * costs nothing. */
static bool xx_gpt_array_geometry_ok(const xx_gpt_header *header,
                                     int64_t base_address, uint32_t block_size,
                                     int64_t total_size, int64_t *array_offset,
                                     int64_t *array_size) {
    uint64_t bytes;
    if (!header || !array_offset || !array_size) return false;
    if (header->entry_count == 0U ||
        header->entry_count > XX_GPT_MAX_ENTRY_COUNT) return false;
    if (header->entry_size < XX_GPT_ENTRY_MIN_SIZE ||
        header->entry_size > XX_GPT_MAX_ENTRY_SIZE ||
        (header->entry_size % 8U) != 0U) return false;
    bytes = (uint64_t)header->entry_count * header->entry_size;
    if (bytes > XX_GPT_MAX_ARRAY_BYTES) return false;
    if (header->partition_entry_lba == 0U) return false;
    if (!xx_gpt_lba_to_offset(base_address, header->partition_entry_lba,
                              block_size, array_offset)) {
        return false;
    }
    if (!xx_gpt_range_within(total_size, *array_offset, (int64_t)bytes)) {
        return false;
    }
    *array_size = (int64_t)bytes;
    return true;
}

/* Verify the entry array CRC by streaming. The array is never held in
 * memory, so its size never becomes an allocation. */
static bool xx_gpt_array_crc_ok(xx_io_device *device, int64_t array_offset,
                                int64_t array_size, uint32_t expected,
                                xx_pd_struct *pd) {
    uint8_t buffer[8192];
    int64_t done = 0;
    uint32_t crc = 0U;
    if (xx_io_seek64(device, array_offset, SEEK_SET) != 0) return false;
    while (done < array_size) {
        int64_t remaining = array_size - done;
        size_t want = remaining < (int64_t)sizeof(buffer)
                          ? (size_t)remaining : sizeof(buffer);
        ssize_t got;
        if (pd && xx_pd_is_stopped(pd)) return false;
        got = xx_io_read(device, buffer, want);
        if (got <= 0 || (size_t)got > want) return false;
        crc = xx_crc32_calc(crc, buffer, (size_t)got);
        done += got;
    }
    return crc == expected;
}

/* Decode the array and publish every used slot. The declared range is
 * clamped to what the device holds, because a truncated dump is common and
 * its leading partitions are still usable; declared_size keeps the figure
 * the table actually stated. */
static bool xx_gpt_collect_entries(Abstractformat *self, xx_gpt_private *parsed,
                                   int64_t array_offset, xx_pd_struct *pd) {
    uint32_t index;
    for (index = 0U; index < parsed->header.entry_count; ++index) {
        uint8_t raw[XX_GPT_ENTRY_MIN_SIZE];
        int64_t entry_offset = array_offset +
                               (int64_t)index * parsed->header.entry_size;
        xx_gpt_entry entry;
        uint64_t blocks;
        uint64_t declared;
        int64_t offset;
        int64_t available;
        if (pd && xx_pd_is_stopped(pd)) return false;
        xx_mem_zero(&entry, sizeof(entry));
        if (!xx_gpt_read_at(self->device, entry_offset, raw, sizeof(raw))) {
            return false;
        }
        if (xx_gpt_guid_is_zero(raw)) continue; /* An unused slot. */
        entry.starting_lba = xx_data_get_u64(raw, sizeof(raw), 32U, false);
        entry.ending_lba = xx_data_get_u64(raw, sizeof(raw), 40U, false);
        /* The ending LBA is inclusive, so a one-block partition has
         * ending == starting. An ending below the start is nonsense and the
         * slot is dropped rather than failing the whole table. */
        if (entry.ending_lba < entry.starting_lba) continue;
        if (entry.starting_lba == 0U) continue; /* Would cover the MBR. */
        blocks = entry.ending_lba - entry.starting_lba + 1U;
        if (!xx_gpt_lba_to_offset(self->base_address, entry.starting_lba,
                                  parsed->block_size, &offset)) {
            continue;
        }
        if (offset >= parsed->input_size) continue;
        if (blocks > (uint64_t)INT64_MAX / parsed->block_size) continue;
        declared = blocks * parsed->block_size;
        available = parsed->input_size - offset;
        if (declared < (uint64_t)available) available = (int64_t)declared;
        entry.attributes = xx_data_get_u64(raw, sizeof(raw), 48U, false);
        entry.entry_index = index;
        entry.header_offset = entry_offset;
        entry.header_size = (int64_t)parsed->header.entry_size;
        entry.data_offset = offset;
        entry.data_size = available;
        entry.declared_size = declared;
        xx_gpt_format_guid(raw, entry.type_guid);
        xx_gpt_format_guid(raw + 16, entry.unique_guid);
        entry.label = xx_gpt_decode_label(raw + 56);
        entry.name = xx_gpt_make_name(index + 1U);
        if (!entry.label || !entry.name || !xx_gpt_append_entry(parsed, &entry)) {
            if (entry.label) xx_str_free(entry.label);
            if (entry.name) xx_str_free(entry.name);
            return false;
        }
        if (offset + available > parsed->archive_end) {
            parsed->archive_end = offset + available;
        }
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* Parsing                                                             */
/* ------------------------------------------------------------------ */

/* Try one (block size, copy) combination. Returns true when the header
 * verified; *array_ok then says whether its entry array verified too. */
static bool xx_gpt_try_header(Abstractformat *self, uint32_t block_size,
                              bool backup, int64_t total_size,
                              xx_gpt_header *header, int64_t *array_offset,
                              bool *array_ok, xx_pd_struct *pd) {
    int64_t header_offset;
    int64_t array_size;
    uint64_t expected_my_lba;
    *array_ok = false;
    if (backup) {
        /* The backup header sits in the last addressable LBA of the medium.
         * A device whose length is not a whole number of blocks simply loses
         * the tail, exactly as a real controller would. */
        int64_t usable = total_size - self->base_address;
        uint64_t blocks;
        if (usable < (int64_t)block_size * 2) return false;
        blocks = (uint64_t)usable / block_size;
        expected_my_lba = blocks - 1U;
    } else {
        expected_my_lba = 1U;
    }
    if (!xx_gpt_lba_to_offset(self->base_address, expected_my_lba, block_size,
                              &header_offset)) {
        return false;
    }
    if (!xx_gpt_read_header(self->device, header_offset, block_size, total_size,
                            expected_my_lba, header)) {
        return false;
    }
    if (!xx_gpt_array_geometry_ok(header, self->base_address, block_size,
                                  total_size, array_offset, &array_size)) {
        return true; /* The header stands; its array does not. */
    }
    *array_ok = xx_gpt_array_crc_ok(self->device, *array_offset, array_size,
                                    header->entries_crc, pd);
    return true;
}

static bool xx_gpt_parse(Abstractformat *self, xx_gpt_private *parsed,
                         xx_pd_struct *pd) {
    static const uint32_t block_sizes[2] = {XX_GPT_BLOCK_SIZE_SMALL,
                                            XX_GPT_BLOCK_SIZE_LARGE};
    int64_t total_size;
    xx_gpt_header header;
    xx_gpt_header fallback_header;
    int64_t array_offset = -1;
    /* Both are only read behind their own guards; zeroing them keeps MSVC
     * from warning about a path it cannot prove is unreachable. */
    int64_t fallback_array_offset = -1;
    uint32_t fallback_block_size = 0U;
    bool fallback_backup = false;
    bool have_fallback = false;
    bool chosen = false;
    unsigned pass;
    unsigned index;
    /* Initialise before the guard clause: callers run the cleanup on their
     * stack copy whatever this returns. */
    if (parsed) {
        xx_mem_zero(parsed, sizeof(*parsed));
        parsed->input_size = -1;
        parsed->archive_end = -1;
    }
    if (!self || !self->device || !parsed || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd))) return false;
    xx_mem_zero(&header, sizeof(header));
    xx_mem_zero(&fallback_header, sizeof(fallback_header));
    total_size = xx_io_total_size(self->device);
    if (total_size <= self->base_address) goto fail;

    /* Two passes. The first looks for a copy whose header and entry array
     * both verify - the primary is preferred, then the backup. The second
     * pass is the degraded case: a header that verified but whose array did
     * not, which still names the geometry of a disk worth reporting. */
    for (pass = 0U; pass < 2U && !chosen; ++pass) {
        bool backup = pass != 0U;
        for (index = 0U; index < 2U && !chosen; ++index) {
            bool array_ok = false;
            int64_t offset = -1;
            if (!xx_gpt_try_header(self, block_sizes[index], backup, total_size,
                                   &header, &offset, &array_ok, pd)) {
                continue;
            }
            if (array_ok) {
                parsed->block_size = block_sizes[index];
                parsed->used_backup = backup;
                parsed->header_crc_valid = true;
                parsed->entries_crc_valid = true;
                parsed->header = header;
                array_offset = offset;
                chosen = true;
            } else if (!have_fallback && offset >= 0) {
                fallback_header = header;
                fallback_array_offset = offset;
                fallback_block_size = block_sizes[index];
                fallback_backup = backup;
                have_fallback = true;
            }
        }
    }
    if (!chosen) {
        /* Nothing verified end to end. A header whose own CRC matched is
         * still a GPT - a 32-bit CRC does not hit by accident - so it is
         * accepted with entries_crc_valid left false. */
        if (!have_fallback) goto fail;
        parsed->block_size = fallback_block_size;
        parsed->used_backup = fallback_backup;
        parsed->header_crc_valid = true;
        parsed->entries_crc_valid = false;
        parsed->header = fallback_header;
        array_offset = fallback_array_offset;
    }
    parsed->input_size = total_size;
    parsed->archive_end = self->base_address +
                          (int64_t)parsed->block_size * 2;
    if (!xx_gpt_collect_entries(self, parsed, array_offset, pd)) goto fail;
    if (parsed->count == 0U) goto fail;
    return true;
fail:
    xx_gpt_private_cleanup(parsed);
    return false;
}

/* ------------------------------------------------------------------ */
/* Archive record plumbing                                             */
/* ------------------------------------------------------------------ */

static bool xx_gpt_copy_options(xx_list_s *destination,
                                const xx_list_s *source) {
    size_t index;
    if (!destination || !source) return source == NULL;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *item = (const xx_meta *)xx_list_at(
            (const xx_list_t *)source, index);
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

static const xx_var *xx_gpt_find_option(const xx_list_s *options,
                                        uint32_t meta_id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *item = (const xx_meta *)xx_list_at(
            (const xx_list_t *)options, index);
        if (item && item->meta_id == meta_id) return &item->var;
    }
    return NULL;
}

/* The payload is carried verbatim, so both sizes are the bytes present. The
 * comment holds the readable type and the attributes field is published as
 * is; the GUIDs and the label reach a caller through
 * xx_gpt_get_partition_info(). */
static bool xx_gpt_populate_record(xx_archive_record *record,
                                   const xx_gpt_entry *entry) {
    if (!record || !entry || !entry->name) return false;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = entry->header_offset;
    record->header_size = entry->header_size;
    record->data_offset = entry->data_offset;
    record->compressed_size = entry->data_size;
    return xx_archive_record_set_original_name(record, entry->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)entry->data_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)entry->data_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          0U) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES,
                                          entry->attributes) &&
           xx_archive_record_set_meta_str(record, XX_META_ID_COMMENT,
                                          xx_gpt_type_name(entry->type_guid)) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

static void xx_gpt_archive_stream_free(void *pointer) {
    xx_gpt_archive_stream *stream = (xx_gpt_archive_stream *)pointer;
    if (!stream) return;
    xx_gpt_private_cleanup(&stream->parsed);
    xx_mem_free(stream);
}

/* The record names are generated here, never taken from the image - the
 * decoded label is deliberately kept out of the extraction path - so this
 * only has to refuse the impossible. */
static bool xx_gpt_safe_name(const char *name) {
    size_t index;
    if (!name || !name[0]) return false;
    for (index = 0U; name[index] != '\0'; ++index) {
        char ch = name[index];
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
              (ch >= '0' && ch <= '9') || ch == '_' || ch == '-' ||
              ch == '.')) {
            return false;
        }
    }
    return name[0] != '.';
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

void xx_gpt_init(xx_gpt *gpt, xx_io_device *dev, int64_t base_address) {
    if (!gpt) return;
    xx_mem_zero(gpt, sizeof(*gpt));
    xx_format_init(&gpt->format, dev, base_address);
    gpt->format.endian = XX_ENDIAN_LITTLE;
    gpt->format.file_type = XX_GPT_FILE_TYPE;
    gpt->format.format_type = XX_TYPE_ARCHIVE;
    gpt->format.is_archive = true;
    xx_format_set_mime_type(&gpt->format, "application/x-gpt");
    xx_format_set_extension(&gpt->format, "img");
    gpt->format.check_is_valid = xx_gpt_check_is_valid;
    gpt->format.handle_base_info = xx_gpt_handle_base_info;
    gpt->format.get_format_size = xx_gpt_get_format_size;
    gpt->format.get_number_of_archive_records =
        xx_gpt_get_number_of_archive_records;
    gpt->format.create_archive_records_reading =
        xx_gpt_create_archive_records_reading;
    gpt->format.get_current_archive_record = xx_gpt_get_current_archive_record;
    gpt->format.unpack_current_archive_record =
        xx_gpt_unpack_current_archive_record;
    gpt->format.archive_record_move_to_next = xx_gpt_archive_record_move_to_next;
    gpt->format.free_archive_records_reading =
        xx_gpt_free_archive_records_reading;
    gpt->format.destroy = xx_gpt_vtable_destroy;
    gpt->archive_end = -1;
}

xx_gpt *xx_gpt_create(xx_io_device *dev, int64_t base_address) {
    xx_gpt *gpt = (xx_gpt *)xx_mem_alloc(sizeof(*gpt));
    if (gpt) xx_gpt_init(gpt, dev, base_address);
    return gpt;
}

void xx_gpt_destroy(xx_gpt *gpt) {
    if (!gpt) return;
    if (gpt->internal) {
        xx_gpt_private_cleanup((xx_gpt_private *)gpt->internal);
        xx_mem_free(gpt->internal);
        gpt->internal = NULL;
    }
    xx_format_cleanup_extra_parameters(&gpt->format);
}

static void xx_gpt_vtable_destroy(Abstractformat *self) {
    xx_gpt_destroy((xx_gpt *)self);
}

void xx_gpt_free(xx_gpt *gpt) {
    if (!gpt) return;
    xx_gpt_destroy(gpt);
    xx_mem_free(gpt);
}

bool xx_gpt_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_gpt_private parsed;
    bool result = xx_gpt_parse(self, &parsed, pd);
    xx_gpt_private_cleanup(&parsed);
    return result;
}

bool xx_gpt_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_gpt_private *parsed;
    xx_gpt *gpt = (xx_gpt *)self;
    int64_t total_size;
    if (!self || !gpt) return false;
    parsed = (xx_gpt_private *)xx_mem_alloc(sizeof(*parsed));
    if (!parsed || !xx_gpt_parse(self, parsed, pd)) {
        if (parsed) xx_mem_free(parsed);
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    if (gpt->internal) {
        xx_gpt_private_cleanup((xx_gpt_private *)gpt->internal);
        xx_mem_free(gpt->internal);
    }
    gpt->internal = parsed;
    gpt->number_of_records = parsed->count;
    gpt->number_of_members = parsed->count;
    gpt->block_size = parsed->block_size;
    gpt->entry_count = parsed->header.entry_count;
    gpt->entry_size = parsed->header.entry_size;
    gpt->first_usable_lba = parsed->header.first_usable_lba;
    gpt->last_usable_lba = parsed->header.last_usable_lba;
    gpt->partition_entry_lba = parsed->header.partition_entry_lba;
    gpt->used_backup = parsed->used_backup;
    gpt->header_crc_valid = parsed->header_crc_valid;
    gpt->entries_crc_valid = parsed->entries_crc_valid;
    gpt->archive_end = parsed->archive_end;
    xx_format_set_version(self, parsed->used_backup ? "1.0 (backup)" : "1.0");
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

int64_t xx_gpt_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self || (!self->base_info_handled &&
                  !xx_format_handle_base_info(self, pd))) return -1;
    return self->format_size;
}

uint64_t xx_gpt_get_number_of_archive_records(Abstractformat *self,
                                              xx_pd_struct *pd) {
    if (!self || (!self->base_info_handled &&
                  !xx_format_handle_base_info(self, pd))) return 0U;
    return ((xx_gpt *)self)->number_of_records;
}

xx_archive_record_state *xx_gpt_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_record_state *state;
    xx_gpt_archive_stream *stream;
    if (!self || !self->device ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    stream = (xx_gpt_archive_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!state || !stream) {
        if (state) xx_mem_free(state);
        if (stream) xx_mem_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    if (!xx_gpt_copy_options(&state->options, options) ||
        !xx_gpt_parse(self, &stream->parsed, pd)) {
        xx_gpt_archive_stream_free(stream);
        xx_archive_record_state_free(state);
        return NULL;
    }
    stream->index = 0U;
    state->internal_state = stream;
    state->free_internal = xx_gpt_archive_stream_free;
    state->total_records = (int64_t)stream->parsed.count;
    if (stream->parsed.count != 0U &&
        xx_gpt_populate_record(&state->current_record,
                               &stream->parsed.entries[0])) {
        state->has_record = true;
        state->current_index = 0;
    }
    return state;
}

const xx_archive_record *xx_gpt_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record : NULL;
}

bool xx_gpt_archive_record_move_to_next(Abstractformat *self,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    xx_gpt_archive_stream *stream;
    if (!self || !state || state->format != self || !state->has_record ||
        !state->internal_state || (pd && xx_pd_is_stopped(pd))) return false;
    stream = (xx_gpt_archive_stream *)state->internal_state;
    ++stream->index;
    if (stream->index >= stream->parsed.count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    if (!xx_gpt_populate_record(&state->current_record,
                                &stream->parsed.entries[stream->index])) {
        state->has_record = false;
        return false;
    }
    ++state->current_index;
    return true;
}

bool xx_gpt_unpack_current_archive_record(Abstractformat *self,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    const xx_archive_record *record;
    const xx_var *option;
    const char *name;
    const char *base = NULL;
    char *owned_base = NULL;
    char *destination = NULL;
    bool result;
    if (!self || !self->device || !state || state->format != self ||
        !state->has_record || (pd && xx_pd_is_stopped(pd))) return false;
    record = &state->current_record;
    name = xx_archive_record_get_original_name(record);
    if (!xx_gpt_safe_name(name)) return false;
    option = xx_gpt_find_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
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
    if (xx_store_create_dirs_a(destination, false)) {
        result = xx_store_unpack_device_to_file(self->device,
                                                record->data_offset,
                                                record->compressed_size,
                                                destination, pd);
        if (!result) xx_rt_remove(destination);
    } else {
        result = false;
    }
    if (owned_base) xx_str_free(owned_base);
    xx_str_free(destination);
    return result;
cleanup:
    if (owned_base) xx_str_free(owned_base);
    if (destination) xx_str_free(destination);
    return false;
}

void xx_gpt_free_archive_records_reading(Abstractformat *self,
                                         xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

uint64_t xx_gpt_get_number_of_records(const xx_gpt *gpt) {
    return gpt ? gpt->number_of_records : 0U;
}
uint64_t xx_gpt_get_number_of_members(const xx_gpt *gpt) {
    return gpt ? gpt->number_of_members : 0U;
}
uint32_t xx_gpt_get_block_size(const xx_gpt *gpt) {
    return gpt ? gpt->block_size : 0U;
}
bool xx_gpt_used_backup(const xx_gpt *gpt) {
    return gpt ? gpt->used_backup : false;
}
int64_t xx_gpt_get_archive_end(const xx_gpt *gpt) {
    return gpt ? gpt->archive_end : -1;
}

const char *xx_gpt_get_disk_guid(const xx_gpt *gpt) {
    if (!gpt || !gpt->internal) return NULL;
    return ((const xx_gpt_private *)gpt->internal)->header.disk_guid;
}

bool xx_gpt_get_partition_info(const xx_gpt *gpt, uint64_t index,
                               xx_gpt_partition_info *info) {
    const xx_gpt_private *parsed;
    const xx_gpt_entry *entry;
    if (!gpt || !info || !gpt->internal) return false;
    parsed = (const xx_gpt_private *)gpt->internal;
    if (index >= parsed->count) return false;
    entry = &parsed->entries[index];
    info->offset = entry->data_offset;
    info->size = entry->data_size;
    info->declared_size = entry->declared_size;
    info->starting_lba = entry->starting_lba;
    info->ending_lba = entry->ending_lba;
    info->attributes = entry->attributes;
    info->entry_index = entry->entry_index;
    info->type_guid = entry->type_guid;
    info->unique_guid = entry->unique_guid;
    info->type_name = xx_gpt_type_name(entry->type_guid);
    info->label = entry->label ? entry->label : "";
    info->name = entry->name;
    return true;
}
