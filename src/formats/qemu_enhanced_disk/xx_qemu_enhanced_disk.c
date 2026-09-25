/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * QEMU Enhanced Disk (QED) images. Written from the QED specification on the
 * QEMU wiki; the field table and the mapping rules are in
 * xx_qemu_enhanced_disk.h.
 *
 * The reader publishes ONE member, the reconstructed guest disk, whose size
 * is the header's image_size. Producing it walks the L1/L2 tables cluster by
 * cluster: an unallocated or zero cluster is written out as zeros and an
 * allocated cluster is copied. QED has no compression, so there is no codec.
 * A backing file is named in the record's comment but never opened, so the
 * clusters an overlay leaves to its backing file come out as zeros.
 *
 * Validity follows QEMU's own open-time and read-time rules, with two
 * deliberate differences. A table_size of 1 cluster is accepted: the
 * specification allows it, and QEMU refuses it only because its table
 * bounds check compares the table's last cluster with its first. A
 * header_size of 0 is refused: the header itself lives in that area, and no
 * writer produces one.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/qemu_enhanced_disk/xx_qemu_enhanced_disk.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

/* Registration placeholder. xxfc_defs.h is shared and is not edited from
 * here, so the alias macro defined next to the enumerator is tested instead;
 * this picks up the real file type as soon as QEMU_ENHANCED_DISK is
 * registered there. */
#ifdef QEMU_ENHANCED_DISK
#define XX_QEMU_ENHANCED_DISK_FILE_TYPE XX_FILE_TYPE_QEMU_ENHANCED_DISK
#else
#define XX_QEMU_ENHANCED_DISK_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define XX_QED_MAGIC UINT32_C(0x00444551) /* "QED\0" little endian */
#define XX_QED_HEADER_SIZE 64U

#define XX_QED_MIN_CLUSTER_SIZE UINT32_C(0x1000)    /* 4 KB */
#define XX_QED_MAX_CLUSTER_SIZE UINT32_C(0x4000000) /* 64 MB */
#define XX_QED_MAX_TABLE_SIZE 16U                   /* clusters */

#define XX_QED_F_BACKING_FILE UINT64_C(0x01)
#define XX_QED_F_NEED_CHECK UINT64_C(0x02)
#define XX_QED_F_BACKING_FORMAT_NO_PROBE UINT64_C(0x04)
#define XX_QED_FEATURE_MASK                                                    \
    (XX_QED_F_BACKING_FILE | XX_QED_F_NEED_CHECK |                             \
     XX_QED_F_BACKING_FORMAT_NO_PROBE)

/* L2 entry values that are not offsets. */
#define XX_QED_CLUSTER_UNALLOCATED UINT64_C(0)
#define XX_QED_CLUSTER_ZERO UINT64_C(1)

/* Guest disks larger than 16 TB are refused rather than listed: the member's
 * size is published as a byte count and extraction writes every byte. */
#define XX_QED_MAX_IMAGE_SIZE ((uint64_t)1 << 44)

/* With image_size capped at 2^44, the number of L1 entries the virtual size
 * can reach is ceil(2^44 / (cluster_size * entries)) and never more than the
 * table's entries; over every legal cluster_size / table_size pair that is
 * at most 2^15, reached with 16 KB clusters and 16-cluster tables. The cap
 * is a second guard on the one allocation this reader sizes from the header:
 * 256 KB at most. */
#define XX_QED_MAX_L1_ENTRIES UINT32_C(0x8000)

/* L2 entries are read this many at a time. Every table has at least 512
 * entries (one 4 KB cluster), so an aligned window never crosses a table. */
#define XX_QED_L2_WINDOW 512U

/* Cluster data is copied through a buffer of this size, whatever the cluster
 * size is, so a 64 MB cluster never becomes a 64 MB allocation. */
#define XX_QED_COPY_CHUNK 0x10000U

/* How many guest clusters the no-destination walk maps. See
 * xx_qed_write_image(). */
