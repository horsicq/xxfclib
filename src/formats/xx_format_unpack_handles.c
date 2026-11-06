/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxfclib/formats/xx_format.h"
#include "xx_format_fast_detect_rules.h"
#include "xx_format_handle_policy.h"
#include "xxfclib/formats/executable/upx_engine/xx_upx_engine.h"
#include "xxfclib/formats/executable/molebox/xx_molebox.h"
#include "xxfclib/formats/archive/zip/xx_zip.h"
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/formats/installer/lifkd/xx_lifkd.h"

typedef struct xx_unpack_handle_row {
    xx_handle_type_t handle;
    xx_file_type_t file_type;
    unsigned carrier;
} xx_unpack_handle_row;

static const xx_unpack_handle_row handle_rows[] = {
#define XX_UNPACK_HANDLE(name, value, type, carrier) {XX_HANDLE_TYPE_##name, XX_FILE_TYPE_##type, carrier},
#if !XFU_FORMAT_SUPPORT_FULL
#define XX_SUPPORTED_FORMAT(type, name, label) {XX_HANDLE_TYPE_##type, XX_FILE_TYPE_##type, 0},
#include "xxfclib/formats/xx_format_support_rows.inc"
#undef XX_SUPPORTED_FORMAT
#else
#include "xxfclib/formats/xx_format_unpack_handle_rows.inc"
#endif
#undef XX_UNPACK_HANDLE
};

static bool xx_handle_can_unpack(const Abstractformat *format)
{
    return format && !xx_format_handle_is_inspection_only(format->file_type) && format->file_type != XX_FILE_TYPE_DER &&
           (!format->base_info_handled || format->is_archive) && format->create_archive_records_reading && format->get_current_archive_record && format->unpack_current_archive_record &&
           format->archive_record_move_to_next && format->free_archive_records_reading;
}

xx_handle_type_t xx_format_handle_type_from_file_type(xx_file_type_t type)
{
    uint8_t dummy = 0;
    xx_io_device *empty;
    Abstractformat *format;
    bool supported;
    size_t i;
    if (xx_format_handle_is_inspection_only(type)) return XX_HANDLE_TYPE_UNKNOWN;
#if XFU_FORMAT_SUPPORT_FULL
    if (type == XX_FILE_TYPE_NUMBERED_SPLIT) return XX_HANDLE_TYPE_NUMBERED_SPLIT;
#endif
    empty = xx_io_mem_open_ro(&dummy, 0U);
    if (!empty) return XX_HANDLE_TYPE_UNKNOWN;
    format = xx_format_create_reader(type, empty, 0);
    supported = xx_handle_can_unpack(format);
    xx_format_free_reader(format);
    xx_io_close(empty);
    if (supported) {
        for (i = 0; i < sizeof(handle_rows) / sizeof(handle_rows[0]); ++i)
            if (!handle_rows[i].carrier && handle_rows[i].file_type == type) return handle_rows[i].handle;
    }
    return XX_HANDLE_TYPE_UNKNOWN;
}

static bool xx_handle_append(xx_list_t *types, xx_handle_type_t type)
{
    return type == XX_HANDLE_TYPE_UNKNOWN || xx_list_contains(types, &type, NULL) || xx_list_append(types, &type);
}

static bool xx_handle_has_carrier_reader(xx_file_type_t type)
{
    size_t i;
    /* These newer native wrappers live under executable/, outside the
     * generated installer/SFX descriptor family. They retain their own
     * independent handles and authenticate their own member structures. */
    if (type == XX_FILE_TYPE_ENIGMA_VIRTUAL_BOX || type == XX_FILE_TYPE_MOLEBOX || type == XX_FILE_TYPE_BINSH_STARKIT || type == XX_FILE_TYPE_UPX) return true;
    for (i = 0; i < sizeof(handle_rows) / sizeof(handle_rows[0]); ++i)
        if (handle_rows[i].carrier && handle_rows[i].file_type == type) return true;
    return false;
}

