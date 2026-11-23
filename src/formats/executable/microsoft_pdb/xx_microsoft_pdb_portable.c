/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Microsoft Portable PDB reader: ECMA-335 ("BSJB") metadata root,
 * stream directory, #Pdb stream, #~ / #- metadata tables, and
 * Document table reader.
 *
 * C port of _mylibs/XPDB (xpdb.cpp / xpdb.h); field and method names
 * follow it so the two can be compared line by line.
 */
#include "xxfclib/formats/executable/microsoft_pdb/xx_microsoft_pdb_portable.h"

#include <stdio.h>
#include <string.h>

#include "xxfclib/data/xx_data.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"

/* ---- String and Device Helpers ------------------------------------------ */

static char *dup_string(const char *src, size_t len)
{
    if (!src) {
        return NULL;
    }
    char *dst = (char *)xx_mem_alloc(len + 1);
    if (!dst) {
        return NULL;
    }
    xx_mem_copy(dst, src, len);
    dst[len] = '\0';
    return dst;
}

static char *dup_str(const char *src)
{
    if (!src) {
        return NULL;
    }
    return dup_string(src, strlen(src));
}

static bool pdb_dev_read(xx_io_device *device, int64_t file_size, int64_t offset, void *buffer, size_t size)
{
    if (!device || !buffer || offset < 0 || (int64_t)size < 0) {
        return false;
    }
    if (size == 0) {
        return true;
    }
    if (file_size >= 0) {
        if (offset > file_size || (int64_t)size > file_size - offset) {
            return false;
        }
    }
    if (xx_io_seek64(device, offset, SEEK_SET) != 0) {
        return false;
    }

    size_t total_read = 0;
    while (total_read < size) {
        ssize_t n = xx_io_read(device, (uint8_t *)buffer + total_read, size - total_read);
        if (n <= 0) {
            return false;
        }
        total_read += (size_t)n;
    }
    return true;
}

/* Format 16 bytes of GUID data as "{XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX}" uppercase, matching XPDB::_guidToString */
static void format_guid(char *out, const uint8_t *data)
{
    uint32_t d1 = xx_data_get_le32(data);
    uint16_t d2 = xx_data_get_le16(data + 4);
    uint16_t d3 = xx_data_get_le16(data + 6);

    snprintf(out, 40, "{%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
             (unsigned int)d1,
             (unsigned int)d2,
             (unsigned int)d3,
             (unsigned int)data[8],
             (unsigned int)data[9],
             (unsigned int)data[10],
             (unsigned int)data[11],
             (unsigned int)data[12],
             (unsigned int)data[13],
             (unsigned int)data[14],
             (unsigned int)data[15]);
}

/* Dynamic string builder for assembling document paths */
typedef struct str_builder {
    char *data;
    size_t length;
    size_t capacity;
    bool oom;
} str_builder;

static void sb_init(str_builder *sb)
{
    sb->data = NULL;
    sb->length = 0;
    sb->capacity = 0;
    sb->oom = false;
}

static bool sb_append_bytes(str_builder *sb, const char *bytes, size_t len)
{
    if (sb->oom || len == 0) {
        return !sb->oom;
    }

    if (sb->length + len + 1 > sb->capacity) {
        size_t new_cap = (sb->capacity == 0) ? 64 : sb->capacity * 2;
        while (new_cap < sb->length + len + 1) {
            new_cap *= 2;
        }
        char *new_data = (char *)xx_mem_realloc(sb->data, new_cap);
        if (!new_data) {
            sb->oom = true;
            return false;
        }
        sb->data = new_data;
        sb->capacity = new_cap;
    }

    xx_mem_copy(sb->data + sb->length, bytes, len);
    sb->length += len;
    sb->data[sb->length] = '\0';
    return true;
}

static bool sb_append_char(str_builder *sb, char c)
{
    return sb_append_bytes(sb, &c, 1);
}