#define XX_QED_PROBE_CLUSTERS UINT64_C(4096)

#define XX_QED_MAX_BACKING_NAME 1024U

/* The member name. QED images carry no name of their own. */
#define XX_QED_MEMBER_NAME "disk.img"

typedef struct xx_qed_private_s {
    uint64_t *l1_table;          /**< l1_count validated L2 table offsets. */
    char *backing_file;          /**< Owned, or NULL. */
    int64_t input_size;
    int64_t base_address;
    uint64_t file_size;          /**< Bytes past base, rounded down to a cluster. */
    uint64_t image_size;
    uint64_t l1_table_offset;
    uint64_t features;
    uint64_t compat_features;
    uint64_t autoclear_features;
    uint64_t header_bytes;       /**< header_size * cluster_size. */
    uint64_t table_bytes;        /**< table_size * cluster_size. */
    uint32_t cluster_size;
    uint32_t cluster_bits;
    uint32_t table_size;
    uint32_t header_size;
    uint32_t entries_bits;       /**< log2 of the entries in one table. */
    uint32_t l1_count;           /**< L1 entries the virtual size reaches. */
    uint32_t backing_filename_offset;
    uint32_t backing_filename_size;
    bool consumed;               /**< The single member has been stepped past. */
} xx_qed_private;

/* One window of one L2 table, so that consecutive guest clusters do not
 * each cost a seek and an eight-byte read. */
typedef struct xx_qed_l2_cache_s {
    uint64_t table;              /**< Host offset of the cached table, 0 none. */
    uint64_t first;              /**< Index of raw[0] within that table. */
    uint8_t raw[XX_QED_L2_WINDOW * 8U];
} xx_qed_l2_cache;

static void xx_qemu_enhanced_disk_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

/* Every read goes through xx_io_seek64: a disk image routinely exceeds 2 GB
 * and long is 32 bits on Win64, so xx_io_seek() would truncate the offset. */
static bool xx_qed_read_at(xx_io_device *device, int64_t offset, void *data,
                           size_t size) {
    uint8_t *out = (uint8_t *)data;
    size_t done = 0U;

    if (!device || (!data && size != 0U) || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0) {
        return false;
    }
    while (done < size) {
        ssize_t got = xx_io_read(device, out + done, size - done);
        if (got <= 0 || (size_t)got > size - done) return false;
        done += (size_t)got;
    }
    return true;
}

static bool xx_qed_write_all(xx_io_device *output, const uint8_t *data,
                             size_t size) {
    size_t done = 0U;

    while (done < size) {
        ssize_t sent = xx_io_write(output, data + done, size - done);
        if (sent <= 0 || (size_t)sent > size - done) return false;
        done += (size_t)sent;
    }
    return true;
}

/* True when [offset, offset + size) lies inside [0, total_size). */
static bool xx_qed_range_within(int64_t total_size, int64_t offset,
                                int64_t size) {
    return (total_size >= 0) && (offset >= 0) && (size >= 0) &&
           (offset <= total_size) && (size <= total_size - offset);
}

static bool xx_qed_is_power_of_two(uint32_t value) {
    return value != 0U && (value & (value - 1U)) == 0U;
}

static uint32_t xx_qed_log2(uint32_t value) {
    uint32_t bits = 0U;

    while (bits < 31U && (UINT32_C(1) << bits) < value) ++bits;
    return bits;
}

/* A data cluster offset: cluster aligned, past the header area, and the
 * cluster ends inside the file. file_size is a whole number of clusters, so
 * "starts before the end" is the same as "ends inside". */
static bool xx_qed_cluster_offset_ok(const xx_qed_private *parsed,
                                     uint64_t offset) {
    return (offset & (uint64_t)(parsed->cluster_size - 1U)) == 0U &&
           offset >= parsed->header_bytes && offset < parsed->file_size;
}

/* An L1 or L2 table offset: a valid cluster offset whose whole table ends
 * inside the file. */
