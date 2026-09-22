/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/ubi/xx_ubi.h"

#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

/* Registration placeholder. xxfc_defs.h is shared and is not edited from
 * here, so the file-type constant is supplied locally until the enumerator
 * lands. Delete this block once XX_FILE_TYPE_UBI exists in the enum. */
#ifdef UBI
#define XX_UBI_FILE_TYPE XX_FILE_TYPE_UBI
#else
#define XX_UBI_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define XX_UBI_EC_HDR_MAGIC UINT32_C(0x55424923)  /* "UBI#" */
#define XX_UBI_VID_HDR_MAGIC UINT32_C(0x55424921) /* "UBI!" */
#define XX_UBI_EC_HDR_SIZE 64
#define XX_UBI_EC_HDR_SIZE_CRC 60
#define XX_UBI_VID_HDR_SIZE 64
#define XX_UBI_VID_HDR_SIZE_CRC 60
#define XX_UBI_VTBL_RECORD_SIZE 172
#define XX_UBI_VTBL_RECORD_SIZE_CRC 168
#define XX_UBI_MAX_VOLUMES 128U
#define XX_UBI_VOL_NAME_MAX 127U

#define XX_UBI_VID_DYNAMIC 1U
#define XX_UBI_VID_STATIC 2U

/* Volume ids at or above this are UBI's own bookkeeping volumes, not user
 * data. The layout volume, which carries the volume table, is the first. */
#define XX_UBI_INTERNAL_VOL_START UINT32_C(0x7FFFF000)
#define XX_UBI_LAYOUT_VOLUME_ID UINT32_C(0x7FFFF000)

/* Bounds. Every one of these is reached only by a hostile or corrupt image;
 * a real one stays far below. */
#define XX_UBI_MIN_PEB_SIZE 1024
#define XX_UBI_MAX_PEB_SIZE (64 * 1024 * 1024)
#define XX_UBI_MAX_PEBS UINT64_C(4000000)
#define XX_UBI_MAX_VOLUME_SLOTS 256U
#define XX_UBI_MAX_LEBS_TOTAL 4000000U
#define XX_UBI_MAX_NAME_SIZE 256U

/* One logical erase block, recovered from one physical erase block. */
typedef struct xx_ubi_leb_s {
    uint32_t lnum;        /**< Logical block number inside its volume. */
    uint32_t size;        /**< Payload bytes this block contributes. */
    uint64_t sqnum;       /**< Newest copy of a duplicated lnum wins. */
    int64_t data_offset;  /**< Absolute device offset of the payload. */
    int64_t peb_offset;   /**< Absolute device offset of the PEB. */
} xx_ubi_leb;

typedef struct xx_ubi_volume_s {
    uint32_t vol_id;
    uint8_t vol_type;        /**< From the volume-id headers. */
    uint8_t vtbl_type;       /**< From the volume table, 0 when absent. */
    bool named;              /**< True when the volume table supplied a name. */
    char name[XX_UBI_MAX_NAME_SIZE];
    uint32_t used_ebs;       /**< Static volumes only, 0 otherwise. */
    uint32_t reserved_pebs;  /**< From the volume table, 0 when absent. */
    /* A volume with an alignment requirement pads the tail of every erase
     * block, so its usable block is smaller than the image-wide one. This is
     * the size a hole in the block sequence stands in for. */
    uint32_t gap_size;
    xx_ubi_leb *lebs;
    size_t count;
    size_t capacity;
    uint32_t block_count;    /**< max lnum + 1 after reassembly. */
    int64_t total_size;      /**< Assembled payload size in bytes. */
} xx_ubi_volume;

typedef struct xx_ubi_private_s {
    xx_ubi_volume *volumes;  /**< Data volumes, in first-seen order. */
    size_t volume_count;
    size_t volume_capacity;
    xx_ubi_volume layout;    /**< The layout volume, kept aside. */
    bool has_layout;
    uint32_t peb_size;
    uint32_t leb_size;
    uint32_t vid_hdr_offset;
    uint32_t data_offset;
    uint32_t image_seq;
    uint64_t peb_count;
    uint64_t mapped_peb_count;
    size_t leb_total;        /**< All blocks recorded, capped. */
    int64_t input_size;
    int64_t archive_end;
} xx_ubi_private;

typedef struct xx_ubi_archive_stream_s {
    xx_ubi_private parsed;
    size_t index;
} xx_ubi_archive_stream;

static void xx_ubi_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers --- */

/* Every read goes through the 64-bit seek. UBI images routinely run past
 * 2 GiB, and xx_io_seek() takes a long, which is 32-bit on Win64. */
