/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * AO-DOS/MicroDOS images are a contiguous 512-byte-block filesystem for the
 * Elektronika BK family.  This reader walks the directory and extent chain
 * directly; no external disk-image or Qt bridge is used.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/aodos/xx_aodos.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#define AODOS_BLOCK_SIZE 512U
#define AODOS_SYSTEM_SIZE (20U * AODOS_BLOCK_SIZE)
#define AODOS_DIRECTORY_OFFSET 0x140U
#define AODOS_DIRECTORY_SIZE (AODOS_SYSTEM_SIZE - AODOS_DIRECTORY_OFFSET)
#define AODOS_ENTRY_SIZE 24U
#define AODOS_NAME_SIZE 14U
#define AODOS_MAX_ENTRIES 413U
#define AODOS_MIN_SIZE (AODOS_SYSTEM_SIZE + AODOS_BLOCK_SIZE)
#define AODOS_MAX_SIZE UINT32_C(0x1000000)
#define AODOS_MAX_DEPTH 16U
#define AODOS_MAX_COLLISIONS 1024U

typedef struct aodos_raw_s {
    uint16_t word;
    uint16_t start_block;
    uint16_t block_count;
    uint16_t load_address;
    uint16_t byte_size;
    int64_t entry_offset;
    uint8_t name[AODOS_NAME_SIZE];
} aodos_raw;

typedef struct aodos_member_s {
    char *name;
    int64_t entry_offset;
    int64_t data_offset;
    uint16_t byte_size;
    uint16_t block_count;
    uint16_t load_address;
    uint16_t word;
    bool folder;
} aodos_member;

typedef struct aodos_stream_s {
    aodos_member *items;
    size_t count;
    size_t index;
    int64_t image_size;
} aodos_stream;

typedef struct aodos_tree_s {
    char *leaf[256];
    char *path[256];
    uint8_t parent[256];
    bool resolving[256];
    char **used;
    size_t used_count;
} aodos_tree;

static const uint8_t aodos_magic[4] = { 0xa0U, 0x00U, 0x16U, 0x01U };

