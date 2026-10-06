/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * VMware hosted sparse and streamOptimized extents. On-disk specification:
 * https://github.com/vmware/open-vmdk/blob/master/vmdk_50_technote.pdf
 * VMware's producer uses zlib-wrapped RFC 1951 streams for compressed grains:
 * https://github.com/vmware/open-vmdk/blob/master/vmdk/sparse.c
 * This reader uses xxfclib's native Deflate implementation, without zlib.
 */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/vmdk/xx_vmdk.h"
#include "xxfclib/formats/fat/xx_fat.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/global/xx_global.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include <stdio.h>
#include <string.h>
#include <limits.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#endif

#ifdef VMDK
#define XX_VMDK_FILE_TYPE XX_FILE_TYPE_VMDK
#else
#define XX_VMDK_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif
#define VMDK_SECTOR UINT64_C(512)
#define VMDK_FLAG_NEWLINE UINT32_C(1)
#define VMDK_FLAG_REDUNDANT UINT32_C(2)
#define VMDK_FLAG_ZERO UINT32_C(4)
#define VMDK_FLAG_COMPRESSED UINT32_C(0x10000)
#define VMDK_FLAG_MARKERS UINT32_C(0x20000)
#define VMDK_MAX_GD_ENTRIES (UINT64_C(1) << 22U)
#define VMDK_MAX_GTES (UINT32_C(1) << 20U)
#define VMDK_MAX_GRAIN_SECTORS (UINT64_C(1) << 16U)
#define VMDK_MAX_GRAINS (UINT64_C(1) << 24U)
#define VMDK_MAX_DESCRIPTOR (UINT64_C(1) << 20U)
#define VMDK_TRANSFER_SIZE 65536U
#define VMDK_MAX_EXTENTS 256U
#define VMDK_MAX_EXTERNAL_SECTORS (UINT64_C(1) << 31U)
#define VMDK_SPLIT_SECTORS UINT64_C(4194304)

typedef struct vmdk_stream {
    uint64_t available, capacity, grain_bytes, grains, entries;
    uint64_t gd_offset, descriptor_end, data_start, data_end;
    uint32_t flags, gtes, version;
    bool compressed, footer, standalone, external;
} vmdk_stream;

typedef struct vmdk_extent_spec {
    char name[256];
    uint64_t sectors, offset_sectors;
    bool sparse;
} vmdk_extent_spec;

typedef struct vmdk_external_layout {
    vmdk_extent_spec extents[VMDK_MAX_EXTENTS];
    uint64_t sectors, available;
    unsigned count, kind;
} vmdk_external_layout;

typedef struct xx_vmdk_external_state {
    vmdk_external_layout *layout;
    xx_io_device *sources[VMDK_MAX_EXTENTS];
    xx_vmdk *sparse[VMDK_MAX_EXTENTS];
    xx_io_device *disks[VMDK_MAX_EXTENTS];
} vmdk_external_state;

static xx_io_device *vmdk_external_disk_open(xx_vmdk *archive,
                                            const xx_list_s *options,
                                            xx_pd_struct *pd);
static bool vmdk_external_extract(Abstractformat *f, const vmdk_stream *s,
                                  const xx_list_s *options,
                                  xx_io_device *destination, xx_pd_struct *pd);
static void vmdk_external_release(vmdk_external_state *state);

/* A read-only, seekable view of the reconstructed logical sectors.  The
 * source device is borrowed; only the grain directory, one table, and one
 * inflated grain are retained. */
typedef struct vmdk_disk {
    xx_io_device device;
    Abstractformat *source;
    vmdk_stream layout;
    uint8_t *directory, *table, *packed, *plain;
    size_t packed_capacity;
    uint64_t table_sector, table_at, table_end, grain;
    uint64_t position, memory_limit;
    unsigned fat_partition;
    bool table_loaded, grain_loaded;
} vmdk_disk;

static uint16_t vmdk_le16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8U));
}
static uint32_t vmdk_le32(const uint8_t *p) {
    return (uint32_t)vmdk_le16(p) | ((uint32_t)vmdk_le16(p + 2U) << 16U);
}
static uint64_t vmdk_le64(const uint8_t *p) {
    return (uint64_t)vmdk_le32(p) | ((uint64_t)vmdk_le32(p + 4U) << 32U);
}
static bool vmdk_stopped(xx_pd_struct *pd) { return pd && xx_pd_is_stopped(pd); }
static bool vmdk_span(uint64_t at, uint64_t size, uint64_t total) {
    return at <= total && size <= total - at;
}
static uint64_t vmdk_round_sector(uint64_t size) {
    return (size + VMDK_SECTOR - 1U) & ~(VMDK_SECTOR - 1U);
}
static bool vmdk_error(xx_pd_struct *pd, const char *message) {
    xx_pd_set_error(pd, XXFC_ERR_INVALID_ARG, message); return false;
}
static bool vmdk_memory_limit(Abstractformat *f, const xx_list_s *options, uint64_t size) {
    const xx_var *v = xx_format_resolve_extra_parameter(f, options, XX_META_ID_OPT_MEMORY_LIMIT);
    return !v || size <= xx_var_get_u64(v);
}
static bool vmdk_read(Abstractformat *f, const vmdk_stream *s, uint64_t at,
                       void *buffer, size_t size, xx_pd_struct *pd) {
    size_t done = 0U;
    if (!f || !f->device || (!buffer && size) || vmdk_stopped(pd) ||
        !vmdk_span(at, size, s->available) ||
        xx_io_seek64(f->device, f->base_address + (int64_t)at, SEEK_SET)) return false;
    while (done < size) {
        size_t want = size - done;
        ssize_t got;
        if (vmdk_stopped(pd)) return false;
        got = xx_io_read(f->device, (uint8_t *)buffer + done, want);
        if (vmdk_stopped(pd) || got <= 0 || (size_t)got > want) return false;
        done += (size_t)got;
    }
    return true;
}
static bool vmdk_write(xx_io_device *d, const void *buffer, size_t size, xx_pd_struct *pd) {
    size_t done = 0U;
    if (vmdk_stopped(pd)) return false;
    if (!d) return true;
    while (done < size) {
        ssize_t got;
        size_t want = size - done;
        if (want > VMDK_TRANSFER_SIZE) want = VMDK_TRANSFER_SIZE;
        if (vmdk_stopped(pd)) return false;
        got = xx_io_write(d, (const uint8_t *)buffer + done, want);
        if (vmdk_stopped(pd) || got <= 0 || (size_t)got > want) return false;
        done += (size_t)got;
    }
    return !vmdk_stopped(pd);
}

/* Parse exact descriptor keys, rather than finding names inside comments or
 * filenames. Only one hosted SPARSE extent is reconstructed here. */
static bool vmdk_equal(const char *at, size_t length, const char *word) {
    return xx_str_len(word) == length && !xx_rt_memcmp(at, word, length);
}
static bool vmdk_space(char c) { return c == ' ' || c == '\t' || c == '\r'; }
static bool vmdk_decimal(const char **position, const char *end, uint64_t *value) {
    const char *at = *position;
    uint64_t number = 0U;
    if (at == end || *at < '0' || *at > '9') return false;
    while (at != end && *at >= '0' && *at <= '9') {
        unsigned digit = (unsigned)(*at++ - '0');
        if (number > (UINT64_MAX - digit) / 10U) return false;
        number = number * 10U + digit;
    }
    *position = at; *value = number; return true;
}

static bool vmdk_safe_sidecar(const char *name) {
    static const char *const reserved[] = {
        "CON", "PRN", "AUX", "NUL", "CONIN$", "CONOUT$", "CLOCK$"
    };
    size_t i, n, stem; char word[16];
    if (!name) return false;
    n = strlen(name);
    if (!n || n > 255U || name[n - 1U] == '.' || name[n - 1U] == ' ') return false;
    for (i = 0U; i < n; ++i) {
        unsigned char c = (unsigned char)name[i];
        if (c < 0x20U || c == 0x7FU || c == '/' || c == '\\' || c == ':' ||
            c == '<' || c == '>' || c == '"' || c == '|' || c == '?' || c == '*') return false;
    }
    stem = 0U; while (stem < n && name[stem] != '.') ++stem;
    while (stem && name[stem - 1U] == ' ') --stem;
    if (!stem || stem >= sizeof(word)) return stem != 0U;
    for (i = 0U; i < stem; ++i) {
        char c = name[i];
        word[i] = c >= 'a' && c <= 'z' ? (char)(c - ('a' - 'A')) : c;
    }
    word[stem] = 0;
    for (i = 0U; i < sizeof(reserved) / sizeof(reserved[0]); ++i)
        if (!strcmp(word, reserved[i])) return false;
    if (stem == 4U && ((!strncmp(word, "COM", 3U) || !strncmp(word, "LPT", 3U)) &&
                       word[3] >= '1' && word[3] <= '9')) return false;
    return true;
}