static bool xx_ubi_read_at(xx_io_device *device, int64_t offset, void *data,
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
static bool xx_ubi_range_within(int64_t total_size, int64_t offset,
                                int64_t size) {
    return (total_size >= 0) && (offset >= 0) && (size >= 0) &&
           (offset <= total_size) && (size <= total_size - offset);
}

/* UBI seeds CRC-32 with 0xFFFFFFFF and does not complement the result, so it
 * is the ordinary CRC-32 of the same bytes with the final inversion undone. */
static uint32_t xx_ubi_crc(const void *data, size_t size) {
    return xx_crc32_calc(0U, data, size) ^ UINT32_C(0xFFFFFFFF);
}

/* Erased flash reads back as all ones. A header of nothing but 0xFF is an
 * absent header, not a corrupt one, and must not fail the parse. */
static bool xx_ubi_is_erased(const uint8_t *data, size_t size) {
    size_t index;
    for (index = 0U; index < size; ++index) {
        if (data[index] != 0xFFU) return false;
    }
    return true;
}

/* ------------------------------------------------------------ geometry --- */

/* An erase-counter header, validated. Returns false for an absent or
 * unusable one; *out_erased distinguishes the two. */
static bool xx_ubi_read_ec_hdr(xx_io_device *device, int64_t peb_offset,
                               int64_t total_size, uint32_t *out_vid_offset,
                               uint32_t *out_data_offset,
                               uint32_t *out_image_seq, bool *out_erased) {
    uint8_t header[XX_UBI_EC_HDR_SIZE];
    if (out_erased) *out_erased = false;
    if (!xx_ubi_range_within(total_size, peb_offset, XX_UBI_EC_HDR_SIZE) ||
        !xx_ubi_read_at(device, peb_offset, header, sizeof(header))) {
        return false;
    }
    if (xx_ubi_is_erased(header, sizeof(header))) {
        if (out_erased) *out_erased = true;
        return false;
    }
    if (xx_data_get_u32(header, sizeof(header), 0U, true) !=
            XX_UBI_EC_HDR_MAGIC ||
        xx_data_get_u32(header, sizeof(header), 60U, true) !=
            xx_ubi_crc(header, XX_UBI_EC_HDR_SIZE_CRC)) {
        return false;
    }
    if (out_vid_offset) {
        *out_vid_offset = xx_data_get_u32(header, sizeof(header), 16U, true);
    }
    if (out_data_offset) {
        *out_data_offset = xx_data_get_u32(header, sizeof(header), 20U, true);
    }
    if (out_image_seq) {
        *out_image_seq = xx_data_get_u32(header, sizeof(header), 24U, true);
    }
    return true;
}

/* The PEB size is not recorded anywhere in the image, so it has to be
 * inferred. Erase block sizes are powers of two, so the candidates are tried
 * smallest first and the first one that puts a well-formed erase-counter
 * header at the second PEB wins. A second pass allows the second PEB to be
 * erased, for images whose first free block sits early. A single-PEB image
 * falls back to its own length. This is the same class of heuristic every
 * other UBI reader uses; it can over-estimate when the blocks that would
 * disambiguate are all erased. */
static bool xx_ubi_detect_peb_size(xx_io_device *device, int64_t base,
                                   int64_t total_size, uint32_t *out_peb_size) {
    int64_t available;
    int pass;
    if (!out_peb_size) return false;
    available = total_size - base;
    if (available < XX_UBI_EC_HDR_SIZE) return false;
    for (pass = 0; pass < 2; ++pass) {
        int64_t candidate;
        for (candidate = XX_UBI_MIN_PEB_SIZE;
             candidate <= XX_UBI_MAX_PEB_SIZE && candidate <= available;
             candidate *= 2) {
            int64_t probe;
            int steps;
            for (probe = candidate, steps = 0;
                 probe + XX_UBI_EC_HDR_SIZE <= available && steps < 64;
                 probe += candidate, ++steps) {
                bool erased = false;
                if (xx_ubi_read_ec_hdr(device, base + probe, total_size, NULL,
                                       NULL, NULL, &erased)) {
                    *out_peb_size = (uint32_t)candidate;
                    return true;
                }
                /* Pass 0 demands the very next block be a real header, which
                 * is what a healthy image looks like. Pass 1 tolerates a run
                 * of erased blocks before the next one. */
                if (pass == 0 || !erased) break;
            }
        }
    }
    /* No second block anywhere: the image holds a single PEB. */
    if (available <= XX_UBI_MAX_PEB_SIZE) {
        *out_peb_size = (uint32_t)available;
        return true;
    }
    return false;
}

/* -------------------------------------------------------------- volumes -- */

static void xx_ubi_volume_cleanup(xx_ubi_volume *volume) {
    if (!volume) return;
    if (volume->lebs) xx_mem_free(volume->lebs);
    xx_mem_zero(volume, sizeof(*volume));
}

static void xx_ubi_private_cleanup(xx_ubi_private *parsed) {
    size_t index;
    if (!parsed) return;
    for (index = 0U; index < parsed->volume_count; ++index) {
        xx_ubi_volume_cleanup(&parsed->volumes[index]);
    }
    if (parsed->volumes) xx_mem_free(parsed->volumes);
    xx_ubi_volume_cleanup(&parsed->layout);
    xx_mem_zero(parsed, sizeof(*parsed));
    parsed->input_size = -1;
    parsed->archive_end = -1;
}

/* Volumes are keyed by vol_id. A linear scan is fine: UBI allows at most 128
 * of them and the slot cap here is 256. */
static xx_ubi_volume *xx_ubi_find_volume(xx_ubi_private *parsed,
                                         uint32_t vol_id) {
    size_t index;
    xx_ubi_volume *grown;
    size_t capacity;
    if (vol_id == XX_UBI_LAYOUT_VOLUME_ID) {
        parsed->layout.vol_id = vol_id;
        parsed->has_layout = true;
        return &parsed->layout;
    }
    if (vol_id >= XX_UBI_INTERNAL_VOL_START) return NULL;
    for (index = 0U; index < parsed->volume_count; ++index) {
        if (parsed->volumes[index].vol_id == vol_id) {
            return &parsed->volumes[index];
        }
    }
    if (parsed->volume_count >= XX_UBI_MAX_VOLUME_SLOTS) return NULL;
    if (parsed->volume_count == parsed->volume_capacity) {
        capacity = parsed->volume_capacity ? parsed->volume_capacity * 2U : 8U;
        if (capacity > SIZE_MAX / sizeof(*parsed->volumes)) return NULL;
        grown = (xx_ubi_volume *)xx_mem_realloc(
            parsed->volumes, capacity * sizeof(*parsed->volumes));
        if (!grown) return NULL;
        parsed->volumes = grown;
        parsed->volume_capacity = capacity;
    }
    xx_mem_zero(&parsed->volumes[parsed->volume_count],
                sizeof(*parsed->volumes));
    parsed->volumes[parsed->volume_count].vol_id = vol_id;
    return &parsed->volumes[parsed->volume_count++];
}

static bool xx_ubi_volume_append(xx_ubi_volume *volume, const xx_ubi_leb *leb) {
    xx_ubi_leb *grown;
    size_t capacity;
    if (!volume || !leb) return false;
    if (volume->count == volume->capacity) {
        capacity = volume->capacity ? volume->capacity * 2U : 16U;
        if (capacity > SIZE_MAX / sizeof(*volume->lebs)) return false;
        grown = (xx_ubi_leb *)xx_mem_realloc(volume->lebs,
                                             capacity * sizeof(*volume->lebs));
        if (!grown) return false;
        volume->lebs = grown;
        volume->capacity = capacity;
    }
    volume->lebs[volume->count++] = *leb;
    return true;
}

/* Order by lnum, then by sqnum so that the newest copy of a duplicated
 * logical block lands last and wins the de-duplication pass. */
static bool xx_ubi_leb_less(const xx_ubi_leb *left, const xx_ubi_leb *right) {
    if (left->lnum != right->lnum) return left->lnum < right->lnum;
    return left->sqnum < right->sqnum;
}

/* Heapsort: the block list is attacker-sized, so an O(n log n) in-place sort
 * with no recursion and no scratch allocation is the safe choice. */
static void xx_ubi_sift_down(xx_ubi_leb *items, size_t start, size_t count) {
    size_t root = start;
    while (root * 2U + 1U < count) {
        size_t child = root * 2U + 1U;
        xx_ubi_leb swap;
        if (child + 1U < count &&
            xx_ubi_leb_less(&items[child], &items[child + 1U])) {
            ++child;
        }
        if (!xx_ubi_leb_less(&items[root], &items[child])) return;
        swap = items[root];
        items[root] = items[child];
        items[child] = swap;
        root = child;
    }
}

static void xx_ubi_sort_lebs(xx_ubi_leb *items, size_t count) {
    size_t index;
    if (count < 2U) return;
    for (index = count / 2U; index-- > 0U;) {
        xx_ubi_sift_down(items, index, count);
    }
    for (index = count; index-- > 1U;) {
        xx_ubi_leb swap = items[0];
        items[0] = items[index];
        items[index] = swap;
        xx_ubi_sift_down(items, 0U, index);
    }
}

/* Sort, drop superseded copies of a logical block, and total up the assembled
 * size. A hole in the lnum sequence contributes a full erase block of erased
 * flash, which is what the block would read back as on the device. */
static bool xx_ubi_volume_assemble(xx_ubi_volume *volume, uint32_t leb_size) {
    size_t read_index;
    size_t write_index = 0U;
    int64_t total = 0;
    uint32_t expected = 0U;
    if (!volume) return false;
    /* A volume that declared its own block capacity keeps it; one recovered
     * without any mapped block falls back to the image-wide size. */
    if (volume->gap_size != 0U) leb_size = volume->gap_size;
    xx_ubi_sort_lebs(volume->lebs, volume->count);
    for (read_index = 0U; read_index < volume->count; ++read_index) {
        if (write_index != 0U &&
            volume->lebs[write_index - 1U].lnum ==
                volume->lebs[read_index].lnum) {
            /* Same logical block seen again; the sort put the higher sqnum
             * later, so overwrite the copy already kept. */
            volume->lebs[write_index - 1U] = volume->lebs[read_index];
            continue;
        }
        volume->lebs[write_index++] = volume->lebs[read_index];
    }
    volume->count = write_index;
    if (volume->count == 0U) {
        volume->block_count = 0U;
        volume->total_size = 0;
        return true;
    }
    for (read_index = 0U; read_index < volume->count; ++read_index) {
        uint32_t lnum = volume->lebs[read_index].lnum;
        /* Gaps are filled, so the running total has to account for every
         * logical block between the previous one and this one. */
        while (expected < lnum) {
            if (total > INT64_MAX - leb_size) return false;
            total += leb_size;
            ++expected;
        }
        if (total > INT64_MAX - volume->lebs[read_index].size) return false;
        total += volume->lebs[read_index].size;
        expected = lnum + 1U;
    }
    volume->block_count = expected;
    volume->total_size = total;
    return true;
}

/* ---------------------------------------------------------- volume table -- */

static bool xx_ubi_plausible_name(const char *name, size_t length) {
    size_t index;
    if (!name || length == 0U) return false;
    for (index = 0U; index < length; ++index) {
        unsigned char ch = (unsigned char)name[index];
        if (ch < 32U || ch == 127U || ch == '/' || ch == '\\') return false;
    }
    return true;
}

/* Read the 128 volume-table records from the layout volume's first block and
 * copy the names onto the volumes already discovered. A record that fails its
 * CRC is skipped rather than failing the parse: the names are a convenience,
 * and the block list recovered from the volume-id headers stands without
 * them. */
static void xx_ubi_apply_vtbl(Abstractformat *self, xx_ubi_private *parsed) {
    uint8_t record[XX_UBI_VTBL_RECORD_SIZE];
    uint32_t slot;
    int64_t offset;
    uint32_t available;
    if (!parsed->has_layout || parsed->layout.count == 0U) return;
    /* The table lives in LEB 0 of the layout volume, which sorting put
     * first. */
    if (parsed->layout.lebs[0].lnum != 0U) return;
    offset = parsed->layout.lebs[0].data_offset;
    available = parsed->layout.lebs[0].size;
    for (slot = 0U; slot < XX_UBI_MAX_VOLUMES; ++slot) {
        xx_ubi_volume *volume;
        uint32_t name_len;
        uint32_t stored_crc;
        if ((uint64_t)(slot + 1U) * XX_UBI_VTBL_RECORD_SIZE > available) break;
        if (!xx_ubi_read_at(self->device,
                            offset + (int64_t)slot * XX_UBI_VTBL_RECORD_SIZE,
                            record, sizeof(record))) {
            return;
        }
        stored_crc = xx_data_get_u32(record, sizeof(record), 168U, true);
        if (stored_crc != xx_ubi_crc(record, XX_UBI_VTBL_RECORD_SIZE_CRC)) {
            continue;
        }
        name_len = xx_data_get_u16(record, sizeof(record), 14U, true);
        if (name_len == 0U || name_len > XX_UBI_VOL_NAME_MAX) continue;
        if (!xx_ubi_plausible_name((const char *)record + 16U, name_len)) {
            continue;
        }
        volume = xx_ubi_find_volume(parsed, slot);
        if (!volume) continue;
        xx_rt_memcpy(volume->name, record + 16U, name_len);
        volume->name[name_len] = '\0';
        volume->named = true;
        volume->vtbl_type = xx_data_get_u8(record, sizeof(record), 12U);
        volume->reserved_pebs =
            xx_data_get_u32(record, sizeof(record), 0U, true);
    }
}

/* --------------------------------------------------------------- parse --- */

/* Sweep every physical erase block, validating both headers and filing each
 * payload under its volume. A PEB that is erased, unheaded or corrupt is
 * skipped; only a structurally impossible image fails outright, because a
 * flash dump with a few bad blocks in it is the normal case rather than the
 * exception. */
static bool xx_ubi_parse(Abstractformat *self, xx_ubi_private *parsed,
                         xx_pd_struct *pd) {
    int64_t total_size;
    uint64_t peb_index;
    uint64_t peb_total;
    bool geometry_known = false;
    size_t index;

    if (parsed) {
        xx_mem_zero(parsed, sizeof(*parsed));
        parsed->input_size = -1;
        parsed->archive_end = -1;
    }
    if (!self || !self->device || !parsed || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    total_size = xx_io_total_size(self->device);
    if (!xx_ubi_range_within(total_size, self->base_address,
                             XX_UBI_EC_HDR_SIZE)) {
        goto fail;
    }
    parsed->input_size = total_size;
    /* The first PEB must carry a valid erase-counter header, or this is not
     * a UBI image at all. */
    if (!xx_ubi_read_ec_hdr(self->device, self->base_address, total_size,
                            &parsed->vid_hdr_offset, &parsed->data_offset,
                            &parsed->image_seq, NULL)) {
        goto fail;
    }
    if (!xx_ubi_detect_peb_size(self->device, self->base_address, total_size,
                                &parsed->peb_size)) {
        goto fail;
    }
    if (parsed->peb_size < XX_UBI_EC_HDR_SIZE) goto fail;
    peb_total = (uint64_t)(total_size - self->base_address) / parsed->peb_size;
    if (peb_total == 0U || peb_total > XX_UBI_MAX_PEBS) goto fail;

    for (peb_index = 0U; peb_index < peb_total; ++peb_index) {
        uint8_t vid[XX_UBI_VID_HDR_SIZE];
        int64_t peb_offset =
            self->base_address + (int64_t)peb_index * parsed->peb_size;
        uint32_t vid_hdr_offset = 0U;
        uint32_t data_offset = 0U;
        uint32_t image_seq = 0U;
        uint32_t vol_id;
        uint32_t payload_size;
        uint32_t block_capacity;
        uint8_t vol_type;
        xx_ubi_volume *volume;
        xx_ubi_leb leb;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        ++parsed->peb_count;
        if (!xx_ubi_read_ec_hdr(self->device, peb_offset, total_size,
                                &vid_hdr_offset, &data_offset, &image_seq,
                                NULL)) {
            continue;
        }
        /* The two offsets are attacker-controlled. Both headers and the
         * payload must fit inside this erase block. */
        if (vid_hdr_offset < XX_UBI_EC_HDR_SIZE ||
            vid_hdr_offset > parsed->peb_size - XX_UBI_VID_HDR_SIZE ||
            data_offset < vid_hdr_offset + XX_UBI_VID_HDR_SIZE ||
            data_offset >= parsed->peb_size) {
            continue;
        }
        if (!geometry_known) {
            parsed->vid_hdr_offset = vid_hdr_offset;
            parsed->data_offset = data_offset;
            parsed->image_seq = image_seq;
            parsed->leb_size = parsed->peb_size - data_offset;
            geometry_known = true;
        }
        if (!xx_ubi_read_at(self->device, peb_offset + vid_hdr_offset, vid,
                            sizeof(vid))) {
            continue;
        }
        /* An erased volume-id header means a free PEB: it has been erased and
         * counted but not yet handed to a volume. */
        if (xx_ubi_is_erased(vid, sizeof(vid))) continue;
        if (xx_data_get_u32(vid, sizeof(vid), 0U, true) !=
                XX_UBI_VID_HDR_MAGIC ||
            xx_data_get_u32(vid, sizeof(vid), 60U, true) !=
                xx_ubi_crc(vid, XX_UBI_VID_HDR_SIZE_CRC)) {
            continue;
        }
        vol_type = xx_data_get_u8(vid, sizeof(vid), 5U);
        vol_id = xx_data_get_u32(vid, sizeof(vid), 8U, true);
        leb.lnum = xx_data_get_u32(vid, sizeof(vid), 12U, true);
        leb.sqnum = xx_data_get_u64(vid, sizeof(vid), 40U, true);
        leb.peb_offset = peb_offset;
        leb.data_offset = peb_offset + data_offset;
        payload_size = parsed->peb_size - data_offset;
        /* A volume whose alignment is not 1 reserves data_pad bytes at the
         * end of every block; those bytes are not part of the volume. */
        {
            uint32_t data_pad = xx_data_get_u32(vid, sizeof(vid), 28U, true);
            if (data_pad >= payload_size) continue;
            payload_size -= data_pad;
        }
        block_capacity = payload_size;
        if (vol_type == XX_UBI_VID_STATIC) {
            /* A static volume records how much of the block it actually
             * used; a larger value than the block holds is corrupt. */
            uint32_t data_size = xx_data_get_u32(vid, sizeof(vid), 20U, true);
            if (data_size > payload_size) continue;
            payload_size = data_size;
        } else if (vol_type != XX_UBI_VID_DYNAMIC) {
            continue;
        }
        leb.size = payload_size;
        if (!xx_ubi_range_within(total_size, leb.data_offset,
                                 (int64_t)payload_size)) {
            continue;
        }
        /* Logical block numbers beyond what this image could possibly hold
         * are rejected; without this a single crafted header would make the
         * assembled size explode. */
        if (leb.lnum >= XX_UBI_MAX_LEBS_TOTAL || leb.lnum >= peb_total) {
            continue;
        }
        if (parsed->leb_total >= XX_UBI_MAX_LEBS_TOTAL) break;
        volume = xx_ubi_find_volume(parsed, vol_id);
        if (!volume) continue;
        if (volume->count == 0U) {
            volume->vol_type = vol_type;
            volume->gap_size = block_capacity;
        }
        if (vol_type == XX_UBI_VID_STATIC) {
            volume->used_ebs = xx_data_get_u32(vid, sizeof(vid), 24U, true);
        }
        if (!xx_ubi_volume_append(volume, &leb)) goto fail;
        ++parsed->leb_total;
        ++parsed->mapped_peb_count;
    }

    if (!geometry_known) goto fail;
    if (parsed->has_layout &&
        !xx_ubi_volume_assemble(&parsed->layout, parsed->leb_size)) {
        goto fail;
    }
    /* Names first: the volume table may introduce a slot that carried no
     * mapped block, and assembling afterwards keeps such a slot consistent. */
    xx_ubi_apply_vtbl(self, parsed);
    for (index = 0U; index < parsed->volume_count; ++index) {
        if (!xx_ubi_volume_assemble(&parsed->volumes[index],
                                    parsed->leb_size)) {
            goto fail;
        }
    }
    /* A volume table alone, with no user volume behind it, is not a useful
     * result; neither is an image whose every PEB was unreadable. */
    if (parsed->volume_count == 0U) goto fail;
    parsed->archive_end =
        self->base_address + (int64_t)peb_total * parsed->peb_size;
    return true;
fail:
    xx_ubi_private_cleanup(parsed);
    return false;
}

/* --------------------------------------------------------------- records -- */

static bool xx_ubi_copy_options(xx_list_s *destination,
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

static const xx_var *xx_ubi_find_option(const xx_list_s *options,
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

/* A reassembled volume is normally a filesystem image. Naming it after what
 * its first block actually opens with lets a caller chain the right reader
 * without re-sniffing, and keeps the UBI layer free of filesystem knowledge
 * beyond this one magic comparison. */
static const char *xx_ubi_volume_extension(Abstractformat *self,
                                           const xx_ubi_volume *volume) {
    uint8_t magic[4];
    if (!volume || volume->count == 0U || volume->lebs[0].lnum != 0U ||
        volume->lebs[0].size < sizeof(magic)) {
        return "img";
    }
    if (!xx_ubi_read_at(self->device, volume->lebs[0].data_offset, magic,
                        sizeof(magic))) {
        return "img";
    }
    /* UBIFS node magic 0x06101831, stored little endian. */
    if (magic[0] == 0x31U && magic[1] == 0x18U && magic[2] == 0x10U &&
        magic[3] == 0x06U) {
        return "ubifs";
    }
    if (magic[0] == 'h' && magic[1] == 's' && magic[2] == 'q' &&
        magic[3] == 's') {
        return "squashfs";
    }
    return "img";
}

/* Append a decimal number to a bounded buffer. Written out by hand because
 * the library is built without the C runtime's string and printf families. */
static size_t xx_ubi_append_u32(char *buffer, size_t used, size_t capacity,
                                uint32_t value) {
    char digits[10];
    size_t count = 0U;
    do {
        digits[count++] = (char)('0' + (value % 10U));
        value /= 10U;
    } while (value != 0U && count < sizeof(digits));
    while (count != 0U && used + 1U < capacity) {
        buffer[used++] = digits[--count];
    }
    return used;
}

static size_t xx_ubi_append_str(char *buffer, size_t used, size_t capacity,
                                const char *text) {
    while (text && *text && used + 1U < capacity) buffer[used++] = *text++;
    return used;
}

static char *xx_ubi_volume_name(Abstractformat *self,
                                const xx_ubi_volume *volume) {
    char buffer[XX_UBI_MAX_NAME_SIZE + 32U];
    size_t used = 0U;
    if (volume->named) {
        used = xx_ubi_append_str(buffer, used, sizeof(buffer), volume->name);
    } else {
        used = xx_ubi_append_str(buffer, used, sizeof(buffer), "volume_");
        used = xx_ubi_append_u32(buffer, used, sizeof(buffer), volume->vol_id);
    }
    used = xx_ubi_append_str(buffer, used, sizeof(buffer), ".");
    used = xx_ubi_append_str(buffer, used, sizeof(buffer),
                             xx_ubi_volume_extension(self, volume));
    buffer[used] = '\0';
    return xx_str_create(buffer);
}

static bool xx_ubi_populate_record(Abstractformat *self,
                                   xx_archive_record *record,
                                   const xx_ubi_volume *volume) {
    char *name;
    bool result;
    if (!record || !volume) return false;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    name = xx_ubi_volume_name(self, volume);
    if (!name) return false;
    /* The payload is a chain of erase blocks scattered across the image, so
     * data_offset only locates the first of them; the record's size is the
     * assembled length and unpacking walks the block list. */
    record->header_offset =
        volume->count != 0U ? volume->lebs[0].peb_offset : -1;
    record->header_size = volume->count != 0U ? XX_UBI_VID_HDR_SIZE : 0;
    record->data_offset =
        volume->count != 0U ? volume->lebs[0].data_offset : -1;
    record->compressed_size = volume->total_size;
    result =
        xx_archive_record_set_original_name(record, name) &&
        xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                       (uint64_t)volume->total_size) &&
        xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                       (uint64_t)volume->total_size) &&
        xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                       0U) &&
        xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false) &&
        xx_archive_record_set_meta_str(
            record, XX_META_ID_COMMENT,
            xx_ubi_volume_type_to_string(volume->vol_type));
    xx_str_free(name);
    return result;
}

