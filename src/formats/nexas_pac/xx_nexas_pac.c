/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Independent native C NeXAS PAC reader. Primary layout evidence (MIT, morkt):
 * https://github.com/morkt/GARbro/blob/master/ArcFormats/Nexas/ArcPAC.cs
 * https://github.com/morkt/GARbro/blob/master/ArcFormats/HuffmanCompression.cs
 * https://github.com/morkt/GARbro/blob/master/ArcFormats/LzssStream.cs
 * No upstream implementation is incorporated.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/nexas_pac/xx_nexas_pac.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/adler32/xx_adler32.h"
#include "xxfclib/global/xx_global.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#ifdef NEXAS_PAC
#define NX_FILE_TYPE XX_FILE_TYPE_NEXAS_PAC
#else
#define NX_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif
#define NX_HEADER_SIZE 12U
#define NX_MAX_COUNT 0xFFFFFU
#define NX_MAX_INDEX (16U * 1024U * 1024U)
#define NX_MAX_PACKED (256U * 1024U * 1024U)

typedef struct nx_member {
    int64_t offset;
    uint32_t size, unpacked_size;
    int64_t header_offset;
    uint32_t header_size;
    char *name;
    bool duplicate;
} nx_member;
typedef struct nx_layout {
    nx_member *members;
    uint32_t count;
    uint32_t method, name_width, record_size;
    bool index_compressed;
    int64_t format_size;
    size_t index;
} nx_layout;
typedef struct nx_name_key {
    const char *name;
    uint32_t index;
} nx_name_key;

static bool nx_stopped(xx_pd_struct *pd) {
    return pd && xx_pd_is_stopped(pd);
}
static uint32_t nx_le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8U |
           (uint32_t)p[2] << 16U | (uint32_t)p[3] << 24U;
}
static bool nx_read(xx_io_device *device, int64_t at, void *buffer,
                    size_t size, xx_pd_struct *pd) {
    size_t done = 0U;
    if (!device || at < 0 || nx_stopped(pd) || xx_io_seek64(device, at, XX_RT_SEEK_SET))
        return false;
    while (done < size) {
        ssize_t got;
        size_t take = size - done;
        if (nx_stopped(pd)) return false;
        if (take > 65536U) take = 65536U;
        got = xx_io_read(device, (uint8_t *)buffer + done, take);
        if (got <= 0 || (size_t)got > take) return false;
        done += (size_t)got;
    }
    return true;
}
static void nx_layout_free(void *ptr) {
    nx_layout *layout = (nx_layout *)ptr;
    uint32_t i;
    if (!layout) return;
    if (layout->members) {
        for (i = 0U; i < layout->count; ++i)
            if (layout->members[i].name) xx_mem_free(layout->members[i].name);
        xx_mem_free(layout->members);
    }
    xx_mem_free(layout);
}
static size_t nx_escape(char *out, uint8_t c) {
    static const char hex[] = "0123456789ABCDEF";
    out[0] = '%'; out[1] = hex[c >> 4U]; out[2] = hex[c & 15U];
    return 3U;
}
static bool nx_lead(uint8_t c) {
    return (c >= 0x81U && c <= 0x9fU) || (c >= 0xe0U && c <= 0xfcU);
}
static bool nx_trail(uint8_t c) {
    return (c >= 0x40U && c <= 0x7eU) || (c >= 0x80U && c <= 0xfcU);
}
static char *nx_name(const uint8_t *raw, size_t size) {
    char *result = (char *)xx_mem_alloc(size * 3U + 14U);
    size_t i = 0U, at = 0U;
    if (!result) return NULL;
    while (i < size) {
        uint8_t c = raw[i++];
        if (nx_lead(c) && i < size && nx_trail(raw[i])) {
            at += nx_escape(result + at, c);
            at += nx_escape(result + at, raw[i++]);
        } else if (c >= 0x80U || c < 0x20U || c == 0x7fU || c == '%') {
            at += nx_escape(result + at, c);
        } else {
            result[at++] = c == '\\' ? '/' : (char)c;
        }
    }
    result[at] = 0;
    return result;
}
static int nx_fold_compare(const char *a, const char *b) {
    for (;;) {
        unsigned char x = (unsigned char)*a++, y = (unsigned char)*b++;
        if (x >= 'A' && x <= 'Z') x = (unsigned char)(x + 'a' - 'A');
        if (y >= 'A' && y <= 'Z') y = (unsigned char)(y + 'a' - 'A');
        if (x != y) return x < y ? -1 : 1;
        if (x == 0U) return 0;
    }
}
static int nx_compare_keys(const void *a, const void *b) {
    const nx_name_key *x = (const nx_name_key *)a;
    const nx_name_key *y = (const nx_name_key *)b;
    int order = nx_fold_compare(x->name, y->name);
    if (order) return order;
    return x->index < y->index ? -1 : x->index > y->index ? 1 : 0;
}
static void nx_suffix(char *name, uint32_t index) {
    char suffix[14];
    size_t length = xx_str_len(name), component = 0U, dot = length, i;
    int amount = xx_rt_snprintf(suffix, sizeof(suffix), "%%_%u", index);
    for (i = 0U; i < length; ++i) if (name[i] == '/') component = i + 1U;
    for (i = length; i > component + 1U; --i)
        if (name[i - 1U] == '.') { dot = i - 1U; break; }
    if (amount <= 0 || (size_t)amount >= sizeof(suffix)) return;
    xx_rt_memmove(name + dot + (size_t)amount, name + dot, length - dot + 1U);
    xx_rt_memcpy(name + dot, suffix, (size_t)amount);
}


