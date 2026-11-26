/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xx_scan_resources_internal.h"
#include "xxfclib/formats/executable/elf/xx_elf.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"

/* Bound child scans independently of the format reader's traversal budget.
 * The ELF resource reader uses the same maximum number of leaf records. */
#define XX_SCAN_RESOURCE_LIMIT UINT32_C(65536)

static bool xx_scan_resource_error(xx_pd_struct *pd, int error, const char *message)
{
    if (pd && !pd->last_error && !xx_pd_is_stopped(pd)) xx_pd_set_error(pd, error, message);
    return false;
}

static bool xx_scan_resources_stopped(xx_pd_struct *pd)
{
    return xx_pd_is_stopped(pd) || (pd && pd->last_error);
}

static bool xx_scan_visit_resource_stream(Abstractformat *format, int64_t origin, int64_t range_size, bool unreadable_is_empty, xx_scan_resource_callback callback,
                                          void *context, xx_pd_struct *pd)
{
    xx_resource_state *state;
    const xx_resource_record *record;
    uint32_t count = 0U;
    bool result = false;

    if (xx_scan_resources_stopped(pd)) return false;
    if (!xx_format_handle_base_info(format, pd)) {
        /* The scan types an ELF by its magic and class alone, as XELF::isValid
         * does. One the reader turns down (a truncated file, a packed .so whose
         * section table points past the end) has no resources; it is no error. */
        if (unreadable_is_empty && !xx_scan_resources_stopped(pd)) return true;
        return xx_scan_resource_error(pd, XXFC_ERR_GENERIC, "Cannot read executable resources");
    }
    if (xx_scan_resources_stopped(pd)) return false;
    state = xx_format_create_resources_reading(format, pd);
    if (!state) return xx_scan_resource_error(pd, XXFC_ERR_GENERIC, "Cannot create scan resource reader");

    for (;;) {
        xx_resource_record absolute_record;
        if (xx_scan_resources_stopped(pd) || state->failed) goto done;
        record = xx_format_get_current_resource(format, state);
        if (!record) {
            result = true;
            break;
        }
        if (count == XX_SCAN_RESOURCE_LIMIT) {
            xx_scan_resource_error(pd, XXFC_ERR_OUT_OF_BOUNDS, "Scan resource count exceeds bounded limit");
            goto done;
        }
        ++count;
        if (record->size < 0 || (record->size > 0 && (record->offset < 0 || record->offset > range_size || record->size > range_size - record->offset))) {
            xx_scan_resource_error(pd, XXFC_ERR_OUT_OF_BOUNDS, "Invalid scan resource range");
            goto done;
        }
        if (record->size > 0) {
            /* The enclosing view has base zero. Translate only the borrowed
             * copy, leaving the iterator's current record unmodified. */
            absolute_record = *record;
            absolute_record.offset += origin;
            if (!callback(context, &absolute_record, pd)) {
                xx_scan_resource_error(pd, XXFC_ERR_GENERIC, "Scan resource callback failed");
                goto done;
            }
            if (xx_scan_resources_stopped(pd)) goto done;
        }
        if (!xx_format_resource_move_to_next(format, state, pd)) {
            result = !state->failed && !xx_scan_resources_stopped(pd);
            break;
        }
    }
done:
    if (!result) xx_scan_resource_error(pd, XXFC_ERR_GENERIC, "Cannot enumerate executable resources");
    xx_format_free_resources_reading(format, state);
    return result;
}

/* ---- PE: XPE::getFileParts(FILEPART_RESOURCE) --------------------------
 * XScanEngine takes the resources of a PE from XPE::getFileParts, which
 * reads the fixed three-level directory with zero-filled reads, maps RVAs
 * through a memory map of the header and the (clamped) section raw data, and
 * drops a record whose payload is unmapped or leaves the file instead of
 * failing. Mirror it so a malformed resource tree yields the same parts. */

#define XX_SCAN_PE_QT_MAX_SECTIONS 0x60U   /* XPE_DEF::S_MAX_SECTIONCOUNT */
#define XX_SCAN_PE_QT_RESOURCE_LIMIT 10000 /* getFileParts(.., 10000, ..) */

typedef struct xx_scan_pe_part {
    uint64_t address;
    int64_t offset;
    int64_t size;
} xx_scan_pe_part;

typedef struct xx_scan_pe_qt {
    xx_io_device *device;
    int64_t total;
    uint64_t module_address;
    uint32_t resource_rva;
    xx_scan_pe_part parts[1U + XX_SCAN_PE_QT_MAX_SECTIONS];
    uint32_t part_count;
} xx_scan_pe_qt;

