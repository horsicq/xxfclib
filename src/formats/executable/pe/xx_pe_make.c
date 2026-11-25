/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxfclib/formats/executable/pe/xx_pe_make.h"

#include <string.h>

#define PE_MAKE_NT_OFFSET 0x80U
#define PE_MAKE_OPTIONAL_OFFSET (PE_MAKE_NT_OFFSET + 24U)
#define PE_MAKE_OPTIONAL_SIZE 240U
#define PE_MAKE_DIRECTORIES (PE_MAKE_OPTIONAL_OFFSET + 112U)
#define PE_MAKE_SECTIONS (PE_MAKE_OPTIONAL_OFFSET + PE_MAKE_OPTIONAL_SIZE)
#define PE_MAKE_FILE_ALIGNMENT 0x200U
#define PE_MAKE_SECTION_ALIGNMENT 0x1000U
#define PE_MAKE_TEXT_RVA 0x1000U

static void pe_put_u16(xx_buf_t *out, size_t offset, uint16_t value)
{
    out->data[offset] = (char)(uint8_t)value;
    out->data[offset + 1U] = (char)(uint8_t)(value >> 8);
}

static void pe_put_u32(xx_buf_t *out, size_t offset, uint32_t value)
{
    pe_put_u16(out, offset, (uint16_t)value);
    pe_put_u16(out, offset + 2U, (uint16_t)(value >> 16));
}

static bool pe_zeros(xx_buf_t *out, size_t count)
{
    static const uint8_t zeros[256] = {0};
    while (count) {
        size_t chunk = count < sizeof(zeros) ? count : sizeof(zeros);
        if (!xx_buf_append(out, zeros, chunk)) return false;
        count -= chunk;
    }
    return true;
}

static size_t pe_align(size_t value, size_t alignment)
{
    return (value + alignment - 1U) / alignment * alignment;
}

static void pe_section(xx_buf_t *out, size_t header, size_t index, const char *name, uint32_t virtual_size, uint32_t rva, uint32_t raw_size, uint32_t raw_pointer,
                       uint32_t characteristics)
{
    size_t at = header + index * 40U;
    memcpy(out->data + at, name, strlen(name));
    pe_put_u32(out, at + 8U, virtual_size);
    pe_put_u32(out, at + 12U, rva);
    pe_put_u32(out, at + 16U, raw_size);
    pe_put_u32(out, at + 20U, raw_pointer);
    pe_put_u32(out, at + 36U, characteristics);
}

static void pe_directory(xx_buf_t *out, size_t base, unsigned int index, uint32_t address, uint32_t size)
{
    pe_put_u32(out, base + PE_MAKE_DIRECTORIES + index * 8U, address);
    pe_put_u32(out, base + PE_MAKE_DIRECTORIES + index * 8U + 4U, size);
}

uint32_t xx_pe_checksum(const uint8_t *image, size_t size, size_t checksum_offset)
{
    uint64_t sum = 0U;
    size_t index;
    if (!image) return 0U;
    for (index = 0U; index + 1U < size; index += 2U) {
        if (index == checksum_offset || index == checksum_offset + 2U) continue;
        sum += (uint32_t)image[index] | ((uint32_t)image[index + 1U] << 8);
        sum = (sum & 0xFFFFU) + (sum >> 16);
    }
    if (size & 1U) {
        sum += image[size - 1U];
        sum = (sum & 0xFFFFU) + (sum >> 16);
    }
    sum = (sum & 0xFFFFU) + (sum >> 16);
    return (uint32_t)(sum + size);
}