static bool xx_qed_table_offset_ok(const xx_qed_private *parsed,
                                   uint64_t offset) {
    return xx_qed_cluster_offset_ok(parsed, offset) &&
           parsed->file_size >= parsed->table_bytes &&
           offset <= parsed->file_size - parsed->table_bytes;
}

static void xx_qed_private_cleanup(xx_qed_private *parsed) {
    if (!parsed) return;
    if (parsed->l1_table) xx_mem_free(parsed->l1_table);
    if (parsed->backing_file) xx_str_free(parsed->backing_file);
    xx_mem_zero(parsed, sizeof(*parsed));
    parsed->input_size = -1;
}

static void xx_qed_private_free(void *pointer) {
    xx_qed_private *parsed = (xx_qed_private *)pointer;

    if (!parsed) return;
    xx_qed_private_cleanup(parsed);
    xx_mem_free(parsed);
}

/* The backing file name is plain bytes, not NUL terminated. It is published
 * for information only - this reader never opens it - so a name that is too
 * long or holds control characters is simply not published. Its range has
 * already been checked against the header area. */
static char *xx_qed_read_backing_file(xx_io_device *device,
                                      const xx_qed_private *parsed) {
    uint32_t size = parsed->backing_filename_size;
    uint32_t index;
    char *name;

    if (size == 0U || size > XX_QED_MAX_BACKING_NAME) return NULL;
    name = (char *)xx_mem_alloc((size_t)size + 1U);
    if (!name) return NULL;
    if (!xx_qed_read_at(device,
                        parsed->base_address +
                            (int64_t)parsed->backing_filename_offset,
                        name, (size_t)size)) {
        xx_mem_free(name);
        return NULL;
    }
    name[size] = '\0';
    for (index = 0U; index < size; ++index) {
        unsigned char ch = (unsigned char)name[index];
        if (ch < 32U || ch == 127U) {
            xx_mem_free(name);
            return NULL;
        }
    }
    return name;
}

/* ---------------------------------------------------------------- parse -- */

