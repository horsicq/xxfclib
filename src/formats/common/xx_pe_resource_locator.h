/* SPDX-License-Identifier: MIT. Bounded PE resource-data locator for SFX readers. */
#ifndef XX_PE_RESOURCE_LOCATOR_H
#define XX_PE_RESOURCE_LOCATOR_H
#include "xx_carrier_helpers.h"

typedef struct xx_sfx_resource_section_s {
    uint32_t va;
    uint32_t raw;
    uint32_t raw_size;
} xx_sfx_resource_section;

static bool xx_sfx_resource_raw(const xx_sfx_resource_section *sections, unsigned count, int64_t total, uint32_t rva, uint32_t length, int64_t *offset)
{
    unsigned i;
    for (i = 0U; i < count; ++i) {
        uint64_t delta, at;
        if (rva < sections[i].va) continue;
        delta = (uint64_t)rva - sections[i].va;
        if (delta > sections[i].raw_size || length > (uint64_t)sections[i].raw_size - delta) continue;
        at = (uint64_t)sections[i].raw + delta;
        if (at > (uint64_t)total || length > (uint64_t)total - at) continue;
        *offset = (int64_t)at;
        return true;
    }
    return false;
}

static bool xx_sfx_resource_table(Abstractformat *format, int64_t root, uint32_t resource_size, uint32_t relative, unsigned *count)
{
    uint8_t header[16];
    uint32_t entries;
    if (relative > resource_size || resource_size - relative < 16U || !pm_read(format, root + relative, header, sizeof(header))) return false;
    entries = (uint32_t)xx_data_get_u16(header + 12, 2, 0, false) + xx_data_get_u16(header + 14, 2, 0, false);
    if (entries > 4096U || entries * 8U > resource_size - relative - 16U) return false;
    *count = entries;
    return true;
}

/* True only when a complete PE resource-data entry begins at `payload` and
 * contains `payload_size` bytes. Bytes merely inside the .rsrc section are
 * not enough: many installers put an unregistered cabinet there. */
static bool xx_sfx_pe_resource_contains(Abstractformat *format, int64_t payload, uint32_t payload_size)
{
    uint8_t dos[64], coff[24], optional[120], section[40], entry[8], data[16];
    xx_sfx_resource_section sections[96];
    uint32_t pe, resource_rva, resource_size;
    uint16_t section_count, optional_size, magic;
    int64_t total, root, section_table;
    unsigned level0_count, level0, visits = 0U;
    if (!format || payload < 0 || payload_size == 0U || (total = pm_available(format)) < 64 || !pm_read(format, 0, dos, sizeof(dos)) || dos[0] != 'M' || dos[1] != 'Z' ||
        (pe = xx_data_get_u32(dos + 60, 4, 0, false)) < 64U || pe > 1048576U || !carrier_range(total, pe, sizeof(coff)) || !pm_read(format, pe, coff, sizeof(coff)) ||
        xx_rt_memcmp(coff, "PE\0\0", 4) != 0)
        return false;
    section_count = xx_data_get_u16(coff + 6, 2, 0, false);
    optional_size = xx_data_get_u16(coff + 20, 2, 0, false);
    if (section_count == 0U || section_count > 96U || optional_size < 120U || !carrier_range(total, (uint64_t)pe + 24U, optional_size) ||
        !pm_read(format, (int64_t)pe + 24, optional, sizeof(optional)))
        return false;
    magic = xx_data_get_u16(optional, 2, 0, false);
    if (magic == 0x10bU) {
        resource_rva = xx_data_get_u32(optional + 112, 4, 0, false);
        resource_size = xx_data_get_u32(optional + 116, 4, 0, false);
    } else if (magic == 0x20bU) {
        uint8_t data_directory[8];
        if (optional_size < 136U || !pm_read(format, (int64_t)pe + 24 + 128, data_directory, sizeof(data_directory))) return false;
        resource_rva = xx_data_get_u32(data_directory, 4, 0, false);
        resource_size = xx_data_get_u32(data_directory + 4, 4, 0, false);
    } else return false;
    if (resource_rva == 0U || resource_size < 16U) return false;
    section_table = (int64_t)pe + 24 + optional_size;
    if (!carrier_range(total, section_table, (uint64_t)section_count * 40U)) return false;
    for (unsigned i = 0U; i < section_count; ++i) {
        if (!pm_read(format, section_table + (int64_t)i * 40, section, sizeof(section))) return false;
        sections[i].va = xx_data_get_u32(section + 12, 4, 0, false);
        sections[i].raw_size = xx_data_get_u32(section + 16, 4, 0, false);
        sections[i].raw = xx_data_get_u32(section + 20, 4, 0, false);
    }
    if (!xx_sfx_resource_raw(sections, section_count, total, resource_rva, resource_size, &root) ||
        !xx_sfx_resource_table(format, root, resource_size, 0U, &level0_count))
        return false;
    for (level0 = 0U; level0 < level0_count; ++level0) {
        unsigned level1_count, level1;
        uint32_t next0;
        if (++visits > 16384U || !pm_read(format, root + 16 + (int64_t)level0 * 8, entry, sizeof(entry))) return false;
        next0 = xx_data_get_u32(entry + 4, 4, 0, false);
        if (!(next0 & 0x80000000U) || !xx_sfx_resource_table(format, root, resource_size, next0 & 0x7fffffffU, &level1_count)) continue;
        for (level1 = 0U; level1 < level1_count; ++level1) {
            unsigned level2_count, level2;
            uint32_t next1, dir1 = next0 & 0x7fffffffU;
            if (++visits > 16384U || !pm_read(format, root + dir1 + 16 + (int64_t)level1 * 8, entry, sizeof(entry))) return false;
            next1 = xx_data_get_u32(entry + 4, 4, 0, false);
            if (!(next1 & 0x80000000U) || !xx_sfx_resource_table(format, root, resource_size, next1 & 0x7fffffffU, &level2_count)) continue;
            for (level2 = 0U; level2 < level2_count; ++level2) {
                uint32_t next2, dir2 = next1 & 0x7fffffffU;
                uint32_t data_rva, data_size;
                int64_t file_at;
                if (++visits > 16384U || !pm_read(format, root + dir2 + 16 + (int64_t)level2 * 8, entry, sizeof(entry))) return false;
                next2 = xx_data_get_u32(entry + 4, 4, 0, false);
                if ((next2 & 0x80000000U) || next2 > resource_size || resource_size - next2 < 16U || !pm_read(format, root + next2, data, sizeof(data))) continue;
                data_rva = xx_data_get_u32(data, 4, 0, false);
                data_size = xx_data_get_u32(data + 4, 4, 0, false);
                if (data_size >= payload_size && xx_sfx_resource_raw(sections, section_count, total, data_rva, data_size, &file_at) && file_at == payload) return true;
            }
        }
    }
    return false;
}

#endif