/* These readers import a user-declared source layout. Their constructors
 * intentionally accept geometry/size (or sampled signal bytes) rather than
 * authenticate a format identity. Keep them selectable by handle or matching
 * suffix, but never infer one from arbitrary bytes during a deep scan. See
 * the native reader headers and xx_disk_additions_detect.inc. Headerless
 * compression readers with complete, strict stream grammars remain eligible. */
static bool xx_handle_auto_candidate(xx_file_type_t type)
{
    switch (type) {
        case XX_FILE_TYPE_HXC_LOGIC_ANALYZER:
        case XX_FILE_TYPE_HXC_RAW_FLOPPY:
        case XX_FILE_TYPE_MICRAL_N_RAW:
        case XX_FILE_TYPE_ANEX86_HDI:
        case XX_FILE_TYPE_APOLLO_AFD:
        case XX_FILE_TYPE_CAMPUTERS_LYNX_LDF:
        case XX_FILE_TYPE_CMD_D1M:
        case XX_FILE_TYPE_CMD_D2M:
        case XX_FILE_TYPE_CMD_D4M:
        case XX_FILE_TYPE_CPM:
        case XX_FILE_TYPE_FDCOPY_CFI:
        case XX_FILE_TYPE_MYZ80:
        case XX_FILE_TYPE_NANOWASP:
        case XX_FILE_TYPE_GOTEK:
        case XX_FILE_TYPE_NORTHSTAR_NSI:
        case XX_FILE_TYPE_OS9_RBF:
        case XX_FILE_TYPE_PCE_XDF:
        case XX_FILE_TYPE_SIMH_DISK:
        case XX_FILE_TYPE_THOMSON_FD: return false;
        case XX_FILE_TYPE_INSTALIT_DATA: return false;
        /* Only the ordered deep fallback below may infer this view. */
        case XX_FILE_TYPE_EXECUTABLE_OVERLAY: return false;
        default: return true;
    }
}

/* Header prefilter for the complete native executable-carrier reader set.
 * Each individual reader still authenticates its carrier and archive. */
static bool xx_handle_is_executable(xx_io_device *dev)
{
    uint8_t h[4];
    if (!xx_io_read_at(dev, 0, h, 2U)) return false;
    if ((h[0] == 'M' && h[1] == 'Z') || (h[0] == 'Z' && h[1] == 'M') || (h[0] == 0x60U && h[1] == 0x1aU) ||
        (h[0] == '#' && h[1] == '!') || h[0] == 0xe9U || h[0] == 0xebU || h[0] == 0x18U || h[0] == 0xc3U) return true;
    return xx_io_read_at(dev, 0, h, sizeof(h)) && h[0] == 0x7fU && h[1] == 'E' && h[2] == 'L' && h[3] == 'F';
}

static void xx_handle_prepare_reader(Abstractformat *format, xx_file_type_t type)
{
    /* Unpack interpretations can use the authenticated ZIP end directory
     * after a stub or other leading data. Physical reader construction stays
     * strict; this opt-in is shared by discovery and selected-handle opening. */
    if (format && (type == XX_FILE_TYPE_ZIP || type == XX_FILE_TYPE_ZIP64)) ((xx_zip *)format)->allow_leading_data = true;
}

static bool xx_handle_probe_format(Abstractformat *format, xx_file_type_t type, xx_list_t *types)
{
    xx_handle_type_t handle = XX_HANDLE_TYPE_UNKNOWN;
    xx_handle_prepare_reader(format, type);
    /* The native UPX reader can use a configured official backend. Never
     * ask that backend to probe unrelated input: first authenticate the
     * bounded carrier/UPX pack-header marker (including COM/SYS loaders). */
#if XFU_FORMAT_SUPPORT_FULL
    if (type == XX_FILE_TYPE_UPX && (!format || !xx_upx_has_marker_device(format->device, NULL))) return true;
#endif
    if (xx_handle_can_unpack(format) && xx_format_is_valid(format, NULL)) {
        bool parsed = (type == XX_FILE_TYPE_WISE_INSTALLATION_SYSTEM) ? true : xx_format_handle_base_info(format, NULL);
        /* A CRC-authenticated 7z encoded-header grammar can identify AES
         * encryption before a password is supplied. Retain that reader so
         * callers can apply their password before opening its directory. */
        if ((parsed && format->is_archive) || (type == XX_FILE_TYPE_7ZIP && format->is_crypted)) handle = xx_format_get_handle_type(format);
    }
    return xx_handle_append(types, handle);
}