static char *sb_detach(str_builder *sb)
{
    if (sb->oom || !sb->data) {
        if (sb->data) {
            xx_mem_free(sb->data);
            sb->data = NULL;
        }
        return NULL;
    }
    char *res = sb->data;
    sb->data = NULL;
    sb->length = 0;
    sb->capacity = 0;
    return res;
}

static void sb_free(str_builder *sb)
{
    if (sb->data) {
        xx_mem_free(sb->data);
        sb->data = NULL;
    }
    sb->length = 0;
    sb->capacity = 0;
    sb->oom = false;
}

/* ---- Public Utilities --------------------------------------------------- */

int32_t xx_ms_pdb_portable_count_bits(uint64_t value)
{
    int32_t result = 0;
    for (int32_t i = 0; i < 64; i++) {
        if (value & ((uint64_t)1 << i)) {
            result++;
        }
    }
    return result;
}

int32_t xx_ms_pdb_portable_read_compressed_uint(const void *data, int32_t data_size, int32_t offset, uint32_t *out_value)
{
    const uint8_t *p = (const uint8_t *)data;

    if (!out_value) {
        return 0;
    }
    *out_value = 0;

    if (!data || offset < 0 || offset >= data_size) {
        return 0;
    }

    uint8_t byte0 = p[offset];

    if ((byte0 & 0x80) == 0) {
        *out_value = (uint32_t)byte0;
        return 1;
    } else if ((byte0 & 0xC0) == 0x80) {
        if (offset + 1 >= data_size) {
            return 0;
        }
        *out_value = ((uint32_t)(byte0 & 0x3F) << 8) | (uint32_t)p[offset + 1];
        return 2;
    } else if ((byte0 & 0xE0) == 0xC0) {
        if (offset + 3 >= data_size) {
            return 0;
        }
        *out_value = ((uint32_t)(byte0 & 0x1F) << 24) |
                     ((uint32_t)p[offset + 1] << 16) |
                     ((uint32_t)p[offset + 2] << 8) |
                     (uint32_t)p[offset + 3];
        return 4;
    }

    return 0;
}

const char *xx_ms_pdb_portable_table_name(uint32_t table)
{
    static const char * const s_names[64] = {
        /* 0x00 */ "Module",
        /* 0x01 */ "TypeRef",
        /* 0x02 */ "TypeDef",
        /* 0x03 */ "0x03",
        /* 0x04 */ "Field",
        /* 0x05 */ "0x05",
        /* 0x06 */ "MethodDef",
        /* 0x07 */ "0x07",
        /* 0x08 */ "Param",
        /* 0x09 */ "0x09",
        /* 0x0a */ "0x0a",
        /* 0x0b */ "0x0b",
        /* 0x0c */ "0x0c",
        /* 0x0d */ "0x0d",
        /* 0x0e */ "0x0e",
        /* 0x0f */ "0x0f",
        /* 0x10 */ "0x10",
        /* 0x11 */ "0x11",
        /* 0x12 */ "0x12",
        /* 0x13 */ "0x13",
        /* 0x14 */ "0x14",
        /* 0x15 */ "0x15",
        /* 0x16 */ "0x16",
        /* 0x17 */ "0x17",
        /* 0x18 */ "0x18",
        /* 0x19 */ "0x19",
        /* 0x1a */ "0x1a",
        /* 0x1b */ "0x1b",
        /* 0x1c */ "0x1c",
        /* 0x1d */ "0x1d",
        /* 0x1e */ "0x1e",
        /* 0x1f */ "0x1f",
        /* 0x20 */ "0x20",
        /* 0x21 */ "0x21",
        /* 0x22 */ "0x22",
        /* 0x23 */ "0x23",
        /* 0x24 */ "0x24",
        /* 0x25 */ "0x25",
        /* 0x26 */ "0x26",
        /* 0x27 */ "0x27",
        /* 0x28 */ "0x28",
        /* 0x29 */ "0x29",
        /* 0x2a */ "0x2a",
        /* 0x2b */ "0x2b",
        /* 0x2c */ "0x2c",
        /* 0x2d */ "0x2d",
        /* 0x2e */ "0x2e",
        /* 0x2f */ "0x2f",
        /* 0x30 */ "Document",
        /* 0x31 */ "MethodDebugInformation",
        /* 0x32 */ "LocalScope",
        /* 0x33 */ "LocalVariable",
        /* 0x34 */ "LocalConstant",
        /* 0x35 */ "ImportScope",
        /* 0x36 */ "StateMachineMethod",
        /* 0x37 */ "CustomDebugInformation",
        /* 0x38 */ "0x38",
        /* 0x39 */ "0x39",
        /* 0x3a */ "0x3a",
        /* 0x3b */ "0x3b",
        /* 0x3c */ "0x3c",
        /* 0x3d */ "0x3d",
        /* 0x3e */ "0x3e",
        /* 0x3f */ "0x3f"
    };

    if (table < 64) {
        return s_names[table];
    }

    static char s_ring[8][16];
    static unsigned int s_ring_idx = 0;
    char *buf = s_ring[(s_ring_idx++) & 7];
    snprintf(buf, sizeof(s_ring[0]), "0x%02x", (unsigned int)table);
    return buf;
}

