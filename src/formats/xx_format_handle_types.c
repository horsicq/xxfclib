/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Lightweight handle identities: no dependency on the native reader catalog.
 */
#include "xxfclib/formats/xx_format.h"
#include "xx_format_handle_policy.h"

typedef struct xx_unpack_handle_identity {
    xx_handle_type_t handle;
    xx_file_type_t file_type;
    unsigned carrier;
    const char *name;
} xx_unpack_handle_identity;

static const xx_unpack_handle_identity identities[] = {
#define XX_UNPACK_HANDLE(name, value, type, carrier) {XX_HANDLE_TYPE_##name, XX_FILE_TYPE_##type, carrier, #name},
#if !XFU_FORMAT_SUPPORT_FULL
#define XX_SUPPORTED_FORMAT(type, name, label) {XX_HANDLE_TYPE_##type, XX_FILE_TYPE_##type, 0, #type},
#include "xxfclib/formats/xx_format_support_rows.inc"
#undef XX_SUPPORTED_FORMAT
#else
#include "xxfclib/formats/xx_format_unpack_handle_rows.inc"
#endif
#undef XX_UNPACK_HANDLE
};

static const xx_unpack_handle_identity *xx_handle_identity(xx_handle_type_t type)
{
    size_t i;
    for (i = 0; i < sizeof(identities) / sizeof(identities[0]); ++i)
        if (identities[i].handle == type) return &identities[i];
    return NULL;
}

static xx_handle_type_t xx_handle_identity_from_file_type(xx_file_type_t type)
{
    size_t i;
    for (i = 0; i < sizeof(identities) / sizeof(identities[0]); ++i)
        if (!identities[i].carrier && identities[i].file_type == type) return identities[i].handle;
    return XX_HANDLE_TYPE_UNKNOWN;
}

xx_file_type_t xx_format_handle_type_file_type(xx_handle_type_t type)
{
    const xx_unpack_handle_identity *identity = xx_handle_identity(type);
    return identity ? identity->file_type : XX_FILE_TYPE_UNKNOWN;
}

const char *xx_format_handle_type_to_string(xx_handle_type_t type)
{
    const xx_unpack_handle_identity *identity = xx_handle_identity(type);
    return identity ? identity->name : "UNKNOWN";
}

static unsigned xx_handle_u16(const uint8_t *p)
{
    return (unsigned)p[0] | (unsigned)p[1] << 8;
}

static uint32_t xx_handle_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* Authenticate the bounded physical PE headers, without linking the PE
 * inspection reader into lightweight runtimes. An MZ stub and a coincidental
 * PE signature alone do not establish a PE unpacking interpretation. */
static bool xx_handle_pe_carrier(Abstractformat *format, uint32_t pe)
{
    uint8_t coff[24], optional[112], section[40];
    int64_t total = xx_io_total_size(format->device), base = format->base_address;
    uint64_t available, table;
    unsigned count, size, magic, minimum, i;
    if (total < base || pe < 64U || pe > 1048576U) return false;
    available = (uint64_t)(total - base);
    if (pe > available || sizeof(coff) > available - pe || !xx_io_read_at(format->device, base + pe, coff, sizeof(coff)) ||
        coff[0] != 'P' || coff[1] != 'E' || coff[2] || coff[3]) return false;
    count = xx_handle_u16(coff + 6);
    size = xx_handle_u16(coff + 20);
    if (!count || count > 96U || size < 96U || size > 4096U || size > available - pe - sizeof(coff) ||
        !xx_io_read_at(format->device, base + pe + sizeof(coff), optional, 96U)) return false;
    magic = xx_handle_u16(optional);
    minimum = magic == 0x10bU ? 96U : magic == 0x20bU ? 112U : 0U;
    if (!minimum || size < minimum || (minimum > 96U && !xx_io_read_at(format->device, base + pe + sizeof(coff), optional, minimum))) return false;
    if (!xx_handle_u32(optional + 60) || xx_handle_u32(optional + 60) > available) return false;
    table = (uint64_t)pe + sizeof(coff) + size;
    if (table > available || (uint64_t)count * sizeof(section) > available - table) return false;
    for (i = 0; i < count; ++i) {
        uint32_t at, bytes;
        if (!xx_io_read_at(format->device, base + (int64_t)table + (int64_t)i * sizeof(section), section, sizeof(section))) return false;
        bytes = xx_handle_u32(section + 16);
        at = xx_handle_u32(section + 20);
        if (bytes && (at > available || bytes > available - at)) return false;
    }
    return true;
}

/* The native reader separately authenticates its complete archive framing
 * before detection publishes a result. */
static unsigned xx_handle_carrier(Abstractformat *format)
{
    uint8_t dos[64];
    uint32_t pe;
    int64_t base = format->base_address;
    if (!format->device || base < 0 || !xx_io_read_at(format->device, base, dos, 2U)) return 0;
    if ((dos[0] == 'M' && dos[1] == 'Z') || (dos[0] == 'Z' && dos[1] == 'M')) {
        if (xx_io_read_at(format->device, base, dos, sizeof(dos))) {
            pe = xx_handle_u32(dos + 60);
            if (dos[0] == 'M' && xx_handle_pe_carrier(format, pe)) return 1U;
        }
        return 2U;
    }
    /* Headerless DOS/CP/M executable stubs. This is only used after an SFX
     * reader has accepted the input, never to recognize arbitrary COM data. */
    if (dos[0] == 0xe9U || dos[0] == 0xebU || dos[0] == 0x18U || dos[0] == 0xc3U) return 3U;
    return 0;
}

xx_handle_type_t xx_format_get_handle_type(Abstractformat *format)
{
    unsigned carrier;
    size_t i;
    if (!format) return XX_HANDLE_TYPE_UNKNOWN;
    if (xx_format_handle_is_inspection_only(format->file_type) || format->file_type == XX_FILE_TYPE_DER ||
        (format->base_info_handled && !format->is_archive) || !format->create_archive_records_reading || !format->get_current_archive_record || !format->unpack_current_archive_record ||
        !format->archive_record_move_to_next || !format->free_archive_records_reading) {
        format->handle_type = XX_HANDLE_TYPE_UNKNOWN;
        return XX_HANDLE_TYPE_UNKNOWN;
    }
    /* A factory may have been given an unvalidated requested carrier. Always
     * refresh from the actual device; PE and DOS handles can share a reader. */
    carrier = xx_handle_carrier(format);
    if (carrier) {
        for (i = 0; i < sizeof(identities) / sizeof(identities[0]); ++i) {
            if (identities[i].carrier == carrier && identities[i].file_type == format->file_type) {
                format->handle_type = identities[i].handle;
                return format->handle_type;
            }
        }
    }
    format->handle_type = xx_handle_identity_from_file_type(format->file_type);
    return format->handle_type;
}
