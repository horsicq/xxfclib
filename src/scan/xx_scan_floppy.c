/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* Raw FAT12/FAT16 floppy images and Atari MSA images for the scan engines.
 * The rules are stated in xx_scan_floppy_internal.h and are shared, step for
 * step, with Qt's XFATImage and XMSA: change both sides together. */

#include "xx_scan_floppy_internal.h"
#include "xxfclib/memory/xx_memory.h"

#include <stdio.h>
#include <string.h>

#define XX_SCAN_FAT_SECTOR 512U
#define XX_SCAN_FAT_ENTRY 32U
#define XX_SCAN_FAT_MAX_RECORDS 10000U
#define XX_SCAN_FAT_MAX_DEPTH 8U
#define XX_SCAN_FAT_MAX_DIR_BYTES 0x200000U
/* Eight directory levels and a file name, 13 bytes each with the separator. */
#define XX_SCAN_FAT_PATH 128U

#define XX_SCAN_MSA_HEADER 10U
#define XX_SCAN_MSA_MAX_SECTORS 36U
#define XX_SCAN_MSA_MAX_TRACK 85U

typedef struct xx_scan_fat_geometry {
    uint32_t sectors_per_cluster;
    uint32_t reserved;
    uint32_t fats;
    uint32_t root_entries;
    uint32_t total;
    uint32_t fat_sectors;
    uint32_t clusters;
    uint32_t cluster_size;
    uint32_t fat_bytes; /* Bytes of one FAT that hold the entries of every cluster. */
    bool fat16;
    int64_t fat_offset;
    int64_t root_offset;
    int64_t data_offset;
    int64_t size; /* Image size. */
} xx_scan_fat_geometry;

struct xx_scan_fat_image {
    xx_io_device *device;
    xx_scan_fat_geometry geometry;
    uint8_t *fat;
    uint8_t *seen;    /* One bit per cluster: directory clusters already read. */
    uint8_t *cluster; /* One cluster of file data. */
    xx_scan_fat_record *records;
    size_t count;
    size_t capacity;
    uint64_t listed; /* Sum of the listed sizes. */
    uint64_t limit;
    bool stop;
    bool oom;
};

static uint32_t xx_scan_floppy_u16le(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U);
}

static uint32_t xx_scan_floppy_u32le(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) | ((uint32_t)bytes[2] << 16U) | ((uint32_t)bytes[3] << 24U);
}

static uint32_t xx_scan_floppy_u16be(const uint8_t *bytes)
{
    return ((uint32_t)bytes[0] << 8U) | (uint32_t)bytes[1];
}

static bool xx_scan_floppy_read(xx_io_device *device, int64_t offset, void *data, size_t size)
{
    uint8_t *bytes = (uint8_t *)data;
    size_t done = 0;
    if (xx_io_seek64(device, offset, SEEK_SET) != 0) return false;
    while (done < size) {
        ssize_t count = xx_io_read(device, bytes + done, size - done);
        if (count <= 0) return false;
        done += (size_t)count;
    }
    return true;
}

