/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/formats/lx/xx_lx.h"
#include "../le/xx_linear_internal.h"

#include "xxfclib/memory/xx_memory.h"

static void xx_lx_vtable_destroy(Abstractformat *format) {
    xx_lx_destroy((xx_lx *)format);
}

void xx_lx_init(xx_lx *lx, xx_io_device *device, int64_t base_address) {
    if (!lx) return;
    xx_linear_init_impl(lx, device, base_address, XX_LX_SIGNATURE);
    lx->format.file_type = XX_FILE_TYPE_LX;
    xx_format_set_mime_type(&lx->format, "application/x-linear-executable");
    xx_format_set_extension(&lx->format, "exe");
    lx->format.check_is_valid = xx_lx_check_is_valid;
    lx->format.handle_base_info = xx_lx_handle_base_info;
    lx->format.get_format_size = xx_lx_get_format_size;
    lx->format.get_number_of_imports = xx_lx_get_number_of_imports;
    lx->format.get_number_of_exports = xx_lx_get_number_of_exports;
    lx->format.get_number_of_resources = xx_lx_get_number_of_resources;
    lx->format.get_memory_map = xx_lx_get_memory_map;
    lx->format.destroy = xx_lx_vtable_destroy;
}

xx_lx *xx_lx_create(xx_io_device *device, int64_t base_address) {
    xx_lx *lx = (xx_lx *)xx_mem_alloc(sizeof(*lx));
    if (lx) xx_lx_init(lx, device, base_address);
    return lx;
}

void xx_lx_destroy(xx_lx *lx) { xx_linear_destroy_impl(lx); }

void xx_lx_free(xx_lx *lx) {
    if (!lx) return;
    xx_lx_destroy(lx);
    xx_mem_free(lx);
}

bool xx_lx_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    return xx_linear_check_impl(format, pd, XX_LX_SIGNATURE);
}

bool xx_lx_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    return xx_linear_handle_impl(format, pd, XX_LX_SIGNATURE);
}

int64_t xx_lx_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return xx_linear_get_format_size_impl(format, pd, XX_LX_SIGNATURE);
}

uint64_t xx_lx_get_number_of_imports(Abstractformat *format,
                                     xx_pd_struct *pd) {
    return xx_linear_get_import_count_impl(format, pd, XX_LX_SIGNATURE);
}

uint64_t xx_lx_get_number_of_exports(Abstractformat *format,
                                     xx_pd_struct *pd) {
    return xx_linear_get_export_count_impl(format, pd, XX_LX_SIGNATURE);
}

uint64_t xx_lx_get_number_of_resources(Abstractformat *format,
                                       xx_pd_struct *pd) {
    return xx_linear_get_resource_count_impl(format, pd, XX_LX_SIGNATURE);
}

bool xx_lx_get_memory_map(Abstractformat *format, xx_memory_map_mode_t mode,
                          xx_memory_map *output, xx_pd_struct *pd) {
    return xx_linear_get_memory_map_impl(format, mode, output, pd,
                                         XX_LX_SIGNATURE);
}

uint32_t xx_lx_get_number_of_objects(const xx_lx *lx) {
    return lx ? lx->object_count : 0U;
}

const xx_linear_object *xx_lx_get_object(const xx_lx *lx, uint32_t index) {
    return lx && lx->objects && index < lx->object_count
               ? &lx->objects[index] : NULL;
}
