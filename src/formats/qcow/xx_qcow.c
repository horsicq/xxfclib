/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * QEMU QCOW2 / QCOW3 disk images. The layout this follows is QEMU's
 * docs/interop/qcow2.txt; the per-field notes live in xx_qcow.h.
 *
 * The reader publishes ONE member, the reconstructed guest disk, whose size
 * is the header's virtual size. Producing it walks the L1/L2 tables cluster
 * by cluster: an unallocated or zero-flagged cluster is written out as zeros,
 * a plain cluster is copied, and a compressed cluster is inflated. The three
 * are genuinely different code paths and each is exercised by the harness.
 *
 * Compressed clusters carry a RAW DEFLATE stream - there is no zlib header,
 * so xx_deflate_decompress_memory() is the entry point and
 * xx_zlib_stream_decode_memory() would fail on the first byte. A version 3
 * image may instead declare zstd, which goes to xx_zstd_decompress_memory().
 * No codec is implemented here.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/qcow/xx_qcow.h"

#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/algo/zstd/xx_zstd.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

/* Registration placeholder. xxfc_defs.h is shared and is not edited from
 * here, so the alias macro defined next to the enumerator is tested instead;
 * this picks up the real file type as soon as QCOW is registered there. */
#ifdef QCOW
#define XX_QCOW_FILE_TYPE XX_FILE_TYPE_QCOW
#else
#define XX_QCOW_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define XX_QCOW_MAGIC UINT32_C(0x514649fb)
#define XX_QCOW_HEADER_V2_SIZE 72
#define XX_QCOW_HEADER_V3_SIZE 104

/* The specification fixes the cluster size at 512 bytes .. 2 MB. */
#define XX_QCOW_MIN_CLUSTER_BITS 9U
#define XX_QCOW_MAX_CLUSTER_BITS 21U

/* An L1 entry covers cluster_size * (cluster_size / 8) guest bytes, so even
 * the smallest cluster needs only 2^30 entries for a 2^48 byte disk. Capping
 * the table at 4 M entries bounds the one allocation this reader makes at
 * 32 MB and still covers every image a sane tool produces. */
#define XX_QCOW_MAX_L1_ENTRIES UINT32_C(0x400000)

/* Guest disks larger than 16 TB are refused rather than listed: the member's
 * size is published as a byte count and extraction writes every byte. */
#define XX_QCOW_MAX_VIRTUAL_SIZE ((uint64_t)1 << 44)

/* Bits 9..55 of an L1 or of a plain L2 entry. */
#define XX_QCOW_OFFSET_MASK UINT64_C(0x00fffffffffffe00)
#define XX_QCOW_FLAG_COMPRESSED (UINT64_C(1) << 62)
#define XX_QCOW_FLAG_ZERO UINT64_C(1)

/* How many guest clusters the no-destination probe decodes. See
 * xx_qcow_write_image(). */
#define XX_QCOW_PROBE_CLUSTERS UINT64_C(64)

#define XX_QCOW_MAX_BACKING_NAME 1024U

/* The member name. QCOW images carry no name of their own. */
#define XX_QCOW_MEMBER_NAME "disk.img"

typedef struct xx_qcow_private_s {
    uint64_t *l1_table;          /**< l1_count entries, host byte offsets. */
    char *backing_file;          /**< Owned, or NULL. */
    int64_t input_size;
    int64_t base_address;
    uint64_t virtual_size;
    uint64_t l1_table_offset;
    uint64_t refcount_table_offset;
    uint64_t snapshots_offset;
    uint64_t incompatible_features;
    uint32_t l1_count;           /**< Entries actually read into l1_table. */
    uint32_t l1_size;            /**< The header's declared entry count. */
    uint32_t version;
    uint32_t cluster_bits;
    uint32_t cluster_size;
    uint32_t l2_bits;            /**< cluster_bits - 3. */
    uint32_t crypt_method;
    uint32_t nb_snapshots;
    uint32_t refcount_order;
    uint8_t compression_type;
    bool consumed;               /**< The single member has been stepped past. */
} xx_qcow_private;

