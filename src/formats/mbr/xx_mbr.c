/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/mbr/xx_mbr.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

/* Registration placeholder. xxfc_defs.h is shared and is not edited from
 * here, so the file-type constant is resolved locally until the enumerator
 * lands. The MBR macro is the alias xxfc_defs.h defines next to every
 * XX_FILE_TYPE_* value, so this block heals itself the moment the enum grows
 * an MBR member; delete it then. */
#ifdef MBR
#define XX_MBR_FILE_TYPE XX_FILE_TYPE_MBR
#else
#define XX_MBR_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

/* The partition entry fields are defined in 512-byte logical blocks whatever
 * the physical sector size of the medium is. */
#define XX_MBR_SECTOR_SIZE 512
#define XX_MBR_TABLE_OFFSET 446
#define XX_MBR_ENTRY_SIZE 16
#define XX_MBR_PRIMARY_COUNT 4
#define XX_MBR_SIGNATURE_OFFSET 510
#define XX_MBR_DISK_SIGNATURE_OFFSET 440

/* Bounds on the EBR walk. The step cap alone would stop a cycle, but a short
 * two-node loop would then still produce thousands of duplicate records, so a
 * visited set of sector offsets runs alongside it. */
#define XX_MBR_MAX_EBR_STEPS 256U
#define XX_MBR_MAX_LOGICAL 128U
#define XX_MBR_MAX_PARTITIONS (XX_MBR_PRIMARY_COUNT + XX_MBR_MAX_LOGICAL)

/* Container types whose payload is an EBR chain rather than a filesystem. */
#define XX_MBR_TYPE_EXTENDED_CHS 0x05U
#define XX_MBR_TYPE_EXTENDED_LBA 0x0FU
#define XX_MBR_TYPE_EXTENDED_LINUX 0x85U
#define XX_MBR_TYPE_GPT_PROTECTIVE 0xEEU

typedef struct xx_mbr_entry_s {
    char *name;
    int64_t header_offset; /**< The 16-byte table entry describing this. */
    int64_t header_size;
    int64_t data_offset;
    int64_t data_size;      /**< Bytes actually present on the device. */
    uint64_t declared_size; /**< sector_count * 512. */
    uint32_t start_lba;     /**< Absolute, already rebased for logicals. */
    uint32_t sector_count;
    uint8_t type;
    uint8_t status;
    bool is_logical;
} xx_mbr_entry;

typedef struct xx_mbr_private_s {
    xx_mbr_entry *entries;
    size_t count;
    size_t capacity;
    /* Visited EBR sector offsets. The cap is small and fixed, so a linear
     * scan is cheaper than the hash set a deep tree would need. */
    int64_t visited[XX_MBR_MAX_EBR_STEPS];
    size_t visited_count;
    int64_t input_size;
    int64_t archive_end;
    uint64_t primary_count;
    uint64_t logical_count;
    uint64_t extended_count;
    uint32_t disk_signature;
    bool is_protective;
} xx_mbr_private;

typedef struct xx_mbr_archive_stream_s {
    xx_mbr_private parsed;
    size_t index;
} xx_mbr_archive_stream;

static void xx_mbr_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------------ */
/* Small helpers                                                       */
/* ------------------------------------------------------------------ */

/* Read exactly size bytes at an absolute device offset. xx_io_seek64() is
 * used deliberately: long is 32-bit on Win64 and a disk image is routinely
 * larger than 2 GiB, so the legacy seek would silently cap the reader. */
