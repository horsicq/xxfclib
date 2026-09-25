/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/yaffs/xx_yaffs.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

/* ------------------------------------------------------------------------ */
/* Layout constants                                                         */
/* ------------------------------------------------------------------------ */

#define XX_YAFFS_HEADER_SIZE 512U   /**< Bytes of an object header we read. */
#define XX_YAFFS_NAME_FIELD 10U     /**< Offset of the name inside a header. */
#define XX_YAFFS_NAME_MAX 255U      /**< YAFFS_MAX_NAME_LENGTH. */
#define XX_YAFFS_EQUIV_FIELD 296U   /**< Hardlink target object id. */
#define XX_YAFFS_ALIAS_FIELD 300U   /**< Symlink target. */
#define XX_YAFFS_ALIAS_MAX 159U     /**< YAFFS_MAX_ALIAS_LENGTH. */
#define XX_YAFFS_TAGS_SIZE 16U      /**< YAFFS2 packed tags, ECC excluded. */

/* YAFFS2 tag conventions (yaffs_packedtags2.c). A header chunk written by
 * the Linux driver carries "extra" information: its chunk id is the flag
 * plus the parent object id, and the top nibble of its object id is the
 * object type. mkyaffs2image writes plain chunk id 0 instead. */
#define XX_YAFFS_EXTRA_HEADER_FLAG 0x80000000U
#define XX_YAFFS_EXTRA_FLAGS_MASK 0xF0000000U
#define XX_YAFFS_EXTRA_TYPE_SHIFT 28U
/* Sequence numbers of live blocks; anything else is a checkpoint block, a
 * bad block marker or not YAFFS at all. */
#define XX_YAFFS_SEQ_LOWEST 0x00001000U
#define XX_YAFFS_SEQ_HIGHEST 0xEFFFFF00U
#define XX_YAFFS_SEQ_CHECKPOINT 0x00000021U
#define XX_YAFFS_SEQ_BAD_BLOCK 0xFFFF0000U
/* YAFFS1 tag fields are 20 / 18 bits wide. */
#define XX_YAFFS1_CHUNK_MASK 0xFFFFFU
#define XX_YAFFS1_OBJECT_MASK 0x3FFFFU

/* Budgets. Every one of these is a hard stop rather than a hint: the chunk
 * count, the object ids and the parent ids all come straight out of the
 * image, so an adversarial file must not be able to trade a few kilobytes of
 * input for an unbounded walk or an unbounded allocation. */
#define XX_YAFFS_MAX_CHUNKS 1048576U      /**< Chunks examined in one image. */
#define XX_YAFFS_MAX_OBJECTS 100000U      /**< Object headers retained. */
#define XX_YAFFS_MAX_DATA_CHUNKS 1048576U /**< Data chunk records retained. */
#define XX_YAFFS_MAX_DEPTH 128U           /**< Parent chain links followed. */
#define XX_YAFFS_MAX_PATH 4096U           /**< Longest rebuilt path. */
#define XX_YAFFS_PROBE_CHUNKS 64U         /**< Chunks scored per candidate. */
#define XX_YAFFS_WINDOW 262144U           /**< Read-ahead of the full scan. */
#define XX_YAFFS_MAX_COLLISIONS 1000U     /**< "name~N" attempts per name. */
/* "name~N" attempts for the whole image: ten thousand objects with one
 * name would otherwise cost fifty million attempts. */
#define XX_YAFFS_CLAIM_BUDGET 2000000U

/* Placed on objects whose parent chain does not reach the root, either
 * because the parent header is missing or because the chain loops. */
#define XX_YAFFS_LOST_DIR "lost+found"

/* ------------------------------------------------------------------------ */
/* Geometry                                                                 */
/* ------------------------------------------------------------------------ */

/** How the tags for a chunk are stored, if they are stored at all. */
typedef enum {
    XX_YAFFS_TAGS_NONE = 0, /**< No tags; layout inferred positionally. */
    XX_YAFFS_TAGS_V1 = 1,   /**< YAFFS1 struct yaffs_spare bitfields. */
    XX_YAFFS_TAGS_V2 = 2    /**< YAFFS2 packed tags2. */
} xx_yaffs_tag_kind;

typedef struct xx_yaffs_geometry_s {
    uint32_t page_size;  /**< NAND page as reported. */
    uint32_t spare_size; /**< OOB bytes per page, 0 for in-band or none. */
    uint32_t tag_offset; /**< Tags start this far into the spare area. */
    uint32_t data_size;  /**< File bytes carried by one chunk. */
    uint32_t chunk_size; /**< Distance from one chunk to the next. */
    uint32_t tag_pos;    /**< Offset of the tags inside a chunk. */
    xx_yaffs_tag_kind kind;
    bool big_endian;
    bool inband; /**< YAFFS2 in-band tags: last 16 bytes of the page. */
} xx_yaffs_geometry;

/** What a chunk's tags say it is. */
typedef enum {
    XX_YAFFS_CHUNK_ERASED = 0, /**< All ones: never written. */
    XX_YAFFS_CHUNK_SKIP,       /**< Valid flash state that holds no file
                                    data: deleted page, bad block,
                                    checkpoint block. */
    XX_YAFFS_CHUNK_INVALID,    /**< Not YAFFS tags. */
    XX_YAFFS_CHUNK_HEADER,     /**< Object header. */
    XX_YAFFS_CHUNK_DATA        /**< One page of file data. */
} xx_yaffs_chunk_class;

/** One chunk's tags, normalised across YAFFS1 and YAFFS2. */
typedef struct xx_yaffs_tags_s {
    uint32_t sequence;   /**< YAFFS2 block sequence, YAFFS1 2-bit serial. */
    uint32_t object_id;
    uint32_t chunk_id;   /**< 0 for any header chunk. */
    uint32_t byte_count;
    bool extra;          /**< YAFFS2 extra header info present. */
    uint32_t extra_parent;
    uint32_t extra_type;
    xx_yaffs_chunk_class kind;
} xx_yaffs_tags;

/** The interesting fields of a struct yaffs_obj_hdr. */
typedef struct xx_yaffs_header_s {
    uint32_t type;
    uint32_t parent_id;
    uint32_t equiv_id;
    uint64_t file_size;
    uint32_t mode;
    char name[XX_YAFFS_NAME_MAX + 1U];
    size_t name_length;
    char alias[XX_YAFFS_ALIAS_MAX + 1U];
} xx_yaffs_header;

/* ------------------------------------------------------------------------ */
/* Parsed state                                                             */
/* ------------------------------------------------------------------------ */

/* Where an object's path hangs: the index of its parent directory in
 * objects[], or one of these two. Paths are not stored whole - a deep tree
 * of long names would cost up to XX_YAFFS_MAX_PATH bytes per object - but
 * rebuilt from the leaves when a record is produced. */
#define XX_YAFFS_TOP_LEVEL SIZE_MAX          /**< Directly in the root. */
#define XX_YAFFS_LOST_PARENT (SIZE_MAX - 1U) /**< In the lost+found stand-in. */

typedef struct xx_yaffs_object_s {
    char *leaf;          /**< Unique host-safe name in its directory, owned.
                              NULL = not listed. */
    size_t parent_slot;  /**< Index of the parent, or one of the above. */
    size_t path_length;  /**< Length of the full path to this object. */
    char *name;          /**< Raw leaf name as stored, owned. */
    char *alias;         /**< Symlink target, owned, or NULL. */
    uint32_t object_id;
    uint32_t parent_id;
    uint32_t equiv_id;
    uint32_t type;
    uint32_t sequence;   /**< Version of the header that was kept. */
    uint64_t file_size;
    int64_t header_offset;
    size_t first_chunk;  /**< First entry of this object in chunks[]. */
    size_t chunk_count;
    uint8_t resolve;     /**< 0 unseen, 1 in progress, 2 resolved. */
    bool orphan;         /**< Parent chain broken, looped or too deep. */
    bool excluded;       /**< Deleted, unlinked or unrepresentable. */
} xx_yaffs_object;

typedef struct xx_yaffs_data_chunk_s {
    uint32_t object_id;   /**< Object id during the scan, index after. */
    uint32_t chunk_id;
    uint32_t sequence;
    uint32_t byte_count;
    int64_t offset;       /**< Offset of the page data, not of the chunk. */
} xx_yaffs_data_chunk;

/** Open addressed object id -> object index map. Slot key 0 means empty, so
 * the stored key is the object id plus one; object id 0 is never valid and
 * 0xFFFFFFFF is rejected before it ever reaches here. */
typedef struct xx_yaffs_slot_s {
    uint32_t key;
    uint32_t index;
} xx_yaffs_slot;

typedef struct xx_yaffs_map_s {
    xx_yaffs_slot *slots;
    size_t capacity;
    size_t count;
} xx_yaffs_map;

/** One name handed out: a leaf inside one directory. */
typedef struct xx_yaffs_name_entry_s {
    const char *leaf;     /**< NULL = empty slot. Owned by an object. */
    size_t parent;        /**< The directory, as an object parent_slot. */
    uint32_t hash;
    uint32_t next_suffix; /**< Next "~N" to try when this leaf is wanted
                               again, so that N objects with one name cost
                               N attempts rather than N * N / 2. */
} xx_yaffs_name_entry;

/** Set of the names already handed out, per directory, compared the way a
 * case-insensitive host compares them (xx_yaffs_fold). */
typedef struct xx_yaffs_names_s {
    xx_yaffs_name_entry *slots;
    size_t capacity;
    size_t used;
} xx_yaffs_names;

typedef struct xx_yaffs_private_s {
    xx_yaffs_geometry geometry;
    xx_yaffs_object *objects;
    size_t object_count;
    size_t object_capacity;
    xx_yaffs_data_chunk *chunks;
    size_t chunk_count;
    size_t chunk_capacity;
    xx_yaffs_map map;
    xx_yaffs_names names;
    size_t claim_budget;  /**< Unique-name attempts left. */
    char *lost_leaf;      /**< Top-level name orphans are filed under. */
    bool lost_tried;      /**< lost_leaf has been claimed, or tried. */
    size_t *records;      /**< Object indexes that make up the listing. */
    size_t record_count;
    int64_t input_size;
    int64_t archive_end;
    int64_t total_chunks;
} xx_yaffs_private;

typedef struct xx_yaffs_archive_stream_s {
    xx_yaffs_private parsed;
    size_t index;
    uint64_t link_budget; /**< Bytes hardlink copies may still write. */
} xx_yaffs_archive_stream;

static void xx_yaffs_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------------------ */
/* Small helpers                                                            */
/* ------------------------------------------------------------------------ */

/* Positioned read. xx_io_seek64() rather than xx_io_seek(), because long is
 * 32 bits on Win64 and a NAND image is routinely larger than 2 GiB. */
