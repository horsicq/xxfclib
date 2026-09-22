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
#define XX_YAFFS_NAME_MAX 254U      /**< Longest name we accept, NUL aside. */
#define XX_YAFFS_ALIAS_FIELD 300U
#define XX_YAFFS_ALIAS_MAX 159U
#define XX_YAFFS_TAGS_SIZE 16U      /**< YAFFS2 packed tags, ECC excluded. */
#define XX_YAFFS_SPARE1_SIZE 16U    /**< YAFFS1 struct yaffs_spare. */

/* Budgets. Every one of these is a hard stop rather than a hint: the chunk
 * count, the object ids and the parent ids all come straight out of the
 * image, so an adversarial file must not be able to trade a few kilobytes of
 * input for an unbounded walk or an unbounded allocation. */
#define XX_YAFFS_MAX_CHUNKS 1048576U     /**< Chunks examined in one image. */
#define XX_YAFFS_MAX_OBJECTS 100000U     /**< Object headers retained. */
#define XX_YAFFS_MAX_DATA_CHUNKS 1048576U /**< Data chunk records retained. */
#define XX_YAFFS_MAX_DEPTH 64U           /**< Parent chain links followed. */
#define XX_YAFFS_MAX_PATH 4096U          /**< Longest rebuilt path. */
#define XX_YAFFS_PROBE_CHUNKS 64U        /**< Chunks scored per candidate. */

/* Placed on objects whose parent chain does not reach the root, either
 * because the parent header is missing or because the chain loops. */
#define XX_YAFFS_LOST_DIR "lost+found"

/* ------------------------------------------------------------------------ */
/* Geometry                                                                 */
/* ------------------------------------------------------------------------ */

/** How the tags for a chunk are stored, if they are stored at all. */
typedef enum {
    XX_YAFFS_TAGS_NONE = 0, /**< No spare area; layout inferred positionally. */
    XX_YAFFS_TAGS_V1 = 1,   /**< YAFFS1 struct yaffs_spare bitfields. */
    XX_YAFFS_TAGS_V2 = 2    /**< YAFFS2 packed tags2. */
} xx_yaffs_tag_kind;

typedef struct xx_yaffs_geometry_s {
    uint32_t page_size;
    uint32_t spare_size;
    uint32_t tag_offset; /**< Tags start this far into the spare area. */
    xx_yaffs_tag_kind kind;
    bool big_endian;
} xx_yaffs_geometry;

/** One chunk's tags, normalised across YAFFS1 and YAFFS2. */
typedef struct xx_yaffs_tags_s {
    uint32_t sequence;
    uint32_t object_id;
    uint32_t chunk_id;
    uint32_t byte_count;
} xx_yaffs_tags;

/** The interesting fields of a struct yaffs_obj_hdr. */
typedef struct xx_yaffs_header_s {
    uint32_t type;
    uint32_t parent_id;
    uint64_t file_size;
    uint32_t mode;
    char name[XX_YAFFS_NAME_MAX + 1U];
    size_t name_length;
} xx_yaffs_header;

/* ------------------------------------------------------------------------ */
/* Parsed state                                                             */
/* ------------------------------------------------------------------------ */

typedef struct xx_yaffs_object_s {
    char *path;          /**< Full path from the root, owned. NULL = dropped. */
    char *name;          /**< Leaf name, owned. */
    uint32_t object_id;
    uint32_t parent_id;
    uint32_t type;
    uint32_t sequence;   /**< Highest sequence seen for this object id. */
    uint64_t file_size;
    int64_t header_offset;
    size_t first_chunk;  /**< First entry of this object in chunks[]. */
    size_t chunk_count;
    uint8_t resolve;     /**< 0 unseen, 1 in progress, 2 resolved. */
    bool orphan;         /**< Parent chain broken, looped or too deep. */
} xx_yaffs_object;