/* The descriptor and sidecars share the same parent path. Reject a symlink
 * or reparse point at the sidecar leaf, so a safe basename cannot redirect
 * I/O outside the descriptor's (possibly itself symlinked) directory. */
static bool vmdk_regular_sidecar(const char *path) {
#ifdef _WIN32
    wchar_t *wide = xx_str_utf8_to_unicode(path);
    DWORD attributes;
    if (!wide) return false;
    attributes = GetFileAttributesW(wide);
    xx_str_free_unicode(wide);
    return attributes != INVALID_FILE_ATTRIBUTES &&
           !(attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT));
#else
    struct stat info;
    return lstat(path, &info) == 0 && S_ISREG(info.st_mode);
#endif
}

/* QEMU's external descriptors are small text files. Keep the descriptor
 * independent of the sidecars so check_is_valid and listing work before the
 * CLI knows the source pathname. Unknown parent metadata is refused. */
static vmdk_external_layout *vmdk_external_parse(Abstractformat *f,
                            const xx_list_s *options, xx_pd_struct *pd) {
    vmdk_stream source;
    vmdk_external_layout *layout = NULL;
    char *text = NULL;
    uint64_t size;
    size_t at = 0U, used;
    unsigned versions = 0U, cids = 0U, parents = 0U, creates = 0U;
    bool valid = false;
    int64_t total;
    if (!f || !f->device || f->base_address < 0 || vmdk_stopped(pd)) return NULL;
    total = xx_io_total_size(f->device);
    if (total < f->base_address) return NULL;
    size = (uint64_t)(total - f->base_address);
    if (size < 128U || size > VMDK_MAX_DESCRIPTOR ||
        !vmdk_memory_limit(f, options, sizeof(*layout) + size + 1U)) return NULL;
    layout = (vmdk_external_layout *)xx_mem_calloc(1U, sizeof(*layout));
    text = (char *)xx_mem_alloc((size_t)size + 1U);
    xx_mem_zero(&source, sizeof(source)); source.available = size;
    if (!layout || !text || !vmdk_read(f, &source, 0U, text, (size_t)size, pd) ||
        memcmp(text, "# Disk DescriptorFile", 21U)) goto done;
    layout->available = size;
    text[size] = 0;
    used = 0U; while (used < size && text[used]) ++used;
    for (at = used; at < size; ++at) if (text[at]) goto done;
    at = 0U;
    while (at < used) {
        const char *start = text + at, *end, *key, *key_end, *value;
        while (at < used && text[at] != '\n') ++at;
        end = text + at;
        if (at < used) ++at;
        while (start < end && vmdk_space(*start)) ++start;
        while (end > start && vmdk_space(end[-1])) --end;
        if (start == end || *start == '#') continue;
        key = start;
        while (start < end && !vmdk_space(*start) && *start != '=') ++start;
        key_end = start;
        if (vmdk_equal(key, (size_t)(key_end - key), "RW")) {
            vmdk_extent_spec *extent;
            const char *name;
            size_t length;
            if (layout->count == VMDK_MAX_EXTENTS) goto done;
            extent = &layout->extents[layout->count++];
            while (start < end && vmdk_space(*start)) ++start;
            if (!vmdk_decimal(&start, end, &extent->sectors) || !extent->sectors ||
                extent->sectors > VMDK_SPLIT_SECTORS || start == end ||
                !vmdk_space(*start)) goto done;
            while (start < end && vmdk_space(*start)) ++start;
            key = start;
            while (start < end && !vmdk_space(*start)) ++start;
            if (vmdk_equal(key, (size_t)(start - key), "SPARSE")) extent->sparse = true;
            else if (!vmdk_equal(key, (size_t)(start - key), "FLAT")) goto done;
            while (start < end && vmdk_space(*start)) ++start;
            if (start == end || *start++ != '"') goto done;
            name = start;
            while (start < end && *start != '"') ++start;
            length = (size_t)(start - name);
            if (start == end || !length || length > 255U) goto done;
            memcpy(extent->name, name, length); extent->name[length] = 0;
            if (!vmdk_safe_sidecar(extent->name)) goto done;
            ++start;
            while (start < end && vmdk_space(*start)) ++start;
            if (!extent->sparse) {
                if (!vmdk_decimal(&start, end, &extent->offset_sectors) ||
                    extent->offset_sectors > VMDK_MAX_EXTERNAL_SECTORS - extent->sectors) goto done;
                while (start < end && vmdk_space(*start)) ++start;
            }
            if (start != end || layout->sectors > VMDK_MAX_EXTERNAL_SECTORS - extent->sectors) goto done;
            layout->sectors += extent->sectors;
            continue;
        }
        value = start;
        while (value < end && vmdk_space(*value)) ++value;
        if (value == end || *value++ != '=') goto done;
        while (value < end && vmdk_space(*value)) ++value;
        if (vmdk_equal(key, (size_t)(key_end - key), "version")) {
            if (++versions != 1U || !vmdk_equal(value, (size_t)(end - value), "1")) goto done;
        } else if (vmdk_equal(key, (size_t)(key_end - key), "CID")) {
            size_t i;
            if (++cids != 1U || end - value != 8) goto done;
            for (i = 0U; i < 8U; ++i)
                if (!((value[i] >= '0' && value[i] <= '9') ||
                      (value[i] >= 'a' && value[i] <= 'f') ||
                      (value[i] >= 'A' && value[i] <= 'F'))) goto done;
        } else if (vmdk_equal(key, (size_t)(key_end - key), "parentCID")) {
            size_t i;
            if (++parents != 1U || end - value != 8) goto done;
            for (i = 0U; i < 8U; ++i)
                if (value[i] != 'f' && value[i] != 'F') goto done;
        } else if (vmdk_equal(key, (size_t)(key_end - key), "createType")) {
            if (++creates != 1U) goto done;
            if (vmdk_equal(value, (size_t)(end - value), "\"monolithicFlat\"")) layout->kind = 1U;
            else if (vmdk_equal(value, (size_t)(end - value), "\"twoGbMaxExtentFlat\"")) layout->kind = 2U;
            else if (vmdk_equal(value, (size_t)(end - value), "\"twoGbMaxExtentSparse\"")) layout->kind = 3U;
            else goto done;
        } else if (!((size_t)(key_end - key) >= 4U &&
                     !memcmp(key, "ddb.", 4U))) goto done;
    }
    if (versions != 1U || cids != 1U || parents != 1U || creates != 1U ||
        !layout->count || !layout->sectors) goto done;
    if (layout->kind == 1U && layout->count != 1U) goto done;
    for (at = 0U; at < layout->count; ++at) {
        const vmdk_extent_spec *extent = &layout->extents[at];
        if (extent->sparse != (layout->kind == 3U) ||
            (layout->kind != 1U && at + 1U < layout->count &&
             extent->sectors != VMDK_SPLIT_SECTORS)) goto done;
    }
    valid = true;
done:
    xx_mem_free(text);
    if (!valid) { xx_mem_free(layout); layout = NULL; }
    return layout;
}
static bool vmdk_descriptor(Abstractformat *f, vmdk_stream *s, const uint8_t *header,
                             const xx_list_s *options, xx_pd_struct *pd) {
    uint64_t sector = vmdk_le64(header + 28U), sectors = vmdk_le64(header + 36U);
    char *text = NULL;
    size_t used, at = 0U;
    unsigned parent_count = 0U, extent_count = 0U, create_count = 0U;
    bool valid = false;
    s->descriptor_end = VMDK_SECTOR;
    if (!sector) return true; /* Parent status remains unknown. */
    if (!sectors || sector > s->available / VMDK_SECTOR ||
        sectors > VMDK_MAX_DESCRIPTOR / VMDK_SECTOR ||
        !vmdk_span(sector * VMDK_SECTOR, sectors * VMDK_SECTOR, s->available) ||
        !vmdk_memory_limit(f, options, sizeof(*s) + sectors * VMDK_SECTOR + 1U)) return false;
    used = (size_t)(sectors * VMDK_SECTOR); text = (char *)xx_mem_alloc(used + 1U);
    if (!text || !vmdk_read(f, s, sector * VMDK_SECTOR, text, used, pd)) goto done;
    text[used] = 0; s->descriptor_end = (sector + sectors) * VMDK_SECTOR;
    if (((xx_vmdk *)f)->parentless_extent) {
        size_t i;
        for (i = 0U; i < used && !text[i]; ++i) { }
        if (i == used) { s->standalone = true; valid = true; goto done; }
    }
    while (at < used && text[at]) {
        const char *start = text + at, *end, *key, *key_end, *value;
        while (at < used && text[at] && text[at] != '\n') ++at;
        end = text + at;
        if (at < used && text[at] == '\n') ++at;
        while (start < end && vmdk_space(*start)) ++start;
        while (end > start && vmdk_space(end[-1])) --end;
        if (start == end || *start == '#') continue;
        key = start;
        while (start < end && !vmdk_space(*start) && *start != '=') ++start;
        key_end = start;
        if (vmdk_equal(key, (size_t)(key_end - key), "RW") ||
            vmdk_equal(key, (size_t)(key_end - key), "RDONLY") ||
            vmdk_equal(key, (size_t)(key_end - key), "NOACCESS")) {
            uint64_t capacity;
            if (++extent_count != 1U || !vmdk_equal(key, (size_t)(key_end - key), "RW")) goto done;
            while (start < end && vmdk_space(*start)) ++start;
            if (!vmdk_decimal(&start, end, &capacity) || capacity != s->capacity / VMDK_SECTOR ||
                start == end || !vmdk_space(*start)) goto done;
            while (start < end && vmdk_space(*start)) ++start;
            key = start;
            while (start < end && !vmdk_space(*start)) ++start;
            if (!vmdk_equal(key, (size_t)(start - key), "SPARSE")) goto done;
            while (start < end && vmdk_space(*start)) ++start;
            if (start == end || *start++ != '"') goto done;
            value = start;
            while (start < end && *start != '"') ++start;
            if (start == value || start == end) goto done;
            ++start;
            while (start < end && vmdk_space(*start)) ++start;
            if (start != end && *start != '#') goto done;
            continue;
        }
        value = start;
        while (value < end && vmdk_space(*value)) ++value;
        if (value == end || *value++ != '=') goto done;
        while (value < end && vmdk_space(*value)) ++value;
        if (vmdk_equal(key, (size_t)(key_end - key), "parentCID")) {
            unsigned i;
            if (++parent_count != 1U || end - value != 8) goto done;
            for (i = 0U; i < 8U; ++i) if (value[i] != 'f' && value[i] != 'F') {
                (void)vmdk_error(pd, "VMDK parent disk is required; parent chains are unsupported"); goto done;
            }
        } else if (vmdk_equal(key, (size_t)(key_end - key), "parentFileNameHint")) {
            if (end - value != 2 || value[0] != '"' || value[1] != '"') {
                (void)vmdk_error(pd, "VMDK parent disk is required; parent chains are unsupported"); goto done;
            }
        } else if (vmdk_equal(key, (size_t)(key_end - key), "createType")) {
            if (++create_count != 1U ||
                !(vmdk_equal(value, (size_t)(end - value), "\"monolithicSparse\"") ||
                  vmdk_equal(value, (size_t)(end - value), "\"streamOptimized\""))) goto done;
        }
    }
    /* Padding cannot hide a second descriptor after its first NUL. */
    while (at < used) if (text[at++]) goto done;
    if (!extent_count || !parent_count) goto done;
    s->standalone = true; valid = true;
done:
    xx_mem_free(text); return valid;
}

