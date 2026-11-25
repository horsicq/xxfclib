/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/formats/executable/windows_dmp64/xx_windows_dmp64.h"

#include "xxfclib/data/xx_data.h"
#include "xxfclib/memory/xx_memory.h"

#include <limits.h>
#include <string.h>

#define XX_DMP64_MAX_BITMAP_SIZE UINT64_C(0x04000000)
#define XX_DMP64_MAX_PAGE_RECORDS UINT64_C(0x00400000)

static bool dmp64_stopped(xx_pd_struct *pd)
{
    return pd && xx_pd_is_stopped(pd);
}

static bool dmp64_magic(const Abstractformat *format)
{
    uint8_t magic[8];
    return format && format->device && format->base_address >= 0 && xx_io_read_at(format->device, format->base_address, magic, sizeof(magic)) &&
           memcmp(magic, "PAGEDU64", 8U) == 0;
}

static unsigned dmp64_popcount8(uint8_t value)
{
    unsigned count = 0U;
    while (value) {
        value &= (uint8_t)(value - 1U);
        ++count;
    }
    return count;
}

void xx_windows_dmp64_init(xx_windows_dmp64 *reader, xx_io_device *device, int64_t base_address)
{
    if (!reader) return;
    xx_mem_zero(reader, sizeof(*reader));
    xx_format_init(&reader->format, device, base_address);
    reader->format.file_type = XX_FILE_TYPE_WINDOWS_DMP64;
    reader->format.check_is_valid = xx_windows_dmp64_check_is_valid;
    reader->format.handle_base_info = xx_windows_dmp64_handle_base_info;
    reader->format.get_format_size = xx_windows_dmp64_get_format_size;
    reader->format.get_memory_map = xx_windows_dmp64_get_memory_map;
    reader->format.endian = XX_ENDIAN_LITTLE;
    reader->format.arch = XX_ARCH_X86_64;
    reader->format.os = XX_OS_WINDOWS;
    reader->format.format_type = XX_TYPE_CUSTOM;
    xx_format_set_extension(&reader->format, "dmp");
    xx_format_set_mime_type(&reader->format, "application/x-windows-kernel-dump");
}

xx_windows_dmp64 *xx_windows_dmp64_create(xx_io_device *device, int64_t base_address)
{
    xx_windows_dmp64 *reader = (xx_windows_dmp64 *)xx_mem_alloc(sizeof(*reader));
    if (reader) xx_windows_dmp64_init(reader, device, base_address);
    return reader;
}

void xx_windows_dmp64_destroy(xx_windows_dmp64 *reader)
{
    if (!reader) return;
    xx_mem_free(reader->pages);
    reader->pages = NULL;
    reader->number_of_pages = 0U;
    xx_format_cleanup_extra_parameters(&reader->format);
}

void xx_windows_dmp64_free(xx_windows_dmp64 *reader)
{
    if (!reader) return;
    xx_windows_dmp64_destroy(reader);
    xx_mem_free(reader);
}

bool xx_windows_dmp64_check_is_valid(Abstractformat *self, xx_pd_struct *pd)
{
    (void)pd;
    return dmp64_magic(self);
}

