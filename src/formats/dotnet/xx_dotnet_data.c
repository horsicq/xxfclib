/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xx_dotnet_data.h"
#include "../pe/xx_pe_data.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/rt/xx_rt.h"
#include <limits.h>

#define PE_FIELD(name_, type_, offset_, size_, property_) {L##name_, L##type_, offset_, size_, property_}
#define PE_COUNT(array_) (sizeof(array_) / sizeof((array_)[0]))
#define PE_DIR_COM_DESCRIPTOR 14U
#define pe_rva_range xx_pe_data_rva_range
#define pe_append_absolute xx_pe_data_append_absolute
#define pe_append_raw_absolute xx_pe_data_append_raw
#define pe_cstring_size xx_pe_data_cstring_size

static uint32_t pe_u32(const xx_pe *pe, int64_t offset)
{
    return xx_io_get_u32(pe->format.device, offset, false);
}
static bool pe_align_up(uint64_t value, uint64_t alignment, uint64_t *result)
{
    if (!alignment || value > UINT64_MAX - (alignment - 1U)) return false;
    *result = (value + alignment - 1U) & ~(alignment - 1U);
    return true;
}
static bool pe_u64_product(uint64_t left, uint64_t right, uint64_t *result)
{
    if (right && left > UINT64_MAX / right) return false;
    *result = left * right;
    return true;
}
static bool pe_append_raw_rva(xx_pe *pe, xx_pe_data_stream *stream, const xx_memory_map *map, uint32_t id, uint64_t rva, uint64_t size)
{
    int64_t offset;
    return !size || (pe_rva_range(pe, map, rva, size, &offset) && pe_append_raw_absolute(pe, stream, id, offset, size));
}