static void xx_qcow_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

/* Every read goes through xx_io_seek64: a disk image routinely exceeds 2 GB
 * and long is 32 bits on Win64, so xx_io_seek() would truncate the offset. */
static bool xx_qcow_read_at(xx_io_device *device, int64_t offset, void *data,
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

/* True when [offset, offset + size) lies inside [0, total_size). */
static bool xx_qcow_range_within(int64_t total_size, int64_t offset,
                                 int64_t size) {
    return (total_size >= 0) && (offset >= 0) && (size >= 0) &&
           (offset <= total_size) && (size <= total_size - offset);
}

/* Convert a host offset taken off the disk into a device offset, refusing
 * anything that does not fit or that runs past the end of the image. */
static bool xx_qcow_host_range(const xx_qcow_private *parsed, uint64_t host,
                               int64_t size, int64_t *out_offset) {
    int64_t offset;

    if (!parsed || !out_offset || host > (uint64_t)INT64_MAX) return false;
    offset = (int64_t)host;
    if (offset > INT64_MAX - parsed->base_address) return false;
    offset += parsed->base_address;
    if (!xx_qcow_range_within(parsed->input_size, offset, size)) return false;
    *out_offset = offset;
    return true;
}

static void xx_qcow_private_cleanup(xx_qcow_private *parsed) {
    if (!parsed) return;
    if (parsed->l1_table) xx_mem_free(parsed->l1_table);
    if (parsed->backing_file) xx_str_free(parsed->backing_file);
    xx_mem_zero(parsed, sizeof(*parsed));
    parsed->input_size = -1;
}

static void xx_qcow_private_free(void *pointer) {
    xx_qcow_private *parsed = (xx_qcow_private *)pointer;

    if (!parsed) return;
    xx_qcow_private_cleanup(parsed);
    xx_mem_free(parsed);
}

/* The backing file name is plain bytes, not NUL terminated. It is published
 * for information only - this reader never opens it - so control characters
 * and separators are simply refused rather than sanitised. */
static char *xx_qcow_read_backing_file(xx_io_device *device,
                                       const xx_qcow_private *parsed,
                                       uint64_t offset, uint32_t size) {
    int64_t at;
    char *name;
    uint32_t index;

    if (size == 0U || size > XX_QCOW_MAX_BACKING_NAME) return NULL;
    if (!xx_qcow_host_range(parsed, offset, (int64_t)size, &at)) return NULL;
    name = (char *)xx_mem_alloc((size_t)size + 1U);
    if (!name) return NULL;
    if (!xx_qcow_read_at(device, at, name, (size_t)size)) {
        xx_mem_free(name);
        return NULL;
    }
    name[size] = '\0';
    for (index = 0U; index < size; ++index) {
        if ((unsigned char)name[index] < 32U) {
            xx_mem_free(name);
            return NULL;
        }
    }
    return name;
}

/* ---------------------------------------------------------------- parse -- */

static bool xx_qcow_parse(Abstractformat *self, xx_qcow_private *parsed,
                          xx_pd_struct *pd) {
    uint8_t header[XX_QCOW_HEADER_V3_SIZE + 8];
    uint8_t *raw_l1 = NULL;
    int64_t total_size;
    int64_t l1_at;
    uint64_t backing_offset;
    uint64_t clusters_per_l1;
    uint64_t needed_l1;
    uint32_t backing_size;
    uint32_t header_length = XX_QCOW_HEADER_V2_SIZE;
    uint32_t index;

    /* Initialised before the guard clauses: xx_qcow_check_is_valid() cleans
     * up its stack copy whatever this returns. */
    if (parsed) {
        xx_mem_zero(parsed, sizeof(*parsed));
        parsed->input_size = -1;
    }
    if (!self || !self->device || !parsed || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    total_size = xx_io_total_size(self->device);
    if (!xx_qcow_range_within(total_size, self->base_address,
                              XX_QCOW_HEADER_V2_SIZE) ||
        !xx_qcow_read_at(self->device, self->base_address, header,
                         XX_QCOW_HEADER_V2_SIZE)) {
        return false;
    }
    if (xx_data_get_u32(header, sizeof(header), 0U, true) != XX_QCOW_MAGIC) {
        return false;
    }
    parsed->input_size = total_size;
    parsed->base_address = self->base_address;
    parsed->version = xx_data_get_u32(header, sizeof(header), 4U, true);
    /* QCOW version 1 shares the magic but not the header: its cluster_bits is
     * a single byte at +32, there is no refcount table, and the L2 entry has
     * no flag bits. Guessing between the two off a shared magic would mean
     * reading v1 fields out of v2 offsets, so v1 is refused outright. Nothing
     * else downstream has to wonder which dialect it is looking at. */
    if (parsed->version != 2U && parsed->version != 3U) goto fail;

    backing_offset = xx_data_get_u64(header, sizeof(header), 8U, true);
    backing_size = xx_data_get_u32(header, sizeof(header), 16U, true);
    parsed->cluster_bits = xx_data_get_u32(header, sizeof(header), 20U, true);
    parsed->virtual_size = xx_data_get_u64(header, sizeof(header), 24U, true);
    parsed->crypt_method = xx_data_get_u32(header, sizeof(header), 32U, true);
    parsed->l1_size = xx_data_get_u32(header, sizeof(header), 36U, true);
    parsed->l1_table_offset = xx_data_get_u64(header, sizeof(header), 40U, true);
    parsed->refcount_table_offset =
        xx_data_get_u64(header, sizeof(header), 48U, true);
    parsed->nb_snapshots = xx_data_get_u32(header, sizeof(header), 60U, true);
    parsed->snapshots_offset = xx_data_get_u64(header, sizeof(header), 64U, true);
    parsed->refcount_order = 4U; /* The version 2 constant. */

    if (parsed->cluster_bits < XX_QCOW_MIN_CLUSTER_BITS ||
        parsed->cluster_bits > XX_QCOW_MAX_CLUSTER_BITS) {
        goto fail;
    }
    parsed->cluster_size = UINT32_C(1) << parsed->cluster_bits;
    parsed->l2_bits = parsed->cluster_bits - 3U;
    if (parsed->virtual_size > XX_QCOW_MAX_VIRTUAL_SIZE) goto fail;
    if (parsed->l1_size > XX_QCOW_MAX_L1_ENTRIES) goto fail;

    if (parsed->version == 3U) {
        if (!xx_qcow_range_within(total_size, self->base_address,
                                  XX_QCOW_HEADER_V3_SIZE) ||
            !xx_qcow_read_at(self->device, self->base_address, header,
                             XX_QCOW_HEADER_V3_SIZE)) {
            goto fail;
        }
        parsed->incompatible_features =
            xx_data_get_u64(header, sizeof(header), 72U, true);
        parsed->refcount_order = xx_data_get_u32(header, sizeof(header), 96U, true);
        header_length = xx_data_get_u32(header, sizeof(header), 100U, true);
        /* compression_type only exists once the header is long enough to
         * hold it; older version 3 writers stop at 104 and mean deflate. */
        if (header_length > XX_QCOW_HEADER_V3_SIZE) {
            uint8_t type = 0U;
            if (!xx_qcow_range_within(total_size,
                                      self->base_address +
                                          XX_QCOW_HEADER_V3_SIZE,
                                      1) ||
                !xx_qcow_read_at(self->device,
                                 self->base_address + XX_QCOW_HEADER_V3_SIZE,
                                 &type, 1U)) {
                goto fail;
            }
            parsed->compression_type = type;
        }
        /* refcount_order is bounded by the spec; a value this reader does not
         * understand does not affect guest data, which never goes through the
         * refcount tables, so it is only sanity checked. */
        if (parsed->refcount_order > 6U) goto fail;
        if (header_length < XX_QCOW_HEADER_V3_SIZE) goto fail;
    }
    /* Only deflate and zstd are defined. An unknown value would make every
     * compressed cluster undecodable, so the image is refused up front rather
     * than half way through an extraction. */
    if (parsed->compression_type > 1U) goto fail;

    /* The L1 table has to cover the whole guest address space. A short table
     * is accepted - the clusters it does not reach simply read as zeros - but
     * a table claiming far more entries than the disk could ever need is a
     * sign of a crafted header. */
    clusters_per_l1 = (uint64_t)1 << (parsed->cluster_bits + parsed->l2_bits);
    needed_l1 = (parsed->virtual_size + clusters_per_l1 - 1U) / clusters_per_l1;
    if (needed_l1 > XX_QCOW_MAX_L1_ENTRIES) goto fail;
    parsed->l1_count = parsed->l1_size;
    if ((uint64_t)parsed->l1_count > needed_l1) {
        parsed->l1_count = (uint32_t)needed_l1;
    }

    if (parsed->l1_count != 0U) {
        size_t bytes = (size_t)parsed->l1_count * 8U;
        if (parsed->l1_table_offset == 0U ||
            (parsed->l1_table_offset & (uint64_t)(parsed->cluster_size - 1U)) !=
                0U ||
            !xx_qcow_host_range(parsed, parsed->l1_table_offset, (int64_t)bytes,
                                &l1_at)) {
            goto fail;
        }
        raw_l1 = (uint8_t *)xx_mem_alloc(bytes);
        parsed->l1_table =
            (uint64_t *)xx_mem_calloc(parsed->l1_count, sizeof(uint64_t));
        if (!raw_l1 || !parsed->l1_table) goto fail;
        if (!xx_qcow_read_at(self->device, l1_at, raw_l1, bytes)) goto fail;
        for (index = 0U; index < parsed->l1_count; ++index) {
            parsed->l1_table[index] =
                xx_data_get_u64(raw_l1, bytes, (size_t)index * 8U, true) &
                XX_QCOW_OFFSET_MASK;
        }
        xx_mem_free(raw_l1);
        raw_l1 = NULL;
    }

    if (backing_offset != 0U && backing_size != 0U) {
        parsed->backing_file = xx_qcow_read_backing_file(
            self->device, parsed, backing_offset, backing_size);
        /* An unreadable backing name is metadata damage, not a reason to
         * refuse the image: the guest clusters are still addressable. */
    }
    return true;

fail:
    if (raw_l1) xx_mem_free(raw_l1);
    xx_qcow_private_cleanup(parsed);
    return false;
}

/* --------------------------------------------------------------- reader -- */

/* Produce one guest cluster into out, which is cluster_size bytes long.
 * Returns false only on a hard error; a cluster that is not present is
 * reported as zeros, which is what the guest would see. */
static bool xx_qcow_read_cluster(Abstractformat *self,
                                 const xx_qcow_private *parsed,
                                 uint64_t cluster_index, uint8_t *out,
                                 uint8_t *scratch) {
    uint64_t l1_index = cluster_index >> parsed->l2_bits;
    uint64_t l2_index = cluster_index & (((uint64_t)1 << parsed->l2_bits) - 1U);
    uint64_t l2_table;
    uint64_t entry;
    uint8_t raw[8];
    int64_t at;

    xx_mem_zero(out, parsed->cluster_size);
    if (l1_index >= (uint64_t)parsed->l1_count || !parsed->l1_table) return true;
    l2_table = parsed->l1_table[l1_index];
    if (l2_table == 0U) return true;
    /* An L2 table is exactly one cluster and is cluster aligned. Checking the
     * alignment here is what stops an entry that points into the middle of
     * the L1 table, or into the header, from being treated as a table. */
    if ((l2_table & (uint64_t)(parsed->cluster_size - 1U)) != 0U) return true;
    if (!xx_qcow_host_range(parsed, l2_table, (int64_t)parsed->cluster_size,
                            &at)) {
        return true;
    }
    /* Only the one entry is read. The L2 table is never materialised, so a
     * hostile L1 entry cannot drive an allocation, and because the caller
     * walks output clusters in a plain loop there is no recursion for an
     * entry pointing back at the L1 table to exploit. */
    if (!xx_qcow_read_at(self->device, at + (int64_t)(l2_index * 8U), raw,
                         sizeof(raw))) {
        return false;
    }
    entry = xx_data_get_u64(raw, sizeof(raw), 0U, true);
    if (entry == 0U) return true;

    if ((entry & XX_QCOW_FLAG_COMPRESSED) != 0U) {
        uint32_t csize_shift = 62U - (parsed->cluster_bits - 8U);
        uint64_t csize_mask = ((uint64_t)1 << (parsed->cluster_bits - 8U)) - 1U;
        uint64_t host = entry & (((uint64_t)1 << csize_shift) - 1U);
        uint64_t sectors = ((entry >> csize_shift) & csize_mask) + 1U;
        int64_t available = (int64_t)(sectors * 512U) - (int64_t)(host & 511U);
        size_t written = 0U;

        if (available <= 0 || available > (int64_t)parsed->cluster_size * 2 +
                                              512) {
            return true;
        }
        if (!xx_qcow_host_range(parsed, host, available, &at)) return true;
        if (!xx_qcow_read_at(self->device, at, scratch, (size_t)available)) {
            return false;
        }
        if (parsed->compression_type == 1U) {
            /* The L2 entry gives the payload's extent rounded up to whole
             * 512-byte sectors, so the buffer normally carries padding after
             * the frame - and xx_zstd_decompress_memory() refuses any input
             * with trailing bytes, having no "bytes consumed" variant to say
             * where the frame ended. Rather than reimplement a frame-length
             * parser, the full extent is tried first and, if it is rejected,
             * the trailing zero padding is dropped and it is tried once more.
             * Two attempts, no search. A cluster padded with NON-zero bytes
             * would still fail; see the reader's notes. */
            size_t exact = (size_t)available;
            if (!xx_zstd_decompress_memory(scratch, exact, out,
                                           parsed->cluster_size, &written)) {
                while (exact != 0U && scratch[exact - 1U] == 0U) --exact;
                if (exact == 0U ||
                    !xx_zstd_decompress_memory(scratch, exact, out,
                                               parsed->cluster_size,
                                               &written)) {
                    return false;
                }
            }
        } else if (!xx_deflate_decompress_memory(scratch, (size_t)available,
                                                 out, parsed->cluster_size,
                                                 &written, false)) {
            /* Raw deflate, no zlib wrapper: is_deflate64 is false and the
             * zlib entry point is deliberately not used. */
            return false;
        }
        /* A short stream leaves the tail of the cluster zeroed, which is what
         * the buffer was primed with. */
        return true;
    }
    if ((entry & XX_QCOW_FLAG_ZERO) != 0U && parsed->version == 3U) {
        return true; /* Explicit zero cluster. */
    }
    l2_table = entry & XX_QCOW_OFFSET_MASK;
    if (l2_table == 0U) return true;
    if ((l2_table & (uint64_t)(parsed->cluster_size - 1U)) != 0U) return true;
    if (!xx_qcow_host_range(parsed, l2_table, (int64_t)parsed->cluster_size,
                            &at)) {
        return true;
    }
    return xx_qcow_read_at(self->device, at, out, parsed->cluster_size);
}

/* Walk the guest address space. When output is NULL nothing is written and
 * the walk stops after XX_QCOW_PROBE_CLUSTERS clusters: the virtual size is a
 * header field, so a crafted image can claim sixteen terabytes in a file of a
 * few kilobytes, and decoding every cluster of that just to answer "is this
 * readable?" is a denial of service with no destination to show for it. With
 * a destination the caller has asked for the whole disk and gets it. */
static bool xx_qcow_write_image(Abstractformat *self,
                                const xx_qcow_private *parsed,
                                xx_io_device *output, xx_pd_struct *pd) {
    uint8_t *cluster;
    uint8_t *scratch;
    uint64_t remaining = parsed->virtual_size;
    uint64_t index = 0U;
    uint64_t budget = output ? UINT64_MAX : XX_QCOW_PROBE_CLUSTERS;
    size_t scratch_size = (size_t)parsed->cluster_size * 2U + 512U;
    bool result = true;

    cluster = (uint8_t *)xx_mem_alloc(parsed->cluster_size);
    scratch = (uint8_t *)xx_mem_alloc(scratch_size);
    if (!cluster || !scratch) {
        if (cluster) xx_mem_free(cluster);
        if (scratch) xx_mem_free(scratch);
        return false;
    }
    while (remaining != 0U && budget != 0U) {
        size_t chunk = remaining < (uint64_t)parsed->cluster_size
                           ? (size_t)remaining
                           : (size_t)parsed->cluster_size;
        size_t done = 0U;

        if (pd && xx_pd_is_stopped(pd)) {
            result = false;
            break;
        }
        if (!xx_qcow_read_cluster(self, parsed, index, cluster, scratch)) {
            result = false;
            break;
        }
        while (output && done < chunk) {
            ssize_t sent = xx_io_write(output, cluster + done, chunk - done);
            if (sent <= 0 || (size_t)sent > chunk - done) {
                result = false;
                break;
            }
            done += (size_t)sent;
        }
        if (!result) break;
        remaining -= (uint64_t)chunk;
        ++index;
        if (budget != UINT64_MAX) --budget;
    }
    xx_mem_free(cluster);
    xx_mem_free(scratch);
    return result;
}

/* ------------------------------------------------------------ lifecycle -- */

void xx_qcow_init(xx_qcow *qcow, xx_io_device *dev, int64_t base_address) {
    if (!qcow) return;
    xx_mem_zero(qcow, sizeof(*qcow));
    xx_format_init(&qcow->format, dev, base_address);
    qcow->format.endian = XX_ENDIAN_BIG;
    qcow->format.file_type = XX_QCOW_FILE_TYPE;
    qcow->format.format_type = XX_TYPE_ARCHIVE;
    qcow->format.is_archive = true;
    xx_format_set_mime_type(&qcow->format, "application/x-qemu-disk");
    xx_format_set_extension(&qcow->format, "qcow2");
    qcow->format.check_is_valid = xx_qcow_check_is_valid;
    qcow->format.handle_base_info = xx_qcow_handle_base_info;
    qcow->format.get_format_size = xx_qcow_get_format_size;
    qcow->format.get_number_of_archive_records =
        xx_qcow_get_number_of_archive_records;
    qcow->format.create_archive_records_reading =
        xx_qcow_create_archive_records_reading;
    qcow->format.get_current_archive_record = xx_qcow_get_current_archive_record;
    qcow->format.unpack_current_archive_record =
        xx_qcow_unpack_current_archive_record;
    qcow->format.archive_record_move_to_next = xx_qcow_archive_record_move_to_next;
    qcow->format.free_archive_records_reading =
        xx_qcow_free_archive_records_reading;
    qcow->format.destroy = xx_qcow_vtable_destroy;
}

xx_qcow *xx_qcow_create(xx_io_device *dev, int64_t base_address) {
    xx_qcow *qcow = (xx_qcow *)xx_mem_alloc(sizeof(*qcow));

    if (qcow) xx_qcow_init(qcow, dev, base_address);
    return qcow;
}

void xx_qcow_destroy(xx_qcow *qcow) {
    if (!qcow) return;
    if (qcow->internal) {
        xx_qcow_private_free(qcow->internal);
        qcow->internal = NULL;
    }
    xx_format_cleanup_extra_parameters(&qcow->format);
}

static void xx_qcow_vtable_destroy(Abstractformat *self) {
    xx_qcow_destroy((xx_qcow *)self);
}

void xx_qcow_free(xx_qcow *qcow) {
    if (!qcow) return;
    xx_qcow_destroy(qcow);
    xx_mem_free(qcow);
}

/* --------------------------------------------------------------- format -- */

bool xx_qcow_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_qcow_private parsed;
    bool result = xx_qcow_parse(self, &parsed, pd);

    xx_qcow_private_cleanup(&parsed);
    return result;
}

bool xx_qcow_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_qcow *qcow = (xx_qcow *)self;
    xx_qcow_private *parsed;

    if (!self || !qcow) return false;
    parsed = (xx_qcow_private *)xx_mem_alloc(sizeof(*parsed));
    if (!parsed || !xx_qcow_parse(self, parsed, pd)) {
        if (parsed) xx_mem_free(parsed);
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    if (qcow->internal) xx_qcow_private_free(qcow->internal);
    qcow->internal = parsed;
    qcow->number_of_records = 1U;
    qcow->virtual_size = parsed->virtual_size;
    qcow->l1_table_offset = parsed->l1_table_offset;
    qcow->refcount_table_offset = parsed->refcount_table_offset;
    qcow->snapshots_offset = parsed->snapshots_offset;
    qcow->incompatible_features = parsed->incompatible_features;
    qcow->version = parsed->version;
    qcow->cluster_bits = parsed->cluster_bits;
    qcow->cluster_size = parsed->cluster_size;
    qcow->crypt_method = parsed->crypt_method;
    qcow->l1_size = parsed->l1_size;
    qcow->nb_snapshots = parsed->nb_snapshots;
    qcow->refcount_order = parsed->refcount_order;
    qcow->compression_type = parsed->compression_type;
    qcow->has_backing_file = parsed->backing_file != NULL;
    qcow->is_encrypted = parsed->crypt_method != 0U;
    /* Host clusters can be written anywhere in the file and the refcount
     * tables live in it too, so the whole device is the format. */
    self->format_size = parsed->input_size - self->base_address;
    self->overlay_offset = -1;
    self->overlay_size = 0;
    self->number_of_archive_records = 1U;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_qcow_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return -1;
    }
    return self->format_size;
}