/* Write one volume out in logical block order, filling any missing block with
 * erased flash so that the extracted image keeps its block alignment - a
 * filesystem inside it addresses by LEB number, so a hole must occupy space
 * rather than close up. */
static bool xx_ubi_extract_volume(Abstractformat *self,
                                  const xx_ubi_private *parsed,
                                  const xx_ubi_volume *volume,
                                  xx_io_device *destination,
                                  xx_pd_struct *pd) {
    uint8_t *buffer;
    size_t buffer_size = 64U * 1024U;
    uint32_t expected = 0U;
    uint32_t gap_size;
    size_t index;
    bool result = false;
    if (!self || !parsed || !volume || !destination) return false;
    gap_size = volume->gap_size != 0U ? volume->gap_size : parsed->leb_size;
    if (gap_size == 0U) return false;
    buffer = (uint8_t *)xx_mem_alloc(buffer_size);
    if (!buffer) return false;
    for (index = 0U; index < volume->count; ++index) {
        const xx_ubi_leb *leb = &volume->lebs[index];
        int64_t offset = leb->data_offset;
        uint32_t left = leb->size;
        if (pd && xx_pd_is_stopped(pd)) goto cleanup;
        while (expected < leb->lnum) {
            uint32_t gap = gap_size;
            xx_rt_memset(buffer, 0xFF, buffer_size);
            while (gap != 0U) {
                size_t step = gap < buffer_size ? gap : buffer_size;
                if (xx_io_write(destination, buffer, step) != (ssize_t)step) {
                    goto cleanup;
                }
                gap -= (uint32_t)step;
            }
            ++expected;
        }
        while (left != 0U) {
            size_t step = left < buffer_size ? left : buffer_size;
            if (!xx_ubi_read_at(self->device, offset, buffer, step) ||
                xx_io_write(destination, buffer, step) != (ssize_t)step) {
                goto cleanup;
            }
            offset += (int64_t)step;
            left -= (uint32_t)step;
        }
        expected = leb->lnum + 1U;
    }
    result = true;
cleanup:
    xx_mem_free(buffer);
    return result;
}