/* Explicit binary Huffman tree: an MSB flag 0 followed by eight symbol bits,
 * or flag 1 followed by its left and right subtrees in pre-order. */
typedef struct nx_bits {
    const uint8_t *bytes;
    size_t size, at;
} nx_bits;
typedef struct nx_tree {
    uint16_t left[512], right[512], next;
} nx_tree;
static bool nx_bits_read(nx_bits *bits, unsigned count, uint32_t *value) {
    uint32_t result = 0U;
    unsigned i;
    for (i = 0U; i < count; ++i) {
        if ((bits->at >> 3U) >= bits->size) return false;
        result = (result << 1U) |
            ((bits->bytes[bits->at >> 3U] >> (7U - (bits->at & 7U))) & 1U);
        ++bits->at;
    }
    *value = result; return true;
}
static bool nx_tree_read(nx_bits *bits, nx_tree *tree, uint16_t *node,
                         unsigned depth, xx_pd_struct *pd) {
    uint32_t value;
    uint16_t branch;
    if (depth > 256U || nx_stopped(pd) || !nx_bits_read(bits, 1U, &value)) return false;
    if (!value) {
        if (!nx_bits_read(bits, 8U, &value)) return false;
        *node = (uint16_t)value; return true;
    }
    if (tree->next >= 512U) return false;
    branch = tree->next++;
    if (!nx_tree_read(bits, tree, &tree->left[branch], depth + 1U, pd) ||
        !nx_tree_read(bits, tree, &tree->right[branch], depth + 1U, pd)) return false;
    *node = branch; return true;
}
static bool nx_huffman_memory(const uint8_t *packed, size_t packed_size,
                              uint8_t *plain, size_t plain_size, xx_pd_struct *pd) {
    nx_bits bits;
    nx_tree tree;
    uint16_t root;
    size_t i;
    bits.bytes = packed; bits.size = packed_size; bits.at = 0U;
    xx_mem_zero(&tree, sizeof(tree)); tree.next = 256U;
    if (!nx_tree_read(&bits, &tree, &root, 0U, pd)) return false;
    for (i = 0U; i < plain_size; ++i) {
        uint16_t node = root;
        if (!(i & 4095U) && nx_stopped(pd)) return false;
        while (node >= 256U) {
            uint32_t bit;
            if (!nx_bits_read(&bits, 1U, &bit)) return false;
            node = bit ? tree.right[node] : tree.left[node];
        }
        plain[i] = (uint8_t)node;
    }
    /* Only the final partial byte's unused bits are padding. */
    return ((bits.at + 7U) >> 3U) == packed_size && !nx_stopped(pd);
}
static nx_layout *nx_index(Abstractformat *format, const uint8_t *bytes,
                            uint32_t count, uint32_t mode, uint32_t width,
                            int64_t data_first, int64_t data_end,
                            int64_t index_at, uint32_t index_size,
                            bool compressed, xx_pd_struct *pd) {
    nx_layout *layout;
    nx_name_key *keys = NULL;
    uint32_t i, record_size = width + 12U;
    bool ok = false;
    if ((uint64_t)count * sizeof(nx_member) > SIZE_MAX ||
        (uint64_t)count * sizeof(*keys) > SIZE_MAX) return NULL;
    layout = (nx_layout *)xx_mem_calloc(1U, sizeof(*layout));
    if (!layout) return NULL;
    layout->count = count; layout->method = mode; layout->name_width = width;
    layout->record_size = record_size; layout->index_compressed = compressed;
    layout->format_size = compressed ? data_end + index_size + 4 : data_first;
    layout->members = (nx_member *)xx_mem_calloc(count, sizeof(*layout->members));
    keys = (nx_name_key *)xx_mem_alloc((size_t)count * sizeof(*keys));
    if (!layout->members || !keys) goto done;
    for (i = 0U; i < count; ++i) {
        const uint8_t *record = bytes + (size_t)i * record_size;
        nx_member *member = &layout->members[i];
        size_t length = 0U, j;
        bool nonspace = false;
        uint32_t offset = nx_le32(record + width);
        uint32_t unpacked = nx_le32(record + width + 4U);
        uint32_t packed = nx_le32(record + width + 8U);
        if (nx_stopped(pd)) goto done;
        while (length < width && record[length]) ++length;
        for (j = 0U; j < length; ++j) if (record[j] > 0x20U) nonspace = true;
        if (!length || !nonspace || (int64_t)offset < data_first ||
            (int64_t)offset > data_end || (uint64_t)packed > (uint64_t)(data_end - offset) ||
            (!mode && packed != unpacked)) goto done;
        member->name = nx_name(record, length);
        if (!member->name) goto done;
        member->offset = format->base_address + offset;
        member->size = packed; member->unpacked_size = unpacked;
        member->header_offset = format->base_address + index_at +
            (compressed ? 0 : (int64_t)i * record_size);
        member->header_size = compressed ? index_size : record_size;
        if (!compressed && (int64_t)offset + packed > layout->format_size)
            layout->format_size = (int64_t)offset + packed;
        keys[i].name = member->name; keys[i].index = i;
    }
    xx_rt_qsort(keys, count, sizeof(*keys), nx_compare_keys);
    if (nx_stopped(pd)) goto done;
    for (i = 1U; i < count; ++i)
        if (!nx_fold_compare(keys[i - 1U].name, keys[i].name))
            layout->members[keys[i].index].duplicate = true;
    for (i = 0U; i < count; ++i) {
        if (nx_stopped(pd)) goto done;
        if (layout->members[i].duplicate) nx_suffix(layout->members[i].name, i);
    }
    ok = true;
done:
    if (keys) xx_mem_free(keys);
    if (!ok) { nx_layout_free(layout); layout = NULL; }
    return layout;
}
static nx_layout *nx_parse_inner(Abstractformat *format, xx_pd_struct *pd) {
    uint8_t header[12], footer[4];
    uint8_t *index = NULL, *packed = NULL;
    nx_layout *layout = NULL;
    int64_t available, total;
    uint32_t count, mode, width, index_size;
    size_t unpacked_size, i;
    if (!format || !format->device || format->base_address < 0 || nx_stopped(pd)) return NULL;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return NULL;
    available = total - format->base_address;
    if (available < 16 ||
        !nx_read(format->device, format->base_address, header, sizeof(header), pd) ||
        xx_rt_memcmp(header, "PAC", 3U) || header[3] == 'K') return NULL;
    count = nx_le32(header + 4U); mode = nx_le32(header + 8U);
    if (!count || count > NX_MAX_COUNT || mode > 4U) return NULL;
    /* Match the primary reader's old-32, old-64, then footer-index order. */
    for (width = 32U; width <= 64U; width += 32U) {
        uint64_t bytes = (uint64_t)count * (width + 12U);
        if (bytes > NX_MAX_INDEX || bytes > (uint64_t)available - 12U) continue;
        index = (uint8_t *)xx_mem_alloc((size_t)bytes);
        if (!index) return NULL;
        if (!nx_read(format->device, format->base_address + 12, index, (size_t)bytes, pd)) goto done;
        layout = nx_index(format, index, count, mode, width, 12 + (int64_t)bytes,
            available, 12, (uint32_t)bytes, false, pd);
        xx_mem_free(index); index = NULL;
        if (layout) return layout;
        if (nx_stopped(pd)) return NULL;
    }
    unpacked_size = (size_t)count * 76U;
    if (unpacked_size > NX_MAX_INDEX ||
        !nx_read(format->device, total - 4, footer, sizeof(footer), pd)) return NULL;
    index_size = nx_le32(footer);
    if (!index_size || (uint64_t)index_size > (uint64_t)unpacked_size * 2U ||
        (int64_t)index_size > available - 16) return NULL;
    packed = (uint8_t *)xx_mem_alloc(index_size);
    index = (uint8_t *)xx_mem_alloc(unpacked_size);
    if (!packed || !index || !nx_read(format->device, total - 4 - index_size, packed, index_size, pd)) goto done;
    for (i = 0U; i < index_size; ++i) {
        if (!(i & 4095U) && nx_stopped(pd)) goto done;
        packed[i] ^= 0xffU;
    }
    if (!nx_huffman_memory(packed, index_size, index, unpacked_size, pd)) goto done;
    layout = nx_index(format, index, count, mode, 64U, 12, available - 4 - index_size,
        available - 4 - index_size, index_size, true, pd);
done:
    if (index) xx_mem_free(index);
    if (packed) xx_mem_free(packed);
    return layout;
}
static nx_layout *nx_parse(Abstractformat *format, xx_pd_struct *pd) {
    int64_t cursor = format && format->device ? xx_io_tell(format->device) : -1;
    nx_layout *layout = nx_parse_inner(format, pd);
    if (cursor >= 0 && xx_io_seek64(format->device, cursor, XX_RT_SEEK_SET)) {
        nx_layout_free(layout); layout = NULL;
    }
    return layout;
}
static void nx_destroy_format(Abstractformat *format) {
    xx_nexas_pac_destroy((xx_nexas_pac *)format);
}
void xx_nexas_pac_init(xx_nexas_pac *archive, xx_io_device *device, int64_t base) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = NX_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_extension(&archive->format, "pac");
    xx_format_set_mime_type(&archive->format, "application/x-nexas-pac");
    archive->format.destroy = nx_destroy_format;
    archive->format.check_is_valid = xx_nexas_pac_check_is_valid;
    archive->format.handle_base_info = xx_nexas_pac_handle_base_info;
    archive->format.get_format_size = xx_nexas_pac_get_format_size;
    archive->format.get_number_of_archive_records = xx_nexas_pac_get_number_of_archive_records;
    archive->format.create_archive_records_reading = xx_nexas_pac_create_archive_records_reading;
    archive->format.get_current_archive_record = xx_nexas_pac_get_current_archive_record;
    archive->format.unpack_current_archive_record = xx_nexas_pac_unpack_current_archive_record;
    archive->format.archive_record_move_to_next = xx_nexas_pac_archive_record_move_to_next;
    archive->format.free_archive_records_reading = xx_nexas_pac_free_archive_records_reading;
}
xx_nexas_pac *xx_nexas_pac_create(xx_io_device *device, int64_t base) {
    xx_nexas_pac *archive = (xx_nexas_pac *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_nexas_pac_init(archive, device, base);
    return archive;
}
void xx_nexas_pac_destroy(xx_nexas_pac *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}
void xx_nexas_pac_free(xx_nexas_pac *archive) {
    if (!archive) return;
    xx_nexas_pac_destroy(archive); xx_mem_free(archive);
}
bool xx_nexas_pac_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    nx_layout *layout = nx_parse(format, pd);
    bool valid = layout != NULL;
    nx_layout_free(layout);
    return valid;
}
bool xx_nexas_pac_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    nx_layout *layout;
    xx_nexas_pac *archive;
    if (!format || nx_stopped(pd)) return false;
    if (format->base_info_handled) return format->is_valid;
    layout = nx_parse(format, pd);
    if (!layout) return false;
    archive = (xx_nexas_pac *)format;
    archive->number_of_records = layout->count;
    archive->method = layout->method;
    archive->name_width = layout->name_width;
    archive->index_compressed = layout->index_compressed;
    format->number_of_archive_records = layout->count;
    format->format_size = layout->format_size;
    format->is_valid = true;
    format->base_info_handled = true;
    nx_layout_free(layout);
    return true;
}
int64_t xx_nexas_pac_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return xx_nexas_pac_handle_base_info(format, pd) ? format->format_size : -1;
}
uint64_t xx_nexas_pac_get_number_of_archive_records(Abstractformat *format, xx_pd_struct *pd) {
    return xx_nexas_pac_handle_base_info(format, pd) ? format->number_of_archive_records : 0U;
}
static bool nx_set_record(Abstractformat *format, xx_archive_record_state *state) {
    nx_layout *layout = (nx_layout *)state->internal_state;
    const nx_member *member = &layout->members[layout->index];
    xx_archive_record *record = &state->current_record;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    (void)format;
    record->header_offset = member->header_offset;
    record->header_size = member->header_size;
    record->data_offset = member->offset;
    record->compressed_size = member->size;
    return xx_archive_record_set_original_name(record, member->name) &&
        xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE, member->unpacked_size) &&
        xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE, member->size) &&
        xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD, layout->method == 4U && member->size == member->unpacked_size ? 0U : layout->method) &&
        xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false) &&
        xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED, false);
}
xx_archive_record_state *xx_nexas_pac_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    nx_layout *layout = nx_parse(format, pd);
    xx_archive_record_state *state;
    size_t i;
    if (!layout) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) { nx_layout_free(layout); return NULL; }
    xx_archive_record_state_init(state, format);
    state->internal_state = layout;
    state->free_internal = nx_layout_free;
    state->total_records = layout->count;
    state->current_index = 0;
    if (options) for (i = 0U; i < options->count; ++i) {
        const xx_meta *meta = (const xx_meta *)xx_list_at((const xx_list_t *)options, i);
        xx_meta copy;
        if (nx_stopped(pd) || !meta) goto fail;
        xx_meta_init(&copy, meta->meta_id);
        if (!xx_var_copy(&copy.var, &meta->var) || !xx_list_append(&state->options, &copy)) {
            xx_meta_cleanup(&copy); goto fail;
        }
    }
    state->has_record = nx_set_record(format, state);
    if (state->has_record) return state;
