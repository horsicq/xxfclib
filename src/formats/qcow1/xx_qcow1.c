/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * QEMU QCOW version 1 disk images. The field layout is documented in
 * xx_qcow1.h. This is original code written from the format's structure;
 * its shape follows the QCOW2 reader next door (src/formats/qcow).
 *
 * The reader publishes ONE member, the reconstructed guest disk, whose size
 * is the header's virtual size. Producing it walks the L1/L2 tables cluster
 * by cluster: an unallocated cluster is written out as zeros, a plain
 * cluster is copied, and a compressed cluster (L2 bit 63) is inflated. The
 * compressed payload is RAW deflate with no zlib header, so
 * xx_deflate_decompress_memory() is the entry point.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/qcow1/xx_qcow1.h"

#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>
#include "../xx_disk_crypto_private.h"

#ifdef QCOW1
#define XX_QCOW1_FILE_TYPE XX_FILE_TYPE_QCOW1
#else
#define XX_QCOW1_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define XX_QCOW1_MAGIC UINT32_C(0x514649fb)
#define XX_QCOW1_HEADER_SIZE 48

/* The bounds QEMU itself enforces: clusters of 512 bytes .. 64 KB and L2
 * tables of at most 64 KB (8 << l2_bits bytes). */
#define XX_QCOW1_MIN_CLUSTER_BITS 9U
#define XX_QCOW1_MAX_CLUSTER_BITS 16U
#define XX_QCOW1_MIN_L2_BITS 6U
#define XX_QCOW1_MAX_L2_BITS 13U

/* 4 M L1 entries (32 MB) covers, at the smallest geometry (2^15 guest bytes
 * per L1 entry), a 128 GB disk and, at QEMU's default geometry (2^21), 8 TB.
 * The table must also be present in the file before it is allocated. */
#define XX_QCOW1_MAX_L1_ENTRIES UINT32_C(0x400000)

/* Guest disks larger than 16 TB are refused. */
#define XX_QCOW1_MAX_VIRTUAL_SIZE ((uint64_t)1 << 44)

#define XX_QCOW1_FLAG_COMPRESSED (UINT64_C(1) << 63)
#define XX_QCOW1_MAX_BACKING_NAME 1024U
#define XX_QCOW1_MEMBER_NAME "disk.img"

/* Output staging buffer; a multiple of every legal cluster size. */
#define XX_QCOW1_STAGE_SIZE ((size_t)1 << 20)

typedef struct xx_qcow1_private_s {
    uint64_t *l1_table;          /**< l1_size entries, host byte offsets. */
    char *backing_file;          /**< Owned, or NULL. */
    int64_t input_size;
    int64_t base_address;
    uint64_t virtual_size;
    uint64_t l1_table_offset;
    uint32_t l1_size;
    uint32_t cluster_bits;
    uint32_t cluster_size;
    uint32_t l2_bits;
    uint32_t crypt_method;
    uint32_t mtime;
    bool consumed;
} xx_qcow1_private;