/* The BPB of a 512-byte boot sector and the layout that follows from it. */
static bool xx_scan_fat_geometry_read(const uint8_t *boot, int64_t size, xx_scan_fat_geometry *geometry)
{
    uint32_t meta;
    uint64_t needed;
    int64_t volume;
    if (size < (int64_t)XX_SCAN_FAT_SECTOR) return false;
    if (xx_scan_floppy_u16le(boot + 11) != XX_SCAN_FAT_SECTOR) return false;
    geometry->sectors_per_cluster = boot[13];
    if (!geometry->sectors_per_cluster || (geometry->sectors_per_cluster & (geometry->sectors_per_cluster - 1U)) || geometry->sectors_per_cluster > 64U) return false;
    geometry->reserved = xx_scan_floppy_u16le(boot + 14);
    if (!geometry->reserved) geometry->reserved = 1U;
    geometry->fats = boot[16];
    if (geometry->fats != 1U && geometry->fats != 2U) return false;
    geometry->root_entries = xx_scan_floppy_u16le(boot + 17);
    if (!geometry->root_entries || (geometry->root_entries % 16U)) return false;
    geometry->total = xx_scan_floppy_u16le(boot + 19);
    if (!geometry->total) geometry->total = xx_scan_floppy_u32le(boot + 32);
    geometry->fat_sectors = xx_scan_floppy_u16le(boot + 22);
    if (!geometry->fat_sectors || !geometry->total) return false;
    meta = geometry->reserved + geometry->fats * geometry->fat_sectors + geometry->root_entries / 16U;
    if (meta >= geometry->total) return false;
    geometry->clusters = (geometry->total - meta) / geometry->sectors_per_cluster;
    if (!geometry->clusters || geometry->clusters >= 65525U) return false;
    geometry->fat16 = geometry->clusters >= 4085U;
    needed = geometry->fat16 ? ((uint64_t)geometry->clusters + 2U) * 2U : (((uint64_t)geometry->clusters + 2U) * 3U + 1U) / 2U;
    if (needed > (uint64_t)geometry->fat_sectors * XX_SCAN_FAT_SECTOR) return false;
    geometry->fat_bytes = (uint32_t)needed;
    geometry->fat_offset = (int64_t)geometry->reserved * XX_SCAN_FAT_SECTOR;
    geometry->root_offset = (int64_t)(geometry->reserved + geometry->fats * geometry->fat_sectors) * XX_SCAN_FAT_SECTOR;
    geometry->data_offset = (int64_t)meta * XX_SCAN_FAT_SECTOR;
    geometry->cluster_size = geometry->sectors_per_cluster * XX_SCAN_FAT_SECTOR;
    volume = (int64_t)geometry->total * XX_SCAN_FAT_SECTOR;
    /* The BPB volume is 1/2 .. 5/4 of the image, and cluster 2 is inside it. */
    if (volume > size + size / 4 || volume < size / 2) return false;
    if (geometry->data_offset + (int64_t)geometry->cluster_size > size) return false;
    geometry->size = size;
    return true;
}

static bool xx_scan_fat_is_data_cluster(const xx_scan_fat_geometry *geometry, uint32_t cluster)
{
    return cluster >= 2U && cluster < geometry->clusters + 2U;
}

/* Whether the whole cluster lies inside the image. */
static bool xx_scan_fat_cluster_in_image(const xx_scan_fat_geometry *geometry, uint32_t cluster)
{
    return geometry->data_offset + (int64_t)(cluster - 1U) * geometry->cluster_size <= geometry->size;
}

static int64_t xx_scan_fat_cluster_offset(const xx_scan_fat_geometry *geometry, uint32_t cluster)
{
    return geometry->data_offset + (int64_t)(cluster - 2U) * geometry->cluster_size;
}

static bool xx_scan_fat_name_ok(const uint8_t *entry)
{
    size_t index;
    for (index = 0; index < 11U; ++index) {
        uint8_t ch = entry[index];
        if (!index && ch == 0x05U) continue;
        if (ch < 0x20U || ch == 0x7FU || ch == '"' || ch == '*' || ch == '/' || ch == ':' || ch == '<' || ch == '>' || ch == '?' || ch == '\\' || ch == '|') return false;
    }
    return entry[0] != 0x20U;
}

/* One root entry: 2 the end marker, 0 not counted (deleted, long-name
 * piece), 1 live, -1 impossible. */
static int xx_scan_fat_classify(const xx_scan_fat_geometry *geometry, const uint8_t *entry)
{
    uint8_t attributes = entry[11];
    uint32_t cluster, size;
    if (!entry[0]) return 2;
    if (entry[0] == 0xE5U) return 0;
    if ((attributes & 0x3FU) == 0x0FU) return 0;
    if (attributes & 0xC0U) return -1;
    if ((attributes & 0x18U) == 0x18U) return -1;
    if (!xx_scan_fat_name_ok(entry)) return -1;
    if (attributes & 0x08U) return 1;
    cluster = xx_scan_floppy_u16le(entry + 26);
    size = xx_scan_floppy_u32le(entry + 28);
    if (attributes & 0x10U) return xx_scan_fat_is_data_cluster(geometry, cluster) ? 1 : -1;
    if (!cluster) return size ? -1 : 1;
    if (!xx_scan_fat_is_data_cluster(geometry, cluster)) return -1;
    if ((uint64_t)size > (uint64_t)geometry->clusters * geometry->cluster_size) return -1;
    return 1;
}

