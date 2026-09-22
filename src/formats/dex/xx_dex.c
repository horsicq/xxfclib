/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/dex/xx_dex.h"

#include "xxfclib/data/xx_data.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define XX_DEX_MAP_ITEM_SIZE 12U
#define XX_DEX_MAX_MAP_ITEMS 65536U

static void xx_dex_vtable_destroy(Abstractformat *self);

static bool xx_dex_version_is_supported(uint32_t version) {
    return version == 35U || (version >= 37U && version <= 40U);
}

static bool xx_dex_read_exact(xx_io_device *device, int64_t offset,
                              void *buffer, size_t size) {
    uint8_t *destination = (uint8_t *)buffer;
    size_t done = 0U;
    if (!device || !buffer || size == 0U || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0) {
        return false;
    }
    while (done < size) {
        ssize_t read_size = xx_io_read(device, destination + done,
                                       size - done);
        if (read_size <= 0) return false;
        done += (size_t)read_size;
    }
    return true;
}

static bool xx_dex_parse_header(Abstractformat *format,
                                xx_dex_header *header,
                                uint32_t *version_number,
                                bool *is_big_endian,
                                xx_pd_struct *pd) {
    uint8_t raw[XX_DEX_HEADER_SIZE];
    uint32_t raw_endian;
    uint32_t version;
    bool big_endian;
    int64_t total;
    int64_t available;

    if (!format || !format->device || !header || !version_number ||
        !is_big_endian || format->base_address < 0 ||
        xx_pd_is_stopped(pd)) {
        return false;
    }
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    available = total - format->base_address;
    if (available < (int64_t)sizeof(raw) ||
        !xx_dex_read_exact(format->device, format->base_address, raw,
                           sizeof(raw)) ||
        xx_pd_is_stopped(pd)) {
        return false;
    }

    if (raw[0] != 'd' || raw[1] != 'e' || raw[2] != 'x' ||
        raw[3] != '\n' || raw[4] < '0' || raw[4] > '9' ||
        raw[5] < '0' || raw[5] > '9' || raw[6] < '0' ||
        raw[6] > '9' || raw[7] != 0U) {
        return false;
    }
    version = (uint32_t)(raw[4] - '0') * 100U +
              (uint32_t)(raw[5] - '0') * 10U +
              (uint32_t)(raw[6] - '0');
    if (!xx_dex_version_is_supported(version)) return false;

    raw_endian = xx_data_get_u32(raw, sizeof(raw), 40U, false);
    if (raw_endian == XX_DEX_ENDIAN_CONSTANT) {
        big_endian = false;
    } else if (raw_endian == XX_DEX_REVERSE_ENDIAN_CONSTANT) {
        big_endian = true;
    } else {
        return false;
    }

    xx_mem_zero(header, sizeof(*header));
    xx_mem_copy(header->magic, raw, XX_DEX_MAGIC_SIZE);
    header->checksum = xx_data_get_u32(raw, sizeof(raw), 8U, big_endian);
    xx_mem_copy(header->signature, raw + 12U, XX_DEX_SIGNATURE_SIZE);
    header->file_size = xx_data_get_u32(raw, sizeof(raw), 32U, big_endian);
    header->header_size = xx_data_get_u32(raw, sizeof(raw), 36U, big_endian);
    /* Keep the raw tag spelling, matching the on-disk DEX header API. */
    header->endian_tag = raw_endian;
    header->link_size = xx_data_get_u32(raw, sizeof(raw), 44U, big_endian);
    header->link_off = xx_data_get_u32(raw, sizeof(raw), 48U, big_endian);
    header->map_off = xx_data_get_u32(raw, sizeof(raw), 52U, big_endian);
    header->string_ids_size =
        xx_data_get_u32(raw, sizeof(raw), 56U, big_endian);
    header->string_ids_off =
        xx_data_get_u32(raw, sizeof(raw), 60U, big_endian);
    header->type_ids_size =
        xx_data_get_u32(raw, sizeof(raw), 64U, big_endian);
    header->type_ids_off =
        xx_data_get_u32(raw, sizeof(raw), 68U, big_endian);
    header->proto_ids_size =
        xx_data_get_u32(raw, sizeof(raw), 72U, big_endian);
    header->proto_ids_off =
        xx_data_get_u32(raw, sizeof(raw), 76U, big_endian);
    header->field_ids_size =
        xx_data_get_u32(raw, sizeof(raw), 80U, big_endian);
    header->field_ids_off =
        xx_data_get_u32(raw, sizeof(raw), 84U, big_endian);
    header->method_ids_size =
        xx_data_get_u32(raw, sizeof(raw), 88U, big_endian);
    header->method_ids_off =
        xx_data_get_u32(raw, sizeof(raw), 92U, big_endian);
    header->class_defs_size =
        xx_data_get_u32(raw, sizeof(raw), 96U, big_endian);
    header->class_defs_off =
        xx_data_get_u32(raw, sizeof(raw), 100U, big_endian);
    header->data_size =
        xx_data_get_u32(raw, sizeof(raw), 104U, big_endian);
    header->data_off =
        xx_data_get_u32(raw, sizeof(raw), 108U, big_endian);

    if (header->header_size != XX_DEX_HEADER_SIZE ||
        header->file_size < header->header_size ||
        header->file_size > (uint64_t)available) {
        return false;
    }
    *version_number = version;
    *is_big_endian = big_endian;
    return true;
}