/* Extraction-time check: the name must stay inside the destination tree on
 * every host this library builds for. Volume names come straight out of the
 * image, so the reserved Windows punctuation is rejected here. */
static bool xx_ubi_safe_name(const char *name) {
    const char *cursor;
    size_t length;
    if (!name || !name[0] || name[0] == '/' || name[0] == '\\' ||
        name[0] == '.') {
        return false;
    }
    for (cursor = name; *cursor; ++cursor) {
        unsigned char ch = (unsigned char)*cursor;
        if (ch < 32U || ch == ':' || ch == '<' || ch == '>' || ch == '"' ||
            ch == '|' || ch == '?' || ch == '*' || ch == '/' || ch == '\\') {
            return false;
        }
    }
    length = xx_str_len(name);
    return name[length - 1U] != ' ' && name[length - 1U] != '.';
}

static void xx_ubi_archive_stream_free(void *pointer) {
    xx_ubi_archive_stream *stream = (xx_ubi_archive_stream *)pointer;
    if (!stream) return;
    xx_ubi_private_cleanup(&stream->parsed);
    xx_mem_free(stream);
}

/* ----------------------------------------------------------- lifecycle --- */

void xx_ubi_init(xx_ubi *ubi, xx_io_device *dev, int64_t base_address) {
    if (!ubi) return;
    xx_mem_zero(ubi, sizeof(*ubi));
    xx_format_init(&ubi->format, dev, base_address);
    ubi->format.endian = XX_ENDIAN_BIG;
    ubi->format.file_type = XX_UBI_FILE_TYPE;
    ubi->format.format_type = XX_TYPE_ARCHIVE;
    ubi->format.is_archive = true;
    xx_format_set_mime_type(&ubi->format, "application/x-ubi");
    xx_format_set_extension(&ubi->format, "ubi");
    ubi->format.check_is_valid = xx_ubi_check_is_valid;
    ubi->format.handle_base_info = xx_ubi_handle_base_info;
    ubi->format.get_format_size = xx_ubi_get_format_size;
    ubi->format.get_number_of_archive_records =
        xx_ubi_get_number_of_archive_records;
    ubi->format.create_archive_records_reading =
        xx_ubi_create_archive_records_reading;
    ubi->format.get_current_archive_record = xx_ubi_get_current_archive_record;
    ubi->format.unpack_current_archive_record =
        xx_ubi_unpack_current_archive_record;
    ubi->format.archive_record_move_to_next = xx_ubi_archive_record_move_to_next;
    ubi->format.free_archive_records_reading =
        xx_ubi_free_archive_records_reading;
    ubi->format.destroy = xx_ubi_vtable_destroy;
    ubi->archive_end = -1;
}