static bool xx_handle_probe(xx_io_device *dev, xx_file_type_t type, xx_list_t *types)
{
    Abstractformat *format = xx_format_create_reader(type, dev, 0);
    bool result = xx_handle_probe_format(format, type, types);
    xx_format_free_reader(format);
    return result;
}

static unsigned char xx_handle_ascii_lower(unsigned char c)
{
    return c >= 'A' && c <= 'Z' ? (unsigned char)(c + ('a' - 'A')) : c;
}

/* Native extensions may contain a single compound suffix or a list such as
 * "zip;jar". Match only complete basename suffixes, case insensitively. */
static bool xx_handle_extension_matches(const char *path, const char *extensions)
{
    const char *name = path, *cursor;
    size_t length = 0;
    if (!path || !extensions) return false;
    for (cursor = path; *cursor; ++cursor)
        if (*cursor == '/' || *cursor == '\\') name = cursor + 1;
    for (cursor = name; *cursor; ++cursor) ++length;
    cursor = extensions;
    while (*cursor) {
        const char *start;
        size_t n = 0, i;
        while (*cursor == ' ' || *cursor == '\t' || *cursor == ';' || *cursor == ',' || *cursor == '|' || *cursor == '/') ++cursor;
        if (*cursor == '*') ++cursor;
        if (*cursor == '.') ++cursor;
        start = cursor;
        while (*cursor && *cursor != ' ' && *cursor != '\t' && *cursor != ';' && *cursor != ',' && *cursor != '|' && *cursor != '/') { ++cursor; ++n; }
        if (!n || length <= n || name[length - n - 1U] != '.') continue;
        for (i = 0; i < n; ++i)
            if (xx_handle_ascii_lower((unsigned char)name[length - n + i]) != xx_handle_ascii_lower((unsigned char)start[i])) break;
        if (i == n) return true;
    }
    return false;
}