static bool xx_dex_get_absolute_offset(const Abstractformat *format,
                                       uint64_t relative,
                                       int64_t *absolute) {
    if (!format || !absolute || format->base_address < 0 ||
        relative > (uint64_t)INT64_MAX ||
        relative > (uint64_t)(INT64_MAX - format->base_address)) {
        return false;
    }
    *absolute = format->base_address + (int64_t)relative;
    return true;
}

static bool xx_dex_range_is_valid(int64_t format_size, uint64_t offset,
                                  uint64_t size) {
    return format_size >= 0 && size != 0U &&
           offset <= (uint64_t)format_size &&
           size <= (uint64_t)format_size - offset &&
           size <= (uint64_t)INT64_MAX;
}

static bool xx_dex_add_bounded_part(Abstractformat *format,
                                    xx_memory_map *output,
                                    int64_t format_size, uint64_t offset,
                                    uint64_t size, xx_file_part_t file_part,
                                    int32_t part_number, const char *name) {
    int64_t absolute;
    if (!xx_dex_range_is_valid(format_size, offset, size) ||
        !xx_dex_get_absolute_offset(format, offset, &absolute)) {
        return false;
    }
    return xx_memory_map_add_part(output, absolute, (int64_t)size,
                                  XX_INVALID_ADDRESS, 0, file_part,
                                  part_number, name, false);
}

static bool xx_dex_add_region(Abstractformat *format, xx_memory_map *output,
                              int64_t format_size, uint32_t offset,
                              uint32_t count, uint32_t entry_size,
                              int32_t *part_number, const char *name) {
    const xx_dex *dex = (const xx_dex *)format;
    uint64_t size;
    if (count == 0U) return true;
    if (!part_number || offset < dex->header.header_size) {
        return true;
    }
    size = (uint64_t)count * (uint64_t)entry_size;
    if (!xx_dex_range_is_valid(format_size, offset, size)) return true;
    if (!xx_dex_add_bounded_part(format, output, format_size, offset, size,
                                 XX_FILE_PART_REGION, *part_number, name)) {
        return false;
    }
    ++*part_number;
    return true;
}

static int xx_dex_compare_map_items(const void *left, const void *right) {
    const xx_dex_map_item *a = (const xx_dex_map_item *)left;
    const xx_dex_map_item *b = (const xx_dex_map_item *)right;
    if (a->offset < b->offset) return -1;
    if (a->offset > b->offset) return 1;
    if (a->type < b->type) return -1;
    if (a->type > b->type) return 1;
    return 0;
}