/* The boot sector's BPB and a root directory that looks like one. */
static bool xx_scan_fat_detect(xx_io_device *device, int64_t size, xx_scan_fat_geometry *geometry)
{
    uint8_t block[XX_SCAN_FAT_SECTOR];
    uint32_t index;
    uint32_t live = 0, bad = 0;
    if (size < (int64_t)XX_SCAN_FAT_SECTOR || !xx_scan_floppy_read(device, 0, block, sizeof(block))) return false;
    if (!xx_scan_fat_geometry_read(block, size, geometry)) return false;
    for (index = 0; index < geometry->root_entries; ++index) {
        uint32_t in_block = index % (XX_SCAN_FAT_SECTOR / XX_SCAN_FAT_ENTRY);
        int verdict;
        if (!in_block && !xx_scan_floppy_read(device, geometry->root_offset + (int64_t)index * XX_SCAN_FAT_ENTRY, block, sizeof(block))) return false;
        verdict = xx_scan_fat_classify(geometry, block + in_block * XX_SCAN_FAT_ENTRY);
        if (verdict == 2) break;
        if (verdict == 1) ++live;
        if (verdict < 0) ++bad;
    }
    return live && bad * 4U <= live;
}

bool xx_scan_floppy_is_fat(xx_io_device *device, int64_t size)
{
    xx_scan_fat_geometry geometry;
    return device && xx_scan_fat_detect(device, size, &geometry);
}

static uint32_t xx_scan_fat_entry(const xx_scan_fat_image *image, uint32_t cluster)
{
    uint32_t offset, value;
    if (image->geometry.fat16) return xx_scan_floppy_u16le(image->fat + (size_t)cluster * 2U);
    offset = cluster + cluster / 2U;
    value = xx_scan_floppy_u16le(image->fat + offset);
    return (cluster & 1U) ? value >> 4U : value & 0xFFFU;
}

/* Whether the chain from cluster holds size bytes inside the image, and
 * whether those clusters follow one another. */
static bool xx_scan_fat_chain_ok(const xx_scan_fat_image *image, uint32_t cluster, uint32_t size, bool *contiguous)
{
    uint64_t needed = ((uint64_t)size + image->geometry.cluster_size - 1U) / image->geometry.cluster_size;
    *contiguous = true;
    while (needed--) {
        uint32_t next;
        if (!xx_scan_fat_is_data_cluster(&image->geometry, cluster) || !xx_scan_fat_cluster_in_image(&image->geometry, cluster)) return false;
        next = xx_scan_fat_entry(image, cluster);
        if (needed && next != cluster + 1U) *contiguous = false;
        cluster = next;
    }
    return true;
}

/* base[size] right-trimmed of spaces; bytes outside 0x20..0x7E and the path
 * separators become '_'. Returns the characters written. */
static size_t xx_scan_fat_name_part(const uint8_t *base, size_t size, char *out)
{
    size_t index;
    while (size && base[size - 1U] == ' ') --size;
    for (index = 0; index < size; ++index) {
        uint8_t ch = base[index];
        out[index] = (ch >= 0x20U && ch < 0x7FU && ch != '/' && ch != '\\') ? (char)ch : '_';
    }
    return size;
}

/* prefix + the entry's 8.3 name, NUL-terminated in path. */
static bool xx_scan_fat_path(const char *prefix, const uint8_t *entry, char path[XX_SCAN_FAT_PATH])
{
    uint8_t base[8];
    char extension[3];
    size_t length = strlen(prefix);
    size_t extension_size;
    if (length + 13U > XX_SCAN_FAT_PATH) return false;
    memcpy(path, prefix, length);
    memcpy(base, entry, sizeof(base));
    if (base[0] == 0x05U) base[0] = 0xE5U;
    length += xx_scan_fat_name_part(base, sizeof(base), path + length);
    extension_size = xx_scan_fat_name_part(entry + 8, 3U, extension);
    if (extension_size) {
        path[length++] = '.';
        memcpy(path + length, extension, extension_size);
        length += extension_size;
    }
    path[length] = '\0';
    return true;
}