xx_ubi *xx_ubi_create(xx_io_device *dev, int64_t base_address) {
    xx_ubi *ubi = (xx_ubi *)xx_mem_alloc(sizeof(*ubi));
    if (ubi) xx_ubi_init(ubi, dev, base_address);
    return ubi;
}

void xx_ubi_destroy(xx_ubi *ubi) {
    if (!ubi) return;
    if (ubi->internal) {
        xx_ubi_private_cleanup((xx_ubi_private *)ubi->internal);
        xx_mem_free(ubi->internal);
        ubi->internal = NULL;
    }
    xx_format_cleanup_extra_parameters(&ubi->format);
}

static void xx_ubi_vtable_destroy(Abstractformat *self) {
    xx_ubi_destroy((xx_ubi *)self);
}

void xx_ubi_free(xx_ubi *ubi) {
    if (!ubi) return;
    xx_ubi_destroy(ubi);
    xx_mem_free(ubi);
}

bool xx_ubi_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_ubi_private parsed;
    bool result = xx_ubi_parse(self, &parsed, pd);
    xx_ubi_private_cleanup(&parsed);
    return result;
}

bool xx_ubi_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_ubi_private *parsed;
    xx_ubi *ubi = (xx_ubi *)self;
    int64_t total_size;
    if (!self || !ubi) return false;
    parsed = (xx_ubi_private *)xx_mem_alloc(sizeof(*parsed));
    if (!parsed || !xx_ubi_parse(self, parsed, pd)) {
        if (parsed) xx_mem_free(parsed);
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    if (ubi->internal) {
        xx_ubi_private_cleanup((xx_ubi_private *)ubi->internal);
        xx_mem_free(ubi->internal);
    }
    ubi->internal = parsed;
    ubi->number_of_records = parsed->volume_count;
    ubi->number_of_members = parsed->volume_count;
    ubi->peb_size = parsed->peb_size;
    ubi->leb_size = parsed->leb_size;
    ubi->vid_hdr_offset = parsed->vid_hdr_offset;
    ubi->data_offset = parsed->data_offset;
    ubi->image_seq = parsed->image_seq;
    ubi->peb_count = parsed->peb_count;
    ubi->mapped_peb_count = parsed->mapped_peb_count;
    ubi->volume_count = parsed->volume_count;
    ubi->archive_end = parsed->archive_end;
    self->format_size = parsed->archive_end - self->base_address;
    total_size = xx_io_total_size(self->device);
    /* Anything after the last whole erase block is a partial block, which UBI
     * itself would never write; it is reported as overlay. */
    if (total_size > parsed->archive_end) {
        self->overlay_offset = parsed->archive_end;
        self->overlay_size = total_size - parsed->archive_end;
    } else {
        self->overlay_offset = -1;
        self->overlay_size = 0;
    }
    self->number_of_archive_records = parsed->volume_count;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_ubi_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return -1;
    }
    return self->format_size;
}

