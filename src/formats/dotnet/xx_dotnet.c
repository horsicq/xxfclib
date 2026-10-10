/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxfclib/formats/dotnet/xx_dotnet.h"
#include "xx_dotnet_data.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"

static void dotnet_destroy_format(Abstractformat *format)
{
    xx_dotnet_destroy((xx_dotnet *)format);
}

void xx_dotnet_init(xx_dotnet *reader, xx_io_device *device, int64_t base)
{
    if (!reader) return;
    xx_pe_init(&reader->pe, device, base);
    reader->pe.format.file_type = XX_FILE_TYPE_DOTNET;
    reader->pe.format.check_is_valid = xx_dotnet_check_is_valid;
    reader->pe.format.handle_base_info = xx_dotnet_handle_base_info;
    reader->pe.format.get_format_size = xx_dotnet_get_format_size;
    reader->pe.format.destroy = dotnet_destroy_format;
    xx_dotnet_setup_data_struct_callbacks(reader);
}

xx_dotnet *xx_dotnet_create(xx_io_device *device, int64_t base)
{
    xx_dotnet *reader = (xx_dotnet *)xx_mem_alloc(sizeof(*reader));
    if (reader) xx_dotnet_init(reader, device, base);
    return reader;
}

void xx_dotnet_destroy(xx_dotnet *reader)
{
    if (reader) xx_pe_destroy(&reader->pe);
}

void xx_dotnet_free(xx_dotnet *reader)
{
    if (!reader) return;
    xx_dotnet_destroy(reader);
    xx_mem_free(reader);
}

bool xx_dotnet_check_is_valid(Abstractformat *format, xx_pd_struct *pd)
{
    xx_dotnet_inspection state = {0};
    xx_dotnet reader;
    int result;
    if (!format || !format->device) return false;
    /* The independent path does not mutate the caller's parsed state. */
    xx_dotnet_init(&reader, format->device, format->base_address);
    reader.pe.format.is_mapped = format->is_mapped;
    reader.pe.format.module_address = format->module_address;
    result = xx_dotnet_inspect_parse(&state, &reader, pd);
    xx_dotnet_inspect_free(&state);
    xx_dotnet_destroy(&reader);
    return result != 0;
}

bool xx_dotnet_handle_base_info(Abstractformat *format, xx_pd_struct *pd)
{
    xx_dotnet_inspection state = {0};
    int result;
    int64_t saved;
    if (!format || !format->device) return false;
    saved = xx_io_tell(format->device);
    if (!xx_pe_handle_base_info(format, pd)) {
        format->file_type = XX_FILE_TYPE_DOTNET;
        if (saved >= 0) xx_io_seek64(format->device, saved, XX_RT_SEEK_SET);
        return false;
    }
    /* PE has set base_info_handled, so its inspector will not dispatch back
     * into this derived callback. */
    result = xx_dotnet_inspect_parse(&state, (xx_dotnet *)format, pd);
    xx_dotnet_inspect_free(&state);
    if (saved >= 0 && xx_io_seek64(format->device, saved, XX_RT_SEEK_SET) != 0) {
        format->is_valid = false;
        format->base_info_handled = false;
        xx_format_invalidate_memory_map(format);
        return false;
    }
    return result != 0;
}

int64_t xx_dotnet_get_format_size(Abstractformat *format, xx_pd_struct *pd)
{
    return format && (format->base_info_handled || xx_dotnet_handle_base_info(format, pd)) ? format->format_size : -1;
}