/* XBinary::read_array: bytes past the end stay zero. */
static void xx_scan_pe_read(const xx_scan_pe_qt *pe, int64_t offset, void *buffer, size_t size)
{
    xx_rt_memset(buffer, 0, size);
    if (offset < 0 || offset >= pe->total) return;
    if ((uint64_t)size > (uint64_t)(pe->total - offset)) size = (size_t)(pe->total - offset);
    if (size && !xx_io_read_at(pe->device, offset, buffer, size)) xx_rt_memset(buffer, 0, size);
}

static uint32_t xx_scan_pe_u32(const xx_scan_pe_qt *pe, int64_t offset)
{
    uint8_t b[4];
    xx_scan_pe_read(pe, offset, b, sizeof(b));
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

static uint16_t xx_scan_pe_u16(const xx_scan_pe_qt *pe, int64_t offset)
{
    uint8_t b[2];
    xx_scan_pe_read(pe, offset, b, sizeof(b));
    return (uint16_t)(b[0] | (b[1] << 8));
}

/* XBinary::addressToOffset: the last physical record holding the address. */
static int64_t xx_scan_pe_address_to_offset(const xx_scan_pe_qt *pe, uint64_t address)
{
    uint32_t i = pe->part_count;
    if (address == UINT64_MAX) return -1;
    while (i--) {
        const xx_scan_pe_part *part = &pe->parts[i];
        if (part->size > 0 && address >= part->address && address - part->address < (uint64_t)part->size)
            return part->offset + (int64_t)(address - part->address);
    }
    return -1;
}

/* The header and section parts of XPE::getFileParts, sorted by address as
 * XBinary::_processMemoryMap does, plus the resource directory RVA. */
static void xx_scan_pe_build_map(xx_scan_pe_qt *pe)
{
    int64_t lfanew = (int32_t)xx_scan_pe_u32(pe, 0x3c);
    int64_t optional = lfanew + 24;
    uint16_t machine = xx_scan_pe_u16(pe, lfanew + 4);
    uint32_t sections = xx_scan_pe_u16(pe, lfanew + 6);
    int64_t table;
    uint64_t file_alignment;
    int64_t headers_size;
    bool is64;
    uint32_t i;

    if (optional < 0 || optional >= pe->total) optional = -1; /* XPE::getOptionalHeaderOffset */
    if (machine == (0x7b79U ^ 0x8664U)) machine = 0x8664U;   /* XPE::_getMachine */
    is64 = machine == 0x8664U || machine == 0x0200U || machine == 0xaa64U || machine == 0x0284U || machine == 0x5064U || machine == 0x6264U;
    pe->module_address = is64 ? ((uint64_t)xx_scan_pe_u32(pe, optional + 24) | ((uint64_t)xx_scan_pe_u32(pe, optional + 28) << 32))
                              : (uint64_t)xx_scan_pe_u32(pe, optional + 28);
    pe->resource_rva = xx_scan_pe_u32(pe, optional + (is64 ? 112 : 96) + 2 * 8);
    file_alignment = optional != -1 ? xx_scan_pe_u32(pe, optional + 36) : 0U;
    if (file_alignment > 0x10000U) file_alignment = 0x200U;
    headers_size = xx_scan_pe_u32(pe, optional + 60);
    if (headers_size > pe->total) headers_size = pe->total;
    if (sections > XX_SCAN_PE_QT_MAX_SECTIONS) sections = XX_SCAN_PE_QT_MAX_SECTIONS;
    table = optional != -1 ? optional + xx_scan_pe_u16(pe, lfanew + 20) : -1;

    pe->parts[0].address = pe->module_address;
    pe->parts[0].offset = 0;
    pe->parts[0].size = headers_size;
    pe->part_count = 1U;
    for (i = 0U; i < sections; ++i) {
        uint32_t virtual_address = 0U, raw_size = 0U, raw_offset = 0U;
        uint64_t aligned;
        xx_scan_pe_part part;
        uint32_t j;
        if (table != -1) {
            int64_t entry = table + (int64_t)i * 40;
            virtual_address = xx_scan_pe_u32(pe, entry + 12);
            raw_size = xx_scan_pe_u32(pe, entry + 16);
            raw_offset = xx_scan_pe_u32(pe, entry + 20);
        }
        if ((int64_t)raw_offset > pe->total) raw_offset = 0U;
        /* The reference compares the 32-bit (wrapping) sum. */
        if ((int64_t)(uint32_t)(raw_offset + raw_size) > pe->total) raw_size = (uint32_t)(pe->total - (int64_t)raw_offset);
        aligned = (uint64_t)raw_offset & ~(file_alignment - 1U);
        part.address = pe->module_address + virtual_address;
        part.offset = (int64_t)aligned;
        part.size = (int64_t)raw_size + (int64_t)((uint64_t)raw_offset - aligned);
        j = pe->part_count;
        while (j > 0U && pe->parts[j - 1U].address > part.address) {
            pe->parts[j] = pe->parts[j - 1U];
            --j;
        }
        pe->parts[j] = part;
        pe->part_count++;
    }
}

/* XPE::getResourcesIDName as an owned UTF-8 string (NULL for an ID). */
static char *xx_scan_pe_resource_name(const xx_scan_pe_qt *pe, int64_t root, uint32_t value)
{
    uint16_t units[1025];
    uint16_t count;
    uint32_t i;
    if (!(value & UINT32_C(0x80000000))) return NULL;
    value &= UINT32_C(0x7fffffff);
    count = xx_scan_pe_u16(pe, root + value);
    if (count > 1024U) count = 1024U;
    for (i = 0U; i < count; ++i) units[i] = xx_scan_pe_u16(pe, root + value + 2 + (int64_t)i * 2);
    units[count] = 0U;
    return xx_rt_utf16_to_utf8(units);
}

/* Report one XPE::getResources record that passes checkOffsetSize. */
static bool xx_scan_pe_report_resource(const xx_scan_pe_qt *pe, int64_t root, uint32_t type, uint32_t name, int64_t offset, uint32_t size, int64_t origin,
                                       xx_scan_resource_callback callback, void *context, xx_pd_struct *pd)
{
    xx_resource_record record;
    bool ok;
    xx_mem_zero(&record, sizeof(record));
    record.type_id = (type & UINT32_C(0x80000000)) ? UINT32_MAX : type;
    record.name_id = (name & UINT32_C(0x80000000)) ? UINT32_MAX : name;
    record.language_id = UINT32_MAX;
    record.type_name = xx_scan_pe_resource_name(pe, root, type);
    record.name = xx_scan_pe_resource_name(pe, root, name);
    record.offset = origin + offset;
    record.size = size;
    ok = callback(context, &record, pd);
    if (!ok) xx_scan_resource_error(pd, XXFC_ERR_GENERIC, "Scan resource callback failed");
    if (record.type_name) xx_mem_free(record.type_name);
    if (record.name) xx_mem_free(record.name);
    return ok && !xx_scan_resources_stopped(pd);
}

static bool xx_scan_visit_pe_resources(xx_io_device *view, int64_t origin, int64_t range_size, xx_scan_resource_callback callback, void *context,
                                       xx_pd_struct *pd)
{
    xx_scan_pe_qt *pe;
    int64_t root;
    int64_t level0;
    uint32_t count0;
    uint32_t i;
    int32_t found = 0;
    bool ok = true;

    pe = (xx_scan_pe_qt *)xx_mem_calloc(1U, sizeof(*pe));
    if (!pe) return xx_scan_resource_error(pd, XXFC_ERR_OUT_OF_MEMORY, "Cannot allocate scan resource map");
    pe->device = view;
    pe->total = range_size;
    xx_scan_pe_build_map(pe);
    /* XPE::isResourcesPresent and XPE::getDataDirectoryOffset */
    root = pe->resource_rva ? xx_scan_pe_address_to_offset(pe, (uint64_t)pe->resource_rva + pe->module_address) : -1;
    if (root == -1) goto done;

    level0 = root;
    count0 = (uint32_t)xx_scan_pe_u16(pe, level0 + 12) + xx_scan_pe_u16(pe, level0 + 14);
    if (count0 > 1000U || xx_scan_pe_u32(pe, level0) != 0U) goto done;
    level0 += 16;
    for (i = 0U; ok && i < count0 && found < XX_SCAN_PE_QT_RESOURCE_LIMIT; ++i, level0 += 8) {
        uint32_t type = xx_scan_pe_u32(pe, level0);
        uint32_t child = xx_scan_pe_u32(pe, level0 + 4) & UINT32_C(0x7fffffff);
        int64_t level1;
        uint32_t count1;
        uint32_t j;
        if (xx_scan_resources_stopped(pd)) {
            ok = false;
            break;
        }
        if (!child) break;
        level1 = root + child;
        if (xx_scan_pe_u32(pe, level1) != 0U) break;
        count1 = (uint32_t)xx_scan_pe_u16(pe, level1 + 12) + xx_scan_pe_u16(pe, level1 + 14);
        level1 += 16;
        if (count1 > 1000U) continue;
        for (j = 0U; ok && j < count1 && found < XX_SCAN_PE_QT_RESOURCE_LIMIT; ++j, level1 += 8) {
            uint32_t name = xx_scan_pe_u32(pe, level1);
            int64_t level2 = root + (xx_scan_pe_u32(pe, level1 + 4) & UINT32_C(0x7fffffff));
            uint32_t count2;
            uint32_t k;
            if (xx_scan_pe_u32(pe, level2) != 0U) break;
            count2 = (uint32_t)xx_scan_pe_u16(pe, level2 + 12) + xx_scan_pe_u16(pe, level2 + 14);
            level2 += 16;
            if (count2 > 1000U) continue;
            for (k = 0U; ok && k < count2 && found < XX_SCAN_PE_QT_RESOURCE_LIMIT; ++k, level2 += 8) {
                int64_t data = root + xx_scan_pe_u32(pe, level2 + 4);
                uint32_t data_rva = xx_scan_pe_u32(pe, data);
                uint32_t data_size = xx_scan_pe_u32(pe, data + 4);
                int64_t offset = xx_scan_pe_address_to_offset(pe, (uint64_t)data_rva + pe->module_address);
                found++;
                /* XBinary::checkOffsetSize */
                if (offset >= 0 && data_size > 0U && offset < range_size && (int64_t)data_size <= range_size - offset)
                    ok = xx_scan_pe_report_resource(pe, root, type, name, offset, data_size, origin, callback, context, pd);
            }
        }
    }
done:
    xx_mem_free(pe);
    return ok && !xx_scan_resources_stopped(pd);
}

bool xx_scan_visit_resources(xx_io_device *device, xx_file_type_t type, int64_t offset, int64_t size, xx_scan_resource_callback callback, void *context, xx_pd_struct *pd)
{
    xx_io_device *view;
    int64_t total_size;
    int64_t range_size;
    int64_t saved_position;
    bool result;

    if (!device || !callback) return xx_scan_resource_error(pd, XXFC_ERR_NULL_PARAM, "Missing scan resource argument");
    if (xx_scan_resources_stopped(pd)) return false;
    if (offset < 0 || size < -1 || (size >= 0 && size > INT64_MAX - offset)) return xx_scan_resource_error(pd, XXFC_ERR_INVALID_ARG, "Invalid scan resource input range");
    switch (type) {
        case XX_FILE_TYPE_PE:
        case XX_FILE_TYPE_PE32:
        case XX_FILE_TYPE_PE64:
        case XX_FILE_TYPE_DOTNET:
        case XX_FILE_TYPE_ELF:
        case XX_FILE_TYPE_ELF32:
        case XX_FILE_TYPE_ELF64: break;
        default: return true;
    }

    saved_position = xx_io_tell(device);
    if (saved_position < 0) return xx_scan_resource_error(pd, XXFC_ERR_IO, "Cannot save scan resource input position");
    total_size = xx_io_total_size(device);
    if ((total_size >= 0 && (offset > total_size || (size >= 0 && size > total_size - offset))) || (total_size < 0 && size == -1)) {
        result = xx_scan_resource_error(pd, total_size < 0 ? XXFC_ERR_IO : XXFC_ERR_OUT_OF_BOUNDS,
                                        total_size < 0 ? "Cannot determine scan resource input size" : "Scan resource range exceeds input");
        goto restore;
    }
    range_size = size >= 0 ? size : total_size - offset;
    view = xx_io_sub_open_ro(device, offset, range_size);
    if (!view) {
        result = xx_scan_resource_error(pd, XXFC_ERR_OUT_OF_MEMORY, "Cannot create scan resource input view");
        goto restore;
    }
    if (type == XX_FILE_TYPE_ELF || type == XX_FILE_TYPE_ELF32 || type == XX_FILE_TYPE_ELF64) {
        xx_elf reader;
        xx_elf_init(&reader, view, 0);
        result = xx_scan_visit_resource_stream(&reader.format, offset, range_size, true, callback, context, pd);
        xx_elf_destroy(&reader);
    } else {
        result = xx_scan_visit_pe_resources(view, offset, range_size, callback, context, pd);
    }
    xx_io_close(view);
restore:
    if (xx_io_seek64(device, saved_position, SEEK_SET) != 0) return xx_scan_resource_error(pd, XXFC_ERR_IO, "Cannot restore scan resource input position");
    return result && !xx_scan_resources_stopped(pd);
}
