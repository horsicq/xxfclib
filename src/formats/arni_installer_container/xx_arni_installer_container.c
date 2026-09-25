/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * ARNI installer container: the "ARNI" LZHUF record chain that the mIRC
 * setup stub keeps in one of its RCDATA resources.  The record layout is in
 * xx_arni_installer_container.h.
 *
 * Sources.  The container rules (the record header, the end record, the
 * packed extent running to the next header, the decoded-size bound and the
 * recovery of member names from the stub's file-name pool) follow XArchive
 * installers/xarnisfx.{h,cpp} (MIT, Copyright (c) 2026
 * hors<horsicq@gmail.com>); the name-pool scan below is a C port of its
 * collectNameTable()/isPlainFileName(), the rest is a C rewrite, not a
 * transliteration.  The codec is the library's own xx_lzhuf_decode_memory(),
 * the parameter set SBX and ZTC already use.  U3 served as the extraction
 * oracle only.
 *
 * Where this differs from the references.  They find the chain by searching
 * the whole executable for the first plausible "ARNI" tag, which is why they
 * must refuse a tag followed by "NG" (the stub's own "WARNING" text).  This
 * reader parses the PE resource directory instead and only ever starts a
 * chain at the first byte of an RCDATA resource, bounded by that resource, so
 * text in the stub is never reached and the "NG" rule is not applied: inside
 * the resource it could only refuse a genuine member whose decoded size has
 * 0x474E in its low word.
 *
 * Only the MZ header, the PE headers, the section table and the resource
 * directory are read to find the payload; nothing in the stub is executed or
 * emulated.
 *
 * Costs.  check_is_valid() reads the DOS and PE headers, the section table,
 * the resource directory down to the RCDATA leaves and ten bytes at each of
 * them.  Only a leaf that opens with a record header is walked, which reads
 * that resource once, front to back.  No member is decoded until unpacked.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/arni_installer_container/xx_arni_installer_container.h"

#include "xxfclib/algo/lzhuf/xx_lzhuf.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

/* Registration placeholder: xxfc_defs.h is shared and not edited from here,
 * so the alias macro that sits next to the enumerator is tested instead. */
#ifdef ARNI_INSTALLER_CONTAINER
#define XX_ARNI_INSTALLER_CONTAINER_FILE_TYPE \
    XX_FILE_TYPE_ARNI_INSTALLER_CONTAINER
#else
#define XX_ARNI_INSTALLER_CONTAINER_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

/* ---------------------------------------------------------------------- */
/* Constants                                                               */

/* Records. */
#define ARNI_HEADER XX_ARNI_INSTALLER_CONTAINER_HEADER_SIZE
#define ARNI_PROBE XX_ARNI_INSTALLER_CONTAINER_END_SIZE
/* One header, one packed byte and the end record. */
#define ARNI_MIN_CONTAINER (ARNI_HEADER + 1 + ARNI_PROBE)
#define ARNI_MAX_SIZE ((int64_t)XX_ARNI_INSTALLER_CONTAINER_MAX_MEMBER)

#define ARNI_KIND_NONE 0
#define ARNI_KIND_MEMBER 1
#define ARNI_KIND_END 2

/* Carrier. */
#define ARNI_DOS_HEADER 0x40
#define ARNI_MAX_LFANEW 0x10000
#define ARNI_PE_HEADER 24
#define ARNI_OPTIONAL_MAX 0x1000
#define ARNI_MAX_SECTIONS 96
#define ARNI_SECTION_SIZE 40
#define ARNI_RT_RCDATA 10U
#define ARNI_DIR_SIZE 16
#define ARNI_ENTRY_SIZE 8
#define ARNI_DATA_ENTRY_SIZE 16

/* Ceilings.  The five reference carriers hold 10 or 11 members in a resource
 * of at most 1.04 MB and have one or two RCDATA entries; the limits only stop
 * a corrupt carrier from asking for unbounded work or memory. */
#define ARNI_MAX_MEMBERS 4096U
#define ARNI_MAX_ENTRY_READS 4096U /* resource directory entries per locate */
#define ARNI_MAX_WALKS 4U          /* RCDATA leaves walked per locate */
#define ARNI_MAX_CONTAINER INT64_C(0x10000000)
#define ARNI_SCAN_BUDGET INT64_C(0x20000000) /* bytes scanned per locate */
#define ARNI_SCAN_CHUNK 0x10000
/* An LZHUF match costs at least one code bit and nine position bits and
 * yields at most 60 bytes, so no stream expands by more than 48 to 1. */
#define ARNI_MAX_RATIO 48
#define ARNI_RATIO_SLACK 64

/* Name pool.  Every reference stub is under 100 KiB; the ceiling only keeps
 * the scan off the size of an unrelated executable.  A run of fewer than
 * three names is too easy to hit by accident to be trusted. */
#define ARNI_MAX_STUB_SCAN INT64_C(0x400000)
#define ARNI_MIN_NAMES 3U
#define ARNI_NAME_MIN 3U
#define ARNI_NAME_MAX 64U
#define ARNI_NAME_BUFFER 72U
#define ARNI_MAX_IMPORTS 256U
#define ARNI_IMPORT_DESCRIPTOR 20

static const uint8_t arni_tag[4] = {'A', 'R', 'N', 'I'};

/* ---------------------------------------------------------------------- */
/* Byte helpers                                                            */

static uint32_t arni_le16(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U);
}

static uint32_t arni_le32(const uint8_t *bytes) {
    return arni_le16(bytes) | (arni_le16(bytes + 2U) << 16U);
}

static bool arni_read_at(xx_io_device *device, int64_t offset, void *buffer,
                         size_t size) {
    size_t done = 0U;
    if (!device || (!buffer && size != 0U) || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0)
        return false;
    while (done < size) {
        ssize_t amount =
            xx_io_read(device, (uint8_t *)buffer + done, size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

/* What the ten bytes at @p bytes are: a member header (its decoded size in
 * @p size), the end record, or neither. */
static int arni_classify(const uint8_t *bytes, int64_t *size) {
    int64_t value;
    if (bytes[0] != 'A' || xx_rt_memcmp(bytes, arni_tag, 4U) != 0)
        return ARNI_KIND_NONE;
    if (xx_rt_memcmp(bytes + 4, arni_tag, 4U) == 0)
        return (bytes[8] == 0x0DU && bytes[9] == 0x0AU) ? ARNI_KIND_END
                                                        : ARNI_KIND_NONE;
    /* Signed on purpose, as the references read it: a size with the top bit
     * set is corrupt, not a two-gigabyte member. */
    value = (int64_t)(int32_t)arni_le32(bytes + 4);
    if (value <= 0 || value >= ARNI_MAX_SIZE) return ARNI_KIND_NONE;
    if (size) *size = value;
    return ARNI_KIND_MEMBER;
}

/* ---------------------------------------------------------------------- */
/* Scanning window                                                         */

/* A read-ahead window over [0, end) of the carrier; every byte it loads is
 * charged to the locate's shared budget. */
typedef struct arni_window_s {
    xx_io_device *device;
    int64_t base;
    int64_t end;
    int64_t start;
    int64_t length;
    int64_t *budget;
    uint8_t *data;
} arni_window;

static bool arni_window_open(arni_window *window, xx_io_device *device,
                             int64_t base, int64_t end, int64_t *budget) {
    xx_mem_zero(window, sizeof(*window));
    window->device = device;
    window->base = base;
    window->end = end;
    window->budget = budget;
    window->start = -1;
    window->data = (uint8_t *)xx_mem_alloc(ARNI_SCAN_CHUNK + ARNI_PROBE);
    return window->data != NULL;
}

static void arni_window_close(arni_window *window) {
    if (window->data) xx_mem_free(window->data);
    window->data = NULL;
}

/* Make [offset, offset + need) resident; false past the end or the budget. */
static bool arni_window_load(arni_window *window, int64_t offset,
                             int64_t need) {
    int64_t length;
    if (offset < 0 || need <= 0 || offset > window->end - need) return false;
    if (window->start >= 0 && offset >= window->start &&
        offset + need <= window->start + window->length)
        return true;
    length = window->end - offset;
    if (length > ARNI_SCAN_CHUNK + ARNI_PROBE)
        length = ARNI_SCAN_CHUNK + ARNI_PROBE;
    if (*window->budget < length) return false;
    *window->budget -= length;
    window->start = -1;
    if (!arni_read_at(window->device, window->base + offset, window->data,
                      (size_t)length))
        return false;
    window->start = offset;
    window->length = length;
    return true;
}

/* The first offset at or after @p from that holds a member header or the end
 * record; -1 when there is none before the window's end. */
static int64_t arni_find_header(arni_window *window, int64_t from,
                                xx_pd_struct *pd) {
    int64_t position = from;
    while (position <= window->end - ARNI_PROBE) {
        const uint8_t *bytes;
        int64_t last;
        if ((pd && xx_pd_is_stopped(pd)) ||
            !arni_window_load(window, position, ARNI_PROBE))
            return -1;
        bytes = window->data + (position - window->start);
        last = window->start + window->length - ARNI_PROBE;
        for (; position <= last; ++position, ++bytes) {
            if (bytes[0] == 'A' &&
                arni_classify(bytes, NULL) != ARNI_KIND_NONE)
                return position;
        }
    }
    return -1;
}

/* ---------------------------------------------------------------------- */
/* Member table                                                            */

typedef struct arni_member_s {
    int64_t header_offset; /**< Base-relative offset of the tag. */
    int64_t data_offset;   /**< Base-relative offset of the packed bytes. */
    int64_t packed_size;
    int64_t unpacked_size;
    char name[ARNI_NAME_BUFFER];
} arni_member;

typedef struct arni_table_s {
    arni_member *items;
    size_t count;
    size_t capacity;
    size_t index;
    uint64_t unpacked_total;
    int64_t container_offset;
    int64_t container_size;
    int64_t chain_end;
    int64_t image_end;
    bool names_recovered;
} arni_table;

static void arni_table_free(arni_table *table) {
    if (!table) return;
    if (table->items) xx_mem_free(table->items);
    xx_mem_free(table);
}

static void arni_table_free_opaque(void *opaque) {
    arni_table_free((arni_table *)opaque);
}

static bool arni_table_append(arni_table *table, int64_t header,
                              int64_t data, int64_t packed,
                              int64_t unpacked) {
    arni_member *member;
    if (table->count >= ARNI_MAX_MEMBERS) return false;
    if (table->count == table->capacity) {
        size_t capacity = table->capacity ? table->capacity * 2U : 16U;
        arni_member *grown = (arni_member *)xx_mem_realloc(
            table->items, capacity * sizeof(*grown));
        if (!grown) return false;
        table->items = grown;
        table->capacity = capacity;
    }
    member = &table->items[table->count++];
    xx_mem_zero(member, sizeof(*member));
    member->header_offset = header;
    member->data_offset = data;
    member->packed_size = packed;
    member->unpacked_size = unpacked;
    table->unpacked_total += (uint64_t)unpacked;
    return true;
}

/* ---------------------------------------------------------------------- */
/* Chain                                                                   */

/* Walk the chain that starts at @p start and must close on its end record
 * before @p end.  A record stores no packed length, so each member runs to
 * the next header; landing on the end record is the only way to succeed. */
static bool arni_walk(xx_io_device *device, int64_t base, int64_t start,
                      int64_t end, int64_t *budget, arni_table *table,
                      int64_t *chain_end, xx_pd_struct *pd) {
    arni_window window;
    int64_t offset = start;
    uint32_t count = 0U;
    bool result = false;

    if (start < 0 || end < start || end - start < ARNI_MIN_CONTAINER ||
        !arni_window_open(&window, device, base, end, budget))
        return false;
    for (;;) {
        int64_t unpacked = 0, data, next, packed;
        int kind;
        if ((pd && xx_pd_is_stopped(pd)) || count >= ARNI_MAX_MEMBERS ||
            !arni_window_load(&window, offset, ARNI_PROBE))
            break;
        kind = arni_classify(window.data + (offset - window.start),
                             &unpacked);
        if (kind == ARNI_KIND_END) {
            if (count == 0U) break;
            if (chain_end) *chain_end = offset + ARNI_PROBE;
            result = true;
            break;
        }
        if (kind != ARNI_KIND_MEMBER) break;
        data = offset + ARNI_HEADER;
        /* A member holds at least one packed byte. */
        next = arni_find_header(&window, data + 1, pd);
        if (next <= data) break;
        packed = next - data;
        if (unpacked > packed * ARNI_MAX_RATIO + ARNI_RATIO_SLACK) break;
        if (table && !arni_table_append(table, offset, data, packed, unpacked))
            break;
        ++count;
        offset = next;
    }
    arni_window_close(&window);
    return result;
}

/* ---------------------------------------------------------------------- */
/* Carrier                                                                 */

typedef struct arni_pe_s {
    xx_io_device *device;
    int64_t base;
    int64_t available;
    int64_t image_end;
    uint32_t section_count;
    uint8_t sections[ARNI_MAX_SECTIONS * ARNI_SECTION_SIZE];
    int64_t rsrc_offset; /**< File offset of the resource directory root. */
    int64_t rsrc_limit;  /**< End of the raw data that holds it. */
    uint32_t entry_reads;
    uint32_t import_rva; /**< Data directory 1, 0 when absent. */
} arni_pe;

typedef struct arni_location_s {
    int64_t available;
    int64_t image_end;
    int64_t container_offset;
    int64_t container_size;
    int64_t chain_end;
    /* Base-relative offsets of the DLL names the import descriptors point
     * at.  They are the stub's other run of plain file names, so the name
     * pool scan must not take them for the member names. */
    uint32_t import_name_count;
    int64_t import_names[ARNI_MAX_IMPORTS];
} arni_location;

/* Map @p size bytes at @p rva to a base-relative file offset through the
 * section table; the whole range must be inside one section's raw data and
 * inside the file.  @p limit receives the end of that raw data. */
static bool arni_rva_to_offset(const arni_pe *pe, uint32_t rva, uint32_t size,
                               int64_t *offset, int64_t *limit) {
    uint32_t index;
    for (index = 0U; index < pe->section_count; ++index) {
        const uint8_t *section = pe->sections + index * ARNI_SECTION_SIZE;
        uint32_t address = arni_le32(section + 12);
        uint32_t raw_size = arni_le32(section + 16);
        uint32_t raw_pointer = arni_le32(section + 20);
        uint32_t delta;
        int64_t raw_end;
        if (rva < address || raw_size == 0U) continue;
        delta = rva - address;
        if (delta >= raw_size || size > raw_size - delta) continue;
        raw_end = (int64_t)raw_pointer + (int64_t)raw_size;
        if (raw_end > pe->available) raw_end = pe->available;
        if ((int64_t)raw_pointer + delta > raw_end - (int64_t)size) continue;
        *offset = (int64_t)raw_pointer + delta;
        if (limit) *limit = raw_end;
        return true;
    }
    return false;
}

/* Read the DOS and PE headers and the section table, and find the resource
 * directory.  False for anything that is not a PE with resources. */
static bool arni_read_pe(arni_pe *pe, xx_io_device *device, int64_t base,
                         int64_t available) {
    uint8_t dos[ARNI_DOS_HEADER];
    uint8_t header[ARNI_PE_HEADER];
    uint8_t optional[ARNI_OPTIONAL_MAX];
    uint32_t lfanew, optional_size, magic, directories, directory_offset;
    uint32_t rsrc_rva, index;
    uint64_t end;
    int64_t table;

    xx_mem_zero(pe, sizeof(*pe));
    pe->device = device;
    pe->base = base;
    pe->available = available;
    if (available < ARNI_DOS_HEADER + ARNI_PE_HEADER ||
        !arni_read_at(device, base, dos, sizeof(dos)) || dos[0] != 'M' ||
        dos[1] != 'Z')
        return false;
    lfanew = arni_le32(dos + 0x3c);
    if (lfanew < ARNI_DOS_HEADER || lfanew > ARNI_MAX_LFANEW ||
        (int64_t)lfanew > available - ARNI_PE_HEADER ||
        !arni_read_at(device, base + lfanew, header, sizeof(header)) ||
        header[0] != 'P' || header[1] != 'E' || header[2] != 0U ||
        header[3] != 0U)
        return false;
    pe->section_count = arni_le16(header + 6);
    optional_size = arni_le16(header + 20);
    if (pe->section_count == 0U || pe->section_count > ARNI_MAX_SECTIONS ||
        optional_size < 2U || optional_size > ARNI_OPTIONAL_MAX)
        return false;
    table = (int64_t)lfanew + ARNI_PE_HEADER + optional_size;
    if (table > available -
                    (int64_t)pe->section_count * ARNI_SECTION_SIZE ||
        !arni_read_at(device, base + lfanew + ARNI_PE_HEADER, optional,
                      optional_size) ||
        !arni_read_at(device, base + table, pe->sections,
                      (size_t)pe->section_count * ARNI_SECTION_SIZE))
        return false;
    magic = arni_le16(optional);
    if (magic == 0x010bU) {
        directory_offset = 96U;
    } else if (magic == 0x020bU) {
        directory_offset = 112U;
    } else {
        return false;
    }
    /* The resource directory is data directory 2. */
    if (optional_size < directory_offset + 3U * 8U) return false;
    directories = arni_le32(optional + directory_offset - 4U);
    if (directories < 3U) return false;
    pe->import_rva = arni_le32(optional + directory_offset + 8U);
    rsrc_rva = arni_le32(optional + directory_offset + 16U);
    if (rsrc_rva == 0U ||
        !arni_rva_to_offset(pe, rsrc_rva, ARNI_DIR_SIZE, &pe->rsrc_offset,
                            &pe->rsrc_limit))
        return false;

    /* What the carrier occupies: its headers and every section's raw data. */
    end = arni_le32(optional + 60);
    for (index = 0U; index < pe->section_count; ++index) {
        const uint8_t *section = pe->sections + index * ARNI_SECTION_SIZE;
        uint64_t raw_size = arni_le32(section + 16);
        uint64_t raw_pointer = arni_le32(section + 20);
        if (raw_size != 0U && raw_pointer + raw_size > end)
            end = raw_pointer + raw_size;
    }
    pe->image_end = end < (uint64_t)available ? (int64_t)end : available;
    return true;
}

/* Read resource directory @p relative: its entry count and where the entries
 * start.  Every directory and entry must lie in the resource raw data. */
static bool arni_read_directory(arni_pe *pe, uint32_t relative,
                                uint32_t *count, int64_t *entries) {
    uint8_t directory[ARNI_DIR_SIZE];
    int64_t offset = pe->rsrc_offset + (int64_t)relative;
    uint32_t total;
    if (offset > pe->rsrc_limit - ARNI_DIR_SIZE ||
        !arni_read_at(pe->device, pe->base + offset, directory,
                      sizeof(directory)))
        return false;
    total = arni_le16(directory + 12) + arni_le16(directory + 14);
    *entries = offset + ARNI_DIR_SIZE;
    /* Entries past the raw data are not read at all. */
    if ((int64_t)total * ARNI_ENTRY_SIZE > pe->rsrc_limit - *entries)
        total = (uint32_t)((pe->rsrc_limit - *entries) / ARNI_ENTRY_SIZE);
    *count = total;
    return true;
}

static bool arni_read_entry(arni_pe *pe, int64_t entries, uint32_t index,
                            uint32_t *name, uint32_t *target) {
    uint8_t entry[ARNI_ENTRY_SIZE];
    if (pe->entry_reads >= ARNI_MAX_ENTRY_READS) return false;
    ++pe->entry_reads;
    if (!arni_read_at(pe->device,
                      pe->base + entries + (int64_t)index * ARNI_ENTRY_SIZE,
                      entry, sizeof(entry)))
        return false;
    *name = arni_le32(entry);
    *target = arni_le32(entry + 4);
    return true;
}

/* Try one resource data entry as the container. */
static bool arni_try_leaf(arni_pe *pe, uint32_t relative, uint32_t *walks,
                          int64_t *budget, arni_location *location,
                          xx_pd_struct *pd) {
    uint8_t entry[ARNI_DATA_ENTRY_SIZE];
    uint8_t head[ARNI_PROBE];
    int64_t offset = pe->rsrc_offset + (int64_t)relative, data, chain_end = 0;
    uint32_t rva, size;

    if (offset > pe->rsrc_limit - ARNI_DATA_ENTRY_SIZE ||
        !arni_read_at(pe->device, pe->base + offset, entry, sizeof(entry)))
        return false;
    rva = arni_le32(entry);
    size = arni_le32(entry + 4);
    if (size < ARNI_MIN_CONTAINER || (int64_t)size > ARNI_MAX_CONTAINER ||
        !arni_rva_to_offset(pe, rva, size, &data, NULL) ||
        !arni_read_at(pe->device, pe->base + data, head, sizeof(head)) ||
        arni_classify(head, NULL) != ARNI_KIND_MEMBER)
        return false;
    if (*walks >= ARNI_MAX_WALKS) return false;
    ++*walks;
    if (!arni_walk(pe->device, pe->base, data, data + (int64_t)size, budget,
                   NULL, &chain_end, pd))
        return false;
    location->container_offset = data;
    location->container_size = size;
    location->chain_end = chain_end;
    return true;
}

/* Record where the import descriptors' DLL names live.  Best effort: a
 * damaged import table only means fewer exclusions. */
static void arni_collect_imports(arni_pe *pe, arni_location *location) {
    int64_t table, limit;
    uint32_t index;
    location->import_name_count = 0U;
    if (pe->import_rva == 0U ||
        !arni_rva_to_offset(pe, pe->import_rva, ARNI_IMPORT_DESCRIPTOR,
                            &table, &limit))
        return;
    for (index = 0U; index < ARNI_MAX_IMPORTS; ++index) {
        uint8_t descriptor[ARNI_IMPORT_DESCRIPTOR];
        int64_t at = table + (int64_t)index * ARNI_IMPORT_DESCRIPTOR, name;
        static const uint8_t zero[ARNI_IMPORT_DESCRIPTOR] = {0};
        if (at > limit - ARNI_IMPORT_DESCRIPTOR ||
            !arni_read_at(pe->device, pe->base + at, descriptor,
                          sizeof(descriptor)) ||
            xx_rt_memcmp(descriptor, zero, sizeof(zero)) == 0)
            break;
        if (arni_rva_to_offset(pe, arni_le32(descriptor + 12), 1U, &name,
                               NULL))
            location->import_names[location->import_name_count++] = name;
    }
}

/* Find the RCDATA resource that holds a complete chain. */
static bool arni_locate(Abstractformat *format, arni_location *location,
                        xx_pd_struct *pd) {
    arni_pe *pe;
    int64_t total, available, root_entries, budget = ARNI_SCAN_BUDGET;
    uint32_t root_count, index, walks = 0U;
    bool found = false;

    if (!format || !format->device || format->base_address < 0) return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    available = total - format->base_address;
    if (available < ARNI_DOS_HEADER + ARNI_PE_HEADER + ARNI_MIN_CONTAINER)
        return false;
    pe = (arni_pe *)xx_mem_alloc(sizeof(*pe));
    if (!pe) return false;
    if (!arni_read_pe(pe, format->device, format->base_address, available) ||
        !arni_read_directory(pe, 0U, &root_count, &root_entries))
        goto done;
    for (index = 0U; !found && index < root_count; ++index) {
        uint32_t type, target, count, name_index;
        int64_t names;
        if (!arni_read_entry(pe, root_entries, index, &type, &target)) break;
        /* RT_RCDATA by number, pointing at a subdirectory. */
        if (type != ARNI_RT_RCDATA || !(target & 0x80000000U)) continue;
        if (!arni_read_directory(pe, target & 0x7FFFFFFFU, &count, &names))
            continue;
        for (name_index = 0U; !found && name_index < count; ++name_index) {
            uint32_t name, next, languages, language_index;
            int64_t language_entries;
            if (!arni_read_entry(pe, names, name_index, &name, &next)) break;
            (void)name; /* "MIRCALL" or 4 in the references; not required */
            if (!(next & 0x80000000U)) {
                found = arni_try_leaf(pe, next, &walks, &budget, location, pd);
                continue;
            }
            if (!arni_read_directory(pe, next & 0x7FFFFFFFU, &languages,
                                     &language_entries))
                continue;
            for (language_index = 0U; !found && language_index < languages;
                 ++language_index) {
                uint32_t language, leaf;
                if (!arni_read_entry(pe, language_entries, language_index,
                                     &language, &leaf))
                    break;
                if (leaf & 0x80000000U) continue;
                found = arni_try_leaf(pe, leaf, &walks, &budget, location, pd);
            }
        }
        /* One RT_RCDATA type entry is all a resource directory has. */
        break;
    }
    if (found) {
        location->available = available;
        location->image_end = pe->image_end;
        if (location->image_end < location->chain_end)
            location->image_end = location->chain_end;
        arni_collect_imports(pe, location);
    }
done:
    xx_mem_free(pe);
    return found && !(pd && xx_pd_is_stopped(pd));
}

/* ---------------------------------------------------------------------- */
/* Member names                                                            */

static bool arni_is_alnum(uint8_t c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9');
}

static uint8_t arni_upper(uint8_t c) {
    return (c >= 'a' && c <= 'z') ? (uint8_t)(c - 0x20U) : c;
}

/* A bare file name the stub could pass to sprintf("%s\\%s", ...): plain
 * ASCII, no space, no separator, no wildcard, an extension of one to four
 * alphanumerics.  Port of XArchive XArniSFX::isPlainFileName() (MIT). */
static bool arni_is_plain_name(const uint8_t *token, size_t size) {
    size_t index, last_dot = (size_t)-1;
    if (size < ARNI_NAME_MIN || size > ARNI_NAME_MAX || token[0] == '.')
        return false;
    for (index = 0U; index < size; ++index) {
        uint8_t c = token[index];
        if (!arni_is_alnum(c) && c != '_' && c != '.' && c != '~' &&
            c != '!' && c != '@' && c != '#' && c != '$' && c != '&' &&
            c != '(' && c != ')' && c != '-' && c != '{' && c != '}' &&
            c != '\'' && c != '+' && c != ',' && c != ';' && c != '=')
            return false;
        if (c == '.') last_dot = index;
    }
    if (last_dot == (size_t)-1 || last_dot == 0U || last_dot >= size - 1U ||
        size - last_dot - 1U > 4U)
        return false;
    for (index = last_dot + 1U; index < size; ++index)
        if (!arni_is_alnum(token[index])) return false;
    return true;
}

/* CON, PRN, AUX, NUL, COM0-9, LPT0-9, CONIN$, CONOUT$ and CLOCK$, with or
 * without an extension, in any case. */
static bool arni_is_device(const char *text) {
    static const char *const names[] = {"CON",    "PRN",     "AUX",
                                        "NUL",    "CONIN$",  "CONOUT$",
                                        "CLOCK$"};
    size_t stem = 0U, index, position;
    while (text[stem] && text[stem] != '.') ++stem;
    while (stem > 0U && text[stem - 1U] == ' ') --stem;
    for (index = 0U; index < sizeof(names) / sizeof(names[0]); ++index) {
        const char *name = names[index];
        for (position = 0U; position < stem && name[position]; ++position)
            if (arni_upper((uint8_t)text[position]) != (uint8_t)name[position])
                break;
        if (position == stem && name[position] == 0) return true;
    }
    if (stem == 4U && text[3] >= '0' && text[3] <= '9') {
        uint8_t a = arni_upper((uint8_t)text[0]), b = arni_upper((uint8_t)text[1]),
                c = arni_upper((uint8_t)text[2]);
        if ((a == 'C' && b == 'O' && c == 'M') ||
            (a == 'L' && b == 'P' && c == 'T'))
            return true;
    }
    return false;
}

static bool arni_same_folded(const char *left, const char *right) {
    while (*left && arni_upper((uint8_t)*left) == arni_upper((uint8_t)*right)) {
        ++left;
        ++right;
    }
    return arni_upper((uint8_t)*left) == arni_upper((uint8_t)*right);
}

/* Find the stub's file-name pool in @p stub and copy it into the table's
 * member names.  The pool is a sequence of NUL-terminated strings padded to
 * an even length, so a run breaks on anything that is not a file name and on
 * more than two NULs in a row.  Exactly one run whose length is the member
 * count is accepted; two such runs mean the pool cannot be told from the
 * noise.  Port of XArchive XArniSFX::collectNameTable() (MIT), plus two
 * additions: a string an import descriptor names (a DLL the stub links
 * against) is not a pool entry, and a device name refuses the pool. */
static bool arni_is_import_name(const arni_location *location, size_t at) {
    uint32_t index;
    for (index = 0U; index < location->import_name_count; ++index)
        if (location->import_names[index] == (int64_t)at) return true;
    return false;
}

static bool arni_collect_names(const uint8_t *stub, size_t size,
                               const arni_location *location,
                               arni_table *table) {
    size_t i = 0U, run_start = 0U, run_length = 0U, match_start = 0U;
    size_t matches = 0U, member, other;

    if (table->count < ARNI_MIN_NAMES) return false;
    while (i < size) {
        size_t padding = 0U, end;
        while (i < size && stub[i] == 0U) {
            ++i;
            ++padding;
        }
        if (i >= size) break;
        if (padding > 2U && run_length != 0U) {
            if (run_length == table->count) {
                ++matches;
                match_start = run_start;
            }
            run_length = 0U;
        }
        end = i;
        while (end < size && stub[end] != 0U) ++end;
        if (end >= size) break; /* an unterminated tail is not a name */
        if (arni_is_plain_name(stub + i, end - i) &&
            !arni_is_import_name(location, i)) {
            if (run_length == 0U) run_start = i;
            ++run_length;
        } else if (run_length != 0U) {
            if (run_length == table->count) {
                ++matches;
                match_start = run_start;
            }
            run_length = 0U;
        }
        i = end + 1U;
    }
    if (run_length != 0U && run_length == table->count) {
        ++matches;
        match_start = run_start;
    }
    if (matches != 1U) return false;

    /* Re-read the one matching run; its tokens are all plain names. */
    i = match_start;
    for (member = 0U; member < table->count; ++member) {
        size_t end;
        while (i < size && stub[i] == 0U) ++i;
        end = i;
        while (end < size && stub[end] != 0U) ++end;
        if (end >= size || !arni_is_plain_name(stub + i, end - i)) return false;
        xx_rt_memcpy(table->items[member].name, stub + i, end - i);
        table->items[member].name[end - i] = 0;
        i = end + 1U;
    }
    /* The destination is a Windows directory: two names that differ only in
     * case would collide there, and a device name is not a file. */
    for (member = 0U; member < table->count; ++member) {
        if (arni_is_device(table->items[member].name)) return false;
        for (other = 0U; other < member; ++other)
            if (arni_same_folded(table->items[member].name,
                                 table->items[other].name))
                return false;
    }
    return true;
}

static void arni_decimal(char *out, size_t value) {
    char digits[24];
    unsigned count = 0U;
    size_t position = 0U;
    do {
        digits[count++] = (char)('0' + value % 10U);
        value /= 10U;
    } while (value && count < sizeof(digits));
    while (count) out[position++] = digits[--count];
    out[position] = 0;
}

/* Name every member: the stub's pool when it can be trusted, otherwise the
 * references' own "File_<n>.bin". */
static void arni_apply_names(xx_io_device *device, int64_t base,
                             const arni_location *location,
                             arni_table *table) {
    size_t index;
    bool named = false;
    if (table->container_offset > 0 &&
        table->container_offset <= ARNI_MAX_STUB_SCAN &&
        table->count >= ARNI_MIN_NAMES) {
        uint8_t *stub = (uint8_t *)xx_mem_alloc((size_t)table->container_offset);
        if (stub) {
            if (arni_read_at(device, base, stub,
                             (size_t)table->container_offset))
                named = arni_collect_names(stub,
                                           (size_t)table->container_offset,
                                           location, table);
            xx_mem_free(stub);
        }
    }
    table->names_recovered = named;
    if (named) return;
    for (index = 0U; index < table->count; ++index) {
        char *name = table->items[index].name;
        xx_rt_memcpy(name, "File_", 5U);
        arni_decimal(name + 5, index);
        xx_rt_memcpy(name + xx_str_len(name), ".bin", 5U);
    }
}

static bool arni_build_table(Abstractformat *format, arni_table **out,
                             xx_pd_struct *pd) {
    arni_location location;
    arni_table *table;
    int64_t budget = ARNI_SCAN_BUDGET, chain_end = 0;

    *out = NULL;
    xx_mem_zero(&location, sizeof(location));
    if (!arni_locate(format, &location, pd)) return false;
    table = (arni_table *)xx_mem_alloc(sizeof(*table));
    if (!table) return false;
    xx_mem_zero(table, sizeof(*table));
    table->container_offset = location.container_offset;
    table->container_size = location.container_size;
    table->image_end = location.image_end;
    if (!arni_walk(format->device, format->base_address,
                   location.container_offset,
                   location.container_offset + location.container_size,
                   &budget, table, &chain_end, pd) ||
        table->count == 0U || chain_end != location.chain_end) {
        arni_table_free(table);
        return false;
    }
    table->chain_end = chain_end;
    arni_apply_names(format->device, format->base_address, &location, table);
    *out = table;
    return true;
}

/* ---------------------------------------------------------------------- */
/* Decoding                                                                */

/* Decode one member into @p destination (NULL only verifies). */
static bool arni_unpack_member(Abstractformat *format,
                               const arni_member *member,
                               xx_io_device *destination, xx_pd_struct *pd) {
    uint8_t *packed = NULL;
    uint8_t *plain = NULL;
    size_t written = 0U, done = 0U;
    int64_t input;
    bool result = false;

    if (member->packed_size <= 0 || member->unpacked_size <= 0 ||
        member->unpacked_size >= ARNI_MAX_SIZE)
        return false;
    /* The decoder stops at the stored size and reads only the bits it needs;
     * no symbol costs more than a few bytes per output byte, so reading at
     * most eight per output byte keeps a member that runs to a far-away
     * header from pulling it all into memory. */
    input = member->unpacked_size * 8 + 64;
    if (input > member->packed_size) input = member->packed_size;
    if ((uint64_t)input > (uint64_t)SIZE_MAX) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    packed = (uint8_t *)xx_mem_alloc((size_t)input);
    plain = (uint8_t *)xx_mem_alloc((size_t)member->unpacked_size);
    if (!packed || !plain ||
        !arni_read_at(format->device,
                      format->base_address + member->data_offset, packed,
                      (size_t)input))
        goto done;
    if (!xx_lzhuf_decode_memory(packed, (size_t)input, plain,
                                (size_t)member->unpacked_size, &written) ||
        written != (size_t)member->unpacked_size)
        goto done;
    if (destination) {
        while (done < written) {
            ssize_t sent =
                xx_io_write(destination, plain + done, written - done);
            if (sent <= 0 || (size_t)sent > written - done) goto done;
            done += (size_t)sent;
        }
    }
    result = true;
done:
    if (packed) xx_mem_free(packed);
    if (plain) xx_mem_free(plain);
    return result;
}

/* ---------------------------------------------------------------------- */
/* Records API helpers                                                     */

static bool arni_copy_options(xx_list_s *destination,
                              const xx_list_s *source) {
    size_t index;
    if (!source) return true;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *original =
            (const xx_meta *)xx_list_at((const xx_list_t *)source, index);
        xx_meta copy;
        if (!original) continue;
        xx_meta_init(&copy, original->meta_id);
        if (!xx_var_copy(&copy.var, &original->var) ||
            !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

static const xx_var *arni_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool arni_set_record(Abstractformat *format, xx_archive_record *record,
                            const arni_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = format->base_address + member->header_offset;
    record->header_size = member->data_offset - member->header_offset;
    record->data_offset = format->base_address + member->data_offset;
    record->compressed_size = member->packed_size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->packed_size) &&
           xx_archive_record_set_meta_u64(record,
                                          XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)member->unpacked_size) &&
           xx_archive_record_set_meta_u64(
               record, XX_META_ID_COMPRESSION_METHOD,
               XX_ARNI_INSTALLER_CONTAINER_METHOD_LZHUF) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false);
}

/* The last gate before a name reaches the file system.  Names are single
 * components here (the pool refuses separators), so anything else fails. */
static bool arni_safe_output_name(const char *name) {
    size_t index;
    bool meaningful = false;
    if (!name || !name[0]) return false;
    for (index = 0U; name[index]; ++index) {
        uint8_t c = (uint8_t)name[index];
        if (c < 0x20U || c >= 0x7FU || c == '/' || c == '\\' || c == ':' ||
            c == '<' || c == '>' || c == '"' || c == '|' || c == '?' ||
            c == '*')
            return false;
        if (c != '.' && c != ' ') meaningful = true;
    }
    if (!meaningful || name[index - 1U] == '.' || name[index - 1U] == ' ')
        return false;
    return !arni_is_device(name);
}

/* ---------------------------------------------------------------------- */
/* Public API                                                              */

static void arni_vtable_destroy(Abstractformat *format) {
    xx_arni_installer_container *archive =
        (xx_arni_installer_container *)format;
    if (archive && archive->table) {
        arni_table_free((arni_table *)archive->table);
        archive->table = NULL;
    }
}

void xx_arni_installer_container_init(xx_arni_installer_container *archive,
                                      xx_io_device *device,
                                      int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_ARNI_INSTALLER_CONTAINER_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-arni-sfx");
    xx_format_set_extension(&archive->format, "exe");
    archive->format.check_is_valid =
        xx_arni_installer_container_check_is_valid;
    archive->format.handle_base_info =
        xx_arni_installer_container_handle_base_info;
    archive->format.get_format_size =
        xx_arni_installer_container_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_arni_installer_container_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_arni_installer_container_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_arni_installer_container_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_arni_installer_container_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_arni_installer_container_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_arni_installer_container_free_archive_records_reading;
    archive->format.destroy = arni_vtable_destroy;
    archive->container_offset = -1;
}

xx_arni_installer_container *xx_arni_installer_container_create(
    xx_io_device *device, int64_t base_address) {
    xx_arni_installer_container *archive =
        (xx_arni_installer_container *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_arni_installer_container_init(archive, device, base_address);
    return archive;
}

void xx_arni_installer_container_destroy(
    xx_arni_installer_container *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper above. */
    arni_vtable_destroy(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_arni_installer_container_free(xx_arni_installer_container *archive) {
    if (!archive) return;
    xx_arni_installer_container_destroy(archive);
    xx_mem_free(archive);
}

bool xx_arni_installer_container_check_is_valid(Abstractformat *format,
                                                xx_pd_struct *pd) {
    arni_location location;
    if (!format || (pd && xx_pd_is_stopped(pd))) return false;
    xx_mem_zero(&location, sizeof(location));
    return arni_locate(format, &location, pd);
}

bool xx_arni_installer_container_handle_base_info(Abstractformat *format,
                                                  xx_pd_struct *pd) {
    xx_arni_installer_container *archive;
    arni_table *table = NULL;
    if (!format || (pd && xx_pd_is_stopped(pd))) return false;
    archive = (xx_arni_installer_container *)format;
    if (archive->table) {
        arni_table_free((arni_table *)archive->table);
        archive->table = NULL;
    }
    if (!arni_build_table(format, &table, pd)) {
        format->is_valid = false;
        format->base_info_handled = false;
        format->format_size = -1;
        format->number_of_archive_records = 0U;
        archive->number_of_records = 0U;
        return false;
    }
    archive->table = table;
    archive->number_of_records = table->count;
    archive->container_offset = table->container_offset;
    archive->container_size = table->container_size;
    archive->chain_end = table->chain_end;
    archive->unpacked_total = table->unpacked_total;
    archive->names_recovered = table->names_recovered;
    format->number_of_archive_records = table->count;
    /* The container sits inside the carrier's resources, so the format is the
     * executable: its headers and every section's raw data. */
    format->format_size = table->image_end;
    format->overlay_offset = -1;
    format->overlay_size = 0;
    format->file_type = XX_ARNI_INSTALLER_CONTAINER_FILE_TYPE;
    format->format_type = XX_TYPE_ARCHIVE;
    format->is_archive = true;
    format->is_valid = true;
    format->base_info_handled = true;
    return true;
}

int64_t xx_arni_installer_container_get_format_size(Abstractformat *format,
                                                    xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_arni_installer_container_handle_base_info(format, pd))
               ? format->format_size
               : -1;
}

uint64_t xx_arni_installer_container_get_number_of_archive_records(
    Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_arni_installer_container_handle_base_info(format, pd))
               ? ((xx_arni_installer_container *)format)->number_of_records
               : 0U;
}

xx_archive_record_state *
xx_arni_installer_container_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    xx_arni_installer_container *archive;
    xx_archive_record_state *state;
    arni_table *table = NULL;
    if (!format || !format->device) return NULL;
    archive = (xx_arni_installer_container *)format;
    /* The table handle_base_info built is handed over rather than walked a
     * second time; a later listing walks again. */
    if (archive->table) {
        table = (arni_table *)archive->table;
        archive->table = NULL;
    } else if (!arni_build_table(format, &table, pd)) {
        return NULL;
    }
    table->index = 0U;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        arni_table_free(table);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = table;
    state->free_internal = arni_table_free_opaque;
    state->total_records = (int64_t)table->count;
    if (!arni_copy_options(&state->options, options) ||
        !arni_set_record(format, &state->current_record, &table->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    state->current_index = 0;
    return state;
}

const xx_archive_record *
xx_arni_installer_container_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_arni_installer_container_archive_record_move_to_next(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    arni_table *table;
    if (!format || !state || state->format != format || !state->has_record ||
        (pd && xx_pd_is_stopped(pd)))
        return false;
    table = (arni_table *)state->internal_state;
    if (!table || table->index + 1U >= table->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++table->index;
    ++state->current_index;
    state->has_record = arni_set_record(format, &state->current_record,
                                        &table->items[table->index]);
    return state->has_record;
}

bool xx_arni_installer_container_unpack_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    arni_table *table;
    const arni_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    bool created = false;

    if (!format || !state || state->format != format || !state->has_record ||
        !(table = (arni_table *)state->internal_state) ||
        table->index >= table->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &table->items[table->index];
    path_option = arni_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    /* No destination: decode and discard, which verifies the member. */
    if (!path_option) return arni_unpack_member(format, member, NULL, pd);
    if (!arni_safe_output_name(member->name)) return false;
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
               ? xx_str_concat3(base, "/", member->name)
               : xx_str_concat(base, member->name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        created = destination != NULL;
        if (!destination) goto done;
        result = arni_unpack_member(format, member, destination, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_io_file_remove_a(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_arni_installer_container_free_archive_records_reading(
    Abstractformat *format, xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