const xx_ms_pdb_portable_stream *xx_ms_pdb_portable_get_stream(const xx_ms_pdb_portable_details *d, const char *name)
{
    if (!d || !d->streams || !name) {
        return NULL;
    }
    for (size_t i = 0; i < d->stream_count; i++) {
        if (d->streams[i].name && strcmp(d->streams[i].name, name) == 0) {
            return &d->streams[i];
        }
    }
    return NULL;
}

/* ---- Internal Blob Readers ---------------------------------------------- */

/* Read a blob from the #Blob heap, matching XPDB::_getMDBlob */
static bool get_md_blob(xx_io_device *device, int64_t file_size,
                        uint32_t blob_offset, uint32_t blob_size,
                        uint32_t index,
                        uint8_t **out_data, uint32_t *out_size)
{
    if (!out_data || !out_size) {
        return false;
    }
    *out_data = NULL;
    *out_size = 0;

    if (blob_size == 0 || index >= blob_size) {
        return false;
    }

    uint32_t rem = blob_size - index;
    uint32_t hdr_to_read = rem < 4 ? rem : 4;
    uint8_t hdr[4];

    if (!pdb_dev_read(device, file_size, (int64_t)blob_offset + (int64_t)index, hdr, (size_t)hdr_to_read)) {
        return false;
    }

    uint32_t blob_len = 0;
    int32_t len_size = xx_ms_pdb_portable_read_compressed_uint(hdr, (int32_t)hdr_to_read, 0, &blob_len);
    if (len_size <= 0) {
        return false;
    }

    if (blob_len == 0) {
        uint8_t *buf = (uint8_t *)xx_mem_alloc(1);
        if (!buf) {
            return false;
        }
        buf[0] = '\0';
        *out_data = buf;
        *out_size = 0;
        return true;
    }

    if ((uint64_t)index + (uint64_t)len_size + (uint64_t)blob_len > (uint64_t)blob_size) {
        return false;
    }

    int64_t data_offset = (int64_t)blob_offset + (int64_t)index + (int64_t)len_size;
    if (file_size >= 0 && data_offset + (int64_t)blob_len > file_size) {
        return false;
    }

    uint8_t *buf = (uint8_t *)xx_mem_alloc((size_t)blob_len + 1);
    if (!buf) {
        return false;
    }

    if (!pdb_dev_read(device, file_size, data_offset, buf, (size_t)blob_len)) {
        xx_mem_free(buf);
        return false;
    }

    buf[blob_len] = '\0';
    *out_data = buf;
    *out_size = blob_len;
    return true;
}