bool xx_windows_dmp64_handle_base_info(Abstractformat *self, xx_pd_struct *pd)
{
    xx_windows_dmp64 *reader = (xx_windows_dmp64 *)self;
    xx_windows_dmp64_header *header;
    xx_windows_dmp64_bitmap_header *bitmap_header;
    uint8_t fixed[XX_DMP64_HEADER_SIZE];
    uint8_t bitmap_fixed[XX_DMP64_BITMAP_HEADER_SIZE];
    uint8_t *bitmap = NULL;
    xx_windows_dmp64_page *pages = NULL;
    int64_t total;
    uint64_t available;
    uint64_t bitmap_offset = XX_DMP64_HEADER_SIZE + XX_DMP64_BITMAP_HEADER_SIZE;
    uint64_t page_index;
    uint64_t present = 0U;
    uint64_t file_page = 0U;
    bool result = false;

    if (!reader || !self->device || self->base_address < 0 || dmp64_stopped(pd)) return false;
    if (self->base_info_handled) return self->is_valid;
    total = xx_io_total_size(self->device);
    if (total < self->base_address || (available = (uint64_t)(total - self->base_address)) < XX_DMP64_HEADER_SIZE + XX_DMP64_BITMAP_HEADER_SIZE || !dmp64_magic(self) ||
        !xx_io_read_at(self->device, self->base_address, fixed, sizeof(fixed)))
        return false;

    header = &reader->header;
    header->major_version = xx_data_get_u32(fixed, sizeof(fixed), 0x08U, false);
    header->minor_version = xx_data_get_u32(fixed, sizeof(fixed), 0x0cU, false);
    header->directory_table_base = xx_data_get_u64(fixed, sizeof(fixed), 0x10U, false);
    header->pfn_database = xx_data_get_u64(fixed, sizeof(fixed), 0x18U, false);
    header->loaded_module_list = xx_data_get_u64(fixed, sizeof(fixed), 0x20U, false);
    header->active_process_head = xx_data_get_u64(fixed, sizeof(fixed), 0x28U, false);
    header->machine_image_type = xx_data_get_u32(fixed, sizeof(fixed), 0x30U, false);
    header->number_of_processors = xx_data_get_u32(fixed, sizeof(fixed), 0x34U, false);
    header->bugcheck_code = xx_data_get_u32(fixed, sizeof(fixed), 0x38U, false);
    for (unsigned index = 0U; index < 4U; ++index) header->bugcheck_parameters[index] = xx_data_get_u64(fixed, sizeof(fixed), 0x40U + index * 8U, false);
    header->debugger_data_block = xx_data_get_u64(fixed, sizeof(fixed), 0x80U, false);
    header->dump_type = xx_data_get_u32(fixed, sizeof(fixed), 0xf98U, false);
    header->required_dump_space = xx_data_get_u64(fixed, sizeof(fixed), 0xfa0U, false);
    header->secondary_data_state = xx_data_get_u32(fixed, sizeof(fixed), 0x103cU, false);
    header->product_type = xx_data_get_u32(fixed, sizeof(fixed), 0x1040U, false);
    header->suite_mask = xx_data_get_u32(fixed, sizeof(fixed), 0x1044U, false);
    if (header->machine_image_type != UINT32_C(0x8664) || (header->dump_type != XX_DMP64_DUMP_TYPE_BITMAP_FULL && header->dump_type != XX_DMP64_DUMP_TYPE_BITMAP_KERNEL))
        return false;

    if (!xx_io_read_at(self->device, self->base_address + XX_DMP64_HEADER_SIZE, bitmap_fixed, sizeof(bitmap_fixed)) || memcmp(bitmap_fixed, "SDMPDUMP", 8U) != 0)
        return false;
    bitmap_header = &reader->bitmap_header;
    bitmap_header->first_page_offset = xx_data_get_u64(bitmap_fixed, sizeof(bitmap_fixed), 0x20U, false);
    bitmap_header->present_page_count = xx_data_get_u64(bitmap_fixed, sizeof(bitmap_fixed), 0x28U, false);
    bitmap_header->addressable_page_count = xx_data_get_u64(bitmap_fixed, sizeof(bitmap_fixed), 0x30U, false);
    if (!bitmap_header->addressable_page_count || bitmap_header->addressable_page_count > UINT64_MAX - 7U) return false;
    bitmap_header->bitmap_size = (bitmap_header->addressable_page_count + 7U) / 8U;
    if (!bitmap_header->bitmap_size || bitmap_header->bitmap_size > XX_DMP64_MAX_BITMAP_SIZE ||
        bitmap_header->present_page_count > bitmap_header->addressable_page_count || bitmap_header->present_page_count > XX_DMP64_MAX_PAGE_RECORDS ||
        bitmap_header->first_page_offset < bitmap_offset || bitmap_header->bitmap_size > bitmap_header->first_page_offset - bitmap_offset ||
        bitmap_header->first_page_offset > available || bitmap_header->present_page_count > (available - bitmap_header->first_page_offset) / XX_DMP64_PAGE_SIZE ||
        bitmap_header->first_page_offset + bitmap_header->present_page_count * XX_DMP64_PAGE_SIZE != available ||
        (header->required_dump_space && header->required_dump_space != available))
        return false;

    bitmap = (uint8_t *)xx_mem_alloc((size_t)bitmap_header->bitmap_size);
    if (!bitmap || !xx_io_read_at(self->device, self->base_address + (int64_t)bitmap_offset, bitmap, (size_t)bitmap_header->bitmap_size)) goto cleanup;
    for (uint64_t index = 0U; index < bitmap_header->bitmap_size; ++index) present += dmp64_popcount8(bitmap[index]);
    /* Ignore high padding bits in the final bitmap byte. */
    if ((bitmap_header->addressable_page_count & 7U) != 0U) {
        uint8_t used_mask = (uint8_t)((UINT32_C(1) << (bitmap_header->addressable_page_count & 7U)) - 1U);
        present -= dmp64_popcount8((uint8_t)(bitmap[bitmap_header->bitmap_size - 1U] & (uint8_t)~used_mask));
    }
    if (present != bitmap_header->present_page_count) goto cleanup;
    if (present > SIZE_MAX / sizeof(*pages)) goto cleanup;
    pages = (xx_windows_dmp64_page *)xx_mem_alloc((size_t)present * sizeof(*pages));
    if (!pages) goto cleanup;
    for (page_index = 0U; page_index < bitmap_header->addressable_page_count; ++page_index) {
        if ((page_index & UINT64_C(0xfff)) == 0U && dmp64_stopped(pd)) goto cleanup;
        if ((bitmap[page_index >> 3U] & (uint8_t)(UINT32_C(1) << (page_index & 7U))) != 0U) {
            if (page_index > UINT64_MAX / XX_DMP64_PAGE_SIZE || file_page >= present) goto cleanup;
            pages[file_page].address = page_index * XX_DMP64_PAGE_SIZE;
            pages[file_page].file_offset = self->base_address + (int64_t)bitmap_header->first_page_offset + (int64_t)(file_page * XX_DMP64_PAGE_SIZE);
            ++file_page;
        }
    }
    if (file_page != present) goto cleanup;

    reader->pages = pages;
    pages = NULL;
    reader->number_of_pages = present;
    self->format_size = (int64_t)available;
    self->overlay_offset = -1;
    self->overlay_size = 0;
    self->is_valid = true;
    self->base_info_handled = true;
    result = true;
cleanup:
    xx_mem_free(pages);
    xx_mem_free(bitmap);
    return result && !dmp64_stopped(pd);
}

