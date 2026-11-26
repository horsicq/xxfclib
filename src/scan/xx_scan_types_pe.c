/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xx_scan_types_internal.h"
#include "xxfclib/formats/executable/pe/xx_pe.h"
#include "xxfclib/formats/executable/dotnet/xx_dotnet.h"

xx_file_type_t xx_scan_type_detect_pe(xx_io_device *device, const uint8_t head[64], size_t head_size, int64_t size, bool *managed, xx_pd_struct *pd)
{
    uint32_t offset;
    uint8_t header[6] = {0};
    size_t index;
    xx_dotnet dotnet;
    bool valid_managed;
    if (managed) *managed = false;
    /* XFormats::_getFileTypes: an MZ/ZM file is a PE when XPE::isValid says so:
     * 0 < e_lfanew (signed 32-bit) < file size and the PE signature there
     * (bytes past the end of the file read as zero). Nothing else is checked:
     * not the optional-header magic, the section table, the size of the
     * headers or an NT header overlapping the DOS header. XPE::getFileType
     * then takes PE32 or PE64 from the machine alone. */
    if (head_size < 0x40U || size <= 0) return XX_FILE_TYPE_UNKNOWN;
    offset = xx_scan_type_u32le(head + 0x3c);
    if (offset == 0U || offset > (uint32_t)INT32_MAX || (int64_t)offset >= (size & INT64_C(0xffffffff))) return XX_FILE_TYPE_UNKNOWN;
    for (index = 0U; index < sizeof(header) && (int64_t)offset + (int64_t)index < size; ++index) {
        if (!xx_scan_type_read_at(device, (int64_t)offset + (int64_t)index, &header[index], 1U)) return XX_FILE_TYPE_UNKNOWN;
    }
    if (header[0] != 'P' || header[1] != 'E' || header[2] || header[3]) return XX_FILE_TYPE_UNKNOWN;
    xx_dotnet_init(&dotnet, device, 0);
    valid_managed = xx_dotnet_check_is_valid(&dotnet.pe.format, pd);
    xx_dotnet_destroy(&dotnet);
    if (managed) *managed = valid_managed;
    return xx_pe_machine_is_64(xx_scan_type_u16le(header + 4)) ? XX_FILE_TYPE_PE64 : XX_FILE_TYPE_PE32;
}
