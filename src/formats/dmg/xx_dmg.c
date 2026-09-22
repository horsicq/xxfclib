/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Ported from XArchive's diskimages/xdmg.cpp, which is the reference this
 * reader follows for the trailer and block-table layouts, for the run
 * validation rules (including hdiutil's habit of parking the running data
 * fork cursor in the comment and terminator descriptors) and for the refusal
 * to fabricate zeros for a codec that is not implemented.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/dmg/xx_dmg.h"

#include "xxfclib/algo/bzip2/xx_bzip2.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/lzfse/xx_lzfse.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/xml/xx_xml.h"

/* Registration placeholder. xxfc_defs.h is shared and is not edited from
 * here, so the file-type constant resolves to UNKNOWN until the enumerator
 * lands. Delete this block once XX_FILE_TYPE_DMG exists in the enum. */
#ifdef DMG
#define XX_DMG_FILE_TYPE XX_FILE_TYPE_DMG
#else
#define XX_DMG_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

/* The property list and one block table are each read into memory whole, so
 * both carry a hard ceiling. 64 MiB is far above anything hdiutil writes. */
#define XX_DMG_MAX_XML_SIZE (UINT64_C(64) << 20)
#define XX_DMG_MAX_MISH_SIZE (UINT64_C(64) << 20)

/* One blkx table per partition, and one run descriptor per stripe. The run
 * cap is per image rather than per partition: a 40-byte descriptor expands to
 * an arbitrary number of sectors, so the table itself is the only thing that
 * grows with the input, and one million of them is already 32 MiB. */
#define XX_DMG_MAX_PARTITIONS 4096U
#define XX_DMG_MAX_RUNS 1000000U

/* Names come out of the property list, so both the text and the nesting are
 * attacker controlled. */
#define XX_DMG_MAX_NAME_SIZE 512U
#define XX_DMG_MAX_XML_DEPTH 64

/*
 * Expansion ceiling.
 *
 * A 40-byte zero-fill descriptor may declare any 64-bit sector count, so
 * nothing in the container bounds what a partition expands to: twelve bytes
 * of run table can ask for petabytes of zeros. That allocates nothing, but an
 * unattended caller would sit there writing it, so a partition declaring an
 * expansion past this ceiling is refused outright rather than started.
 *
 * XX_META_ID_OPT_MAX_MEMBER_SIZE overrides it in either direction, following
 * the convention the sparse, zip and rar readers use.
 */
#define XX_DMG_DEFAULT_MAX_EXPANDED (UINT64_C(64) << 30)

/** Staging buffer for raw copies and for zero fill. */
#define XX_DMG_STAGING_SIZE 65536U

/* A compressed run is staged in memory, input and output both, so its
 * declared output size is capped well below the whole-partition ceiling.
 * hdiutil writes runs of at most a few megabytes. */
#define XX_DMG_MAX_RUN_OUTPUT (UINT64_C(64) << 20)
#define XX_DMG_MAX_RUN_INPUT (UINT64_C(64) << 20)

typedef struct xx_dmg_run_s {
    uint32_t type;
    uint64_t sector_count;
    uint64_t data_offset;  /**< Relative to the partition's data. */
    uint64_t data_length;
} xx_dmg_run;

typedef struct xx_dmg_partition_s {
    char *name;            /**< Sanitised member name, never NULL. */
    uint64_t start_sector; /**< The mish header's first sector. */
    uint64_t sector_count;
    uint64_t data_offset;  /**< Relative to the data fork. */
    size_t run_first;      /**< Index of the first run in parsed->runs. */
    size_t run_count;
    int64_t expanded_size;
} xx_dmg_partition;

typedef struct xx_dmg_private_s {
    xx_dmg_partition *partitions;
    size_t count;
    size_t capacity;
    xx_dmg_run *runs;
    size_t run_count;
    size_t run_capacity;
    int64_t input_size;
    int64_t koly_offset;
    int64_t archive_end;
    int64_t data_fork_offset;
    int64_t data_fork_length;
    int64_t xml_offset;
    int64_t xml_length;
    uint64_t sector_count;
    uint32_t version;
    uint32_t flags;
    uint32_t image_variant;
} xx_dmg_private;

typedef struct xx_dmg_archive_stream_s {
    xx_dmg_private parsed;
    size_t index;
} xx_dmg_archive_stream;

static void xx_dmg_vtable_destroy(Abstractformat *self);
static uint64_t xx_dmg_max_expanded(const Abstractformat *self,
                                    const xx_list_s *options);

/* ------------------------------------------------------------------------ */
/* Bounded I/O and arithmetic                                                */
/* ------------------------------------------------------------------------ */

/* All positioning goes through seek64: a DMG routinely exceeds 2 GiB and
 * long is 32-bit on Win64. */