static uint32_t xx_dex_fixed_item_size(uint16_t type) {
    switch (type) {
        case XX_DEX_MAP_STRING_ID_ITEM:
        case XX_DEX_MAP_TYPE_ID_ITEM:
        case XX_DEX_MAP_CALL_SITE_ID_ITEM:
            return 4U;
        case XX_DEX_MAP_PROTO_ID_ITEM:
            return 12U;
        case XX_DEX_MAP_FIELD_ID_ITEM:
        case XX_DEX_MAP_METHOD_ID_ITEM:
        case XX_DEX_MAP_METHOD_HANDLE_ITEM:
            return 8U;
        case XX_DEX_MAP_CLASS_DEF_ITEM:
            return 32U;
        default:
            return 0U;
    }
}

const char *xx_dex_map_item_type_to_string(uint16_t type) {
    switch (type) {
        case XX_DEX_MAP_HEADER_ITEM: return "HEADER_ITEM";
        case XX_DEX_MAP_STRING_ID_ITEM: return "STRING_ID_ITEM";
        case XX_DEX_MAP_TYPE_ID_ITEM: return "TYPE_ID_ITEM";
        case XX_DEX_MAP_PROTO_ID_ITEM: return "PROTO_ID_ITEM";
        case XX_DEX_MAP_FIELD_ID_ITEM: return "FIELD_ID_ITEM";
        case XX_DEX_MAP_METHOD_ID_ITEM: return "METHOD_ID_ITEM";
        case XX_DEX_MAP_CLASS_DEF_ITEM: return "CLASS_DEF_ITEM";
        case XX_DEX_MAP_CALL_SITE_ID_ITEM: return "CALL_SITE_ID_ITEM";
        case XX_DEX_MAP_METHOD_HANDLE_ITEM: return "METHOD_HANDLE_ITEM";
        case XX_DEX_MAP_MAP_LIST: return "MAP_LIST";
        case XX_DEX_MAP_TYPE_LIST: return "TYPE_LIST";
        case XX_DEX_MAP_ANNOTATION_SET_REF_LIST:
            return "ANNOTATION_SET_REF_LIST";
        case XX_DEX_MAP_ANNOTATION_SET_ITEM: return "ANNOTATION_SET_ITEM";
        case XX_DEX_MAP_CLASS_DATA_ITEM: return "CLASS_DATA_ITEM";
        case XX_DEX_MAP_CODE_ITEM: return "CODE_ITEM";
        case XX_DEX_MAP_STRING_DATA_ITEM: return "STRING_DATA_ITEM";
        case XX_DEX_MAP_DEBUG_INFO_ITEM: return "DEBUG_INFO_ITEM";
        case XX_DEX_MAP_ANNOTATION_ITEM: return "ANNOTATION_ITEM";
        case XX_DEX_MAP_ENCODED_ARRAY_ITEM: return "ENCODED_ARRAY_ITEM";
        case XX_DEX_MAP_ANNOTATIONS_DIRECTORY_ITEM:
            return "ANNOTATIONS_DIRECTORY_ITEM";
        case XX_DEX_MAP_HIDDENAPI_CLASS_DATA_ITEM:
            return "HIDDENAPI_CLASS_DATA_ITEM";
        default: return "UNKNOWN";
    }
}