typedef struct xx_yaffs_data_chunk_s {
    uint32_t object_index;
    uint32_t chunk_id;
    uint32_t sequence;
    uint32_t byte_count;
    int64_t offset;      /**< Offset of the page data, not of the chunk. */
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

typedef struct xx_yaffs_private_s {
    xx_yaffs_geometry geometry;
    xx_yaffs_object *objects;
    size_t object_count;
    size_t object_capacity;
    xx_yaffs_data_chunk *chunks;
    size_t chunk_count;
    size_t chunk_capacity;
    xx_yaffs_map map;
    size_t *records;      /**< Object indexes that make up the listing. */
    size_t record_count;
    int64_t input_size;
    int64_t archive_end;
    int64_t chunk_size;   /**< page_size + spare_size. */
    int64_t total_chunks;
} xx_yaffs_private;

typedef struct xx_yaffs_archive_stream_s {
    xx_yaffs_private parsed;
    size_t index;
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

/* ------------------------------------------------------------------------ */
/* Tag decoding                                                             */
/* ------------------------------------------------------------------------ */

/* YAFFS2: four little- or big-endian words at the head of the spare. Images
 * built without --yaffs-ecclayout carry two extra bytes first, which is what
 * geometry.tag_offset absorbs. */
static void xx_yaffs_decode_tags_v2(const uint8_t *spare, bool big_endian,
                                    xx_yaffs_tags *tags) {
    tags->sequence = xx_data_get_u32(spare, XX_YAFFS_TAGS_SIZE, 0U, big_endian);
    tags->object_id = xx_data_get_u32(spare, XX_YAFFS_TAGS_SIZE, 4U, big_endian);
    tags->chunk_id = xx_data_get_u32(spare, XX_YAFFS_TAGS_SIZE, 8U, big_endian);
    tags->byte_count =
        xx_data_get_u32(spare, XX_YAFFS_TAGS_SIZE, 12U, big_endian);
}

/* YAFFS1: struct yaffs_tags is eight bytes of compiler packed bitfields
 *
 *     chunk_id : 20, serial_number : 2, byte_count : 10,
 *     object_id : 18, ecc : 12
 *
 * scattered through struct yaffs_spare at bytes 0..2, 5..7 and 11..12. A
 * little endian target fills the word from the least significant bit up; a
 * big endian one from the most significant bit down, so the two cases need
 * different shifts rather than a byte swap of the same shifts. */
static void xx_yaffs_decode_tags_v1(const uint8_t *spare, bool big_endian,
                                    xx_yaffs_tags *tags) {
    uint8_t raw[8];
    uint64_t value = 0U;
    size_t index;
    raw[0] = spare[0];
    raw[1] = spare[1];
    raw[2] = spare[2];
    raw[3] = spare[5];
    raw[4] = spare[6];
    raw[5] = spare[7];
    raw[6] = spare[11];
    raw[7] = spare[12];
    if (big_endian) {
        for (index = 0U; index < 8U; ++index) {
            value = (value << 8U) | (uint64_t)raw[index];
        }
        tags->chunk_id = (uint32_t)((value >> 44U) & UINT64_C(0xFFFFF));
        tags->sequence = (uint32_t)((value >> 42U) & UINT64_C(3));
        tags->byte_count = (uint32_t)((value >> 32U) & UINT64_C(0x3FF));
        tags->object_id = (uint32_t)((value >> 14U) & UINT64_C(0x3FFFF));
    } else {
        for (index = 8U; index-- > 0U;) {
            value = (value << 8U) | (uint64_t)raw[index];
        }
        tags->chunk_id = (uint32_t)(value & UINT64_C(0xFFFFF));
        tags->sequence = (uint32_t)((value >> 20U) & UINT64_C(3));
        tags->byte_count = (uint32_t)((value >> 22U) & UINT64_C(0x3FF));
        tags->object_id = (uint32_t)((value >> 32U) & UINT64_C(0x3FFFF));
    }
}

/* Read and decode the tags of the chunk that starts at chunk_offset. Returns
 * false when the spare area is missing or unreadable. */
static bool xx_yaffs_read_tags(xx_io_device *device,
                               const xx_yaffs_geometry *geometry,
                               int64_t total_size, int64_t chunk_offset,
                               xx_yaffs_tags *tags) {
    uint8_t spare[XX_YAFFS_TAGS_SIZE];
    int64_t offset;
    if (!geometry || !tags || geometry->kind == XX_YAFFS_TAGS_NONE) return false;
    xx_mem_zero(tags, sizeof(*tags));
    offset = chunk_offset + (int64_t)geometry->page_size +
             (int64_t)geometry->tag_offset;
    if (!xx_yaffs_range_within(total_size, offset, (int64_t)sizeof(spare)) ||
        !xx_yaffs_read_at(device, offset, spare, sizeof(spare))) {
        return false;
    }
    if (geometry->kind == XX_YAFFS_TAGS_V1) {
        xx_yaffs_decode_tags_v1(spare, geometry->big_endian, tags);
    } else {
        xx_yaffs_decode_tags_v2(spare, geometry->big_endian, tags);
    }
    return true;
}

/* ------------------------------------------------------------------------ */
/* Object header decoding                                                   */
/* ------------------------------------------------------------------------ */

/* A name is one path component, so a separator or a control byte means the
 * page is not a header at all. An empty name is legal only for the root
 * directory, which the caller checks. */
static bool xx_yaffs_plausible_name(const char *name, size_t length) {
    size_t index;
    for (index = 0U; index < length; ++index) {
        unsigned char ch = (unsigned char)name[index];
        if (ch < 32U || ch == 127U || ch == '/' || ch == '\\') return false;
    }
    return true;
}

/* Decode a 512 byte object header page. The layout is documented in the
 * header; the name is NUL terminated inside its field and the rest of the
 * page is 0xFF, because mkyaffs2image fills the struct with 0xFF first. */
static bool xx_yaffs_decode_header(const uint8_t *page, bool big_endian,
                                   xx_yaffs_header *header) {
    uint32_t checksum;
    uint32_t size_low;
    uint32_t size_high;
    size_t length = 0U;
    xx_mem_zero(header, sizeof(*header));
    header->type = xx_data_get_u32(page, XX_YAFFS_HEADER_SIZE, 0U, big_endian);
    header->parent_id =
        xx_data_get_u32(page, XX_YAFFS_HEADER_SIZE, 4U, big_endian);
    checksum = xx_data_get_u16(page, XX_YAFFS_HEADER_SIZE, 8U, big_endian);
    /* sum_no_longer_used has been a fixed 0xFFFF since YAFFS dropped name
     * checksums; it is the single most useful structural constraint there
     * is, and binwalk leans on it for the same reason. */
    if (checksum != 0xFFFFU) return false;
    if (header->type > XX_YAFFS_OBJECT_TYPE_SPECIAL) return false;
    if (header->parent_id == 0U || header->parent_id == 0xFFFFFFFFU) {
        return false;
    }
    while (length <= XX_YAFFS_NAME_MAX &&
           page[XX_YAFFS_NAME_FIELD + length] != 0U) {
        ++length;
    }
    if (length > XX_YAFFS_NAME_MAX) return false;
    if (!xx_yaffs_plausible_name((const char *)(page + XX_YAFFS_NAME_FIELD),
                                 length)) {
        return false;
    }
    if (length != 0U) {
        xx_mem_copy(header->name, page + XX_YAFFS_NAME_FIELD, length);
    }
    header->name[length] = '\0';
    header->name_length = length;
    header->mode = xx_data_get_u32(page, XX_YAFFS_HEADER_SIZE, 268U, big_endian);
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
    return true;
}

static bool xx_yaffs_read_header(xx_io_device *device, int64_t total_size,
                                 int64_t chunk_offset, bool big_endian,
                                 xx_yaffs_header *header) {
    uint8_t page[XX_YAFFS_HEADER_SIZE];
    if (!xx_yaffs_range_within(total_size, chunk_offset,
                               (int64_t)sizeof(page)) ||
        !xx_yaffs_read_at(device, chunk_offset, page, sizeof(page))) {
        return false;
    }
    return xx_yaffs_decode_header(page, big_endian, header);
}

/* ------------------------------------------------------------------------ */
/* Geometry detection                                                       */
/* ------------------------------------------------------------------------ */

/* The candidate space, in the order it is searched. Ties are broken by this
 * order - the first candidate to reach a given score keeps it - so the sizes
 * are listed with the common real-world value first. */
static const uint32_t xx_yaffs_page_sizes[] = {2048U, 512U,  1024U,
                                               4096U, 8192U, 16384U};
static const uint32_t xx_yaffs_spare_sizes[] = {64U, 16U, 32U, 128U, 256U, 0U};

/* Score one geometry over the leading chunks of the image. A negative score
 * rejects the candidate outright. */
static int xx_yaffs_score_geometry(xx_io_device *device,
                                   const xx_yaffs_geometry *geometry,
                                   int64_t base, int64_t total_size,
                                   const uint8_t *root_page) {
    xx_yaffs_header header;
    xx_yaffs_tags tags;
    int64_t chunk_size = (int64_t)geometry->page_size +
                         (int64_t)geometry->spare_size;
    int64_t available = total_size - base;
    int64_t chunks = available / chunk_size;
    int64_t index;
    int64_t pending_pages = 0; /* TAGS_NONE: data pages still owed to a file. */
    int score = 0;
    if (chunks < 1) return -1;
    /* Chunk 0 must be SOME object's header -- not necessarily the root's.
     * Requiring a DIRECTORY with parent 1 here rejected real mkyaffs2image
     * output (binwalk's own tests/inputs/yaffs2.bin begins with a FILE whose
     * parent is the implicit root), so the type is only required to be one of
     * the five defined kinds and the parent to be a plausible object id. The
     * discriminating work is done by the tags below and by the scoring pass:
     * chunk_id must be 0, because an object header is always chunk 0 of its
     * object. This is the whole of the structural signature, so it is checked
     * before any per-candidate work. The page is the same bytes for every
     * candidate, so the caller reads it once and it is only decoded again
     * here, at this candidate's endianness. */
    if (!xx_yaffs_decode_header(root_page, geometry->big_endian, &header) ||
        header.type < XX_YAFFS_OBJECT_TYPE_FILE ||
        header.type > XX_YAFFS_OBJECT_TYPE_SPECIAL ||
        header.parent_id < XX_YAFFS_OBJECTID_ROOT) {
        return -1;
    }
    score = 10;
    if (geometry->kind != XX_YAFFS_TAGS_NONE) {
        /* The root object is object 1 and its header is chunk 0 of that
         * object. Getting both out of the spare pins the page size, the
         * spare offset, the endianness and the tag layout at once. */
        if (!xx_yaffs_read_tags(device, geometry, total_size, base, &tags) ||
            tags.object_id < XX_YAFFS_OBJECTID_ROOT || tags.chunk_id != 0U) {
            return -1;
        }
        score += 20;
        if (geometry->kind == XX_YAFFS_TAGS_V2 && tags.sequence != 0U &&
            tags.sequence != 0xFFFFFFFFU) {
            score += 5;
        }
    }
    if (chunks > (int64_t)XX_YAFFS_PROBE_CHUNKS) {
        chunks = (int64_t)XX_YAFFS_PROBE_CHUNKS;
    }
    for (index = 1; index < chunks; ++index) {
        int64_t offset = base + index * chunk_size;
        if (geometry->kind == XX_YAFFS_TAGS_NONE) {
            /* Without a spare there is nothing to key on but the page
             * contents, so a page is a header only when no file is still
             * owed data - exactly the order mkyaffs2image writes. */
            if (pending_pages > 0) {
                /* Data pages are free: crediting them would let a page size
                 * that is too small win by inventing extra pages. */
                --pending_pages;
                continue;
            }
            if (xx_yaffs_read_header(device, total_size, offset,
                                     geometry->big_endian, &header) &&
                header.name_length != 0U) {
                score += 3;
                if (header.type == XX_YAFFS_OBJECT_TYPE_FILE &&
                    header.file_size != 0U) {
                    pending_pages = (int64_t)((header.file_size +
                                               geometry->page_size - 1U) /
                                              geometry->page_size);
                }
            } else {
                /* A chunk that is neither a header nor data owed to one is a
                 * hole in the story. Holes are what tells a too-small page
                 * size apart from the right one, since the smaller size sees
                 * every real header and a pile of filler in between. */
                --score;
            }
            continue;
        }
        if (!xx_yaffs_read_tags(device, geometry, total_size, offset, &tags)) {
            break;
        }
        /* An erased chunk reads as all ones; it is neither a hit nor a miss. */
        if (tags.object_id == 0xFFFFFFFFU || tags.object_id == 0U) continue;
        if (tags.byte_count > geometry->page_size) continue;
        ++score;
        if (tags.chunk_id == 0U &&
            xx_yaffs_read_header(device, total_size, offset,
                                 geometry->big_endian, &header)) {
            score += 3;
        }
    }
    return score;
}

/* Brute-force the page size, spare size, spare offset, endianness and tag
 * layout. The space is at most 6 pages x 6 spares x 2 endiannesses x 2 spare
 * offsets x 2 tag layouts, and almost all of it is rejected by the two reads
 * that chunk 0 costs, so the search is bounded and deterministic. */
static bool xx_yaffs_detect(xx_io_device *device, int64_t base,
                            int64_t total_size, xx_yaffs_geometry *out,
                            xx_pd_struct *pd) {
    uint8_t root_page[XX_YAFFS_HEADER_SIZE];
    xx_yaffs_header header;
    xx_yaffs_geometry best;
    int best_score = 0;
    size_t kind_index;
    static const xx_yaffs_tag_kind kinds[] = {
        XX_YAFFS_TAGS_V2, XX_YAFFS_TAGS_V1, XX_YAFFS_TAGS_NONE};
    if (!device || !out) return false;
    xx_mem_zero(&best, sizeof(best));
    /* One read settles whether this can be YAFFS at all: the candidates only
     * ever differ from chunk 0's page onwards, and every one of them demands
     * the same root directory header there. */
    if (!xx_yaffs_range_within(total_size, base, (int64_t)sizeof(root_page)) ||
        !xx_yaffs_read_at(device, base, root_page, sizeof(root_page))) {
        return false;
    }
    if (!xx_yaffs_decode_header(root_page, false, &header) &&
        !xx_yaffs_decode_header(root_page, true, &header)) {
        return false;
    }
    for (kind_index = 0U;
         kind_index < sizeof(kinds) / sizeof(kinds[0]); ++kind_index) {
        size_t endian_index;
        /* The spare-less layout is a last resort: it has no tags to key on,
         * so it is only tried when nothing with tags matched at all. That
         * also keeps it - the one candidate kind whose scan cannot be cut
         * short by a cheap tag check - out of the common path. */
        if (kinds[kind_index] == XX_YAFFS_TAGS_NONE && best_score > 0) break;
        for (endian_index = 0U; endian_index < 2U; ++endian_index) {
            size_t offset_index;
            for (offset_index = 0U; offset_index < 2U; ++offset_index) {
                size_t page_index;
                /* Only YAFFS2 has the no-ECC layout that shifts the tags. */
                if (offset_index != 0U && kinds[kind_index] != XX_YAFFS_TAGS_V2) {
                    continue;
                }
                for (page_index = 0U;
                     page_index < sizeof(xx_yaffs_page_sizes) /
                                      sizeof(xx_yaffs_page_sizes[0]);
                     ++page_index) {
                    size_t spare_index;
                    for (spare_index = 0U;
                         spare_index < sizeof(xx_yaffs_spare_sizes) /
                                           sizeof(xx_yaffs_spare_sizes[0]);
                         ++spare_index) {
                        xx_yaffs_geometry candidate;
                        int score;
                        candidate.page_size = xx_yaffs_page_sizes[page_index];
                        candidate.spare_size = xx_yaffs_spare_sizes[spare_index];
                        candidate.tag_offset = offset_index ? 2U : 0U;
                        candidate.kind = kinds[kind_index];
                        candidate.big_endian = endian_index != 0U;
                        if (pd && xx_pd_is_stopped(pd)) return false;
                        /* A spare has to be there to hold tags, and has to
                         * be big enough to hold them at the given offset. */
                        if (candidate.kind == XX_YAFFS_TAGS_NONE) {
                            if (candidate.spare_size != 0U) continue;
                        } else if (candidate.spare_size <
                                   candidate.tag_offset + XX_YAFFS_TAGS_SIZE) {
                            continue;
                        }
                        score = xx_yaffs_score_geometry(
                            device, &candidate, base, total_size, root_page);
                        if (score > best_score) {
                            best_score = score;
                            best = candidate;
                        }
                    }
                }
            }
        }
    }
    if (best_score <= 0) return false;
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
/* Parsed state lifetime                                                    */
/* ------------------------------------------------------------------------ */

static void xx_yaffs_private_cleanup(xx_yaffs_private *parsed) {
    size_t index;
    if (!parsed) return;
    for (index = 0U; index < parsed->object_count; ++index) {
        if (parsed->objects[index].path) xx_str_free(parsed->objects[index].path);
        if (parsed->objects[index].name) xx_str_free(parsed->objects[index].name);
    }
    if (parsed->objects) xx_mem_free(parsed->objects);
    if (parsed->chunks) xx_mem_free(parsed->chunks);
    if (parsed->records) xx_mem_free(parsed->records);
    xx_yaffs_map_cleanup(&parsed->map);
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

/* Record an object header. A repeated object id is an update in the log, so
 * the newest sequence number wins and the older record is overwritten in
 * place - which keeps the map and the object indexes stable. */
static bool xx_yaffs_add_object(xx_yaffs_private *parsed, uint32_t object_id,
                                uint32_t sequence, int64_t header_offset,
                                const xx_yaffs_header *header) {
    size_t index;
    xx_yaffs_object *object;
    char *name;
    if (xx_yaffs_map_get(&parsed->map, object_id, &index)) {
        object = &parsed->objects[index];
        if (sequence < object->sequence) return true;
        name = xx_str_create(header->name);
        if (!name) return false;
        if (object->name) xx_str_free(object->name);
        object->name = name;
        object->parent_id = header->parent_id;
        object->type = header->type;
        object->sequence = sequence;
        object->file_size = header->file_size;
        object->header_offset = header_offset;
        return true;
    }
    if (parsed->object_count >= XX_YAFFS_MAX_OBJECTS) return true;
    if (!xx_yaffs_reserve_objects(parsed)) return false;
    object = &parsed->objects[parsed->object_count];
    xx_mem_zero(object, sizeof(*object));
    object->name = xx_str_create(header->name);
    if (!object->name) return false;
    object->object_id = object_id;
    object->parent_id = header->parent_id;
    object->type = header->type;
    object->sequence = sequence;
    object->file_size = header->file_size;
    object->header_offset = header_offset;
    if (!xx_yaffs_map_put(&parsed->map, object_id,
                          (uint32_t)parsed->object_count)) {
        xx_str_free(object->name);
        object->name = NULL;
        return false;
    }
    ++parsed->object_count;
    return true;
}

static bool xx_yaffs_add_chunk(xx_yaffs_private *parsed, size_t object_index,
                               uint32_t chunk_id, uint32_t sequence,
                               uint32_t byte_count, int64_t offset) {
    xx_yaffs_data_chunk *chunk;
    if (parsed->chunk_count >= XX_YAFFS_MAX_DATA_CHUNKS) return true;
    if (!xx_yaffs_reserve_chunks(parsed)) return false;
    chunk = &parsed->chunks[parsed->chunk_count];
    chunk->object_index = (uint32_t)object_index;
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

/* Order by (object, chunk id, sequence) so that each object's chunks become
 * one contiguous run, in file order, with the newest version of a repeated
 * chunk last. Heapsort: qsort() is CRT, and recursion depth is one more
 * thing that would have to be bounded against a hostile input. */
static bool xx_yaffs_chunk_less(const xx_yaffs_data_chunk *left,
                                const xx_yaffs_data_chunk *right) {
    if (left->object_index != right->object_index) {
        return left->object_index < right->object_index;
    }
    if (left->chunk_id != right->chunk_id) {
        return left->chunk_id < right->chunk_id;
    }
    return left->sequence < right->sequence;
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

static char *xx_yaffs_join_path(const char *prefix, const char *name) {
    size_t prefix_size = prefix ? xx_str_len(prefix) : 0U;
    size_t name_size = name ? xx_str_len(name) : 0U;
    char *combined;
    if (!name || name_size == 0U || prefix_size >= XX_YAFFS_MAX_PATH ||
        name_size > XX_YAFFS_MAX_PATH - prefix_size -
                        (prefix_size != 0U ? 1U : 0U)) {
        return NULL;
    }
    combined = (char *)xx_mem_alloc(prefix_size + name_size +
                                    (prefix_size != 0U ? 2U : 1U));
    if (!combined) return NULL;
    if (prefix_size != 0U) {
        xx_mem_copy(combined, prefix, prefix_size);
        combined[prefix_size] = '/';
        xx_mem_copy(combined + prefix_size + 1U, name, name_size);
        combined[prefix_size + 1U + name_size] = '\0';
    } else {
        xx_mem_copy(combined, name, name_size);
        combined[name_size] = '\0';
    }
    return combined;
}

/* Resolve one object's full path by walking its parent chain.
 *
 * The chain is the cycle hazard in this format: parent_id is a raw, entirely
 * unvalidated field, so an object can name itself, or two objects can name
 * each other, or ten thousand can form one long ring. The walk therefore
 * marks each object "in progress" on the way up and stops the moment it
 * meets a mark it made itself - and stops again at XX_YAFFS_MAX_DEPTH links
 * whether or not a cycle was proven. Everything reached along a chain that
 * ended badly is flagged as an orphan and filed under lost+found rather than
 * being dropped, so a single bad link does not cost a whole subtree.
 *
 * Objects already resolved short-circuit the walk, which makes the whole
 * pass linear in the number of objects no matter how deep the tree is. */
static bool xx_yaffs_resolve_path(xx_yaffs_private *parsed, size_t start,
                                  size_t *stack) {
    size_t depth = 0U;
    size_t current = start;
    size_t base = parsed->object_count; /* object_count means "no base". */
    bool orphan = false;
    for (;;) {
        xx_yaffs_object *object = &parsed->objects[current];
        size_t parent_index;
        if (object->resolve == 2U) {
            base = current;
            orphan = object->orphan;
            break;
        }
        if (object->resolve == 1U) {
            /* Walked into our own footprints: the chain is a cycle. */
            orphan = true;
            break;
        }
        if (depth >= XX_YAFFS_MAX_DEPTH) {
            orphan = true;
            break;
        }
        object->resolve = 1U;
        stack[depth++] = current;
        if (object->object_id == XX_YAFFS_OBJECTID_ROOT ||
            object->parent_id == XX_YAFFS_OBJECTID_ROOT) {
            break;
        }
        if (object->parent_id == XX_YAFFS_OBJECTID_UNLINKED ||
            object->parent_id == XX_YAFFS_OBJECTID_DELETED) {
            /* Deleted and unlinked objects are not part of the tree. */
            orphan = true;
            break;
        }
        if (!xx_yaffs_map_get(&parsed->map, object->parent_id, &parent_index)) {
            orphan = true;
            break;
        }
        current = parent_index;
    }
    /* Unwind, building each path from the one above it. */
    while (depth-- > 0U) {
        xx_yaffs_object *object = &parsed->objects[stack[depth]];
        const char *prefix = "";
        object->resolve = 2U;
        object->orphan = orphan;
        if (object->object_id == XX_YAFFS_OBJECTID_ROOT) {
            /* The root directory itself has no path and is not listed. */
            base = stack[depth];
            continue;
        }
        if (base < parsed->object_count && parsed->objects[base].path) {
            prefix = parsed->objects[base].path;
        } else if (orphan) {
            prefix = XX_YAFFS_LOST_DIR;
        }
        if (object->name && object->name[0] != '\0') {
            /* A NULL here means the path outgrew XX_YAFFS_MAX_PATH, or the
             * allocation failed. Either way the object is simply left out of
             * the listing; one unrepresentable name must not sink the walk. */
            object->path = xx_yaffs_join_path(prefix, object->name);
        }
        base = stack[depth];
    }
    return true;
}

static bool xx_yaffs_build_paths(xx_yaffs_private *parsed) {
    size_t *stack;
    size_t index;
    if (parsed->object_count == 0U) return true;
    stack = (size_t *)xx_mem_calloc(XX_YAFFS_MAX_DEPTH + 1U, sizeof(*stack));
    if (!stack) return false;
    for (index = 0U; index < parsed->object_count; ++index) {
        if (parsed->objects[index].resolve != 2U &&
            !xx_yaffs_resolve_path(parsed, index, stack)) {
            xx_mem_free(stack);
            return false;
        }
    }
    xx_mem_free(stack);
    return true;
}

/* ------------------------------------------------------------------------ */
/* Scanning                                                                 */
/* ------------------------------------------------------------------------ */

/* Pass A: every chunk whose tags say "chunk 0 of object N" and whose page
 * decodes as an object header becomes an object. */
static bool xx_yaffs_scan_headers(Abstractformat *self,
                                  xx_yaffs_private *parsed, xx_pd_struct *pd) {
    int64_t index;
    for (index = 0; index < parsed->total_chunks; ++index) {
        int64_t offset = self->base_address + index * parsed->chunk_size;
        xx_yaffs_tags tags;
        xx_yaffs_header header;
        if (pd && xx_pd_is_stopped(pd)) return false;
        if (!xx_yaffs_read_tags(self->device, &parsed->geometry,
                                parsed->input_size, offset, &tags)) {
            continue;
        }
        if (tags.chunk_id != 0U || tags.object_id == 0U ||
            tags.object_id == 0xFFFFFFFFU) {
            continue;
        }
        if (!xx_yaffs_read_header(self->device, parsed->input_size, offset,
                                  parsed->geometry.big_endian, &header)) {
            continue;
        }
        /* Only the root may be nameless. */
        if (header.name_length == 0U &&
            tags.object_id != XX_YAFFS_OBJECTID_ROOT) {
            continue;
        }
        if (!xx_yaffs_add_object(parsed, tags.object_id, tags.sequence, offset,
                                 &header)) {
            return false;
        }
    }
    return true;
}

/* Pass B: every chunk whose tags name a known object and a non-zero chunk id
 * is one page of that object's data. */
static bool xx_yaffs_scan_data(Abstractformat *self, xx_yaffs_private *parsed,
                               xx_pd_struct *pd) {
    int64_t index;
    for (index = 0; index < parsed->total_chunks; ++index) {
        int64_t offset = self->base_address + index * parsed->chunk_size;
        xx_yaffs_tags tags;
        size_t object_index;
        if (pd && xx_pd_is_stopped(pd)) return false;
        if (!xx_yaffs_read_tags(self->device, &parsed->geometry,
                                parsed->input_size, offset, &tags)) {
            continue;
        }
        if (tags.chunk_id == 0U || tags.object_id == 0U ||
            tags.object_id == 0xFFFFFFFFU) {
            continue;
        }
        /* The top bit of chunk_id marks YAFFS2 in-band tags, where the
         * header travels in the tags instead of in a page. Those chunks
         * carry no file data and are skipped; see the report notes. */
        if ((tags.chunk_id & 0x80000000U) != 0U) continue;
        if (!xx_yaffs_map_get(&parsed->map, tags.object_id, &object_index)) {
            continue;
        }
        if (parsed->objects[object_index].type != XX_YAFFS_OBJECT_TYPE_FILE) {
            continue;
        }
        if (!xx_yaffs_add_chunk(parsed, object_index, tags.chunk_id,
                                tags.sequence, tags.byte_count, offset)) {
            return false;
        }
    }
    return true;
}

/* The spare-less layout has no tags at all, so objects and their data are
 * recovered positionally: a page is an object header unless the preceding
 * file header is still owed data pages, and object ids are assigned in the
 * order mkyaffs2image assigns them - 1 for the root, then upward. This is a
 * heuristic, it is scored below every tagged candidate, and it only ever
 * runs when no tagged geometry matched at all. */
static bool xx_yaffs_scan_positional(Abstractformat *self,
                                     xx_yaffs_private *parsed,
                                     xx_pd_struct *pd) {
    int64_t index;
    uint32_t next_id = XX_YAFFS_OBJECTID_ROOT;
    uint32_t pending_object = 0U;
    uint32_t pending_chunk = 0U;
    int64_t pending_pages = 0;
    for (index = 0; index < parsed->total_chunks; ++index) {
        int64_t offset = self->base_address + index * parsed->chunk_size;
        xx_yaffs_header header;
        size_t object_index;
        if (pd && xx_pd_is_stopped(pd)) return false;
        if (pending_pages > 0) {
            if (xx_yaffs_map_get(&parsed->map, pending_object, &object_index) &&
                !xx_yaffs_add_chunk(parsed, object_index, ++pending_chunk, 0U,
                                    parsed->geometry.page_size, offset)) {
                return false;
            }
            --pending_pages;
            continue;
        }
        if (!xx_yaffs_read_header(self->device, parsed->input_size, offset,
                                  parsed->geometry.big_endian, &header)) {
            continue;
        }
        if (header.name_length == 0U && next_id != XX_YAFFS_OBJECTID_ROOT) {
            continue;
        }
        if (next_id == 0xFFFFFFFFU) break;
        if (!xx_yaffs_add_object(parsed, next_id, 0U, offset, &header)) {
            return false;
        }
        if (header.type == XX_YAFFS_OBJECT_TYPE_FILE && header.file_size != 0U) {
            pending_object = next_id;
            pending_chunk = 0U;
            pending_pages = (int64_t)((header.file_size +
                                       parsed->geometry.page_size - 1U) /
                                      parsed->geometry.page_size);
        }
        /* mkyaffs2image hands out the root id 1 and then starts at
         * YAFFS_NOBJECT_BUCKETS + 1, skipping the reserved lost+found,
         * unlinked and deleted ids; mirror that so the synthesised parent
         * ids line up with the ones written into the headers. */
        next_id = next_id == XX_YAFFS_OBJECTID_ROOT ? 257U : next_id + 1U;
    }
    return true;
}

/* Turn the sorted chunk array into one contiguous run per object. */
static void xx_yaffs_assign_chunk_runs(xx_yaffs_private *parsed) {
    size_t index = 0U;
    while (index < parsed->chunk_count) {
        size_t object_index = parsed->chunks[index].object_index;
        size_t start = index;
        while (index < parsed->chunk_count &&
               parsed->chunks[index].object_index == object_index) {
            ++index;
        }
        if (object_index < parsed->object_count) {
            parsed->objects[object_index].first_chunk = start;
            parsed->objects[object_index].chunk_count = index - start;
        }
    }
}

/* The listing: every object that got a path, in object order. */
static bool xx_yaffs_build_records(xx_yaffs_private *parsed) {
    size_t index;
    size_t count = 0U;
    for (index = 0U; index < parsed->object_count; ++index) {
        if (parsed->objects[index].path) ++count;
    }
    if (count == 0U) return false;
    parsed->records = (size_t *)xx_mem_calloc(count, sizeof(*parsed->records));
    if (!parsed->records) return false;
    for (index = 0U; index < parsed->object_count; ++index) {
        if (parsed->objects[index].path) {
            parsed->records[parsed->record_count++] = index;
        }
    }
    return true;
}

static bool xx_yaffs_parse(Abstractformat *self, xx_yaffs_private *parsed,
                           xx_pd_struct *pd) {
    int64_t total_size;
    int64_t available;
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
    parsed->chunk_size = (int64_t)parsed->geometry.page_size +
                         (int64_t)parsed->geometry.spare_size;
    available = total_size - self->base_address;
    parsed->total_chunks = available / parsed->chunk_size;
    if (parsed->total_chunks > (int64_t)XX_YAFFS_MAX_CHUNKS) {
        parsed->total_chunks = (int64_t)XX_YAFFS_MAX_CHUNKS;
    }
    parsed->archive_end =
        self->base_address + parsed->total_chunks * parsed->chunk_size;
    if (parsed->geometry.kind == XX_YAFFS_TAGS_NONE) {
        if (!xx_yaffs_scan_positional(self, parsed, pd)) goto fail;
    } else {
        if (!xx_yaffs_scan_headers(self, parsed, pd)) goto fail;
        if (!xx_yaffs_scan_data(self, parsed, pd)) goto fail;
    }
    xx_yaffs_sort_chunks(parsed->chunks, parsed->chunk_count);
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

static bool xx_yaffs_populate_record(xx_archive_record *record,
                                     const xx_yaffs_private *parsed,
                                     size_t object_index) {
    const xx_yaffs_object *object;
    int64_t data_offset;
    int64_t size;
    bool is_folder;
    if (!record || !parsed || object_index >= parsed->object_count) return false;
    object = &parsed->objects[object_index];
    if (!object->path) return false;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    is_folder = object->type == XX_YAFFS_OBJECT_TYPE_DIRECTORY;
    size = object->type == XX_YAFFS_OBJECT_TYPE_FILE
               ? (int64_t)(object->file_size & INT64_MAX)
               : 0;
    /* A file's pages are scattered, so data_offset can only point at the
     * first of them; unpacking walks the chunk run instead. */
    data_offset = object->chunk_count != 0U
                      ? parsed->chunks[object->first_chunk].offset
                      : object->header_offset;
    record->header_offset = object->header_offset;
    record->header_size = (int64_t)XX_YAFFS_HEADER_SIZE;
    record->data_offset = data_offset;
    record->compressed_size = size;
    return xx_archive_record_set_original_name(record, object->path) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          0U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           is_folder);
}

static void xx_yaffs_archive_stream_free(void *pointer) {
    xx_yaffs_archive_stream *stream = (xx_yaffs_archive_stream *)pointer;
    if (!stream) return;
    xx_yaffs_private_cleanup(&stream->parsed);
    xx_mem_free(stream);
}

/* Extraction-time name check: the path must stay inside the destination tree
 * on every host this library builds for, so the punctuation Windows reserves
 * is refused here even though YAFFS may legally carry it. */
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

/* Walk one file's chunk run, writing the pages out in chunk id order. The
 * run is sorted, so a repeated chunk id appears as a group whose last member
 * carries the newest sequence number; that member is the live one. */
static bool xx_yaffs_write_file(Abstractformat *self,
                                const xx_yaffs_private *parsed,
                                const xx_yaffs_object *object,
                                const char *destination, xx_pd_struct *pd) {
    xx_io_device *output;
    uint8_t *page;
    uint64_t remaining = object->file_size;
    uint32_t expected = 1U;
    size_t index = object->first_chunk;
    size_t end = object->first_chunk + object->chunk_count;
    bool result = true;
    output = xx_io_file_open(destination, "wb");
    if (!output) return false;
    if (remaining == 0U) {
        xx_io_close(output);
        return true;
    }
    page = (uint8_t *)xx_mem_alloc(parsed->geometry.page_size);
    if (!page) {
        xx_io_close(output);
        return false;
    }
    while (remaining != 0U && index < end) {
        size_t last = index;
        size_t want;
        ssize_t written;
        while (last + 1U < end &&
               parsed->chunks[last + 1U].chunk_id ==
                   parsed->chunks[index].chunk_id) {
            ++last;
        }
        if (pd && xx_pd_is_stopped(pd)) {
            result = false;
            break;
        }
        if (parsed->chunks[last].chunk_id != expected) {
            result = false;
            break;
        }
        want = remaining < (uint64_t)parsed->geometry.page_size
                   ? (size_t)remaining
                   : (size_t)parsed->geometry.page_size;
        if (!xx_yaffs_read_at(self->device, parsed->chunks[last].offset, page,
                              want)) {
            result = false;
            break;
        }
        written = xx_io_write(output, page, want);
        if (written < 0 || (size_t)written != want) {
            result = false;
            break;
        }
        remaining -= (uint64_t)want;
        ++expected;
        index = last + 1U;
    }
    if (remaining != 0U) result = false;
    xx_mem_free(page);
    xx_io_close(output);
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
 * YAFFS has no magic, so this has to be a late-dispatch probe that every
 * other format gets offered first - and a probe that runs over every format
 * must not be linear in the size of the file. Parsing a 256 MiB NAND dump
 * costs a pass over a hundred thousand chunks; deciding that chunk 0 is not
 * a root object header costs one 512 byte read. So the validity check stops
 * at the geometry search, which touches at most XX_YAFFS_PROBE_CHUNKS
 * chunks of the winning candidate and far fewer of the rest.
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
    yaffs->has_spare = parsed->geometry.kind != XX_YAFFS_TAGS_NONE;
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
    if (!xx_yaffs_copy_options(&state->options, options) ||
        !xx_yaffs_parse(self, &stream->parsed, pd)) {
        xx_yaffs_archive_stream_free(stream);
        xx_archive_record_state_free(state);
        return NULL;
    }
    stream->index = 0U;
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
    const xx_yaffs_archive_stream *stream;
    const xx_yaffs_object *object;
    const xx_var *option;
    const char *name;
    const char *base = NULL;
    char *owned_base = NULL;
    char *destination = NULL;
    bool result;
    if (!self || !self->device || !state || state->format != self ||
        !state->has_record || !state->internal_state ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (const xx_yaffs_archive_stream *)state->internal_state;
    if (stream->index >= stream->parsed.record_count) return false;
    object = &stream->parsed.objects[stream->parsed.records[stream->index]];
    record = &state->current_record;
    name = xx_archive_record_get_original_name(record);
    if (!xx_yaffs_safe_name(name)) return false;
    option = xx_yaffs_find_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!option) {
        /* Test mode: no destination, so only confirm the payload is present
         * and inside the device. */
        int64_t total = xx_io_total_size(self->device);
        size_t index;
        if (object->type != XX_YAFFS_OBJECT_TYPE_FILE) return true;
        for (index = 0U; index < object->chunk_count; ++index) {
            const xx_yaffs_data_chunk *chunk =
                &stream->parsed.chunks[object->first_chunk + index];
            if (!xx_yaffs_range_within(
                    total, chunk->offset,
                    (int64_t)stream->parsed.geometry.page_size)) {
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
    } else if (object->type == XX_YAFFS_OBJECT_TYPE_FILE) {
        if (xx_store_create_dirs_a(destination, false)) {
            result = xx_yaffs_write_file(self, &stream->parsed, object,
                                         destination, pd);
            if (!result) xx_rt_remove(destination);
        } else {
            result = false;
        }
    } else {
        /* Symlinks, hardlinks and device nodes carry no payload here. They
         * stay in the listing but nothing is written for them. */
        result = true;
    }
    if (owned_base) xx_str_free(owned_base);
    xx_str_free(destination);
    return result;
cleanup:
    if (owned_base) xx_str_free(owned_base);
    if (destination) xx_str_free(destination);
    return false;
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