static bool vmdk_metadata_marker(Abstractformat *f, const vmdk_stream *s,
                                  uint64_t offset, uint64_t bytes, uint32_t type,
                                  xx_pd_struct *pd) {
    uint8_t marker[16];
    return offset >= VMDK_SECTOR &&
           vmdk_read(f, s, offset - VMDK_SECTOR, marker, sizeof(marker), pd) &&
           vmdk_le64(marker) == vmdk_round_sector(bytes) / VMDK_SECTOR &&
           !vmdk_le32(marker + 8U) && vmdk_le32(marker + 12U) == type;
}

typedef struct vmdk_inflate_output {
    xx_io_device device;
    uint8_t *buffer;
    size_t capacity, written;
} vmdk_inflate_output;
static ssize_t vmdk_inflate_write(xx_io_device *d, const void *data, size_t size) {
    vmdk_inflate_output *out = (vmdk_inflate_output *)d;
    if (size > out->capacity - out->written) return -1;
    xx_mem_copy(out->buffer + out->written, data, size); out->written += size;
    return (ssize_t)size;
}
static bool vmdk_inflate(const uint8_t *compressed, size_t size, uint8_t *plain,
                          size_t grain_bytes, size_t logical_bytes, size_t *written,
                          xx_pd_struct *pd) {
    vmdk_inflate_output output;
    size_t consumed = 0U;
    xx_mem_zero(&output, sizeof(output)); output.device.write = vmdk_inflate_write;
    output.buffer = plain; output.capacity = grain_bytes;
    if (size < 7U || !xx_zlib_stream_header_is_valid(compressed, size) ||
        !xx_deflate_unpack_memory_to_device_ex(compressed + 2U, size - 6U,
            &output.device, &consumed, false, pd) || consumed != size - 6U ||
        (output.written != grain_bytes && output.written != logical_bytes) ||
        !xx_zlib_stream_trailer_matches(compressed, size, plain, output.written) ||
        vmdk_stopped(pd)) return false;
    *written = output.written; return true;
}

/* Validate every indexed grain. Extraction reads the actual bytes even in a
 * verify-only pass. Descriptorless holes are listable but never guessed. */
static bool vmdk_walk(Abstractformat *f, const vmdk_stream *s,
                       const xx_list_s *options, xx_io_device *destination,
                       bool extract, xx_pd_struct *pd) {
    uint8_t *directory = NULL, *table = NULL, *transfer = NULL;
    uint8_t *compressed = NULL, *plain = NULL;
    uint64_t base_memory = sizeof(*s) + s->entries * 4U + (uint64_t)s->gtes * 4U;
    uint64_t cached = UINT64_MAX, table_at = 0U, table_end = 0U, produced = 0U, grain;
    size_t compressed_capacity = 0U;
    bool valid = false;
    int level = -1;
    if (!vmdk_memory_limit(f, options, base_memory) ||
        s->entries > SIZE_MAX / 4U || (uint64_t)s->gtes * 4U > SIZE_MAX) return false;
    directory = (uint8_t *)xx_mem_alloc((size_t)s->entries * 4U);
    table = (uint8_t *)xx_mem_alloc((size_t)s->gtes * 4U);
    if (!directory || !table ||
        !vmdk_read(f, s, s->gd_offset, directory, (size_t)s->entries * 4U, pd)) goto done;
    if (extract) {
        const xx_var *limit = xx_format_resolve_extra_parameter(f, options, XX_META_ID_OPT_MAX_MEMBER_SIZE);
        if (limit && s->capacity > xx_var_get_u64(limit)) goto done;
        if (!vmdk_memory_limit(f, options, base_memory + VMDK_TRANSFER_SIZE)) goto done;
        transfer = (uint8_t *)xx_mem_alloc(VMDK_TRANSFER_SIZE);
        if (!transfer) goto done;
        level = xx_pd_enter_level(pd, s->capacity, "Reconstructing VMDK disk");
    }
    for (grain = 0U; grain < s->grains; ++grain) {
        uint32_t table_sector = vmdk_le32(directory + (size_t)(grain / s->gtes) * 4U);
        uint32_t data_sector = 0U;
        uint64_t output = s->capacity - produced;
        if (output > s->grain_bytes) output = s->grain_bytes;
        if (vmdk_stopped(pd)) goto done;
        if (table_sector) {
            if (cached != table_sector) {
                uint64_t bytes = (uint64_t)s->gtes * 4U;
                table_at = (uint64_t)table_sector * VMDK_SECTOR; table_end = table_at + bytes;
                if (table_at < s->descriptor_end || !vmdk_span(table_at, bytes, s->data_end)) goto done;
                if (s->footer && (table_at < s->data_start + VMDK_SECTOR ||
                    !vmdk_span(table_at, vmdk_round_sector(bytes), s->gd_offset - VMDK_SECTOR) ||
                    !vmdk_metadata_marker(f, s, table_at, bytes, 1U, pd))) goto done;
                if (!vmdk_read(f, s, table_at, table, (size_t)bytes, pd)) goto done;
                cached = table_sector;
            }
            data_sector = vmdk_le32(table + (size_t)(grain % s->gtes) * 4U);
        }
        if (data_sector == 1U && !(s->flags & VMDK_FLAG_ZERO)) goto done;
        if (!data_sector || data_sector == 1U) {
            if (extract) {
                uint64_t left = output;
                if (!data_sector && !s->standalone) {
                    (void)vmdk_error(pd, "VMDK unallocated grain has no proven standalone descriptor; a parent may be required"); goto done;
                }
                if (destination) {
                    xx_mem_zero(transfer, VMDK_TRANSFER_SIZE);
                    while (left) {
                        size_t n = left < VMDK_TRANSFER_SIZE ? (size_t)left : VMDK_TRANSFER_SIZE;
                        if (!vmdk_write(destination, transfer, n, pd)) goto done;
                        left -= n;
                    }
                }
            }
        } else {
            uint64_t data_at = (uint64_t)data_sector * VMDK_SECTOR;
            uint64_t data_limit = s->footer ? table_at - VMDK_SECTOR : s->data_end;
            if (data_at < s->data_start || (!s->footer &&
                (data_at < table_end || data_at < s->gd_offset + s->entries * 4U))) goto done;
            if (s->compressed) {
                uint8_t marker[14];
                uint32_t length;
                uint64_t maximum = s->grain_bytes * 2U + 64U;
                if (!vmdk_read(f, s, data_at, marker, sizeof(marker), pd) ||
                    vmdk_le64(marker) != grain * (s->grain_bytes / VMDK_SECTOR)) goto done;
                length = vmdk_le32(marker + 8U);
                if (length < 7U || length > maximum ||
                    !vmdk_span(data_at, 12U + (uint64_t)length, data_limit) ||
                    (s->footer && !vmdk_span(data_at, vmdk_round_sector(12U + (uint64_t)length), data_limit)) ||
                    !xx_zlib_stream_header_is_valid(marker + 12U, 2U)) goto done;
                if (extract) {
                    size_t written;
                    size_t inflate_buffer = xx_get_file_buffer_size();
                    size_t retained = compressed_capacity > length ? compressed_capacity : length;
                    uint64_t budget = base_memory + VMDK_TRANSFER_SIZE + s->grain_bytes +
                                      (uint64_t)retained + 32768U;
                    budget += inflate_buffer ? inflate_buffer : XX_DEFAULT_FILE_BUFFER_SIZE;
                    if (!vmdk_memory_limit(f, options, budget) || s->grain_bytes > SIZE_MAX) goto done;
                    if (!plain) plain = (uint8_t *)xx_mem_alloc((size_t)s->grain_bytes);
                    if (compressed_capacity < length) {
                        xx_mem_free(compressed); compressed = NULL; compressed_capacity = 0U;
                        compressed = (uint8_t *)xx_mem_alloc(length);
                        if (compressed) compressed_capacity = length;
                    }
                    if (!plain || !compressed ||
                        !vmdk_read(f, s, data_at + 12U, compressed, length, pd) ||
                        !vmdk_inflate(compressed, length, plain, (size_t)s->grain_bytes,
                            (size_t)output, &written, pd) ||
                        !vmdk_write(destination, plain, (size_t)output, pd)) goto done;
                }
            } else {
                uint64_t left = output, at = data_at;
                if (!vmdk_span(data_at, output, data_limit)) goto done;
                while (extract && left) {
                    size_t n = left < VMDK_TRANSFER_SIZE ? (size_t)left : VMDK_TRANSFER_SIZE;
                    if (!vmdk_read(f, s, at, transfer, n, pd) || !vmdk_write(destination, transfer, n, pd)) goto done;
                    left -= n; at += n;
                }
            }
        }
        produced += output;
        if (extract) xx_pd_set_current(pd, level, produced);
    }
    valid = produced == s->capacity && !vmdk_stopped(pd);
done:
    if (extract) xx_pd_leave_level(pd, level);
    xx_mem_free(directory); xx_mem_free(table); xx_mem_free(transfer);
    xx_mem_free(compressed); xx_mem_free(plain); return valid;
}