/* KOI8-R's upper half, kept as Unicode scalar values before UTF-8 emission. */
static const uint16_t aodos_koi8r_high[128] = {
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

static uint16_t aodos_le16(const uint8_t *bytes) {
    return (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U);
}

static bool aodos_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

static bool aodos_contains(const uint8_t *buffer, size_t size,
                           const char *needle) {
    size_t needle_size, index;
    if (!buffer || !needle || !(needle_size = xx_rt_strlen(needle)) ||
        needle_size > size) return false;
    for (index = 0U; index <= size - needle_size; ++index)
        if (xx_rt_memcmp(buffer + index, needle, needle_size) == 0) return true;
    return false;
}

static bool aodos_all_f6(const uint8_t *bytes, size_t size) {
    size_t index;
    if (!bytes || size == 0U) return false;
    for (index = 0U; index < size; ++index)
        if (bytes[index] != 0xf6U) return false;
    return true;
}

static char *aodos_copy_string(const char *source) {
    size_t size;
    char *result;
    if (!source || (size = xx_str_len(source)) == SIZE_MAX) return NULL;
    result = (char *)xx_mem_alloc(size + 1U);
    if (!result) return NULL;
    xx_rt_memcpy(result, source, size + 1U);
    return result;
}

static bool aodos_append_utf8(char *output, size_t capacity, size_t *at,
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

static bool aodos_reserved_name(const char *name) {
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

static char *aodos_component(const uint8_t *bytes, size_t size) {
    char *result;
    size_t input = size, output = 0U, index;
    if (!bytes || size == 0U || size > (SIZE_MAX - 2U) / 3U) return NULL;
    while (input != 0U && bytes[input - 1U] == ' ') --input;
    result = (char *)xx_mem_alloc(input * 3U + 2U);
    if (!result) return NULL;
    for (index = 0U; index < input; ++index) {
        uint16_t codepoint = bytes[index] < 0x80U ? bytes[index] :
                             aodos_koi8r_high[bytes[index] - 0x80U];
        if (codepoint < 0x20U || codepoint == '"' || codepoint == '*' ||
            codepoint == ':' || codepoint == '<' || codepoint == '>' ||
            codepoint == '?' || codepoint == '|' || codepoint == '/' ||
            codepoint == '\\') codepoint = '_';
        if (!aodos_append_utf8(result, input * 3U + 1U, &output, codepoint)) {
            xx_mem_free(result);
            return NULL;
        }
    }
    while (output != 0U && (result[output - 1U] == ' ' ||
                             result[output - 1U] == '.')) --output;
    if (output == 0U) result[output++] = '_';
    result[output] = 0;
    if (aodos_reserved_name(result)) {
        xx_rt_memmove(result + 1U, result, output + 1U);
        result[0] = '_';
    }
    return result;
}

static bool aodos_path_equal(const char *first, const char *second) {
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

static char *aodos_join(const char *parent, const char *leaf) {
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

static char *aodos_suffix(const char *leaf, unsigned suffix) {
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
    if (after != 0U) xx_rt_memcpy(result + before + suffix_size, leaf + before, after);
    result[leaf_size + suffix_size] = 0;
    return result;
}

static void aodos_tree_cleanup(aodos_tree *tree) {
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

static bool aodos_tree_claim(aodos_tree *tree, const char *parent,
                             const char *leaf, char **result) {
    unsigned suffix;
    if (!tree || !parent || !leaf || !result) return false;
    for (suffix = 1U; suffix <= AODOS_MAX_COLLISIONS; ++suffix) {
        char *candidate_leaf = suffix == 1U ? aodos_copy_string(leaf) :
                                                aodos_suffix(leaf, suffix - 1U);
        char *candidate;
        size_t index;
        bool used = false;
        if (!candidate_leaf) return false;
        candidate = aodos_join(parent, candidate_leaf);
        xx_mem_free(candidate_leaf);
        if (!candidate) return false;
        for (index = 0U; index < tree->used_count; ++index) {
            if (aodos_path_equal(tree->used[index], candidate)) {
                used = true;
                break;
            }
        }
        if (!used) {
            if (tree->used_count > SIZE_MAX / sizeof(*tree->used) - 1U) {
                xx_mem_free(candidate);
                return false;
            }
            char **grown = (char **)xx_mem_realloc(tree->used,
                                                    (tree->used_count + 1U) *
                                                    sizeof(*grown));
            char *stored;
            if (!grown) {
                xx_mem_free(candidate);
                return false;
            }
            tree->used = grown;
            stored = aodos_copy_string(candidate);
            if (!stored) {
                xx_mem_free(candidate);
                return false;
            }
            tree->used[tree->used_count++] = stored;
            *result = candidate;
            return true;
        }
        xx_mem_free(candidate);
    }
    return false;
}

static bool aodos_tree_resolve(aodos_tree *tree, unsigned index,
                               unsigned depth, const char **result) {
    const char *parent_path;
    char *path;
    if (!tree || !result || depth > AODOS_MAX_DEPTH) return false;
    if (index == 0U) {
        *result = "";
        return true;
    }
    if (tree->path[index]) {
        *result = tree->path[index];
        return true;
    }
    if (!tree->leaf[index] || tree->resolving[index]) return false;
    tree->resolving[index] = true;
    if (!aodos_tree_resolve(tree, tree->parent[index], depth + 1U,
                            &parent_path) ||
        !aodos_tree_claim(tree, parent_path, tree->leaf[index], &path)) {
        tree->resolving[index] = false;
        return false;
    }
    tree->resolving[index] = false;
    tree->path[index] = path;
    *result = path;
    return true;
}

static void aodos_stream_free(void *opaque) {
    aodos_stream *stream = (aodos_stream *)opaque;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index)
        if (stream->items[index].name) xx_mem_free(stream->items[index].name);
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool aodos_add_member(aodos_stream *stream, const aodos_member *member) {
    aodos_member *grown;
    if (!stream || !member || stream->count >= AODOS_MAX_ENTRIES ||
        stream->count > SIZE_MAX / sizeof(*grown) - 1U) return false;
    grown = (aodos_member *)xx_mem_realloc(stream->items,
                                           (stream->count + 1U) * sizeof(*grown));
    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}

static bool aodos_parse(Abstractformat *format, aodos_stream **result) {
    uint8_t boot[AODOS_BLOCK_SIZE];
    uint8_t *directory = NULL;
    aodos_raw *raw = NULL;
    aodos_tree tree;
    aodos_stream *stream = NULL;
    int64_t total, size, total_blocks, chain_end = -1, first_extent = -1;
    size_t raw_count = 0U, index;
    bool terminator = false;
    xx_mem_zero(&tree, sizeof(tree));
    if (!format || !format->device || !result || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size < (int64_t)AODOS_MIN_SIZE || size > (int64_t)AODOS_MAX_SIZE ||
        size % (int64_t)AODOS_BLOCK_SIZE != 0 ||
        !aodos_read_at(format->device, format->base_address, boot, sizeof(boot)) ||
        xx_rt_memcmp(boot, aodos_magic, sizeof(aodos_magic)) != 0 ||
        !aodos_contains(boot, sizeof(boot), "AO-DOS")) return false;
    directory = (uint8_t *)xx_mem_alloc(AODOS_DIRECTORY_SIZE);
    raw = (aodos_raw *)xx_mem_calloc(AODOS_MAX_ENTRIES, sizeof(*raw));
    stream = (aodos_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!directory || !raw || !stream ||
        !aodos_read_at(format->device,
                       format->base_address + AODOS_DIRECTORY_OFFSET,
                       directory, AODOS_DIRECTORY_SIZE)) goto fail;
    total_blocks = size / (int64_t)AODOS_BLOCK_SIZE;
    for (index = 0U; index < AODOS_MAX_ENTRIES; ++index) {
        const uint8_t *entry = directory + index * AODOS_ENTRY_SIZE;
        aodos_raw value;
        int64_t extent_end;
        xx_mem_zero(&value, sizeof(value));
        value.entry_offset = (int64_t)AODOS_DIRECTORY_OFFSET +
                             (int64_t)index * AODOS_ENTRY_SIZE;
        value.word = aodos_le16(entry);
        xx_rt_memcpy(value.name, entry + 2U, AODOS_NAME_SIZE);
        value.start_block = aodos_le16(entry + 16U);
        value.block_count = aodos_le16(entry + 18U);
        value.load_address = aodos_le16(entry + 20U);
        value.byte_size = aodos_le16(entry + 22U);
        extent_end = (int64_t)value.start_block + value.block_count;
        if (aodos_all_f6(value.name, sizeof(value.name))) {
            if (value.word != 0xffffU || extent_end != total_blocks ||
                (chain_end >= 0 && value.start_block != chain_end)) goto fail;
            terminator = true;
            break;
        }
        if (value.name[0] == ' ') goto fail;
        for (size_t character = 0U; character < sizeof(value.name); ++character)
            if (value.name[character] < 0x20U) goto fail;
        if (extent_end > total_blocks ||
            (value.word != 0xffffU &&
             (int64_t)value.byte_size >
                 (int64_t)value.block_count * AODOS_BLOCK_SIZE)) goto fail;
        if (value.block_count != 0U) {
            if (chain_end < 0) {
                if (value.start_block < 1U) goto fail;
                first_extent = (int64_t)value.start_block * AODOS_BLOCK_SIZE;
            } else if (value.start_block != chain_end) {
                goto fail;
            }
            chain_end = extent_end;
        }
        raw[raw_count++] = value;
    }
    if (!terminator || chain_end < 0 || first_extent < 0 ||
        (int64_t)AODOS_DIRECTORY_OFFSET +
            (int64_t)(raw_count + 1U) * AODOS_ENTRY_SIZE > first_extent)
        goto fail;
    for (index = 0U; index < raw_count; ++index) {
        unsigned own;
        char *leaf;
        if (raw[index].word == 0xffffU) continue;
        own = raw[index].word & 0xffU;
        if (own == 0U) continue;
        if (tree.leaf[own]) goto fail;
        leaf = aodos_component(raw[index].name, sizeof(raw[index].name));
        if (!leaf) goto fail;
        tree.leaf[own] = leaf;
        tree.parent[own] = (uint8_t)(raw[index].word >> 8U);
    }
    for (index = 0U; index < raw_count; ++index) {
        const char *parent_path;
        char *path;
        aodos_member member;
        unsigned own;
        if (raw[index].word == 0xffffU) continue;
        xx_mem_zero(&member, sizeof(member));
        member.entry_offset = format->base_address + raw[index].entry_offset;
        member.word = raw[index].word;
        member.block_count = raw[index].block_count;
        member.load_address = raw[index].load_address;
        own = raw[index].word & 0xffU;
        if (own != 0U) {
            const char *directory_path;
            if (!aodos_tree_resolve(&tree, own, 0U, &directory_path)) goto fail;
            member.name = aodos_copy_string(directory_path);
            member.folder = true;
            member.data_offset = member.entry_offset;
        } else {
            char *leaf = aodos_component(raw[index].name, sizeof(raw[index].name));
            if (!leaf || !aodos_tree_resolve(&tree,
                                             raw[index].word >> 8U, 0U,
                                             &parent_path) ||
                !aodos_tree_claim(&tree, parent_path, leaf, &path)) {
                if (leaf) xx_mem_free(leaf);
                goto fail;
            }
            xx_mem_free(leaf);
            member.name = path;
            member.byte_size = raw[index].byte_size;
            member.data_offset = format->base_address +
                                 (int64_t)raw[index].start_block * AODOS_BLOCK_SIZE;
        }
        if (!member.name || !aodos_add_member(stream, &member)) {
            if (member.name) xx_mem_free(member.name);
            goto fail;
        }
    }
    if (stream->count == 0U) goto fail;
    stream->image_size = size;
    aodos_tree_cleanup(&tree);
    xx_mem_free(directory);
    xx_mem_free(raw);
    *result = stream;
    return true;
fail:
    aodos_tree_cleanup(&tree);
    if (directory) xx_mem_free(directory);
    if (raw) xx_mem_free(raw);
    aodos_stream_free(stream);
    return false;
}

static bool aodos_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *aodos_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *item =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (item && item->meta_id == id) return &item->var;
    }
    return NULL;
}

static bool aodos_set_record(xx_archive_record *record,
                             const aodos_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->entry_offset;
    record->header_size = AODOS_ENTRY_SIZE;
    record->data_offset = member->data_offset;
    record->compressed_size = member->folder ? 0 : member->byte_size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          member->folder ? 0U : member->byte_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          member->folder ? 0U : member->byte_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          0U) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES,
                                          member->word) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           member->folder);
}

static bool aodos_verify_data(Abstractformat *format,
                              const aodos_member *member,
                              xx_pd_struct *pd) {
    uint8_t buffer[4096];
    size_t remaining;
    int64_t offset;
    if (!format || !member || member->folder) return member && member->folder;
    remaining = member->byte_size;
    offset = member->data_offset;
    while (remaining != 0U) {
        size_t amount = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
        if ((pd && xx_pd_is_stopped(pd)) ||
            !aodos_read_at(format->device, offset, buffer, amount)) return false;
        offset += (int64_t)amount;
        remaining -= amount;
    }
    return true;
}

void xx_aodos_init(xx_aodos *image, xx_io_device *device, int64_t base_address) {
    if (!image) return;
    xx_mem_zero(image, sizeof(*image));
    xx_format_init(&image->format, device, base_address);
    image->format.endian = XX_ENDIAN_LITTLE;
    image->format.file_type = XX_FILE_TYPE_AODOS;
    image->format.format_type = XX_TYPE_ARCHIVE;
    image->format.is_archive = true;
    xx_format_set_mime_type(&image->format, "application/x-aodos-disk-image");
    xx_format_set_extension(&image->format, "img");
    image->format.check_is_valid = xx_aodos_check_is_valid;
    image->format.handle_base_info = xx_aodos_handle_base_info;
    image->format.get_format_size = xx_aodos_get_format_size;
    image->format.get_number_of_archive_records =
        xx_aodos_get_number_of_archive_records;
    image->format.create_archive_records_reading =
        xx_aodos_create_archive_records_reading;
    image->format.get_current_archive_record =
        xx_aodos_get_current_archive_record;
    image->format.unpack_current_archive_record =
        xx_aodos_unpack_current_archive_record;
    image->format.archive_record_move_to_next =
        xx_aodos_archive_record_move_to_next;
    image->format.free_archive_records_reading =
        xx_aodos_free_archive_records_reading;
    image->image_end = -1;
}

xx_aodos *xx_aodos_create(xx_io_device *device, int64_t base_address) {
    xx_aodos *image = (xx_aodos *)xx_mem_alloc(sizeof(*image));
    if (image) xx_aodos_init(image, device, base_address);
    return image;
}

void xx_aodos_destroy(xx_aodos *image) {
    if (image) xx_format_cleanup_extra_parameters(&image->format);
}

void xx_aodos_free(xx_aodos *image) {
    if (!image) return;
    xx_aodos_destroy(image);
    xx_mem_free(image);
}

bool xx_aodos_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    aodos_stream *stream;
    (void)pd;
    if (!aodos_parse(format, &stream)) return false;
    aodos_stream_free(stream);
    return true;
}