static int xx_handle_signature(xx_file_type_t type, xx_io_device *dev)
{
    Abstractextractor *extractor = xx_abstract_extractor_get(type);
    if (!extractor) return 0;
#if !XFU_FORMAT_SUPPORT_FULL
    return extractor->signature ? extractor->signature(dev, 0, false) : -1;
#else
    uint8_t magic[4];
    if (type == XX_FILE_TYPE_IBMPACK) {
        /* A signature is only a scheduling hint. Native chain validation
         * still precedes publication of this handle, including for .001. */
        return xx_io_read_at(dev,0,magic,sizeof(magic)) && magic[0]==0xa5 && magic[1]==0x96 &&
            ((magic[2]==0xfe && magic[3]==0xff) || (magic[2]==0xff && magic[3]==0xff) ||
             (magic[2]==0x14 && magic[3]==0x0a) || (magic[2]==0 && magic[3]==0x14)) ? 1 : 0;
    }
    if (type == XX_FILE_TYPE_RTPATCH_SETUP_DATA) {
        uint8_t h[16]; unsigned n;
        return xx_io_read_at(dev,0,h,sizeof(h)) && (n=(unsigned)h[14]|((unsigned)h[15]<<8U))>=2U && n<=13U &&
            xx_io_read_at(dev,16,h,n+2U) && !h[n-1U] && h[n]==0xb5U && h[n+1U]==0x9cU ? 1 : 0;
    }
    if (type == XX_FILE_TYPE_RNC) {
        uint8_t h[12];
        return xx_io_read_at(dev,0,h,sizeof(h)) &&
            ((h[0]=='R' && h[1]=='N' && h[2]=='C' && (h[3]==1U || h[3]==2U)) ||
             (!xx_rt_memcmp(h,"BULLFROGRNC",11U) && (h[11]==1U || h[11]==2U))) ? 1 : 0;
    }
    if (type == XX_FILE_TYPE_RED) {
        return xx_io_read_at(dev,0,magic,sizeof(magic)) && magic[0]=='R' && magic[1]=='R' &&
            magic[2]==1U && magic[3]>=39U ? 1 : 0;
    }
    if (type == XX_FILE_TYPE_QUANTUM) {
        uint8_t h[8];
        return xx_io_read_at(dev,0,h,sizeof(h)) && h[0]=='D' && h[1]=='S' && !h[2] && h[3] &&
            (h[4]||h[5]) && h[6]>=10U && h[6]<=21U ? 1 : 0;
    }
    if (type == XX_FILE_TYPE_NETWARE2) {
        uint8_t h[20];
        return xx_io_read_at(dev,0,h,sizeof(h)) &&
            !xx_rt_memcmp(h,"\x23\0\0\0\x10" "NetWareFileInfo",sizeof(h)) ? 1 : 0;
    }
    if (type == XX_FILE_TYPE_NETWAREPACKED) {
        uint8_t h[31];
        return xx_io_read_at(dev,0,h,sizeof(h)) && !xx_rt_memcmp(h,"Packed File ",12U) &&
            h[24]==0x1aU && h[25]==1U && h[26]==10U ? 1 : 0;
    }
    if (type == XX_FILE_TYPE_LIFKD) {
        uint8_t h[54];
        /* Schedule the complete native chain check before a .001 suffix can
         * hide this headerless installer format. */
        return xx_io_read_at(dev,0,h,sizeof(h)) && xx_lifkd_is_member_header(h,sizeof(h)) ? 1 : 0;
    }
    if (type == XX_FILE_TYPE_GTU) {
        uint8_t h[16];
        int64_t size = xx_io_total_size(dev);
        /* Headerless GTU kits also use .001. Schedule full native validation
         * before the suffix-only split interpretation can hide this handle. */
        return size >= 16 && (uint64_t)size <= UINT32_MAX && xx_io_read_at(dev,0,h,sizeof(h)) &&
            xx_data_get_u32(h,4,0,false) == (uint64_t)size && h[8] == 1U && !h[9] &&
            h[10] <= 1U && !h[11] && !h[12] && !h[13] && (h[14] || h[15]) ? 1 : 0;
    }
    if (type == XX_FILE_TYPE_PE_RAWFILE || type == XX_FILE_TYPE_PKLITE) {
        uint8_t mz[2];
        return xx_io_read_at(dev,0,mz,sizeof(mz)) && mz[0]=='M' && mz[1]=='Z' ? 1 : 0;
    }
    if (type == XX_FILE_TYPE_FURY_DAT) {
        uint8_t h[24];
        return xx_io_read_at(dev,0,h,sizeof(h)) && (h[0] || h[1]) && h[2]>=0x21U && h[2]<=0x7eU && h[23]<=1U ? 1 : 0;
    }
    if (type == XX_FILE_TYPE_SOD_INSTALLER) {
        uint8_t h[12];unsigned year;
        if(!xx_io_read_at(dev,0,h,sizeof(h)))return 0;
        year=h[0]|((unsigned)h[1]<<8);
        /* Date prefilter only; the reader must validate every named record. */
        return year>=1980U && year<=2100U && h[2]>=1 && h[2]<=12 && !h[3] ? 1 : 0;
    }
    if (type == XX_FILE_TYPE_UPX) return xx_upx_has_marker_device(dev, NULL) ? 1 : 0;
    if (type == XX_FILE_TYPE_SFX_HCI_INSTALIT) {
        uint8_t tail[12], tag[6];
        int64_t size = xx_io_total_size(dev);
        uint32_t end, start;
        if (xx_io_read_at(dev, 0, magic, sizeof(magic)) && magic[0] == 'M' && magic[1] == 'Z') return 1;
        if (size < 35 || !xx_io_read_at(dev, size - 12, tail, sizeof(tail))) return 0;
        end = xx_data_get_u32(tail, 4, 0, false);
        start = xx_data_get_u32(tail + 8, 4, 0, false);
        /* Standalone volumes have no prefix signature. This bounded footer
         * hint asks the native reader to validate the complete catalog. */
        return start > 0U && (int64_t)start + 6 < end && end <= size - 12 &&
               xx_data_get_u32(tail + 4, 4, 0, false) == 0U &&
               xx_io_read_at(dev, start, tag, sizeof(tag)) && !xx_rt_memcmp(tag, "\x05[PVM]", sizeof(tag)) ? 1 : 0;
    }
    if (type == XX_FILE_TYPE_STARFORCE_OVL) {
        int64_t size = xx_io_total_size(dev);
        /* Prefilters only: the reader subsequently authenticates the full
         * directory, payload extents and directory zlib checksum. */
        if (xx_io_read_at(dev, 0, magic, sizeof(magic)) &&
            ((!xx_rt_memcmp(magic, "OVL ", 4U)) || (magic[0] == 'M' && magic[1] == 'Z'))) return 1;
        return size >= 20 && xx_io_read_at(dev, size - 4, magic, sizeof(magic)) && !xx_rt_memcmp(magic, "SOVL", 4U) ? 1 : 0;
    }
    if (type == XX_FILE_TYPE_MOLEBOX) return xx_molebox_has_candidate_device(dev, 0) ? 1 : 0;
    if (type == XX_FILE_TYPE_ENIGMA_VIRTUAL_BOX && xx_io_read_at(dev, 0, magic, sizeof(magic)) &&
        magic[0] == 'E' && magic[1] == 'V' && magic[2] == 'B' && !magic[3]) return 1;
    return extractor->signature ? extractor->signature(dev, 0, false) : xx_format_fast_detect_rules(&type, 1U, dev, 0, false);
#endif
}