static bool xx_qed_parse(Abstractformat *self, xx_qed_private *parsed,
                         xx_pd_struct *pd) {
    uint8_t header[XX_QED_HEADER_SIZE];
    uint8_t *raw_l1 = NULL;
    int64_t total_size;
    uint64_t available;
    uint64_t needed_l1;
    uint32_t l1_shift;
    uint32_t max_bits;
    uint32_t index;

    /* Initialised before the guard clauses: check_is_valid() cleans up its
     * stack copy whatever this returns. */
    if (parsed) {
        xx_mem_zero(parsed, sizeof(*parsed));
        parsed->input_size = -1;
    }
    if (!self || !self->device || !parsed || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    total_size = xx_io_total_size(self->device);
    if (!xx_qed_range_within(total_size, self->base_address,
                             (int64_t)XX_QED_HEADER_SIZE) ||
        !xx_qed_read_at(self->device, self->base_address, header,
                        sizeof(header))) {
        return false;
    }
    if (xx_data_get_u32(header, sizeof(header), 0U, false) != XX_QED_MAGIC) {
        return false;
    }
    parsed->input_size = total_size;
    parsed->base_address = self->base_address;
    parsed->cluster_size = xx_data_get_u32(header, sizeof(header), 4U, false);
    parsed->table_size = xx_data_get_u32(header, sizeof(header), 8U, false);
    parsed->header_size = xx_data_get_u32(header, sizeof(header), 12U, false);
    parsed->features = xx_data_get_u64(header, sizeof(header), 16U, false);
    parsed->compat_features = xx_data_get_u64(header, sizeof(header), 24U, false);
    parsed->autoclear_features =
        xx_data_get_u64(header, sizeof(header), 32U, false);
    parsed->l1_table_offset = xx_data_get_u64(header, sizeof(header), 40U, false);
    parsed->image_size = xx_data_get_u64(header, sizeof(header), 48U, false);
    parsed->backing_filename_offset =
        xx_data_get_u32(header, sizeof(header), 56U, false);
    parsed->backing_filename_size =
        xx_data_get_u32(header, sizeof(header), 60U, false);

    /* An unknown feature bit changes the on-disk format; QEMU refuses it
     * too. compat and autoclear bits are ignorable by definition. */
    if ((parsed->features & ~XX_QED_FEATURE_MASK) != 0U) goto fail;
    if (!xx_qed_is_power_of_two(parsed->cluster_size) ||
        parsed->cluster_size < XX_QED_MIN_CLUSTER_SIZE ||
        parsed->cluster_size > XX_QED_MAX_CLUSTER_SIZE) {
        goto fail;
    }
    if (!xx_qed_is_power_of_two(parsed->table_size) ||
        parsed->table_size > XX_QED_MAX_TABLE_SIZE) {
        goto fail;
    }
    if (parsed->header_size == 0U ||
        parsed->header_size > UINT32_MAX / parsed->cluster_size) {
        goto fail;
    }
    parsed->cluster_bits = xx_qed_log2(parsed->cluster_size);
    /* entries = table_size * cluster_size / 8, all powers of two. */
    parsed->entries_bits =
        parsed->cluster_bits + xx_qed_log2(parsed->table_size) - 3U;
    parsed->header_bytes =
        (uint64_t)parsed->header_size * (uint64_t)parsed->cluster_size;
    parsed->table_bytes =
        (uint64_t)parsed->table_size * (uint64_t)parsed->cluster_size;

    /* The virtual size is whole sectors and no larger than the two table
     * levels can address, entries * entries * cluster_size. */
    if ((parsed->image_size & UINT64_C(511)) != 0U) goto fail;
    if (parsed->image_size > XX_QED_MAX_IMAGE_SIZE) goto fail;
    max_bits = 2U * parsed->entries_bits + parsed->cluster_bits;
    if (max_bits < 64U && parsed->image_size > ((uint64_t)1 << max_bits)) {
        goto fail;
    }

    /* QEMU compares every offset with the file length rounded down to a
     * whole cluster, so a cluster that is only partly present is invalid. */
    available = (uint64_t)(total_size - self->base_address);
    parsed->file_size = available & ~(uint64_t)(parsed->cluster_size - 1U);
    if (!xx_qed_table_offset_ok(parsed, parsed->l1_table_offset)) goto fail;

    /* Only the L1 entries the virtual size can reach are read. l1_shift is
     * at most 26 + 27 and image_size at most 2^44, so nothing overflows. */
    l1_shift = parsed->cluster_bits + parsed->entries_bits;
    needed_l1 = (parsed->image_size + (((uint64_t)1 << l1_shift) - 1U)) >>
                l1_shift;
    if (needed_l1 > ((uint64_t)1 << parsed->entries_bits) ||
        needed_l1 > (uint64_t)XX_QED_MAX_L1_ENTRIES) {
        goto fail;
    }
    parsed->l1_count = (uint32_t)needed_l1;
    if (parsed->l1_count != 0U) {
        size_t bytes = (size_t)parsed->l1_count * 8U;

        raw_l1 = (uint8_t *)xx_mem_alloc(bytes);
        parsed->l1_table =
            (uint64_t *)xx_mem_calloc(parsed->l1_count, sizeof(uint64_t));
        if (!raw_l1 || !parsed->l1_table) goto fail;
        if (!xx_qed_read_at(self->device,
                            self->base_address +
                                (int64_t)parsed->l1_table_offset,
                            raw_l1, bytes)) {
            goto fail;
        }
        /* Every reachable L1 entry is checked now, so a table offset that
         * points outside the file, into the header or off a cluster boundary
         * is a refusal up front rather than a failure half way through an
         * extraction. */
        for (index = 0U; index < parsed->l1_count; ++index) {
            uint64_t entry =
                xx_data_get_u64(raw_l1, bytes, (size_t)index * 8U, false);
            if (entry != 0U && !xx_qed_table_offset_ok(parsed, entry)) {
                goto fail;
            }
            parsed->l1_table[index] = entry;
        }
        xx_mem_free(raw_l1);
        raw_l1 = NULL;
    }

    if ((parsed->features & XX_QED_F_BACKING_FILE) != 0U) {
        /* The name has to sit inside the header area; QEMU refuses the
         * image otherwise. header_bytes <= L1 offset < file_size, so this
         * range is inside the device as well. */
        if ((uint64_t)parsed->backing_filename_offset +
                (uint64_t)parsed->backing_filename_size >
            parsed->header_bytes) {
            goto fail;
        }
        parsed->backing_file = xx_qed_read_backing_file(self->device, parsed);
    }
    return true;

fail:
    if (raw_l1) xx_mem_free(raw_l1);
    xx_qed_private_cleanup(parsed);
    return false;
}

/* --------------------------------------------------------------- reader -- */

/* Map one guest cluster. Returns 1 with *host set for an allocated cluster,
 * 0 for a cluster that reads as zeros (unallocated or a zero cluster), and
 * -1 when the L2 entry is corrupt or cannot be read. */
static int xx_qed_map_cluster(Abstractformat *self,
                              const xx_qed_private *parsed,
                              xx_qed_l2_cache *cache, uint64_t cluster,
                              uint64_t *host) {
    uint64_t l1_index = cluster >> parsed->entries_bits;
    uint64_t l2_index =
        cluster & (((uint64_t)1 << parsed->entries_bits) - 1U);
    uint64_t first = l2_index & ~(uint64_t)(XX_QED_L2_WINDOW - 1U);
    uint64_t table;
    uint64_t entry;

    if (l1_index >= (uint64_t)parsed->l1_count || !parsed->l1_table) return 0;
    table = parsed->l1_table[l1_index];
    if (table == 0U) return 0;
    if (cache->table != table || cache->first != first) {
        /* table was validated in parse: the whole table, and so this window
         * of it, lies inside the file. */
        cache->table = 0U;
        if (!xx_qed_read_at(self->device,
                            parsed->base_address + (int64_t)table +
                                (int64_t)(first * 8U),
                            cache->raw, sizeof(cache->raw))) {
            return -1;
        }
        cache->table = table;
        cache->first = first;
    }
    entry = xx_data_get_u64(cache->raw, sizeof(cache->raw),
                            (size_t)(l2_index - first) * 8U, false);
    if (entry == XX_QED_CLUSTER_UNALLOCATED || entry == XX_QED_CLUSTER_ZERO) {
        return 0;
    }
    /* QEMU fails the read here too rather than guessing. */
    if (!xx_qed_cluster_offset_ok(parsed, entry)) return -1;
    *host = entry;
    return 1;
}

/* Walk the guest address space. When output is NULL nothing is read or
 * written beyond the tables and the walk stops after XX_QED_PROBE_CLUSTERS
 * clusters: the virtual size is a header field, so a small file can claim
 * terabytes, and mapping every cluster of that just to answer "is this
 * readable?" is a denial of service with no destination to show for it.
 * With a destination the caller has asked for the whole disk and gets it. */
static bool xx_qed_write_image(Abstractformat *self,
                               const xx_qed_private *parsed,
                               xx_io_device *output, xx_pd_struct *pd) {
    xx_qed_l2_cache *cache;
    uint8_t *buffer;
    uint64_t remaining = parsed->image_size;
    uint64_t cluster = 0U;
    uint64_t budget = output ? UINT64_MAX : XX_QED_PROBE_CLUSTERS;
    bool buffer_is_zero = false;
    bool result = true;

    cache = (xx_qed_l2_cache *)xx_mem_alloc(sizeof(*cache));
    buffer = (uint8_t *)xx_mem_alloc(XX_QED_COPY_CHUNK);
    if (!cache || !buffer) {
        if (cache) xx_mem_free(cache);
        if (buffer) xx_mem_free(buffer);
        return false;
    }
    cache->table = 0U;
    cache->first = 0U;
    while (remaining != 0U && budget != 0U) {
        uint64_t chunk = remaining < (uint64_t)parsed->cluster_size
                             ? remaining
                             : (uint64_t)parsed->cluster_size;
        uint64_t host = 0U;
        uint64_t done = 0U;
        int kind;

        if (pd && xx_pd_is_stopped(pd)) {
            result = false;
            break;
        }
        kind = xx_qed_map_cluster(self, parsed, cache, cluster, &host);
        if (kind < 0) {
            result = false;
            break;
        }
        while (output && done < chunk) {
            size_t piece = chunk - done < (uint64_t)XX_QED_COPY_CHUNK
                               ? (size_t)(chunk - done)
                               : (size_t)XX_QED_COPY_CHUNK;
            if (kind == 0) {
                if (!buffer_is_zero) {
                    xx_mem_zero(buffer, XX_QED_COPY_CHUNK);
                    buffer_is_zero = true;
                }
            } else {
                /* host + cluster_size <= file_size, checked by the map. */
                buffer_is_zero = false;
                if (!xx_qed_read_at(self->device,
                                    parsed->base_address + (int64_t)host +
                                        (int64_t)done,
                                    buffer, piece)) {
                    result = false;
                    break;
                }
            }
            if (!xx_qed_write_all(output, buffer, piece)) {
                result = false;
                break;
            }
            done += (uint64_t)piece;
        }
        if (!result) break;
        remaining -= chunk;
        ++cluster;
        if (budget != UINT64_MAX) --budget;
    }
    xx_mem_free(cache);
    xx_mem_free(buffer);
    return result;
}

/* ------------------------------------------------------------ lifecycle -- */

void xx_qemu_enhanced_disk_init(xx_qemu_enhanced_disk *archive,
                                xx_io_device *dev, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, dev, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_QEMU_ENHANCED_DISK_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-qed");
    xx_format_set_extension(&archive->format, "qed");
    archive->format.check_is_valid = xx_qemu_enhanced_disk_check_is_valid;
    archive->format.handle_base_info = xx_qemu_enhanced_disk_handle_base_info;
    archive->format.get_format_size = xx_qemu_enhanced_disk_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_qemu_enhanced_disk_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_qemu_enhanced_disk_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_qemu_enhanced_disk_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_qemu_enhanced_disk_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_qemu_enhanced_disk_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_qemu_enhanced_disk_free_archive_records_reading;
    archive->format.destroy = xx_qemu_enhanced_disk_vtable_destroy;
}

xx_qemu_enhanced_disk *xx_qemu_enhanced_disk_create(xx_io_device *dev,
                                                    int64_t base_address) {
    xx_qemu_enhanced_disk *archive =
        (xx_qemu_enhanced_disk *)xx_mem_alloc(sizeof(*archive));

    if (archive) xx_qemu_enhanced_disk_init(archive, dev, base_address);
    return archive;
}

void xx_qemu_enhanced_disk_destroy(xx_qemu_enhanced_disk *archive) {
    if (!archive) return;
    if (archive->internal) {
        xx_qed_private_free(archive->internal);
        archive->internal = NULL;
    }
    xx_format_cleanup_extra_parameters(&archive->format);
}

static void xx_qemu_enhanced_disk_vtable_destroy(Abstractformat *self) {
    xx_qemu_enhanced_disk_destroy((xx_qemu_enhanced_disk *)self);
}

void xx_qemu_enhanced_disk_free(xx_qemu_enhanced_disk *archive) {
    if (!archive) return;
    xx_qemu_enhanced_disk_destroy(archive);
    xx_mem_free(archive);
}

/* --------------------------------------------------------------- format -- */

bool xx_qemu_enhanced_disk_check_is_valid(Abstractformat *self,
                                          xx_pd_struct *pd) {
    xx_qed_private parsed;
    bool result = xx_qed_parse(self, &parsed, pd);

    xx_qed_private_cleanup(&parsed);
    return result;
}

bool xx_qemu_enhanced_disk_handle_base_info(Abstractformat *self,
                                            xx_pd_struct *pd) {
    xx_qemu_enhanced_disk *archive = (xx_qemu_enhanced_disk *)self;
    xx_qed_private *parsed;

    if (!self) return false;
    parsed = (xx_qed_private *)xx_mem_alloc(sizeof(*parsed));
    if (!parsed || !xx_qed_parse(self, parsed, pd)) {
        if (parsed) xx_mem_free(parsed);
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    if (archive->internal) xx_qed_private_free(archive->internal);
    archive->internal = parsed;
    archive->number_of_records = 1U;
    archive->image_size = parsed->image_size;
    archive->l1_table_offset = parsed->l1_table_offset;
    archive->features = parsed->features;
    archive->compat_features = parsed->compat_features;
    archive->autoclear_features = parsed->autoclear_features;
    archive->cluster_size = parsed->cluster_size;
    archive->table_size = parsed->table_size;
    archive->header_size = parsed->header_size;
    archive->backing_filename_offset = parsed->backing_filename_offset;
    archive->backing_filename_size = parsed->backing_filename_size;
    archive->has_backing_file =
        (parsed->features & XX_QED_F_BACKING_FILE) != 0U;
    archive->needs_check = (parsed->features & XX_QED_F_NEED_CHECK) != 0U;
    /* Clusters are appended anywhere in the file and QEMU takes the file's
     * length as the image's extent, so the whole device is the format. */
    self->format_size = parsed->input_size - self->base_address;
    self->overlay_offset = -1;
    self->overlay_size = 0;
    self->number_of_archive_records = 1U;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_qemu_enhanced_disk_get_format_size(Abstractformat *self,
                                              xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return -1;
    }
    return self->format_size;
}

uint64_t xx_qemu_enhanced_disk_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return ((xx_qemu_enhanced_disk *)self)->number_of_records;
}

bool xx_qemu_enhanced_disk_unpack_to_device(xx_qemu_enhanced_disk *archive,
                                            xx_io_device *output,
                                            xx_pd_struct *pd) {
    Abstractformat *self = archive ? &archive->format : NULL;

    if (!self || !self->device || !output ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd)) ||
        !archive->internal) {
        return false;
    }
    return xx_qed_write_image(self, (const xx_qed_private *)archive->internal,
                              output, pd);
}