static vmdk_stream *vmdk_parse(Abstractformat *f, const xx_list_s *options, xx_pd_struct *pd) {
    uint8_t header[512];
    vmdk_stream *s = NULL;
    int64_t total;
    uint64_t gd_sector, grain_sectors, overhead;
    uint16_t algorithm;
    if (!f || !f->device || f->base_address < 0 || vmdk_stopped(pd)) return NULL;
    total = xx_io_total_size(f->device);
    if (total < f->base_address || total - f->base_address < 512 ||
        !vmdk_memory_limit(f, options, sizeof(*s))) return NULL;
    s = (vmdk_stream *)xx_mem_calloc(1U, sizeof(*s));
    if (!s) return NULL;
    s->available = (uint64_t)(total - f->base_address); s->data_end = s->available;
    if (!vmdk_read(f, s, 0U, header, sizeof(header), pd)) goto fail;
    if (xx_rt_memcmp(header, "KDMV", 4U)) {
        vmdk_external_layout *layout = vmdk_external_parse(f, options, pd);
        if (!layout) goto fail;
        s->external = true;
        s->capacity = layout->sectors * VMDK_SECTOR;
        xx_mem_free(layout);
        return s;
    }
    gd_sector = vmdk_le64(header + 56U);
    if (gd_sector == UINT64_MAX) {
        uint8_t marker[16], eos[16];
        if (s->available < 2048U || s->available % VMDK_SECTOR ||
            !vmdk_read(f, s, s->available - 1536U, marker, sizeof(marker), pd) ||
            vmdk_le64(marker) != 1U || vmdk_le32(marker + 8U) || vmdk_le32(marker + 12U) != 3U ||
            !vmdk_read(f, s, s->available - 512U, eos, sizeof(eos), pd) ||
            vmdk_le64(eos) || vmdk_le32(eos + 8U) || vmdk_le32(eos + 12U) ||
            !vmdk_read(f, s, s->available - 1024U, header, sizeof(header), pd) ||
            xx_rt_memcmp(header, "KDMV", 4U)) goto fail;
        s->footer = true; s->data_end = s->available - 1536U;
        gd_sector = vmdk_le64(header + 56U); /* Footer is authoritative. */
    }
    s->version = vmdk_le32(header + 4U); s->flags = vmdk_le32(header + 8U);
    algorithm = vmdk_le16(header + 77U); s->compressed = (s->flags & VMDK_FLAG_COMPRESSED) != 0U;
    if (s->version < 1U || s->version > 3U ||
        (s->flags & ~(VMDK_FLAG_NEWLINE | VMDK_FLAG_REDUNDANT | VMDK_FLAG_ZERO |
                      VMDK_FLAG_COMPRESSED | VMDK_FLAG_MARKERS)) ||
        ((s->flags & VMDK_FLAG_NEWLINE) && xx_rt_memcmp(header + 73U, "\n \r\n", 4U)) ||
        (s->compressed ? algorithm != 1U || !(s->flags & VMDK_FLAG_MARKERS) :
            algorithm != 0U || (s->flags & VMDK_FLAG_MARKERS)) ||
        (s->footer && (!s->compressed || (s->flags & VMDK_FLAG_REDUNDANT)))) goto fail;
    s->capacity = vmdk_le64(header + 12U); grain_sectors = vmdk_le64(header + 20U);
    s->gtes = vmdk_le32(header + 44U); overhead = vmdk_le64(header + 64U);
    if (!s->capacity || s->capacity > (UINT64_C(1) << 40U) || !grain_sectors ||
        grain_sectors > VMDK_MAX_GRAIN_SECTORS || (grain_sectors & (grain_sectors - 1U)) ||
        !s->gtes || s->gtes > VMDK_MAX_GTES || !gd_sector ||
        gd_sector > s->data_end / VMDK_SECTOR || overhead > s->data_end / VMDK_SECTOR) goto fail;
    s->grains = (s->capacity + grain_sectors - 1U) / grain_sectors;
    s->entries = (s->grains + s->gtes - 1U) / s->gtes;
    if (s->grains > VMDK_MAX_GRAINS || !s->entries || s->entries > VMDK_MAX_GD_ENTRIES) goto fail;
    s->capacity *= VMDK_SECTOR; s->grain_bytes = grain_sectors * VMDK_SECTOR;
    s->gd_offset = gd_sector * VMDK_SECTOR;
    if (!vmdk_span(s->gd_offset, s->entries * 4U, s->data_end) ||
        !vmdk_descriptor(f, s, header, options, pd) || s->gd_offset < s->descriptor_end) goto fail;
    if (((xx_vmdk *)f)->parentless_extent && !vmdk_le64(header + 28U))
        s->standalone = true;
    s->data_start = overhead * VMDK_SECTOR;
    if (s->data_start < s->descriptor_end) s->data_start = s->descriptor_end;
    if (s->footer && (s->gd_offset < s->data_start + VMDK_SECTOR ||
        s->gd_offset + vmdk_round_sector(s->entries * 4U) != s->data_end ||
        !vmdk_metadata_marker(f, s, s->gd_offset, s->entries * 4U, 2U, pd))) goto fail;
    if (!vmdk_walk(f, s, options, NULL, false, pd)) goto fail;
    return s;
fail:
    xx_mem_free(s); return NULL;
}
static vmdk_stream *vmdk_open(Abstractformat *f, const xx_list_s *options, xx_pd_struct *pd) {
    int64_t cursor = f && f->device ? xx_io_tell(f->device) : -1;
    vmdk_stream *s = vmdk_parse(f, options, pd);
    if (cursor >= 0 && xx_io_seek64(f->device, cursor, SEEK_SET)) { xx_mem_free(s); s = NULL; }
    return s;
}