static bool xx_scan_fat_append(xx_scan_fat_image *image, const char *path, uint32_t cluster, uint32_t size, bool contiguous)
{
    xx_scan_fat_record *record;
    size_t length = strlen(path);
    if (image->count == image->capacity) {
        size_t capacity = image->capacity ? image->capacity * 2U : 64U;
        xx_scan_fat_record *grown = (xx_scan_fat_record *)xx_mem_realloc(image->records, capacity * sizeof(*grown));
        if (!grown) return false;
        image->records = grown;
        image->capacity = capacity;
    }
    record = &image->records[image->count];
    record->name = (char *)xx_mem_alloc(length + 1U);
    if (!record->name) return false;
    memcpy(record->name, path, length + 1U);
    record->offset = xx_scan_fat_cluster_offset(&image->geometry, cluster);
    record->size = size;
    record->cluster = cluster;
    record->contiguous = contiguous;
    ++image->count;
    return true;
}

/* The clusters of a directory, each read once for the whole walk. */
static uint8_t *xx_scan_fat_read_directory(xx_scan_fat_image *image, uint32_t cluster, size_t *size)
{
    const xx_scan_fat_geometry *geometry = &image->geometry;
    uint8_t *data = NULL;
    size_t length = 0;
    while (xx_scan_fat_is_data_cluster(geometry, cluster) && !(image->seen[cluster >> 3U] & (1U << (cluster & 7U))) && xx_scan_fat_cluster_in_image(geometry, cluster) &&
           length < XX_SCAN_FAT_MAX_DIR_BYTES) {
        uint8_t *grown = (uint8_t *)xx_mem_realloc(data, length + geometry->cluster_size);
        if (!grown) {
            image->oom = true;
            break;
        }
        data = grown;
        image->seen[cluster >> 3U] |= (uint8_t)(1U << (cluster & 7U));
        if (!xx_scan_floppy_read(image->device, xx_scan_fat_cluster_offset(geometry, cluster), data + length, geometry->cluster_size)) break;
        length += geometry->cluster_size;
        cluster = xx_scan_fat_entry(image, cluster);
    }
    *size = length;
    return data;
}

static void xx_scan_fat_walk(xx_scan_fat_image *image, const uint8_t *data, size_t size, const char *prefix, unsigned depth)
{
    const xx_scan_fat_geometry *geometry = &image->geometry;
    size_t position;
    for (position = 0; position + XX_SCAN_FAT_ENTRY <= size; position += XX_SCAN_FAT_ENTRY) {
        const uint8_t *entry = data + position;
        char path[XX_SCAN_FAT_PATH];
        uint8_t attributes = entry[11];
        uint32_t cluster, file_size;
        bool contiguous;
        if (image->stop || image->oom) return;
        if (!entry[0]) return;
        if (entry[0] == 0xE5U || entry[0] == 0x2EU) continue;
        if ((attributes & 0x3FU) == 0x0FU || (attributes & 0x08U)) continue;
        cluster = xx_scan_floppy_u16le(entry + 26);
        file_size = xx_scan_floppy_u32le(entry + 28);
        if (!xx_scan_fat_path(prefix, entry, path)) continue;
        if (attributes & 0x10U) {
            uint8_t *directory;
            size_t directory_size = 0;
            size_t length;
            if (depth >= XX_SCAN_FAT_MAX_DEPTH) continue;
            directory = xx_scan_fat_read_directory(image, cluster, &directory_size);
            length = strlen(path);
            path[length] = '/';
            path[length + 1U] = '\0';
            if (directory) xx_scan_fat_walk(image, directory, directory_size, path, depth + 1U);
            xx_mem_free(directory);
            continue;
        }
        if (!file_size || (uint64_t)file_size > (uint64_t)geometry->clusters * geometry->cluster_size) continue;
        if (!xx_scan_fat_chain_ok(image, cluster, file_size, &contiguous)) continue;
        if (image->listed + file_size > image->limit || image->count >= XX_SCAN_FAT_MAX_RECORDS) {
            image->stop = true;
            return;
        }
        image->listed += file_size;
        if (!xx_scan_fat_append(image, path, cluster, file_size, contiguous)) {
            image->oom = true;
            return;
        }
    }
}