/* -------------------------------------------------------------- records -- */

static bool xx_qed_copy_options(xx_list_s *destination,
                                const xx_list_s *source) {
    size_t index;

    if (!destination || !source) return source == NULL;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *item =
            (const xx_meta *)xx_list_at((const xx_list_t *)source, index);
        xx_meta copy;
        if (!item) continue;
        xx_meta_init(&copy, item->meta_id);
        if (!xx_var_copy(&copy.var, &item->var) ||
            !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

static const xx_var *xx_qed_find_option(const xx_list_s *options,
                                        uint32_t meta_id) {
    size_t index;

    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *item =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (item && item->meta_id == meta_id) return &item->var;
    }
    return NULL;
}

static bool xx_qed_populate_record(xx_archive_record *record,
                                   const xx_qed_private *parsed) {
    uint64_t stored = (uint64_t)(parsed->input_size - parsed->base_address);

    if (!record || !parsed) return false;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = parsed->base_address;
    record->header_size = (int64_t)parsed->header_bytes;
    /* The guest image is scattered across the file, so there is no single
     * data extent; the record points at the L1 table, which is where reading
     * it begins. */
    record->data_offset =
        (int64_t)parsed->l1_table_offset + parsed->base_address;
    record->compressed_size = (int64_t)stored;
    if (!xx_archive_record_set_original_name(record, XX_QED_MEMBER_NAME) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                        parsed->image_size) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                        stored) ||
        !xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false) ||
        !xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                         false)) {
        return false;
    }
    if (parsed->backing_file &&
        !xx_archive_record_set_meta_str(record, XX_META_ID_COMMENT,
                                        parsed->backing_file)) {
        return false;
    }
    return true;
}