static int vmdk_disk_close_cb(xx_io_device *device) {
    vmdk_disk *disk = (vmdk_disk *)device;
    if (!disk) return -1;
    xx_mem_free(disk->directory);
    xx_mem_free(disk->table);
    xx_mem_free(disk->packed);
    xx_mem_free(disk->plain);
    xx_mem_free(disk);
    return 0;
}

static int64_t vmdk_disk_size_cb(xx_io_device *device) {
    return device ? (int64_t)((vmdk_disk *)device)->layout.capacity : -1;
}

static int64_t vmdk_disk_tell_cb(xx_io_device *device) {
    return device ? (int64_t)((vmdk_disk *)device)->position : -1;
}

static int vmdk_disk_seek64_cb(xx_io_device *device, int64_t offset, int whence) {
    vmdk_disk *disk = (vmdk_disk *)device;
    int64_t base;
    if (!disk) return -1;
    if (whence == SEEK_SET) base = 0;
    else if (whence == SEEK_CUR) base = (int64_t)disk->position;
    else if (whence == SEEK_END) base = (int64_t)disk->layout.capacity;
    else return -1;
    if (offset < -base || offset > (int64_t)disk->layout.capacity - base)
        return -1;
    disk->position = (uint64_t)(base + offset);
    return 0;
}

static int vmdk_disk_seek_cb(xx_io_device *device, long offset, int whence) {
    return vmdk_disk_seek64_cb(device, (int64_t)offset, whence);
}

/* The parser has already walked every GTE and validated its bounds.  Recheck
 * the touched table and grain because an underlying source can change after
 * the iterator was opened. */
static bool vmdk_disk_sector(vmdk_disk *disk, uint64_t grain,
                             uint32_t *sector, uint64_t *limit) {
    const vmdk_stream *s = &disk->layout;
    uint32_t table_sector = vmdk_le32(disk->directory +
                                      (size_t)(grain / s->gtes) * 4U);
    *sector = 0U;
    *limit = s->data_end;
    if (!table_sector) return true;
    if (!disk->table_loaded || disk->table_sector != table_sector) {
        uint64_t bytes = (uint64_t)s->gtes * 4U;
        uint64_t at = (uint64_t)table_sector * VMDK_SECTOR;
        if (at < s->descriptor_end || !vmdk_span(at, bytes, s->data_end) ||
            (s->footer && (at < s->data_start + VMDK_SECTOR ||
              !vmdk_span(at, vmdk_round_sector(bytes),
                         s->gd_offset - VMDK_SECTOR) ||
              !vmdk_metadata_marker(disk->source, s, at, bytes, 1U, NULL))) ||
            !vmdk_read(disk->source, s, at, disk->table, (size_t)bytes, NULL))
            return false;
        disk->table_sector = table_sector;
        disk->table_at = at;
        disk->table_end = at + bytes;
        disk->table_loaded = true;
    }
    *sector = vmdk_le32(disk->table + (size_t)(grain % s->gtes) * 4U);
    *limit = s->footer ? disk->table_at - VMDK_SECTOR : s->data_end;
    return true;
}

static bool vmdk_disk_read_part(vmdk_disk *disk, uint64_t position,
                                uint8_t *out, size_t size) {
    const vmdk_stream *s = &disk->layout;
    uint64_t grain = position / s->grain_bytes;
    uint64_t within = position % s->grain_bytes;
    uint64_t remaining = s->capacity - grain * s->grain_bytes;
    uint64_t limit, at;
    uint32_t sector;
    if (remaining > s->grain_bytes) remaining = s->grain_bytes;
    if (grain >= s->grains || size > remaining - within ||
        !vmdk_disk_sector(disk, grain, &sector, &limit)) return false;
    if (!sector || sector == 1U) {
        if ((!sector && !s->standalone) ||
            (sector == 1U && !(s->flags & VMDK_FLAG_ZERO))) return false;
        xx_mem_zero(out, size);
        return true;
    }
    at = (uint64_t)sector * VMDK_SECTOR;
    if (at < s->data_start ||
        (!s->footer && (at < disk->table_end ||
                        at < s->gd_offset + s->entries * 4U))) return false;
    if (!s->compressed)
        return vmdk_span(at, within + size, limit) &&
               vmdk_read(disk->source, s, at + within, out, size, NULL);
    if (!disk->grain_loaded || disk->grain != grain) {
        uint8_t marker[14];
        uint32_t length;
        size_t written;
        size_t inflate_buffer = xx_get_file_buffer_size();
        uint64_t budget;
        if (!vmdk_read(disk->source, s, at, marker, sizeof(marker), NULL) ||
            vmdk_le64(marker) != grain * (s->grain_bytes / VMDK_SECTOR))
            return false;
        length = vmdk_le32(marker + 8U);
        if (length < 7U || length > s->grain_bytes * 2U + 64U ||
            !vmdk_span(at, 12U + (uint64_t)length, limit) ||
            (s->footer &&
             !vmdk_span(at, vmdk_round_sector(12U + (uint64_t)length), limit)) ||
            !xx_zlib_stream_header_is_valid(marker + 12U, 2U)) return false;
        budget = sizeof(*disk) + s->entries * 4U + (uint64_t)s->gtes * 4U +
                 s->grain_bytes + (disk->packed_capacity > length ?
                 disk->packed_capacity : length) + 32768U +
                 (inflate_buffer ? inflate_buffer : XX_DEFAULT_FILE_BUFFER_SIZE);
        if (budget > disk->memory_limit || s->grain_bytes > SIZE_MAX) return false;
        if (!disk->plain)
            disk->plain = (uint8_t *)xx_mem_alloc((size_t)s->grain_bytes);
        if (disk->packed_capacity < length) {
            xx_mem_free(disk->packed);
            disk->packed = (uint8_t *)xx_mem_alloc(length);
            disk->packed_capacity = disk->packed ? length : 0U;
        }
        if (!disk->plain || !disk->packed ||
            !vmdk_read(disk->source, s, at + 12U, disk->packed, length, NULL) ||
            !vmdk_inflate(disk->packed, length, disk->plain,
                          (size_t)s->grain_bytes, (size_t)remaining,
                          &written, NULL)) return false;
        disk->grain = grain;
        disk->grain_loaded = true;
    }
    xx_mem_copy(out, disk->plain + (size_t)within, size);
    return true;
}

static ssize_t vmdk_disk_read_cb(xx_io_device *device, void *buffer, size_t size) {
    vmdk_disk *disk = (vmdk_disk *)device;
    uint64_t position;
    int64_t source_cursor;
    size_t done = 0U;
    bool good = true;
    if (!disk || (!buffer && size)) return -1;
    if (size > VMDK_TRANSFER_SIZE) size = VMDK_TRANSFER_SIZE;
    if (size > disk->layout.capacity - disk->position)
        size = (size_t)(disk->layout.capacity - disk->position);
    if (!size) return 0;
    source_cursor = xx_io_tell(disk->source->device);
    if (source_cursor < 0) return -1;
    position = disk->position;
    while (done < size) {
        uint64_t within = position % disk->layout.grain_bytes;
        size_t part = size - done;
        if (part > disk->layout.grain_bytes - within)
            part = (size_t)(disk->layout.grain_bytes - within);
        if (!vmdk_disk_read_part(disk, position,
                                 (uint8_t *)buffer + done, part)) {
            good = false;
            break;
        }
        position += part;
        done += part;
    }
    if (xx_io_seek64(disk->source->device, source_cursor, SEEK_SET))
        good = false;
    if (!good) return -1;
    disk->position = position;
    return (ssize_t)done;
}

static xx_io_device *vmdk_disk_open(Abstractformat *source,
                                    const vmdk_stream *layout,
                                    const xx_list_s *options) {
    vmdk_disk *disk;
    const xx_var *limit;
    uint64_t base_memory;
    int64_t cursor;
    if (!source || !layout || layout->entries > SIZE_MAX / 4U ||
        (uint64_t)layout->gtes * 4U > SIZE_MAX) return NULL;
    base_memory = sizeof(vmdk_disk) + layout->entries * 4U +
                  (uint64_t)layout->gtes * 4U;
    if (!vmdk_memory_limit(source, options, base_memory)) return NULL;
    disk = (vmdk_disk *)xx_mem_calloc(1U, sizeof(*disk));
    if (!disk) return NULL;
    disk->source = source;
    disk->layout = *layout;
    limit = xx_format_resolve_extra_parameter(source, options,
                                               XX_META_ID_OPT_MEMORY_LIMIT);
    disk->memory_limit = limit ? xx_var_get_u64(limit) : UINT64_MAX;
    disk->directory = (uint8_t *)xx_mem_alloc((size_t)layout->entries * 4U);
    disk->table = (uint8_t *)xx_mem_alloc((size_t)layout->gtes * 4U);
    cursor = xx_io_tell(source->device);
    if (!disk->directory || !disk->table || cursor < 0 ||
        !vmdk_read(source, layout, layout->gd_offset, disk->directory,
                   (size_t)layout->entries * 4U, NULL) ||
        xx_io_seek64(source->device, cursor, SEEK_SET)) {
        if (cursor >= 0) (void)xx_io_seek64(source->device, cursor, SEEK_SET);
        vmdk_disk_close_cb(&disk->device);
        return NULL;
    }
    disk->device.read = vmdk_disk_read_cb;
    disk->device.seek = vmdk_disk_seek_cb;
    disk->device.seek64 = vmdk_disk_seek64_cb;
    disk->device.tell = vmdk_disk_tell_cb;
    disk->device.total_size = vmdk_disk_size_cb;
    disk->device.close = vmdk_disk_close_cb;
    disk->device.priv = disk;
    return &disk->device;
}