/* Decode Document table Name blob, matching XPDB::_getMDDocumentName */
static char *get_md_document_name(xx_io_device *device, int64_t file_size,
                                  uint32_t blob_offset, uint32_t blob_size,
                                  uint32_t name_index)
{
    uint8_t *name_blob = NULL;
    uint32_t name_blob_size = 0;

    if (!get_md_blob(device, file_size, blob_offset, blob_size, name_index, &name_blob, &name_blob_size)) {
        return NULL;
    }

    if (name_blob_size < 1) {
        xx_mem_free(name_blob);
        return dup_string("", 0);
    }

    uint8_t separator = name_blob[0];

    str_builder sb;
    sb_init(&sb);

    int32_t offset = 1;
    bool first = true;

    while (offset < (int32_t)name_blob_size) {
        uint32_t part_index = 0;
        int32_t part_size = xx_ms_pdb_portable_read_compressed_uint(name_blob, (int32_t)name_blob_size, offset, &part_index);
        if (part_size <= 0) {
            break;
        }

        offset += part_size;

        if (!first) {
            if (separator != 0) {
                if (!sb_append_char(&sb, (char)separator)) {
                    break;
                }
            }
        }
        first = false;

        if (part_index != 0) {
            uint8_t *part_data = NULL;
            uint32_t part_len = 0;
            if (get_md_blob(device, file_size, blob_offset, blob_size, part_index, &part_data, &part_len)) {
                if (part_len > 0 && part_data) {
                    if (!sb_append_bytes(&sb, (const char *)part_data, (size_t)part_len)) {
                        xx_mem_free(part_data);
                        break;
                    }
                }
                if (part_data) {
                    xx_mem_free(part_data);
                }
            }
        }
    }

    xx_mem_free(name_blob);

    if (sb.oom) {
        sb_free(&sb);
        return NULL;
    }

    if (!sb.data) {
        return dup_string("", 0);
    }

    return sb_detach(&sb);
}

/* ---- Public API Implementation ------------------------------------------ */