/* Less nested views precede their compound forms, independently of catalog
 * order. The unpacker can therefore prefer the final, most specific handle. */
static unsigned xx_handle_priority(xx_handle_type_t handle)
{
    xx_file_type_t type = xx_format_handle_type_file_type(handle);
    switch (type) {
        case XX_FILE_TYPE_PE_RAWFILE: case XX_FILE_TYPE_PKLITE: return 0U;
        case XX_FILE_TYPE_GZ: case XX_FILE_TYPE_BZ2: case XX_FILE_TYPE_XZ:
        case XX_FILE_TYPE_LZIP: case XX_FILE_TYPE_LZMA: case XX_FILE_TYPE_LZ4:
        case XX_FILE_TYPE_ZSTD: case XX_FILE_TYPE_ZIP: return 0U;
        case XX_FILE_TYPE_TAR_GZ: case XX_FILE_TYPE_TAR_BZ2: case XX_FILE_TYPE_TAR_XZ:
        case XX_FILE_TYPE_TAR_LZ4: case XX_FILE_TYPE_TAR_ZSTD: case XX_FILE_TYPE_TAR_COMPRESS:
        case XX_FILE_TYPE_TAR_LZIP: case XX_FILE_TYPE_TAR_LZMA: case XX_FILE_TYPE_TAR_LZOP: return 2U;
        case XX_FILE_TYPE_STARFORCE_OVL: case XX_FILE_TYPE_STUFFIT5: case XX_FILE_TYPE_TARMA_INSTALLER: case XX_FILE_TYPE_SOD_INSTALLER: return 2U;
          /* A validated PFX chain exposes decoded members inside the Atari
           * executable, while GEMDOS_LHA exposes its compressed LHA payload. */
          case XX_FILE_TYPE_LARC_PFX: return 2U;
          case XX_FILE_TYPE_NPM: return 3U;
        default: return 1U;
    }
}

