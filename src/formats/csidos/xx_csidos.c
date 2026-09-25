/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * CSI-DOS floppy images for the Soviet Elektronika BK-0011M (.IMG / .BKD).
 *
 * The image is a flat array of 512-byte blocks.  Blocks 0 and 1 hold the
 * bootstrap, blocks 2..9 hold the catalogue and the payload starts at
 * block 10.  Each catalogue block opens with a 12-byte header whose first
 * word repeats the block's own number; block 2 additionally carries the
 * volume block count and the three 0xa653 signature words.  The remaining
 * 500 bytes are exactly 25 packed 20-byte entries:
 *
 *   +0x00  u16   parent directory identifier (low byte; 0x01 is the root,
 *                0xff marks an erased entry, other unknown values are
 *                treated as the root - 0xc7 is CSIDOS3.EXE's own marker)
 *   +0x02  char  name[8],   space/NUL padded
 *   +0x0a  char  extension[3], space/NUL padded, all-NUL for a directory
 *   +0x0d  u8    attributes; on a directory this is its own identifier
 *   +0x0e  u16   first block of the payload
 *   +0x10  u8    unused by this reader (load address low byte)
 *   +0x11  u8    unused by this reader (load address high byte)
 *   +0x12  u16   length in bytes
 *
 * A slot whose name starts with NUL is free and is skipped, not treated as
 * a terminator: live entries routinely follow erased ones.  Names are
 * KOI8-R byte strings and are converted to UTF-8 like the sibling AO-DOS
 * reader does.  Nothing is compressed; every member is stored.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/csidos/xx_csidos.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#ifdef CSIDOS
#define XX_CSIDOS_FILE_TYPE XX_FILE_TYPE_CSIDOS
#else
#define XX_CSIDOS_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define CSIDOS_BLOCK_SIZE 512U
#define CSIDOS_CATALOGUE_FIRST_BLOCK 2U
#define CSIDOS_CATALOGUE_MAX_BLOCKS 8U
#define CSIDOS_BLOCK_HEADER_SIZE 12U
#define CSIDOS_ENTRY_SIZE 20U
#define CSIDOS_ENTRIES_PER_BLOCK 25U
#define CSIDOS_MAX_ENTRIES \
    (CSIDOS_CATALOGUE_MAX_BLOCKS * CSIDOS_ENTRIES_PER_BLOCK)
#define CSIDOS_FIRST_DATA_BLOCK 10U
#define CSIDOS_SIGNATURE 0xa653U
#define CSIDOS_NAME_SIZE 8U
#define CSIDOS_EXTENSION_SIZE 3U
#define CSIDOS_ROOT_ID 0x01U
#define CSIDOS_ERASED_ID 0xffU
#define CSIDOS_MIN_SIZE ((CSIDOS_FIRST_DATA_BLOCK + 1U) * CSIDOS_BLOCK_SIZE)
#define CSIDOS_MAX_SIZE UINT32_C(0x1000000)
#define CSIDOS_MAX_DEPTH 16U
#define CSIDOS_MAX_COLLISIONS 1024U

typedef struct csidos_raw_s {
    uint16_t parent;
    uint16_t start_block;
    uint16_t byte_size;
    uint8_t attributes;
    bool folder;
    bool owns_identifier;
    int64_t entry_offset;
    uint8_t name[CSIDOS_NAME_SIZE];
    uint8_t extension[CSIDOS_EXTENSION_SIZE];
} csidos_raw;

typedef struct csidos_member_s {
    char *name;
    int64_t entry_offset;
    int64_t data_offset;
    uint16_t byte_size;
    uint8_t attributes;
    bool folder;
} csidos_member;

typedef struct csidos_stream_s {
    csidos_member *items;
    size_t count;
    size_t index;
    int64_t image_size;
} csidos_stream;

typedef struct csidos_tree_s {
    char *leaf[256];
    char *path[256];
    uint8_t parent[256];
    bool resolving[256];
    char **used;
    size_t used_count;
} csidos_tree;

/* KOI8-R's upper half, kept as Unicode scalar values before UTF-8 emission. */
static const uint16_t csidos_koi8r_high[128] = {
    0x2500,0x2502,0x250c,0x2510,0x2514,0x2518,0x251c,0x2524,
    0x252c,0x2534,0x253c,0x2580,0x2584,0x2588,0x258c,0x2590,
    0x2591,0x2592,0x2593,0x2320,0x25a0,0x2219,0x221a,0x2248,
    0x2264,0x2265,0x00a0,0x2321,0x00b0,0x00b2,0x00b7,0x00f7,
    0x2550,0x2551,0x2552,0x0451,0x2553,0x2554,0x2555,0x2556,
    0x2557,0x2558,0x2559,0x255a,0x255b,0x255c,0x255d,0x255e,
    0x255f,0x2560,0x2561,0x0401,0x2562,0x2563,0x2564,0x2565,
    0x2566,0x2567,0x2568,0x2569,0x256a,0x256b,0x256c,0x00a9,
    0x044e,0x0430,0x0431,0x0446,0x0434,0x0435,0x0444,0x0433,
    0x0445,0x0438,0x0439,0x043a,0x043b,0x043c,0x043d,0x043e,
    0x043f,0x044f,0x0440,0x0441,0x0442,0x0443,0x0436,0x0432,
    0x044c,0x044b,0x0437,0x0448,0x044d,0x0449,0x0447,0x044a,
    0x042e,0x0410,0x0411,0x0426,0x0414,0x0415,0x0424,0x0413,
    0x0425,0x0418,0x0419,0x041a,0x041b,0x041c,0x041d,0x041e,
    0x041f,0x042f,0x0420,0x0421,0x0422,0x0423,0x0416,0x0412,
    0x042c,0x042b,0x0417,0x0428,0x042d,0x0429,0x0427,0x042a
};

static uint16_t csidos_le16(const uint8_t *bytes) {
    return (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U);
}

static bool csidos_read_at(xx_io_device *device, int64_t offset, void *buffer,
                           size_t size) {
    size_t done = 0U;
    if (!device || (!buffer && size != 0U) || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0)
        return false;
    while (done < size) {
        ssize_t amount = xx_io_read(device, (uint8_t *)buffer + done,
                                    size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

static char *csidos_copy_string(const char *source) {
    size_t size;
    char *result;
    if (!source || (size = xx_str_len(source)) == SIZE_MAX) return NULL;
    result = (char *)xx_mem_alloc(size + 1U);
    if (!result) return NULL;
    xx_rt_memcpy(result, source, size + 1U);
    return result;
}

static bool csidos_append_utf8(char *output, size_t capacity, size_t *at,
                               uint16_t codepoint) {
    if (!output || !at) return false;
    if (codepoint < 0x80U) {
        if (*at >= capacity) return false;
        output[(*at)++] = (char)codepoint;
    } else if (codepoint < 0x800U) {
        if (capacity - *at < 2U) return false;
        output[(*at)++] = (char)(0xc0U | (codepoint >> 6U));
        output[(*at)++] = (char)(0x80U | (codepoint & 0x3fU));
    } else {
        if (capacity - *at < 3U) return false;
        output[(*at)++] = (char)(0xe0U | (codepoint >> 12U));
        output[(*at)++] = (char)(0x80U | ((codepoint >> 6U) & 0x3fU));
        output[(*at)++] = (char)(0x80U | (codepoint & 0x3fU));
    }
    return true;
}

static bool csidos_reserved_name(const char *name) {
    char stem[16];
    size_t at = 0U;
    if (!name) return false;
    while (name[at] && name[at] != '.' && at + 1U < sizeof(stem)) {
        unsigned char value = (unsigned char)name[at];
        stem[at] = (char)(value >= 'a' && value <= 'z' ? value - 32U : value);
        ++at;
    }
    stem[at] = 0;
    if (xx_rt_strcmp(stem, "AUX") == 0 || xx_rt_strcmp(stem, "CON") == 0 ||
        xx_rt_strcmp(stem, "NUL") == 0 || xx_rt_strcmp(stem, "PRN") == 0 ||
        xx_rt_strcmp(stem, "CLOCK$") == 0) return true;
    return at == 4U && (xx_rt_memcmp(stem, "COM", 3U) == 0 ||
                        xx_rt_memcmp(stem, "LPT", 3U) == 0) &&
           stem[3] >= '1' && stem[3] <= '9';
}

/* Strip the space/NUL padding CSI-DOS uses on both sides of a field. */
static size_t csidos_trim(const uint8_t *bytes, size_t size, size_t *start) {
    size_t first = 0U, last = size;
    while (first < last && (bytes[first] == ' ' || bytes[first] == 0U)) ++first;
    while (last > first && (bytes[last - 1U] == ' ' || bytes[last - 1U] == 0U))
        --last;
    *start = first;
    return last - first;
}

/* Build "name.ext" from one catalogue entry, mapping KOI8-R to UTF-8 and
 * neutralising anything that would escape a single path component. */
static char *csidos_component(const csidos_raw *raw) {
    uint8_t packed[CSIDOS_NAME_SIZE + CSIDOS_EXTENSION_SIZE + 1U];
    char *result;
    size_t name_start, extension_start, name_size, extension_size;
    size_t packed_size = 0U, index, output = 0U, capacity;
    if (!raw) return NULL;
    name_size = csidos_trim(raw->name, CSIDOS_NAME_SIZE, &name_start);
    extension_size = csidos_trim(raw->extension, CSIDOS_EXTENSION_SIZE,
                                 &extension_start);
    /* "CSIDOS3." plus "EXE" is spelled with the separator already in the
     * name field; keep exactly one. */
    if (name_size != 0U && raw->name[name_start + name_size - 1U] == '.')
        --name_size;
    if (name_size == 0U) return NULL;
    for (index = 0U; index < name_size; ++index)
        packed[packed_size++] = raw->name[name_start + index];
    if (extension_size != 0U) {
        packed[packed_size++] = '.';
        for (index = 0U; index < extension_size; ++index)
            packed[packed_size++] = raw->extension[extension_start + index];
    }
    capacity = packed_size * 3U + 1U;
    result = (char *)xx_mem_alloc(capacity + 1U);
    if (!result) return NULL;
    for (index = 0U; index < packed_size; ++index) {
        uint16_t codepoint = packed[index] < 0x80U
                                 ? packed[index]
                                 : csidos_koi8r_high[packed[index] - 0x80U];
        if (codepoint < 0x20U || codepoint == '"' || codepoint == '*' ||
            codepoint == ':' || codepoint == '<' || codepoint == '>' ||
            codepoint == '?' || codepoint == '|' || codepoint == '/' ||
            codepoint == '\\') codepoint = '_';
        if (!csidos_append_utf8(result, capacity, &output, codepoint)) {
            xx_mem_free(result);
            return NULL;
        }
    }
    while (output != 0U &&
           (result[output - 1U] == ' ' || result[output - 1U] == '.')) --output;
    if (output == 0U) result[output++] = '_';
    result[output] = 0;
    if (csidos_reserved_name(result)) {
        xx_rt_memmove(result + 1U, result, output + 1U);
        result[0] = '_';
    }
    return result;
}

static bool csidos_path_equal(const char *first, const char *second) {
    size_t index = 0U;
    if (!first || !second) return false;
    while (first[index] && second[index]) {
        unsigned char a = (unsigned char)first[index];
        unsigned char b = (unsigned char)second[index];
        if (a >= 'A' && a <= 'Z') a += 'a' - 'A';
        if (b >= 'A' && b <= 'Z') b += 'a' - 'A';
        if (a != b) return false;
        ++index;
    }
    return first[index] == second[index];
}

static char *csidos_join(const char *parent, const char *leaf) {
    size_t parent_size, leaf_size;
    char *result;
    if (!parent || !leaf) return NULL;
    parent_size = xx_str_len(parent);
    leaf_size = xx_str_len(leaf);
    if (leaf_size > SIZE_MAX - parent_size - (parent_size != 0U ? 2U : 1U))
        return NULL;
    result = (char *)xx_mem_alloc(parent_size + leaf_size +
                                  (parent_size != 0U ? 2U : 1U));
    if (!result) return NULL;
    if (parent_size != 0U) {
        xx_rt_memcpy(result, parent, parent_size);
        result[parent_size] = '/';
        xx_rt_memcpy(result + parent_size + 1U, leaf, leaf_size + 1U);
    } else {
        xx_rt_memcpy(result, leaf, leaf_size + 1U);
    }
    return result;
}

static char *csidos_suffix(const char *leaf, unsigned suffix) {
    char suffix_text[16];
    const char *dot;
    size_t leaf_size, suffix_size, before, after;
    char *result;
    if (!leaf || suffix == 0U ||
        xx_rt_snprintf(suffix_text, sizeof(suffix_text), "~%u", suffix) < 0)
        return NULL;
    leaf_size = xx_str_len(leaf);
    suffix_size = xx_str_len(suffix_text);
    dot = xx_rt_strrchr(leaf, '.');
    if (dot && dot != leaf && leaf_size - (size_t)(dot - leaf) <= 32U) {
        before = (size_t)(dot - leaf);
        after = leaf_size - before;
    } else {
        before = leaf_size;
        after = 0U;
    }
    if (suffix_size > SIZE_MAX - leaf_size - 1U) return NULL;
    result = (char *)xx_mem_alloc(leaf_size + suffix_size + 1U);
    if (!result) return NULL;
    xx_rt_memcpy(result, leaf, before);
    xx_rt_memcpy(result + before, suffix_text, suffix_size);
    if (after != 0U)
        xx_rt_memcpy(result + before + suffix_size, leaf + before, after);
    result[leaf_size + suffix_size] = 0;
    return result;
}

static void csidos_tree_cleanup(csidos_tree *tree) {
    size_t index;
    if (!tree) return;
    for (index = 0U; index < 256U; ++index) {
        if (tree->leaf[index]) xx_mem_free(tree->leaf[index]);
        if (tree->path[index]) xx_mem_free(tree->path[index]);
    }
    for (index = 0U; index < tree->used_count; ++index)
        if (tree->used[index]) xx_mem_free(tree->used[index]);
    if (tree->used) xx_mem_free(tree->used);
    xx_mem_zero(tree, sizeof(*tree));
}

static bool csidos_tree_claim(csidos_tree *tree, const char *parent,
                              const char *leaf, char **result) {
    unsigned suffix;
    if (!tree || !parent || !leaf || !result) return false;
    for (suffix = 1U; suffix <= CSIDOS_MAX_COLLISIONS; ++suffix) {
        char *candidate_leaf = suffix == 1U
                                   ? csidos_copy_string(leaf)
                                   : csidos_suffix(leaf, suffix - 1U);
        char *candidate;
        char **grown;
        char *stored;
        size_t index;
        bool used = false;
        if (!candidate_leaf) return false;
        candidate = csidos_join(parent, candidate_leaf);
        xx_mem_free(candidate_leaf);
        if (!candidate) return false;
        for (index = 0U; index < tree->used_count; ++index) {
            if (csidos_path_equal(tree->used[index], candidate)) {
                used = true;
                break;
            }
        }
        if (used) {
            xx_mem_free(candidate);
            continue;
        }
        if (tree->used_count > SIZE_MAX / sizeof(*tree->used) - 1U) {
            xx_mem_free(candidate);
            return false;
        }
        grown = (char **)xx_mem_realloc(tree->used,
                                        (tree->used_count + 1U) *
                                            sizeof(*grown));
        if (!grown) {
            xx_mem_free(candidate);
            return false;
        }
        tree->used = grown;
        stored = csidos_copy_string(candidate);
        if (!stored) {
            xx_mem_free(candidate);
            return false;
        }
        tree->used[tree->used_count++] = stored;
        *result = candidate;
        return true;
    }
    return false;
}

/* The root is identifier 1.  Any identifier with no live directory entry -
 * 0xc7 on CSIDOS3.EXE, 0xca on a directory left over from a rename - is
 * resolved to the root, which is what CSI-DOS itself displays. */
static bool csidos_tree_resolve(csidos_tree *tree, unsigned identifier,
                                unsigned depth, const char **result) {
    const char *parent_path;
    char *path;
    if (!tree || !result || depth > CSIDOS_MAX_DEPTH) return false;
    if (identifier > 0xffU || identifier == CSIDOS_ROOT_ID ||
        identifier == CSIDOS_ERASED_ID || !tree->leaf[identifier]) {
        *result = "";
        return true;
    }
    if (tree->path[identifier]) {
        *result = tree->path[identifier];
        return true;
    }
    if (tree->resolving[identifier]) return false;
    tree->resolving[identifier] = true;
    if (!csidos_tree_resolve(tree, tree->parent[identifier], depth + 1U,
                             &parent_path) ||
        !csidos_tree_claim(tree, parent_path, tree->leaf[identifier], &path)) {
        tree->resolving[identifier] = false;
        return false;
    }
    tree->resolving[identifier] = false;
    tree->path[identifier] = path;
    *result = path;
    return true;
}

static void csidos_stream_free(void *opaque) {
    csidos_stream *stream = (csidos_stream *)opaque;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index)
        if (stream->items[index].name) xx_mem_free(stream->items[index].name);
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool csidos_add_member(csidos_stream *stream,
                              const csidos_member *member) {
    csidos_member *grown;
    if (!stream || !member || stream->count >= CSIDOS_MAX_ENTRIES ||
        stream->count > SIZE_MAX / sizeof(*grown) - 1U) return false;
    grown = (csidos_member *)xx_mem_realloc(stream->items,
                                            (stream->count + 1U) *
                                                sizeof(*grown));
    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}

static bool csidos_all_zero(const uint8_t *bytes, size_t size) {
    size_t index;
    for (index = 0U; index < size; ++index)
        if (bytes[index] != 0U) return false;
    return true;
}

static bool csidos_parse(Abstractformat *format, csidos_stream **result) {
    uint8_t block[CSIDOS_BLOCK_SIZE];
    csidos_raw *raw = NULL;
    csidos_tree tree;
    csidos_stream *stream = NULL;
    int64_t total, size, total_blocks, declared_blocks;
    size_t raw_count = 0U, index;
    unsigned catalogue_block;
    bool has_file = false;
    xx_mem_zero(&tree, sizeof(tree));
    if (!format || !format->device || !result || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size < (int64_t)CSIDOS_MIN_SIZE || size > (int64_t)CSIDOS_MAX_SIZE ||
        size % (int64_t)CSIDOS_BLOCK_SIZE != 0) return false;
    total_blocks = size / (int64_t)CSIDOS_BLOCK_SIZE;
    if (!csidos_read_at(format->device,
                        format->base_address +
                            (int64_t)CSIDOS_CATALOGUE_FIRST_BLOCK *
                                CSIDOS_BLOCK_SIZE,
                        block, sizeof(block))) return false;
    /* The catalogue root block identifies itself, repeats the signature word
     * three times and declares the volume's block count. */
    declared_blocks = (int64_t)csidos_le16(block + 2U);
    if (csidos_le16(block) != CSIDOS_CATALOGUE_FIRST_BLOCK ||
        csidos_le16(block + 4U) != CSIDOS_SIGNATURE ||
        csidos_le16(block + 6U) != CSIDOS_SIGNATURE ||
        block[9] != (uint8_t)(CSIDOS_SIGNATURE >> 8U) ||
        csidos_le16(block + 10U) != 0U ||
        declared_blocks <= (int64_t)CSIDOS_FIRST_DATA_BLOCK ||
        declared_blocks > total_blocks) return false;
    raw = (csidos_raw *)xx_mem_calloc(CSIDOS_MAX_ENTRIES, sizeof(*raw));
    stream = (csidos_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!raw || !stream) goto fail;
    for (catalogue_block = CSIDOS_CATALOGUE_FIRST_BLOCK;
         catalogue_block < CSIDOS_CATALOGUE_FIRST_BLOCK +
                               CSIDOS_CATALOGUE_MAX_BLOCKS;
         ++catalogue_block) {
        int64_t block_offset = (int64_t)catalogue_block * CSIDOS_BLOCK_SIZE;
        size_t slot;
        if (block_offset + (int64_t)CSIDOS_BLOCK_SIZE > size) break;
        if (catalogue_block != CSIDOS_CATALOGUE_FIRST_BLOCK &&
            (!csidos_read_at(format->device,
                             format->base_address + block_offset, block,
                             sizeof(block)) ||
             csidos_le16(block) != catalogue_block)) break;
        for (slot = 0U; slot < CSIDOS_ENTRIES_PER_BLOCK; ++slot) {
            const uint8_t *entry = block + CSIDOS_BLOCK_HEADER_SIZE +
                                   slot * CSIDOS_ENTRY_SIZE;
            csidos_raw value;
            xx_mem_zero(&value, sizeof(value));
            if (entry[2] == 0U) continue; /* free slot */
            value.parent = csidos_le16(entry);
            xx_rt_memcpy(value.name, entry + 2U, CSIDOS_NAME_SIZE);
            xx_rt_memcpy(value.extension, entry + 10U, CSIDOS_EXTENSION_SIZE);
            value.attributes = entry[13];
            value.start_block = csidos_le16(entry + 14U);
            value.byte_size = csidos_le16(entry + 18U);
            value.folder = csidos_all_zero(value.extension,
                                           CSIDOS_EXTENSION_SIZE);
            value.entry_offset = block_offset +
                                 (int64_t)CSIDOS_BLOCK_HEADER_SIZE +
                                 (int64_t)slot * CSIDOS_ENTRY_SIZE;
            if ((value.parent & 0xffU) == CSIDOS_ERASED_ID)
                continue; /* erased */
            if (!value.folder) {
                /* A live payload must start past the catalogue and lie
                 * wholly inside the image. */
                if (value.start_block < CSIDOS_FIRST_DATA_BLOCK ||
                    (int64_t)value.start_block * CSIDOS_BLOCK_SIZE +
                            (int64_t)value.byte_size > size) goto fail;
                has_file = true;
            }
            if (raw_count >= CSIDOS_MAX_ENTRIES) goto fail;
            raw[raw_count++] = value;
        }
    }
    if (!has_file) goto fail;
    /* A directory publishes its own identifier in the attribute byte.  The
     * first entry claiming an identifier owns it; later duplicates are
     * stale rename leftovers. */
    for (index = 0U; index < raw_count; ++index) {
        unsigned identifier = raw[index].attributes;
        char *leaf;
        if (!raw[index].folder || identifier == 0U ||
            identifier == CSIDOS_ROOT_ID || identifier == CSIDOS_ERASED_ID ||
            tree.leaf[identifier]) continue;
        leaf = csidos_component(&raw[index]);
        if (!leaf) goto fail;
        tree.leaf[identifier] = leaf;
        tree.parent[identifier] = (uint8_t)(raw[index].parent & 0xffU);
        raw[index].owns_identifier = true;
    }
    for (index = 0U; index < raw_count; ++index) {
        const char *parent_path;
        csidos_member member;
        xx_mem_zero(&member, sizeof(member));
        member.entry_offset = format->base_address + raw[index].entry_offset;
        member.attributes = raw[index].attributes;
        if (raw[index].folder) {
            const char *directory_path;
            if (!raw[index].owns_identifier) continue;
            if (!csidos_tree_resolve(&tree, raw[index].attributes, 0U,
                                     &directory_path)) goto fail;
            member.name = csidos_copy_string(directory_path);
            member.folder = true;
            member.data_offset = member.entry_offset;
        } else {
            char *leaf = csidos_component(&raw[index]);
            char *path = NULL;
            if (!leaf) goto fail;
            if (!csidos_tree_resolve(&tree, raw[index].parent & 0xffU, 0U,
                                     &parent_path) ||
                !csidos_tree_claim(&tree, parent_path, leaf, &path)) {
                xx_mem_free(leaf);
                goto fail;
            }
            xx_mem_free(leaf);
            member.name = path;
            member.byte_size = raw[index].byte_size;
            member.data_offset = format->base_address +
                                 (int64_t)raw[index].start_block *
                                     CSIDOS_BLOCK_SIZE;
        }
        if (!member.name || !csidos_add_member(stream, &member)) {
            if (member.name) xx_mem_free(member.name);
            goto fail;
        }
    }
    if (stream->count == 0U) goto fail;
    stream->image_size = size;
    csidos_tree_cleanup(&tree);
    xx_mem_free(raw);
    *result = stream;
    return true;
fail:
    csidos_tree_cleanup(&tree);
    if (raw) xx_mem_free(raw);
    csidos_stream_free(stream);
    return false;
}

static bool csidos_copy_options(xx_list_s *destination,
                                const xx_list_s *source) {
    size_t index;
    if (!source) return true;
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

static const xx_var *csidos_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *item =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (item && item->meta_id == id) return &item->var;
    }
    return NULL;
}

static bool csidos_set_record(xx_archive_record *record,
                              const csidos_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->entry_offset;
    record->header_size = CSIDOS_ENTRY_SIZE;
    record->data_offset = member->data_offset;
    record->compressed_size = member->folder ? 0 : member->byte_size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          member->folder ? 0U
                                                         : member->byte_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          member->folder ? 0U
                                                         : member->byte_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          0U) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES,
                                          member->attributes) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           member->folder);
}