static bool xx_yaffs_read_at(xx_io_device *device, int64_t offset, void *data,
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

static bool xx_yaffs_range_within(int64_t total_size, int64_t offset,
                                  int64_t size) {
    return (total_size >= 0) && (offset >= 0) && (size >= 0) &&
           (offset <= total_size) && (size <= total_size - offset);
}

/* Reads bounded by the input size, optionally through a read-ahead window.
 * The full scan visits every chunk in order and takes a few bytes from
 * each; straight from the device that is one or two system calls per chunk
 * and, on a cold disk, one small random read each. The detection probe
 * reads too little to need the window and runs unbuffered. */
typedef struct xx_yaffs_source_s {
    xx_io_device *device;
    int64_t total_size;
    uint8_t *window;      /**< NULL: read straight from the device. */
    int64_t window_start; /**< Offset of window[0]; -1 when empty. */
    size_t window_length;
} xx_yaffs_source;

static void xx_yaffs_source_init(xx_yaffs_source *source,
                                 xx_io_device *device, int64_t total_size,
                                 bool buffered) {
    source->device = device;
    source->total_size = total_size;
    source->window_start = -1;
    source->window_length = 0U;
    /* A failed allocation only costs speed. */
    source->window = buffered ? (uint8_t *)xx_mem_alloc(XX_YAFFS_WINDOW) : NULL;
}

static void xx_yaffs_source_cleanup(xx_yaffs_source *source) {
    if (source->window) xx_mem_free(source->window);
    source->window = NULL;
    source->window_start = -1;
}

static bool xx_yaffs_source_read(xx_yaffs_source *source, int64_t offset,
                                 void *data, size_t size) {
    if (!xx_yaffs_range_within(source->total_size, offset, (int64_t)size)) {
        return false;
    }
    if (!source->window || size > XX_YAFFS_WINDOW) {
        return xx_yaffs_read_at(source->device, offset, data, size);
    }
    if (source->window_start < 0 || offset < source->window_start ||
        offset - source->window_start >
            (int64_t)source->window_length - (int64_t)size) {
        int64_t left = source->total_size - offset;
        size_t want = left < (int64_t)XX_YAFFS_WINDOW ? (size_t)left
                                                      : XX_YAFFS_WINDOW;
        source->window_start = -1;
        if (!xx_yaffs_read_at(source->device, offset, source->window, want)) {
            return false;
        }
        source->window_start = offset;
        source->window_length = want;
    }
    xx_mem_copy(data, source->window + (size_t)(offset - source->window_start),
                size);
    return true;
}

static unsigned xx_yaffs_popcount8(uint8_t value) {
    unsigned count = 0U;
    while (value != 0U) {
        count += value & 1U;
        value = (uint8_t)(value >> 1U);
    }
    return count;
}

static bool xx_yaffs_all_ones(const uint8_t *data, size_t size) {
    size_t index;
    for (index = 0U; index < size; ++index) {
        if (data[index] != 0xFFU) return false;
    }
    return true;
}

/* ------------------------------------------------------------------------ */
/* Tag decoding                                                             */
/* ------------------------------------------------------------------------ */

/* YAFFS2: four words, sequence / object / chunk / byte count. Images built
 * without the ECC layout carry two bad-block-marker bytes first, which is
 * what geometry.tag_offset absorbs; in-band tags sit at the end of the page.
 */
static void xx_yaffs_decode_tags_v2(const uint8_t *raw, uint32_t data_size,
                                    bool big_endian, xx_yaffs_tags *tags) {
    uint32_t sequence = xx_data_get_u32(raw, XX_YAFFS_TAGS_SIZE, 0U, big_endian);
    uint32_t object_id =
        xx_data_get_u32(raw, XX_YAFFS_TAGS_SIZE, 4U, big_endian);
    uint32_t chunk_id = xx_data_get_u32(raw, XX_YAFFS_TAGS_SIZE, 8U, big_endian);
    uint32_t byte_count =
        xx_data_get_u32(raw, XX_YAFFS_TAGS_SIZE, 12U, big_endian);
    xx_mem_zero(tags, sizeof(*tags));
    tags->sequence = sequence;
    tags->object_id = object_id;
    tags->chunk_id = chunk_id;
    tags->byte_count = byte_count;
    if (sequence == 0xFFFFFFFFU && object_id == 0xFFFFFFFFU) {
        tags->kind = XX_YAFFS_CHUNK_ERASED;
        return;
    }
    if (sequence == XX_YAFFS_SEQ_CHECKPOINT ||
        sequence == XX_YAFFS_SEQ_BAD_BLOCK) {
        tags->kind = XX_YAFFS_CHUNK_SKIP;
        return;
    }
    if (sequence < XX_YAFFS_SEQ_LOWEST || sequence > XX_YAFFS_SEQ_HIGHEST) {
        tags->kind = XX_YAFFS_CHUNK_INVALID;
        return;
    }
    if ((chunk_id & XX_YAFFS_EXTRA_HEADER_FLAG) != 0U) {
        tags->extra = true;
        tags->extra_parent = chunk_id & ~XX_YAFFS_EXTRA_FLAGS_MASK;
        tags->extra_type = object_id >> XX_YAFFS_EXTRA_TYPE_SHIFT;
        tags->object_id = object_id & ~XX_YAFFS_EXTRA_FLAGS_MASK;
        tags->chunk_id = 0U;
        tags->byte_count = 0U;
        tags->kind = tags->object_id != 0U ? XX_YAFFS_CHUNK_HEADER
                                           : XX_YAFFS_CHUNK_INVALID;
        return;
    }
    /* Outside a header the type nibble is never set. */
    if (object_id == 0U || (object_id & XX_YAFFS_EXTRA_FLAGS_MASK) != 0U) {
        tags->kind = XX_YAFFS_CHUNK_INVALID;
        return;
    }
    if (chunk_id == 0U) {
        tags->kind = XX_YAFFS_CHUNK_HEADER;
        return;
    }
    tags->kind = byte_count <= data_size ? XX_YAFFS_CHUNK_DATA
                                         : XX_YAFFS_CHUNK_INVALID;
}

/* YAFFS1: struct yaffs_spare is sixteen bytes
 *
 *     tb0 tb1 tb2 tb3 page_status block_status tb4 tb5
 *     ecc1[3] tb6 tb7 ecc2[3]
 *
 * and tb0..tb7 are struct yaffs_tags, eight bytes of compiler packed
 *
 *     chunk_id : 20, serial_number : 2, byte_count : 10,
 *     object_id : 18, ecc : 12, (byte_count_msb) : 2
 *
 * A little endian target fills each 32-bit unit from the least significant
 * bit up; a big endian one from the most significant bit down. */
static void xx_yaffs_decode_tags_v1(const uint8_t *spare, uint32_t data_size,
                                    bool big_endian, xx_yaffs_tags *tags) {
    uint8_t raw[8];
    uint32_t low;
    uint32_t high;
    xx_mem_zero(tags, sizeof(*tags));
    raw[0] = spare[0];
    raw[1] = spare[1];
    raw[2] = spare[2];
    raw[3] = spare[3];
    raw[4] = spare[6];
    raw[5] = spare[7];
    raw[6] = spare[11];
    raw[7] = spare[12];
    if (xx_yaffs_all_ones(raw, sizeof(raw))) {
        tags->kind = XX_YAFFS_CHUNK_ERASED;
        return;
    }
    low = xx_data_get_u32(raw, sizeof(raw), 0U, big_endian);
    high = xx_data_get_u32(raw, sizeof(raw), 4U, big_endian);
    if (big_endian) {
        tags->chunk_id = (low >> 12U) & XX_YAFFS1_CHUNK_MASK;
        tags->sequence = (low >> 10U) & 3U;
        tags->byte_count = low & 0x3FFU;
        tags->object_id = (high >> 14U) & XX_YAFFS1_OBJECT_MASK;
    } else {
        tags->chunk_id = low & XX_YAFFS1_CHUNK_MASK;
        tags->sequence = (low >> 20U) & 3U;
        tags->byte_count = (low >> 22U) & 0x3FFU;
        tags->object_id = high & XX_YAFFS1_OBJECT_MASK;
    }
    /* A cleared page status marks a deleted chunk, a cleared block status a
     * bad block; one flipped bit is tolerated, as YAFFS1 itself does. */
    if (xx_yaffs_popcount8(spare[4]) < 7U || xx_yaffs_popcount8(spare[5]) < 7U) {
        tags->kind = XX_YAFFS_CHUNK_SKIP;
        return;
    }
    if (tags->object_id == 0U || tags->object_id == XX_YAFFS1_OBJECT_MASK) {
        tags->kind = XX_YAFFS_CHUNK_INVALID;
        return;
    }
    if (tags->chunk_id == 0U) {
        tags->kind = XX_YAFFS_CHUNK_HEADER;
        return;
    }
    tags->kind = tags->byte_count <= data_size ? XX_YAFFS_CHUNK_DATA
                                               : XX_YAFFS_CHUNK_INVALID;
}

/* Read and decode the tags of the chunk that starts at chunk_offset. Returns
 * false when the tag bytes are missing or unreadable. */
static bool xx_yaffs_read_tags(xx_yaffs_source *source,
                               const xx_yaffs_geometry *geometry,
                               int64_t chunk_offset, xx_yaffs_tags *tags) {
    uint8_t raw[XX_YAFFS_TAGS_SIZE];
    int64_t offset;
    if (!geometry || !tags || geometry->kind == XX_YAFFS_TAGS_NONE) return false;
    offset = chunk_offset + (int64_t)geometry->tag_pos;
    if (!xx_yaffs_source_read(source, offset, raw, sizeof(raw))) return false;
    if (geometry->kind == XX_YAFFS_TAGS_V1) {
        xx_yaffs_decode_tags_v1(raw, geometry->data_size, geometry->big_endian,
                                tags);
    } else {
        xx_yaffs_decode_tags_v2(raw, geometry->data_size, geometry->big_endian,
                                tags);
    }
    return true;
}

/* ------------------------------------------------------------------------ */
/* Object header decoding                                                   */
/* ------------------------------------------------------------------------ */

/* Decode a 512 byte object header page. The layout is documented in the
 * public header. With @p strict the name must also be one printable path
 * component - the form every image builder writes - which is what the
 * detection gate uses; the full scan only refuses what cannot be a name at
 * all and leaves the rest to the host-safe renaming. */
static bool xx_yaffs_decode_header(const uint8_t *page, bool big_endian,
                                   bool strict, xx_yaffs_header *header) {
    uint32_t checksum;
    uint32_t size_low;
    uint32_t size_high;
    size_t length = 0U;
    size_t index;
    xx_mem_zero(header, sizeof(*header));
    header->type = xx_data_get_u32(page, XX_YAFFS_HEADER_SIZE, 0U, big_endian);
    header->parent_id =
        xx_data_get_u32(page, XX_YAFFS_HEADER_SIZE, 4U, big_endian);
    checksum = xx_data_get_u16(page, XX_YAFFS_HEADER_SIZE, 8U, big_endian);
    /* sum_no_longer_used has been a fixed 0xFFFF since YAFFS dropped name
     * checksums; it is the single most useful structural constraint there
     * is, and binwalk and unblob lean on it for the same reason. */
    if (checksum != 0xFFFFU) return false;
    if (header->type < XX_YAFFS_OBJECT_TYPE_FILE ||
        header->type > XX_YAFFS_OBJECT_TYPE_SPECIAL) {
        return false;
    }
    if (header->parent_id == 0U || header->parent_id == 0xFFFFFFFFU) {
        return false;
    }
    /* At most YAFFS_MAX_NAME_LENGTH bytes, as the driver's
     * strncpy(name, oh->name, 255) reads it: a 255 byte name written with
     * strncpy() by older image builders has no NUL, and byte 255 of the
     * field is then left 0xFF by their memset. */
    while (length < XX_YAFFS_NAME_MAX &&
           page[XX_YAFFS_NAME_FIELD + length] != 0U) {
        ++length;
    }
    for (index = 0U; index < length; ++index) {
        unsigned char ch = page[XX_YAFFS_NAME_FIELD + index];
        if (ch == '/') return false;
        if (strict && (ch < 32U || ch == 127U || ch == '\\')) return false;
    }
    if (length != 0U) {
        xx_mem_copy(header->name, page + XX_YAFFS_NAME_FIELD, length);
    }
    header->name[length] = '\0';
    header->name_length = length;
    header->mode = xx_data_get_u32(page, XX_YAFFS_HEADER_SIZE, 268U, big_endian);
    header->equiv_id = xx_data_get_u32(page, XX_YAFFS_HEADER_SIZE,
                                       XX_YAFFS_EQUIV_FIELD, big_endian);
    size_low = xx_data_get_u32(page, XX_YAFFS_HEADER_SIZE, 292U, big_endian);
    size_high = xx_data_get_u32(page, XX_YAFFS_HEADER_SIZE, 496U, big_endian);
    /* An all-ones word means "field unused", not "four billion". */
    if (size_high != 0xFFFFFFFFU) {
        header->file_size = ((uint64_t)size_high << 32U) | (uint64_t)size_low;
    } else if (size_low != 0xFFFFFFFFU) {
        header->file_size = size_low;
    } else {
        header->file_size = 0U;
    }
    if (header->type == XX_YAFFS_OBJECT_TYPE_SYMLINK) {
        const uint8_t *alias = page + XX_YAFFS_ALIAS_FIELD;
        size_t alias_length = 0U;
        while (alias_length < XX_YAFFS_ALIAS_MAX && alias[alias_length] != 0U &&
               alias[alias_length] != 0xFFU) {
            ++alias_length;
        }
        if (alias_length != 0U) xx_mem_copy(header->alias, alias, alias_length);
        header->alias[alias_length] = '\0';
    }
    return true;
}

static bool xx_yaffs_read_header(xx_yaffs_source *source,
                                 int64_t chunk_offset, bool big_endian,
                                 bool strict, xx_yaffs_header *header) {
    uint8_t page[XX_YAFFS_HEADER_SIZE];
    if (!xx_yaffs_source_read(source, chunk_offset, page, sizeof(page))) {
        return false;
    }
    return xx_yaffs_decode_header(page, big_endian, strict, header);
}

/* The first chunk of every image an image builder writes is an object in
 * the root directory: mkyaffs2image starts with the first entry of the
 * source tree, mkyaffsimage and the kernel with the root directory itself,
 * whose parent is also 1. A hardlink cannot come first, since its target
 * must precede it. This is the whole of the detection signature:
 *
 *     type (1 file, 2 symlink, 3 directory, 5 special)  u32
 *     parent object id 1                                 u32
 *     FF FF                                              u16
 *     a printable name, NUL terminated (empty only for the root)
 */
static bool xx_yaffs_first_header(const uint8_t *page, bool big_endian,
                                  xx_yaffs_header *header) {
    if (!xx_yaffs_decode_header(page, big_endian, true, header)) return false;
    if (header->parent_id != XX_YAFFS_OBJECTID_ROOT ||
        header->type == XX_YAFFS_OBJECT_TYPE_HARDLINK) {
        return false;
    }
    if (header->name_length == 0U &&
        header->type != XX_YAFFS_OBJECT_TYPE_DIRECTORY) {
        return false;
    }
    return true;
}

/* ------------------------------------------------------------------------ */
/* Geometry detection                                                       */
/* ------------------------------------------------------------------------ */

/* The candidate space, in the order it is searched. Ties are broken by this
 * order - the first candidate to reach a given score keeps it - so the sizes
 * are listed with the common real-world value first. */
static const uint32_t xx_yaffs_page_sizes[] = {2048U, 512U,  1024U,
                                               4096U, 8192U, 16384U};
static const uint32_t xx_yaffs_spare_sizes[] = {64U,  16U,  32U, 128U,
                                                256U, 512U, 224U, 448U};

/* Chunk 0 against one tagged candidate: its tags must describe an object
 * header, and when the driver stored the parent and type in the tags as
 * well they must agree with the page. */
static bool xx_yaffs_first_tags_match(const xx_yaffs_tags *tags,
                                      const xx_yaffs_header *header) {
    if (tags->kind != XX_YAFFS_CHUNK_HEADER) return false;
    if (tags->extra && (tags->extra_parent != header->parent_id ||
                        tags->extra_type != header->type)) {
        return false;
    }
    /* Only the root directory is object 1, and it is nameless. */
    if (tags->object_id == XX_YAFFS_OBJECTID_ROOT &&
        header->type != XX_YAFFS_OBJECT_TYPE_DIRECTORY) {
        return false;
    }
    return true;
}

/* Values a real image carries: sequence numbers count block allocations up
 * from 0x1000 and object ids are handed out densely from 257, so both stay
 * small. Tags read from the wrong spare offset still decode - two bytes
 * early, a YAFFS2 sequence of 0x1000 reads as 0x1000FFFF - but their numbers
 * are not small, and that is what separates the two candidates. */
static bool xx_yaffs_plausible_tags(const xx_yaffs_geometry *geometry,
                                    const xx_yaffs_tags *tags) {
    if (geometry->kind != XX_YAFFS_TAGS_V2) return true;
    return tags->sequence <= 0x00FFFFFFU && tags->object_id <= 0x0003FFFFU;
}

/* Score one tagged geometry over the leading chunks of the image. A negative
 * score rejects the candidate outright. */
static int xx_yaffs_score_tagged(xx_yaffs_source *source,
                                 const xx_yaffs_geometry *geometry,
                                 int64_t base,
                                 const xx_yaffs_header *first) {
    xx_yaffs_header header;
    xx_yaffs_tags tags;
    uint32_t seen[XX_YAFFS_PROBE_CHUNKS];
    size_t seen_count = 0U;
    int64_t chunk_size = (int64_t)geometry->chunk_size;
    int64_t chunks = (source->total_size - base) / chunk_size;
    int64_t index;
    int valid = 0;
    int invalid = 0;
    int score;
    if (chunks < 1) return -1;
    if (!xx_yaffs_read_tags(source, geometry, base, &tags) ||
        !xx_yaffs_first_tags_match(&tags, first)) {
        return -1;
    }
    score = 30;
    if (xx_yaffs_plausible_tags(geometry, &tags)) score += 2;
    seen[seen_count++] = tags.object_id;
    if (chunks > (int64_t)XX_YAFFS_PROBE_CHUNKS) {
        chunks = (int64_t)XX_YAFFS_PROBE_CHUNKS;
    }
    for (index = 1; index < chunks; ++index) {
        int64_t offset = base + index * chunk_size;
        if (!xx_yaffs_read_tags(source, geometry, offset, &tags)) break;
        if ((tags.kind == XX_YAFFS_CHUNK_HEADER ||
             tags.kind == XX_YAFFS_CHUNK_DATA) &&
            xx_yaffs_plausible_tags(geometry, &tags)) {
            score += 2;
        }
        switch (tags.kind) {
        case XX_YAFFS_CHUNK_HEADER:
            /* Header tags over a page that is not a header are as bad as
             * no tags at all. */
            if (xx_yaffs_read_header(source, offset, geometry->big_endian,
                                     false, &header) &&
                (!tags.extra || (tags.extra_parent == header.parent_id &&
                                 tags.extra_type == header.type))) {
                size_t at;
                score += 5;
                ++valid;
                /* A parent the tags already introduced: the tree holds
                 * together under this reading of the tags. */
                for (at = 0U; at < seen_count; ++at) {
                    if (seen[at] == header.parent_id) {
                        score += 3;
                        break;
                    }
                }
                if (seen_count < XX_YAFFS_PROBE_CHUNKS) {
                    seen[seen_count++] = tags.object_id;
                }
            } else {
                score -= 3;
                ++invalid;
            }
            break;
        case XX_YAFFS_CHUNK_DATA:
            score += 2;
            ++valid;
            break;
        case XX_YAFFS_CHUNK_INVALID:
            score -= 3;
            ++invalid;
            break;
        default:
            /* Erased, deleted, bad or checkpoint: no evidence either way. */
            break;
        }
    }
    /* More garbage than YAFFS among the chunks that follow means the tags
     * of chunk 0 matched by chance. */
    if (invalid > valid) return -1;
    return score;
}

/* Score the tag-less layout: nothing to key on but the page contents, so a
 * page must be a header exactly where the previous file's data ends - the
 * order mkyaffs2image writes. Any miss rejects the candidate, since this
 * layout is only a fallback for an image whose spare area was stripped. */
static int xx_yaffs_score_positional(xx_yaffs_source *source,
                                     const xx_yaffs_geometry *geometry,
                                     int64_t base,
                                     const xx_yaffs_header *first) {
    xx_yaffs_header header;
    int64_t chunk_size = (int64_t)geometry->chunk_size;
    int64_t chunks = (source->total_size - base) / chunk_size;
    int64_t index = 0;
    int headers = 0;
    int probed = 0;
    if (chunks < 2) return -1;
    header = *first;
    for (;;) {
        uint8_t page[XX_YAFFS_HEADER_SIZE];
        int64_t offset;
        ++headers;
        if (header.type == XX_YAFFS_OBJECT_TYPE_FILE) {
            uint64_t pages = (header.file_size + geometry->data_size - 1U) /
                             geometry->data_size;
            if (pages > (uint64_t)chunks) return -1;
            index += (int64_t)pages;
        }
        ++index;
        if (index >= chunks || ++probed >= (int)XX_YAFFS_PROBE_CHUNKS) break;
        offset = base + index * chunk_size;
        if (!xx_yaffs_source_read(source, offset, page, sizeof(page))) {
            return -1;
        }
        /* An erased page ends the image. */
        if (xx_yaffs_all_ones(page, sizeof(page))) break;
        if (!xx_yaffs_decode_header(page, geometry->big_endian, false,
                                    &header) ||
            header.name_length == 0U) {
            return -1;
        }
    }
    return headers >= 2 ? 10 + headers : -1;
}

static void xx_yaffs_set_oob(xx_yaffs_geometry *geometry, uint32_t page,
                             uint32_t spare, uint32_t tag_offset) {
    geometry->page_size = page;
    geometry->spare_size = spare;
    geometry->tag_offset = tag_offset;
    geometry->data_size = page;
    geometry->chunk_size = page + spare;
    geometry->tag_pos = page + tag_offset;
    geometry->inband = false;
}

/* Brute-force the page size, spare size, spare offset, endianness and tag
 * layout. Everything is keyed on chunk 0, which must be the first object
 * header (xx_yaffs_first_header); a file whose first 512 bytes are not that
 * costs one read and two decodes. */
static bool xx_yaffs_detect(xx_io_device *device, int64_t base,
                            int64_t total_size, xx_yaffs_geometry *out,
                            xx_pd_struct *pd) {
    uint8_t page[XX_YAFFS_HEADER_SIZE];
    xx_yaffs_header first[2];
    bool first_ok[2];
    xx_yaffs_geometry best;
    xx_yaffs_source source;
    int best_score = 0;
    bool stopped = false;
    size_t endian_index;
    size_t page_index;
    size_t spare_index;
    if (!device || !out) return false;
    xx_mem_zero(&best, sizeof(best));
    if (!xx_yaffs_range_within(total_size, base, (int64_t)sizeof(page)) ||
        !xx_yaffs_read_at(device, base, page, sizeof(page))) {
        return false;
    }
    first_ok[0] = xx_yaffs_first_header(page, false, &first[0]);
    first_ok[1] = xx_yaffs_first_header(page, true, &first[1]);
    if (!first_ok[0] && !first_ok[1]) return false;

    /* Past the gate: the candidates read the leading chunks over and over,
     * so they share one read-ahead window. */
    xx_yaffs_source_init(&source, device, total_size, true);
    for (endian_index = 0U; endian_index < 2U && !stopped; ++endian_index) {
        if (!first_ok[endian_index]) continue;
        for (page_index = 0U; page_index < sizeof(xx_yaffs_page_sizes) /
                                               sizeof(xx_yaffs_page_sizes[0]);
             ++page_index) {
            uint32_t page_size = xx_yaffs_page_sizes[page_index];
            xx_yaffs_geometry candidate;
            int score;
            xx_mem_zero(&candidate, sizeof(candidate));
            candidate.big_endian = endian_index != 0U;
            if (pd && xx_pd_is_stopped(pd)) {
                stopped = true;
                break;
            }
            /* Out-of-band tags: YAFFS2 at spare offset 0 or 2, YAFFS1. */
            for (spare_index = 0U;
                 spare_index < sizeof(xx_yaffs_spare_sizes) /
                                   sizeof(xx_yaffs_spare_sizes[0]);
                 ++spare_index) {
                uint32_t spare = xx_yaffs_spare_sizes[spare_index];
                uint32_t variant;
                if ((uint64_t)spare * 8U > page_size) continue;
                for (variant = 0U; variant < 3U; ++variant) {
                    uint32_t tag_offset = variant == 1U ? 2U : 0U;
                    candidate.kind =
                        variant == 2U ? XX_YAFFS_TAGS_V1 : XX_YAFFS_TAGS_V2;
                    if (spare < tag_offset + XX_YAFFS_TAGS_SIZE) continue;
                    xx_yaffs_set_oob(&candidate, page_size, spare, tag_offset);
                    score = xx_yaffs_score_tagged(&source, &candidate, base,
                                                  &first[endian_index]);
                    if (score > best_score) {
                        best_score = score;
                        best = candidate;
                    }
                }
            }
            /* In-band tags: the last sixteen bytes of every page. */
            candidate.kind = XX_YAFFS_TAGS_V2;
            candidate.page_size = page_size;
            candidate.spare_size = 0U;
            candidate.tag_offset = 0U;
            candidate.data_size = page_size - XX_YAFFS_TAGS_SIZE;
            candidate.chunk_size = page_size;
            candidate.tag_pos = page_size - XX_YAFFS_TAGS_SIZE;
            candidate.inband = true;
            score = xx_yaffs_score_tagged(&source, &candidate, base,
                                          &first[endian_index]);
            if (score > best_score) {
                best_score = score;
                best = candidate;
            }
        }
    }
    /* The tag-less layout is a last resort: it has no tags to key on, so it
     * is only tried when nothing with tags matched at all. */
    if (best_score <= 0 && !stopped) {
        for (endian_index = 0U; endian_index < 2U && !stopped; ++endian_index) {
            if (!first_ok[endian_index]) continue;
            for (page_index = 0U;
                 page_index < sizeof(xx_yaffs_page_sizes) /
                                  sizeof(xx_yaffs_page_sizes[0]);
                 ++page_index) {
                xx_yaffs_geometry candidate;
                int score;
                if (pd && xx_pd_is_stopped(pd)) {
                    stopped = true;
                    break;
                }
                xx_mem_zero(&candidate, sizeof(candidate));
                candidate.kind = XX_YAFFS_TAGS_NONE;
                candidate.big_endian = endian_index != 0U;
                xx_yaffs_set_oob(&candidate, xx_yaffs_page_sizes[page_index],
                                 0U, 0U);
                score = xx_yaffs_score_positional(&source, &candidate, base,
                                                  &first[endian_index]);
                if (score > best_score) {
                    best_score = score;
                    best = candidate;
                }
            }
        }
    }
    xx_yaffs_source_cleanup(&source);
    if (stopped || best_score <= 0) return false;
    *out = best;
    return true;
}

/* ------------------------------------------------------------------------ */
/* Object id map                                                            */
/* ------------------------------------------------------------------------ */

static void xx_yaffs_map_cleanup(xx_yaffs_map *map) {
    if (!map) return;
    if (map->slots) xx_mem_free(map->slots);
    xx_mem_zero(map, sizeof(*map));
}

static size_t xx_yaffs_map_slot(const xx_yaffs_map *map, uint32_t object_id) {
    uint64_t key = (uint64_t)object_id;
    key = (key ^ (key >> 29U)) * UINT64_C(0xbf58476d1ce4e5b9);
    key ^= key >> 32U;
    return (size_t)key & (map->capacity - 1U);
}

static bool xx_yaffs_map_put(xx_yaffs_map *map, uint32_t object_id,
                             uint32_t index);

static bool xx_yaffs_map_grow(xx_yaffs_map *map) {
    xx_yaffs_map grown;
    size_t capacity = map->capacity ? map->capacity * 2U : 256U;
    size_t index;
    if (capacity < map->capacity || capacity > SIZE_MAX / sizeof(*grown.slots)) {
        return false;
    }
    grown.slots = (xx_yaffs_slot *)xx_mem_calloc(capacity, sizeof(*grown.slots));
    if (!grown.slots) return false;
    grown.capacity = capacity;
    grown.count = 0U;
    for (index = 0U; index < map->capacity; ++index) {
        if (map->slots[index].key != 0U) {
            if (!xx_yaffs_map_put(&grown, map->slots[index].key - 1U,
                                  map->slots[index].index)) {
                xx_mem_free(grown.slots);
                return false;
            }
        }
    }
    if (map->slots) xx_mem_free(map->slots);
    *map = grown;
    return true;
}

/* Insert or overwrite. Object ids are attacker chosen, so the table is sized
 * by the number of objects actually kept, which the object cap bounds. */
static bool xx_yaffs_map_put(xx_yaffs_map *map, uint32_t object_id,
                             uint32_t index) {
    size_t slot;
    if (!map || object_id == 0xFFFFFFFFU) return false;
    if ((map->count + 1U) * 4U >= map->capacity * 3U) {
        if (!xx_yaffs_map_grow(map)) return false;
    }
    slot = xx_yaffs_map_slot(map, object_id);
    while (map->slots[slot].key != 0U) {
        if (map->slots[slot].key == object_id + 1U) {
            map->slots[slot].index = index;
            return true;
        }
        slot = (slot + 1U) & (map->capacity - 1U);
    }
    map->slots[slot].key = object_id + 1U;
    map->slots[slot].index = index;
    ++map->count;
    return true;
}

static bool xx_yaffs_map_get(const xx_yaffs_map *map, uint32_t object_id,
                             size_t *out_index) {
    size_t slot;
    if (!map || !map->slots || object_id == 0xFFFFFFFFU) return false;
    slot = xx_yaffs_map_slot(map, object_id);
    while (map->slots[slot].key != 0U) {
        if (map->slots[slot].key == object_id + 1U) {
            if (out_index) *out_index = map->slots[slot].index;
            return true;
        }
        slot = (slot + 1U) & (map->capacity - 1U);
    }
    return false;
}

/* ------------------------------------------------------------------------ */
/* Host-safe, unique names                                                  */
/* ------------------------------------------------------------------------ */

static char xx_yaffs_upper(char c) {
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

/* Windows device names, with or without an extension, in any case:
 * CON PRN AUX NUL CONIN$ CONOUT$ CLOCK$, and COM / LPT followed by a digit or
 * a superscript one, two or three. */
static bool xx_yaffs_reserved_name(const char *name) {
    static const char *const devices[] = {"CON",    "PRN",     "AUX",   "NUL",
                                          "CONIN$", "CONOUT$", "CLOCK$"};
    size_t stem = 0U;
    size_t index;
    size_t word;
    while (name[stem] && name[stem] != '.') ++stem;
    while (stem > 0U && name[stem - 1U] == ' ') --stem;
    for (word = 0U; word < sizeof(devices) / sizeof(devices[0]); ++word) {
        const char *device = devices[word];
        for (index = 0U; index < stem; ++index) {
            if (!device[index] || xx_yaffs_upper(name[index]) != device[index]) {
                break;
            }
        }
        if (index == stem && device[stem] == 0) return true;
    }
    if (stem >= 4U &&
        ((xx_yaffs_upper(name[0]) == 'C' && xx_yaffs_upper(name[1]) == 'O' &&
          xx_yaffs_upper(name[2]) == 'M') ||
         (xx_yaffs_upper(name[0]) == 'L' && xx_yaffs_upper(name[1]) == 'P' &&
          xx_yaffs_upper(name[2]) == 'T'))) {
        if (stem == 4U && name[3] >= '0' && name[3] <= '9') return true;
        /* U+00B9, U+00B2, U+00B3 in UTF-8. */
        if (stem == 5U && (uint8_t)name[3] == 0xC2U &&
            ((uint8_t)name[4] == 0xB9U || (uint8_t)name[4] == 0xB2U ||
             (uint8_t)name[4] == 0xB3U)) {
            return true;
        }
    }
    return false;
}

/* Length of the well-formed UTF-8 sequence at @p text, or 0. */
static size_t xx_yaffs_utf8_length(const uint8_t *text, size_t available) {
    uint8_t lead = text[0];
    size_t length;
    size_t index;
    uint32_t value;
    if (lead < 0x80U) return 1U;
    if (lead >= 0xC2U && lead <= 0xDFU) {
        length = 2U;
        value = lead & 0x1FU;
    } else if (lead >= 0xE0U && lead <= 0xEFU) {
        length = 3U;
        value = lead & 0x0FU;
    } else if (lead >= 0xF0U && lead <= 0xF4U) {
        length = 4U;
        value = lead & 0x07U;
    } else {
        return 0U;
    }
    if (length > available) return 0U;
    for (index = 1U; index < length; ++index) {
        if ((text[index] & 0xC0U) != 0x80U) return 0U;
        value = (value << 6U) | (text[index] & 0x3FU);
    }
    if ((length == 3U && (value < 0x800U || (value >= 0xD800U &&
                                              value <= 0xDFFFU))) ||
        (length == 4U && (value < 0x10000U || value > 0x10FFFFU))) {
        return 0U;
    }
    return length;
}

/* One stored name as one host-safe path component: bytes that are not
 * well-formed UTF-8, control characters and the punctuation Windows
 * reserves become '_', trailing dots and spaces (which Windows drops, and
 * which make "." and "..") become '_', and a device name gets a '_' prefix.
 */
static char *xx_yaffs_component(const char *raw) {
    size_t size = xx_str_len(raw);
    size_t at = 0U;
    size_t index = 0U;
    char *result = (char *)xx_mem_alloc(size + 3U);
    if (!result) return NULL;
    result[at++] = '_'; /* room for a device-name prefix */
    while (index < size) {
        const uint8_t *cursor = (const uint8_t *)raw + index;
        size_t length = xx_yaffs_utf8_length(cursor, size - index);
        if (length == 0U) {
            result[at++] = '_';
            ++index;
            continue;
        }
        if (length == 1U) {
            char c = (char)cursor[0];
            if ((uint8_t)c < 0x20U || (uint8_t)c == 0x7FU || c == '/' ||
                c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' ||
                c == '<' || c == '>' || c == '|') {
                c = '_';
            }
            result[at++] = c;
        } else {
            xx_mem_copy(result + at, cursor, length);
            at += length;
        }
        index += length;
    }
    for (index = at; index > 1U; --index) {
        if (result[index - 1U] != '.' && result[index - 1U] != ' ') break;
        result[index - 1U] = '_';
    }
    if (at == 1U) result[at++] = '_';
    result[at] = '\0';
    if (!xx_yaffs_reserved_name(result + 1U)) {
        xx_rt_memmove(result, result + 1U, at);
    }
    return result;
}

/* The code point at *cursor, advancing past it. Leaves are well-formed UTF-8
 * by the time they get here (xx_yaffs_component); a stray byte would be
 * taken as itself. */
static uint32_t xx_yaffs_next_code_point(const char **cursor) {
    const uint8_t *text = (const uint8_t *)*cursor;
    size_t length = xx_yaffs_utf8_length(text, 4U);
    uint32_t value;
    size_t index;
    if (length <= 1U) {
        *cursor += 1;
        return text[0];
    }
    value = text[0] & (length == 2U ? 0x1FU : (length == 3U ? 0x0FU : 0x07U));
    for (index = 1U; index < length; ++index) {
        value = (value << 6U) | (text[index] & 0x3FU);
    }
    *cursor += length;
    return value;
}

/* The representative of a code point's case class, for the uniqueness check
 * only. Windows compares names through its upcase table, and other hosts
 * fold case as well, so two names any of them may take for one file must
 * compare equal here, or the second would overwrite the first. Exact for
 * the scripts whose case pairs follow a pattern. The Latin blocks whose
 * pairs do not (Latin Extended-B, IPA, phonetic extensions, Latin
 * Extended-C, -D, -E) cross-map among themselves, so all of them fold to one
 * class, and so does Greek Extended: that can only cost an extra "~N"
 * rename, never let a collision through. */
static uint32_t xx_yaffs_fold(uint32_t c) {
    if (c < 0x80U) return (c >= 'a' && c <= 'z') ? c - 0x20U : c;
    if (c < 0x100U) {
        if (c == 0xB5U) return 0x39CU; /* micro sign: Greek capital mu */
        if (c == 0xFFU) return 0x178U;
        return (c >= 0xE0U && c != 0xF7U) ? c - 0x20U : c;
    }
    if (c < 0x180U) {
        if (c == 0x130U || c == 0x131U) return 'I';
        if (c == 0x17FU) return 'S';
        if (c == 0x138U || c == 0x149U || c == 0x178U) return c;
        if ((c >= 0x139U && c <= 0x148U) || (c >= 0x179U && c <= 0x17EU)) {
            return (c & 1U) ? c : c - 1U;
        }
        return c & ~1U;
    }
    if (c < 0x2B0U || (c >= 0x1D00U && c <= 0x1DBFU) ||
        (c >= 0x2C60U && c <= 0x2C7FU) || (c >= 0xA720U && c <= 0xA7FFU) ||
        (c >= 0xAB30U && c <= 0xAB6FU)) {
        return 0x180U;
    }
    if (c == 0x345U || c == 0x1FBEU) return 0x399U; /* iota subscript */
    if (c >= 0x370U && c < 0x400U) {
        if (c >= 0x3B1U && c <= 0x3CBU) return c == 0x3C2U ? 0x3A3U : c - 0x20U;
        if (c == 0x3ACU) return 0x386U;
        if (c >= 0x3ADU && c <= 0x3AFU) return c - 0x25U;
        if (c == 0x3CCU) return 0x38CU;
        if (c == 0x3CDU || c == 0x3CEU) return c - 0x3FU;
        if (c <= 0x373U || c == 0x376U || c == 0x377U ||
            (c >= 0x3D8U && c <= 0x3EFU)) {
            return c & ~1U;
        }
        switch (c) {
        case 0x37BU: case 0x37CU: case 0x37DU: return c + 0x82U;
        case 0x3F3U: return 0x37FU;
        case 0x3D7U: return 0x3CFU;
        case 0x3D0U: return 0x392U;
        case 0x3D1U: case 0x3F4U: return 0x398U;
        case 0x3D5U: return 0x3A6U;
        case 0x3D6U: return 0x3A0U;
        case 0x3F0U: return 0x39AU;
        case 0x3F1U: return 0x3A1U;
        case 0x3F5U: return 0x395U;
        case 0x3F2U: return 0x3F9U;
        case 0x3F8U: return 0x3F7U;
        case 0x3FBU: return 0x3FAU;
        default: return c;
        }
    }
    if (c >= 0x400U && c < 0x530U) {
        if (c >= 0x430U && c <= 0x44FU) return c - 0x20U;
        if (c >= 0x450U && c <= 0x45FU) return c - 0x50U;
        if (c == 0x4CFU) return 0x4C0U;
        if (c >= 0x4C1U && c <= 0x4CEU) return (c & 1U) ? c : c - 1U;
        if ((c >= 0x460U && c <= 0x481U) || (c >= 0x48AU && c <= 0x4BFU) ||
            c >= 0x4D0U) {
            return c & ~1U;
        }
        return c;
    }
    if (c >= 0x561U && c <= 0x586U) return c - 0x30U; /* Armenian */
    if (c >= 0x1C80U && c <= 0x1C88U) {
        static const uint16_t old_cyrillic[9] = {0x412U, 0x414U, 0x41EU,
                                                 0x421U, 0x422U, 0x422U,
                                                 0x42AU, 0x462U, 0xA64AU};
        return old_cyrillic[c - 0x1C80U];
    }
    if (c >= 0x2D00U && c <= 0x2D2DU) return c - 0x1C60U; /* Georgian */
    if ((c >= 0x1C90U && c <= 0x1CBAU) || (c >= 0x1CBDU && c <= 0x1CBFU)) {
        return c - 0xBC0U;
    }
    if (c >= 0xAB70U && c <= 0xABBFU) return c - 0x97D0U; /* Cherokee */
    if (c >= 0x13F8U && c <= 0x13FDU) return c - 8U;
    if (c >= 0x1E00U && c <= 0x1EFFU) {
        if (c == 0x1E9BU) return 0x1E60U;
        if (c == 0x1E9EU) return 0xDFU;
        if (c >= 0x1E96U && c <= 0x1E9FU) return c;
        return c & ~1U;
    }
    if (c >= 0x1F00U && c <= 0x1FFFU) return 0x1F00U;
    switch (c) {
    case 0x2126U: return 0x3A9U; /* ohm */
    case 0x212AU: return 'K';    /* kelvin */
    case 0x212BU: return 0xC5U;  /* angstrom */
    case 0x214EU: return 0x2132U;
    case 0x2184U: return 0x2183U;
    default: break;
    }
    if (c >= 0x2170U && c <= 0x217FU) return c - 0x10U;
    if (c >= 0x24D0U && c <= 0x24E9U) return c - 0x1AU;
    if (c >= 0x2C30U && c <= 0x2C5FU) return c - 0x30U;
    if (c >= 0x2C80U && c <= 0x2CE3U) return c & ~1U;
    if (c >= 0x2CEBU && c <= 0x2CEEU) return (c & 1U) ? c : c - 1U;
    if (c == 0x2CF3U) return 0x2CF2U;
    if ((c >= 0xA640U && c <= 0xA66DU) || (c >= 0xA680U && c <= 0xA69BU)) {
        return c & ~1U;
    }
    if (c >= 0xFF41U && c <= 0xFF5AU) return c - 0x20U;
    if ((c >= 0x10428U && c <= 0x1044FU) || (c >= 0x104D8U && c <= 0x104FBU)) {
        return c - 0x28U;
    }
    if (c >= 0x10CC0U && c <= 0x10CF2U) return c - 0x40U;
    if ((c >= 0x118C0U && c <= 0x118DFU) || (c >= 0x16E60U && c <= 0x16E7FU)) {
        return c - 0x20U;
    }
    if (c >= 0x1E922U && c <= 0x1E943U) return c - 0x22U;
    return c;
}

static uint32_t xx_yaffs_leaf_hash(size_t parent, const char *leaf) {
    uint64_t wide = (uint64_t)parent;
    uint32_t hash = 2166136261U;
    hash = (hash ^ (uint32_t)wide) * 16777619U;
    hash = (hash ^ (uint32_t)(wide >> 32U)) * 16777619U;
    while (*leaf) {
        hash = (hash ^ xx_yaffs_fold(xx_yaffs_next_code_point(&leaf))) *
               16777619U;
    }
    return hash;
}

static bool xx_yaffs_leaf_equal(const char *first, const char *second) {
    while (*first && *second) {
        if (xx_yaffs_fold(xx_yaffs_next_code_point(&first)) !=
            xx_yaffs_fold(xx_yaffs_next_code_point(&second))) {
            return false;
        }
    }
    return *first == 0 && *second == 0;
}

static void xx_yaffs_names_cleanup(xx_yaffs_names *names) {
    if (!names) return;
    if (names->slots) xx_mem_free(names->slots);
    xx_mem_zero(names, sizeof(*names));
}

static xx_yaffs_name_entry *xx_yaffs_names_find(const xx_yaffs_names *names,
                                                size_t parent,
                                                const char *leaf,
                                                uint32_t hash) {
    size_t mask;
    size_t at;
    if (names->capacity == 0U) return NULL;
    mask = names->capacity - 1U;
    for (at = hash & mask; names->slots[at].leaf; at = (at + 1U) & mask) {
        if (names->slots[at].hash == hash && names->slots[at].parent == parent &&
            xx_yaffs_leaf_equal(names->slots[at].leaf, leaf)) {
            return &names->slots[at];
        }
    }
    return NULL;
}

static void xx_yaffs_names_place(xx_yaffs_name_entry *slots, size_t capacity,
                                 const xx_yaffs_name_entry *entry) {
    size_t mask = capacity - 1U;
    size_t at = entry->hash & mask;
    while (slots[at].leaf) at = (at + 1U) & mask;
    slots[at] = *entry;
}

/* @p leaf must outlive the set: it is owned by an object. Any entry pointer
 * obtained earlier is invalid afterwards. */
static bool xx_yaffs_names_add(xx_yaffs_names *names, size_t parent,
                               const char *leaf, uint32_t hash) {
    xx_yaffs_name_entry entry;
    if ((names->used + 1U) * 2U > names->capacity) {
        size_t capacity = names->capacity ? names->capacity * 2U : 256U;
        size_t index;
        xx_yaffs_name_entry *slots;
        if (capacity < names->capacity ||
            capacity > SIZE_MAX / sizeof(*slots)) {
            return false;
        }
        slots = (xx_yaffs_name_entry *)xx_mem_calloc(capacity, sizeof(*slots));
        if (!slots) return false;
        for (index = 0U; index < names->capacity; ++index) {
            if (names->slots[index].leaf) {
                xx_yaffs_names_place(slots, capacity, &names->slots[index]);
            }
        }
        if (names->slots) xx_mem_free(names->slots);
        names->slots = slots;
        names->capacity = capacity;
    }
    entry.leaf = leaf;
    entry.parent = parent;
    entry.hash = hash;
    entry.next_suffix = 1U;
    xx_yaffs_names_place(names->slots, names->capacity, &entry);
    ++names->used;
    return true;
}

/* leaf with "~suffix" before its extension: "name~2.txt". */
static char *xx_yaffs_suffixed(const char *leaf, uint32_t suffix) {
    char suffix_text[16];
    const char *dot = xx_rt_strrchr(leaf, '.');
    size_t leaf_size = xx_str_len(leaf);
    size_t suffix_size;
    size_t before;
    char *combined;
    if (xx_rt_snprintf(suffix_text, sizeof(suffix_text), "~%u",
                       (unsigned)suffix) < 0) {
        return NULL;
    }
    suffix_size = xx_str_len(suffix_text);
    before = (dot && dot != leaf && leaf_size - (size_t)(dot - leaf) <= 32U)
                 ? (size_t)(dot - leaf)
                 : leaf_size;
    combined = (char *)xx_mem_alloc(leaf_size + suffix_size + 1U);
    if (!combined) return NULL;
    xx_mem_copy(combined, leaf, before);
    xx_mem_copy(combined + before, suffix_text, suffix_size);
    xx_mem_copy(combined + before + suffix_size, leaf + before,
                leaf_size - before);
    combined[leaf_size + suffix_size] = '\0';
    return combined;
}

/* Does a leaf fit under a directory whose path is prefix_length long? */
static bool xx_yaffs_path_fits(size_t prefix_length, const char *leaf) {
    size_t leaf_size = xx_str_len(leaf);
    size_t separator = prefix_length != 0U ? 1U : 0U;
    return prefix_length <= XX_YAFFS_MAX_PATH &&
           leaf_size <= XX_YAFFS_MAX_PATH - prefix_length - separator;
}

/* A host-safe leaf for @p raw_name in directory @p parent that no earlier
 * object in that directory has: the name itself, else "name~N". Returns an
 * owned string, or NULL when the name cannot be placed - the full path
 * would outgrow XX_YAFFS_MAX_PATH, or the attempt budget is spent. */
static char *xx_yaffs_claim(xx_yaffs_private *parsed, size_t parent,
                            size_t prefix_length, const char *raw_name) {
    xx_yaffs_names *names = &parsed->names;
    xx_yaffs_name_entry *entry;
    char *base = xx_yaffs_component(raw_name);
    uint32_t hash;
    uint32_t suffix;
    unsigned attempt;
    if (!base) return NULL;
    if (parsed->claim_budget == 0U || !xx_yaffs_path_fits(prefix_length, base)) {
        xx_mem_free(base);
        return NULL;
    }
    --parsed->claim_budget;
    hash = xx_yaffs_leaf_hash(parent, base);
    entry = xx_yaffs_names_find(names, parent, base, hash);
    if (!entry) {
        if (!xx_yaffs_names_add(names, parent, base, hash)) {
            xx_mem_free(base);
            return NULL;
        }
        return base;
    }
    /* Taken: continue the "~N" series where the last claim of this name
     * left it. */
    suffix = entry->next_suffix;
    for (attempt = 0U; attempt < XX_YAFFS_MAX_COLLISIONS &&
                       parsed->claim_budget != 0U && suffix < 0xFFFFFFFFU;
         ++attempt, ++suffix) {
        char *candidate;
        uint32_t candidate_hash;
        --parsed->claim_budget;
        candidate = xx_yaffs_suffixed(base, suffix);
        if (!candidate) break;
        if (!xx_yaffs_path_fits(prefix_length, candidate)) {
            xx_mem_free(candidate);
            break;
        }
        candidate_hash = xx_yaffs_leaf_hash(parent, candidate);
        if (!xx_yaffs_names_find(names, parent, candidate, candidate_hash)) {
            /* Update before the insertion, which may move the entries. */
            entry->next_suffix = suffix + 1U;
            xx_mem_free(base);
            if (!xx_yaffs_names_add(names, parent, candidate, candidate_hash)) {
                xx_mem_free(candidate);
                return NULL;
            }
            return candidate;
        }
        xx_mem_free(candidate);
    }
    entry->next_suffix = suffix;
    xx_mem_free(base);
    return NULL;
}

/* ------------------------------------------------------------------------ */
/* Parsed state lifetime                                                    */
/* ------------------------------------------------------------------------ */

static void xx_yaffs_private_cleanup(xx_yaffs_private *parsed) {
    size_t index;
    if (!parsed) return;
    for (index = 0U; index < parsed->object_count; ++index) {
        if (parsed->objects[index].leaf) xx_mem_free(parsed->objects[index].leaf);
        if (parsed->objects[index].name) xx_mem_free(parsed->objects[index].name);
        if (parsed->objects[index].alias) {
            xx_mem_free(parsed->objects[index].alias);
        }
    }
    if (parsed->objects) xx_mem_free(parsed->objects);
    if (parsed->chunks) xx_mem_free(parsed->chunks);
    if (parsed->records) xx_mem_free(parsed->records);
    if (parsed->lost_leaf) xx_mem_free(parsed->lost_leaf);
    xx_yaffs_map_cleanup(&parsed->map);
    xx_yaffs_names_cleanup(&parsed->names);
    xx_mem_zero(parsed, sizeof(*parsed));
    parsed->input_size = -1;
    parsed->archive_end = -1;
}

static bool xx_yaffs_reserve_objects(xx_yaffs_private *parsed) {
    xx_yaffs_object *grown;
    size_t capacity;
    if (parsed->object_count < parsed->object_capacity) return true;
    capacity = parsed->object_capacity ? parsed->object_capacity * 2U : 64U;
    if (capacity < parsed->object_capacity ||
        capacity > SIZE_MAX / sizeof(*parsed->objects)) {
        return false;
    }
    grown = (xx_yaffs_object *)xx_mem_realloc(
        parsed->objects, capacity * sizeof(*parsed->objects));
    if (!grown) return false;
    parsed->objects = grown;
    parsed->object_capacity = capacity;
    return true;
}

static bool xx_yaffs_reserve_chunks(xx_yaffs_private *parsed) {
    xx_yaffs_data_chunk *grown;
    size_t capacity;
    if (parsed->chunk_count < parsed->chunk_capacity) return true;
    capacity = parsed->chunk_capacity ? parsed->chunk_capacity * 2U : 256U;
    if (capacity < parsed->chunk_capacity ||
        capacity > SIZE_MAX / sizeof(*parsed->chunks)) {
        return false;
    }
    grown = (xx_yaffs_data_chunk *)xx_mem_realloc(
        parsed->chunks, capacity * sizeof(*parsed->chunks));
    if (!grown) return false;
    parsed->chunks = grown;
    parsed->chunk_capacity = capacity;
    return true;
}

/* Does version @p candidate of a chunk replace version @p current? YAFFS2
 * orders by block sequence number, and within one block by position, which
 * the scan visits in order. YAFFS1 has a two-bit serial that is bumped each
 * time a chunk is rewritten, so the newer copy is the one whose serial is
 * one more, modulo four, than the other's. */
static bool xx_yaffs_newer(xx_yaffs_tag_kind kind, uint32_t current,
                           uint32_t candidate) {
    if (kind == XX_YAFFS_TAGS_V1) {
        return candidate == ((current + 1U) & 3U) || candidate == current;
    }
    return candidate >= current;
}

static char *xx_yaffs_copy_text(const char *text) {
    size_t length = xx_str_len(text);
    char *copy = (char *)xx_mem_alloc(length + 1U);
    if (!copy) return NULL;
    if (length != 0U) xx_mem_copy(copy, text, length);
    copy[length] = '\0';
    return copy;
}

/* Record an object header. A repeated object id is an update in the log, so
 * the newer version wins and the older record is overwritten in place -
 * which keeps the map and the object indexes stable. */
static bool xx_yaffs_add_object(xx_yaffs_private *parsed, uint32_t object_id,
                                uint32_t sequence, int64_t header_offset,
                                const xx_yaffs_header *header) {
    size_t index;
    xx_yaffs_object *object;
    char *name;
    char *alias = NULL;
    if (object_id == 0U || object_id == 0xFFFFFFFFU) return true;
    if (xx_yaffs_map_get(&parsed->map, object_id, &index)) {
        object = &parsed->objects[index];
        if (!xx_yaffs_newer(parsed->geometry.kind, object->sequence, sequence)) {
            return true;
        }
    } else {
        if (parsed->object_count >= XX_YAFFS_MAX_OBJECTS) return true;
        if (!xx_yaffs_reserve_objects(parsed)) return false;
        object = &parsed->objects[parsed->object_count];
        xx_mem_zero(object, sizeof(*object));
        if (!xx_yaffs_map_put(&parsed->map, object_id,
                              (uint32_t)parsed->object_count)) {
            return false;
        }
        object->object_id = object_id;
        ++parsed->object_count;
    }
    name = xx_yaffs_copy_text(header->name);
    if (!name) return false;
    if (header->type == XX_YAFFS_OBJECT_TYPE_SYMLINK) {
        alias = xx_yaffs_copy_text(header->alias);
        if (!alias) {
            xx_mem_free(name);
            return false;
        }
    }
    if (object->name) xx_mem_free(object->name);
    if (object->alias) xx_mem_free(object->alias);
    object->name = name;
    object->alias = alias;
    object->parent_id = header->parent_id;
    object->equiv_id = header->equiv_id;
    object->type = header->type;
    object->sequence = sequence;
    object->file_size = header->file_size;
    object->header_offset = header_offset;
    return true;
}

static bool xx_yaffs_add_chunk(xx_yaffs_private *parsed, uint32_t object_id,
                               uint32_t chunk_id, uint32_t sequence,
                               uint32_t byte_count, int64_t offset) {
    xx_yaffs_data_chunk *chunk;
    if (parsed->chunk_count >= XX_YAFFS_MAX_DATA_CHUNKS) return true;
    if (!xx_yaffs_reserve_chunks(parsed)) return false;
    chunk = &parsed->chunks[parsed->chunk_count];
    chunk->object_id = object_id;
    chunk->chunk_id = chunk_id;
    chunk->sequence = sequence;
    chunk->byte_count = byte_count;
    chunk->offset = offset;
    ++parsed->chunk_count;
    return true;
}

/* ------------------------------------------------------------------------ */
/* Chunk ordering                                                           */
/* ------------------------------------------------------------------------ */

/* Order by (object, chunk id, position) so that each object's chunks become
 * one contiguous run, with every copy of a repeated chunk together in the
 * order it was written. Heapsort: qsort() is CRT, and recursion depth is one
 * more thing that would have to be bounded against a hostile input. */
static bool xx_yaffs_chunk_less(const xx_yaffs_data_chunk *left,
                                const xx_yaffs_data_chunk *right) {
    if (left->object_id != right->object_id) {
        return left->object_id < right->object_id;
    }
    if (left->chunk_id != right->chunk_id) {
        return left->chunk_id < right->chunk_id;
    }
    return left->offset < right->offset;
}

static void xx_yaffs_sift_down(xx_yaffs_data_chunk *items, size_t root,
                               size_t count) {
    while (root * 2U + 1U < count) {
        size_t child = root * 2U + 1U;
        xx_yaffs_data_chunk temporary;
        if (child + 1U < count &&
            xx_yaffs_chunk_less(&items[child], &items[child + 1U])) {
            ++child;
        }
        if (!xx_yaffs_chunk_less(&items[root], &items[child])) return;
        temporary = items[root];
        items[root] = items[child];
        items[child] = temporary;
        root = child;
    }
}

static void xx_yaffs_sort_chunks(xx_yaffs_data_chunk *items, size_t count) {
    size_t index;
    if (count < 2U) return;
    for (index = count / 2U; index-- > 0U;) {
        xx_yaffs_sift_down(items, index, count);
    }
    for (index = count; index-- > 1U;) {
        xx_yaffs_data_chunk temporary = items[0];
        items[0] = items[index];
        items[index] = temporary;
        xx_yaffs_sift_down(items, 0U, index);
    }
}

/* ------------------------------------------------------------------------ */
/* Path rebuilding                                                          */
/* ------------------------------------------------------------------------ */

/* The top-level name orphans are filed under, claimed on first use like any
 * other name, so that it cannot collide with a real entry of the root. */
static bool xx_yaffs_lost_ready(xx_yaffs_private *parsed) {
    if (!parsed->lost_tried) {
        parsed->lost_tried = true;
        parsed->lost_leaf = xx_yaffs_claim(parsed, XX_YAFFS_TOP_LEVEL, 0U,
                                           XX_YAFFS_LOST_DIR);
    }
    return parsed->lost_leaf != NULL;
}

/* Place one object in the tree - its directory and a unique leaf in it - by
 * walking its parent chain.
 *
 * The chain is the cycle hazard in this format: parent_id is a raw, entirely
 * unvalidated field, so an object can name itself, or two objects can name
 * each other, or ten thousand can form one long ring. The walk therefore
 * marks each object "in progress" on the way up and stops the moment it
 * meets a mark it made itself - and stops again at XX_YAFFS_MAX_DEPTH links
 * whether or not a cycle was proven. Everything reached along a chain that
 * ended badly is flagged as an orphan and filed under lost+found rather than
 * being dropped, so a single bad link does not cost a whole subtree. A chain
 * that ends in the unlinked or deleted pseudo-directory is not part of the
 * tree at all and is left out.
 *
 * Objects already resolved short-circuit the walk, which makes the whole
 * pass linear in the number of objects no matter how deep the tree is. */
static void xx_yaffs_resolve_path(xx_yaffs_private *parsed, size_t start,
                                  size_t *stack) {
    size_t depth = 0U;
    size_t current = start;
    size_t parent_slot = XX_YAFFS_TOP_LEVEL;
    size_t prefix_length = 0U;
    bool orphan = false;
    bool excluded = false;
    for (;;) {
        xx_yaffs_object *object = &parsed->objects[current];
        size_t parent_index;
        xx_yaffs_object *parent;
        if (object->resolve == 1U || depth >= XX_YAFFS_MAX_DEPTH) {
            /* Walked into our own footprints, or too deep: a cycle. */
            orphan = true;
            break;
        }
        object->resolve = 1U;
        stack[depth++] = current;
        if (object->object_id == XX_YAFFS_OBJECTID_ROOT ||
            object->parent_id == XX_YAFFS_OBJECTID_ROOT) {
            break;
        }
        if (object->object_id == XX_YAFFS_OBJECTID_UNLINKED ||
            object->object_id == XX_YAFFS_OBJECTID_DELETED ||
            object->parent_id == XX_YAFFS_OBJECTID_UNLINKED ||
            object->parent_id == XX_YAFFS_OBJECTID_DELETED) {
            excluded = true;
            break;
        }
        if (!xx_yaffs_map_get(&parsed->map, object->parent_id, &parent_index)) {
            orphan = true;
            break;
        }
        parent = &parsed->objects[parent_index];
        if (parent->type != XX_YAFFS_OBJECT_TYPE_DIRECTORY) {
            orphan = true;
            break;
        }
        if (parent->resolve == 2U) {
            /* Only the root may be resolved without a leaf and still hold
             * children, and a child of the root never gets here. */
            if (parent->excluded || !parent->leaf) {
                excluded = true;
            } else {
                parent_slot = parent_index;
                prefix_length = parent->path_length;
                orphan = parent->orphan;
            }
            break;
        }
        current = parent_index;
    }
    if (orphan && !excluded && parent_slot == XX_YAFFS_TOP_LEVEL) {
        /* The chain broke before reaching a resolved parent. */
        if (xx_yaffs_lost_ready(parsed)) {
            parent_slot = XX_YAFFS_LOST_PARENT;
            prefix_length = xx_str_len(parsed->lost_leaf);
        } else {
            excluded = true;
        }
    }
    /* Unwind, placing each object in the directory above it. */
    while (depth-- > 0U) {
        size_t index = stack[depth];
        xx_yaffs_object *object = &parsed->objects[index];
        object->resolve = 2U;
        object->orphan = orphan;
        object->excluded = excluded;
        if (excluded || object->object_id == XX_YAFFS_OBJECTID_ROOT) {
            /* The root directory itself has no name and is not listed;
             * what hangs below it is top level. */
            parent_slot = XX_YAFFS_TOP_LEVEL;
            prefix_length = 0U;
            continue;
        }
        if (object->name && object->name[0] != '\0') {
            /* A NULL here means the path outgrew XX_YAFFS_MAX_PATH, the
             * name budget ran out or an allocation failed. Either way the
             * object is left out of the listing, and so is everything
             * below it. */
            object->leaf = xx_yaffs_claim(parsed, parent_slot, prefix_length,
                                          object->name);
        }
        if (!object->leaf) {
            excluded = true;
            object->excluded = true;
            continue;
        }
        object->parent_slot = parent_slot;
        object->path_length = prefix_length + (prefix_length != 0U ? 1U : 0U) +
                              xx_str_len(object->leaf);
        parent_slot = index;
        prefix_length = object->path_length;
    }
}

static bool xx_yaffs_build_paths(xx_yaffs_private *parsed) {
    size_t *stack;
    size_t index;
    if (parsed->object_count == 0U) return true;
    stack = (size_t *)xx_mem_calloc(XX_YAFFS_MAX_DEPTH + 1U, sizeof(*stack));
    if (!stack) return false;
    parsed->claim_budget = XX_YAFFS_CLAIM_BUDGET;
    for (index = 0U; index < parsed->object_count; ++index) {
        if (parsed->objects[index].resolve != 2U) {
            xx_yaffs_resolve_path(parsed, index, stack);
        }
    }
    xx_mem_free(stack);
    /* The leaves replace the raw names; nothing reads those again. */
    for (index = 0U; index < parsed->object_count; ++index) {
        if (parsed->objects[index].name) {
            xx_mem_free(parsed->objects[index].name);
            parsed->objects[index].name = NULL;
        }
    }
    return true;
}

/* ------------------------------------------------------------------------ */
/* Scanning                                                                 */
/* ------------------------------------------------------------------------ */

/* One pass over every chunk: headers become objects, data chunks are kept
 * by object id until the objects are all known. Also measures where the
 * image ends: after the last chunk that carries YAFFS tags, plus the erased
 * chunks that follow it. */
static bool xx_yaffs_scan_tagged(Abstractformat *self, xx_yaffs_private *parsed,
                                 xx_yaffs_source *source, xx_pd_struct *pd) {
    const xx_yaffs_geometry *geometry = &parsed->geometry;
    int64_t index;
    int64_t end_chunks = 0;
    bool erased_run = false;
    for (index = 0; index < parsed->total_chunks; ++index) {
        int64_t offset =
            self->base_address + index * (int64_t)geometry->chunk_size;
        xx_yaffs_tags tags;
        xx_yaffs_header header;
        if (pd && xx_pd_is_stopped(pd)) return false;
        if (!xx_yaffs_read_tags(source, geometry, offset, &tags)) break;
        switch (tags.kind) {
        case XX_YAFFS_CHUNK_HEADER:
            end_chunks = index + 1;
            erased_run = true;
            if (!xx_yaffs_read_header(source, offset, geometry->big_endian,
                                      false, &header)) {
                break;
            }
            if (tags.extra && (tags.extra_parent != header.parent_id ||
                               tags.extra_type != header.type)) {
                break;
            }
            /* Only the root may be nameless. */
            if (header.name_length == 0U &&
                tags.object_id != XX_YAFFS_OBJECTID_ROOT) {
                break;
            }
            if (!xx_yaffs_add_object(parsed, tags.object_id, tags.sequence,
                                     offset, &header)) {
                return false;
            }
            break;
        case XX_YAFFS_CHUNK_DATA:
            end_chunks = index + 1;
            erased_run = true;
            if (!xx_yaffs_add_chunk(parsed, tags.object_id, tags.chunk_id,
                                    tags.sequence, tags.byte_count, offset)) {
                return false;
            }
            break;
        case XX_YAFFS_CHUNK_SKIP:
            end_chunks = index + 1;
            erased_run = true;
            break;
        case XX_YAFFS_CHUNK_ERASED:
            if (erased_run) end_chunks = index + 1;
            break;
        default:
            erased_run = false;
            break;
        }
    }
    parsed->archive_end = self->base_address +
                          end_chunks * (int64_t)geometry->chunk_size;
    return true;
}

/* The tag-less layout has no tags at all, so objects and their data are
 * recovered positionally: a page is an object header unless the preceding
 * file header is still owed data pages, and object ids are assigned in the
 * order mkyaffs2image assigns them - 1 for the root if it was written, then
 * upward from 257. This is a heuristic, and it only ever runs when no tagged
 * geometry matched at all. */
static bool xx_yaffs_scan_positional(Abstractformat *self,
                                     xx_yaffs_private *parsed,
                                     xx_yaffs_source *source,
                                     xx_pd_struct *pd) {
    const xx_yaffs_geometry *geometry = &parsed->geometry;
    int64_t index;
    uint32_t next_id = 257U;
    uint32_t pending_object = 0U;
    uint32_t pending_chunk = 0U;
    int64_t pending_pages = 0;
    int64_t end_chunks = 0;
    for (index = 0; index < parsed->total_chunks; ++index) {
        int64_t offset =
            self->base_address + index * (int64_t)geometry->chunk_size;
        xx_yaffs_header header;
        uint32_t object_id;
        if (pd && xx_pd_is_stopped(pd)) return false;
        if (pending_pages > 0) {
            if (!xx_yaffs_add_chunk(parsed, pending_object, ++pending_chunk,
                                    0U, geometry->data_size, offset)) {
                return false;
            }
            end_chunks = index + 1;
            --pending_pages;
            continue;
        }
        if (!xx_yaffs_read_header(source, offset, geometry->big_endian, false,
                                  &header)) {
            break;
        }
        if (index == 0 && header.name_length == 0U &&
            header.type == XX_YAFFS_OBJECT_TYPE_DIRECTORY) {
            object_id = XX_YAFFS_OBJECTID_ROOT;
        } else if (header.name_length == 0U) {
            break;
        } else {
            object_id = next_id++;
        }
        if (next_id == 0xFFFFFFFFU) break;
        if (!xx_yaffs_add_object(parsed, object_id, 0U, offset, &header)) {
            return false;
        }
        end_chunks = index + 1;
        if (header.type == XX_YAFFS_OBJECT_TYPE_FILE && header.file_size != 0U) {
            uint64_t pages = (header.file_size + geometry->data_size - 1U) /
                             geometry->data_size;
            if (pages > (uint64_t)(parsed->total_chunks - index - 1)) {
                pages = (uint64_t)(parsed->total_chunks - index - 1);
            }
            pending_object = object_id;
            pending_chunk = 0U;
            pending_pages = (int64_t)pages;
        }
    }
    parsed->archive_end = self->base_address +
                          end_chunks * (int64_t)geometry->chunk_size;
    return true;
}

/* Swap each chunk's object id for its object index, drop chunks that belong
 * to no known regular file, and turn the sorted array into one contiguous
 * run per object. */
static void xx_yaffs_assign_chunk_runs(xx_yaffs_private *parsed) {
    size_t read_index;
    size_t write_index = 0U;
    size_t index = 0U;
    for (read_index = 0U; read_index < parsed->chunk_count; ++read_index) {
        xx_yaffs_data_chunk chunk = parsed->chunks[read_index];
        size_t object_index;
        if (!xx_yaffs_map_get(&parsed->map, chunk.object_id, &object_index) ||
            parsed->objects[object_index].type != XX_YAFFS_OBJECT_TYPE_FILE) {
            continue;
        }
        chunk.object_id = (uint32_t)object_index;
        parsed->chunks[write_index++] = chunk;
    }
    parsed->chunk_count = write_index;
    xx_yaffs_sort_chunks(parsed->chunks, parsed->chunk_count);
    while (index < parsed->chunk_count) {
        size_t object_index = parsed->chunks[index].object_id;
        size_t start = index;
        while (index < parsed->chunk_count &&
               parsed->chunks[index].object_id == object_index) {
            ++index;
        }
        if (object_index < parsed->object_count) {
            parsed->objects[object_index].first_chunk = start;
            parsed->objects[object_index].chunk_count = index - start;
        }
    }
}

/* The listing: every object that got a place in the tree, in object order. */
static bool xx_yaffs_build_records(xx_yaffs_private *parsed) {
    size_t index;
    size_t count = 0U;
    for (index = 0U; index < parsed->object_count; ++index) {
        if (parsed->objects[index].leaf) ++count;
    }
    if (count == 0U) return true;
    parsed->records = (size_t *)xx_mem_calloc(count, sizeof(*parsed->records));
    if (!parsed->records) return false;
    for (index = 0U; index < parsed->object_count; ++index) {
        if (parsed->objects[index].leaf) {
            parsed->records[parsed->record_count++] = index;
        }
    }
    return true;
}

static bool xx_yaffs_parse(Abstractformat *self, xx_yaffs_private *parsed,
                           xx_pd_struct *pd) {
    int64_t total_size;
    int64_t available;
    xx_yaffs_source source;
    bool scanned;
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
    if (total_size <= self->base_address) goto fail;
    parsed->input_size = total_size;
    if (!xx_yaffs_detect(self->device, self->base_address, total_size,
                         &parsed->geometry, pd)) {
        goto fail;
    }
    available = total_size - self->base_address;
    parsed->total_chunks = available / (int64_t)parsed->geometry.chunk_size;
    if (parsed->total_chunks > (int64_t)XX_YAFFS_MAX_CHUNKS) {
        parsed->total_chunks = (int64_t)XX_YAFFS_MAX_CHUNKS;
    }
    xx_yaffs_source_init(&source, self->device, total_size, true);
    scanned = parsed->geometry.kind == XX_YAFFS_TAGS_NONE
                  ? xx_yaffs_scan_positional(self, parsed, &source, pd)
                  : xx_yaffs_scan_tagged(self, parsed, &source, pd);
    xx_yaffs_source_cleanup(&source);
    if (!scanned) goto fail;
    if (parsed->archive_end <= self->base_address) goto fail;
    xx_yaffs_assign_chunk_runs(parsed);
    if (!xx_yaffs_build_paths(parsed)) goto fail;
    if (!xx_yaffs_build_records(parsed)) goto fail;
    return true;
fail:
    xx_yaffs_private_cleanup(parsed);
    return false;
}

/* ------------------------------------------------------------------------ */
/* Archive record plumbing                                                  */
/* ------------------------------------------------------------------------ */

static bool xx_yaffs_copy_options(xx_list_s *destination,
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

static const xx_var *xx_yaffs_find_option(const xx_list_s *options,
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

/* The object whose data a record carries: the file itself, or for a hard
 * link the file it points at (one level; a link to a link carries none). */
static const xx_yaffs_object *xx_yaffs_data_object(
    const xx_yaffs_private *parsed, const xx_yaffs_object *object) {
    size_t index;
    if (object->type == XX_YAFFS_OBJECT_TYPE_FILE) return object;
    if (object->type != XX_YAFFS_OBJECT_TYPE_HARDLINK ||
        !xx_yaffs_map_get(&parsed->map, object->equiv_id, &index) ||
        parsed->objects[index].type != XX_YAFFS_OBJECT_TYPE_FILE) {
        return NULL;
    }
    return &parsed->objects[index];
}

/* The full path of a placed object, from the leaves up its parent slots.
 * path_length was checked against XX_YAFFS_MAX_PATH when the leaves were
 * claimed; every step here is checked again, so an inconsistency yields
 * NULL rather than a short or overrun buffer. */
static char *xx_yaffs_build_path(const xx_yaffs_private *parsed,
                                 size_t object_index) {
    const xx_yaffs_object *object = &parsed->objects[object_index];
    size_t length = object->path_length;
    size_t at = length;
    size_t current = object_index;
    size_t steps;
    char *path;
    if (!object->leaf || length == 0U || length > XX_YAFFS_MAX_PATH) {
        return NULL;
    }
    path = (char *)xx_mem_alloc(length + 1U);
    if (!path) return NULL;
    path[length] = '\0';
    /* Every step but the last consumes a leaf and a separator. */
    for (steps = 0U; steps <= XX_YAFFS_MAX_PATH; ++steps) {
        const char *leaf;
        size_t leaf_length;
        size_t parent;
        if (current == XX_YAFFS_LOST_PARENT) {
            leaf = parsed->lost_leaf;
            parent = XX_YAFFS_TOP_LEVEL;
        } else if (current < parsed->object_count) {
            leaf = parsed->objects[current].leaf;
            parent = parsed->objects[current].parent_slot;
        } else {
            break;
        }
        if (!leaf) break;
        leaf_length = xx_str_len(leaf);
        if (leaf_length == 0U || leaf_length > at) break;
        at -= leaf_length;
        xx_mem_copy(path + at, leaf, leaf_length);
        if (parent == XX_YAFFS_TOP_LEVEL) {
            if (at == 0U) return path;
            break;
        }
        if (at == 0U) break;
        path[--at] = '/';
        current = parent;
    }
    xx_mem_free(path);
    return NULL;
}

static bool xx_yaffs_populate_record(xx_archive_record *record,
                                     const xx_yaffs_private *parsed,
                                     size_t object_index) {
    const xx_yaffs_object *object;
    const xx_yaffs_object *data;
    char *path;
    int64_t data_offset;
    int64_t size;
    bool is_folder;
    bool result;
    if (!record || !parsed || object_index >= parsed->object_count) return false;
    object = &parsed->objects[object_index];
    path = xx_yaffs_build_path(parsed, object_index);
    if (!path) return false;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    is_folder = object->type == XX_YAFFS_OBJECT_TYPE_DIRECTORY;
    data = xx_yaffs_data_object(parsed, object);
    size = data ? (int64_t)(data->file_size & INT64_MAX) : 0;
    /* A file's pages are scattered, so data_offset can only point at the
     * first of them; unpacking walks the chunk run instead. */
    data_offset = (data && data->chunk_count != 0U)
                      ? parsed->chunks[data->first_chunk].offset
                      : object->header_offset;
    record->header_offset = object->header_offset;
    record->header_size = (int64_t)XX_YAFFS_HEADER_SIZE;
    record->data_offset = data_offset;
    record->compressed_size = size;
    result = xx_archive_record_set_original_name(record, path) &&
             xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                            (uint64_t)size) &&
             xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                            (uint64_t)size) &&
             xx_archive_record_set_meta_u64(record,
                                            XX_META_ID_COMPRESSION_METHOD, 0U) &&
             xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                             is_folder);
    xx_mem_free(path);
    if (!result) return false;
    if (object->type == XX_YAFFS_OBJECT_TYPE_SYMLINK && object->alias &&
        object->alias[0] != '\0' &&
        !xx_archive_record_set_meta_str(record, XX_META_ID_LINK_TARGET,
                                        object->alias)) {
        return false;
    }
    return true;
}

static void xx_yaffs_archive_stream_free(void *pointer) {
    xx_yaffs_archive_stream *stream = (xx_yaffs_archive_stream *)pointer;
    if (!stream) return;
    xx_yaffs_private_cleanup(&stream->parsed);
    xx_mem_free(stream);
}

/* Extraction-time name check, a second line behind the renaming done when
 * the paths were built: relative, no empty, "." or ".." component, nothing
 * Windows reserves. */
static bool xx_yaffs_safe_name(const char *name) {
    const char *component;
    const char *cursor;
    if (!name || !name[0] || name[0] == '/' || name[0] == '\\') return false;
    component = name;
    for (cursor = name;; ++cursor) {
        unsigned char ch = (unsigned char)*cursor;
        if (ch == ':' || ch == '<' || ch == '>' || ch == '"' || ch == '|' ||
            ch == '?' || ch == '*' || (ch != 0U && ch < 32U)) {
            return false;
        }
        if (ch == '/' || ch == '\\' || ch == 0U) {
            size_t length = (size_t)(cursor - component);
            if (length == 0U || (length == 1U && component[0] == '.') ||
                (length == 2U && component[0] == '.' && component[1] == '.') ||
                component[length - 1U] == ' ' ||
                component[length - 1U] == '.') {
                return false;
            }
            if (ch == 0U) return true;
            component = cursor + 1;
        }
    }
}

/* Walk one file's chunk run in chunk id order and pick, for each chunk id,
 * the copy that was written last. @p output may be NULL, which only checks
 * that every page the file size calls for is present. A missing page fails
 * the file: silently zero-filling it would let a few bytes of header claim
 * any amount of output. */
static bool xx_yaffs_emit_file(Abstractformat *self,
                               const xx_yaffs_private *parsed,
                               const xx_yaffs_object *object,
                               xx_io_device *output, xx_pd_struct *pd) {
    uint8_t *page = NULL;
    uint64_t remaining = object->file_size;
    uint32_t expected = 1U;
    size_t index = object->first_chunk;
    size_t end = object->first_chunk + object->chunk_count;
    uint32_t data_size = parsed->geometry.data_size;
    bool result = true;
    if (remaining == 0U) return true;
    if (output) {
        page = (uint8_t *)xx_mem_alloc(data_size);
        if (!page) return false;
    }
    while (remaining != 0U && index < end) {
        size_t last = index;
        size_t best = index;
        size_t want;
        if (pd && xx_pd_is_stopped(pd)) {
            result = false;
            break;
        }
        while (last + 1U < end &&
               parsed->chunks[last + 1U].chunk_id ==
                   parsed->chunks[index].chunk_id) {
            ++last;
            if (xx_yaffs_newer(parsed->geometry.kind,
                               parsed->chunks[best].sequence,
                               parsed->chunks[last].sequence)) {
                best = last;
            }
        }
        if (parsed->chunks[best].chunk_id < expected) {
            /* Only reachable through chunk id 0, which is never data. */
            index = last + 1U;
            continue;
        }
        if (parsed->chunks[best].chunk_id != expected) {
            result = false;
            break;
        }
        want = remaining < (uint64_t)data_size ? (size_t)remaining
                                               : (size_t)data_size;
        if (!xx_yaffs_range_within(parsed->input_size,
                                   parsed->chunks[best].offset,
                                   (int64_t)want)) {
            result = false;
            break;
        }
        if (output) {
            ssize_t written;
            if (!xx_yaffs_read_at(self->device, parsed->chunks[best].offset,
                                  page, want)) {
                result = false;
                break;
            }
            written = xx_io_write(output, page, want);
            if (written < 0 || (size_t)written != want) {
                result = false;
                break;
            }
        }
        remaining -= (uint64_t)want;
        ++expected;
        index = last + 1U;
    }
    if (remaining != 0U) result = false;
    if (page) xx_mem_free(page);
    return result;
}

static bool xx_yaffs_write_file(Abstractformat *self,
                                const xx_yaffs_private *parsed,
                                const xx_yaffs_object *object,
                                const char *destination, xx_pd_struct *pd) {
    xx_io_device *output;
    bool result;
    /* Check before creating anything, so a broken file leaves no stub. */
    if (!xx_yaffs_emit_file(self, parsed, object, NULL, pd)) return false;
    output = xx_io_file_open(destination, "wb");
    if (!output) return false;
    result = xx_yaffs_emit_file(self, parsed, object, output, pd);
    xx_io_close(output);
    /* Only output this call created is discarded; a file that did not open
     * above was never touched. */
    if (!result) xx_rt_remove(destination);
    return result;
}

/* ------------------------------------------------------------------------ */
/* Construction and vtable                                                  */
/* ------------------------------------------------------------------------ */

void xx_yaffs_init(xx_yaffs *yaffs, xx_io_device *dev, int64_t base_address) {
    if (!yaffs) return;
    xx_mem_zero(yaffs, sizeof(*yaffs));
    xx_format_init(&yaffs->format, dev, base_address);
    yaffs->format.endian = XX_ENDIAN_LITTLE;
    yaffs->format.file_type = XX_YAFFS_FILE_TYPE;
    yaffs->format.format_type = XX_TYPE_ARCHIVE;
    yaffs->format.is_archive = true;
    xx_format_set_mime_type(&yaffs->format, "application/x-yaffs");
    xx_format_set_extension(&yaffs->format, "yaffs");
    yaffs->format.check_is_valid = xx_yaffs_check_is_valid;
    yaffs->format.handle_base_info = xx_yaffs_handle_base_info;
    yaffs->format.get_format_size = xx_yaffs_get_format_size;
    yaffs->format.get_number_of_archive_records =
        xx_yaffs_get_number_of_archive_records;
    yaffs->format.create_archive_records_reading =
        xx_yaffs_create_archive_records_reading;
    yaffs->format.get_current_archive_record =
        xx_yaffs_get_current_archive_record;
    yaffs->format.unpack_current_archive_record =
        xx_yaffs_unpack_current_archive_record;
    yaffs->format.archive_record_move_to_next =
        xx_yaffs_archive_record_move_to_next;
    yaffs->format.free_archive_records_reading =
        xx_yaffs_free_archive_records_reading;
    yaffs->format.destroy = xx_yaffs_vtable_destroy;
    yaffs->archive_end = -1;
}

xx_yaffs *xx_yaffs_create(xx_io_device *dev, int64_t base_address) {
    xx_yaffs *yaffs = (xx_yaffs *)xx_mem_alloc(sizeof(*yaffs));
    if (yaffs) xx_yaffs_init(yaffs, dev, base_address);
    return yaffs;
}

void xx_yaffs_destroy(xx_yaffs *yaffs) {
    if (!yaffs) return;
    if (yaffs->internal) {
        xx_yaffs_private_cleanup((xx_yaffs_private *)yaffs->internal);
        xx_mem_free(yaffs->internal);
        yaffs->internal = NULL;
    }
    xx_format_cleanup_extra_parameters(&yaffs->format);
}

static void xx_yaffs_vtable_destroy(Abstractformat *self) {
    xx_yaffs_destroy((xx_yaffs *)self);
}

void xx_yaffs_free(xx_yaffs *yaffs) {
    if (!yaffs) return;
    xx_yaffs_destroy(yaffs);
    xx_mem_free(yaffs);
}

/* Detection only, deliberately not a full parse.
 *
 * YAFFS has no magic, so this is a late-dispatch probe that every other
 * format gets offered first - and a probe that runs over every otherwise
 * unknown file must not be linear in the size of the file. Deciding that
 * chunk 0 is not an object header costs one 512 byte read; a candidate
 * geometry then costs at most XX_YAFFS_PROBE_CHUNKS tag reads.
 * xx_yaffs_handle_base_info() does the real work. */
bool xx_yaffs_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_yaffs_geometry geometry;
    int64_t total_size;
    if (!self || !self->device || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    total_size = xx_io_total_size(self->device);
    if (total_size <= self->base_address) return false;
    return xx_yaffs_detect(self->device, self->base_address, total_size,
                           &geometry, pd);
}

bool xx_yaffs_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_yaffs_private *parsed;
    xx_yaffs *yaffs = (xx_yaffs *)self;
    int64_t total_size;
    if (!self || !yaffs) return false;
    parsed = (xx_yaffs_private *)xx_mem_alloc(sizeof(*parsed));
    if (!parsed || !xx_yaffs_parse(self, parsed, pd)) {
        if (parsed) xx_mem_free(parsed);
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    if (yaffs->internal) {
        xx_yaffs_private_cleanup((xx_yaffs_private *)yaffs->internal);
        xx_mem_free(yaffs->internal);
    }
    yaffs->internal = parsed;
    yaffs->number_of_records = parsed->record_count;
    yaffs->number_of_members = parsed->record_count;
    yaffs->page_size = parsed->geometry.page_size;
    yaffs->spare_size = parsed->geometry.spare_size;
    yaffs->tag_offset = parsed->geometry.tag_offset;
    yaffs->version = parsed->geometry.kind == XX_YAFFS_TAGS_V1 ? 1U : 2U;
    yaffs->big_endian = parsed->geometry.big_endian;
    yaffs->has_spare = parsed->geometry.kind != XX_YAFFS_TAGS_NONE &&
                       !parsed->geometry.inband;
    yaffs->archive_end = parsed->archive_end;
    self->endian = parsed->geometry.big_endian ? XX_ENDIAN_BIG
                                               : XX_ENDIAN_LITTLE;
    self->format_size = parsed->archive_end - self->base_address;
    total_size = xx_io_total_size(self->device);
    if (total_size > parsed->archive_end) {
        self->overlay_offset = parsed->archive_end;
        self->overlay_size = total_size - parsed->archive_end;
    } else {
        self->overlay_offset = -1;
        self->overlay_size = 0;
    }
    self->number_of_archive_records = parsed->record_count;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_yaffs_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return -1;
    }
    return self->format_size;
}