uint64_t xx_ubi_get_number_of_archive_records(Abstractformat *self,
                                              xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return ((xx_ubi *)self)->number_of_records;
}

xx_archive_record_state *xx_ubi_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_record_state *state;
    xx_ubi_archive_stream *stream;
    if (!self || !self->device ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    stream = (xx_ubi_archive_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!state || !stream) {
        if (state) xx_mem_free(state);
        if (stream) xx_mem_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    if (!xx_ubi_copy_options(&state->options, options) ||
        !xx_ubi_parse(self, &stream->parsed, pd)) {
        xx_ubi_archive_stream_free(stream);
        xx_archive_record_state_free(state);
        return NULL;
    }
    stream->index = 0U;
    state->internal_state = stream;
    state->free_internal = xx_ubi_archive_stream_free;
    state->total_records = (int64_t)stream->parsed.volume_count;
    if (stream->parsed.volume_count != 0U &&
        xx_ubi_populate_record(self, &state->current_record,
                               &stream->parsed.volumes[0])) {
        state->has_record = true;
        state->current_index = 0;
    }
    return state;
}

const xx_archive_record *xx_ubi_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_ubi_archive_record_move_to_next(Abstractformat *self,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    xx_ubi_archive_stream *stream;
    if (!self || !state || state->format != self || !state->has_record ||
        !state->internal_state || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_ubi_archive_stream *)state->internal_state;
    ++stream->index;
    if (stream->index >= stream->parsed.volume_count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    if (!xx_ubi_populate_record(self, &state->current_record,
                                &stream->parsed.volumes[stream->index])) {
        state->has_record = false;
        return false;
    }
    ++state->current_index;
    return true;
}

bool xx_ubi_unpack_current_archive_record(Abstractformat *self,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    xx_ubi_archive_stream *stream;
    const xx_ubi_volume *volume;
    const xx_var *option;
    const char *name;
    const char *base = NULL;
    char *owned_base = NULL;
    char *destination_path = NULL;
    xx_io_device *destination = NULL;
    bool result = false;

    if (!self || !self->device || !state || state->format != self ||
        !state->has_record || !state->internal_state ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_ubi_archive_stream *)state->internal_state;
    if (stream->index >= stream->parsed.volume_count) return false;
    volume = &stream->parsed.volumes[stream->index];
    name = xx_archive_record_get_original_name(&state->current_record);
    if (!xx_ubi_safe_name(name)) return false;

    option = xx_ubi_find_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!option) {
        /* No destination: report whether every block of the volume is
         * addressable on the device. */
        size_t index;
        for (index = 0U; index < volume->count; ++index) {
            if (!xx_ubi_range_within(stream->parsed.input_size,
                                     volume->lebs[index].data_offset,
                                     (int64_t)volume->lebs[index].size)) {
                return false;
            }
        }
        return true;
    }
    if (option->type == XX_VAR_TYPE_STRING ||
        option->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(option);
    } else if (option->type == XX_VAR_TYPE_WSTRING ||
               option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(option));
        base = owned_base;
    }
    if (!base) goto cleanup;
    if (base[0] && base[xx_str_len(base) - 1U] != '/' &&
        base[xx_str_len(base) - 1U] != '\\') {
        destination_path = xx_str_concat3(base, "/", name);
    } else {
        destination_path = xx_str_concat(base, name);
    }
    if (!destination_path) goto cleanup;
    if (!xx_store_create_dirs_a(destination_path, false)) goto cleanup;
    destination = xx_io_file_open(destination_path, "wb");
    if (!destination) goto cleanup;
    result = xx_ubi_extract_volume(self, &stream->parsed, volume, destination,
                                   pd);
    xx_io_close(destination);
    destination = NULL;
    if (!result) xx_rt_remove(destination_path);

cleanup:
    if (destination) xx_io_close(destination);
    if (owned_base) xx_str_free(owned_base);
    if (destination_path) xx_str_free(destination_path);
    return result;
}