fail:
    xx_archive_record_state_free(state);
    return NULL;
}
const xx_archive_record *xx_nexas_pac_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
        ? &state->current_record : NULL;
}
bool xx_nexas_pac_archive_record_move_to_next(Abstractformat *format,
    xx_archive_record_state *state, xx_pd_struct *pd) {
    nx_layout *layout;
    if (!format || !state || state->format != format || !state->has_record ||
        !(layout = (nx_layout *)state->internal_state)) return false;
    if (nx_stopped(pd) || layout->index + 1U >= layout->count) {
        state->has_record = false; return false;
    }
    ++layout->index;
    state->current_index = (int64_t)layout->index;
    state->has_record = nx_set_record(format, state);
    return state->has_record;
}
static const xx_var *nx_option(Abstractformat *format, const xx_list_s *options, uint32_t id) {
    return xx_format_resolve_extra_parameter(format, options, id);
}

typedef struct nx_sink {
    xx_io_device device;
    xx_io_device *destination;
    xx_pd_struct *pd;
    uint8_t *buffer;
    size_t capacity, used;
    uint64_t expected, written;
    uint32_t adler;
} nx_sink;
static bool nx_sink_flush(nx_sink *sink) {
    size_t done = 0U;
    if (nx_stopped(sink->pd)) return false;
    while (sink->destination && done < sink->used) {
        ssize_t amount;
        if (nx_stopped(sink->pd)) return false;
        amount = xx_io_write(sink->destination, sink->buffer + done, sink->used - done);
        if (amount <= 0 || (size_t)amount > sink->used - done) return false;
        done += (size_t)amount;
    }
    sink->used = 0U; return !nx_stopped(sink->pd);
}
static ssize_t nx_sink_write(xx_io_device *device, const void *bytes, size_t size) {
    nx_sink *sink = (nx_sink *)device->priv;
    size_t done = 0U;
    if (nx_stopped(sink->pd) || size > sink->expected - sink->written) return -1;
    sink->adler = xx_adler32_update(sink->adler, bytes, size);
    while (done < size) {
        size_t take = size - done;
        if (nx_stopped(sink->pd)) return -1;
        if (take > sink->capacity - sink->used) take = sink->capacity - sink->used;
        if (sink->buffer + sink->used != (const uint8_t *)bytes + done)
            xx_rt_memcpy(sink->buffer + sink->used, (const uint8_t *)bytes + done, take);
        sink->used += take; done += take;
        if (sink->used == sink->capacity && !nx_sink_flush(sink)) return -1;
    }
    sink->written += size; return (ssize_t)size;
}
static bool nx_huffman(const uint8_t *packed, size_t packed_size, nx_sink *sink) {
    nx_bits bits;
    nx_tree tree;
    uint16_t root;
    bits.bytes = packed; bits.size = packed_size; bits.at = 0U;
    xx_mem_zero(&tree, sizeof(tree)); tree.next = 256U;
    if (!nx_tree_read(&bits, &tree, &root, 0U, sink->pd)) return false;
    while (sink->written < sink->expected) {
        uint16_t node = root;
        uint8_t value;
        if (nx_stopped(sink->pd)) return false;
        while (node >= 256U) {
            uint32_t bit;
            if (!nx_bits_read(&bits, 1U, &bit)) return false;
            node = bit ? tree.right[node] : tree.left[node];
        }
        value = (uint8_t)node;
        if (nx_sink_write(&sink->device, &value, 1U) != 1) return false;
    }
    return ((bits.at + 7U) >> 3U) == packed_size;
}
static bool nx_lzss(const uint8_t *packed, size_t size, nx_sink *sink) {
    uint8_t frame[4096];
    size_t at = 0U;
    unsigned frame_at = 0xfeeU;
    xx_mem_zero(frame, sizeof(frame));
    while (sink->written < sink->expected) {
        uint8_t flags;
        unsigned bit;
        if (at >= size || nx_stopped(sink->pd)) return false;
        flags = packed[at++];
        for (bit = 1U; bit < 256U && sink->written < sink->expected; bit <<= 1U) {
            if (flags & bit) {
                uint8_t value;
                if (at >= size) return false;
                value = packed[at++]; frame[frame_at++ & 4095U] = value;
                if (nx_sink_write(&sink->device, &value, 1U) != 1) return false;
            } else {
                unsigned offset, count, i;
                if (size - at < 2U) return false;
                offset = packed[at] | ((unsigned)(packed[at + 1U] & 0xf0U) << 4U);
                count = (packed[at + 1U] & 15U) + 3U; at += 2U;
                if (count > sink->expected - sink->written) return false;
                for (i = 0U; i < count; ++i) {
                    uint8_t value = frame[offset++ & 4095U];
                    frame[frame_at++ & 4095U] = value;
                    if (nx_sink_write(&sink->device, &value, 1U) != 1) return false;
                }
            }
        }
    }
    return at == size;
}
bool xx_nexas_pac_unpack_current_archive_record_to_device(Abstractformat *format,
    xx_archive_record_state *state, xx_io_device *destination, xx_pd_struct *pd) {
    nx_layout *layout;
    const nx_member *member;
    const xx_var *limit;
    uint8_t *packed = NULL, *buffer = NULL;
    size_t capacity = xx_get_file_buffer_size();
    int64_t cursor, total;
    uint32_t mode;
    nx_sink sink;
    bool ok = false;
    if (!format || !format->device || !state || state->format != format ||
        !state->has_record || !(layout = (nx_layout *)state->internal_state) ||
        layout->index >= layout->count || destination == format->device || nx_stopped(pd)) return false;
    member = &layout->members[layout->index];
    total = xx_io_total_size(format->device);
    if (member->offset < 0 || member->offset > total ||
        (int64_t)member->size > total - member->offset) return false;
    mode = layout->method == 4U && member->size == member->unpacked_size ? 0U : layout->method;
    limit = nx_option(format, &state->options, XX_META_ID_OPT_MAX_MEMBER_SIZE);
    if (limit && member->unpacked_size > xx_var_get_u64(limit)) return false;
    if (mode && member->size > NX_MAX_PACKED) return false;
    if (!member->size && !member->unpacked_size) return !nx_stopped(pd);
    if (!capacity) capacity = XX_DEFAULT_FILE_BUFFER_SIZE;
    if (capacity > 65536U) capacity = 65536U;
    if (capacity > member->unpacked_size) capacity = member->unpacked_size;
    if (!capacity) capacity = 1U;
    limit = nx_option(format, &state->options, XX_META_ID_OPT_MEMORY_LIMIT);
    if (limit) {
        uint64_t maximum = xx_var_get_u64(limit), needed = mode ? member->size : 0U;
        if (needed >= maximum) return false;
        if (capacity > maximum - needed) capacity = (size_t)(maximum - needed);
    }
    cursor = xx_io_tell(format->device);
    buffer = (uint8_t *)xx_mem_alloc(capacity);
    if (!buffer) goto done;
    xx_mem_zero(&sink, sizeof(sink)); sink.device.priv = &sink; sink.device.write = nx_sink_write;
    sink.destination = destination; sink.pd = pd; sink.expected = member->unpacked_size;
    sink.buffer = buffer; sink.capacity = capacity; sink.adler = XX_ADLER32_INIT;
    if (!mode) {
        uint32_t at = 0U;
        while (at < member->size) {
            size_t take = member->size - at;
            if (take > capacity) take = capacity;
            if (!nx_read(format->device, member->offset + at, buffer, take, pd)) goto done;
            /* Stored input and sink stage intentionally share the buffer. */
            if (nx_sink_write(&sink.device, buffer, take) != (ssize_t)take || !nx_sink_flush(&sink)) goto done;
            at += (uint32_t)take;
        }
        ok = true;
    } else {
        size_t consumed = 0U;
        if (!member->size && !member->unpacked_size) { ok = true; goto finish; }
        if (!member->size || !(packed = (uint8_t *)xx_mem_alloc(member->size)) ||
            !nx_read(format->device, member->offset, packed, member->size, pd)) goto done;
        if (mode == 1U) ok = nx_lzss(packed, member->size, &sink);
        else if (mode == 2U) ok = nx_huffman(packed, member->size, &sink);
        else if (mode == 3U || mode == 4U) {
            uint32_t expected_adler;
            if (member->size < 6U || !xx_zlib_stream_header_is_valid(packed, member->size)) goto done;
            expected_adler = (uint32_t)packed[member->size - 4U] << 24U |
                (uint32_t)packed[member->size - 3U] << 16U |
                (uint32_t)packed[member->size - 2U] << 8U | packed[member->size - 1U];
            ok = xx_deflate_unpack_memory_to_device_ex(packed + 2U, member->size - 6U,
                &sink.device, &consumed, false, pd) && consumed == member->size - 6U && sink.adler == expected_adler;
        }
    }
finish:
    ok = ok && sink.written == sink.expected && nx_sink_flush(&sink) && !nx_stopped(pd);
done:
    if (packed) xx_mem_free(packed);
    if (buffer) xx_mem_free(buffer);
    if (cursor >= 0 && xx_io_seek64(format->device, cursor, XX_RT_SEEK_SET)) ok = false;
    return ok;
}
static bool nx_reserved(const char *component, size_t length) {
    static const char *const names[] = {"CON", "PRN", "AUX", "NUL", "CLOCK$", "CONIN$", "CONOUT$"};
    char stem[9];
    size_t n = 0U, i;
    while (n < length && component[n] != '.') ++n;
    while (n && component[n - 1U] == ' ') --n;
    if (n >= sizeof(stem)) return false;
    for (i = 0U; i < n; ++i) {
        char c = component[i]; stem[i] = c >= 'a' && c <= 'z' ? (char)(c + 'A' - 'a') : c;
    }
    stem[n] = 0;
    if (n == 4U && stem[3] >= '0' && stem[3] <= '9' &&
        (!xx_rt_memcmp(stem, "COM", 3U) || !xx_rt_memcmp(stem, "LPT", 3U))) return true;
    for (i = 0U; i < sizeof(names) / sizeof(names[0]); ++i)
        if (!xx_str_cmp(stem, names[i])) return true;
    return false;
}
static bool nx_safe_name(const char *name) {
    const char *component = name, *at;
    if (!name || !*name || *name == '/') return false;
    for (at = name;; ++at) {
        unsigned char c = (unsigned char)*at;
        if (c == ':' || c == '<' || c == '>' || c == '"' || c == '|' ||
            c == '?' || c == '*' || c == '\\' || c == 127U || (c && c < 32U)) return false;
        if (c == '/' || !c) {
            size_t n = (size_t)(at - component);
            if (!n || component[0] == ' ' || component[n - 1U] == '.' ||
                component[n - 1U] == ' ' || nx_reserved(component, n)) return false;
            if (!c) return true;
            component = at + 1;
        }
    }
}
bool xx_nexas_pac_unpack_current_archive_record(Abstractformat *format,
    xx_archive_record_state *state, xx_pd_struct *pd) {
    nx_layout *layout;
    const xx_var *path_option, *overwrite_option;
    const char *base = NULL;
    char *owned_base = NULL, *path = NULL, *stage_path = NULL;
    xx_io_device *stage = NULL;
    bool ok = false, overwrite = false;
    unsigned attempt;
    size_t prefix = 0U, n;
    if (!format || !state || state->format != format || !state->has_record ||
        !(layout = (nx_layout *)state->internal_state) ||
        layout->index >= layout->count || nx_stopped(pd)) return false;
    path_option = nx_option(format, &state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option)
        return xx_nexas_pac_unpack_current_archive_record_to_device(format, state, NULL, pd);
    if (!nx_safe_name(layout->members[layout->index].name)) return false;
    if (path_option->type == XX_VAR_TYPE_STRING || path_option->type == XX_VAR_TYPE_STRING_VIEW)
        base = xx_var_get_str(path_option);
    else if (path_option->type == XX_VAR_TYPE_WSTRING || path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option)); base = owned_base;
    }
    if (!base) goto done;
    overwrite_option = nx_option(format, &state->options, XX_META_ID_OPT_OVERWRITE);
    if (overwrite_option) overwrite = xx_var_get_bool(overwrite_option);
    path = !*base || base[xx_str_len(base) - 1U] == '/' || base[xx_str_len(base) - 1U] == '\\'
        ? xx_str_concat(base, layout->members[layout->index].name)
        : xx_str_concat3(base, "/", layout->members[layout->index].name);
    if (!path || (!overwrite && xx_io_file_exists_a(path)) ||
        !xx_store_create_dirs_a(path, false)) goto done;
    for (n = 0U; path[n]; ++n) if (path[n] == '/' || path[n] == '\\') prefix = n + 1U;
    stage_path = (char *)xx_mem_alloc(prefix + 50U);
    if (!stage_path) goto done;
    xx_rt_memcpy(stage_path, path, prefix);
    for (attempt = 0U; attempt < 128U && !nx_stopped(pd); ++attempt) {
        int wrote = xx_rt_snprintf(stage_path + prefix, 50U,
            ".xxfc-nexas-%u-%u.tmp", (unsigned)layout->index, attempt);
        if (wrote <= 0) goto done;
        /* An archive member may legally use the staging component itself. */
        if (!nx_fold_compare(stage_path, path)) continue;
        stage = xx_io_file_open(stage_path, "wbx");
        if (stage) break;
    }
    if (!stage) goto done;
    ok = xx_nexas_pac_unpack_current_archive_record_to_device(format, state, stage, pd);
    if (xx_io_close(stage)) ok = false;
    stage = NULL;
    if (ok && !nx_stopped(pd)) ok = xx_io_file_replace_a(stage_path, path, overwrite);
    else ok = false;
    if (!ok) (void)xx_io_file_remove_a(stage_path);
done:
    if (stage) { (void)xx_io_close(stage); (void)xx_io_file_remove_a(stage_path); }
    if (stage_path) xx_mem_free(stage_path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return ok;
}
void xx_nexas_pac_free_archive_records_reading(Abstractformat *format,
    xx_archive_record_state *state) {
    (void)format; xx_archive_record_state_free(state);
}