static void xx_qcow1_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_qcow1_read_at(xx_io_device *device, int64_t offset, void *data,
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

static bool xx_qcow1_range_within(int64_t total_size, int64_t offset,
                                  int64_t size) {
    return (total_size >= 0) && (offset >= 0) && (size >= 0) &&
           (offset <= total_size) && (size <= total_size - offset);
}

/* Host offset (relative to the image start) -> device offset, refusing
 * anything that does not fit or that runs past the end of the device. */
static bool xx_qcow1_host_range(const xx_qcow1_private *parsed, uint64_t host,
                                int64_t size, int64_t *out_offset) {
    int64_t offset;

    if (!parsed || !out_offset || host > (uint64_t)INT64_MAX) return false;
    offset = (int64_t)host;
    if (offset > INT64_MAX - parsed->base_address) return false;
    offset += parsed->base_address;
    if (!xx_qcow1_range_within(parsed->input_size, offset, size)) return false;
    *out_offset = offset;
    return true;
}

static void xx_qcow1_private_cleanup(xx_qcow1_private *parsed) {
    if (!parsed) return;
    if (parsed->l1_table) xx_mem_free(parsed->l1_table);
    if (parsed->backing_file) xx_mem_free(parsed->backing_file);
    xx_mem_zero(parsed, sizeof(*parsed));
    parsed->input_size = -1;
}

static void xx_qcow1_private_free(void *pointer) {
    xx_qcow1_private *parsed = (xx_qcow1_private *)pointer;

    if (!parsed) return;
    xx_qcow1_private_cleanup(parsed);
    xx_mem_free(parsed);
}

/* Informational only; never opened. Control characters are refused. */
static char *xx_qcow1_read_backing_file(xx_io_device *device,
                                        const xx_qcow1_private *parsed,
                                        uint64_t offset, uint32_t size) {
    int64_t at;
    char *name;
    uint32_t index;

    if (size == 0U || size > XX_QCOW1_MAX_BACKING_NAME) return NULL;
    if (!xx_qcow1_host_range(parsed, offset, (int64_t)size, &at)) return NULL;
    name = (char *)xx_mem_alloc((size_t)size + 1U);
    if (!name) return NULL;
    if (!xx_qcow1_read_at(device, at, name, (size_t)size)) {
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

static bool xx_qcow1_parse_impl(Abstractformat *self, xx_qcow1_private *parsed,
                           const xx_list_s *options, xx_pd_struct *pd) {
    uint8_t header[XX_QCOW1_HEADER_SIZE];
    uint8_t *raw_l1 = NULL;
    int64_t total_size;
    int64_t l1_at;
    uint64_t backing_offset;
    uint64_t per_l1;
    uint64_t needed_l1;
    uint32_t backing_size;
    uint32_t index;

    if (parsed) {
        xx_mem_zero(parsed, sizeof(*parsed));
        parsed->input_size = -1;
    }
    if (!self || !self->device || !parsed || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    total_size = xx_io_total_size(self->device);
    if (!xx_qcow1_range_within(total_size, self->base_address,
                               XX_QCOW1_HEADER_SIZE) ||
        !xx_qcow1_read_at(self->device, self->base_address, header,
                          XX_QCOW1_HEADER_SIZE)) {
        return false;
    }
    if (xx_data_get_u32(header, sizeof(header), 0U, true) != XX_QCOW1_MAGIC ||
        xx_data_get_u32(header, sizeof(header), 4U, true) != 1U) {
        return false;
    }
    parsed->input_size = total_size;
    parsed->base_address = self->base_address;
    backing_offset = xx_data_get_u64(header, sizeof(header), 8U, true);
    backing_size = xx_data_get_u32(header, sizeof(header), 16U, true);
    parsed->mtime = xx_data_get_u32(header, sizeof(header), 20U, true);
    parsed->virtual_size = xx_data_get_u64(header, sizeof(header), 24U, true);
    parsed->cluster_bits = header[32];
    parsed->l2_bits = header[33];
    parsed->crypt_method = xx_data_get_u32(header, sizeof(header), 36U, true);
    parsed->l1_table_offset = xx_data_get_u64(header, sizeof(header), 40U, true);

    if (parsed->cluster_bits < XX_QCOW1_MIN_CLUSTER_BITS ||
        parsed->cluster_bits > XX_QCOW1_MAX_CLUSTER_BITS ||
        parsed->l2_bits < XX_QCOW1_MIN_L2_BITS ||
        parsed->l2_bits > XX_QCOW1_MAX_L2_BITS ||
        parsed->crypt_method > 1U ||
        parsed->virtual_size > XX_QCOW1_MAX_VIRTUAL_SIZE) {
        goto fail;
    }
    parsed->cluster_size = UINT32_C(1) << parsed->cluster_bits;

    /* The L1 size is not stored; it follows from the virtual size. */
    per_l1 = (uint64_t)1 << (parsed->cluster_bits + parsed->l2_bits);
    needed_l1 = (parsed->virtual_size + per_l1 - 1U) / per_l1;
    if (needed_l1 > XX_QCOW1_MAX_L1_ENTRIES) goto fail;
    parsed->l1_size = (uint32_t)needed_l1;

    if (parsed->l1_size != 0U) {
        size_t bytes = (size_t)parsed->l1_size * 8U;
        /* The table sits after the header (QEMU puts it right behind the
         * header and backing name, 8-byte aligned). */
        if (parsed->l1_table_offset < (uint64_t)XX_QCOW1_HEADER_SIZE ||
            !xx_qcow1_host_range(parsed, parsed->l1_table_offset,
                                 (int64_t)bytes, &l1_at)) {
            goto fail;
        }
        { uint64_t limit, live_bytes=0; const xx_qcow1_private *live=(const xx_qcow1_private*)((xx_qcow1*)self)->internal;
          if(live) live_bytes=(uint64_t)live->l1_size*8U+sizeof(*live)+1024U;
          if (!dc_limit(self, options, XX_META_ID_OPT_MEMORY_LIMIT, UINT64_MAX, &limit) || (uint64_t)bytes * 2U + sizeof(*parsed) + live_bytes > limit) goto fail; }
        raw_l1 = (uint8_t *)xx_mem_alloc(bytes);
        parsed->l1_table =
            (uint64_t *)xx_mem_calloc(parsed->l1_size, sizeof(uint64_t));
        if (!raw_l1 || !parsed->l1_table) goto fail;
        if (!xx_qcow1_read_at(self->device, l1_at, raw_l1, bytes)) goto fail;
        for (index = 0U; index < parsed->l1_size; ++index) {
            parsed->l1_table[index] =
                xx_data_get_u64(raw_l1, bytes, (size_t)index * 8U, true);
        }
        xx_mem_free(raw_l1);
        raw_l1 = NULL;
    }

    if (backing_offset != 0U && backing_size != 0U) {
        parsed->backing_file = xx_qcow1_read_backing_file(
            self->device, parsed, backing_offset, backing_size);
    }
    return true;

fail:
    if (raw_l1) xx_mem_free(raw_l1);
    xx_qcow1_private_cleanup(parsed);
    return false;
}

static bool xx_qcow1_parse(Abstractformat *self, xx_qcow1_private *parsed, const xx_list_s *options, xx_pd_struct *pd) {
    int64_t cursor=self&&self->device?xx_io_tell(self->device):-1; bool ok=xx_qcow1_parse_impl(self,parsed,options,pd);
    if(cursor>=0&&xx_io_seek64(self->device,cursor,SEEK_SET)!=0) { xx_qcow1_private_cleanup(parsed); ok=false; } return ok;
}

/* --------------------------------------------------------------- reader -- */

/* The L2 table the walk is currently in. At most 8 << 13 = 64 KB. */
typedef struct xx_qcow1_l2_cache_s {
    uint8_t *table;
    uint64_t l1_index;
    bool valid;
} xx_qcow1_l2_cache;

/* Produce one guest cluster into out (cluster_size bytes). An unallocated
 * cluster reads as zeros; a pointer outside the image is corruption and
 * fails the extraction rather than being silently zero-filled. */
static bool xx_qcow1_read_cluster(Abstractformat *self,
                                  const xx_qcow1_private *parsed,
                                  uint64_t cluster_index, uint8_t *out,
                                  uint8_t *scratch, xx_qcow1_l2_cache *cache, dc_crypto *crypto, xx_pd_struct *pd) {
    uint64_t l1_index = cluster_index >> parsed->l2_bits;
    uint64_t l2_index = cluster_index & (((uint64_t)1 << parsed->l2_bits) - 1U);
    size_t l2_bytes = (size_t)8U << parsed->l2_bits;
    uint64_t l2_table;
    uint64_t entry;
    int64_t at;

    xx_mem_zero(out, parsed->cluster_size);
    if (l1_index >= (uint64_t)parsed->l1_size || !parsed->l1_table) return true;
    l2_table = parsed->l1_table[l1_index];
    if (l2_table == 0U) return true;
    if (!cache->valid || cache->l1_index != l1_index) {
        cache->valid = false;
        /* The whole L2 table must be inside the image. */
        if (!xx_qcow1_host_range(parsed, l2_table, (int64_t)l2_bytes, &at) ||
            !xx_qcow1_read_at(self->device, at, cache->table, l2_bytes)) {
            return false;
        }
        cache->l1_index = l1_index;
        cache->valid = true;
    }
    entry = xx_data_get_u64(cache->table, l2_bytes, (size_t)l2_index * 8U, true);
    if (entry == 0U) return true;

    if ((entry & XX_QCOW1_FLAG_COMPRESSED) != 0U) {
        if (parsed->crypt_method != 0U) return false;
        uint32_t shift = 63U - parsed->cluster_bits;
        uint64_t host = entry & (((uint64_t)1 << shift) - 1U);
        uint64_t csize = (entry >> shift) & (uint64_t)(parsed->cluster_size - 1U);
        size_t written = 0U;

        if (csize == 0U ||
            !xx_qcow1_host_range(parsed, host, (int64_t)csize, &at) ||
            !xx_qcow1_read_at(self->device, at, scratch, (size_t)csize)) {
            return false;
        }
        /* Raw deflate, no zlib wrapper. A short stream leaves the tail of
         * the cluster zeroed. */
        return xx_deflate_decompress_memory(scratch, (size_t)csize, out,
                                            parsed->cluster_size, &written,
                                            false);
    }
    if (!xx_qcow1_host_range(parsed, entry, (int64_t)parsed->cluster_size,
                             &at)) {
        /* The last cluster of an image may be cut short by the writer;
         * accept a partial cluster that ends exactly at the end of file. */
        int64_t tail;
        if (entry > (uint64_t)INT64_MAX ||
            !xx_qcow1_host_range(parsed, entry, 0, &at)) {
            return false;
        }
        if (parsed->crypt_method != 0U) return false;
        tail = parsed->input_size - at;
        if (tail <= 0) return false;
        return xx_qcow1_read_at(self->device, at, out, (size_t)tail);
    }
    if (!xx_qcow1_read_at(self->device, at, out, parsed->cluster_size)) return false;
    return parsed->crypt_method == 0U || dc_decrypt(crypto, cluster_index * (uint64_t)parsed->cluster_size / 512U, out, parsed->cluster_size, pd);
}

static bool xx_qcow1_write_all(xx_io_device *output, const uint8_t *data,
                               size_t size) {
    size_t done = 0U;

    while (done < size) {
        ssize_t sent = xx_io_write(output, data + done, size - done);
        if (sent <= 0 || (size_t)sent > size - done) return false;
        done += (size_t)sent;
    }
    return true;
}

/* Walk every guest cluster. A NULL destination verifies all stored clusters.
 * Output is staged in a 1 MB buffer; absent sparse L1 extents are skipped. */
static bool xx_qcow1_write_image(Abstractformat *self,
                                 const xx_qcow1_private *parsed,
                                 xx_io_device *output, dc_crypto *crypto, xx_pd_struct *pd) {
    uint8_t *stage;
    uint8_t *scratch;
    xx_qcow1_l2_cache cache;
    size_t stage_size = output ? XX_QCOW1_STAGE_SIZE : parsed->cluster_size;
    size_t fill = 0U;
    uint64_t remaining = parsed->virtual_size;
    uint64_t index = 0U;

    bool result = true;

    xx_mem_zero(&cache, sizeof(cache));
    stage = (uint8_t *)xx_mem_alloc(stage_size);
    scratch = (uint8_t *)xx_mem_alloc(parsed->cluster_size);
    cache.table = (uint8_t *)xx_mem_alloc((size_t)8U << parsed->l2_bits);
    if (!stage || !scratch || !cache.table) {
        if (stage) xx_mem_free(stage);
        if (scratch) xx_mem_free(scratch);
        if (cache.table) xx_mem_free(cache.table);
        return false;
    }
    while (remaining != 0U) {
        /* A NULL destination still verifies the complete mapping. An absent
         * L1 extent has no stored bytes or compressed stream to consume. */
        uint64_t l1 = index >> parsed->l2_bits;
        if (!output && (l1 >= parsed->l1_size || parsed->l1_table[l1] == 0U)) {
            uint64_t clusters = (UINT64_C(1) << parsed->l2_bits) - (index & ((UINT64_C(1) << parsed->l2_bits)-1U));
            uint64_t bytes = clusters * parsed->cluster_size;
            if (xx_pd_is_stopped(pd)) { result=false; break; }
            if (bytes > remaining) bytes=remaining;
            remaining-=bytes; index+=(bytes+parsed->cluster_size-1U)/parsed->cluster_size; continue;
        }

        size_t chunk = remaining < (uint64_t)parsed->cluster_size
                           ? (size_t)remaining
                           : (size_t)parsed->cluster_size;

        if (pd && xx_pd_is_stopped(pd)) {
            result = false;
            break;
        }
        if (fill + parsed->cluster_size > stage_size) {
            if (output && !xx_qcow1_write_all(output, stage, fill)) {
                result = false;
                break;
            }
            fill = 0U;
        }
        if (!xx_qcow1_read_cluster(self, parsed, index, stage + fill, scratch,
                                   &cache, crypto, pd)) {
            result = false;
            break;
        }
        fill += chunk;
        remaining -= (uint64_t)chunk;
        ++index;
    }
    if (result && output && fill != 0U &&
        !xx_qcow1_write_all(output, stage, fill)) {
        result = false;
    }
    dc_clear(stage,stage_size); dc_clear(scratch,parsed->cluster_size);
    xx_mem_free(stage);
    xx_mem_free(scratch);
    xx_mem_free(cache.table);
    return result;
}

/* ------------------------------------------------------------ lifecycle -- */

void xx_qcow1_init(xx_qcow1 *qcow1, xx_io_device *dev, int64_t base_address) {
    if (!qcow1) return;
    xx_mem_zero(qcow1, sizeof(*qcow1));
    xx_format_init(&qcow1->format, dev, base_address);
    qcow1->format.endian = XX_ENDIAN_BIG;
    qcow1->format.file_type = XX_QCOW1_FILE_TYPE;
    qcow1->format.format_type = XX_TYPE_ARCHIVE;
    qcow1->format.is_archive = true;
    xx_format_set_mime_type(&qcow1->format, "application/x-qemu-disk");
    xx_format_set_extension(&qcow1->format, "qcow");
    qcow1->format.check_is_valid = xx_qcow1_check_is_valid;
    qcow1->format.handle_base_info = xx_qcow1_handle_base_info;
    qcow1->format.get_format_size = xx_qcow1_get_format_size;
    qcow1->format.get_number_of_archive_records =
        xx_qcow1_get_number_of_archive_records;
    qcow1->format.create_archive_records_reading =
        xx_qcow1_create_archive_records_reading;
    qcow1->format.get_current_archive_record =
        xx_qcow1_get_current_archive_record;
    qcow1->format.unpack_current_archive_record =
        xx_qcow1_unpack_current_archive_record;
    qcow1->format.archive_record_move_to_next =
        xx_qcow1_archive_record_move_to_next;
    qcow1->format.free_archive_records_reading =
        xx_qcow1_free_archive_records_reading;
    qcow1->format.destroy = xx_qcow1_vtable_destroy;
}

xx_qcow1 *xx_qcow1_create(xx_io_device *dev, int64_t base_address) {
    xx_qcow1 *qcow1 = (xx_qcow1 *)xx_mem_alloc(sizeof(*qcow1));

    if (qcow1) xx_qcow1_init(qcow1, dev, base_address);
    return qcow1;
}

void xx_qcow1_destroy(xx_qcow1 *qcow1) {
    if (!qcow1) return;
    if (qcow1->internal) {
        xx_qcow1_private_free(qcow1->internal);
        qcow1->internal = NULL;
    }
    xx_format_cleanup_extra_parameters(&qcow1->format);
}

static void xx_qcow1_vtable_destroy(Abstractformat *self) {
    xx_qcow1_destroy((xx_qcow1 *)self);
}

void xx_qcow1_free(xx_qcow1 *qcow1) {
    if (!qcow1) return;
    xx_qcow1_destroy(qcow1);
    xx_mem_free(qcow1);
}

/* --------------------------------------------------------------- format -- */

bool xx_qcow1_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_qcow1_private parsed;
    bool result = xx_qcow1_parse(self, &parsed, NULL, pd);

    xx_qcow1_private_cleanup(&parsed);
    return result;
}

bool xx_qcow1_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_qcow1 *qcow1 = (xx_qcow1 *)self;
    xx_qcow1_private *parsed;

    if (!self) return false;
    parsed = (xx_qcow1_private *)xx_mem_alloc(sizeof(*parsed));
    if (!parsed || !xx_qcow1_parse(self, parsed, NULL, pd)) {
        if (parsed) xx_mem_free(parsed);
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    if (qcow1->internal) xx_qcow1_private_free(qcow1->internal);
    qcow1->internal = parsed;
    qcow1->number_of_records = 1U;
    qcow1->virtual_size = parsed->virtual_size;
    qcow1->l1_table_offset = parsed->l1_table_offset;
    qcow1->cluster_bits = parsed->cluster_bits;
    qcow1->cluster_size = parsed->cluster_size;
    qcow1->l2_bits = parsed->l2_bits;
    qcow1->l1_size = parsed->l1_size;
    qcow1->crypt_method = parsed->crypt_method;
    qcow1->mtime = parsed->mtime;
    qcow1->has_backing_file = parsed->backing_file != NULL;
    qcow1->is_encrypted = parsed->crypt_method != 0U;
    /* Clusters may be anywhere in the file: the whole device is the format. */
    self->format_size = parsed->input_size - self->base_address;
    self->overlay_offset = -1;
    self->overlay_size = 0;
    self->number_of_archive_records = 1U;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_qcow1_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return -1;
    }
    return self->format_size;
}

uint64_t xx_qcow1_get_number_of_archive_records(Abstractformat *self,
                                                xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return ((xx_qcow1 *)self)->number_of_records;
}

/* -------------------------------------------------------------- records -- */

static bool xx_qcow1_copy_options(xx_list_s *destination,
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

static XXFC_MAYBE_UNUSED const xx_var *xx_qcow1_find_option(const xx_list_s *options,
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

static bool xx_qcow1_populate_record(xx_archive_record *record,
                                     const xx_qcow1_private *parsed) {
    if (!record || !parsed) return false;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = parsed->base_address;
    record->header_size = XX_QCOW1_HEADER_SIZE;
    record->data_offset =
        (int64_t)parsed->l1_table_offset + parsed->base_address;
    record->compressed_size = parsed->input_size - parsed->base_address;
    if (!xx_archive_record_set_original_name(record, XX_QCOW1_MEMBER_NAME) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                        parsed->virtual_size) ||
        !xx_archive_record_set_meta_u64(
            record, XX_META_ID_COMPRESSED_SIZE,
            (uint64_t)(parsed->input_size - parsed->base_address)) ||
        !xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false) ||
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

xx_archive_record_state *xx_qcow1_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_record_state *state;
    xx_qcow1_private *parsed;

    if (!self || !self->device ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    parsed = (xx_qcow1_private *)xx_mem_calloc(1U, sizeof(*parsed));
    if (!state || !parsed) {
        if (state) xx_mem_free(state);
        if (parsed) xx_mem_free(parsed);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    if (!xx_qcow1_copy_options(&state->options, options) ||
        !xx_qcow1_parse(self, parsed, options, pd)) {
        xx_qcow1_private_free(parsed);
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->internal_state = parsed;
    state->free_internal = xx_qcow1_private_free;
    state->total_records = 1;
    if (!xx_qcow1_populate_record(&state->current_record, parsed)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_qcow1_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_qcow1_archive_record_move_to_next(Abstractformat *self,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    xx_qcow1_private *parsed;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    parsed = (xx_qcow1_private *)state->internal_state;
    if (parsed) parsed->consumed = true;
    xx_archive_record_cleanup(&state->current_record);
    xx_archive_record_init(&state->current_record);
    state->has_record = false;
    return false;
}

static bool xx_qcow1_unpack_impl(Abstractformat *self,
                                            xx_archive_record_state *state,
                                            dc_crypto *crypto, xx_pd_struct *pd) {
    xx_qcow1_private *parsed;
    const xx_var *option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *destination = NULL;
    char *stage = NULL;
    xx_io_device *output = NULL;
    bool result;
    bool overwrite = false;

    if (!self || !self->device || !state || state->format != self ||
        !state->has_record || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    parsed = (xx_qcow1_private *)state->internal_state;
    if (!parsed || parsed->consumed) return false;
    option = xx_format_resolve_extra_parameter(self, &state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!option) return xx_qcow1_write_image(self, parsed, NULL, crypto, pd);
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
        destination = xx_str_concat3(base, "/", XX_QCOW1_MEMBER_NAME);
    } else {
        destination = xx_str_concat(base, XX_QCOW1_MEMBER_NAME);
    }
    if (owned_base) xx_str_free(owned_base);
    if (!destination) return false;
    if (!xx_store_create_dirs_a(destination, false)) {
        xx_str_free(destination);
        return false;
    }
    option=xx_format_resolve_extra_parameter(self,&state->options,XX_META_ID_OPT_OVERWRITE);
    overwrite=option&&xx_var_get_bool(option);
    if(dc_same_path(destination,xx_io_source_path(self->device)) || (!overwrite&&xx_io_file_exists_a(destination))) { xx_str_free(destination); return false; }
    output = dc_stage(destination,&stage);
    result = output != NULL && xx_qcow1_write_image(self, parsed, output, crypto, pd);
    if (output && xx_io_close(output) != 0) result = false;
    if (result && stage) result=!xx_pd_is_stopped(pd)&&xx_io_file_replace_a(stage,destination,overwrite);
    if(stage) { if(!result) xx_io_file_remove_a(stage); xx_str_free(stage); }
    xx_str_free(destination);
    return result;
}


bool xx_qcow1_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    xx_qcow1_private *parsed; dc_crypto crypto; const uint8_t *password; size_t password_size; char *owned=NULL;
    uint8_t key[16]={0}; uint64_t memory, member, required; int64_t cursor; bool result=false;
    if(!self || !self->device || !state || state->format!=self || !state->has_record) return false;
    parsed=(xx_qcow1_private*)state->internal_state; if(!parsed || parsed->consumed || xx_pd_is_stopped(pd)) return false;
    xx_mem_zero(&crypto,sizeof(crypto)); cursor=xx_io_tell(self->device);
    required=(uint64_t)parsed->l1_size*8U + XX_QCOW1_STAGE_SIZE+(uint64_t)parsed->cluster_size+((uint64_t)8U<<parsed->l2_bits) + sizeof(crypto) + 8192U;
    { const xx_qcow1_private *live=(const xx_qcow1_private*)((xx_qcow1*)self)->internal; if(live&&live!=parsed) required+=(uint64_t)live->l1_size*8U+sizeof(*live)+1024U; }
    if(!dc_limit(self,&state->options,XX_META_ID_OPT_MEMORY_LIMIT,UINT64_MAX,&memory) || !dc_limit(self,&state->options,XX_META_ID_OPT_MAX_MEMBER_SIZE,UINT64_MAX,&member) || required>memory || parsed->virtual_size>member) goto done;
    if(parsed->crypt_method) {
        if(!dc_password(self,&state->options,&password,&password_size,&owned,memory-required)) { xx_pd_set_error(pd,XXFC_ERR_INVALID_ARG,"Disk image password required"); goto done; }
        if(parsed->crypt_method==1U) { if(password_size>16) password_size=16; dc_copy(key,password,password_size); if(!dc_crypto_init(&crypto,DC_CBC_PLAIN64,key,16)) goto done; }
    }
    result=xx_qcow1_unpack_impl(self,state,&crypto,pd);
done:
    if(owned) { dc_clear(owned,xx_str_len(owned)); xx_str_free(owned); } dc_clear(key,sizeof(key)); dc_clear(&crypto,sizeof(crypto));
    if(cursor>=0 && xx_io_seek64(self->device,cursor,SEEK_SET)!=0) result=false;
    return result;
}

void xx_qcow1_free_archive_records_reading(Abstractformat *self,
                                           xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

/* ------------------------------------------------------------ accessors -- */

uint64_t xx_qcow1_get_virtual_size(const xx_qcow1 *qcow1) {
    return qcow1 ? qcow1->virtual_size : 0U;
}
uint32_t xx_qcow1_get_cluster_size(const xx_qcow1 *qcow1) {
    return qcow1 ? qcow1->cluster_size : 0U;
}
uint32_t xx_qcow1_get_crypt_method(const xx_qcow1 *qcow1) {
    return qcow1 ? qcow1->crypt_method : 0U;
}
const char *xx_qcow1_get_backing_file(const xx_qcow1 *qcow1) {
    const xx_qcow1_private *parsed =
        qcow1 ? (const xx_qcow1_private *)qcow1->internal : NULL;
    return parsed ? parsed->backing_file : NULL;
}