static bool csidos_verify_data(Abstractformat *format,
                               const csidos_member *member,
                               xx_pd_struct *pd) {
    uint8_t buffer[4096];
    size_t remaining;
    int64_t offset;
    if (!format || !member) return false;
    if (member->folder) return true;
    remaining = member->byte_size;
    offset = member->data_offset;
    while (remaining != 0U) {
        size_t amount = remaining < sizeof(buffer) ? remaining
                                                   : sizeof(buffer);
        if ((pd && xx_pd_is_stopped(pd)) ||
            !csidos_read_at(format->device, offset, buffer, amount))
            return false;
        offset += (int64_t)amount;
        remaining -= amount;
    }
    return true;
}

void xx_csidos_init(xx_csidos *image, xx_io_device *device,
                    int64_t base_address) {
    if (!image) return;
    xx_mem_zero(image, sizeof(*image));
    xx_format_init(&image->format, device, base_address);
    image->format.endian = XX_ENDIAN_LITTLE;
    image->format.file_type = XX_CSIDOS_FILE_TYPE;
    image->format.format_type = XX_TYPE_ARCHIVE;
    image->format.is_archive = true;
    xx_format_set_mime_type(&image->format, "application/x-csidos-disk-image");
    xx_format_set_extension(&image->format, "img");
    image->format.check_is_valid = xx_csidos_check_is_valid;
    image->format.handle_base_info = xx_csidos_handle_base_info;
    image->format.get_format_size = xx_csidos_get_format_size;
    image->format.get_number_of_archive_records =
        xx_csidos_get_number_of_archive_records;
    image->format.create_archive_records_reading =
        xx_csidos_create_archive_records_reading;
    image->format.get_current_archive_record =
        xx_csidos_get_current_archive_record;
    image->format.unpack_current_archive_record =
        xx_csidos_unpack_current_archive_record;
    image->format.archive_record_move_to_next =
        xx_csidos_archive_record_move_to_next;
    image->format.free_archive_records_reading =
        xx_csidos_free_archive_records_reading;
    image->image_end = -1;
}

