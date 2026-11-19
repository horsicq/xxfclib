/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * What the readers contribute to the DataStructs scripts (RULES.md, "What
 * stays in C"): which script serves a reader the file type does not
 * identify, the decoded inner format of a wrapper type, and fact tables for
 * boundaries only a decoder can find. No structure descriptions live here.
 */
#include "xx_xxds_internal.h"

#include "xxfclib/rt/xx_rt.h"

#ifndef XXFC_STREAM_HAS_GZ
#define XXFC_STREAM_HAS_GZ 1
#endif
#ifndef XXFC_STREAM_HAS_PE
#define XXFC_STREAM_HAS_PE 1
#endif
#ifndef XXFC_STREAM_HAS_ELF
#define XXFC_STREAM_HAS_ELF 1
#endif
#ifndef XXFC_STREAM_HAS_MACHO
#define XXFC_STREAM_HAS_MACHO 1
#endif
#ifndef XXFC_STREAM_HAS_DOTNET
#define XXFC_STREAM_HAS_DOTNET 1
#endif
#ifndef XXFC_STREAM_HAS_RAR
#define XXFC_STREAM_HAS_RAR 1
#endif
#ifndef XXFC_STREAM_HAS_ZIP
#define XXFC_STREAM_HAS_ZIP 1
#endif
#ifndef XXFC_STREAM_HAS_XORARCHIVE
#define XXFC_STREAM_HAS_XORARCHIVE 1
#endif
#ifndef XXFC_STREAM_HAS_XZ
#define XXFC_STREAM_HAS_XZ 1
#endif
#ifndef XXFC_STREAM_HAS_7ZIP
#define XXFC_STREAM_HAS_7ZIP 1
#endif
#ifndef XXFC_STREAM_HAS_TAR
#define XXFC_STREAM_HAS_TAR 1
#endif
#ifndef XXFC_STREAM_HAS_TAR_GZ
#define XXFC_STREAM_HAS_TAR_GZ 1
#endif
#ifndef XXFC_STREAM_HAS_TAR_BZ2
#define XXFC_STREAM_HAS_TAR_BZ2 1
#endif
#ifndef XXFC_STREAM_HAS_TAR_XZ
#define XXFC_STREAM_HAS_TAR_XZ 1
#endif
#ifndef XXFC_STREAM_HAS_TAR_LZ4
#define XXFC_STREAM_HAS_TAR_LZ4 1
#endif
#ifndef XXFC_STREAM_HAS_TAR_ZSTD
#define XXFC_STREAM_HAS_TAR_ZSTD 1
#endif
#define XXDS_HAS_TAR_WRAPPER (XXFC_STREAM_HAS_TAR_GZ || XXFC_STREAM_HAS_TAR_BZ2 || XXFC_STREAM_HAS_TAR_XZ || XXFC_STREAM_HAS_TAR_LZ4 || XXFC_STREAM_HAS_TAR_ZSTD)

#if XXFC_STREAM_HAS_PE
extern bool xx_pe_handle_base_info(Abstractformat *format, xx_pd_struct *pd);
#endif
#if XXFC_STREAM_HAS_ELF
extern bool xx_elf_handle_base_info(Abstractformat *format, xx_pd_struct *pd);
#endif
#if XXFC_STREAM_HAS_MACHO
extern bool xx_macho_handle_base_info(Abstractformat *format, xx_pd_struct *pd);
#endif
#if XXFC_STREAM_HAS_DOTNET
extern bool xx_dotnet_handle_base_info(Abstractformat *format, xx_pd_struct *pd);
#endif

#if XXFC_STREAM_HAS_GZ
#include "../formats/archive/gz/xx_gz_private.h"
#endif
#if XXFC_STREAM_HAS_RAR
#include "xxfclib/formats/archive/rar/xx_rar.h"
#endif
#if XXFC_STREAM_HAS_ZIP
#include "../formats/archive/zip/xx_zip_private.h"
#endif
#if XXFC_STREAM_HAS_ZIP && XXFC_STREAM_HAS_XORARCHIVE
#include "xxfclib/formats/archive/xorarchive/xx_xorarchive.h"
#endif
#if XXFC_STREAM_HAS_XZ
#include "../formats/archive/xz/xx_xz_private.h"
#endif
#if XXFC_STREAM_HAS_7ZIP
#include "../formats/archive/7zip/xx_7zip_private.h"
#endif
#if XXFC_STREAM_HAS_TAR
#include "../formats/archive/tar/xx_tar_private.h"
#endif
#if XXDS_HAS_TAR_WRAPPER
#include "../formats/archive/tar_common/xx_tar_common.h"
#endif
#if XXFC_STREAM_HAS_TAR_GZ
#include "xxfclib/formats/archive/tar_gz/xx_tar_gz.h"
extern xx_tar_common *xx_tar_gz_common(xx_tar_gz *tar_gz);
extern bool xx_tar_gz_decode(Abstractformat *outer, xx_io_device *destination, int64_t *compressed_size, xx_pd_struct *pd);
#endif
#if XXFC_STREAM_HAS_TAR_BZ2
#include "xxfclib/formats/archive/tar_bz2/xx_tar_bz2.h"
extern xx_tar_common *xx_tar_bz2_common(Abstractformat *self);
extern bool xx_tar_bz2_decode(Abstractformat *outer, xx_io_device *destination, int64_t *compressed_size, xx_pd_struct *pd);
#endif
#if XXFC_STREAM_HAS_TAR_XZ
#include "xxfclib/formats/archive/tar_xz/xx_tar_xz.h"
extern xx_tar_common *xx_tar_xz_common(xx_tar_xz *tar_xz, bool create);
extern bool xx_tar_xz_decode(Abstractformat *outer, xx_io_device *destination, int64_t *compressed_size, xx_pd_struct *pd);
#endif
#if XXFC_STREAM_HAS_TAR_LZ4
#include "xxfclib/formats/archive/tar_lz4/xx_tar_lz4.h"
extern xx_tar_common *xx_tar_lz4_common(xx_tar_lz4 *tar_lz4, bool create);
extern bool xx_tar_lz4_decode(Abstractformat *outer, xx_io_device *destination, int64_t *compressed_size, xx_pd_struct *pd);
#endif
#if XXFC_STREAM_HAS_TAR_ZSTD
#include "xxfclib/formats/archive/tar_zstd/xx_tar_zstd.h"
extern xx_tar_common *xx_tar_zstd_common(xx_tar_zstd *tar_zstd, bool create);
extern bool xx_tar_zstd_decode(Abstractformat *outer, xx_io_device *destination, int64_t *compressed_size, xx_pd_struct *pd);
#endif

#if XXFC_STREAM_HAS_ZIP
/* An xx_zip reader: the ZIP family types, and an XORARCHIVE that unmasks to
 * ZIP (its container is an xx_zip over the unmasking device). */
static bool xxds_is_zip(const Abstractformat *format)
{
    switch (format->file_type) {
        case XX_FILE_TYPE_ZIP:
        case XX_FILE_TYPE_ZIP64:
        case XX_FILE_TYPE_JAR:
        case XX_FILE_TYPE_APK:
        case XX_FILE_TYPE_IPA:
        case XX_FILE_TYPE_APKS:
        case XX_FILE_TYPE_ZIP_PSC:
        case XX_FILE_TYPE_CRX: return true;
#if XXFC_STREAM_HAS_XORARCHIVE
        case XX_FILE_TYPE_XORARCHIVE:
            return format->handle_base_info == xx_xorarchive_handle_base_info && ((const xx_xorarchive *)format)->family == XX_XORARCHIVE_FAMILY_ZIP;
#endif
        default: return false;
    }
}
#endif

const char *xx_xxds_reader_script(Abstractformat *format)
{
    if (!format) return NULL;
#if XXFC_STREAM_HAS_ZIP && XXFC_STREAM_HAS_XORARCHIVE
    if (format->file_type == XX_FILE_TYPE_XORARCHIVE) return xxds_is_zip(format) ? "zip" : NULL;
#endif
#if XXFC_STREAM_HAS_DOTNET
    if (format->handle_base_info == xx_dotnet_handle_base_info) return "dotnet";
#endif
#if XXFC_STREAM_HAS_PE
    if (format->handle_base_info == xx_pe_handle_base_info) return "pe";
#endif
#if XXFC_STREAM_HAS_ELF
    if (format->handle_base_info == xx_elf_handle_base_info) return "elf";
#endif
#if XXFC_STREAM_HAS_MACHO
    if (format->handle_base_info == xx_macho_handle_base_info) return "macho";
#endif
    return NULL;
}

Abstractformat *xx_xxds_reader_inner(Abstractformat *format, xx_pd_struct *pd)
{
#if XXDS_HAS_TAR_WRAPPER
    /* A compressed TAR reports the structures of the TAR it decodes to: base
     * info through the wrapper's own decode function, then the inner TAR
     * reader. */
    xx_tar_common *common = NULL;
    xx_tar_common_decode_fn decode = NULL;
    if (!format) return NULL;
    switch (format->file_type) {
#if XXFC_STREAM_HAS_TAR_GZ
        case XX_FILE_TYPE_TAR_GZ:
        case XX_FILE_TYPE_NPM:
            common = xx_tar_gz_common((xx_tar_gz *)format);
            decode = xx_tar_gz_decode;
            break;
#endif
#if XXFC_STREAM_HAS_TAR_BZ2
        case XX_FILE_TYPE_TAR_BZ2:
            common = xx_tar_bz2_common(format);
            decode = xx_tar_bz2_decode;
            break;
#endif
#if XXFC_STREAM_HAS_TAR_XZ
        case XX_FILE_TYPE_TAR_XZ:
            common = xx_tar_xz_common((xx_tar_xz *)format, true);
            decode = xx_tar_xz_decode;
            break;
#endif
#if XXFC_STREAM_HAS_TAR_LZ4
        case XX_FILE_TYPE_TAR_LZ4:
            common = xx_tar_lz4_common((xx_tar_lz4 *)format, true);
            decode = xx_tar_lz4_decode;
            break;
#endif
#if XXFC_STREAM_HAS_TAR_ZSTD
        case XX_FILE_TYPE_TAR_ZSTD:
            common = xx_tar_zstd_common((xx_tar_zstd *)format, true);
            decode = xx_tar_zstd_decode;
            break;
#endif
        default: return format;
    }
    if (!common || (!format->base_info_handled && !xx_tar_common_handle_base_info(common, format, decode, pd)) || !common->tar) return NULL;
    return &common->tar->format;
#else
    (void)pd;
    return format;
#endif
}

#if XXFC_STREAM_HAS_GZ
/* members: one row per gzip member. Optional parts are -1 when absent. */
static bool xxds_gz_members(Abstractformat *format, xx_xxds_table *table, xx_pd_struct *pd)
{
    xx_gz_private *priv;
    int64_t base = format->base_address;
    if ((!format->base_info_handled && !xx_format_handle_base_info(format, pd)) || !(priv = (xx_gz_private *)((xx_gz *)format)->internal)) return false;
    for (size_t i = 0; i < priv->count; ++i) {
        const xx_gz_member *m = &priv->members[i];
        if (!xx_xxds_table_row(table)) return false;
        xx_xxds_table_int(table, "header", m->header_offset - base);
        xx_xxds_table_int(table, "extra", m->extra_offset < 0 ? -1 : m->extra_offset - base);
        xx_xxds_table_int(table, "extraSize", m->extra_size);
        xx_xxds_table_int(table, "name", m->name_offset < 0 ? -1 : m->name_offset - base);
        xx_xxds_table_int(table, "nameSize", m->name_size);
        xx_xxds_table_int(table, "comment", m->comment_offset < 0 ? -1 : m->comment_offset - base);
        xx_xxds_table_int(table, "commentSize", m->comment_size);
        xx_xxds_table_int(table, "headerCrc", m->header_crc_offset < 0 ? -1 : m->header_crc_offset - base);
        xx_xxds_table_int(table, "data", m->data_offset - base);
        xx_xxds_table_int(table, "dataSize", m->compressed_size);
        xx_xxds_table_int(table, "trailer", m->trailer_offset - base);
    }
    return true;
}
#endif

#if XXFC_STREAM_HAS_RAR
/* archive: one row. version 4, 5 or 14 (RAR 1.4); signature and firstHeader
 * are where the reader found the signature (after an SFX stub or inside a PE
 * resource) and the first block. */
static bool xxds_rar_archive(Abstractformat *format, xx_xxds_table *table, xx_pd_struct *pd)
{
    const xx_rar *rar = (const xx_rar *)format;
    int64_t base = format->base_address;
    if ((!format->base_info_handled && !xx_format_handle_base_info(format, pd)) || !format->is_valid || rar->signature_offset < base ||
        rar->first_header_offset < base || !xx_xxds_table_row(table))
        return false;
    xx_xxds_table_int(table, "version", (int64_t)rar->version);
    xx_xxds_table_int(table, "signature", rar->signature_offset - base);
    xx_xxds_table_int(table, "firstHeader", rar->first_header_offset - base);
    return true;
}
#endif

#if XXFC_STREAM_HAS_ZIP
static int64_t xxds_rel_offset(int64_t offset, int64_t base)
{
    return offset < 0 ? -1 : offset - base;
}

/* archive: one row. records (central directory entry count), cd, eocd,
 * zip64 (0/1), zip64Eocd, zip64EocdSize; offsets -1 when absent. base is
 * the format's device offset (zip.xxds compares the format size with the
 * absolute EOCD offset). */
static bool xxds_zip_archive(Abstractformat *format, xx_xxds_table *table, xx_pd_struct *pd)
{
    const xx_zip *zip = (const xx_zip *)format;
    int64_t base = format->base_address;
    if (!xx_format_handle_split_format(format, pd) || (!format->base_info_handled && !xx_format_handle_base_info(format, pd)) || !format->is_valid ||
        !xx_xxds_table_row(table))
        return false;
    base = format->base_address;
    xx_xxds_table_int(table, "records", zip->number_of_records > (uint64_t)INT64_MAX ? INT64_MAX : (int64_t)zip->number_of_records);
    xx_xxds_table_int(table, "cd", xxds_rel_offset(zip->cd_offset, base));
    xx_xxds_table_int(table, "eocd", xxds_rel_offset(zip->eocd_offset, base));
    xx_xxds_table_int(table, "zip64", zip->is_zip64 ? 1 : 0);
    xx_xxds_table_int(table, "zip64Eocd", xxds_rel_offset(zip->zip64_eocd_offset, base));
    xx_xxds_table_int(table, "zip64EocdSize", zip->zip64_eocd_size);
    xx_xxds_table_int(table, "base", base);
    return true;
}

/* entries: one row per central directory entry, in directory order, as the
 * reader parses them (ZIP64 extra values, split-disk offsets, the local
 * header it resolves). cd, cdSize: the directory entry; header, headerSize:
 * its local header (header -1 without one, headerSize 0 when the local
 * header did not resolve); data, dataSize: the stored payload (data -1 when
 * the local header did not resolve). The table fails where an entry does not
 * parse. */
static bool xxds_zip_entries(Abstractformat *format, xx_xxds_table *table, xx_pd_struct *pd)
{
    xx_zip *zip = (xx_zip *)format;
    int64_t base, device_size, cursor;
    if (!xx_format_handle_split_format(format, pd) || (!format->base_info_handled && !xx_format_handle_base_info(format, pd)) || !format->is_valid)
        return false;
    base = format->base_address;
    device_size = xx_io_total_size(format->device);
    cursor = zip->cd_offset;
    for (uint64_t i = 0; i < zip->number_of_records && cursor >= 0; ++i) {
        xx_archive_record record;
        int64_t entry_size = 0;
        bool parsed;
        xx_archive_record_init(&record);
        parsed = !(pd && xx_pd_is_stopped(pd)) && xx_zip_parse_cd_entry(zip, format->device, device_size, cursor, &record, &entry_size);
        if (parsed) {
            (void)xx_zip_resolve_record_data_offset(zip, &record);
            parsed = xx_xxds_table_row(table);
        }
        if (parsed) {
            xx_xxds_table_int(table, "cd", cursor - base);
            xx_xxds_table_int(table, "cdSize", entry_size);
            xx_xxds_table_int(table, "header", xxds_rel_offset(record.header_offset, base));
            xx_xxds_table_int(table, "headerSize", record.header_size);
            xx_xxds_table_int(table, "data", xxds_rel_offset(record.data_offset, base));
            xx_xxds_table_int(table, "dataSize", record.compressed_size);
        }
        xx_archive_record_cleanup(&record);
        if (!parsed) return false;
        cursor += entry_size;
    }
    return true;
}
#endif

#if XXFC_STREAM_HAS_XZ
static xx_xz_private *xxds_xz_private(Abstractformat *format, xx_pd_struct *pd)
{
    if (!format->base_info_handled && !xx_format_handle_base_info(format, pd)) return NULL;
    return (xx_xz_private *)((xx_xz *)format)->internal;
}

/* streams: one row per stream, in file order. header, index/indexSize,
 * footer, padding/paddingSize (stream padding after the footer), and the
 * stream's blocks as firstBlock/blockCount rows of the blocks table. */
static bool xxds_xz_streams(Abstractformat *format, xx_xxds_table *table, xx_pd_struct *pd)
{
    xx_xz_private *priv = xxds_xz_private(format, pd);
    int64_t base = format->base_address;
    if (!priv) return false;
    for (size_t i = 0; i < priv->stream_count; ++i) {
        const xx_xz_stream *stream = &priv->streams[i];
        if (!xx_xxds_table_row(table)) return false;
        xx_xxds_table_int(table, "header", stream->header_offset - base);
        xx_xxds_table_int(table, "index", stream->index_offset - base);
        xx_xxds_table_int(table, "indexSize", stream->index_size);
        xx_xxds_table_int(table, "footer", stream->footer_offset - base);
        xx_xxds_table_int(table, "padding", stream->padding_offset - base);
        xx_xxds_table_int(table, "paddingSize", stream->padding_size);
        xx_xxds_table_int(table, "firstBlock", (int64_t)stream->first_block);
        xx_xxds_table_int(table, "blockCount", (int64_t)stream->block_count);
    }
    return true;
}

/* blocks: one row per block of all streams. header/headerSize, data/
 * dataSize (compressed data), padding/paddingSize (block padding), check/
 * checkSize. */
static bool xxds_xz_blocks(Abstractformat *format, xx_xxds_table *table, xx_pd_struct *pd)
{
    xx_xz_private *priv = xxds_xz_private(format, pd);
    int64_t base = format->base_address;
    if (!priv) return false;
    for (size_t i = 0; i < priv->block_count; ++i) {
        const xx_xz_block *block = &priv->blocks[i];
        if ((pd && xx_pd_is_stopped(pd)) || !xx_xxds_table_row(table)) return false;
        xx_xxds_table_int(table, "header", block->header_offset - base);
        xx_xxds_table_int(table, "headerSize", block->header_size);
        xx_xxds_table_int(table, "data", block->data_offset - base);
        xx_xxds_table_int(table, "dataSize", block->compressed_size);
        xx_xxds_table_int(table, "padding", block->padding_offset - base);
        xx_xxds_table_int(table, "paddingSize", block->padding_size);
        xx_xxds_table_int(table, "check", block->check_offset - base);
        xx_xxds_table_int(table, "checkSize", block->check_size);
    }
    return true;
}
#endif

#if XXFC_STREAM_HAS_7ZIP
static xx_7zip_private *xxds_7zip_private(Abstractformat *format, xx_pd_struct *pd)
{
    if (!format->base_info_handled && !xx_format_handle_base_info(format, pd)) return NULL;
    return format->is_valid ? (xx_7zip_private *)((xx_7zip *)format)->internal : NULL;
}

/* archive: one row. nextHeader, nextHeaderSize: the stored next header as the
 * start header gives it (nextHeaderSize 0 when there is none). */
static bool xxds_7zip_archive(Abstractformat *format, xx_xxds_table *table, xx_pd_struct *pd)
{
    const xx_7zip *archive = (const xx_7zip *)format;
    if (!xxds_7zip_private(format, pd) || !xx_xxds_table_row(table)) return false;
    xx_xxds_table_int(table, "nextHeader", archive->next_header_offset - format->base_address);
    xx_xxds_table_int(table, "nextHeaderSize", archive->next_header_size <= (uint64_t)INT64_MAX ? (int64_t)archive->next_header_size : -1);
    return true;
}

/* packs: one row per pack stream, in pack order: offset, size. The table
 * fails where a pack stream does not lie inside the device. */
static bool xxds_7zip_packs(Abstractformat *format, xx_xxds_table *table, xx_pd_struct *pd)
{
    xx_7zip_private *priv = xxds_7zip_private(format, pd);
    if (!priv) return false;
    for (uint64_t i = 0; i < priv->streams.num_packs; ++i) {
        int64_t offset;
        uint64_t size;
        if ((pd && xx_pd_is_stopped(pd)) || !xx_7zip_pack_offset(format, &priv->streams, i, &offset, &size) || size > (uint64_t)INT64_MAX ||
            !xx_xxds_table_row(table))
            return false;
        xx_xxds_table_int(table, "offset", offset - format->base_address);
        xx_xxds_table_int(table, "size", (int64_t)size);
    }
    return true;
}
#endif

#if XXFC_STREAM_HAS_TAR
static xx_tar_private *xxds_tar_private(Abstractformat *format, xx_pd_struct *pd)
{
    if (!format->base_info_handled && !xx_format_handle_base_info(format, pd)) return NULL;
    return (xx_tar_private *)((xx_tar *)format)->internal;
}

/* archive: one row. endMarker, end: the end-of-archive zero blocks run from
 * endMarker to end (the archive end the reader measured). */
static bool xxds_tar_archive(Abstractformat *format, xx_xxds_table *table, xx_pd_struct *pd)
{
    xx_tar_private *priv = xxds_tar_private(format, pd);
    if (!priv || !xx_xxds_table_row(table)) return false;
    xx_xxds_table_int(table, "endMarker", priv->end_marker_offset - format->base_address);
    xx_xxds_table_int(table, "end", priv->archive_end - format->base_address);
    return true;
}

/* members: one row per physical header (metadata headers included), in
 * file order: header (512-byte header block), data, dataSize (stored data),
 * paddedSize (the data rounded up to whole blocks as the reader measured it). */
static bool xxds_tar_members(Abstractformat *format, xx_xxds_table *table, xx_pd_struct *pd)
{
    xx_tar_private *priv = xxds_tar_private(format, pd);
    if (!priv) return false;
    for (size_t i = 0; i < priv->count; ++i) {
        const xx_tar_member *member = &priv->members[i];
        if ((pd && xx_pd_is_stopped(pd)) || !xx_xxds_table_row(table)) return false;
        xx_xxds_table_int(table, "header", member->header_offset - format->base_address);
        xx_xxds_table_int(table, "data", member->data_offset - format->base_address);
        xx_xxds_table_int(table, "dataSize", member->data_size);
        xx_xxds_table_int(table, "paddedSize", member->padded_size);
    }
    return true;
}
#endif

/* archiveRecords: one row per archive record of any reader that iterates
 * records, in iteration order: header_offset, header_size, data_offset,
 * compressed_size (offsets relative to the format start; a negative result
 * stays negative), original_name, and compression_method / crc32 when the
 * record carries that metadata. No table when the reader has no record
 * iteration or cannot start it. */
static bool xxds_archive_records(Abstractformat *format, xx_xxds_table *table, xx_pd_struct *pd)
{
    xx_archive_record_state *state;
    const xx_archive_record *record;
    int64_t base = format->base_address;
    bool ok = true;
    if (!format->create_archive_records_reading || !format->get_current_archive_record || !format->archive_record_move_to_next ||
        !format->free_archive_records_reading || !(state = (format->create_archive_records_reading)(format, NULL, pd)))
        return false;
    for (record = (format->get_current_archive_record)(format, state); ok && record; record = (format->get_current_archive_record)(format, state)) {
        const char *name = xx_archive_record_get_original_name(record);
        ok = !(pd && xx_pd_is_stopped(pd)) && xx_xxds_table_row(table);
        if (!ok) break;
        xx_xxds_table_int(table, "header_offset", record->header_offset - base);
        xx_xxds_table_int(table, "header_size", record->header_size);
        xx_xxds_table_int(table, "data_offset", record->data_offset - base);
        xx_xxds_table_int(table, "compressed_size", record->compressed_size);
        xx_xxds_table_str(table, "original_name", name, name ? xx_rt_strlen(name) : 0U);
        /* Optional metadata: the key exists only when the record carries it. */
        if (xx_archive_record_find_meta(record, XX_META_ID_COMPRESSION_METHOD))
            xx_xxds_table_int(table, "compression_method", (int64_t)xx_archive_record_get_meta_u64(record, XX_META_ID_COMPRESSION_METHOD, 0U));
        if (xx_archive_record_find_meta(record, XX_META_ID_CRC32))
            xx_xxds_table_int(table, "crc32", (int64_t)xx_archive_record_get_meta_u64(record, XX_META_ID_CRC32, 0U));
        if (!(format->archive_record_move_to_next)(format, state, pd)) break;
    }
    (format->free_archive_records_reading)(format, state);
    return ok;
}

/* ------------------------------------------------- facts of the schema scripts
 * The sx_<family> scripts were generated from the retired schema engine, whose walkers read a few members of
 * concrete readers. Those members are published here, one table per need; offsets are relative to the base
 * the script runs on. Profiles without the reader factory have none of these readers. */
#if !defined(XXFC_PROFILE_APK_EMUL) && !defined(XXFC_PROFILE_DOTNET_EMUL) && !defined(XXFC_PROFILE_DIE_ENGINE)
#define XXDS_HAS_SCHEMA_FACTS 1
#include "xxfclib/formats/archive/sfx_tgcf/xx_sfx_tgcf.h"
#include "xxfclib/formats/executable/binsh_starkit/xx_binsh_starkit.h"
#include "xxfclib/formats/executable/molebox/xx_molebox.h"
#include "xxfclib/formats/filesystem/apfs/xx_apfs.h"
#include "xxfclib/formats/filesystem/btrfs/xx_btrfs.h"
#include "xxfclib/formats/filesystem/cpm/xx_cpm.h"
#include "xxfclib/formats/filesystem/gpt/xx_gpt.h"
#include "xxfclib/formats/filesystem/psa_disk/xx_psa_disk.h"
#include "xxfclib/formats/filesystem/volume/xx_volume.h"

bool xx_xxds_reader_container(Abstractformat *format, int64_t *base, int64_t *size)
{
    int64_t inner = 0;
    if (!format) return false;
    if (format->file_type == XX_FILE_TYPE_PSA_DISK) return xx_psa_disk_get_container_span(format, base, size) && *base >= 0 && *base <= format->base_address;
    if (format->file_type == XX_FILE_TYPE_BINSH_STARKIT)
        return xx_binsh_starkit_get_container_span(format, base, size, &inner) && *base >= 0 && *base <= format->base_address;
    return false;
}

/* The base the script of `format` runs on: the container base for the two container types. */
static int64_t xxds_script_base(Abstractformat *format)
{
    int64_t base = 0, size = 0;
    return xx_xxds_reader_container(format, &base, &size) ? base : format->base_address;
}

/* "reader": the members of the concrete reader the walkers read (one row). */
static bool xxds_schema_reader(Abstractformat *format, xx_xxds_table *table, xx_pd_struct *pd)
{
    if ((!format->base_info_handled && !xx_format_handle_base_info(format, pd)) || !format->is_valid) return false;
    switch (format->file_type) {
        case XX_FILE_TYPE_GPT: {
            const xx_gpt *gpt = (const xx_gpt *)format;
            return xx_xxds_table_row(table) && xx_xxds_table_int(table, "block_size", gpt->block_size) && xx_xxds_table_int(table, "used_backup", gpt->used_backup ? 1 : 0);
        }
        case XX_FILE_TYPE_PC98_PARTITIONS: {
            const xx_volume *volume = (const xx_volume *)format;
            return xx_xxds_table_row(table) && xx_xxds_table_int(table, "logical_sector_size", volume->logical_sector_size);
        }
        case XX_FILE_TYPE_APFS: {
            const xx_apfs *apfs = (const xx_apfs *)format;
            return xx_xxds_table_row(table) && xx_xxds_table_int(table, "block_size", apfs->block_size) && xx_xxds_table_int(table, "omap_oid", (int64_t)apfs->omap_oid) &&
                   xx_xxds_table_int(table, "volume_count", apfs->volume_count);
        }
        case XX_FILE_TYPE_BTRFS: {
            const xx_btrfs *btrfs = (const xx_btrfs *)format;
            return xx_xxds_table_row(table) && xx_xxds_table_int(table, "super_offset", btrfs->super_offset - format->base_address) &&
                   xx_xxds_table_int(table, "csum_type", btrfs->csum_type) && xx_xxds_table_int(table, "node_size", btrfs->node_size) &&
                   xx_xxds_table_int(table, "devid", (int64_t)btrfs->devid) && xx_xxds_table_int(table, "root_tree", (int64_t)btrfs->root_tree) &&
                   xx_xxds_table_int(table, "chunk_root", (int64_t)btrfs->chunk_root) && xx_xxds_table_int(table, "fs_tree_root", (int64_t)btrfs->fs_tree_root) &&
                   xx_xxds_table_str(table, "fsid", (const char *)btrfs->fsid, sizeof(btrfs->fsid)) &&
                   xx_xxds_table_str(table, "metadata_uuid", (const char *)btrfs->metadata_uuid, sizeof(btrfs->metadata_uuid));
        }
        default: return false;
    }
}

/* "reader.volumes": xx_apfs.volumes[]. */
static bool xxds_schema_apfs_volumes(Abstractformat *format, xx_xxds_table *table, xx_pd_struct *pd)
{
    const xx_apfs *apfs = (const xx_apfs *)format;
    if (format->file_type != XX_FILE_TYPE_APFS || (!format->base_info_handled && !xx_format_handle_base_info(format, pd)) || !format->is_valid) return false;
    for (uint32_t i = 0; i < apfs->volume_count && i < XX_APFS_MAX_VOLUMES; ++i) {
        if (!xx_xxds_table_row(table) || !xx_xxds_table_int(table, "block", apfs->volumes[i].block) || !xx_xxds_table_int(table, "omap_oid", (int64_t)apfs->volumes[i].omap_oid) ||
            !xx_xxds_table_int(table, "superblock_valid", apfs->volumes[i].superblock_valid ? 1 : 0))
            return false;
    }
    return true;
}

/* "container": the authenticated outer container; base is where the script's offsets start (0). */
static bool xxds_schema_container(Abstractformat *format, xx_xxds_table *table)
{
    int64_t base = 0, size = 0, inner = 0;
    if (!xx_xxds_reader_container(format, &base, &size)) return false;
    if (format->file_type == XX_FILE_TYPE_BINSH_STARKIT && !xx_binsh_starkit_get_container_span(format, &base, &size, &inner)) return false;
    return xx_xxds_table_row(table) && xx_xxds_table_int(table, "base", 0) && xx_xxds_table_int(table, "size", size) && xx_xxds_table_int(table, "inner", inner - base);
}

static bool xxds_schema_tgcf(Abstractformat *format, xx_xxds_table *table, xx_pd_struct *pd)
{
    int64_t at = 0, size = 0;
    if (format->file_type != XX_FILE_TYPE_SFX_TGCF || !xx_sfx_tgcf_get_payload_span(format, &at, &size, pd)) return false;
    return xx_xxds_table_row(table) && xx_xxds_table_int(table, "at", at - format->base_address) && xx_xxds_table_int(table, "size", size);
}

/* "moleboxMetadata": xx_molebox_get_physical_metadata() per package index. */
static bool xxds_schema_molebox(Abstractformat *format, xx_xxds_table *table, xx_pd_struct *pd)
{
    xx_molebox_physical_metadata m;
    if (format->file_type != XX_FILE_TYPE_MOLEBOX) return false;
    for (uint32_t i = 0; i < 64U; ++i) {
        if (!xx_molebox_get_physical_metadata(format, i, &m, pd)) break;
        if (!xx_xxds_table_row(table) || !xx_xxds_table_int(table, "package_count", m.package_count) ||
            !xx_xxds_table_int(table, "package_offset", m.package_offset - format->base_address) || !xx_xxds_table_int(table, "tree_offset", m.tree_offset - format->base_address) ||
            !xx_xxds_table_int(table, "header_offset", m.header_offset - format->base_address) || !xx_xxds_table_int(table, "catalog_offset", m.catalog_offset - format->base_address) ||
            !xx_xxds_table_int(table, "tree_size", m.tree_size) || !xx_xxds_table_int(table, "catalog_size", m.catalog_size) ||
            !xx_xxds_table_int(table, "has_verified_prefix", m.has_verified_prefix ? 1 : 0))
            return false;
        if (i + 1U >= m.package_count) break;
    }
    return true;
}

/* "cpmDirectory": xx_cpm_get_directory_slot() per slot index. */
static bool xxds_schema_cpm(Abstractformat *format, xx_xxds_table *table, xx_pd_struct *pd)
{
    xx_archive_record_state *state;
    xx_cpm_directory_slot slot;
    bool ok = true;
    if (format->create_archive_records_reading != xx_cpm_create_archive_records_reading) return false;
    state = xx_format_create_archive_records_reading(format, NULL, pd);
    if (!state) return false;
    for (uint32_t i = 0; ok && i < 262144U; ++i) {
        if (!xx_cpm_get_directory_slot(format, state, i, &slot)) break;
        ok = xx_xxds_table_row(table) && xx_xxds_table_int(table, "offset", slot.offset - format->base_address) && xx_xxds_table_int(table, "entry_count", slot.entry_count) &&
             xx_xxds_table_int(table, "allocation_pointer_size", slot.allocation_pointer_size) && xx_xxds_table_int(table, "version", (int64_t)slot.version) &&
             xx_xxds_table_int(table, "length_mode", (int64_t)slot.length_mode);
        if (i + 1U >= slot.entry_count) break;
    }
    xx_format_free_archive_records_reading(format, state);
    return ok;
}

static bool xxds_schema_table(Abstractformat *format, const char *name, xx_xxds_table *table, xx_pd_struct *pd)
{
    if (!xx_rt_strcmp(name, "reader")) return xxds_schema_reader(format, table, pd);
    if (!xx_rt_strcmp(name, "reader.volumes")) return xxds_schema_apfs_volumes(format, table, pd);
    if (!xx_rt_strcmp(name, "container")) return xxds_schema_container(format, table);
    if (!xx_rt_strcmp(name, "tgcfPayload")) return xxds_schema_tgcf(format, table, pd);
    if (!xx_rt_strcmp(name, "moleboxMetadata")) return xxds_schema_molebox(format, table, pd);
    if (!xx_rt_strcmp(name, "cpmDirectory")) return xxds_schema_cpm(format, table, pd);
    return false;
}
xx_file_type_t xx_xxds_reader_alias_type(xx_file_type_t type)
{
    switch (type) {
        case XX_FILE_TYPE_DIE_MUSIC_NUNN_FUNKTRACKER: return XX_FILE_TYPE_TRACKER_FUNK;
        case XX_FILE_TYPE_DIE_MUSIC_EXTREMESTRACKER_AMS: return XX_FILE_TYPE_TRACKER_AMS;
        case XX_FILE_TYPE_DIE_MUSIC_MLAT_ADLIBTRACKER_MAD: return XX_FILE_TYPE_MAD_TRACKER;
        case XX_FILE_TYPE_DIE_MUSIC_OPENMPT_MPTM: return XX_FILE_TYPE_TRACKER_IT;
        case XX_FILE_TYPE_DIE_MUSIC_STANDARD_MIDI: return XX_FILE_TYPE_MIDI;
        case XX_FILE_TYPE_DIE_MUSIC_ELYSSIS_AMUSIC_AMD: return XX_FILE_TYPE_ADLIB_AMD;
        case XX_FILE_TYPE_DIE_MUSIC_TYR_BOOMTRACKER4_CFF: return XX_FILE_TYPE_CUDFM_CFF;
        case XX_FILE_TYPE_DIE_MUSIC_FAUST_FAUSTMUSICCREATOR_SNG: return XX_FILE_TYPE_FAUST_FMC;
        case XX_FILE_TYPE_DIE_MUSIC_MK_JAMZ: return XX_FILE_TYPE_ADLIB_MKJ;
        case XX_FILE_TYPE_DIE_MUSIC_PYROFYRE_MASTERTRACKER: return XX_FILE_TYPE_ADLIB_MTR;
        case XX_FILE_TYPE_DIE_MUSIC_SURPRISEPRODUCTIONS_ADLIBTRACKER: return XX_FILE_TYPE_ADLIB_SA2;
        case XX_FILE_TYPE_DIE_MUSIC_OREN_LIQUIDTRACKER: return XX_FILE_TYPE_TRACKER_LIQUID;
        case XX_FILE_TYPE_DIE_MUSIC_DLUSION_XTRACKER_DMF: return XX_FILE_TYPE_TRACKER_DMF;
        case XX_FILE_TYPE_DIE_MUSIC_LONERANGER_POLYTRACKER_PTM: return XX_FILE_TYPE_TRACKER_PTM;
        case XX_FILE_TYPE_DIE_MUSIC_LINCOLN_QUADRACOMPOSER_EMOD: return XX_FILE_TYPE_TRACKER_EMOD;
        case XX_FILE_TYPE_DIE_MUSIC_DELWICHE_MADTRACKER2: return XX_FILE_TYPE_TRACKER_MT2;
        case XX_FILE_TYPE_DIE_MUSIC_EPIC_MASI_PSM: return XX_FILE_TYPE_TRACKER_PSM;
        case XX_FILE_TYPE_DIE_MUSIC_SOFTJEE_DIGITALTRACKER_DTM: return XX_FILE_TYPE_TRACKER_DTM;
        case XX_FILE_TYPE_DIE_MUSIC_HASENFRATZ_REALTRACKER: return XX_FILE_TYPE_TRACKER_REAL;
        case XX_FILE_TYPE_DIE_MUSIC_SIMPLET_MEGATRACKER: return XX_FILE_TYPE_TRACKER_MEGATRACKER;
        case XX_FILE_TYPE_DIE_MUSIC_LIONET_AMOSMUSICBANK_ABK: return XX_FILE_TYPE_AMOS_MUSIC_BANK;
        case XX_FILE_TYPE_DIE_MUSIC_VIDEOGAMEMUSIC_VGM: return XX_FILE_TYPE_VGM_LOG;
        case XX_FILE_TYPE_DIE_MUSIC_BIENIAS_SLIGHTATARIPLAYER: return XX_FILE_TYPE_ATARI_SAP;
        case XX_FILE_TYPE_DIE_MUSIC_BEN_SC68000: return XX_FILE_TYPE_SC68_MUSIC;
        case XX_FILE_TYPE_DIE_MUSIC_ABYSS_HIGHESTEXPERIENCE: return XX_FILE_TYPE_AMIGA_AHX;
        case XX_FILE_TYPE_DIE_MUSIC_BULBA_AYEMUL: return XX_FILE_TYPE_ZX_AYEMUL;
        case XX_FILE_TYPE_DIE_MUSIC_APCTCP_DIGIBOOSTERPRO_DBM: return XX_FILE_TYPE_TRACKER_DBM;
        case XX_FILE_TYPE_DIE_MUSIC_SANDER_OKTALYZER: return XX_FILE_TYPE_TRACKER_OKT;
        case XX_FILE_TYPE_DIE_MUSIC_HUSS_FASTTRACKER2_XM: return XX_FILE_TYPE_TRACKER_XM;
        case XX_FILE_TYPE_DIE_MUSIC_PSI_SCREAMTACKER3_S3M: return XX_FILE_TYPE_TRACKER_S3M;
        case XX_FILE_TYPE_DIE_MUSIC_LIM_IMPULSETRACKER: return XX_FILE_TYPE_TRACKER_IT;
        case XX_FILE_TYPE_DIE_MUSIC_STARSCREAM_MULTITRACKER: return XX_FILE_TYPE_TRACKER_MTM;
        case XX_FILE_TYPE_DIE_MUSIC_MAS_ULTRATRACKER: return XX_FILE_TYPE_TRACKER_ULT;
        case XX_FILE_TYPE_DIE_MUSIC_APCTCP_DIGIBOOSTER_DIGI: return XX_FILE_TYPE_TRACKER_DIGI;
        case XX_FILE_TYPE_DIE_MUSIC_AMIGAFREELANCERS_PROTRACKER_MOD: return XX_FILE_TYPE_TRACKER_MOD;
        case XX_FILE_TYPE_DIE_MUSIC_POTTER_FARANDOLE: return XX_FILE_TYPE_TRACKER_FAR;
        case XX_FILE_TYPE_DIE_MUSIC_NFACTOR_DIGITRAKKER_MDL: return XX_FILE_TYPE_TRACKER_MDL;
        case XX_FILE_TYPE_DIE_MUSIC_BWSB_GDM: return XX_FILE_TYPE_TRACKER_GDM;
        case XX_FILE_TYPE_DIE_MUSIC_KINNUNEN_OCTAMED_MMD: return XX_FILE_TYPE_TRACKER_MED;
        case XX_FILE_TYPE_DIE_MUSIC_ROEDER_IMAGOORPHEUS_IMF: return XX_FILE_TYPE_TRACKER_IMF;
        case XX_FILE_TYPE_DIE_MUSIC_DSMI_AMF: return XX_FILE_TYPE_TRACKER_AMF;
        case XX_FILE_TYPE_DIE_MUSIC_EA_ASYLUMMUSICFORMAT: return XX_FILE_TYPE_ASYLUM_AMF;
        case XX_FILE_TYPE_DIE_MUSIC_HALLER_SOUNDFX_MULTIMEDIASOUND: return XX_FILE_TYPE_TRACKER_SOUNDFX;
        case XX_FILE_TYPE_DIE_MUSIC_REALITYADLIBTRACKER_RAD: return XX_FILE_TYPE_ADLIB_RAD;
        case XX_FILE_TYPE_DIE_MUSIC_SEIFERT_HSC: return XX_FILE_TYPE_ADLIB_HSC;
        case XX_FILE_TYPE_DIE_MUSIC_JCH_EDLIB_D00_D01: return XX_FILE_TYPE_ADLIB_D00;
        case XX_FILE_TYPE_DIE_MUSIC_BOB_ADLIBMUSIC_BAM: return XX_FILE_TYPE_ADLIB_BAM;
        case XX_FILE_TYPE_DIE_MUSIC_THEBRAIN_EASYADLIB_MBF: return XX_FILE_TYPE_ADLIB_BMF;
        case XX_FILE_TYPE_DIE_MUSIC_VERHAAG_DIGITALFM: return XX_FILE_TYPE_ADLIB_DFM;
        case XX_FILE_TYPE_DIE_MUSIC_LOUDNESS_SOUNDSYSTEM_LDS: return XX_FILE_TYPE_ADLIB_LDS;
        case XX_FILE_TYPE_DIE_MUSIC_ADLIB_ROL: return XX_FILE_TYPE_ADLIB_ROL;
        case XX_FILE_TYPE_DIE_MUSIC_BJERREGAARD_JBM: return XX_FILE_TYPE_ADLIB_JBM;
        case XX_FILE_TYPE_DIE_MUSIC_TAYLOR_EXTRASIMPLEMUSIC_XSM: return XX_FILE_TYPE_ADLIB_XSM;
        case XX_FILE_TYPE_DIE_MUSIC_SILVERMAN_ADLIB_KSM: return XX_FILE_TYPE_KEN_KSM;
        case XX_FILE_TYPE_DIE_MUSIC_RAWOPLCAPTURE_RAWADATA: return XX_FILE_TYPE_RDOS_RAW;
        case XX_FILE_TYPE_DIE_MUSIC_CORLETT_PSF_SERIES: return XX_FILE_TYPE_SONY_PSF;
        case XX_FILE_TYPE_DIE_MUSIC_GAMEBOY_SOUND: return XX_FILE_TYPE_GAMEBOY_GBS;
        case XX_FILE_TYPE_DIE_MUSIC_HUDSONENTSYS_HES: return XX_FILE_TYPE_HES_SOUND;
        case XX_FILE_TYPE_DIE_MUSIC_NINTENDOSOUNDFORMAT_NESMNSFE: return XX_FILE_TYPE_NINTENDO_NSF;
        case XX_FILE_TYPE_DIE_MUSIC_RURURU_S98: return XX_FILE_TYPE_S98_LOG;
        case XX_FILE_TYPE_DIE_MUSIC_C64SID: return XX_FILE_TYPE_PSID_SID;
        case XX_FILE_TYPE_DIE_MUSIC_NINTENDO_SNESSPC700: return XX_FILE_TYPE_SNES_SPC;
        case XX_FILE_TYPE_DIE_MUSIC_LEONARD_YM: return XX_FILE_TYPE_YAMAHA_YM;
        case XX_FILE_TYPE_DIE_MUSIC_CREATIVELABS_CMF: return XX_FILE_TYPE_CREATIVE_CMF;
        case XX_FILE_TYPE_DIE_MUSIC_DOSBOXRAWOPL: return XX_FILE_TYPE_DOSBOX_DRO;
        case XX_FILE_TYPE_TAR_COMPRESS: return XX_FILE_TYPE_UNIX_COMPRESS;
        case XX_FILE_TYPE_TAR_LZIP: return XX_FILE_TYPE_LZIP;
        case XX_FILE_TYPE_TAR_LZMA: return XX_FILE_TYPE_LZMA;
        case XX_FILE_TYPE_TAR_LZOP: return XX_FILE_TYPE_LZOP;
        default: return type;
    }
}

bool xx_xxds_reader_alias_open(Abstractformat *format, xx_pd_struct *pd, Abstractformat **canonical)
{
    xx_file_type_t type = format ? xx_xxds_reader_alias_type(format->file_type) : XX_FILE_TYPE_UNKNOWN;
    Abstractformat *reader;
    *canonical = NULL;
    if (!format || type == format->file_type) return true;
    if ((!format->base_info_handled && !xx_format_handle_base_info(format, pd)) || !format->is_valid) return false;
    /* Base zero keeps the caller's device coordinates: the factory's non-zero-base path would create a
     * sliced view whose offsets start at zero. */
    reader = xx_format_create_reader(type, format->device, 0);
    if (!reader) return false;
    xx_format_set_base_address(reader, format->base_address);
    if (!xx_format_handle_base_info(reader, pd) || !reader->is_valid) {
        xx_format_free_reader(reader);
        return false;
    }
    *canonical = reader;
    return true;
}

void xx_xxds_reader_alias_close(Abstractformat *canonical)
{
    if (canonical) xx_format_free_reader(canonical);
}
#else
#define XXDS_HAS_SCHEMA_FACTS 0
xx_file_type_t xx_xxds_reader_alias_type(xx_file_type_t type)
{
    return type;
}

bool xx_xxds_reader_alias_open(Abstractformat *format, xx_pd_struct *pd, Abstractformat **canonical)
{
    (void)format;
    (void)pd;
    *canonical = NULL;
    return true;
}

void xx_xxds_reader_alias_close(Abstractformat *canonical)
{
    (void)canonical;
}

bool xx_xxds_reader_container(Abstractformat *format, int64_t *base, int64_t *size)
{
    (void)format;
    (void)base;
    (void)size;
    return false;
}
#endif

bool xx_xxds_reader_table(Abstractformat *format, const char *name, xx_xxds_table *table, xx_pd_struct *pd)
{
    if (!format || !name || !table) return false;
    if (!xx_rt_strcmp(name, "archiveRecords")) return xxds_archive_records(format, table, pd);
#if XXDS_HAS_SCHEMA_FACTS
    if (xxds_schema_table(format, name, table, pd)) return true;
#endif
#if XXFC_STREAM_HAS_GZ
    if (format->file_type == XX_FILE_TYPE_GZ && !xx_rt_strcmp(name, "members")) return xxds_gz_members(format, table, pd);
#endif
#if XXFC_STREAM_HAS_RAR
    if (format->file_type == XX_FILE_TYPE_RAR && format->handle_base_info == xx_rar_handle_base_info && !xx_rt_strcmp(name, "archive"))
        return xxds_rar_archive(format, table, pd);
#endif
#if XXFC_STREAM_HAS_ZIP
    if (xxds_is_zip(format)) {
        if (!xx_rt_strcmp(name, "archive")) return xxds_zip_archive(format, table, pd);
        if (!xx_rt_strcmp(name, "entries")) return xxds_zip_entries(format, table, pd);
    }
#endif
#if XXFC_STREAM_HAS_XZ
    if (format->file_type == XX_FILE_TYPE_XZ && format->handle_base_info == xx_xz_handle_base_info) {
        if (!xx_rt_strcmp(name, "streams")) return xxds_xz_streams(format, table, pd);
        if (!xx_rt_strcmp(name, "blocks")) return xxds_xz_blocks(format, table, pd);
    }
#endif
#if XXFC_STREAM_HAS_7ZIP
    if (format->file_type == XX_FILE_TYPE_7ZIP) {
        if (!xx_rt_strcmp(name, "archive")) return xxds_7zip_archive(format, table, pd);
        if (!xx_rt_strcmp(name, "packs")) return xxds_7zip_packs(format, table, pd);
    }
#endif
#if XXFC_STREAM_HAS_TAR
    if (format->file_type == XX_FILE_TYPE_TAR && format->handle_base_info == xx_tar_handle_base_info) {
        if (!xx_rt_strcmp(name, "archive")) return xxds_tar_archive(format, table, pd);
        if (!xx_rt_strcmp(name, "members")) return xxds_tar_members(format, table, pd);
    }
#endif
    (void)pd;
    return false;
}