static bool xx_dmg_read_at(xx_io_device *device, int64_t offset, void *data,
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

static bool xx_dmg_write_all(xx_io_device *device, const void *data,
                             size_t size) {
    const uint8_t *in = (const uint8_t *)data;
    size_t done = 0U;
    if (!device || (!data && size != 0U)) return false;
    while (done < size) {
        ssize_t put = xx_io_write(device, in + done, size - done);
        if (put <= 0 || (size_t)put > size - done) return false;
        done += (size_t)put;
    }
    return true;
}

static bool xx_dmg_add(int64_t left, uint64_t right, int64_t *result) {
    if (!result || left < 0 || right > (uint64_t)(INT64_MAX - left)) {
        return false;
    }
    *result = left + (int64_t)right;
    return true;
}

/* True when [offset, offset + size) lies inside [0, total_size). */
static bool xx_dmg_range_within(int64_t total_size, int64_t offset,
                                int64_t size) {
    return (total_size >= 0) && (offset >= 0) && (size >= 0) &&
           (offset <= total_size) && (size <= total_size - offset);
}

/* True when [offset, offset + length) lies inside [0, available), in the
 * unsigned arithmetic the UDIF fields are expressed in. */
static bool xx_dmg_urange_within(uint64_t offset, uint64_t length,
                                 uint64_t available) {
    return offset <= available && length <= available - offset;
}

/* Sectors to bytes, refusing anything that will not fit an int64_t. */
static bool xx_dmg_sectors_to_bytes(uint64_t sectors, int64_t *result) {
    if (!result || sectors > (uint64_t)INT64_MAX / XX_DMG_SECTOR_SIZE) {
        return false;
    }
    *result = (int64_t)(sectors * XX_DMG_SECTOR_SIZE);
    return true;
}

/* ------------------------------------------------------------------------ */
/* Base64                                                                    */
/* ------------------------------------------------------------------------ */

static int xx_dmg_base64_value(char ch) {
    if (ch >= 'A' && ch <= 'Z') return ch - 'A';
    if (ch >= 'a' && ch <= 'z') return ch - 'a' + 26;
    if (ch >= '0' && ch <= '9') return ch - '0' + 52;
    if (ch == '+') return 62;
    if (ch == '/') return 63;
    return -1;
}

/* Decode a plist <data> body. Whitespace is stripped, the alphabet is
 * checked strictly, and padding is only allowed at the very end, so a hostile
 * plist cannot spell one block table two different ways. */
static bool xx_dmg_base64_decode(const char *text, uint64_t limit,
                                 uint8_t **out_data, size_t *out_size) {
    size_t length;
    size_t index;
    size_t symbols = 0U;
    size_t padding = 0U;
    uint8_t *data;
    size_t written = 0U;
    uint32_t accum = 0U;
    int accum_symbols = 0;
    if (!text || !out_data || !out_size) return false;
    *out_data = NULL;
    *out_size = 0U;
    length = xx_str_len(text);
    for (index = 0U; index < length; ++index) {
        char ch = text[index];
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') continue;
        if (ch == '=') {
            ++padding;
            if (padding > 2U) return false;
        } else {
            /* Padding may only be followed by more padding. */
            if (padding != 0U || xx_dmg_base64_value(ch) < 0) return false;
            ++symbols;
        }
    }
    symbols += padding;
    if (symbols == 0U || (symbols & 3U) != 0U) return false;
    if ((uint64_t)(symbols / 4U) * 3U - padding > limit) return false;
    data = (uint8_t *)xx_mem_alloc(symbols / 4U * 3U);
    if (!data) return false;
    for (index = 0U; index < length; ++index) {
        char ch = text[index];
        int value;
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') continue;
        if (ch == '=') break;
        value = xx_dmg_base64_value(ch);
        accum = (accum << 6) | (uint32_t)value;
        if (++accum_symbols == 4) {
            data[written++] = (uint8_t)(accum >> 16);
            data[written++] = (uint8_t)(accum >> 8);
            data[written++] = (uint8_t)accum;
            accum = 0U;
            accum_symbols = 0;
        }
    }
    if (padding == 1U) {
        /* Three symbols carry 18 bits and two bytes; the low two bits are
         * unused and must be zero for the spelling to be canonical. */
        if (accum_symbols != 3 || (accum & 3U) != 0U) {
            xx_mem_free(data);
            return false;
        }
        data[written++] = (uint8_t)(accum >> 10);
        data[written++] = (uint8_t)(accum >> 2);
    } else if (padding == 2U) {
        if (accum_symbols != 2 || (accum & 15U) != 0U) {
            xx_mem_free(data);
            return false;
        }
        data[written++] = (uint8_t)(accum >> 4);
    } else if (accum_symbols != 0) {
        xx_mem_free(data);
        return false;
    }
    *out_data = data;
    *out_size = written;
    return true;
}

/* ------------------------------------------------------------------------ */
/* Bookkeeping                                                               */
/* ------------------------------------------------------------------------ */

static void xx_dmg_private_cleanup(xx_dmg_private *parsed) {
    size_t index;
    if (!parsed) return;
    for (index = 0U; index < parsed->count; ++index) {
        if (parsed->partitions[index].name) {
            xx_str_free(parsed->partitions[index].name);
        }
    }
    if (parsed->partitions) xx_mem_free(parsed->partitions);
    if (parsed->runs) xx_mem_free(parsed->runs);
    xx_mem_zero(parsed, sizeof(*parsed));
    parsed->input_size = -1;
    parsed->koly_offset = -1;
    parsed->archive_end = -1;
}

static bool xx_dmg_append_run(xx_dmg_private *parsed, const xx_dmg_run *run) {
    xx_dmg_run *grown;
    size_t capacity;
    if (!parsed || !run || parsed->run_count >= XX_DMG_MAX_RUNS) return false;
    if (parsed->run_count == parsed->run_capacity) {
        capacity = parsed->run_capacity ? parsed->run_capacity * 2U : 64U;
        if (capacity < parsed->run_count ||
            capacity > SIZE_MAX / sizeof(*parsed->runs)) {
            return false;
        }
        grown = (xx_dmg_run *)xx_mem_realloc(parsed->runs,
                                             capacity * sizeof(*parsed->runs));
        if (!grown) return false;
        parsed->runs = grown;
        parsed->run_capacity = capacity;
    }
    parsed->runs[parsed->run_count++] = *run;
    return true;
}

/* Takes ownership of partition->name on success. */
static bool xx_dmg_append_partition(xx_dmg_private *parsed,
                                    xx_dmg_partition *partition) {
    xx_dmg_partition *grown;
    size_t capacity;
    if (!parsed || !partition || !partition->name ||
        parsed->count >= XX_DMG_MAX_PARTITIONS) {
        return false;
    }
    if (parsed->count == parsed->capacity) {
        capacity = parsed->capacity ? parsed->capacity * 2U : 16U;
        if (capacity < parsed->count ||
            capacity > SIZE_MAX / sizeof(*parsed->partitions)) {
            return false;
        }
        grown = (xx_dmg_partition *)xx_mem_realloc(
            parsed->partitions, capacity * sizeof(*parsed->partitions));
        if (!grown) return false;
        parsed->partitions = grown;
        parsed->capacity = capacity;
    }
    parsed->partitions[parsed->count++] = *partition;
    xx_mem_zero(partition, sizeof(*partition));
    return true;
}

/* ------------------------------------------------------------------------ */
/* Names                                                                     */
/* ------------------------------------------------------------------------ */

/* A blkx display name is free text - "Apple_HFS (Apple_HFS : 2)" is typical -
 * so it is folded down to a filename rather than used as one. Everything
 * outside the portable set becomes an underscore, which keeps the name
 * recognisable without letting a separator, a drive letter or a control byte
 * through. */
static char *xx_dmg_make_member_name(const char *display, size_t index) {
    char buffer[XX_DMG_MAX_NAME_SIZE];
    size_t used = 0U;
    size_t position;
    size_t value;
    bool has_alnum = false;
    if (display) {
        size_t length = xx_str_len(display);
        size_t cursor;
        for (cursor = 0U; cursor < length && used + 8U < sizeof(buffer);
             ++cursor) {
            unsigned char ch = (unsigned char)display[cursor];
            bool keep = (ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z') ||
                        (ch >= 'a' && ch <= 'z') || ch == '.' || ch == '-' ||
                        ch == '_';
            if (keep) {
                has_alnum = has_alnum || !(ch == '.' || ch == '-' || ch == '_');
                buffer[used++] = (char)ch;
            } else if (used != 0U && buffer[used - 1U] != '_') {
                buffer[used++] = '_';
            }
        }
        while (used != 0U && (buffer[used - 1U] == '_' ||
                              buffer[used - 1U] == '.')) {
            --used;
        }
    }
    if (used == 0U || !has_alnum) {
        /* Nothing usable in the display name, so fall back on the position
         * in the blkx array, which is always unique. */
        static const char prefix[] = "partition_";
        used = sizeof(prefix) - 1U;
        xx_rt_memcpy(buffer, prefix, used);
        value = index;
        position = used;
        do {
            buffer[position++] = (char)('0' + (value % 10U));
            value /= 10U;
        } while (value != 0U && position + 8U < sizeof(buffer));
        /* The digits went in backwards. */
        {
            size_t left = used;
            size_t right = position - 1U;
            while (left < right) {
                char swap = buffer[left];
                buffer[left++] = buffer[right];
                buffer[right--] = swap;
            }
        }
        used = position;
    }
    /* Every member is a raw partition image. */
    buffer[used++] = '.';
    buffer[used++] = 'i';
    buffer[used++] = 'm';
    buffer[used++] = 'g';
    buffer[used] = '\0';
    return xx_str_dup(buffer);
}

/* ------------------------------------------------------------------------ */
/* Block table parsing                                                       */
/* ------------------------------------------------------------------------ */

/* Validate one mish block table and append its runs.
 *
 * The rules are the reference's: every run's first sector must continue where
 * the previous one stopped, the control descriptors carry no sectors and no
 * bytes, the terminator is last, and the runs together must cover exactly the
 * sector count the header declares. */
static bool xx_dmg_parse_mish(Abstractformat *self, xx_dmg_private *parsed,
                              const uint8_t *data, size_t size,
                              const char *display_name) {
    xx_dmg_partition partition;
    uint32_t magic;
    uint32_t version;
    uint32_t run_count;
    uint64_t covered = 0U;
    uint64_t remaining_fork;
    uint32_t index;
    bool terminator_seen = false;
    size_t run_first = parsed->run_count;
    if (size < XX_DMG_MISH_HEADER_SIZE) return false;
    magic = xx_data_get_u32(data, size, 0U, true);
    version = xx_data_get_u32(data, size, 4U, true);
    run_count = xx_data_get_u32(data, size, 200U, true);
    if (magic != XX_DMG_MISH_MAGIC || version != 1U || run_count == 0U) {
        return false;
    }
    /* The table must be exactly as long as its descriptor count says. */
    if ((uint64_t)run_count > (XX_DMG_MAX_RUNS - parsed->run_count)) {
        return false;
    }
    if ((uint64_t)size != (uint64_t)XX_DMG_MISH_HEADER_SIZE +
                              (uint64_t)run_count * XX_DMG_RUN_SIZE) {
        return false;
    }
    xx_mem_zero(&partition, sizeof(partition));
    partition.start_sector = xx_data_get_u64(data, size, 8U, true);
    partition.sector_count = xx_data_get_u64(data, size, 16U, true);
    partition.data_offset = xx_data_get_u64(data, size, 24U, true);
    if (partition.sector_count >
            (uint64_t)INT64_MAX / XX_DMG_SECTOR_SIZE ||
        partition.start_sector > parsed->sector_count ||
        partition.sector_count > parsed->sector_count - partition.start_sector ||
        partition.data_offset > (uint64_t)parsed->data_fork_length) {
        return false;
    }
    remaining_fork = (uint64_t)parsed->data_fork_length - partition.data_offset;

    for (index = 0U; index < run_count; ++index) {
        size_t offset = (size_t)XX_DMG_MISH_HEADER_SIZE +
                        (size_t)index * XX_DMG_RUN_SIZE;
        xx_dmg_run run;
        uint64_t first_sector;
        uint64_t expected_output;
        xx_mem_zero(&run, sizeof(run));
        run.type = xx_data_get_u32(data, size, offset, true);
        first_sector = xx_data_get_u64(data, size, offset + 8U, true);
        run.sector_count = xx_data_get_u64(data, size, offset + 16U, true);
        run.data_offset = xx_data_get_u64(data, size, offset + 24U, true);
        run.data_length = xx_data_get_u64(data, size, offset + 32U, true);
        if (terminator_seen || first_sector != covered) return false;

        if (run.type == XX_DMG_RUN_COMMENT || run.type == XX_DMG_RUN_TERMINATOR) {
            /* hdiutil parks the running data-fork cursor in these two, so
             * only the sector and length fields have to be empty; the cursor
             * is still required to point inside the fork. */
            if (run.sector_count != 0U || run.data_length != 0U ||
                run.data_offset > remaining_fork) {
                return false;
            }
            if (run.type == XX_DMG_RUN_TERMINATOR) {
                if (index != run_count - 1U) return false;
                terminator_seen = true;
            }
        } else {
            bool zero_fill = (run.type == XX_DMG_RUN_ZEROFILL) ||
                             (run.type == XX_DMG_RUN_IGNORE);
            if ((run.sector_count == 0U && !zero_fill) ||
                run.sector_count > (uint64_t)INT64_MAX / XX_DMG_SECTOR_SIZE ||
                covered > partition.sector_count ||
                run.sector_count > partition.sector_count - covered) {
                return false;
            }
            expected_output = run.sector_count * XX_DMG_SECTOR_SIZE;
            switch (run.type) {
                case XX_DMG_RUN_ZEROFILL:
                case XX_DMG_RUN_IGNORE:
                    if (run.data_length != 0U) return false;
                    break;
                case XX_DMG_RUN_RAW:
                    if (run.data_length != expected_output) return false;
                    break;
                case XX_DMG_RUN_ADC:
                case XX_DMG_RUN_ZLIB:
                case XX_DMG_RUN_BZIP2:
                case XX_DMG_RUN_LZFSE:
                case XX_DMG_RUN_LZMA:
                    if (run.data_length == 0U) return false;
                    break;
                default: return false;
            }
            if (!zero_fill &&
                !xx_dmg_urange_within(run.data_offset, run.data_length,
                                      remaining_fork)) {
                return false;
            }
            covered += run.sector_count;
        }
        if (!xx_dmg_append_run(parsed, &run)) return false;
    }
    if (!terminator_seen || covered != partition.sector_count) return false;

    partition.run_first = run_first;
    partition.run_count = parsed->run_count - run_first;
    if (!xx_dmg_sectors_to_bytes(covered, &partition.expanded_size)) {
        return false;
    }
    /* The ceiling is applied here rather than only at extraction time: a
     * forty-byte zero-fill descriptor can declare petabytes of output, and
     * every route into the expander - including the direct
     * xx_dmg_unpack_partition_to_device() - goes through this parse, so a
     * partition past the ceiling is never even described. */
    if ((uint64_t)partition.expanded_size > xx_dmg_max_expanded(self, NULL)) {
        return false;
    }
    partition.name = xx_dmg_make_member_name(display_name, parsed->count);
    if (!partition.name) return false;
    if (!xx_dmg_append_partition(parsed, &partition)) {
        xx_str_free(partition.name);
        return false;
    }
    return true;
}

/* ------------------------------------------------------------------------ */
/* Property list                                                             */
/* ------------------------------------------------------------------------ */

/* Where the walk is in the plist. The structure being looked for is
 * plist -> dict -> key "resource-fork" -> dict -> key "blkx" -> array of
 * dicts, each with "Name"/"CFName" strings and a "Data" block table. */
typedef enum xx_dmg_plist_state_e {
    XX_DMG_PLIST_FIND_BLKX = 0, /**< Before the blkx key. */
    XX_DMG_PLIST_FIND_ARRAY,    /**< The key was seen; its array is next. */
    XX_DMG_PLIST_IN_ARRAY,      /**< Between partition dicts. */
    XX_DMG_PLIST_IN_PARTITION,  /**< Inside one partition dict. */
    XX_DMG_PLIST_DONE
} xx_dmg_plist_state;

/* Which element's text the cursor is about to land on. */
typedef enum xx_dmg_pending_e {
    XX_DMG_PENDING_NONE = 0,
    XX_DMG_PENDING_KEY,
    XX_DMG_PENDING_STRING,
    XX_DMG_PENDING_DATA
} xx_dmg_pending;

/* Which partition field the current <string> or <data> belongs to. */
#define XX_DMG_FIELD_NONE 0
#define XX_DMG_FIELD_NAME 1
#define XX_DMG_FIELD_CFNAME 2
#define XX_DMG_FIELD_DATA 3

static int xx_dmg_field_for_key(const char *key) {
    if (!key) return XX_DMG_FIELD_NONE;
    if (xx_str_cmp(key, "Name") == 0) return XX_DMG_FIELD_NAME;
    if (xx_str_cmp(key, "CFName") == 0) return XX_DMG_FIELD_CFNAME;
    if (xx_str_cmp(key, "Data") == 0) return XX_DMG_FIELD_DATA;
    return XX_DMG_FIELD_NONE;
}

/* Walk the property list and parse every blkx block table it names.
 *
 * The cursor reports a self-closing element without pushing it, so nesting is
 * tracked here rather than read back from the cursor. */
static bool xx_dmg_parse_plist(Abstractformat *self, xx_dmg_private *parsed,
                               const char *xml, size_t xml_size,
                               xx_pd_struct *pd) {
    xx_xml cursor;
    xx_dmg_plist_state state = XX_DMG_PLIST_FIND_BLKX;
    xx_dmg_pending pending = XX_DMG_PENDING_NONE;
    int level = 0;
    int array_level = -1;
    int partition_level = -1;
    int field = XX_DMG_FIELD_NONE;
    char *name = NULL;
    char *cfname = NULL;
    uint8_t *mish = NULL;
    size_t mish_size = 0U;
    bool result = false;
    xx_xml_init(&cursor, xml, xml_size);
    while (xx_xml_next(&cursor)) {
        xx_xml_type_t type = xx_xml_type(&cursor);
        const char *element = xx_xml_name(&cursor);
        if (pd && xx_pd_is_stopped(pd)) goto cleanup;
        if (type == XX_XML_START) {
            bool self_closing = cursor.self_closing;
            if (!self_closing) {
                ++level;
                if (level > XX_DMG_MAX_XML_DEPTH) goto cleanup;
            }
            pending = XX_DMG_PENDING_NONE;
            if (state == XX_DMG_PLIST_FIND_BLKX) {
                if (xx_str_cmp(element, "key") == 0 && !self_closing) {
                    pending = XX_DMG_PENDING_KEY;
                }
            } else if (state == XX_DMG_PLIST_FIND_ARRAY) {
                /* The value for the blkx key must be the array itself; an
                 * empty one ends the walk with no partitions. */
                if (xx_str_cmp(element, "array") != 0) goto cleanup;
                if (self_closing) {
                    state = XX_DMG_PLIST_DONE;
                    break;
                }
                state = XX_DMG_PLIST_IN_ARRAY;
                array_level = level;
            } else if (state == XX_DMG_PLIST_IN_ARRAY) {
                if (xx_str_cmp(element, "dict") != 0 || self_closing) {
                    goto cleanup;
                }
                state = XX_DMG_PLIST_IN_PARTITION;
                partition_level = level;
                field = XX_DMG_FIELD_NONE;
            } else if (state == XX_DMG_PLIST_IN_PARTITION) {
                /* Only the immediate children of the partition dict are
                 * read; anything nested deeper is ignored, not followed. */
                if (level == partition_level + 1 && !self_closing) {
                    if (xx_str_cmp(element, "key") == 0) {
                        pending = XX_DMG_PENDING_KEY;
                    } else if (xx_str_cmp(element, "string") == 0) {
                        pending = XX_DMG_PENDING_STRING;
                    } else if (xx_str_cmp(element, "data") == 0) {
                        pending = XX_DMG_PENDING_DATA;
                    }
                }
            }
            continue;
        }
        if (type == XX_XML_END) {
            --level;
            if (level < 0) goto cleanup;
            pending = XX_DMG_PENDING_NONE;
            if (state == XX_DMG_PLIST_IN_PARTITION && level < partition_level) {
                /* The partition dict closed: a table is required, a display
                 * name is not. */
                if (!mish) goto cleanup;
                if (!xx_dmg_parse_mish(self, parsed, mish, mish_size,
                                       name ? name : cfname)) {
                    goto cleanup;
                }
                if (name) xx_str_free(name);
                if (cfname) xx_str_free(cfname);
                xx_mem_free(mish);
                name = NULL;
                cfname = NULL;
                mish = NULL;
                mish_size = 0U;
                state = XX_DMG_PLIST_IN_ARRAY;
                partition_level = -1;
            } else if (state == XX_DMG_PLIST_IN_ARRAY && level < array_level) {
                state = XX_DMG_PLIST_DONE;
                break;
            }
            continue;
        }
        if (type != XX_XML_TEXT) {
            pending = XX_DMG_PENDING_NONE;
            continue;
        }
        if (pending == XX_DMG_PENDING_KEY) {
            const char *text = xx_xml_text(&cursor);
            if (state == XX_DMG_PLIST_FIND_BLKX) {
                if (text && xx_str_cmp(text, "blkx") == 0) {
                    state = XX_DMG_PLIST_FIND_ARRAY;
                }
            } else if (state == XX_DMG_PLIST_IN_PARTITION) {
                field = xx_dmg_field_for_key(text);
            }
        } else if (pending == XX_DMG_PENDING_STRING &&
                   state == XX_DMG_PLIST_IN_PARTITION) {
            const char *text = xx_xml_text(&cursor);
            if (field == XX_DMG_FIELD_NAME && !name && text) {
                name = xx_str_dup(text);
                if (!name) goto cleanup;
            } else if (field == XX_DMG_FIELD_CFNAME && !cfname && text) {
                cfname = xx_str_dup(text);
                if (!cfname) goto cleanup;
            }
        } else if (pending == XX_DMG_PENDING_DATA &&
                   state == XX_DMG_PLIST_IN_PARTITION &&
                   field == XX_DMG_FIELD_DATA && !mish) {
            if (!xx_dmg_base64_decode(xx_xml_text(&cursor),
                                      XX_DMG_MAX_MISH_SIZE, &mish,
                                      &mish_size)) {
                goto cleanup;
            }
        }
        pending = XX_DMG_PENDING_NONE;
    }
    /* A malformed document is refused outright; so is one that never
     * produced a blkx array. */
    result = !xx_xml_failed(&cursor) && state == XX_DMG_PLIST_DONE &&
             parsed->count != 0U;
cleanup:
    if (name) xx_str_free(name);
    if (cfname) xx_str_free(cfname);
    if (mish) xx_mem_free(mish);
    xx_xml_cleanup(&cursor);
    return result;
}

/* ------------------------------------------------------------------------ */
/* Trailer                                                                   */
/* ------------------------------------------------------------------------ */

/* Read and validate the koly trailer. The offsets the trailer carries are
 * relative to the archive base, not to the device. */
static bool xx_dmg_parse_koly(Abstractformat *self, xx_dmg_private *parsed) {
    uint8_t trailer[XX_DMG_KOLY_SIZE];
    uint64_t data_fork_offset;
    uint64_t data_fork_length;
    uint64_t xml_offset;
    uint64_t xml_length;
    uint64_t payload_limit;
    if (parsed->input_size < XX_DMG_KOLY_SIZE) return false;
    parsed->koly_offset = parsed->input_size - XX_DMG_KOLY_SIZE;
    if (parsed->koly_offset < self->base_address) return false;
    if (!xx_dmg_read_at(self->device, parsed->koly_offset, trailer,
                        sizeof(trailer))) {
        return false;
    }
    if (xx_data_get_u32(trailer, sizeof(trailer), 0U, true) !=
            XX_DMG_KOLY_MAGIC ||
        xx_data_get_u32(trailer, sizeof(trailer), 4U, true) != 4U ||
        xx_data_get_u32(trailer, sizeof(trailer), 8U, true) !=
            XX_DMG_KOLY_SIZE) {
        return false;
    }
    parsed->version = xx_data_get_u32(trailer, sizeof(trailer), 4U, true);
    parsed->flags = xx_data_get_u32(trailer, sizeof(trailer), 12U, true);
    data_fork_offset = xx_data_get_u64(trailer, sizeof(trailer), 24U, true);
    data_fork_length = xx_data_get_u64(trailer, sizeof(trailer), 32U, true);
    xml_offset = xx_data_get_u64(trailer, sizeof(trailer), 216U, true);
    xml_length = xx_data_get_u64(trailer, sizeof(trailer), 224U, true);
    parsed->image_variant = xx_data_get_u32(trailer, sizeof(trailer), 488U, true);
    parsed->sector_count = xx_data_get_u64(trailer, sizeof(trailer), 492U, true);

    /* Everything the trailer points at must sit between the archive base and
     * the trailer itself. */
    payload_limit = (uint64_t)(parsed->koly_offset - self->base_address);
    if (!xx_dmg_urange_within(data_fork_offset, data_fork_length,
                              payload_limit) ||
        !xx_dmg_urange_within(xml_offset, xml_length, payload_limit)) {
        return false;
    }
    if (xml_length == 0U || xml_length > XX_DMG_MAX_XML_SIZE) {
        /* A resource-fork-only image carries its block tables in the legacy
         * resource fork instead. That path is not implemented here, so such
         * an image is reported as not a DMG this reader can read rather than
         * being half-parsed. */
        return false;
    }
    if (parsed->sector_count > (uint64_t)INT64_MAX / XX_DMG_SECTOR_SIZE) {
        return false;
    }
    parsed->data_fork_offset = (int64_t)data_fork_offset;
    parsed->data_fork_length = (int64_t)data_fork_length;
    parsed->xml_offset = (int64_t)xml_offset;
    parsed->xml_length = (int64_t)xml_length;
    parsed->archive_end = parsed->koly_offset + XX_DMG_KOLY_SIZE;
    return true;
}

static bool xx_dmg_parse(Abstractformat *self, xx_dmg_private *parsed,
                         xx_pd_struct *pd) {
    char *xml = NULL;
    int64_t xml_start;
    bool result = false;
    /* Initialise before the guard clause: callers run the cleanup on their
     * stack copy whatever this returns. */
    if (parsed) {
        xx_mem_zero(parsed, sizeof(*parsed));
        parsed->input_size = -1;
        parsed->koly_offset = -1;
        parsed->archive_end = -1;
    }
    if (!self || !self->device || !parsed || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    parsed->input_size = xx_io_total_size(self->device);
    if (!xx_dmg_parse_koly(self, parsed)) goto fail;
    if (!xx_dmg_add(self->base_address, (uint64_t)parsed->xml_offset,
                    &xml_start) ||
        !xx_dmg_range_within(parsed->input_size, xml_start,
                             parsed->xml_length)) {
        goto fail;
    }
    /* The property list is read whole, which the 64 MiB ceiling above bounds;
     * one extra byte terminates it so the cursor can be handed text. */
    xml = (char *)xx_mem_alloc((size_t)parsed->xml_length + 1U);
    if (!xml || !xx_dmg_read_at(self->device, xml_start, xml,
                                (size_t)parsed->xml_length)) {
        goto fail;
    }
    xml[parsed->xml_length] = '\0';
    result = xx_dmg_parse_plist(self, parsed, xml, (size_t)parsed->xml_length,
                                pd);
fail:
    if (xml) xx_mem_free(xml);
    if (!result) xx_dmg_private_cleanup(parsed);
    return result;
}

/* ------------------------------------------------------------------------ */
/* Expansion                                                                 */
/* ------------------------------------------------------------------------ */

static bool xx_dmg_emit_zeros(xx_io_device *destination, int64_t size,
                              xx_pd_struct *pd) {
    uint8_t staging[XX_DMG_STAGING_SIZE];
    if (size < 0) return false;
    xx_rt_memset(staging, 0, sizeof(staging));
    while (size > 0) {
        size_t step = (size < (int64_t)sizeof(staging)) ? (size_t)size
                                                        : sizeof(staging);
        if ((pd && xx_pd_is_stopped(pd)) ||
            !xx_dmg_write_all(destination, staging, step)) {
            return false;
        }
        size -= (int64_t)step;
    }
    return true;
}

static bool xx_dmg_emit_raw(xx_io_device *source, int64_t offset,
                            xx_io_device *destination, int64_t size,
                            xx_pd_struct *pd) {
    uint8_t staging[XX_DMG_STAGING_SIZE];
    if (size < 0 || offset < 0) return false;
    if (size != 0 && xx_io_seek64(source, offset, SEEK_SET) != 0) return false;
    while (size > 0) {
        size_t step = (size < (int64_t)sizeof(staging)) ? (size_t)size
                                                        : sizeof(staging);
        size_t done = 0U;
        if (pd && xx_pd_is_stopped(pd)) return false;
        while (done < step) {
            ssize_t got = xx_io_read(source, staging + done, step - done);
            if (got <= 0 || (size_t)got > step - done) return false;
            done += (size_t)got;
        }
        if (!xx_dmg_write_all(destination, staging, step)) return false;
        size -= (int64_t)step;
    }
    return true;
}

/* Stage one compressed run in memory and decode it.
 *
 * Both sides are bounded before anything is allocated: the run's own declared
 * lengths are already inside the data fork, and the two ceilings here keep a
 * single run from standing in for the whole image. */
static bool xx_dmg_emit_compressed(xx_io_device *source, int64_t offset,
                                   int64_t input_size,
                                   xx_io_device *destination,
                                   int64_t output_size, uint32_t type,
                                   xx_pd_struct *pd) {
    uint8_t *input = NULL;
    uint8_t *output = NULL;
    size_t written = 0U;
    bool decoded = false;
    if (input_size <= 0 || output_size < 0 ||
        (uint64_t)input_size > XX_DMG_MAX_RUN_INPUT ||
        (uint64_t)output_size > XX_DMG_MAX_RUN_OUTPUT) {
        return false;
    }
    if (pd && xx_pd_is_stopped(pd)) return false;
    input = (uint8_t *)xx_mem_alloc((size_t)input_size);
    output = (uint8_t *)xx_mem_alloc(output_size ? (size_t)output_size : 1U);
    if (!input || !output ||
        !xx_dmg_read_at(source, offset, input, (size_t)input_size)) {
        goto cleanup;
    }
    switch (type) {
        case XX_DMG_RUN_ZLIB:
            /* hdiutil writes a complete RFC 1950 stream per run, header and
             * all, not the raw Deflate a negative-window caller would
             * produce, so the zlib wrapper is what is fed here. */
            decoded = xx_zlib_stream_decode_memory(input, (size_t)input_size,
                                                   output, (size_t)output_size,
                                                   &written);
            break;
        case XX_DMG_RUN_BZIP2:
            decoded = xx_bzip2_decompress_memory(input, (size_t)input_size,
                                                 output, (size_t)output_size,
                                                 &written);
            break;
        case XX_DMG_RUN_LZFSE:
            decoded = xx_lzfse_decompress_memory(input, (size_t)input_size,
                                                 output, (size_t)output_size,
                                                 &written);
            break;
        default:
            /* ADC and LZMA runs reach here. There is no decoder for either,
             * and writing zeros in their place would report a wrong image as
             * a correct one, so the expansion fails instead. */
            decoded = false;
            break;
    }
    if (!decoded || written != (size_t)output_size) {
        decoded = false;
        goto cleanup;
    }
    decoded = xx_dmg_write_all(destination, output, (size_t)output_size);
cleanup:
    if (input) xx_mem_free(input);
    if (output) xx_mem_free(output);
    return decoded;
}

static bool xx_dmg_expand(Abstractformat *self, const xx_dmg_private *parsed,
                          size_t index, xx_io_device *destination,
                          xx_pd_struct *pd) {
    const xx_dmg_partition *partition;
    int64_t partition_base;
    size_t cursor;
    if (!self || !self->device || !parsed || !destination ||
        index >= parsed->count) {
        return false;
    }
    partition = &parsed->partitions[index];
    /* Every run's data offset is relative to the partition's slice of the
     * data fork, which is itself relative to the archive base. */
    if (!xx_dmg_add(self->base_address, (uint64_t)parsed->data_fork_offset,
                    &partition_base) ||
        !xx_dmg_add(partition_base, partition->data_offset, &partition_base)) {
        return false;
    }
    for (cursor = 0U; cursor < partition->run_count; ++cursor) {
        const xx_dmg_run *run = &parsed->runs[partition->run_first + cursor];
        int64_t output_size;
        int64_t input_offset;
        if (pd && xx_pd_is_stopped(pd)) return false;
        if (run->type == XX_DMG_RUN_COMMENT ||
            run->type == XX_DMG_RUN_TERMINATOR) {
            continue;
        }
        if (!xx_dmg_sectors_to_bytes(run->sector_count, &output_size)) {
            return false;
        }
        if (run->type == XX_DMG_RUN_ZEROFILL || run->type == XX_DMG_RUN_IGNORE) {
            /* "Ignore" means the writer may leave those sectors alone; a
             * reassembled image has to put something there, and zero is what
             * every other UDIF reader writes. */
            if (!xx_dmg_emit_zeros(destination, output_size, pd)) return false;
            continue;
        }
        if (!xx_dmg_add(partition_base, run->data_offset, &input_offset) ||
            !xx_dmg_range_within(parsed->input_size, input_offset,
                                 (int64_t)run->data_length)) {
            return false;
        }
        if (run->type == XX_DMG_RUN_RAW) {
            if (!xx_dmg_emit_raw(self->device, input_offset, destination,
                                 output_size, pd)) {
                return false;
            }
            continue;
        }
        if (!xx_dmg_emit_compressed(self->device, input_offset,
                                    (int64_t)run->data_length, destination,
                                    output_size, run->type, pd)) {
            return false;
        }
    }
    return true;
}

/* ------------------------------------------------------------------------ */
/* Record plumbing                                                           */
/* ------------------------------------------------------------------------ */

static bool xx_dmg_copy_options(xx_list_s *destination,
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

static const xx_var *xx_dmg_find_option(const xx_list_s *options,
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

/* Resolve the expansion ceiling. An operation-specific value wins over a
 * format-wide one, and the built-in default applies when neither is set. */
static uint64_t xx_dmg_max_expanded(const Abstractformat *self,
                                    const xx_list_s *options) {
    const xx_var *limit =
        self ? xx_format_resolve_extra_parameter(self, options,
                                                 XX_META_ID_OPT_MAX_MEMBER_SIZE)
             : NULL;
    if (!limit) return XX_DMG_DEFAULT_MAX_EXPANDED;
    switch (limit->type) {
        case XX_VAR_TYPE_UINT8:
        case XX_VAR_TYPE_UINT16:
        case XX_VAR_TYPE_UINT32:
        case XX_VAR_TYPE_UINT64: return xx_var_get_u64(limit);
        case XX_VAR_TYPE_INT8:
        case XX_VAR_TYPE_INT16:
        case XX_VAR_TYPE_INT32:
        case XX_VAR_TYPE_INT64: {
            int64_t value = xx_var_get_i64(limit);
            return value >= 0 ? (uint64_t)value : XX_DMG_DEFAULT_MAX_EXPANDED;
        }
        default: return XX_DMG_DEFAULT_MAX_EXPANDED;
    }
}

/* The compressed size a partition reports is the sum of its runs' payloads,
 * which is what the member actually occupies in the data fork. */
static uint64_t xx_dmg_packed_size(const xx_dmg_private *parsed,
                                   const xx_dmg_partition *partition) {
    uint64_t total = 0U;
    size_t cursor;
    for (cursor = 0U; cursor < partition->run_count; ++cursor) {
        const xx_dmg_run *run = &parsed->runs[partition->run_first + cursor];
        if (run->type == XX_DMG_RUN_COMMENT ||
            run->type == XX_DMG_RUN_TERMINATOR) {
            continue;
        }
        if (run->data_length > UINT64_MAX - total) return total;
        total += run->data_length;
    }
    return total;
}

/* The method reported for the member as a whole: a partition may mix run
 * types, so the first compressed run it carries names it, and a partition
 * with none is reported as stored. */
static uint64_t xx_dmg_partition_method(const xx_dmg_private *parsed,
                                        const xx_dmg_partition *partition) {
    size_t cursor;
    for (cursor = 0U; cursor < partition->run_count; ++cursor) {
        uint32_t type = parsed->runs[partition->run_first + cursor].type;
        if (type != XX_DMG_RUN_ZEROFILL && type != XX_DMG_RUN_RAW &&
            type != XX_DMG_RUN_IGNORE && type != XX_DMG_RUN_COMMENT &&
            type != XX_DMG_RUN_TERMINATOR) {
            return type;
        }
    }
    return XX_DMG_RUN_RAW;
}

static bool xx_dmg_populate_record(xx_archive_record *record,
                                   const xx_dmg_private *parsed, size_t index,
                                   int64_t base_address) {
    const xx_dmg_partition *partition;
    int64_t data_offset;
    if (!record || !parsed || index >= parsed->count) return false;
    partition = &parsed->partitions[index];
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = parsed->koly_offset;
    record->header_size = XX_DMG_KOLY_SIZE;
    /* The member has no single contiguous payload - it is a set of runs
     * scattered through the data fork - so the record points at where the
     * partition's slice of that fork begins. */
    if (!xx_dmg_add(base_address, (uint64_t)parsed->data_fork_offset,
                    &data_offset) ||
        !xx_dmg_add(data_offset, partition->data_offset, &data_offset)) {
        return false;
    }
    record->data_offset = data_offset;
    record->compressed_size = (int64_t)xx_dmg_packed_size(parsed, partition);
    return xx_archive_record_set_original_name(record, partition->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)partition->expanded_size) &&
           xx_archive_record_set_meta_u64(
               record, XX_META_ID_COMPRESSED_SIZE,
               (uint64_t)record->compressed_size) &&
           xx_archive_record_set_meta_u64(
               record, XX_META_ID_COMPRESSION_METHOD,
               xx_dmg_partition_method(parsed, partition)) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

static void xx_dmg_archive_stream_free(void *pointer) {
    xx_dmg_archive_stream *stream = (xx_dmg_archive_stream *)pointer;
    if (!stream) return;
    xx_dmg_private_cleanup(&stream->parsed);
    xx_mem_free(stream);
}

/* ------------------------------------------------------------------------ */
/* Public interface                                                          */
/* ------------------------------------------------------------------------ */

void xx_dmg_init(xx_dmg *dmg, xx_io_device *dev, int64_t base_address) {
    if (!dmg) return;
    xx_mem_zero(dmg, sizeof(*dmg));
    xx_format_init(&dmg->format, dev, base_address);
    dmg->format.endian = XX_ENDIAN_BIG;
    dmg->format.file_type = XX_DMG_FILE_TYPE;
    dmg->format.format_type = XX_TYPE_ARCHIVE;
    dmg->format.is_archive = true;
    xx_format_set_mime_type(&dmg->format, "application/x-apple-diskimage");
    xx_format_set_extension(&dmg->format, "dmg");
    dmg->format.check_is_valid = xx_dmg_check_is_valid;
    dmg->format.handle_base_info = xx_dmg_handle_base_info;
    dmg->format.get_format_size = xx_dmg_get_format_size;
    dmg->format.get_number_of_archive_records =
        xx_dmg_get_number_of_archive_records;
    dmg->format.create_archive_records_reading =
        xx_dmg_create_archive_records_reading;
    dmg->format.get_current_archive_record = xx_dmg_get_current_archive_record;
    dmg->format.unpack_current_archive_record =
        xx_dmg_unpack_current_archive_record;
    dmg->format.archive_record_move_to_next = xx_dmg_archive_record_move_to_next;
    dmg->format.free_archive_records_reading =
        xx_dmg_free_archive_records_reading;
    dmg->format.destroy = xx_dmg_vtable_destroy;
    dmg->koly_offset = -1;
    dmg->archive_end = -1;
}

xx_dmg *xx_dmg_create(xx_io_device *dev, int64_t base_address) {
    xx_dmg *dmg = (xx_dmg *)xx_mem_alloc(sizeof(*dmg));
    if (dmg) xx_dmg_init(dmg, dev, base_address);
    return dmg;
}

void xx_dmg_destroy(xx_dmg *dmg) {
    if (!dmg) return;
    if (dmg->internal) {
        xx_dmg_private_cleanup((xx_dmg_private *)dmg->internal);
        xx_mem_free(dmg->internal);
        dmg->internal = NULL;
    }
    xx_format_cleanup_extra_parameters(&dmg->format);
}

static void xx_dmg_vtable_destroy(Abstractformat *self) {
    xx_dmg_destroy((xx_dmg *)self);
}

void xx_dmg_free(xx_dmg *dmg) {
    if (!dmg) return;
    xx_dmg_destroy(dmg);
    xx_mem_free(dmg);
}

bool xx_dmg_probe_device(xx_io_device *dev, int64_t base_address) {
    uint8_t trailer[16];
    int64_t total_size;
    int64_t offset;
    if (!dev || base_address < 0) return false;
    total_size = xx_io_total_size(dev);
    if (total_size < XX_DMG_KOLY_SIZE) return false;
    offset = total_size - XX_DMG_KOLY_SIZE;
    if (offset < base_address) return false;
    if (!xx_dmg_read_at(dev, offset, trailer, sizeof(trailer))) return false;
    return xx_data_get_u32(trailer, sizeof(trailer), 0U, true) ==
               XX_DMG_KOLY_MAGIC &&
           xx_data_get_u32(trailer, sizeof(trailer), 4U, true) == 4U &&
           xx_data_get_u32(trailer, sizeof(trailer), 8U, true) ==
               XX_DMG_KOLY_SIZE;
}

bool xx_dmg_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_dmg_private parsed;
    bool result;
    /* The cheap probe first: the trailer's magic is at the end of the file,
     * so nothing a prefilter has already read can stand in for it. */
    if (!self || !xx_dmg_probe_device(self->device, self->base_address)) {
        return false;
    }
    result = xx_dmg_parse(self, &parsed, pd);
    xx_dmg_private_cleanup(&parsed);
    return result;
}

bool xx_dmg_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_dmg_private *parsed;
    xx_dmg *dmg = (xx_dmg *)self;
    int64_t total_size;
    if (!self || !dmg) return false;
    parsed = (xx_dmg_private *)xx_mem_alloc(sizeof(*parsed));
    if (!parsed || !xx_dmg_parse(self, parsed, pd)) {
        if (parsed) xx_mem_free(parsed);
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    if (dmg->internal) {
        xx_dmg_private_cleanup((xx_dmg_private *)dmg->internal);
        xx_mem_free(dmg->internal);
    }
    dmg->internal = parsed;
    dmg->number_of_records = parsed->count;
    dmg->number_of_members = parsed->count;
    dmg->sector_count = parsed->sector_count;
    dmg->version = parsed->version;
    dmg->flags = parsed->flags;
    dmg->image_variant = parsed->image_variant;
    dmg->koly_offset = parsed->koly_offset;
    dmg->data_fork_offset = parsed->data_fork_offset;
    dmg->data_fork_length = parsed->data_fork_length;
    dmg->xml_offset = parsed->xml_offset;
    dmg->xml_length = parsed->xml_length;
    dmg->archive_end = parsed->archive_end;
    self->format_size = parsed->archive_end - self->base_address;
    total_size = xx_io_total_size(self->device);
    if (total_size > parsed->archive_end) {
        self->overlay_offset = parsed->archive_end;
        self->overlay_size = total_size - parsed->archive_end;
    } else {
        self->overlay_offset = -1;
        self->overlay_size = 0;
    }
    self->number_of_archive_records = parsed->count;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_dmg_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return -1;
    }
    return self->format_size;
}

uint64_t xx_dmg_get_number_of_archive_records(Abstractformat *self,
                                              xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return ((xx_dmg *)self)->number_of_records;
}

bool xx_dmg_unpack_partition_to_device(xx_dmg *dmg, size_t index,
                                       xx_io_device *destination,
                                       xx_pd_struct *pd) {
    xx_dmg_private parsed;
    bool result;
    if (!dmg || !destination) return false;
    result = xx_dmg_parse(&dmg->format, &parsed, pd) &&
             xx_dmg_expand(&dmg->format, &parsed, index, destination, pd);
    xx_dmg_private_cleanup(&parsed);
    return result;
}

xx_archive_record_state *xx_dmg_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_record_state *state;
    xx_dmg_archive_stream *stream;
    if (!self || !self->device ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    stream = (xx_dmg_archive_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!state || !stream) {
        if (state) xx_mem_free(state);
        if (stream) xx_mem_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    if (!xx_dmg_copy_options(&state->options, options) ||
        !xx_dmg_parse(self, &stream->parsed, pd)) {
        xx_dmg_archive_stream_free(stream);
        xx_archive_record_state_free(state);
        return NULL;
    }
    stream->index = 0U;
    state->internal_state = stream;
    state->free_internal = xx_dmg_archive_stream_free;
    state->total_records = (int64_t)stream->parsed.count;
    if (stream->parsed.count != 0U &&
        xx_dmg_populate_record(&state->current_record, &stream->parsed, 0U,
                               self->base_address)) {
        state->has_record = true;
        state->current_index = 0;
    }
    return state;
}

const xx_archive_record *xx_dmg_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_dmg_archive_record_move_to_next(Abstractformat *self,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    xx_dmg_archive_stream *stream;
    if (!self || !state || state->format != self || !state->has_record ||
        !state->internal_state || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_dmg_archive_stream *)state->internal_state;
    ++stream->index;
    if (stream->index >= stream->parsed.count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    if (!xx_dmg_populate_record(&state->current_record, &stream->parsed,
                                stream->index, self->base_address)) {
        state->has_record = false;
        return false;
    }
    ++state->current_index;
    return true;
}

/* A DMG member is reassembled from its runs, not copied, so extraction goes
 * through the expander rather than xx_store_unpack_device_to_file(). */
bool xx_dmg_unpack_current_archive_record(Abstractformat *self,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    xx_dmg_archive_stream *stream;
    const xx_dmg_partition *partition;
    const xx_var *option;
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
    stream = (xx_dmg_archive_stream *)state->internal_state;
    if (stream->index >= stream->parsed.count) return false;
    partition = &stream->parsed.partitions[stream->index];
    /* A ceiling supplied with this read session is honoured here; the parse
     * itself allocates nothing proportional to the expansion. */
    if (partition->expanded_size < 0 ||
        (uint64_t)partition->expanded_size >
            xx_dmg_max_expanded(self, &state->options)) {
        return false;
    }

    option = xx_dmg_find_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!option) {
        /* No destination: report whether the container is addressable. */
        return stream->parsed.archive_end >= 0 &&
               stream->parsed.archive_end <= stream->parsed.input_size;
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
        destination_path = xx_str_concat3(base, "/", partition->name);
    } else {
        destination_path = xx_str_concat(base, partition->name);
    }
    if (!destination_path) goto cleanup;
    if (!xx_store_create_dirs_a(destination_path, false)) goto cleanup;
    destination = xx_io_file_open(destination_path, "wb");
    if (!destination) goto cleanup;
    result = xx_dmg_expand(self, &stream->parsed, stream->index, destination,
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

void xx_dmg_free_archive_records_reading(Abstractformat *self,
                                         xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

uint64_t xx_dmg_get_number_of_records(const xx_dmg *dmg) {
    return dmg ? dmg->number_of_records : 0U;
}
uint64_t xx_dmg_get_number_of_members(const xx_dmg *dmg) {
    return dmg ? dmg->number_of_members : 0U;
}
uint64_t xx_dmg_get_sector_count(const xx_dmg *dmg) {
    return dmg ? dmg->sector_count : 0U;
}
int64_t xx_dmg_get_data_fork_length(const xx_dmg *dmg) {
    return dmg ? dmg->data_fork_length : -1;
}
int64_t xx_dmg_get_xml_length(const xx_dmg *dmg) {
    return dmg ? dmg->xml_length : -1;
}
int64_t xx_dmg_get_archive_end(const xx_dmg *dmg) {
    return dmg ? dmg->archive_end : -1;
}