xx_archive_record_state *xx_qemu_enhanced_disk_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_record_state *state;
    xx_qed_private *parsed;

    if (!self || !self->device ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    parsed = (xx_qed_private *)xx_mem_calloc(1U, sizeof(*parsed));
    if (!state || !parsed) {
        if (state) xx_mem_free(state);
        if (parsed) xx_mem_free(parsed);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    if (!xx_qed_copy_options(&state->options, options) ||
        !xx_qed_parse(self, parsed, pd)) {
        xx_qed_private_free(parsed);
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->internal_state = parsed;
    state->free_internal = xx_qed_private_free;
    state->total_records = 1;
    if (!xx_qed_populate_record(&state->current_record, parsed)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_qemu_enhanced_disk_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_qemu_enhanced_disk_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    xx_qed_private *parsed;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    /* One member, so the first step is always the last. */
    parsed = (xx_qed_private *)state->internal_state;
    if (parsed) parsed->consumed = true;
    xx_archive_record_cleanup(&state->current_record);
    xx_archive_record_init(&state->current_record);
    state->has_record = false;
    return false;
}

bool xx_qemu_enhanced_disk_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    xx_qed_private *parsed;
    const xx_var *option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *destination = NULL;
    xx_io_device *output = NULL;
    size_t base_length;
    bool result;
    bool created = false;

    if (!self || !self->device || !state || state->format != self ||
        !state->has_record || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    parsed = (xx_qed_private *)state->internal_state;
    if (!parsed || parsed->consumed) return false;

    option = xx_qed_find_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!option) {
        /* No destination: map the first clusters and discard them, which is
         * a real check that the L2 tables hold up. */
        return xx_qed_write_image(self, parsed, NULL, pd);
    }
    if (option->type == XX_VAR_TYPE_STRING ||
        option->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(option);
    } else if (option->type == XX_VAR_TYPE_WSTRING ||
               option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(option));
        base = owned_base;
    }
    if (!base) {
        if (owned_base) xx_str_free(owned_base);
        return false;
    }
    base_length = xx_str_len(base);
    if (base_length != 0U && base[base_length - 1U] != '/' &&
        base[base_length - 1U] != '\\') {
        destination = xx_str_concat3(base, "/", XX_QED_MEMBER_NAME);
    } else {
        destination = xx_str_concat(base, XX_QED_MEMBER_NAME);
    }
    if (owned_base) xx_str_free(owned_base);
    if (!destination) return false;
    if (!xx_store_create_dirs_a(destination, false)) {
        xx_str_free(destination);
        return false;
    }
    output = xx_io_file_open(destination, "wb");
    created = output != NULL;
    result = output != NULL && xx_qed_write_image(self, parsed, output, pd);
    if (output && xx_io_close(output) != 0) result = false;
    if (!result && created) xx_rt_remove(destination);
    xx_str_free(destination);
    return result;
}

void xx_qemu_enhanced_disk_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

/* ------------------------------------------------------------ accessors -- */

uint64_t xx_qemu_enhanced_disk_get_image_size(
    const xx_qemu_enhanced_disk *archive) {
    return archive ? archive->image_size : 0U;
}
uint32_t xx_qemu_enhanced_disk_get_cluster_size(
    const xx_qemu_enhanced_disk *archive) {
    return archive ? archive->cluster_size : 0U;
}
uint32_t xx_qemu_enhanced_disk_get_table_size(
    const xx_qemu_enhanced_disk *archive) {
    return archive ? archive->table_size : 0U;
}
uint64_t xx_qemu_enhanced_disk_get_features(
    const xx_qemu_enhanced_disk *archive) {
    return archive ? archive->features : 0U;
}
const char *xx_qemu_enhanced_disk_get_backing_file(
    const xx_qemu_enhanced_disk *archive) {
    const xx_qed_private *parsed =
        archive ? (const xx_qed_private *)archive->internal : NULL;
    return parsed ? parsed->backing_file : NULL;
}