uint64_t xx_yaffs_get_number_of_archive_records(Abstractformat *self,
                                                xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return ((xx_yaffs *)self)->number_of_records;
}

xx_archive_record_state *xx_yaffs_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_record_state *state;
    xx_yaffs_archive_stream *stream;
    if (!self || !self->device ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    stream = (xx_yaffs_archive_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!state || !stream) {
        if (state) xx_mem_free(state);
        if (stream) xx_mem_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    if (!xx_yaffs_copy_options(&state->options, options)) {
        xx_yaffs_archive_stream_free(stream);
        xx_archive_record_state_free(state);
        return NULL;
    }
    if (((xx_yaffs *)self)->internal) {
        /* Take over the tree handle_base_info() built rather than holding
         * two copies of it; a later reading parses again. */
        xx_yaffs *yaffs = (xx_yaffs *)self;
        stream->parsed = *(xx_yaffs_private *)yaffs->internal;
        xx_mem_free(yaffs->internal);
        yaffs->internal = NULL;
    } else if (!xx_yaffs_parse(self, &stream->parsed, pd)) {
        xx_yaffs_archive_stream_free(stream);
        xx_archive_record_state_free(state);
        return NULL;
    }
    stream->index = 0U;
    /* Hard links copy their target's data again; all of them together may
     * write no more than the image itself holds. */
    stream->link_budget = (uint64_t)stream->parsed.input_size;
    state->internal_state = stream;
    state->free_internal = xx_yaffs_archive_stream_free;
    state->total_records = (int64_t)stream->parsed.record_count;
    if (stream->parsed.record_count != 0U &&
        xx_yaffs_populate_record(&state->current_record, &stream->parsed,
                                 stream->parsed.records[0])) {
        state->has_record = true;
        state->current_index = 0;
    }
    return state;
}

const xx_archive_record *xx_yaffs_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_yaffs_archive_record_move_to_next(Abstractformat *self,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    xx_yaffs_archive_stream *stream;
    if (!self || !state || state->format != self || !state->has_record ||
        !state->internal_state || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_yaffs_archive_stream *)state->internal_state;
    ++stream->index;
    if (stream->index >= stream->parsed.record_count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    if (!xx_yaffs_populate_record(&state->current_record, &stream->parsed,
                                  stream->parsed.records[stream->index])) {
        state->has_record = false;
        return false;
    }
    ++state->current_index;
    return true;
}

bool xx_yaffs_unpack_current_archive_record(Abstractformat *self,
                                            xx_archive_record_state *state,
                                            xx_pd_struct *pd) {
    const xx_archive_record *record;
    xx_yaffs_archive_stream *stream;
    const xx_yaffs_object *object;
    const xx_yaffs_object *data;
    const xx_var *option;
    const char *name;
    const char *base = NULL;
    char *owned_base = NULL;
    char *destination = NULL;
    bool result = false;
    if (!self || !self->device || !state || state->format != self ||
        !state->has_record || !state->internal_state ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_yaffs_archive_stream *)state->internal_state;
    if (stream->index >= stream->parsed.record_count) return false;
    object = &stream->parsed.objects[stream->parsed.records[stream->index]];
    record = &state->current_record;
    name = xx_archive_record_get_original_name(record);
    if (!xx_yaffs_safe_name(name)) return false;
    data = xx_yaffs_data_object(&stream->parsed, object);
    if (data && object->type == XX_YAFFS_OBJECT_TYPE_HARDLINK) {
        if (data->file_size > stream->link_budget) return false;
        stream->link_budget -= data->file_size;
    }
    option = xx_yaffs_find_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!option) {
        /* Test mode: no destination, so only confirm the payload is all
         * there and inside the device. */
        return !data ||
               xx_yaffs_emit_file(self, &stream->parsed, data, NULL, pd);
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
        destination = xx_str_concat(base, "/");
        if (!destination) goto cleanup;
        {
            char *joined = xx_str_concat(destination, name);
            xx_str_free(destination);
            destination = joined;
        }
    } else {
        destination = xx_str_concat(base, name);
    }
    if (!destination) goto cleanup;
    if (object->type == XX_YAFFS_OBJECT_TYPE_DIRECTORY) {
        result = xx_store_create_dirs_a(destination, true);
    } else if (data) {
        if (xx_store_create_dirs_a(destination, false)) {
            result = xx_yaffs_write_file(self, &stream->parsed, data,
                                         destination, pd);
        }
    } else {
        /* Symlinks (target in XX_META_ID_LINK_TARGET), device nodes and
         * links to non-files carry no payload. They stay in the listing
         * but nothing is written for them. */
        result = true;
    }
cleanup:
    if (owned_base) xx_str_free(owned_base);
    if (destination) xx_str_free(destination);
    return result;
}

void xx_yaffs_free_archive_records_reading(Abstractformat *self,
                                           xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

uint64_t xx_yaffs_get_number_of_records(const xx_yaffs *yaffs) {
    return yaffs ? yaffs->number_of_records : 0U;
}
uint64_t xx_yaffs_get_number_of_members(const xx_yaffs *yaffs) {
    return yaffs ? yaffs->number_of_members : 0U;
}
uint32_t xx_yaffs_get_page_size(const xx_yaffs *yaffs) {
    return yaffs ? yaffs->page_size : 0U;
}
uint32_t xx_yaffs_get_spare_size(const xx_yaffs *yaffs) {
    return yaffs ? yaffs->spare_size : 0U;
}
uint32_t xx_yaffs_get_version(const xx_yaffs *yaffs) {
    return yaffs ? yaffs->version : 0U;
}
bool xx_yaffs_get_big_endian(const xx_yaffs *yaffs) {
    return yaffs ? yaffs->big_endian : false;
}
int64_t xx_yaffs_get_archive_end(const xx_yaffs *yaffs) {
    return yaffs ? yaffs->archive_end : -1;
}