static bool xx_dex_add_regions(xx_dex *dex, xx_memory_map *output,
                               int64_t format_size, int32_t *part_number) {
    const xx_dex_header *header = &dex->header;
    uint64_t link_size = header->link_size;

    if (link_size != 0U && header->link_off >= header->header_size &&
        xx_dex_range_is_valid(format_size, header->link_off, link_size)) {
        if (!xx_dex_add_bounded_part(&dex->format, output, format_size,
                                     header->link_off, link_size,
                                     XX_FILE_PART_REGION, *part_number,
                                     "link")) {
            return false;
        }
        ++*part_number;
    }
    return xx_dex_add_region(&dex->format, output, format_size,
                             header->string_ids_off,
                             header->string_ids_size, 4U, part_number,
                             "string_ids") &&
           xx_dex_add_region(&dex->format, output, format_size,
                             header->type_ids_off, header->type_ids_size,
                             4U, part_number, "type_ids") &&
           xx_dex_add_region(&dex->format, output, format_size,
                             header->proto_ids_off, header->proto_ids_size,
                             12U, part_number, "proto_ids") &&
           xx_dex_add_region(&dex->format, output, format_size,
                             header->field_ids_off, header->field_ids_size,
                             8U, part_number, "field_ids") &&
           xx_dex_add_region(&dex->format, output, format_size,
                             header->method_ids_off,
                             header->method_ids_size, 8U, part_number,
                             "method_ids") &&
           xx_dex_add_region(&dex->format, output, format_size,
                             header->class_defs_off,
                             header->class_defs_size, 32U, part_number,
                             "class_defs") &&
           xx_dex_add_region(&dex->format, output, format_size,
                             header->data_off, header->data_size, 1U,
                             part_number, "data");
}

static bool xx_dex_add_sections(xx_dex *dex, xx_memory_map *output,
                                int64_t format_size, int32_t *part_number,
                                xx_pd_struct *pd) {
    xx_dex_map_item *items = NULL;
    uint64_t available_items;
    uint32_t declared_count;
    uint32_t item_count;
    uint32_t i;
    int64_t map_absolute;
    bool result = false;

    if (dex->header.map_off == 0U) return true;
    if (dex->header.map_off < dex->header.header_size ||
        !xx_dex_range_is_valid(format_size, dex->header.map_off, 4U) ||
        !xx_dex_get_absolute_offset(&dex->format, dex->header.map_off,
                                    &map_absolute)) {
        return true;
    }
    declared_count = xx_io_get_u32(dex->format.device, map_absolute,
                                    dex->is_big_endian);
    available_items = ((uint64_t)format_size - dex->header.map_off - 4U) /
                      XX_DEX_MAP_ITEM_SIZE;
    item_count = declared_count;
    if ((uint64_t)item_count > available_items)
        item_count = (uint32_t)available_items;
    if (item_count > XX_DEX_MAX_MAP_ITEMS)
        item_count = XX_DEX_MAX_MAP_ITEMS;
    if (item_count == 0U) return true;

    items = (xx_dex_map_item *)xx_mem_calloc(item_count, sizeof(*items));
    if (!items) return false;
    for (i = 0U; i < item_count; ++i) {
        int64_t item_absolute = map_absolute + 4 +
                                (int64_t)i * XX_DEX_MAP_ITEM_SIZE;
        if (xx_pd_is_stopped(pd)) goto done;
        items[i].type = xx_io_get_u16(dex->format.device, item_absolute,
                                      dex->is_big_endian);
        items[i].count = xx_io_get_u32(dex->format.device,
                                       item_absolute + 4,
                                       dex->is_big_endian);
        items[i].offset = xx_io_get_u32(dex->format.device,
                                        item_absolute + 8,
                                        dex->is_big_endian);
    }
    xx_rt_qsort(items, item_count, sizeof(*items), xx_dex_compare_map_items);

    for (i = 0U; i < item_count; ++i) {
        uint64_t next_offset = (uint64_t)format_size;
        uint64_t section_size;
        uint32_t fixed_size;
        uint32_t j;

        if (xx_pd_is_stopped(pd)) goto done;
        if (items[i].type == XX_DEX_MAP_HEADER_ITEM ||
            items[i].count == 0U ||
            items[i].offset < dex->header.header_size ||
            items[i].offset >= (uint64_t)format_size) {
            continue;
        }
        if (i != 0U && items[i - 1U].offset == items[i].offset)
            continue;
        for (j = i + 1U; j < item_count; ++j) {
            if (items[j].offset > items[i].offset &&
                items[j].offset <= (uint64_t)format_size) {
                next_offset = items[j].offset;
                break;
            }
        }

        fixed_size = xx_dex_fixed_item_size(items[i].type);
        if (fixed_size != 0U) {
            section_size = (uint64_t)items[i].count * fixed_size;
        } else if (items[i].type == XX_DEX_MAP_MAP_LIST) {
            uint32_t map_count;
            uint64_t maximum_count;
            int64_t section_absolute;
            if (!xx_dex_get_absolute_offset(&dex->format, items[i].offset,
                                            &section_absolute) ||
                (uint64_t)format_size - items[i].offset < 4U) {
                continue;
            }
            map_count = xx_io_get_u32(dex->format.device, section_absolute,
                                      dex->is_big_endian);
            maximum_count = ((uint64_t)format_size - items[i].offset - 4U) /
                            XX_DEX_MAP_ITEM_SIZE;
            if ((uint64_t)map_count > maximum_count)
                map_count = (uint32_t)maximum_count;
            if (map_count > XX_DEX_MAX_MAP_ITEMS)
                map_count = XX_DEX_MAX_MAP_ITEMS;
            section_size = 4U + (uint64_t)map_count * XX_DEX_MAP_ITEM_SIZE;
        } else {
            section_size = next_offset - items[i].offset;
        }
        if (section_size == 0U ||
            section_size > next_offset - items[i].offset ||
            !xx_dex_range_is_valid(format_size, items[i].offset,
                                   section_size)) {
            continue;
        }
        if (!xx_dex_add_bounded_part(&dex->format, output, format_size,
                                     items[i].offset, section_size,
                                     XX_FILE_PART_SECTION, *part_number,
                                     xx_dex_map_item_type_to_string(
                                         items[i].type))) {
            goto done;
        }
        ++*part_number;
    }
    result = true;

done:
    xx_mem_free(items);
    return result && !xx_pd_is_stopped(pd);
}

