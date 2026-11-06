/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xx_scan_types_internal.h"
#include "xxfclib/formats/executable/pe/xx_pe.h"
#include "xxfclib/formats/executable/dotnet/xx_dotnet.h"

xx_file_type_t xx_scan_type_detect_pe(xx_io_device *device, const uint8_t head[64], size_t head_size, int64_t size,
                                      bool *managed, xx_pd_struct *pd)
{
    uint32_t offset;
    uint8_t signature[26];
    uint16_t magic;
    xx_dotnet dotnet;
    bool valid_managed;
    if (managed) *managed = false;
    if (head_size < 64U) return XX_FILE_TYPE_UNKNOWN;
    offset = xx_scan_type_u32le(head + 0x3c);
    if (offset > 1024U * 1024U || (size >= 0 && (uint64_t)offset + sizeof(signature) > (uint64_t)size) ||
        !xx_scan_type_read_at(device, offset, signature, sizeof(signature)) || memcmp(signature, "PE\0\0", 4))
        return XX_FILE_TYPE_UNKNOWN;
    magic = xx_scan_type_u16le(signature + 24);
    if (magic != 0x10bU && magic != 0x20bU) return XX_FILE_TYPE_UNKNOWN;
    /* Tiny executable packers overlap their DOS/NT headers. Require complete
     * bounded PE validation in that case; a short MZ containing only a PE
     * marker is insufficient. */
    if (offset < head_size) {
        xx_pe compact;
        bool valid;
        xx_pe_init(&compact, device, 0);
        valid = xx_pe_check_is_valid(&compact.format, pd);
        xx_pe_destroy(&compact);
        if (!valid) return XX_FILE_TYPE_MSDOS;
    }
    xx_dotnet_init(&dotnet, device, 0);
    valid_managed = xx_dotnet_check_is_valid(&dotnet.pe.format, pd);
    xx_dotnet_destroy(&dotnet);
    if (managed) *managed = valid_managed;
    return magic == 0x10bU ? XX_FILE_TYPE_PE32 : XX_FILE_TYPE_PE64;
}