bool xx_ms_pdb_portable_read_info(xx_io_device *device, xx_ms_pdb_portable_details *out)
{
    if (!device || !out) {
        return false;
    }

    xx_mem_zero(out, sizeof(*out));

    int64_t total_size = xx_io_total_size(device);
    if (total_size >= 0 && total_size < 32) {
        return false;
    }

    /* 1. Signature "BSJB" at offset 0 */
    uint8_t sig_buf[4];
    if (!pdb_dev_read(device, total_size, 0, sig_buf, 4)) {
        return false;
    }
    if (xx_data_get_le32(sig_buf) != XX_MS_PDB_PORTABLE_SIGNATURE) {
        return false;
    }

    /* 2. Major and Minor version at offset 4 and 6 */
    uint8_t ver_buf[4];
    if (!pdb_dev_read(device, total_size, 4, ver_buf, 4)) {
        return false;
    }
    out->major_version = xx_data_get_le16(ver_buf);
    out->minor_version = xx_data_get_le16(ver_buf + 2);

    /* 3. Version string length at offset 12 */
    uint8_t len_buf[4];
    if (!pdb_dev_read(device, total_size, 12, len_buf, 4)) {
        return false;
    }
    uint32_t version_length = xx_data_get_le32(len_buf);
    if (version_length > 256) {
        return false;
    }
    if (total_size >= 0 && (int64_t)XX_MS_PDB_PORTABLE_VERSION_OFFSET + (int64_t)version_length + 4 > total_size) {
        return false;
    }

    /* 4. Version string at offset 16 */
    char ver_str_buf[264];
    if (version_length > 0) {
        if (!pdb_dev_read(device, total_size, XX_MS_PDB_PORTABLE_VERSION_OFFSET, ver_str_buf, version_length)) {
            return false;
        }
    }
    ver_str_buf[version_length] = '\0';

    size_t actual_ver_len = 0;
    while (actual_ver_len < (size_t)version_length && ver_str_buf[actual_ver_len] != '\0') {
        actual_ver_len++;
    }
    out->version = dup_string(ver_str_buf, actual_ver_len);
    if (!out->version) {
        return false;
    }

    /* 5. Flags and NumberOfStreams directly following version string */
    int64_t offset = (int64_t)XX_MS_PDB_PORTABLE_VERSION_OFFSET + (int64_t)version_length;
    uint8_t flags_streams[4];
    if (!pdb_dev_read(device, total_size, offset, flags_streams, 4)) {
        xx_ms_pdb_portable_details_free(out);
        return false;
    }
    out->flags = xx_data_get_le16(flags_streams);
    out->number_of_streams = xx_data_get_le16(flags_streams + 2);
    offset += 4;

    out->is_valid = true;

    /* 6. Stream headers: offset, size, and name padded to 4-byte boundary */
    size_t cap = out->number_of_streams > 0 ? (size_t)out->number_of_streams : 8;
    out->streams = (xx_ms_pdb_portable_stream *)xx_mem_alloc(cap * sizeof(xx_ms_pdb_portable_stream));
    if (!out->streams) {
        xx_ms_pdb_portable_details_free(out);
        return false;
    }

    for (uint32_t i = 0; i < out->number_of_streams; i++) {
        if (total_size >= 0 && (offset + 8) > total_size) {
            break;
        }

        uint8_t hdr[8];
        if (!pdb_dev_read(device, total_size, offset, hdr, 8)) {
            break;
        }

        uint32_t stream_offset = xx_data_get_le32(hdr);
        uint32_t stream_size = xx_data_get_le32(hdr + 4);
        int64_t header_offset = offset;

        size_t max_name = 64;
        if (total_size >= 0 && (offset + 8 + (int64_t)max_name) > total_size) {
            max_name = (size_t)(total_size - (offset + 8));
        }

        char name_buf[68];
        if (!pdb_dev_read(device, total_size, offset + 8, name_buf, max_name)) {
            break;
        }
        name_buf[max_name] = '\0';

        size_t name_len = 0;
        while (name_len < max_name && name_buf[name_len] != '\0') {
            name_len++;
        }

        int64_t padded = (int64_t)(((name_len + 1) + 3) & ~3);
        offset += 8 + padded;

        if (total_size < 0 || (int64_t)stream_offset + (int64_t)stream_size <= total_size) {
            if (out->stream_count >= cap) {
                size_t new_cap = cap * 2;
                xx_ms_pdb_portable_stream *new_streams = (xx_ms_pdb_portable_stream *)xx_mem_realloc(out->streams, new_cap * sizeof(xx_ms_pdb_portable_stream));
                if (!new_streams) {
                    break;
                }
                out->streams = new_streams;
                cap = new_cap;
            }

            out->streams[out->stream_count].header_offset = header_offset;
            out->streams[out->stream_count].offset = stream_offset;
            out->streams[out->stream_count].size = stream_size;
            out->streams[out->stream_count].name = dup_string(name_buf, name_len);
            out->stream_count++;
        }
    }

    /* 7. #Pdb stream: GUID, timestamp, entry point, referenced type-system tables */
    const xx_ms_pdb_portable_stream *pdb_stream = xx_ms_pdb_portable_get_stream(out, XX_MS_PDB_PORTABLE_STREAM_PDB);
    if (pdb_stream && pdb_stream->size >= 32) {
        size_t pdb_read_size = pdb_stream->size < 0x10000 ? (size_t)pdb_stream->size : 0x10000;
        uint8_t *pData = (uint8_t *)xx_mem_alloc(pdb_read_size);
        if (pData) {
            if (pdb_dev_read(device, total_size, (int64_t)pdb_stream->offset, pData, pdb_read_size)) {
                char guid_str[40];
                format_guid(guid_str, pData);
                out->pdb_id = dup_str(guid_str);
                xx_mem_copy(out->pdb_id_raw, pData, 20);
                out->pdb_stamp = xx_data_get_le32(pData + 16);
                out->stamp = out->pdb_stamp;
                out->entry_point = xx_data_get_le32(pData + 20);
                out->referenced_type_system_tables = xx_data_get_le64(pData + 24);

                size_t row_offset = 32;
                for (uint32_t nTable = 0; nTable < XX_MS_PDB_TABLE_MAX; nTable++) {
                    if (out->referenced_type_system_tables & ((uint64_t)1 << nTable)) {
                        if (row_offset + 4 > pdb_read_size) {
                            break;
                        }
                        uint32_t rows = xx_data_get_le32(pData + row_offset);
                        out->type_system_table_rows[nTable] = rows;
                        out->table_rows[nTable] = rows;
                        row_offset += 4;
                    }
                }
                out->pdb_stream_valid = true;
            }
            xx_mem_free(pData);
        }
    }

    /* 8. #~ or #- metadata tables stream header */
    const xx_ms_pdb_portable_stream *table_stream = xx_ms_pdb_portable_get_stream(out, XX_MS_PDB_PORTABLE_STREAM_TABLES);
    if (!table_stream) {
        table_stream = xx_ms_pdb_portable_get_stream(out, XX_MS_PDB_PORTABLE_STREAM_TABLES_UNCOMPRESSED);
    }
    if (table_stream && table_stream->size >= 24) {
        size_t tbl_read_size = table_stream->size < 0x10000 ? (size_t)table_stream->size : 0x10000;
        uint8_t *pData = (uint8_t *)xx_mem_alloc(tbl_read_size);
        if (pData) {
            if (pdb_dev_read(device, total_size, (int64_t)table_stream->offset, pData, tbl_read_size)) {
                out->tables_major_version = pData[4];
                out->tables_minor_version = pData[5];
                out->heap_sizes = pData[6];
                out->valid_tables = xx_data_get_le64(pData + 8);
                out->valid_mask = out->valid_tables;
                out->sorted_tables = xx_data_get_le64(pData + 16);
                out->sorted_mask = out->sorted_tables;

                size_t row_offset = 24;
                for (uint32_t nTable = 0; nTable < XX_MS_PDB_TABLE_MAX; nTable++) {
                    if (out->valid_tables & ((uint64_t)1 << nTable)) {
                        if (row_offset + 4 > tbl_read_size) {
                            break;
                        }
                        uint32_t rows = xx_data_get_le32(pData + row_offset);
                        out->table_rows[nTable] = rows;
                        row_offset += 4;
                    }
                }
                out->tables_valid = true;
            }
            xx_mem_free(pData);
        }
    }

    return true;
}