typedef struct vmdk_external_disk {
    xx_io_device device;
    vmdk_external_state *state; /* borrowed from the archive */
    uint64_t position;
} vmdk_external_disk;

static int vmdk_external_disk_close_cb(xx_io_device *device) {
    xx_mem_free(device);
    return 0;
}
static int64_t vmdk_external_disk_size_cb(xx_io_device *device) {
    vmdk_external_disk *disk = (vmdk_external_disk *)device;
    return disk ? (int64_t)(disk->state->layout->sectors * VMDK_SECTOR) : -1;
}
static int64_t vmdk_external_disk_tell_cb(xx_io_device *device) {
    return device ? (int64_t)((vmdk_external_disk *)device)->position : -1;
}
static int vmdk_external_disk_seek64_cb(xx_io_device *device, int64_t offset, int whence) {
    vmdk_external_disk *disk = (vmdk_external_disk *)device;
    int64_t base, capacity;
    if (!disk) return -1;
    capacity = (int64_t)(disk->state->layout->sectors * VMDK_SECTOR);
    if (whence == SEEK_SET) base = 0;
    else if (whence == SEEK_CUR) base = (int64_t)disk->position;
    else if (whence == SEEK_END) base = capacity;
    else return -1;
    if (offset < -base || offset > capacity - base) return -1;
    disk->position = (uint64_t)(base + offset);
    return 0;
}
static int vmdk_external_disk_seek_cb(xx_io_device *device, long offset, int whence) {
    return vmdk_external_disk_seek64_cb(device, (int64_t)offset, whence);
}
static ssize_t vmdk_external_disk_read_cb(xx_io_device *device, void *buffer, size_t size) {
    vmdk_external_disk *disk = (vmdk_external_disk *)device;
    vmdk_external_layout *layout;
    uint64_t position, base = 0U;
    size_t done = 0U;
    unsigned index;
    if (!disk || (!buffer && size)) return -1;
    layout = disk->state->layout;
    if (size > VMDK_TRANSFER_SIZE) size = VMDK_TRANSFER_SIZE;
    if (size > layout->sectors * VMDK_SECTOR - disk->position)
        size = (size_t)(layout->sectors * VMDK_SECTOR - disk->position);
    position = disk->position;
    while (done < size) {
        const vmdk_extent_spec *extent;
        xx_io_device *source;
        uint64_t within, at, capacity = 0U;
        size_t part;
        int64_t cursor;
        for (index = 0U; index < layout->count; ++index) {
            capacity = layout->extents[index].sectors * VMDK_SECTOR;
            if (position - base < capacity) break;
            base += capacity;
        }
        if (index == layout->count) return -1;
        extent = &layout->extents[index];
        within = position - base;
        part = size - done;
        if (part > capacity - within) part = (size_t)(capacity - within);
        at = within + (extent->sparse ? 0U : extent->offset_sectors * VMDK_SECTOR);
        source = extent->sparse ? disk->state->disks[index] : disk->state->sources[index];
        cursor = xx_io_tell(source);
        if (cursor < 0 || xx_io_seek64(source, (int64_t)at, SEEK_SET) ||
            xx_io_read(source, (uint8_t *)buffer + done, part) != (ssize_t)part) {
            if (cursor >= 0) (void)xx_io_seek64(source, cursor, SEEK_SET);
            return -1;
        }
        if (xx_io_seek64(source, cursor, SEEK_SET)) return -1;
        done += part; position += part;
    }
    disk->position = position;
    return (ssize_t)done;
}

static void vmdk_external_release(vmdk_external_state *state) {
    unsigned i;
    if (!state) return;
    for (i = 0U; i < VMDK_MAX_EXTENTS; ++i) {
        if (state->disks[i]) (void)xx_io_close(state->disks[i]);
        if (state->sparse[i]) xx_vmdk_free(state->sparse[i]);
        if (state->sources[i]) (void)xx_io_close(state->sources[i]);
    }
    xx_mem_free(state->layout);
    xx_mem_free(state);
}

bool xx_vmdk_open_data_files(xx_vmdk *archive, const char *descriptor_path) {
    vmdk_external_state *state = NULL;
    vmdk_external_layout *layout;
    size_t directory = 0U, i;
    unsigned index;
    if (!archive || !descriptor_path || archive->external) return false;
    layout = vmdk_external_parse(&archive->format, NULL, NULL);
    if (!layout) return false;
    for (i = 0U; descriptor_path[i]; ++i)
        if (descriptor_path[i] == '/' || descriptor_path[i] == '\\') directory = i + 1U;
    state = (vmdk_external_state *)xx_mem_calloc(1U, sizeof(*state));
    if (!state) { xx_mem_free(layout); return false; }
    state->layout = layout;
    for (index = 0U; index < layout->count; ++index) {
        const vmdk_extent_spec *extent = &layout->extents[index];
        size_t name_size = strlen(extent->name);
        char *path;
        int64_t size;
        if (!vmdk_safe_sidecar(extent->name) ||
            directory > SIZE_MAX - name_size - 1U) goto fail;
        path = (char *)xx_mem_alloc(directory + name_size + 1U);
        if (!path) goto fail;
        memcpy(path, descriptor_path, directory);
        memcpy(path + directory, extent->name, name_size + 1U);
        if (vmdk_regular_sidecar(path))
            state->sources[index] = xx_io_file_open(path, "rb");
        xx_mem_free(path);
        if (!state->sources[index]) goto fail;
        size = xx_io_total_size(state->sources[index]);
        if (size < 0) goto fail;
        if (extent->sparse) {
            state->sparse[index] = xx_vmdk_create(state->sources[index], 0);
            if (!state->sparse[index]) goto fail;
            state->sparse[index]->parentless_extent = true;
            state->disks[index] = xx_vmdk_open_disk_device(state->sparse[index], NULL);
            if (!state->disks[index] ||
                xx_io_total_size(state->disks[index]) != (int64_t)(extent->sectors * VMDK_SECTOR)) goto fail;
        } else if ((uint64_t)size <
                   (extent->offset_sectors + extent->sectors) * VMDK_SECTOR) goto fail;
    }
    archive->external = state;
    return true;
fail:
    vmdk_external_release(state);
    return false;
}

static xx_io_device *vmdk_external_disk_open(xx_vmdk *archive,
                                            const xx_list_s *options,
                                            xx_pd_struct *pd) {
    vmdk_external_disk *disk;
    vmdk_external_layout *current;
    if (!archive || !archive->external || vmdk_stopped(pd) ||
        !vmdk_memory_limit(&archive->format, options,
                           sizeof(vmdk_external_disk) + VMDK_TRANSFER_SIZE)) return NULL;
    current = vmdk_external_parse(&archive->format, options, pd);
    if (!current) return NULL;
    if (memcmp(current, archive->external->layout, sizeof(*current))) {
        xx_mem_free(current); return NULL;
    }
    xx_mem_free(current);
    disk = (vmdk_external_disk *)xx_mem_calloc(1U, sizeof(*disk));
    if (!disk) return NULL;
    disk->state = archive->external;
    disk->device.read = vmdk_external_disk_read_cb;
    disk->device.seek = vmdk_external_disk_seek_cb;
    disk->device.seek64 = vmdk_external_disk_seek64_cb;
    disk->device.tell = vmdk_external_disk_tell_cb;
    disk->device.total_size = vmdk_external_disk_size_cb;
    disk->device.close = vmdk_external_disk_close_cb;
    disk->device.priv = disk;
    return &disk->device;
}