void xx_dex_init(xx_dex *dex, xx_io_device *device, int64_t base_address) {
    if (!dex) return;
    xx_mem_zero(dex, sizeof(*dex));
    xx_format_init(&dex->format, device, base_address);
    dex->format.file_type = XX_FILE_TYPE_DEX;
    dex->format.os = XX_OS_ANDROID;
    dex->format.format_type = XX_TYPE_CONSOLE_APPLICATION;
    dex->format.arch = XX_ARCH_DALVIK;
    dex->format.is_executable = true;
    xx_format_set_mime_type(&dex->format, "application/vnd.android.dex");
    xx_format_set_extension(&dex->format, "dex");
    dex->format.check_is_valid = xx_dex_check_is_valid;
    dex->format.handle_base_info = xx_dex_handle_base_info;
    dex->format.get_format_size = xx_dex_get_format_size;
    dex->format.get_memory_map = xx_dex_get_memory_map;
    dex->format.destroy = xx_dex_vtable_destroy;
}

xx_dex *xx_dex_create(xx_io_device *device, int64_t base_address) {
    xx_dex *dex = (xx_dex *)xx_mem_alloc(sizeof(*dex));
    if (!dex) return NULL;
    xx_dex_init(dex, device, base_address);
    return dex;
}

void xx_dex_destroy(xx_dex *dex) {
    if (!dex) return;
    if (dex->format.close) (void)dex->format.close(&dex->format);
    xx_format_cleanup_extra_parameters(&dex->format);
}

static void xx_dex_vtable_destroy(Abstractformat *self) {
    if (self) xx_dex_destroy((xx_dex *)self);
}

void xx_dex_free(xx_dex *dex) {
    if (!dex) return;
    xx_dex_destroy(dex);
    xx_mem_free(dex);
}

bool xx_dex_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_dex_header header;
    uint32_t version;
    bool big_endian;
    return xx_dex_parse_header(self, &header, &version, &big_endian, pd);
}