xx_scan_fat_image *xx_scan_fat_open(xx_io_device *device, int64_t size, bool *oom)
{
    xx_scan_fat_image *image;
    xx_scan_fat_geometry geometry;
    uint8_t *root = NULL;
    size_t root_size;
    if (oom) *oom = false;
    if (!device || !xx_scan_fat_detect(device, size, &geometry)) return NULL;
    image = (xx_scan_fat_image *)xx_mem_alloc(sizeof(*image));
    if (!image) goto no_memory;
    memset(image, 0, sizeof(*image));
    image->device = device;
    image->geometry = geometry;
    image->limit = (uint64_t)geometry.clusters * geometry.cluster_size * 2U;
    root_size = (size_t)geometry.root_entries * XX_SCAN_FAT_ENTRY;
    /* One byte past the last entry: a FAT12 entry is read as two bytes. */
    image->fat = (uint8_t *)xx_mem_alloc((size_t)geometry.fat_bytes + 1U);
    image->seen = (uint8_t *)xx_mem_alloc((size_t)(geometry.clusters + 2U) / 8U + 1U);
    image->cluster = (uint8_t *)xx_mem_alloc(geometry.cluster_size);
    root = (uint8_t *)xx_mem_alloc(root_size);
    if (!image->fat || !image->seen || !image->cluster || !root) goto no_memory;
    memset(image->seen, 0, (size_t)(geometry.clusters + 2U) / 8U + 1U);
    image->fat[geometry.fat_bytes] = 0;
    if (!xx_scan_floppy_read(device, geometry.fat_offset, image->fat, geometry.fat_bytes) || !xx_scan_floppy_read(device, geometry.root_offset, root, root_size)) {
        xx_mem_free(root);
        xx_scan_fat_close(image);
        return NULL;
    }
    xx_scan_fat_walk(image, root, root_size, "", 0U);
    xx_mem_free(root);
    root = NULL;
    if (image->oom) goto no_memory;
    return image;
no_memory:
    if (oom) *oom = true;
    xx_mem_free(root);
    xx_scan_fat_close(image);
    return NULL;
}

size_t xx_scan_fat_count(const xx_scan_fat_image *image)
{
    return image ? image->count : 0U;
}

const xx_scan_fat_record *xx_scan_fat_record_at(const xx_scan_fat_image *image, size_t index)
{
    return image && index < image->count ? &image->records[index] : NULL;
}

bool xx_scan_fat_read(xx_scan_fat_image *image, size_t index, xx_io_device *destination)
{
    const xx_scan_fat_geometry *geometry;
    uint32_t cluster, remaining;
    if (!image || index >= image->count || !destination) return false;
    geometry = &image->geometry;
    cluster = image->records[index].cluster;
    remaining = image->records[index].size;
    while (remaining) {
        uint32_t amount = remaining < geometry->cluster_size ? remaining : geometry->cluster_size;
        if (!xx_scan_fat_is_data_cluster(geometry, cluster) || !xx_scan_fat_cluster_in_image(geometry, cluster)) return false;
        if (!xx_scan_floppy_read(image->device, xx_scan_fat_cluster_offset(geometry, cluster), image->cluster, amount)) return false;
        if (xx_io_write(destination, image->cluster, amount) != (ssize_t)amount) return false;
        remaining -= amount;
        cluster = xx_scan_fat_entry(image, cluster);
    }
    return true;
}

void xx_scan_fat_close(xx_scan_fat_image *image)
{
    size_t index;
    if (!image) return;
    for (index = 0; index < image->count; ++index) xx_mem_free(image->records[index].name);
    xx_mem_free(image->records);
    xx_mem_free(image->fat);
    xx_mem_free(image->seen);
    xx_mem_free(image->cluster);
    xx_mem_free(image);
}

typedef struct xx_scan_msa_header {
    uint32_t track_size; /* Decoded bytes of one track side. */
    uint32_t tracks;     /* Track sides stored. */
} xx_scan_msa_header;

static bool xx_scan_msa_header_read(xx_io_device *device, int64_t size, xx_scan_msa_header *header)
{
    uint8_t bytes[XX_SCAN_MSA_HEADER];
    uint32_t sectors, sides, first, last;
    if (!device || size < (int64_t)XX_SCAN_MSA_HEADER || !xx_scan_floppy_read(device, 0, bytes, sizeof(bytes))) return false;
    if (bytes[0] != 0x0EU || bytes[1] != 0x0FU) return false;
    sectors = xx_scan_floppy_u16be(bytes + 2);
    sides = xx_scan_floppy_u16be(bytes + 4);
    first = xx_scan_floppy_u16be(bytes + 6);
    last = xx_scan_floppy_u16be(bytes + 8);
    if (!sectors || sectors > XX_SCAN_MSA_MAX_SECTORS || sides > 1U || first > last || last > XX_SCAN_MSA_MAX_TRACK) return false;
    header->track_size = sectors * XX_SCAN_FAT_SECTOR;
    header->tracks = (last - first + 1U) * (sides + 1U);
    return true;
}