static void xx_handle_order(xx_list_t *types)
{
    size_t i;
    for (i = 1U; i < xx_list_count(types); ++i) {
        size_t j = i;
        xx_handle_type_t value = *(xx_handle_type_t *)xx_list_at(types, i);
        while (j && xx_handle_priority(*(xx_handle_type_t *)xx_list_at(types, j - 1U)) > xx_handle_priority(value)) {
            *(xx_handle_type_t *)xx_list_at(types, j) = *(xx_handle_type_t *)xx_list_at(types, j - 1U);
            --j;
        }
        *(xx_handle_type_t *)xx_list_at(types, j) = value;
    }
}

xx_list_t *xx_format_get_unpack_handle_types_device(xx_io_device *dev, bool deep)
{
    xx_list_t *types;
    int64_t saved;
    bool executable;
    size_t i;
    if (!dev || (saved = xx_io_tell(dev)) < 0) return NULL;
    types = xx_list_create(sizeof(xx_handle_type_t), NULL);
    if (!types) return NULL;
    executable = xx_handle_is_executable(dev);

    /* Fixed signatures first. All executable installer/SFX readers are tried
     * for a recognized carrier, even when one other reader already matched. */
    for (i = 0; i < sizeof(handle_rows) / sizeof(handle_rows[0]); ++i) {
        const xx_unpack_handle_row *row = &handle_rows[i];
        int signature;
        if (row->carrier || !xx_handle_auto_candidate(row->file_type)) continue;

        signature = xx_handle_signature(row->file_type, dev);
        if (signature != 1 && (!executable || !xx_handle_has_carrier_reader(row->file_type))) continue;
        if (xx_io_seek64(dev, saved, SEEK_SET) != 0 || !xx_handle_probe(dev, row->file_type, types)) goto failed;
    }

    /* A suffix is considered only if content yielded no unpacking handle.
     * A suffix remains a hint: its native reader must accept the input. */
    if (!xx_list_count(types)) {
        const char *path = xx_io_source_path(dev);
        xx_file_type_t hint = xx_format_get_file_type_extension(path);
        /* COM images have no invariant initial opcode. Once magic found
         * nothing, the source suffix justifies asking every known carrier
         * reader to validate its own COM-compatible stub and archive. */
        if (xx_handle_extension_matches(path, "com")) {
            for (i = 0; i < sizeof(handle_rows) / sizeof(handle_rows[0]); ++i) {
                const xx_unpack_handle_row *row = &handle_rows[i];
                if (row->carrier || !xx_handle_auto_candidate(row->file_type) || !xx_handle_has_carrier_reader(row->file_type)) continue;
                if (!xx_handle_probe(dev, row->file_type, types)) goto failed;
            }
        }
        /* Numbered raw volumes use the unpacker's multi-file backend, rather
         * than a single native format factory. A nonempty opened .001 file
         * is sufficient for its extension-based interpretation. */
#if XFU_FORMAT_SUPPORT_FULL
        if (hint == XX_FILE_TYPE_NUMBERED_SPLIT && xx_io_total_size(dev) > 0) {
            if (!xx_handle_append(types, XX_HANDLE_TYPE_NUMBERED_SPLIT)) goto failed;
        } else
#endif
        if (hint != XX_FILE_TYPE_UNKNOWN && !xx_handle_probe(dev, hint, types)) goto failed;
        /* The canonical hint map deliberately omits ambiguous extensions.
         * Try every compiled reader declaring that suffix and keep every
         * validated interpretation, including compound suffixes. */
        if (path) for (i = 0; i < sizeof(handle_rows) / sizeof(handle_rows[0]); ++i) {
            const xx_unpack_handle_row *row = &handle_rows[i];
            Abstractformat *format;
            bool ok = true;
            if (row->carrier || row->file_type == hint || row->file_type == XX_FILE_TYPE_EXECUTABLE_OVERLAY) continue;
            format = xx_format_create_reader(row->file_type, dev, 0);
            if (xx_handle_can_unpack(format) && xx_handle_extension_matches(path, xx_format_get_extension(format)))
                ok = xx_handle_probe_format(format, row->file_type, types);
            xx_format_free_reader(format);
            if (!ok) goto failed;
        }
    }

    if (deep && !xx_list_count(types)) {
        /* Specific SFX readers have already had their chance. Authenticate
         * adjacent executable boundaries before weaker structural readers. */
        if (executable && !xx_handle_probe(dev, XX_FILE_TYPE_EXECUTABLE_OVERLAY, types)) goto failed;
        if (!xx_list_count(types)) for (i = 0; i < sizeof(handle_rows) / sizeof(handle_rows[0]); ++i) {
            const xx_unpack_handle_row *row = &handle_rows[i];
            /* Signature rules and carving anchors are not exhaustive reader
             * grammars: V7 TAR has no ustar marker, and ZIP can authenticate
             * its directory after leading bytes. Once shallow recognition
             * fails, let every eligible native reader validate structure. */
            if (row->carrier || !xx_handle_auto_candidate(row->file_type)) continue;
            if (xx_io_seek64(dev, saved, SEEK_SET) != 0 || !xx_handle_probe(dev, row->file_type, types)) goto failed;
        }
    }
    xx_handle_order(types);
    if (xx_io_seek64(dev, saved, SEEK_SET) != 0) goto failed;
    return types;
failed:
    (void)xx_io_seek64(dev, saved, SEEK_SET);
    xx_list_destroy(types);
    return NULL;
}