bool xx_aodos_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    aodos_stream *stream;
    xx_aodos *image;
    (void)pd;
    if (!format || !aodos_parse(format, &stream)) return false;
    image = (xx_aodos *)format;
    image->number_of_records = stream->count;
    image->image_end = format->base_address + stream->image_size;
    format->number_of_archive_records = stream->count;
    format->format_size = stream->image_size;
    format->overlay_offset = -1;
    format->overlay_size = 0;
    format->is_valid = true;
    format->base_info_handled = true;
    aodos_stream_free(stream);
    return true;
}

int64_t xx_aodos_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_aodos_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_aodos_get_number_of_archive_records(Abstractformat *format,
                                                 xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_aodos_handle_base_info(format, pd))
               ? ((xx_aodos *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_aodos_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    aodos_stream *stream;
    xx_archive_record_state *state;
    (void)pd;
    if (!aodos_parse(format, &stream)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        aodos_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = aodos_stream_free;
    state->total_records = stream->count;
    if (!aodos_copy_options(&state->options, options) ||
        !aodos_set_record(&state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_aodos_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_aodos_archive_record_move_to_next(Abstractformat *format,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    aodos_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (aodos_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = aodos_set_record(&state->current_record,
                                         &stream->items[stream->index]);
    return state->has_record;
}

bool xx_aodos_unpack_current_archive_record(Abstractformat *format,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    aodos_stream *stream;
    aodos_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (aodos_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    path_option = aodos_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        return aodos_verify_data(format, member, pd);
    }
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
    result = xx_store_unpack_device_to_file(format->device, member->data_offset,
                                            member->byte_size, path, pd);
done:
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_aodos_free_archive_records_reading(Abstractformat *format,
                                           xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