void xx_ms_pdb_portable_details_free(xx_ms_pdb_portable_details *d)
{
    if (!d) {
        return;
    }

    if (d->version) {
        xx_mem_free(d->version);
        d->version = NULL;
    }
    if (d->pdb_id) {
        xx_mem_free(d->pdb_id);
        d->pdb_id = NULL;
    }
    if (d->streams) {
        for (size_t i = 0; i < d->stream_count; i++) {
            if (d->streams[i].name) {
                xx_mem_free(d->streams[i].name);
                d->streams[i].name = NULL;
            }
        }
        xx_mem_free(d->streams);
        d->streams = NULL;
    }

    xx_mem_zero(d, sizeof(*d));
}

bool xx_ms_pdb_portable_get_documents(xx_io_device *device, int32_t limit, char ***names, size_t *count)
{
    if (!device || !names || !count) {
        return false;
    }

    *names = NULL;
    *count = 0;

    if (limit == 0) {
        return true;
    }

    xx_ms_pdb_portable_details info;
    if (!xx_ms_pdb_portable_read_info(device, &info)) {
        return true; /* Not a portable PDB: return true with empty count */
    }

    if (!info.tables_valid) {
        xx_ms_pdb_portable_details_free(&info);
        return true;
    }

    if ((info.valid_tables & ((uint64_t)1 << XX_MS_PDB_TABLE_DOCUMENT)) == 0) {
        xx_ms_pdb_portable_details_free(&info);
        return true;
    }

    /* Standalone PDB constraint: tables in front of Document cannot be measured without full schema */
    for (uint32_t t = 0; t < XX_MS_PDB_TABLE_DOCUMENT; t++) {
        if (info.valid_tables & ((uint64_t)1 << t)) {
            xx_ms_pdb_portable_details_free(&info);
            return true;
        }
    }

    const xx_ms_pdb_portable_stream *table_stream = xx_ms_pdb_portable_get_stream(&info, XX_MS_PDB_PORTABLE_STREAM_TABLES);
    if (!table_stream || table_stream->size == 0) {
        table_stream = xx_ms_pdb_portable_get_stream(&info, XX_MS_PDB_PORTABLE_STREAM_TABLES_UNCOMPRESSED);
    }
    const xx_ms_pdb_portable_stream *blob_stream = xx_ms_pdb_portable_get_stream(&info, XX_MS_PDB_PORTABLE_STREAM_BLOB);

    if (!table_stream || table_stream->size == 0 || !blob_stream || blob_stream->size == 0) {
        xx_ms_pdb_portable_details_free(&info);
        return true;
    }

    int32_t blob_idx_size = (info.heap_sizes & XX_MS_PDB_PORTABLE_HEAPSIZE_BLOB) ? 4 : 2;
    int32_t guid_idx_size = (info.heap_sizes & XX_MS_PDB_PORTABLE_HEAPSIZE_GUID) ? 4 : 2;
    int32_t row_size = blob_idx_size + guid_idx_size + blob_idx_size + guid_idx_size;

    uint32_t num_rows = info.table_rows[XX_MS_PDB_TABLE_DOCUMENT];
    if (num_rows == 0) {
        xx_ms_pdb_portable_details_free(&info);
        return true;
    }

    int64_t table_offset = (int64_t)table_stream->offset + 24 + (int64_t)xx_ms_pdb_portable_count_bits(info.valid_tables) * 4;
    int64_t total_size = xx_io_total_size(device);

    size_t result_capacity = (limit > 0 && (size_t)limit < (size_t)num_rows) ? (size_t)limit : (size_t)num_rows;
    if (result_capacity == 0) {
        result_capacity = 1;
    }

    char **result_list = (char **)xx_mem_alloc(result_capacity * sizeof(char *));
    if (!result_list) {
        xx_ms_pdb_portable_details_free(&info);
        return false;
    }
    size_t result_count = 0;

    for (uint32_t i = 0; i < num_rows; i++) {
        if (limit >= 0 && (int32_t)result_count >= limit) {
            break;
        }

        int64_t row_file_offset = table_offset + (int64_t)i * (int64_t)row_size;

        if (row_file_offset + (int64_t)row_size > (int64_t)table_stream->offset + (int64_t)table_stream->size) {
            break;
        }
        if (total_size >= 0 && row_file_offset + (int64_t)row_size > total_size) {
            break;
        }

        uint8_t name_idx_buf[4];
        if (!pdb_dev_read(device, total_size, row_file_offset, name_idx_buf, (size_t)blob_idx_size)) {
            break;
        }

        uint32_t name_index = 0;
        if (blob_idx_size == 2) {
            name_index = xx_data_get_le16(name_idx_buf);
        } else {
            name_index = xx_data_get_le32(name_idx_buf);
        }

        char *doc_name = get_md_document_name(device, total_size, blob_stream->offset, blob_stream->size, name_index);
        if (doc_name) {
            if (doc_name[0] != '\0') {
                if (result_count >= result_capacity) {
                    size_t new_cap = result_capacity * 2;
                    char **new_list = (char **)xx_mem_realloc(result_list, new_cap * sizeof(char *));
                    if (!new_list) {
                        xx_mem_free(doc_name);
                        xx_ms_pdb_portable_documents_free(result_list, result_count);
                        xx_ms_pdb_portable_details_free(&info);
                        return false;
                    }
                    result_list = new_list;
                    result_capacity = new_cap;
                }
                result_list[result_count++] = doc_name;
            } else {
                xx_mem_free(doc_name);
            }
        }
    }

    xx_ms_pdb_portable_details_free(&info);

    if (result_count == 0) {
        xx_mem_free(result_list);
        *names = NULL;
        *count = 0;
    } else {
        *names = result_list;
        *count = result_count;
    }

    return true;
}

void xx_ms_pdb_portable_documents_free(char **names, size_t count)
{
    if (!names) {
        return;
    }

    for (size_t i = 0; i < count; i++) {
        if (names[i]) {
            xx_mem_free(names[i]);
        }
    }
    xx_mem_free(names);
}