int64_t xx_windows_dmp64_get_format_size(Abstractformat *self, xx_pd_struct *pd)
{
    return self && (self->base_info_handled || xx_windows_dmp64_handle_base_info(self, pd)) ? self->format_size : -1;
}

const xx_windows_dmp64_header *xx_windows_dmp64_get_header(xx_windows_dmp64 *reader, xx_pd_struct *pd)
{
    return reader && (reader->format.base_info_handled || xx_windows_dmp64_handle_base_info(&reader->format, pd)) ? &reader->header : NULL;
}

const xx_windows_dmp64_bitmap_header *xx_windows_dmp64_get_bitmap_header(xx_windows_dmp64 *reader, xx_pd_struct *pd)
{
    return reader && (reader->format.base_info_handled || xx_windows_dmp64_handle_base_info(&reader->format, pd)) ? &reader->bitmap_header : NULL;
}

uint64_t xx_windows_dmp64_get_number_of_pages(xx_windows_dmp64 *reader, xx_pd_struct *pd)
{
    return reader && (reader->format.base_info_handled || xx_windows_dmp64_handle_base_info(&reader->format, pd)) ? reader->number_of_pages : 0U;
}

const xx_windows_dmp64_page *xx_windows_dmp64_get_page(xx_windows_dmp64 *reader, uint64_t index, xx_pd_struct *pd)
{
    return reader && index < xx_windows_dmp64_get_number_of_pages(reader, pd) ? &reader->pages[index] : NULL;
}