bool xx_pe_make_image(xx_buf_t *out, const xx_pe_make_options *options)
{
    size_t base;
    size_t optional;
    bool has_resources;
    uint32_t headers_size;
    uint32_t text_raw;
    uint32_t rsrc_raw;
    uint32_t rsrc_raw_size = 0U;
    uint32_t reloc_rva;
    uint32_t reloc_raw;
    uint32_t image_end = PE_MAKE_TEXT_RVA + PE_MAKE_SECTION_ALIGNMENT;
    uint16_t sections = 1U;
    size_t index = 0U;

    if (!out || !options || (options->resources_size && !options->resources) || (options->overlay_size && !options->overlay) ||
        (options->certificate_size && !options->certificate) || options->resources_size > 0x10000000U)
        return false;
    has_resources = options->resources && options->resources_size;
    if (has_resources) {
        rsrc_raw_size = (uint32_t)pe_align(options->resources_size, PE_MAKE_FILE_ALIGNMENT);
        ++sections;
    }
    reloc_rva = has_resources ? XX_PE_MAKE_RESOURCE_RVA + (uint32_t)pe_align(options->resources_size, PE_MAKE_SECTION_ALIGNMENT) : XX_PE_MAKE_RESOURCE_RVA;
    if (options->reloc_after_resources) ++sections;
    /* Compact headers end exactly at the section table. */
    headers_size = options->compact_headers ? (uint32_t)pe_align(PE_MAKE_SECTIONS + 40U * (size_t)sections, PE_MAKE_FILE_ALIGNMENT) : 0x400U;
    text_raw = headers_size;
    rsrc_raw = text_raw + PE_MAKE_FILE_ALIGNMENT;
    reloc_raw = rsrc_raw + rsrc_raw_size;

    /* The image may be appended to existing content: offsets are relative. */
    base = out->size;
    if (!pe_zeros(out, headers_size)) return false;
    out->data[base] = 'M';
    out->data[base + 1U] = 'Z';
    pe_put_u32(out, base + 0x3CU, PE_MAKE_NT_OFFSET);
    memcpy(out->data + base + PE_MAKE_NT_OFFSET, "PE\0\0", 4U);
    pe_put_u16(out, base + PE_MAKE_NT_OFFSET + 4U, options->machine ? options->machine : 0x8664U);
    pe_put_u16(out, base + PE_MAKE_NT_OFFSET + 6U, sections);
    pe_put_u16(out, base + PE_MAKE_NT_OFFSET + 20U, PE_MAKE_OPTIONAL_SIZE);
    pe_put_u16(out, base + PE_MAKE_NT_OFFSET + 22U, 0x0022U); /* EXECUTABLE | LARGE_ADDRESS_AWARE */
    optional = base + PE_MAKE_OPTIONAL_OFFSET;
    pe_put_u16(out, optional, 0x20BU);
    pe_put_u32(out, optional + 4U, PE_MAKE_FILE_ALIGNMENT);
    pe_put_u32(out, optional + 16U, PE_MAKE_TEXT_RVA); /* entry point */
    pe_put_u32(out, optional + 20U, PE_MAKE_TEXT_RVA); /* base of code */
    pe_put_u32(out, optional + 24U, 0x40000000U);      /* image base 0x140000000 */
    pe_put_u32(out, optional + 28U, 0x1U);
    pe_put_u32(out, optional + 32U, PE_MAKE_SECTION_ALIGNMENT);
    pe_put_u32(out, optional + 36U, PE_MAKE_FILE_ALIGNMENT);
    pe_put_u16(out, optional + 40U, 6U); /* OS version */
    pe_put_u16(out, optional + 48U, 6U); /* subsystem version */
    pe_put_u32(out, optional + 60U, headers_size);
    pe_put_u16(out, optional + 68U, 3U);      /* console */
    pe_put_u16(out, optional + 70U, 0x8160U); /* NX, dynamic base, high entropy, TS aware */
    pe_put_u32(out, optional + 72U, 0x100000U);
    pe_put_u32(out, optional + 80U, 0x1000U);
    pe_put_u32(out, optional + 88U, 0x100000U);
    pe_put_u32(out, optional + 96U, 0x1000U);
    pe_put_u32(out, optional + 108U, 16U);

    pe_section(out, base + PE_MAKE_SECTIONS, index++, ".text", 0x10U, PE_MAKE_TEXT_RVA, PE_MAKE_FILE_ALIGNMENT, text_raw, 0x60000020U);
    if (has_resources) {
        pe_section(out, base + PE_MAKE_SECTIONS, index++, ".rsrc", (uint32_t)options->resources_size, XX_PE_MAKE_RESOURCE_RVA, rsrc_raw_size, rsrc_raw, 0x40000040U);
        pe_directory(out, base, 2U, XX_PE_MAKE_RESOURCE_RVA, (uint32_t)options->resources_size);
        image_end = reloc_rva;
    }
    if (options->reloc_after_resources) {
        pe_section(out, base + PE_MAKE_SECTIONS, index++, ".reloc", 0x0CU, reloc_rva, PE_MAKE_FILE_ALIGNMENT, reloc_raw, 0x42000040U);
        pe_directory(out, base, 5U, reloc_rva, 0x0CU);
        image_end = reloc_rva + PE_MAKE_SECTION_ALIGNMENT;
    }
    pe_put_u32(out, optional + 56U, image_end);

    /* .text: a single ret. */
    if (!pe_zeros(out, PE_MAKE_FILE_ALIGNMENT)) return false;
    ((unsigned char *)out->data)[base + text_raw] = 0xC3U;
    if (has_resources && (!xx_buf_append(out, options->resources, options->resources_size) || !pe_zeros(out, rsrc_raw_size - options->resources_size))) return false;
    if (options->reloc_after_resources) {
        /* One empty base-relocation block for the .text page. */
        size_t at = out->size;
        if (!pe_zeros(out, PE_MAKE_FILE_ALIGNMENT)) return false;
        pe_put_u32(out, at, PE_MAKE_TEXT_RVA);
        pe_put_u32(out, at + 4U, 0x0CU);
    }
    if (options->overlay_size && !xx_buf_append(out, options->overlay, options->overlay_size)) return false;
    if (options->certificate_size) {
        size_t offset;
        if (!pe_zeros(out, pe_align(out->size - base, 8U) - (out->size - base))) return false;
        offset = out->size - base;
        if (offset > UINT32_MAX || options->certificate_size > UINT32_MAX || !xx_buf_append(out, options->certificate, options->certificate_size)) return false;
        pe_directory(out, base, 4U, (uint32_t)offset, (uint32_t)options->certificate_size);
    }
    if (!xx_buf_ok(out)) return false;
    if (options->checksum) pe_put_u32(out, optional + 64U, xx_pe_checksum((const uint8_t *)out->data + base, out->size - base, PE_MAKE_OPTIONAL_OFFSET + 64U));
    return true;
}