xx_list_t *xx_format_get_supported_unpack_handle_types(void)
{
    xx_list_t *types = xx_list_create(sizeof(xx_handle_type_t), NULL);
    xx_io_device *empty;
    uint8_t dummy = 0;
    size_t i;
    if (!types) return NULL;
    empty = xx_io_mem_open_ro(&dummy, 0U);
    if (!empty) { xx_list_destroy(types); return NULL; }
#if XFU_FORMAT_SUPPORT_FULL
    if (!xx_handle_append(types, XX_HANDLE_TYPE_NUMBERED_SPLIT)) goto failed;
#endif
    for (i = 0; i < sizeof(handle_rows) / sizeof(handle_rows[0]); ++i) {
        const xx_unpack_handle_row *row = &handle_rows[i];
        Abstractformat *format;
        bool supported;
        if (row->carrier) continue;
        format = xx_format_create_reader(row->file_type, empty, 0);
        supported = xx_handle_can_unpack(format);
        xx_format_free_reader(format);
        if (supported) {
            size_t j;
            if (!xx_handle_append(types, row->handle)) goto failed;
            for (j = 0; j < sizeof(handle_rows) / sizeof(handle_rows[0]); ++j)
                if (handle_rows[j].carrier && handle_rows[j].file_type == row->file_type && !xx_handle_append(types, handle_rows[j].handle)) goto failed;
        }
    }
    xx_io_close(empty);
    return types;
failed:
    xx_io_close(empty);
    xx_list_destroy(types);
    return NULL;
}

Abstractformat *xx_format_create_handle_reader(xx_handle_type_t type, xx_io_device *dev, int64_t base_address)
{
    xx_file_type_t file_type = xx_format_handle_type_file_type(type);
    Abstractformat *format;
    if (file_type == XX_FILE_TYPE_UNKNOWN) return NULL;
    format = xx_format_create_reader(file_type, dev, base_address);
    if (!xx_handle_can_unpack(format)) { xx_format_free_reader(format); return NULL; }
    xx_handle_prepare_reader(format, file_type);
    format->handle_type = type;
    return format;
}