bool xx_dex_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_dex *dex;
    xx_dex_header header;
    uint32_t version;
    bool big_endian;
    int64_t total;
    int64_t available;
    int64_t format_size;
    char version_string[4];

    if (!self || xx_pd_is_stopped(pd)) return false;
    self->base_info_handled = false;
    self->is_valid = false;
    if (!xx_dex_parse_header(self, &header, &version, &big_endian, pd))
        return false;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return false;
    available = total - self->base_address;
    if (header.file_size >= header.header_size) {
        format_size = header.file_size <= (uint64_t)available
                          ? (int64_t)header.file_size
                          : available;
    } else {
        format_size = available;
    }

    dex = (xx_dex *)self;
    dex->header = header;
    dex->version_number = version;
    dex->is_big_endian = big_endian;
    self->endian = big_endian ? XX_ENDIAN_BIG : XX_ENDIAN_LITTLE;
    self->format_size = format_size;
    if (format_size < available) {
        self->overlay_offset = self->base_address + format_size;
        self->overlay_size = available - format_size;
    } else {
        self->overlay_offset = -1;
        self->overlay_size = 0;
    }
    (void)xx_rt_snprintf(version_string, sizeof(version_string), "%03u",
                   (unsigned)version);
    xx_format_set_version(self, version_string);
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_dex_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    return self && (self->base_info_handled ||
                    xx_dex_handle_base_info(self, pd))
               ? self->format_size
               : -1;
}

bool xx_dex_get_memory_map(Abstractformat *self,
                           xx_memory_map_mode_t mode,
                           xx_memory_map *output,
                           xx_pd_struct *pd) {
    xx_dex *dex;
    int64_t total;
    int64_t available;
    int64_t format_size;
    int32_t part_number = 0;

    if (!self || !output || !self->device || !self->base_info_handled ||
        self->base_address < 0 || xx_pd_is_stopped(pd)) {
        return false;
    }
    if (mode == XX_MEMORY_MAP_MODE_UNKNOWN)
        mode = XX_MEMORY_MAP_MODE_REGIONS;
    if (mode != XX_MEMORY_MAP_MODE_REGIONS &&
        mode != XX_MEMORY_MAP_MODE_SECTIONS) {
        return false;
    }
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return false;
    available = total - self->base_address;
    format_size = self->format_size;
    if (format_size < (int64_t)XX_DEX_HEADER_SIZE ||
        format_size > available) {
        return false;
    }
    dex = (xx_dex *)self;

    output->binary_offset = self->base_address;
    output->module_address = XX_INVALID_ADDRESS;
    output->is_image = self->is_mapped;
    output->binary_size = available;
    output->entry_point_address = XX_INVALID_ADDRESS;
    output->code_base = -1;
    output->start_load_offset = -1;
    output->file_type = self->file_type;
    output->format_type = self->format_type;
    output->endian = self->endian;
    output->arch = self->arch;
    output->mode = mode;

    if (!xx_dex_add_bounded_part(self, output, format_size, 0U,
                                 dex->header.header_size,
                                 XX_FILE_PART_HEADER, part_number++,
                                 "Header")) {
        return false;
    }
    if (mode == XX_MEMORY_MAP_MODE_REGIONS) {
        if (!xx_dex_add_regions(dex, output, format_size, &part_number))
            return false;
    } else if (!xx_dex_add_sections(dex, output, format_size, &part_number,
                                    pd)) {
        return false;
    }
    if (format_size < available &&
        !xx_dex_add_bounded_part(self, output, available,
                                 (uint64_t)format_size,
                                 (uint64_t)(available - format_size),
                                 XX_FILE_PART_OVERLAY, part_number,
                                 "Overlay")) {
        return false;
    }
    return !xx_pd_is_stopped(pd) && xx_memory_map_finalize(output);
}

const xx_dex_header *xx_dex_get_header(const xx_dex *dex) {
    return dex ? &dex->header : NULL;
}

uint32_t xx_dex_get_version_number(const xx_dex *dex) {
    return dex ? dex->version_number : 0U;
}

bool xx_dex_is_big_endian(const xx_dex *dex) {
    return dex && dex->is_big_endian;
}