void xx_ubi_free_archive_records_reading(Abstractformat *self,
                                         xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

/* --------------------------------------------------------- accessors --- */

uint64_t xx_ubi_get_number_of_records(const xx_ubi *ubi) {
    return ubi ? ubi->number_of_records : 0U;
}
uint64_t xx_ubi_get_number_of_members(const xx_ubi *ubi) {
    return ubi ? ubi->number_of_members : 0U;
}
uint32_t xx_ubi_get_peb_size(const xx_ubi *ubi) {
    return ubi ? ubi->peb_size : 0U;
}
uint32_t xx_ubi_get_leb_size(const xx_ubi *ubi) {
    return ubi ? ubi->leb_size : 0U;
}
uint32_t xx_ubi_get_image_seq(const xx_ubi *ubi) {
    return ubi ? ubi->image_seq : 0U;
}
uint64_t xx_ubi_get_volume_count(const xx_ubi *ubi) {
    return ubi ? ubi->volume_count : 0U;
}
int64_t xx_ubi_get_archive_end(const xx_ubi *ubi) {
    return ubi ? ubi->archive_end : -1;
}

const char *xx_ubi_volume_type_to_string(uint32_t vol_type) {
    switch (vol_type) {
        case XX_UBI_VID_DYNAMIC: return "Dynamic";
        case XX_UBI_VID_STATIC: return "Static";
        default: return "Unknown";
    }
}