static const xx_windows_dmp64_page *dmp64_find_page(const xx_windows_dmp64 *reader, uint64_t address)
{
    uint64_t page_address = address & ~UINT64_C(0xfff);
    uint64_t low = 0U;
    uint64_t high = reader ? reader->number_of_pages : 0U;
    while (low < high) {
        uint64_t middle = low + (high - low) / 2U;
        if (reader->pages[middle].address < page_address) low = middle + 1U;
        else high = middle;
    }
    return low < reader->number_of_pages && reader->pages[low].address == page_address ? &reader->pages[low] : NULL;
}

bool xx_windows_dmp64_read_memory(xx_windows_dmp64 *reader, uint64_t address, void *buffer, size_t size, xx_pd_struct *pd)
{
    uint8_t *output = (uint8_t *)buffer;
    size_t done = 0U;
    if ((!buffer && size) || !reader || (!reader->format.base_info_handled && !xx_windows_dmp64_handle_base_info(&reader->format, pd))) return false;
    while (done < size) {
        const xx_windows_dmp64_page *page;
        uint64_t current;
        size_t within;
        size_t chunk;
        if (dmp64_stopped(pd) || address > UINT64_MAX - done) return false;
        current = address + done;
        page = dmp64_find_page(reader, current);
        if (!page) return false;
        within = (size_t)(current & (XX_DMP64_PAGE_SIZE - 1U));
        chunk = XX_DMP64_PAGE_SIZE - within;
        if (chunk > size - done) chunk = size - done;
        if (!xx_io_read_at(reader->format.device, page->file_offset + (int64_t)within, output + done, chunk)) return false;
        done += chunk;
    }
    return !dmp64_stopped(pd);
}

bool xx_windows_dmp64_get_memory_map(Abstractformat *self, xx_memory_map_mode_t mode, xx_memory_map *output, xx_pd_struct *pd)
{
    xx_windows_dmp64 *reader = (xx_windows_dmp64 *)self;
    uint64_t index;
    if (!reader || !output || (mode != XX_MEMORY_MAP_MODE_UNKNOWN && mode != XX_MEMORY_MAP_MODE_SECTIONS) ||
        (!self->base_info_handled && !xx_windows_dmp64_handle_base_info(self, pd)))
        return false;
    output->binary_offset = self->base_address;
    output->binary_size = self->format_size;
    output->module_address = 0U;
    output->entry_point_address = 0U;
    output->code_base = 0;
    output->start_load_offset = self->base_address;
    output->file_type = self->file_type;
    output->format_type = self->format_type;
    output->endian = self->endian;
    output->arch = self->arch;
    output->mode = XX_MEMORY_MAP_MODE_SECTIONS;
    output->is_image = true;
    if (!xx_memory_map_add_part(output, self->base_address, (int64_t)reader->bitmap_header.first_page_offset, XX_INVALID_ADDRESS, 0, XX_FILE_PART_HEADER, 0,
                                "DMP64 headers and bitmap", false) ||
        !xx_memory_map_reserve(output, (size_t)reader->number_of_pages + 1U))
        return false;
    for (index = 0U; index < reader->number_of_pages; ++index) {
        if ((index & UINT64_C(0xfff)) == 0U && dmp64_stopped(pd)) return false;
        if (!xx_memory_map_add_part(output, reader->pages[index].file_offset, XX_DMP64_PAGE_SIZE, reader->pages[index].address, XX_DMP64_PAGE_SIZE, XX_FILE_PART_SECTION,
                                    index <= INT32_MAX ? (int32_t)index + 1 : -1, "Memory_Section", false))
            return false;
    }
    return !dmp64_stopped(pd) && xx_memory_map_finalize(output);
}