xx_csidos *xx_csidos_create(xx_io_device *device, int64_t base_address) {
    xx_csidos *image = (xx_csidos *)xx_mem_alloc(sizeof(*image));
    if (image) xx_csidos_init(image, device, base_address);
    return image;
}

void xx_csidos_destroy(xx_csidos *image) {
    if (image) xx_format_cleanup_extra_parameters(&image->format);
}

void xx_csidos_free(xx_csidos *image) {
    if (!image) return;
    xx_csidos_destroy(image);
    xx_mem_free(image);
}

bool xx_csidos_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    csidos_stream *stream;
    (void)pd;
    if (!csidos_parse(format, &stream)) return false;
    csidos_stream_free(stream);
    return true;
}

bool xx_csidos_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    csidos_stream *stream;
    xx_csidos *image;
    (void)pd;
    if (!format || !csidos_parse(format, &stream)) return false;
    image = (xx_csidos *)format;
    image->number_of_records = stream->count;
    image->image_end = format->base_address + stream->image_size;
    format->number_of_archive_records = stream->count;
    format->format_size = stream->image_size;
    format->overlay_offset = -1;
    format->overlay_size = 0;
    format->is_valid = true;
    format->base_info_handled = true;
    csidos_stream_free(stream);
    return true;
}

int64_t xx_csidos_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_csidos_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_csidos_get_number_of_archive_records(Abstractformat *format,
                                                 xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_csidos_handle_base_info(format, pd))
               ? ((xx_csidos *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_csidos_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    csidos_stream *stream;
    xx_archive_record_state *state;
    (void)pd;
    if (!csidos_parse(format, &stream)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        csidos_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = csidos_stream_free;
    state->total_records = stream->count;
    if (!csidos_copy_options(&state->options, options) ||
        !csidos_set_record(&state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_csidos_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_csidos_archive_record_move_to_next(Abstractformat *format,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    csidos_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (csidos_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = csidos_set_record(&state->current_record,
                                          &stream->items[stream->index]);
    return state->has_record;
}

bool xx_csidos_unpack_current_archive_record(Abstractformat *format,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    csidos_stream *stream;
    csidos_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (csidos_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    path_option = csidos_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) return csidos_verify_data(format, member, pd);
    if (path_option->type == XX_VAR_TYPE_STRING ||
        path_option->type == XX_VAR_TYPE_STRING_VIEW)
        base = xx_var_get_str(path_option);
    else if (path_option->type == XX_VAR_TYPE_WSTRING ||
             path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base = owned_base;
    }
    if (!base) goto done;
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' &&
            base[xx_str_len(base) - 1U] != '\\')
               ? xx_str_concat3(base, "/", member->name)
               : xx_str_concat(base, member->name);
    if (!path) goto done;
    if (member->folder) {
        result = xx_store_create_dirs_a(path, true);
        goto done;
    }
    if (!xx_store_create_dirs_a(path, false)) goto done;
    result = xx_store_unpack_device_to_file(format->device,
                                            member->data_offset,
                                            member->byte_size, path, pd);
done:
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_csidos_free_archive_records_reading(Abstractformat *format,
                                            xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