static bool vmdk_external_extract(Abstractformat *f, const vmdk_stream *s,
                                  const xx_list_s *options,
                                  xx_io_device *destination, xx_pd_struct *pd) {
    xx_io_device *disk;
    uint8_t *buffer;
    uint64_t produced = 0U;
    const xx_var *limit;
    int level;
    bool valid = false;
    if (!s || !s->external) return false;
    limit = xx_format_resolve_extra_parameter(f, options, XX_META_ID_OPT_MAX_MEMBER_SIZE);
    if (limit && s->capacity > xx_var_get_u64(limit)) return false;
    disk = vmdk_external_disk_open((xx_vmdk *)f, options, pd);
    if (!disk) return false;
    buffer = (uint8_t *)xx_mem_alloc(VMDK_TRANSFER_SIZE);
    if (!buffer) { (void)xx_io_close(disk); return false; }
    level = xx_pd_enter_level(pd, s->capacity, "Reconstructing VMDK disk");
    while (produced < s->capacity && !vmdk_stopped(pd)) {
        size_t amount = (size_t)(s->capacity - produced < VMDK_TRANSFER_SIZE ?
                                 s->capacity - produced : VMDK_TRANSFER_SIZE);
        if (xx_io_read(disk, buffer, amount) != (ssize_t)amount ||
            !vmdk_write(destination, buffer, amount, pd)) break;
        produced += amount;
        xx_pd_set_current(pd, level, produced);
    }
    valid = produced == s->capacity && !vmdk_stopped(pd);
    xx_pd_leave_level(pd, level);
    xx_mem_free(buffer);
    if (xx_io_close(disk)) valid = false;
    return valid;
}

xx_io_device *xx_vmdk_open_disk_device(xx_vmdk *archive,
                                        xx_pd_struct *pd) {
    vmdk_stream *layout;
    xx_io_device *device;
    if (!archive) return NULL;
    layout = vmdk_open(&archive->format, NULL, pd);
    if (!layout) return NULL;
    device = layout->external ? vmdk_external_disk_open(archive, NULL, pd) :
                                vmdk_disk_open(&archive->format, layout, NULL);
    xx_mem_free(layout);
    return device;
}

static bool vmdk_fat_type(uint8_t type) {
    return type == 0x01U || type == 0x04U || type == 0x06U ||
           type == 0x0BU || type == 0x0CU || type == 0x0EU;
}

/* Only compose a disk with one conventional MBR FAT partition. Mixed or
 * multi-partition disks retain their raw disk.img member so no partition is
 * silently omitted. The FAT reader validates the boot sector and all of its
 * filesystem geometry before this is exposed to callers. */
static bool vmdk_fat_partition(xx_io_device *disk,
                                uint64_t *byte_offset, unsigned *number) {
    uint8_t mbr[512];
    unsigned i, present = 0U;
    uint64_t start = 0U, sectors = 0U;
    uint8_t type = 0U;
    if (xx_io_seek64(disk, 0, SEEK_SET) ||
        xx_io_read(disk, mbr, sizeof(mbr)) != (ssize_t)sizeof(mbr) ||
        mbr[510] != 0x55U || mbr[511] != 0xAAU) return false;
    for (i = 0U; i < 4U; ++i) {
        const uint8_t *entry = mbr + 446U + i * 16U;
        uint64_t count = vmdk_le32(entry + 12U);
        if (!count) continue;
        if (++present != 1U || (entry[0] != 0U && entry[0] != 0x80U))
            return false;
        start = vmdk_le32(entry + 8U);
        sectors = count;
        type = entry[4U];
        *number = i + 1U;
    }
    if (present != 1U || !vmdk_fat_type(type) || !start ||
        start >= (uint64_t)xx_io_total_size(disk) / VMDK_SECTOR ||
        sectors > (uint64_t)xx_io_total_size(disk) / VMDK_SECTOR - start)
        return false;
    *byte_offset = start * VMDK_SECTOR;
    return true;
}

static xx_fat *vmdk_fat_open(Abstractformat *source,
                             const vmdk_stream *layout,
                             const xx_list_s *options, unsigned *partition,
                             xx_io_device **disk_out, xx_pd_struct *pd) {
    xx_io_device *disk = vmdk_disk_open(source, layout, options);
    xx_fat *fat = NULL;
    uint64_t offset;
    if (!disk) return NULL;
    if (vmdk_fat_partition(disk, &offset, partition)) {
        fat = xx_fat_create(disk, (int64_t)offset);
        if (fat && (!xx_fat_handle_base_info(&fat->format, pd) ||
                    !xx_fat_get_number_of_archive_records(&fat->format, pd))) {
            xx_fat_free(fat);
            fat = NULL;
        }
    }
    if (!fat) {
        xx_io_close(disk);
        return NULL;
    }
    ((vmdk_disk *)disk)->fat_partition = *partition;
    *disk_out = disk;
    return fat;
}

static bool vmdk_fat_label(unsigned partition, char label[24]) {
    int n = xx_rt_snprintf(label, 24U, "FAT.Partition.%u", partition);
    return n > 0 && n < 24;
}

static bool vmdk_fat_prefix_record(xx_archive_record_state *state,
                                    const char *label) {
    const char *name = xx_archive_record_get_original_name(&state->current_record);
    char *prefixed;
    bool result;
    if (!name) return false;
    prefixed = xx_str_concat3(label, "/", name);
    if (!prefixed) return false;
    result = xx_archive_record_set_original_name(&state->current_record,
                                                  prefixed);
    xx_str_free(prefixed);
    return result;
}

static bool vmdk_fat_prefix_output(xx_archive_record_state *state,
                                    const char *label) {
    size_t i;
    for (i = 0U; i < state->options.count; ++i) {
        xx_meta *meta = (xx_meta *)xx_list_at(&state->options, i);
        const char *base = NULL;
        char *wide_base = NULL, *subdir;
        bool result;
        if (!meta || meta->meta_id != XX_META_ID_OPT_UNPACK_PATH)
            continue;
        if (meta->var.type == XX_VAR_TYPE_STRING ||
            meta->var.type == XX_VAR_TYPE_STRING_VIEW)
            base = xx_var_get_str(&meta->var);
        else if (meta->var.type == XX_VAR_TYPE_WSTRING ||
                 meta->var.type == XX_VAR_TYPE_WSTRING_VIEW)
            base = wide_base = xx_str_unicode_to_utf8(xx_var_get_wstr(&meta->var));
        if (!base) { xx_str_free(wide_base); return false; }
        subdir = base[0] ? xx_str_concat3(base, "/", label) :
                           xx_str_dup(label);
        xx_str_free(wide_base);
        if (!subdir) return false;
        result = xx_var_set_str(&meta->var, subdir);
        xx_str_free(subdir);
        return result;
    }
    return true;
}
static bool vmdk_extract(Abstractformat *f, const vmdk_stream *cached,
                          const xx_list_s *options, xx_io_device *destination, xx_pd_struct *pd) {
    int64_t cursor;
    vmdk_stream *s;
    bool valid = false;
    if (!f || !f->device || destination == f->device || vmdk_stopped(pd)) return false;
    cursor = xx_io_tell(f->device); s = vmdk_parse(f, options, pd);
    if (s && (!cached || !xx_rt_memcmp(cached, s, sizeof(*s))))
        valid = s->external ? vmdk_external_extract(f, s, options, destination, pd) :
                              vmdk_walk(f, s, options, destination, true, pd);
    xx_mem_free(s);
    if (cursor >= 0 && xx_io_seek64(f->device, cursor, SEEK_SET)) valid = false;
    return valid && !vmdk_stopped(pd);
}

static bool vmdk_copy_options(xx_list_s *destination, const xx_list_s *source) {
    size_t index;
    if (!source) return true;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *original = (const xx_meta *)xx_list_at((const xx_list_t *)source, index);
        xx_meta copy;
        if (!original) continue;
        xx_meta_init(&copy, original->meta_id);
        if (!xx_var_copy(&copy.var, &original->var) || !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy); return false;
        }
    }
    return true;
}
static bool vmdk_set_record(Abstractformat *f, xx_archive_record *record, const vmdk_stream *s) {
    xx_archive_record_cleanup(record); xx_archive_record_init(record);
    record->header_offset = f->base_address; record->header_size = 512;
    record->data_offset = f->base_address; record->compressed_size = (int64_t)s->available;
    return xx_archive_record_set_original_name(record, "disk.img") &&
        xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE, s->available) &&
        xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE, s->capacity) &&
        xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD, s->compressed ? 2U : 1U) &&
        xx_archive_record_set_meta_u64(record, XX_META_ID_FLAGS, s->flags) &&
        xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false) &&
        xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED, false);
}
static void vmdk_free_stream(void *s) { xx_mem_free(s); }