uint64_t xx_qcow_get_number_of_archive_records(Abstractformat *self,
                                               xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return ((xx_qcow *)self)->number_of_records;
}

/* -------------------------------------------------------------- records -- */

static bool xx_qcow_copy_options(xx_list_s *destination,
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

static const xx_var *xx_qcow_find_option(const xx_list_s *options,
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

static bool xx_qcow_populate_record(xx_archive_record *record,
                                    const xx_qcow_private *parsed) {
    if (!record || !parsed) return false;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = parsed->base_address;
    record->header_size = parsed->version == 3U ? XX_QCOW_HEADER_V3_SIZE
                                                : XX_QCOW_HEADER_V2_SIZE;
    /* The guest image is scattered across the file, so there is no single
     * data extent; the record points at the L1 table, which is where reading
     * it begins. */
    record->data_offset = (int64_t)parsed->l1_table_offset + parsed->base_address;
    record->compressed_size = parsed->input_size - parsed->base_address;
    if (!xx_archive_record_set_original_name(record, XX_QCOW_MEMBER_NAME) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                        parsed->virtual_size) ||
        !xx_archive_record_set_meta_u64(
            record, XX_META_ID_COMPRESSED_SIZE,
            (uint64_t)(parsed->input_size - parsed->base_address)) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                        parsed->compression_type) ||
        !xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false) ||
        /* crypt_method != 0 means the guest clusters are ciphertext and this
         * reader will not produce plaintext for them. */
        !xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                         parsed->crypt_method != 0U) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_ENCRYPTION_METHOD,
                                        parsed->crypt_method)) {
        return false;
    }
    if (parsed->backing_file &&
        !xx_archive_record_set_meta_str(record, XX_META_ID_COMMENT,
                                        parsed->backing_file)) {
        return false;
    }
    return true;
}