static bool xx_mbr_read_at(xx_io_device *device, int64_t offset, void *data,
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
static bool xx_mbr_range_within(int64_t total_size, int64_t offset,
                                int64_t size) {
    return (total_size >= 0) && (offset >= 0) && (size >= 0) &&
           (offset <= total_size) && (size <= total_size - offset);
}

/* Turn an absolute LBA into a device offset, refusing anything that would
 * not survive the multiplication or land outside the device. */
static bool xx_mbr_lba_to_offset(int64_t base_address, uint64_t lba,
                                 int64_t *result) {
    uint64_t bytes;
    if (!result || base_address < 0) return false;
    if (lba > (uint64_t)INT64_MAX / XX_MBR_SECTOR_SIZE) return false;
    bytes = lba * XX_MBR_SECTOR_SIZE;
    if (bytes > (uint64_t)(INT64_MAX - base_address)) return false;
    *result = base_address + (int64_t)bytes;
    return true;
}

/* Build a record name of the form "partition12". xx_str_* has no formatter
 * and the CRT is off limits, so the digits are laid down by hand. */
static char *xx_mbr_make_name(unsigned index) {
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

const char *xx_mbr_type_name(uint8_t type) {
    /* The common types only. An unrecognised byte is still published - the
     * caller recurses into the payload and lets the format detector decide -
     * so this table is presentation, not policy. */
    switch (type) {
        case 0x00U: return "Empty";
        case 0x01U: return "FAT12";
        case 0x04U: return "FAT16 <32M";
        case 0x05U: return "Extended (CHS)";
        case 0x06U: return "FAT16";
        case 0x07U: return "NTFS/exFAT/HPFS";
        case 0x0BU: return "FAT32 (CHS)";
        case 0x0CU: return "FAT32 (LBA)";
        case 0x0EU: return "FAT16 (LBA)";
        case 0x0FU: return "Extended (LBA)";
        case 0x11U: return "Hidden FAT12";
        case 0x14U: return "Hidden FAT16 <32M";
        case 0x16U: return "Hidden FAT16";
        case 0x17U: return "Hidden NTFS/HPFS";
        case 0x1BU: return "Hidden FAT32";
        case 0x1CU: return "Hidden FAT32 (LBA)";
        case 0x1EU: return "Hidden FAT16 (LBA)";
        case 0x27U: return "Windows Recovery";
        case 0x39U: return "Plan 9";
        case 0x3CU: return "PartitionMagic recovery";
        case 0x41U: return "PReP boot";
        case 0x42U: return "Windows dynamic (LDM)";
        case 0x43U: return "Linux";
        case 0x4DU: return "QNX4 primary";
        case 0x4EU: return "QNX4 secondary";
        case 0x4FU: return "QNX4 tertiary";
        case 0x63U: return "UNIX System V";
        case 0x7FU: return "Alt OS development";
        case 0x80U: return "Minix (old)";
        case 0x81U: return "Minix";
        case 0x82U: return "Linux swap";
        case 0x83U: return "Linux";
        case 0x84U: return "Hibernation";
        case 0x85U: return "Extended (Linux)";
        case 0x86U: return "NTFS volume set";
        case 0x87U: return "NTFS volume set";
        case 0x88U: return "Linux plaintext";
        case 0x8EU: return "Linux LVM";
        case 0x96U: return "ISO-9660";
        case 0xA0U: return "Laptop hibernation";
        case 0xA5U: return "FreeBSD";
        case 0xA6U: return "OpenBSD";
        case 0xA8U: return "Apple UFS";
        case 0xA9U: return "NetBSD";
        case 0xABU: return "Apple boot";
        case 0xAFU: return "Apple HFS/HFS+";
        case 0xB1U: return "QNX6";
        case 0xB2U: return "QNX6";
        case 0xB3U: return "QNX6";
        case 0xBEU: return "Solaris boot";
        case 0xBFU: return "Solaris";
        case 0xE8U: return "LUKS";
        case 0xEBU: return "BeOS BFS";
        case 0xEEU: return "GPT protective";
        case 0xEFU: return "EFI System";
        case 0xFBU: return "VMware VMFS";
        case 0xFCU: return "VMware swap";
        case 0xFDU: return "Linux RAID auto";
        default: return "Unknown";
    }
}

static bool xx_mbr_is_extended_type(uint8_t type) {
    return type == XX_MBR_TYPE_EXTENDED_CHS ||
           type == XX_MBR_TYPE_EXTENDED_LBA ||
           type == XX_MBR_TYPE_EXTENDED_LINUX;
}

/* ------------------------------------------------------------------ */
/* Sector plausibility                                                 */
/* ------------------------------------------------------------------ */

/* The 0x55 0xAA trailer is not an MBR signature; it marks a bootable sector,
 * and a FAT or NTFS boot sector carries it too. Those sectors begin with a
 * short jump to the boot code followed by a BIOS Parameter Block, and their
 * bytes 446..509 - where the partition table would live - are boot code,
 * which occasionally passes a purely structural table check. Reject anything
 * that carries a credible BPB so the filesystem reader keeps its own images.
 */
static bool xx_mbr_looks_like_boot_sector(const uint8_t *sector) {
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint8_t media;
    uint8_t number_of_fats;
    /* An explicit filesystem name settles it without any guessing. */
    if (xx_rt_memcmp(sector + 3, "NTFS    ", 8U) == 0 ||
        xx_rt_memcmp(sector + 3, "EXFAT   ", 8U) == 0 ||
        xx_rt_memcmp(sector + 54, "FAT12   ", 8U) == 0 ||
        xx_rt_memcmp(sector + 54, "FAT16   ", 8U) == 0 ||
        xx_rt_memcmp(sector + 54, "FAT     ", 8U) == 0 ||
        xx_rt_memcmp(sector + 82, "FAT32   ", 8U) == 0) {
        return true;
    }
    /* Otherwise require the jump plus a BPB whose every field is legal. */
    if (!(sector[0] == 0xEBU && sector[2] == 0x90U) && sector[0] != 0xE9U) {
        return false;
    }
    bytes_per_sector = (uint16_t)(sector[11] | ((uint16_t)sector[12] << 8));
    sectors_per_cluster = sector[13];
    number_of_fats = sector[16];
    media = sector[21];
    if (bytes_per_sector != 512U && bytes_per_sector != 1024U &&
        bytes_per_sector != 2048U && bytes_per_sector != 4096U) {
        return false;
    }
    /* Powers of two from 1 to 128; NTFS also encodes 2^-n as 0xF1..0xFF. */
    if (sectors_per_cluster == 0U ||
        (sectors_per_cluster & (uint8_t)(sectors_per_cluster - 1U)) != 0U) {
        return false;
    }
    if (media < 0xF0U) return false;
    /* NTFS and exFAT leave the FAT count zero; FAT uses 1 or 2. */
    if (number_of_fats > 2U) return false;
    return true;
}

/* ------------------------------------------------------------------ */
/* Entry bookkeeping                                                   */
/* ------------------------------------------------------------------ */

static void xx_mbr_private_cleanup(xx_mbr_private *parsed) {
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

static bool xx_mbr_append_entry(xx_mbr_private *parsed, xx_mbr_entry *entry) {
    xx_mbr_entry *grown;
    size_t capacity;
    if (!parsed || !entry || !entry->name ||
        parsed->count >= XX_MBR_MAX_PARTITIONS) {
        return false;
    }
    if (parsed->count == parsed->capacity) {
        capacity = parsed->capacity ? parsed->capacity * 2U : 8U;
        if (capacity < parsed->count ||
            capacity > SIZE_MAX / sizeof(*parsed->entries)) return false;
        grown = (xx_mbr_entry *)xx_mem_realloc(
            parsed->entries, capacity * sizeof(*parsed->entries));
        if (!grown) return false;
        parsed->entries = grown;
        parsed->capacity = capacity;
    }
    parsed->entries[parsed->count++] = *entry;
    xx_mem_zero(entry, sizeof(*entry));
    return true;
}

/* Record an EBR sector offset, reporting whether it was already on the
 * chain. A full table is reported as "seen" so the walk stops rather than
 * continuing with a set that can no longer remember anything. */
static bool xx_mbr_visited_mark(xx_mbr_private *parsed, int64_t offset) {
    size_t index;
    if (!parsed) return true;
    for (index = 0U; index < parsed->visited_count; ++index) {
        if (parsed->visited[index] == offset) return true;
    }
    if (parsed->visited_count >= XX_MBR_MAX_EBR_STEPS) return true;
    parsed->visited[parsed->visited_count++] = offset;
    return false;
}

/* Build one publishable partition from a decoded table entry. The declared
 * range is clamped to what the device actually holds, because truncated disk
 * dumps are common and the leading partitions of one are still usable; the
 * untruncated figure stays available in declared_size. Returns false only on
 * a range that cannot be published at all. */
static bool xx_mbr_make_entry(xx_mbr_private *parsed, int64_t base_address,
                              int64_t table_entry_offset, uint64_t start_lba,
                              uint32_t sector_count, uint8_t type,
                              uint8_t status, bool is_logical,
                              unsigned name_index) {
    xx_mbr_entry entry;
    int64_t offset;
    uint64_t declared;
    int64_t available;
    if (!parsed || start_lba == 0U || sector_count == 0U) return false;
    if (!xx_mbr_lba_to_offset(base_address, start_lba, &offset)) return false;
    if (offset >= parsed->input_size) return false;
    declared = (uint64_t)sector_count * XX_MBR_SECTOR_SIZE;
    available = parsed->input_size - offset;
    if (declared < (uint64_t)available) available = (int64_t)declared;
    xx_mem_zero(&entry, sizeof(entry));
    entry.name = xx_mbr_make_name(name_index);
    if (!entry.name) return false;
    entry.header_offset = table_entry_offset;
    entry.header_size = XX_MBR_ENTRY_SIZE;
    entry.data_offset = offset;
    entry.data_size = available;
    entry.declared_size = declared;
    entry.start_lba = (uint32_t)start_lba;
    entry.sector_count = sector_count;
    entry.type = type;
    entry.status = status;
    entry.is_logical = is_logical;
    if (!xx_mbr_append_entry(parsed, &entry)) {
        xx_str_free(entry.name);
        return false;
    }
    if (offset + available > parsed->archive_end) {
        parsed->archive_end = offset + available;
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* The EBR chain                                                       */
/* ------------------------------------------------------------------ */

/* Follow the linked list of Extended Boot Records inside one extended
 * partition. A malformed link ends the chain rather than the whole parse:
 * the primaries and the logicals found so far stay usable, which is the
 * behaviour a partially corrupt image needs. */
static void xx_mbr_walk_ebr_chain(Abstractformat *self, xx_mbr_private *parsed,
                                  uint64_t extended_start_lba,
                                  uint64_t extended_sector_count,
                                  xx_pd_struct *pd) {
    uint64_t current_lba = extended_start_lba;
    uint64_t extended_end_lba = extended_start_lba + extended_sector_count;
    unsigned steps;
    for (steps = 0U; steps < XX_MBR_MAX_EBR_STEPS; ++steps) {
        uint8_t sector[XX_MBR_SECTOR_SIZE];
        int64_t sector_offset;
        const uint8_t *first;
        const uint8_t *second;
        uint8_t first_type;
        uint8_t first_status;
        uint32_t first_start;
        uint32_t first_count;
        uint8_t second_type;
        uint32_t second_start;
        uint64_t logical_lba;

        if (pd && xx_pd_is_stopped(pd)) return;
        if (parsed->logical_count >= XX_MBR_MAX_LOGICAL) return;
        if (current_lba < extended_start_lba ||
            current_lba >= extended_end_lba) {
            return;
        }
        if (!xx_mbr_lba_to_offset(self->base_address, current_lba,
                                  &sector_offset) ||
            !xx_mbr_range_within(parsed->input_size, sector_offset,
                                 XX_MBR_SECTOR_SIZE)) {
            return;
        }
        /* The cycle guard. An EBR that names itself, or any longer loop,
         * is caught here before it can be walked twice. */
        if (xx_mbr_visited_mark(parsed, sector_offset)) return;
        if (!xx_mbr_read_at(self->device, sector_offset, sector,
                            sizeof(sector))) {
            return;
        }
        if (sector[XX_MBR_SIGNATURE_OFFSET] != 0x55U ||
            sector[XX_MBR_SIGNATURE_OFFSET + 1] != 0xAAU) {
            return;
        }
        first = sector + XX_MBR_TABLE_OFFSET;
        second = first + XX_MBR_ENTRY_SIZE;
        first_status = first[0];
        first_type = first[4];
        first_start = xx_data_get_u32(first, XX_MBR_ENTRY_SIZE, 8U, false);
        first_count = xx_data_get_u32(first, XX_MBR_ENTRY_SIZE, 12U, false);
        second_type = second[4];
        second_start = xx_data_get_u32(second, XX_MBR_ENTRY_SIZE, 8U, false);

        /* Entry 0 is the logical partition, addressed relative to this EBR
         * sector. Its status field is advisory here; an implausible one only
         * disqualifies the entry. */
        if (first_type != 0U && first_count != 0U &&
            (first_status == 0x00U || first_status == 0x80U) &&
            !xx_mbr_is_extended_type(first_type)) {
            logical_lba = current_lba + first_start;
            if (first_start != 0U && logical_lba >= current_lba &&
                logical_lba < extended_end_lba &&
                (uint64_t)first_count <= extended_end_lba - logical_lba) {
                if (xx_mbr_make_entry(
                        parsed, self->base_address,
                        sector_offset + XX_MBR_TABLE_OFFSET, logical_lba,
                        first_count, first_type, first_status, true,
                        (unsigned)(XX_MBR_PRIMARY_COUNT + 1U +
                                   parsed->logical_count))) {
                    ++parsed->logical_count;
                }
            }
        }

        /* Entry 1 links on, and its first-sector field is relative to the
         * start of the extended partition rather than to this EBR. */
        if (!xx_mbr_is_extended_type(second_type) || second_start == 0U) return;
        if ((uint64_t)second_start >= extended_sector_count) return;
        current_lba = extended_start_lba + second_start;
    }
}

/* ------------------------------------------------------------------ */
/* Parsing                                                             */
/* ------------------------------------------------------------------ */

static bool xx_mbr_parse(Abstractformat *self, xx_mbr_private *parsed,
                         xx_pd_struct *pd) {
    uint8_t sector[XX_MBR_SECTOR_SIZE];
    int64_t total_size;
    unsigned index;
    unsigned candidates = 0U;
    unsigned protective = 0U;
    uint8_t types[XX_MBR_PRIMARY_COUNT];
    uint8_t statuses[XX_MBR_PRIMARY_COUNT];
    uint32_t starts[XX_MBR_PRIMARY_COUNT];
    uint32_t counts[XX_MBR_PRIMARY_COUNT];
    /* Initialise before the guard clause: callers such as
     * xx_mbr_check_is_valid() run xx_mbr_private_cleanup() on their stack
     * copy whatever this returns. */
    if (parsed) {
        xx_mem_zero(parsed, sizeof(*parsed));
        parsed->input_size = -1;
        parsed->archive_end = -1;
    }
    if (!self || !self->device || !parsed || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd))) return false;
    total_size = xx_io_total_size(self->device);
    /* A partition table that describes nothing beyond its own sector is not
     * a partition table; require at least one further sector to exist. */
    if (!xx_mbr_range_within(total_size, self->base_address,
                             2 * XX_MBR_SECTOR_SIZE) ||
        !xx_mbr_read_at(self->device, self->base_address, sector,
                        sizeof(sector))) {
        goto fail;
    }
    if (sector[XX_MBR_SIGNATURE_OFFSET] != 0x55U ||
        sector[XX_MBR_SIGNATURE_OFFSET + 1] != 0xAAU) {
        goto fail;
    }
    if (xx_mbr_looks_like_boot_sector(sector)) goto fail;
    parsed->input_size = total_size;
    parsed->archive_end = self->base_address + XX_MBR_SECTOR_SIZE;
    parsed->disk_signature = xx_data_get_u32(sector, sizeof(sector),
                                             XX_MBR_DISK_SIGNATURE_OFFSET,
                                             false);

    /* Pass one: decode and sanity-check the whole table before publishing
     * anything, so that one bogus status byte rejects the image rather than
     * yielding a half-credible partition list. */
    for (index = 0U; index < XX_MBR_PRIMARY_COUNT; ++index) {
        const uint8_t *raw = sector + XX_MBR_TABLE_OFFSET +
                             index * XX_MBR_ENTRY_SIZE;
        statuses[index] = raw[0];
        types[index] = raw[4];
        starts[index] = xx_data_get_u32(raw, XX_MBR_ENTRY_SIZE, 8U, false);
        counts[index] = xx_data_get_u32(raw, XX_MBR_ENTRY_SIZE, 12U, false);
        if (types[index] == 0U && counts[index] == 0U && starts[index] == 0U) {
            continue; /* An unused slot; it need not be zero-filled. */
        }
        if (statuses[index] != 0x00U && statuses[index] != 0x80U) goto fail;
        if (types[index] == 0U || counts[index] == 0U || starts[index] == 0U) {
            goto fail;
        }
        ++candidates;
        if (types[index] == XX_MBR_TYPE_GPT_PROTECTIVE) ++protective;
    }
    if (candidates == 0U) goto fail;

    /* A protective MBR guards a GPT disk. Its single 0xEE entry covers the
     * whole medium and holds no filesystem, so nothing is published for it;
     * the caller learns what it is from xx_mbr_is_protective(). */
    if (protective == candidates) {
        parsed->is_protective = true;
        return true;
    }

    /* Pass two: publish. */
    for (index = 0U; index < XX_MBR_PRIMARY_COUNT; ++index) {
        int64_t table_entry_offset = self->base_address + XX_MBR_TABLE_OFFSET +
                                     index * XX_MBR_ENTRY_SIZE;
        if (types[index] == 0U || counts[index] == 0U || starts[index] == 0U) {
            continue;
        }
        if (xx_mbr_is_extended_type(types[index])) {
            /* The container itself holds no filesystem and is not published;
             * its logicals are. */
            ++parsed->extended_count;
            xx_mbr_walk_ebr_chain(self, parsed, starts[index], counts[index],
                                  pd);
            continue;
        }
        if (types[index] == XX_MBR_TYPE_GPT_PROTECTIVE) {
            /* A 0xEE entry mixed in with real partitions is a hybrid MBR.
             * The GPT reader covers that region; skip it here. */
            continue;
        }
        if (xx_mbr_make_entry(parsed, self->base_address, table_entry_offset,
                              starts[index], counts[index], types[index],
                              statuses[index], false, index + 1U)) {
            ++parsed->primary_count;
        }
    }
    if (parsed->count == 0U) goto fail;
    return true;
fail:
    xx_mbr_private_cleanup(parsed);
    return false;
}

/* ------------------------------------------------------------------ */
/* Archive record plumbing                                             */
/* ------------------------------------------------------------------ */

static bool xx_mbr_copy_options(xx_list_s *destination,
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

static const xx_var *xx_mbr_find_option(const xx_list_s *options,
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

/* A partition record carries its payload verbatim, so the compressed and
 * uncompressed sizes are the bytes present on the device; the untruncated
 * figure from the table goes in ATTRIBUTES' company below. The type byte is
 * published as ATTRIBUTES and the status byte as FLAGS, with the readable
 * type name as the comment. */
static bool xx_mbr_populate_record(xx_archive_record *record,
                                   const xx_mbr_entry *entry) {
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
                                          entry->type) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_FLAGS,
                                          entry->status) &&
           xx_archive_record_set_meta_str(record, XX_META_ID_COMMENT,
                                          xx_mbr_type_name(entry->type)) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

static void xx_mbr_archive_stream_free(void *pointer) {
    xx_mbr_archive_stream *stream = (xx_mbr_archive_stream *)pointer;
    if (!stream) return;
    xx_mbr_private_cleanup(&stream->parsed);
    xx_mem_free(stream);
}

/* The record names are generated here, never taken from the image, so this
 * only has to refuse the impossible rather than sanitise hostile input. */
static bool xx_mbr_safe_name(const char *name) {
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

void xx_mbr_init(xx_mbr *mbr, xx_io_device *dev, int64_t base_address) {
    if (!mbr) return;
    xx_mem_zero(mbr, sizeof(*mbr));
    xx_format_init(&mbr->format, dev, base_address);
    mbr->format.endian = XX_ENDIAN_LITTLE;
    mbr->format.file_type = XX_MBR_FILE_TYPE;
    mbr->format.format_type = XX_TYPE_ARCHIVE;
    mbr->format.is_archive = true;
    xx_format_set_mime_type(&mbr->format, "application/x-mbr");
    xx_format_set_extension(&mbr->format, "img");
    mbr->format.check_is_valid = xx_mbr_check_is_valid;
    mbr->format.handle_base_info = xx_mbr_handle_base_info;
    mbr->format.get_format_size = xx_mbr_get_format_size;
    mbr->format.get_number_of_archive_records =
        xx_mbr_get_number_of_archive_records;
    mbr->format.create_archive_records_reading =
        xx_mbr_create_archive_records_reading;
    mbr->format.get_current_archive_record = xx_mbr_get_current_archive_record;
    mbr->format.unpack_current_archive_record =
        xx_mbr_unpack_current_archive_record;
    mbr->format.archive_record_move_to_next = xx_mbr_archive_record_move_to_next;
    mbr->format.free_archive_records_reading =
        xx_mbr_free_archive_records_reading;
    mbr->format.destroy = xx_mbr_vtable_destroy;
    mbr->archive_end = -1;
}

xx_mbr *xx_mbr_create(xx_io_device *dev, int64_t base_address) {
    xx_mbr *mbr = (xx_mbr *)xx_mem_alloc(sizeof(*mbr));
    if (mbr) xx_mbr_init(mbr, dev, base_address);
    return mbr;
}

void xx_mbr_destroy(xx_mbr *mbr) {
    if (!mbr) return;
    if (mbr->internal) {
        xx_mbr_private_cleanup((xx_mbr_private *)mbr->internal);
        xx_mem_free(mbr->internal);
        mbr->internal = NULL;
    }
    xx_format_cleanup_extra_parameters(&mbr->format);
}

static void xx_mbr_vtable_destroy(Abstractformat *self) {
    xx_mbr_destroy((xx_mbr *)self);
}

void xx_mbr_free(xx_mbr *mbr) {
    if (!mbr) return;
    xx_mbr_destroy(mbr);
    xx_mem_free(mbr);
}

bool xx_mbr_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_mbr_private parsed;
    bool result = xx_mbr_parse(self, &parsed, pd);
    xx_mbr_private_cleanup(&parsed);
    return result;
}

bool xx_mbr_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_mbr_private *parsed;
    xx_mbr *mbr = (xx_mbr *)self;
    int64_t total_size;
    if (!self || !mbr) return false;
    parsed = (xx_mbr_private *)xx_mem_alloc(sizeof(*parsed));
    if (!parsed || !xx_mbr_parse(self, parsed, pd)) {
        if (parsed) xx_mem_free(parsed);
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    if (mbr->internal) {
        xx_mbr_private_cleanup((xx_mbr_private *)mbr->internal);
        xx_mem_free(mbr->internal);
    }
    mbr->internal = parsed;
    mbr->number_of_records = parsed->count;
    mbr->number_of_members = parsed->count;
    mbr->number_of_primary = parsed->primary_count;
    mbr->number_of_logical = parsed->logical_count;
    mbr->number_of_extended = parsed->extended_count;
    mbr->disk_signature = parsed->disk_signature;
    mbr->is_protective = parsed->is_protective;
    mbr->archive_end = parsed->archive_end;
    xx_format_set_version(self, parsed->is_protective ? "protective" : "1");
    /* The format spans the boot sector plus everything the table claims; a
     * disk usually ends there, and anything past it is overlay. */
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

int64_t xx_mbr_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self || (!self->base_info_handled &&
                  !xx_format_handle_base_info(self, pd))) return -1;
    return self->format_size;
}

uint64_t xx_mbr_get_number_of_archive_records(Abstractformat *self,
                                              xx_pd_struct *pd) {
    if (!self || (!self->base_info_handled &&
                  !xx_format_handle_base_info(self, pd))) return 0U;
    return ((xx_mbr *)self)->number_of_records;
}

xx_archive_record_state *xx_mbr_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_record_state *state;
    xx_mbr_archive_stream *stream;
    if (!self || !self->device ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    stream = (xx_mbr_archive_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!state || !stream) {
        if (state) xx_mem_free(state);
        if (stream) xx_mem_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    if (!xx_mbr_copy_options(&state->options, options) ||
        !xx_mbr_parse(self, &stream->parsed, pd)) {
        xx_mbr_archive_stream_free(stream);
        xx_archive_record_state_free(state);
        return NULL;
    }
    stream->index = 0U;
    state->internal_state = stream;
    state->free_internal = xx_mbr_archive_stream_free;
    state->total_records = (int64_t)stream->parsed.count;
    if (stream->parsed.count != 0U &&
        xx_mbr_populate_record(&state->current_record,
                               &stream->parsed.entries[0])) {
        state->has_record = true;
        state->current_index = 0;
    }
    return state;
}

const xx_archive_record *xx_mbr_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record : NULL;
}

bool xx_mbr_archive_record_move_to_next(Abstractformat *self,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    xx_mbr_archive_stream *stream;
    if (!self || !state || state->format != self || !state->has_record ||
        !state->internal_state || (pd && xx_pd_is_stopped(pd))) return false;
    stream = (xx_mbr_archive_stream *)state->internal_state;
    ++stream->index;
    if (stream->index >= stream->parsed.count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    if (!xx_mbr_populate_record(&state->current_record,
                                &stream->parsed.entries[stream->index])) {
        state->has_record = false;
        return false;
    }
    ++state->current_index;
    return true;
}

bool xx_mbr_unpack_current_archive_record(Abstractformat *self,
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
    if (!xx_mbr_safe_name(name)) return false;
    option = xx_mbr_find_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
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

void xx_mbr_free_archive_records_reading(Abstractformat *self,
                                         xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

uint64_t xx_mbr_get_number_of_records(const xx_mbr *mbr) {
    return mbr ? mbr->number_of_records : 0U;
}
uint64_t xx_mbr_get_number_of_members(const xx_mbr *mbr) {
    return mbr ? mbr->number_of_members : 0U;
}
uint32_t xx_mbr_get_disk_signature(const xx_mbr *mbr) {
    return mbr ? mbr->disk_signature : 0U;
}
bool xx_mbr_is_protective(const xx_mbr *mbr) {
    return mbr ? mbr->is_protective : false;
}
int64_t xx_mbr_get_archive_end(const xx_mbr *mbr) {
    return mbr ? mbr->archive_end : -1;
}

bool xx_mbr_get_partition_info(const xx_mbr *mbr, uint64_t index,
                               xx_mbr_partition_info *info) {
    const xx_mbr_private *parsed;
    const xx_mbr_entry *entry;
    if (!mbr || !info || !mbr->internal) return false;
    parsed = (const xx_mbr_private *)mbr->internal;
    if (index >= parsed->count) return false;
    entry = &parsed->entries[index];
    info->offset = entry->data_offset;
    info->size = entry->data_size;
    info->declared_size = entry->declared_size;
    info->start_lba = entry->start_lba;
    info->sector_count = entry->sector_count;
    info->type = entry->type;
    info->status = entry->status;
    info->is_logical = entry->is_logical;
    info->type_name = xx_mbr_type_name(entry->type);
    info->name = entry->name;
    return true;
}