void xx_vmdk_init(xx_vmdk *archive, xx_io_device *device, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive)); xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE; archive->format.file_type = XX_VMDK_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE; archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-vmdk");
    xx_format_set_extension(&archive->format, "vmdk");
    archive->format.check_is_valid = xx_vmdk_check_is_valid;
    archive->format.handle_base_info = xx_vmdk_handle_base_info;
    archive->format.get_format_size = xx_vmdk_get_format_size;
    archive->format.get_number_of_archive_records = xx_vmdk_get_number_of_archive_records;
    archive->format.create_archive_records_reading = xx_vmdk_create_archive_records_reading;
    archive->format.get_current_archive_record = xx_vmdk_get_current_archive_record;
    archive->format.unpack_current_archive_record = xx_vmdk_unpack_current_archive_record;
    archive->format.archive_record_move_to_next = xx_vmdk_archive_record_move_to_next;
    archive->format.free_archive_records_reading = xx_vmdk_free_archive_records_reading;
    archive->archive_end = -1;
}
xx_vmdk *xx_vmdk_create(xx_io_device *device, int64_t base_address) {
    xx_vmdk *archive = (xx_vmdk *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_vmdk_init(archive, device, base_address);
    return archive;
}
void xx_vmdk_destroy(xx_vmdk *archive) {
    if (archive) {
        vmdk_external_release(archive->external);
        xx_format_cleanup_extra_parameters(&archive->format);
    }
}
void xx_vmdk_free(xx_vmdk *archive) {
    if (!archive) return;
    xx_vmdk_destroy(archive); xx_mem_free(archive);
}
bool xx_vmdk_check_is_valid(Abstractformat *f, xx_pd_struct *pd) {
    vmdk_stream *s = vmdk_open(f, NULL, pd);
    if (!s) return false;
    xx_mem_free(s); return true;
}
bool xx_vmdk_handle_base_info(Abstractformat *f, xx_pd_struct *pd) {
    vmdk_stream *s = vmdk_open(f, NULL, pd);
    xx_io_device *disk = NULL;
    xx_fat *fat;
    unsigned partition = 0U;
    if (!s) {
        if (f) { f->format_size = -1; f->number_of_archive_records = 0U;
            f->is_valid = false; f->base_info_handled = false; }
        return false;
    }
    fat = s->external ? NULL : vmdk_fat_open(f, s, NULL, &partition, &disk, pd);
    ((xx_vmdk *)f)->number_of_records = fat ?
        xx_fat_get_number_of_archive_records(&fat->format, pd) : 1U;
    ((xx_vmdk *)f)->archive_end = f->base_address + (int64_t)s->available;
    f->number_of_archive_records = ((xx_vmdk *)f)->number_of_records;
    f->format_size = (int64_t)s->available;
    f->file_type = XX_VMDK_FILE_TYPE; f->format_type = XX_TYPE_ARCHIVE; f->is_archive = true;
    f->overlay_offset = -1; f->overlay_size = 0; f->is_valid = true; f->base_info_handled = true;
    if (fat) { xx_fat_free(fat); xx_io_close(disk); }
    xx_mem_free(s); return true;
}
int64_t xx_vmdk_get_format_size(Abstractformat *f, xx_pd_struct *pd) {
    return f && (f->base_info_handled || xx_vmdk_handle_base_info(f, pd)) ? f->format_size : -1;
}
uint64_t xx_vmdk_get_number_of_archive_records(Abstractformat *f, xx_pd_struct *pd) {
    return f && (f->base_info_handled || xx_vmdk_handle_base_info(f, pd)) ? f->number_of_archive_records : 0U;
}
xx_archive_record_state *xx_vmdk_create_archive_records_reading(
    Abstractformat *f, const xx_list_s *options, xx_pd_struct *pd) {
    vmdk_stream *s = vmdk_open(f, options, pd);
    xx_archive_record_state *state;
    xx_io_device *disk = NULL;
    xx_fat *fat;
    unsigned partition = 0U;
    char label[24];
    if (!s) return NULL;
    fat = s->external ? NULL : vmdk_fat_open(f, s, options, &partition, &disk, pd);
    if (fat) {
        state = xx_fat_create_archive_records_reading(&fat->format,
                                                        options, pd);
        if (!state || !vmdk_fat_label(partition, label) ||
            !vmdk_fat_prefix_output(state, label) ||
            (state->has_record && !vmdk_fat_prefix_record(state, label))) {
            if (state) xx_fat_free_archive_records_reading(&fat->format, state);
            xx_fat_free(fat);
            xx_io_close(disk);
            xx_mem_free(s);
            return NULL;
        }
        xx_mem_free(s);
        return state;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) { xx_mem_free(s); return NULL; }
    xx_archive_record_state_init(state, f); state->internal_state = s;
    state->free_internal = vmdk_free_stream; state->total_records = 1U;
    if (!vmdk_copy_options(&state->options, options) || !vmdk_set_record(f, &state->current_record, s)) {
        xx_archive_record_state_free(state); return NULL;
    }
    state->has_record = true; return state;
}
const xx_archive_record *xx_vmdk_get_current_archive_record(Abstractformat *f, xx_archive_record_state *state) {
    if (f && state && state->format != f)
        return xx_fat_get_current_archive_record(state->format, state);
    return f && state && state->format == f && state->has_record ? &state->current_record : NULL;
}
bool xx_vmdk_archive_record_move_to_next(Abstractformat *f, xx_archive_record_state *state, xx_pd_struct *pd) {
    if (f && state && state->format != f) {
        unsigned partition = ((vmdk_disk *)state->format->device)->fat_partition;
        char label[24];
        if (!xx_fat_archive_record_move_to_next(state->format, state, pd))
            return false;
        if (!vmdk_fat_label(partition, label) ||
            !vmdk_fat_prefix_record(state, label)) {
            state->has_record = false;
            return false;
        }
        return true;
    }
    if (!f || !state || state->format != f || vmdk_stopped(pd)) return false;
    state->has_record = false; state->current_index = 1U; return false;
}

bool xx_vmdk_unpack_current_archive_record(Abstractformat *f, xx_archive_record_state *state, xx_pd_struct *pd) {
    const xx_var *option;
    vmdk_stream *s;
    const char *base;
    char *owned_base = NULL, *path = NULL, *stage = NULL;
    xx_io_device *destination = NULL;
    size_t capacity;
    unsigned attempt;
    bool valid = false, overwrite = false;
    if (f && state && state->format != f)
        return xx_fat_unpack_current_archive_record(state->format, state, pd);
    if (!f || !state || state->format != f || !state->has_record ||
        !(s = (vmdk_stream *)state->internal_state) || vmdk_stopped(pd)) return false;
    option = xx_format_resolve_extra_parameter(f, &state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!option) return vmdk_extract(f, s, &state->options, NULL, pd);
    if (option->type == XX_VAR_TYPE_STRING || option->type == XX_VAR_TYPE_STRING_VIEW) base = xx_var_get_str(option);
    else if (option->type == XX_VAR_TYPE_WSTRING || option->type == XX_VAR_TYPE_WSTRING_VIEW)
        base = owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(option));
    else return false;
    if (!base) goto done;
    path = base[0] ? xx_str_concat3(base, "/", "disk.img") : xx_str_dup("disk.img");
    option = xx_format_resolve_extra_parameter(f, &state->options, XX_META_ID_OPT_OVERWRITE);
    overwrite = option && xx_var_get_bool(option);
    if (!path || (!overwrite && xx_io_file_exists_a(path)) || !xx_store_create_dirs_a(path, false) ||
        xx_str_len(base) > SIZE_MAX - 40U) goto done;
    capacity = xx_str_len(base) + 40U; stage = (char *)xx_mem_alloc(capacity);
    if (!stage) goto done;
    for (attempt = 0U; attempt < 128U && !vmdk_stopped(pd); ++attempt) {
        int length = xx_rt_snprintf(stage, capacity, "%s%s.xx_vmdk.tmp.%u", base, base[0] ? "/" : "", attempt);
        if (length < 0 || (size_t)length >= capacity) goto done;
        destination = xx_io_file_open(stage, "wbx");
        if (destination) break;
    }
    if (!destination) goto done;
    valid = vmdk_extract(f, s, &state->options, destination, pd);
    if (xx_io_close(destination)) valid = false;
    destination = NULL;
    if (valid && !vmdk_stopped(pd)) valid = xx_io_file_replace_a(stage, path, overwrite);
    else valid = false;
    if (!valid) (void)xx_io_file_remove_a(stage);
done:
    if (destination) { (void)xx_io_close(destination); (void)xx_io_file_remove_a(stage); }
    xx_mem_free(stage); xx_str_free(path); xx_str_free(owned_base); return valid;
}
void xx_vmdk_free_archive_records_reading(Abstractformat *f, xx_archive_record_state *state) {
    if (f && state && state->format != f) {
        xx_fat *fat = (xx_fat *)state->format;
        xx_io_device *disk = fat->format.device;
        xx_fat_free_archive_records_reading(&fat->format, state);
        xx_fat_free(fat);
        xx_io_close(disk);
        return;
    }
    (void)f; xx_archive_record_state_free(state);
}
bool xx_vmdk_unpack_to_device(xx_vmdk *archive, uint64_t index,
                              xx_io_device *destination, xx_pd_struct *pd) {
    return archive && destination && !index && vmdk_extract(&archive->format, NULL, NULL, destination, pd);
}