bool xx_scan_msa_info(xx_io_device *device, int64_t size, int64_t *packed_size, int64_t *image_size)
{
    xx_scan_msa_header header;
    int64_t position = XX_SCAN_MSA_HEADER;
    uint32_t track;
    if (!xx_scan_msa_header_read(device, size, &header)) return false;
    for (track = 0; track < header.tracks; ++track) {
        uint8_t bytes[2];
        uint32_t length;
        if (position + 2 > size || !xx_scan_floppy_read(device, position, bytes, sizeof(bytes))) return false;
        length = xx_scan_floppy_u16be(bytes);
        position += 2;
        if (!length || length > header.track_size || position + (int64_t)length > size) return false;
        position += length;
    }
    if (packed_size) *packed_size = position - (int64_t)XX_SCAN_MSA_HEADER;
    if (image_size) *image_size = (int64_t)header.tracks * header.track_size;
    return true;
}

bool xx_scan_floppy_is_msa(xx_io_device *device, int64_t size)
{
    return xx_scan_msa_info(device, size, NULL, NULL);
}

bool xx_scan_msa_decode(xx_io_device *device, int64_t size, xx_io_device *destination, bool *oom)
{
    xx_scan_msa_header header;
    int64_t position = XX_SCAN_MSA_HEADER;
    uint8_t *packed, *track_data;
    uint32_t track;
    bool result = false;
    if (oom) *oom = false;
    if (!destination || !xx_scan_msa_info(device, size, NULL, NULL) || !xx_scan_msa_header_read(device, size, &header)) return false;
    packed = (uint8_t *)xx_mem_alloc(header.track_size);
    track_data = (uint8_t *)xx_mem_alloc(header.track_size);
    if (!packed || !track_data) {
        if (oom) *oom = true;
        goto done;
    }
    for (track = 0; track < header.tracks; ++track) {
        uint8_t bytes[2];
        const uint8_t *output = packed;
        uint32_t length;
        if (!xx_scan_floppy_read(device, position, bytes, sizeof(bytes))) goto done;
        length = xx_scan_floppy_u16be(bytes);
        position += 2;
        if (!length || length > header.track_size || !xx_scan_floppy_read(device, position, packed, length)) goto done;
        if (length != header.track_size) {
            uint32_t read = 0, written = 0;
            while (read < length) {
                uint8_t value = packed[read++];
                uint32_t count = 1U;
                if (value == 0xE5U) {
                    if (length - read < 3U) goto done;
                    value = packed[read];
                    count = xx_scan_floppy_u16be(packed + read + 1U);
                    read += 3U;
                }
                if (!count || count > header.track_size - written) goto done;
                memset(track_data + written, value, count);
                written += count;
            }
            if (written != header.track_size) goto done;
            output = track_data;
        }
        if (xx_io_write(destination, output, header.track_size) != (ssize_t)header.track_size) goto done;
        position += length;
    }
    result = true;
done:
    xx_mem_free(packed);
    xx_mem_free(track_data);
    return result;
}

bool xx_scan_floppy_head_may_be_image(const uint8_t *data, size_t size)
{
    uint32_t sectors_per_cluster, root_entries;
    if (!data) return false;
    if (size >= 2U && data[0] == 0x0EU && data[1] == 0x0FU) return true;
    if (size < XX_SCAN_FAT_SECTOR) return false;
    sectors_per_cluster = data[13];
    root_entries = xx_scan_floppy_u16le(data + 17);
    return xx_scan_floppy_u16le(data + 11) == XX_SCAN_FAT_SECTOR && sectors_per_cluster && !(sectors_per_cluster & (sectors_per_cluster - 1U)) &&
           sectors_per_cluster <= 64U && (data[16] == 1U || data[16] == 2U) && root_entries && !(root_entries % 16U) && xx_scan_floppy_u16le(data + 22) &&
           (xx_scan_floppy_u16le(data + 19) || xx_scan_floppy_u32le(data + 32));
}