xx_archive_record_state *xx_qcow_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_record_state *state;
    xx_qcow_private *parsed;

    if (!self || !self->device ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    parsed = (xx_qcow_private *)xx_mem_calloc(1U, sizeof(*parsed));
    if (!state || !parsed) {
        if (state) xx_mem_free(state);
        if (parsed) xx_mem_free(parsed);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    if (!xx_qcow_copy_options(&state->options, options) ||
        !xx_qcow_parse(self, parsed, pd)) {
        xx_qcow_private_free(parsed);
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->internal_state = parsed;
    state->free_internal = xx_qcow_private_free;
    state->total_records = 1;
    if (!xx_qcow_populate_record(&state->current_record, parsed)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_qcow_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_qcow_archive_record_move_to_next(Abstractformat *self,
                                         xx_archive_record_state *state,
                                         xx_pd_struct *pd) {
    xx_qcow_private *parsed;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    /* One member, so the first step is always the last. */
    parsed = (xx_qcow_private *)state->internal_state;
    if (parsed) parsed->consumed = true;
    xx_archive_record_cleanup(&state->current_record);
    xx_archive_record_init(&state->current_record);
    state->has_record = false;
    return false;
}

bool xx_qcow_unpack_current_archive_record(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_qcow_private *parsed;
    const xx_var *option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *destination = NULL;
    xx_io_device *output = NULL;
    bool result;
    bool created = false;

    if (!self || !self->device || !state || state->format != self ||
        !state->has_record || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    parsed = (xx_qcow_private *)state->internal_state;
    if (!parsed || parsed->consumed) return false;
    /* An encrypted image's clusters are ciphertext. Writing them out as if
     * they were the guest disk would be worse than refusing. */
    if (parsed->crypt_method != 0U) return false;

    option = xx_qcow_find_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!option) {
        /* No destination: decode every cluster and discard it, which is a
         * real check that the tables and the compressed streams hold up. */
        return xx_qcow_write_image(self, parsed, NULL, pd);
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
    if (base[0] != '\0' && base[xx_str_len(base) - 1U] != '/' &&
        base[xx_str_len(base) - 1U] != '\\') {
        destination = xx_str_concat3(base, "/", XX_QCOW_MEMBER_NAME);
    } else {
        destination = xx_str_concat(base, XX_QCOW_MEMBER_NAME);
    }
    if (owned_base) xx_str_free(owned_base);
    if (!destination) return false;
    if (!xx_store_create_dirs_a(destination, false)) {
        xx_str_free(destination);
        return false;
    }
    output = xx_io_file_open(destination, "wb");
    created = output != NULL;
    result = output != NULL && xx_qcow_write_image(self, parsed, output, pd);
    if (output && xx_io_close(output) != 0) result = false;
    if (!result && created) xx_rt_remove(destination);
    xx_str_free(destination);
    return result;
}

void xx_qcow_free_archive_records_reading(Abstractformat *self,
                                          xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

/* ------------------------------------------------------------ accessors -- */

uint64_t xx_qcow_get_virtual_size(const xx_qcow *qcow) {
    return qcow ? qcow->virtual_size : 0U;
}
uint32_t xx_qcow_get_version(const xx_qcow *qcow) {
    return qcow ? qcow->version : 0U;
}
uint32_t xx_qcow_get_cluster_size(const xx_qcow *qcow) {
    return qcow ? qcow->cluster_size : 0U;
}
uint32_t xx_qcow_get_crypt_method(const xx_qcow *qcow) {
    return qcow ? qcow->crypt_method : 0U;
}
const char *xx_qcow_get_backing_file(const xx_qcow *qcow) {
    const xx_qcow_private *parsed =
        qcow ? (const xx_qcow_private *)qcow->internal : NULL;
    return parsed ? parsed->backing_file : NULL;
}