static const xx_data_struct_field_desc pe_cor20_fields[] = {
    PE_FIELD("cb", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    PE_FIELD("MajorRuntimeVersion", "uint16", 4, 2, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    PE_FIELD("MinorRuntimeVersion", "uint16", 6, 2, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    PE_FIELD("MetaData.VirtualAddress", "uint32", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("MetaData.Size", "uint32", 12, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    PE_FIELD("Flags", "uint32", 16, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    PE_FIELD("EntryPointTokenOrRVA", "uint32", 20, 4, XX_DATA_STRUCT_RECORD_PROPERTY_POINTER),
    PE_FIELD("Resources.VirtualAddress", "uint32", 24, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("Resources.Size", "uint32", 28, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    PE_FIELD("StrongNameSignature.VirtualAddress", "uint32", 32, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("StrongNameSignature.Size", "uint32", 36, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    PE_FIELD("CodeManagerTable.VirtualAddress", "uint32", 40, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("CodeManagerTable.Size", "uint32", 44, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    PE_FIELD("VTableFixups.VirtualAddress", "uint32", 48, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("VTableFixups.Size", "uint32", 52, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    PE_FIELD("ExportAddressTableJumps.VirtualAddress", "uint32", 56, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("ExportAddressTableJumps.Size", "uint32", 60, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    PE_FIELD("ManagedNativeHeader.VirtualAddress", "uint32", 64, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("ManagedNativeHeader.Size", "uint32", 68, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE)};

static const xx_data_struct_field_desc pe_clr_root_fields[] = {
    PE_FIELD("Signature", "char[4]", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID), PE_FIELD("MajorVersion", "uint16", 4, 2, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    PE_FIELD("MinorVersion", "uint16", 6, 2, XX_DATA_STRUCT_RECORD_PROPERTY_NONE), PE_FIELD("Reserved", "uint32", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_RESERVED),
    PE_FIELD("VersionLength", "uint32", 12, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE)};

static const xx_data_struct_field_desc pe_clr_storage_fields[] = {PE_FIELD("Flags", "uint8", 0, 1, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
                                                                  PE_FIELD("Pad", "uint8", 1, 1, XX_DATA_STRUCT_RECORD_PROPERTY_RESERVED),
                                                                  PE_FIELD("Streams", "uint16", 2, 2, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT)};

static const xx_data_struct_field_desc pe_clr_stream_fields[] = {PE_FIELD("Offset", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
                                                                 PE_FIELD("Size", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE)};

static const xx_data_struct_field_desc pe_clr_vtable_fields[] = {PE_FIELD("RVA", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
                                                                 PE_FIELD("Count", "uint16", 4, 2, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
                                                                 PE_FIELD("Type", "uint16", 6, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS)};

static bool pe_append_clr_metadata(xx_pe *pe, xx_pe_data_stream *stream, const xx_memory_map *map, uint32_t rva, uint32_t size, xx_pd_struct *pd)
{
    int64_t offset;
    uint32_t version_length;
    uint64_t storage_relative;
    uint16_t streams;
    uint64_t cursor;
    if (!rva || size < 20U || !pe_rva_range(pe, map, rva, size, &offset)) return true;
    if (pe_u32(pe, offset) != UINT32_C(0x424a5342)) return pe_append_raw_absolute(pe, stream, XX_DOTNET_DATA_STRUCT_CLR_RAW, offset, size);
    version_length = pe_u32(pe, offset + 12);
    if (version_length > size - 16U || !pe_align_up(16U + version_length, 4U, &storage_relative) || storage_relative > size - 4U)
        return pe_append_raw_absolute(pe, stream, XX_DOTNET_DATA_STRUCT_CLR_RAW, offset, size);
    if (!pe_append_absolute(pe, stream, XX_DOTNET_DATA_STRUCT_CLR_METADATA_ROOT, offset, 16U, 16U, 1U, XX_DATA_STRUCT_TYPE_STRUCT) ||
        (version_length &&
         !pe_append_absolute(pe, stream, XX_PE_DATA_STRUCT_ASCII_STRING, offset + 16, 1U, version_length, version_length, XX_DATA_STRUCT_TYPE_RAW_DATA)) ||
        !pe_append_raw_absolute(pe, stream, XX_DATA_STRUCT_ID_RAW_DATA, offset + 16 + version_length, storage_relative - 16U - version_length) ||
        !pe_append_absolute(pe, stream, XX_DOTNET_DATA_STRUCT_CLR_METADATA_STORAGE_HEADER, offset + (int64_t)storage_relative, 4U, 4U, 1U, XX_DATA_STRUCT_TYPE_STRUCT))
        return false;
    streams = xx_io_get_u16(pe->format.device, offset + (int64_t)storage_relative + 2, false);
    cursor = storage_relative + 4U;
    for (uint16_t index = 0U; index < streams; ++index) {
        uint64_t name_length;
        uint64_t next;
        uint32_t stream_offset;
        uint32_t stream_size;
        if (xx_pd_is_stopped(pd)) return false;
        if (size - cursor < 9U) break;
        name_length = pe_cstring_size(pe, offset + (int64_t)cursor + 8, size - cursor - 8U);
        if (!name_length || !pe_align_up(cursor + 8U + name_length, 4U, &next) || next > size) break;
        if (!pe_append_absolute(pe, stream, XX_DOTNET_DATA_STRUCT_CLR_STREAM_HEADER, offset + (int64_t)cursor, next - cursor, next - cursor, 1U,
                                XX_DATA_STRUCT_TYPE_ENTRY))
            return false;
        stream_offset = pe_u32(pe, offset + (int64_t)cursor);
        stream_size = pe_u32(pe, offset + (int64_t)cursor + 4);
        if (stream_offset <= size && stream_size <= size - stream_offset &&
            !pe_append_raw_absolute(pe, stream, XX_DOTNET_DATA_STRUCT_CLR_RAW, offset + stream_offset, stream_size))
            return false;
        cursor = next;
    }
    return true;
}

static bool pe_append_clr_raw_directory(xx_pe *pe, xx_pe_data_stream *stream, const xx_memory_map *map, int64_t cor_offset, uint32_t field_offset)
{
    uint32_t rva = pe_u32(pe, cor_offset + field_offset);
    uint32_t size = pe_u32(pe, cor_offset + field_offset + 4);
    if (!rva || !size || !pe_rva_range(pe, map, rva, size, NULL)) return true;
    return pe_append_raw_rva(pe, stream, map, XX_DOTNET_DATA_STRUCT_CLR_RAW, rva, size);
}

static bool pe_append_clr(xx_pe *pe, xx_pe_data_stream *stream, const xx_memory_map *map, xx_pd_struct *pd)
{
    uint32_t rva = pe->data_directory_rva[PE_DIR_COM_DESCRIPTOR];
    uint32_t size = pe->data_directory_size[PE_DIR_COM_DESCRIPTOR];
    int64_t offset;
    uint32_t cb;
    uint32_t fixup_rva;
    uint32_t fixup_size;
    int64_t fixup_offset;
    uint64_t fixup_count;
    if (!rva || !size) return true;
    if (size < 72U || !pe_rva_range(pe, map, rva, size, &offset)) return true;
    cb = pe_u32(pe, offset);
    if (cb < 72U) return pe_append_raw_absolute(pe, stream, XX_PE_DATA_STRUCT_RESERVED_DIRECTORY_RAW, offset, size);
    if (!pe_append_absolute(pe, stream, XX_DOTNET_DATA_STRUCT_COR20_HEADER, offset, 72U, 72U, 1U, XX_DATA_STRUCT_TYPE_STRUCT) ||
        !pe_append_raw_absolute(pe, stream, XX_PE_DATA_STRUCT_RESERVED_DIRECTORY_RAW, offset + 72, size - 72U) ||
        !pe_append_clr_metadata(pe, stream, map, pe_u32(pe, offset + 8), pe_u32(pe, offset + 12), pd))
        return false;
    if (!pe_append_clr_raw_directory(pe, stream, map, offset, 24U) || !pe_append_clr_raw_directory(pe, stream, map, offset, 32U) ||
        !pe_append_clr_raw_directory(pe, stream, map, offset, 40U) || !pe_append_clr_raw_directory(pe, stream, map, offset, 56U) ||
        !pe_append_clr_raw_directory(pe, stream, map, offset, 64U))
        return false;
    fixup_rva = pe_u32(pe, offset + 48);
    fixup_size = pe_u32(pe, offset + 52);
    if (!fixup_rva || fixup_size < 8U || !pe_rva_range(pe, map, fixup_rva, fixup_size, &fixup_offset)) return true;
    fixup_count = fixup_size / 8U;
    if (!pe_append_absolute(pe, stream, XX_DOTNET_DATA_STRUCT_CLR_VTABLE_FIXUP, fixup_offset, 8U, fixup_count * 8U, fixup_count, XX_DATA_STRUCT_TYPE_ENTRY)) return false;
    for (uint64_t index = 0U; index < fixup_count; ++index) {
        int64_t entry = fixup_offset + (int64_t)(index * 8U);
        if (xx_pd_is_stopped(pd)) return false;
        uint32_t table_rva = pe_u32(pe, entry);
        uint16_t table_count = xx_io_get_u16(pe->format.device, entry + 4, false);
        uint16_t type = xx_io_get_u16(pe->format.device, entry + 6, false);
        uint64_t stride = (type & 2U) ? 8U : 4U;
        uint64_t bytes;
        if (pe_u64_product(table_count, stride, &bytes) && pe_rva_range(pe, map, table_rva, bytes, NULL) &&
            !pe_append_raw_rva(pe, stream, map, XX_DOTNET_DATA_STRUCT_CLR_RAW, table_rva, bytes))
            return false;
    }
    return pe_append_raw_absolute(pe, stream, XX_PE_DATA_STRUCT_RESERVED_DIRECTORY_RAW, fixup_offset + (int64_t)(fixup_count * 8U), fixup_size - fixup_count * 8U);
}

static const char *dotnet_data_id_to_string(Abstractformat *format, uint32_t id)
{
    switch (id) {
        case XX_DOTNET_DATA_STRUCT_COR20_HEADER: return "IMAGE_COR20_HEADER";
        case XX_DOTNET_DATA_STRUCT_CLR_METADATA_ROOT: return "STORAGESIGNATURE";
        case XX_DOTNET_DATA_STRUCT_CLR_METADATA_STORAGE_HEADER: return "STORAGEHEADER";
        case XX_DOTNET_DATA_STRUCT_CLR_STREAM_HEADER: return "STORAGESTREAM";
        case XX_DOTNET_DATA_STRUCT_CLR_VTABLE_FIXUP: return "IMAGE_COR_VTABLEFIXUP";
        case XX_DOTNET_DATA_STRUCT_CLR_RAW: return "CLR_RAW";
        default: return xx_pe_data_struct_id_to_string(format, id);
    }
}
static uint32_t dotnet_data_string_to_id(Abstractformat *format, const char *name)
{
    if (!name) return XX_DOTNET_DATA_STRUCT_UNKNOWN;
    for (uint32_t id = XX_DOTNET_DATA_STRUCT_COR20_HEADER; id <= XX_DOTNET_DATA_STRUCT_CLR_RAW; ++id)
        if (xx_rt_strcmp(name, dotnet_data_id_to_string(format, id)) == 0) return id;
    return xx_pe_data_struct_string_to_id(format, name);
}
static xx_data_struct_state *dotnet_create_data_structs(Abstractformat *format, xx_pd_struct *pd)
{
    return xx_pe_create_data_structs_reading_extended(format, pe_append_clr, pd);
}
static xx_data_struct_record_state *dotnet_create_records(Abstractformat *format, const xx_data_struct *ds, xx_pd_struct *pd)
{
    const xx_data_struct_field_desc *fields = NULL;
    size_t count = 0U;
    xx_data_struct_field_desc stream_fields[3];
    if (!ds) return NULL;
    switch (ds->id) {
        case XX_DOTNET_DATA_STRUCT_COR20_HEADER:
            fields = pe_cor20_fields;
            count = PE_COUNT(pe_cor20_fields);
            break;
        case XX_DOTNET_DATA_STRUCT_CLR_METADATA_ROOT:
            fields = pe_clr_root_fields;
            count = PE_COUNT(pe_clr_root_fields);
            break;
        case XX_DOTNET_DATA_STRUCT_CLR_METADATA_STORAGE_HEADER:
            fields = pe_clr_storage_fields;
            count = PE_COUNT(pe_clr_storage_fields);
            break;
        case XX_DOTNET_DATA_STRUCT_CLR_STREAM_HEADER:
            stream_fields[0] = pe_clr_stream_fields[0];
            stream_fields[1] = pe_clr_stream_fields[1];
            stream_fields[2].name = L"StreamName";
            stream_fields[2].type = L"char[]";
            stream_fields[2].rel_offset = 8;
            stream_fields[2].size = ds->total_size > 8 ? ds->total_size - 8 : 0;
            stream_fields[2].property = XX_DATA_STRUCT_RECORD_PROPERTY_STRING;
            fields = stream_fields;
            count = PE_COUNT(stream_fields);
            break;
        case XX_DOTNET_DATA_STRUCT_CLR_VTABLE_FIXUP:
            fields = pe_clr_vtable_fields;
            count = PE_COUNT(pe_clr_vtable_fields);
            break;
        case XX_DOTNET_DATA_STRUCT_CLR_RAW: return NULL;
        default: return xx_pe_create_data_struct_records_reading(format, ds, pd);
    }
    return xx_pe_data_create_records_with_fields(format, ds, fields, count, pd);
}
void xx_dotnet_setup_data_struct_callbacks(xx_dotnet *dotnet)
{
    Abstractformat *format;
    if (!dotnet) return;
    format = &dotnet->pe.format;
    format->data_struct_id_to_string = dotnet_data_id_to_string;
    format->data_struct_string_to_id = dotnet_data_string_to_id;
    format->create_data_structs_reading = dotnet_create_data_structs;
    format->create_data_struct_records_reading = dotnet_create_records;
}
