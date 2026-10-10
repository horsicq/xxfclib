/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Outlook Express 5/6 DBX message store.  xx_outlook_express_dbx_mailbox.h
 * carries the structure tables.  Written from the structure description of
 * the format; no reference code was ported.
 *
 * Everything the file says is untrusted: every node, info object and block
 * must repeat its own offset, lie inside the file and have sane sizes; a
 * child node must name its parent; the tree walk is depth- and
 * visit-bounded, never enters a node twice and never lists a message
 * (first block) twice; info objects and block bytes share file-size
 * budgets so a tree that reuses them cannot make the walk quadratic; a
 * block chain is walked with Brent's cycle check
 * and may not claim more bytes than the file holds.  A damaged subtree or
 * info object is skipped, a damaged block chain is listed but refuses to
 * extract.
 */

#include "xxfclib/global/xx_global.h"
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/outlook_express_dbx_mailbox/xx_outlook_express_dbx_mailbox.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>
#include "xxfclib/data/xx_data.h"

#ifdef OUTLOOK_EXPRESS_DBX_MAILBOX
#define XX_OUTLOOK_EXPRESS_DBX_MAILBOX_FILE_TYPE XX_FILE_TYPE_OUTLOOK_EXPRESS_DBX_MAILBOX
#else
#define XX_OUTLOOK_EXPRESS_DBX_MAILBOX_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define DBX_HEADER_SIZE ((int64_t)XX_OUTLOOK_EXPRESS_DBX_MAILBOX_HEADER_SIZE)
#define DBX_COUNT_OFFSET 0xC4U
#define DBX_ROOT_OFFSET 0xE4U
#define DBX_HEAD_READ 0xE8U
#define DBX_NODE_HEADER 0x18U
#define DBX_NODE_ENTRIES 0x33U
#define DBX_NODE_ENTRY 12U
#define DBX_NODE_SIZE (DBX_NODE_HEADER + DBX_NODE_ENTRIES * DBX_NODE_ENTRY)
#define DBX_MAX_DEPTH 64U
#define DBX_INFO_HEADER 12U
#define DBX_BLOCK_HEADER 16U
/* Outlook Express writes 0x200-byte bodies; anything past 1 MiB is not a
 * message block. */
#define DBX_MAX_BLOCK_BODY 0x100000U
/* 1M records (24 MB of index); a 2 GB store of 2 KB messages holds 1M. */
#define DBX_MAX_MESSAGES ((size_t)0x100000U)
#define DBX_COPY_CHUNK 0x10000U
#define DBX_ATTR_MESSAGE 0x04U

static const uint8_t g_dbx_magic[20] = {0xCF, 0xAD, 0x12, 0xFE, 0xC5, 0xFD, 0x74, 0x6F, 0x66, 0xE3, 0xD1, 0x11, 0x9A, 0x4E, 0x00, 0xC0, 0x4F, 0xD7, 0x5E, 0x5B};

typedef struct dbx_frame_s {
    uint32_t offset;
    int32_t position; /**< -1: the leftmost child is still to be visited. */
    uint32_t used;
    uint8_t node[DBX_NODE_SIZE];
} dbx_frame;

typedef struct dbx_stream_s {
    size_t index;
    size_t count;
} dbx_stream;

typedef struct dbx_parse_s {
    xx_io_device *device;
    int64_t base;
    int64_t size;
    int64_t extent;
    uint64_t visits;
    uint64_t visit_limit;
    uint64_t info_budget;  /**< Info objects still allowed to be read. */
    uint64_t block_budget; /**< Block bytes all chains may still claim. */
    xx_outlook_express_dbx_mailbox_message *messages;
    size_t count;
    size_t capacity;
    size_t limit;
    /* Open-addressing set of the node and first-block offsets already
     * claimed: in a sound store no node is reached twice and no two info
     * objects share a message, so a second claim is refused. */
    uint32_t *seen;
    size_t seen_capacity;
    size_t seen_used;
    bool failed; /**< Allocation failure: the parse as a whole fails. */
} dbx_parse;

static bool dbx_read_at(xx_io_device *device, int64_t offset, void *buffer, size_t size)
{
    size_t done = 0U;
    if (!device || (!buffer && size != 0U) || offset < 0 || xx_io_seek64(device, offset, SEEK_SET) != 0) return false;
    while (done < size) {
        ssize_t amount = xx_io_read(device, (uint8_t *)buffer + done, size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

/* True when [offset, offset + length) lies after the file header and inside
 * the @p size bytes of the store. */
static bool dbx_in_file(int64_t size, uint32_t offset, uint64_t length)
{
    return (int64_t)offset >= DBX_HEADER_SIZE && (uint64_t)offset <= (uint64_t)size && length <= (uint64_t)size - (uint64_t)offset;
}

static void dbx_touch(dbx_parse *parse, uint32_t offset, uint64_t length)
{
    int64_t end = (int64_t)offset + (int64_t)length;
    if (end > parse->size) end = parse->size;
    if (end > parse->extent) parse->extent = end;
}

/* Header: magic, message-store class id, and a root that is either absent
 * or a node that names itself.  This is all check_is_valid reads. */
static bool dbx_header(Abstractformat *format, int64_t *size_out, uint32_t *count_out, uint32_t *root_out)
{
    uint8_t head[DBX_HEAD_READ];
    uint8_t node[DBX_NODE_HEADER];
    int64_t total, size;
    uint32_t root;
    if (!format || !format->device || format->base_address < 0) return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size < DBX_HEADER_SIZE || !dbx_read_at(format->device, format->base_address, head, sizeof(head)) || xx_rt_memcmp(head, g_dbx_magic, sizeof(g_dbx_magic)) != 0)
        return false;
    root = xx_data_get_u32(head + DBX_ROOT_OFFSET, 4, 0, false);
    if (root != 0U) {
        if (!dbx_in_file(size, root, DBX_NODE_HEADER) || !dbx_read_at(format->device, format->base_address + (int64_t)root, node, sizeof(node)) ||
            xx_data_get_u32(node, 4, 0, false) != root || node[0x11] > DBX_NODE_ENTRIES ||
            !dbx_in_file(size, root, DBX_NODE_HEADER + (uint64_t)node[0x11] * DBX_NODE_ENTRY))
            return false;
    }
    if (size_out) *size_out = size;
    if (count_out) *count_out = xx_data_get_u32(head + DBX_COUNT_OFFSET, 4, 0, false);
    if (root_out) *root_out = root;
    return true;
}

/* Walk one block chain.  With @p destination the used bytes are written
 * there.  Fails on a block that is out of the file or does not name itself,
 * on a loop, and on a chain claiming more bytes than the file holds. */
static bool dbx_chain(xx_io_device *device, int64_t base, int64_t size, uint32_t first, xx_io_device *destination, uint64_t *size_out, int64_t *extent, uint64_t *budget,
                      xx_pd_struct *pd)
{
    uint8_t header[DBX_BLOCK_HEADER];
    uint8_t *buffer = NULL;
    uint32_t offset = first, saved = first;
    uint64_t power = 1U, lambda = 0U, total = 0U, consumed = 0U;
    bool result = false;
    if (destination) {
        buffer = (uint8_t *)xx_mem_alloc(DBX_COPY_CHUNK);
        if (!buffer) return false;
    }
    for (;;) {
        uint32_t body, used, next;
        if (pd && xx_pd_is_stopped(pd)) goto done;
        if (!dbx_in_file(size, offset, DBX_BLOCK_HEADER) || !dbx_read_at(device, base + (int64_t)offset, header, sizeof(header)) ||
            xx_data_get_u32(header, 4, 0, false) != offset)
            goto done;
        body = xx_data_get_u32(header + 4U, 4, 0, false);
        used = xx_data_get_u32(header + 8U, 4, 0, false);
        next = xx_data_get_u32(header + 12U, 4, 0, false);
        if (body == 0U || body > DBX_MAX_BLOCK_BODY || used > body || !dbx_in_file(size, offset, (uint64_t)DBX_BLOCK_HEADER + used)) goto done;
        consumed += (uint64_t)DBX_BLOCK_HEADER + used;
        if (consumed > (uint64_t)size) goto done;
        if (budget) {
            if (*budget < (uint64_t)DBX_BLOCK_HEADER + used) goto done;
            *budget -= (uint64_t)DBX_BLOCK_HEADER + used;
        }
        if (extent) {
            int64_t end = (int64_t)offset + DBX_BLOCK_HEADER + (int64_t)body;
            if (end > size) end = size;
            if (end > *extent) *extent = end;
        }
        if (destination && used != 0U) {
            uint32_t done_bytes = 0U;
            while (done_bytes < used) {
                uint32_t piece = used - done_bytes;
                if (piece > DBX_COPY_CHUNK) piece = DBX_COPY_CHUNK;
                if (!dbx_read_at(device, base + (int64_t)offset + DBX_BLOCK_HEADER + (int64_t)done_bytes, buffer, piece) ||
                    xx_io_write(destination, buffer, piece) != (ssize_t)piece)
                    goto done;
                done_bytes += piece;
            }
        }
        total += used;
        if (next == 0U) break;
        /* Brent: the saved block is compared with every later one, and moves
         * ahead whenever the stretch doubles, so any loop is caught. */
        if (next == saved) goto done;
        if (++lambda == power) {
            saved = next;
            power <<= 1U;
            lambda = 0U;
        }
        offset = next;
    }
    if (size_out) *size_out = total;
    result = true;
done:
    if (buffer) xx_mem_free(buffer);
    return result;
}

static size_t dbx_slot(const uint32_t *slots, size_t capacity, uint32_t key)
{
    size_t slot = (size_t)(key * UINT32_C(0x9E3779B1)) & (capacity - 1U);
    while (slots[slot] != 0U && slots[slot] != key) slot = (slot + 1U) & (capacity - 1U);
    return slot;
}

/* Claim offset @p key (never 0: every structure lies past the header).
 * False when it was claimed before or the set cannot grow. */
static bool dbx_claim(dbx_parse *parse, uint32_t key)
{
    size_t slot;
    if ((parse->seen_used + 1U) * 2U > parse->seen_capacity) {
        size_t capacity = parse->seen_capacity ? parse->seen_capacity * 2U : 256U;
        size_t index;
        uint32_t *slots;
        if (capacity > ((size_t)-1) / sizeof(uint32_t) || !(slots = (uint32_t *)xx_mem_calloc(capacity, sizeof(uint32_t)))) {
            parse->failed = true;
            return false;
        }
        for (index = 0U; index < parse->seen_capacity; ++index)
            if (parse->seen[index] != 0U) slots[dbx_slot(slots, capacity, parse->seen[index])] = parse->seen[index];
        if (parse->seen) xx_mem_free(parse->seen);
        parse->seen = slots;
        parse->seen_capacity = capacity;
    }
    slot = dbx_slot(parse->seen, parse->seen_capacity, key);
    if (parse->seen[slot] == key) return false;
    parse->seen[slot] = key;
    ++parse->seen_used;
    return true;
}

static void dbx_add_info(dbx_parse *parse, uint32_t info)
{
    uint8_t header[DBX_INFO_HEADER];
    uint8_t attributes[255U * 4U];
    uint32_t body, count, index, address = 0U;
    bool found = false;
    xx_outlook_express_dbx_mailbox_message *message;
    if (parse->failed || info == 0U || parse->info_budget == 0U) return;
    --parse->info_budget;
    if (!dbx_in_file(parse->size, info, DBX_INFO_HEADER) || !dbx_read_at(parse->device, parse->base + (int64_t)info, header, sizeof(header)) ||
        xx_data_get_u32(header, 4, 0, false) != info)
        return;
    body = xx_data_get_u32(header + 4U, 4, 0, false);
    count = header[0x0A];
    if ((uint64_t)count * 4U > body || !dbx_in_file(parse->size, info, (uint64_t)DBX_INFO_HEADER + body) ||
        !dbx_read_at(parse->device, parse->base + (int64_t)info + DBX_INFO_HEADER, attributes, count * 4U))
        return;
    dbx_touch(parse, info, (uint64_t)DBX_INFO_HEADER + body);
    for (index = 0U; index < count && !found; ++index) {
        uint32_t attribute = xx_data_get_u32(attributes + index * 4U, 4, 0, false);
        uint32_t value = attribute >> 8U;
        if ((attribute & 0x7FU) != DBX_ATTR_MESSAGE) continue;
        if (attribute & 0x80U) {
            address = value;
        } else {
            uint8_t word[4];
            uint64_t data = (uint64_t)count * 4U + value;
            if (data + 4U > body || !dbx_read_at(parse->device, parse->base + (int64_t)info + DBX_INFO_HEADER + (int64_t)data, word, sizeof(word))) return;
            address = xx_data_get_u32(word, 4, 0, false);
        }
        found = true;
    }
    if (!found || address == 0U || parse->count >= parse->limit || !dbx_claim(parse, address)) return;
    if (parse->count == parse->capacity) {
        size_t capacity = parse->capacity ? parse->capacity * 2U : 64U;
        void *grown;
        if (capacity > parse->limit) capacity = parse->limit;
        grown = xx_mem_realloc(parse->messages, capacity * sizeof(*parse->messages));
        if (!grown) {
            parse->failed = true;
            return;
        }
        parse->messages = (xx_outlook_express_dbx_mailbox_message *)grown;
        parse->capacity = capacity;
    }
    message = &parse->messages[parse->count++];
    message->info_offset = info;
    message->first_block = address;
    message->size = 0U;
    message->broken = !dbx_chain(parse->device, parse->base, parse->size, address, NULL, &message->size, &parse->extent, &parse->block_budget, NULL);
}

/* Load node @p offset into @p frame; false for a node that is damaged, out
 * of the file, over budget or already on the path from the root. */
static bool dbx_load_node(dbx_parse *parse, dbx_frame *stack, size_t depth, uint32_t offset)
{
    dbx_frame *frame = &stack[depth];
    size_t index;
    uint32_t used;
    if (depth >= DBX_MAX_DEPTH || ++parse->visits > parse->visit_limit || !dbx_in_file(parse->size, offset, DBX_NODE_HEADER)) return false;
    for (index = 0U; index < depth; ++index)
        if (stack[index].offset == offset) return false;
    if (!dbx_read_at(parse->device, parse->base + (int64_t)offset, frame->node, DBX_NODE_HEADER) || xx_data_get_u32(frame->node, 4, 0, false) != offset ||
        (depth != 0U && xx_data_get_u32(frame->node + 0x0CU, 4, 0, false) != stack[depth - 1U].offset))
        return false;
    used = frame->node[0x11];
    if (used > DBX_NODE_ENTRIES || !dbx_in_file(parse->size, offset, DBX_NODE_HEADER + (uint64_t)used * DBX_NODE_ENTRY) ||
        (used != 0U && !dbx_read_at(parse->device, parse->base + (int64_t)offset + DBX_NODE_HEADER, frame->node + DBX_NODE_HEADER, used * DBX_NODE_ENTRY)) ||
        !dbx_claim(parse, offset))
        return false;
    /* Outlook Express always allocates the full node. */
    dbx_touch(parse, offset, DBX_NODE_SIZE);
    frame->offset = offset;
    frame->position = -1;
    frame->used = used;
    return true;
}

static bool dbx_parse_all(Abstractformat *format, dbx_parse *parse, uint32_t *count_out, uint32_t *root_out, xx_pd_struct *pd)
{
    dbx_frame *stack;
    size_t depth;
    uint32_t root, declared;
    xx_mem_zero(parse, sizeof(*parse));
    if (!dbx_header(format, &parse->size, &declared, &root)) return false;
    parse->device = format->device;
    parse->base = format->base_address;
    parse->extent = DBX_HEADER_SIZE;
    /* Every distinct node takes at least its 0x18-byte header, and every
     * message an info header plus a block header. */
    parse->visit_limit = (uint64_t)parse->size / DBX_NODE_HEADER + 1U;
    /* A B-tree node holds at least one entry, so there are never more
     * nodes than messages. */
    if (parse->visit_limit > DBX_MAX_MESSAGES + 1U) parse->visit_limit = DBX_MAX_MESSAGES + 1U;
    /* In a sound store info objects and blocks never overlap, so all of them
     * together fit in the file; a hostile tree that shares them cannot make
     * the walk quadratic. */
    parse->info_budget = (uint64_t)parse->size / DBX_INFO_HEADER + 1U;
    parse->block_budget = (uint64_t)parse->size;
    parse->limit = (size_t)((uint64_t)parse->size / (DBX_INFO_HEADER + DBX_BLOCK_HEADER));
    if (parse->limit > DBX_MAX_MESSAGES) parse->limit = DBX_MAX_MESSAGES;
    if (count_out) *count_out = declared;
    if (root_out) *root_out = root;
    if (root == 0U) return true;
    stack = (dbx_frame *)xx_mem_calloc(DBX_MAX_DEPTH, sizeof(*stack));
    if (!stack) return false;
    if (!dbx_load_node(parse, stack, 0U, root)) {
        xx_mem_free(stack);
        if (parse->seen) xx_mem_free(parse->seen);
        return false;
    }
    depth = 1U;
    while (depth > 0U && !parse->failed) {
        dbx_frame *frame = &stack[depth - 1U];
        uint32_t child;
        if (pd && xx_pd_is_stopped(pd)) {
            parse->failed = true;
            break;
        }
        if (frame->position < 0) {
            frame->position = 0;
            child = xx_data_get_u32(frame->node + 0x08U, 4, 0, false);
        } else if ((uint32_t)frame->position < frame->used) {
            const uint8_t *entry = frame->node + DBX_NODE_HEADER + (size_t)frame->position * DBX_NODE_ENTRY;
            ++frame->position;
            dbx_add_info(parse, xx_data_get_u32(entry, 4, 0, false));
            child = xx_data_get_u32(entry + 4U, 4, 0, false);
        } else {
            --depth;
            continue;
        }
        if (child != 0U && dbx_load_node(parse, stack, depth, child)) ++depth;
    }
    xx_mem_free(stack);
    if (parse->seen) xx_mem_free(parse->seen);
    parse->seen = NULL;
    if (parse->failed) {
        if (parse->messages) xx_mem_free(parse->messages);
        parse->messages = NULL;
        parse->count = 0U;
        return false;
    }
    return true;
}

static bool dbx_ensure(Abstractformat *format, xx_pd_struct *pd)
{
    xx_outlook_express_dbx_mailbox *archive = (xx_outlook_express_dbx_mailbox *)format;
    dbx_parse parse;
    if (!format) return false;
    if (archive->parsed) return true;
    if (!dbx_parse_all(format, &parse, &archive->declared_count, &archive->root_node, pd)) return false;
    archive->messages = parse.messages;
    archive->message_count = parse.count;
    archive->number_of_records = parse.count;
    archive->parsed = true;
    format->number_of_archive_records = parse.count;
    format->format_size = parse.extent;
    format->is_valid = true;
    format->base_info_handled = true;
    return true;
}

static void dbx_stream_free(void *opaque)
{
    if (opaque) xx_mem_free(opaque);
}

static bool dbx_copy_options(xx_list_s *destination, const xx_list_s *source)
{
    size_t index;
    if (!source) return true;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *original = (const xx_meta *)xx_list_at((const xx_list_t *)source, index);
        xx_meta copy;
        if (!original) continue;
        xx_meta_init(&copy, original->meta_id);
        if (!xx_var_copy(&copy.var, &original->var) || !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

static const xx_var *dbx_option(const xx_list_s *options, uint32_t id)
{
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta = (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

/* Members are named by their position in the tree, so names are always
 * safe and never collide. */
static void dbx_member_name(size_t index, char *name, size_t size)
{
    (void)xx_rt_snprintf(name, size, "%05llu.eml", (unsigned long long)(index + 1U));
}

static bool dbx_set_record(xx_archive_record *record, const xx_outlook_express_dbx_mailbox *archive, size_t index)
{
    const xx_outlook_express_dbx_mailbox_message *message = &archive->messages[index];
    char name[32];
    dbx_member_name(index, name, sizeof(name));
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = archive->format.base_address + (int64_t)message->info_offset;
    record->header_size = DBX_INFO_HEADER;
    record->data_offset = archive->format.base_address + (int64_t)message->first_block + DBX_BLOCK_HEADER;
    record->compressed_size = (int64_t)message->size;
    return xx_archive_record_set_original_name(record, name) && xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE, message->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE, message->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD, 0U) && xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED, false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

void xx_outlook_express_dbx_mailbox_init(xx_outlook_express_dbx_mailbox *archive, xx_io_device *device, int64_t base_address)
{
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_OUTLOOK_EXPRESS_DBX_MAILBOX_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-dbx");
    xx_format_set_extension(&archive->format, "dbx");
    archive->format.check_is_valid = xx_outlook_express_dbx_mailbox_check_is_valid;
    archive->format.handle_base_info = xx_outlook_express_dbx_mailbox_handle_base_info;
    archive->format.get_format_size = xx_outlook_express_dbx_mailbox_get_format_size;
    archive->format.get_number_of_archive_records = xx_outlook_express_dbx_mailbox_get_number_of_archive_records;
    archive->format.create_archive_records_reading = xx_outlook_express_dbx_mailbox_create_archive_records_reading;
    archive->format.get_current_archive_record = xx_outlook_express_dbx_mailbox_get_current_archive_record;
    archive->format.unpack_current_archive_record = xx_outlook_express_dbx_mailbox_unpack_current_archive_record;
    archive->format.archive_record_move_to_next = xx_outlook_express_dbx_mailbox_archive_record_move_to_next;
    archive->format.free_archive_records_reading = xx_outlook_express_dbx_mailbox_free_archive_records_reading;
}

xx_outlook_express_dbx_mailbox *xx_outlook_express_dbx_mailbox_create(xx_io_device *device, int64_t base_address)
{
    xx_outlook_express_dbx_mailbox *archive = (xx_outlook_express_dbx_mailbox *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_outlook_express_dbx_mailbox_init(archive, device, base_address);
    return archive;
}

void xx_outlook_express_dbx_mailbox_destroy(xx_outlook_express_dbx_mailbox *archive)
{
    if (!archive) return;
    if (archive->messages) xx_mem_free(archive->messages);
    archive->messages = NULL;
    archive->message_count = 0U;
    archive->parsed = false;
    xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_outlook_express_dbx_mailbox_free(xx_outlook_express_dbx_mailbox *archive)
{
    if (!archive) return;
    xx_outlook_express_dbx_mailbox_destroy(archive);
    xx_mem_free(archive);
}

bool xx_outlook_express_dbx_mailbox_check_is_valid(Abstractformat *format, xx_pd_struct *pd)
{
    (void)pd;
    return dbx_header(format, NULL, NULL, NULL);
}

bool xx_outlook_express_dbx_mailbox_handle_base_info(Abstractformat *format, xx_pd_struct *pd)
{
    return dbx_ensure(format, pd);
}

int64_t xx_outlook_express_dbx_mailbox_get_format_size(Abstractformat *format, xx_pd_struct *pd)
{
    return format && dbx_ensure(format, pd) ? format->format_size : -1;
}

uint64_t xx_outlook_express_dbx_mailbox_get_number_of_archive_records(Abstractformat *format, xx_pd_struct *pd)
{
    return format && dbx_ensure(format, pd) ? ((xx_outlook_express_dbx_mailbox *)format)->number_of_records : 0U;
}

bool xx_outlook_express_dbx_mailbox_unpack_message(xx_outlook_express_dbx_mailbox *archive, size_t index, xx_io_device *destination, xx_pd_struct *pd)
{
    const xx_outlook_express_dbx_mailbox_message *message;
    int64_t size;
    uint64_t produced = 0U;
    if (!archive || !dbx_ensure(&archive->format, pd) || index >= archive->message_count) return false;
    message = &archive->messages[index];
    if (message->broken || !dbx_header(&archive->format, &size, NULL, NULL)) return false;
    return dbx_chain(archive->format.device, archive->format.base_address, size, message->first_block, destination, &produced, NULL, NULL, pd) &&
           produced == message->size;
}

xx_archive_record_state *xx_outlook_express_dbx_mailbox_create_archive_records_reading(Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd)
{
    xx_outlook_express_dbx_mailbox *archive = (xx_outlook_express_dbx_mailbox *)format;
    dbx_stream *stream;
    xx_archive_record_state *state;
    if (!format || !dbx_ensure(format, pd)) return NULL;
    stream = (dbx_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return NULL;
    stream->count = archive->message_count;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_mem_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = dbx_stream_free;
    state->total_records = stream->count;
    if (!dbx_copy_options(&state->options, options)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    if (stream->count != 0U) {
        if (!dbx_set_record(&state->current_record, archive, 0U)) {
            xx_archive_record_state_free(state);
            return NULL;
        }
        state->has_record = true;
    }
    return state;
}

const xx_archive_record *xx_outlook_express_dbx_mailbox_get_current_archive_record(Abstractformat *format, xx_archive_record_state *state)
{
    return format && state && state->format == format && state->has_record ? &state->current_record : NULL;
}

bool xx_outlook_express_dbx_mailbox_archive_record_move_to_next(Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd)
{
    dbx_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format || !(stream = (dbx_stream *)state->internal_state) || !((xx_outlook_express_dbx_mailbox *)format)->parsed ||
        stream->index + 1U >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++stream->index;
    if (!dbx_set_record(&state->current_record, (xx_outlook_express_dbx_mailbox *)format, stream->index)) {
        state->has_record = false;
        return false;
    }
    state->has_record = true;
    return true;
}

bool xx_outlook_express_dbx_mailbox_unpack_current_archive_record(Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd)
{
    xx_outlook_express_dbx_mailbox *archive = (xx_outlook_express_dbx_mailbox *)format;
    dbx_stream *stream;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    char name[32];
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record || !(stream = (dbx_stream *)state->internal_state) || stream->index >= stream->count ||
        (pd && xx_pd_is_stopped(pd)))
        return false;
    path_option = dbx_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) return xx_outlook_express_dbx_mailbox_unpack_message(archive, stream->index, NULL, pd);
    if (archive->messages[stream->index].broken) return false;
    dbx_member_name(stream->index, name, sizeof(name));
    if (path_option->type == XX_VAR_TYPE_STRING || path_option->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(path_option);
    } else if (path_option->type == XX_VAR_TYPE_WSTRING || path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base = owned_base;
    }
    if (!base) goto done;
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' && base[xx_str_len(base) - 1U] != '\\') ? xx_str_concat3(base, "/", name) : xx_str_concat(base, name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        if (!destination) goto done;
        created = true;
        result = xx_outlook_express_dbx_mailbox_unpack_message(archive, stream->index, destination, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_outlook_express_dbx_mailbox_free_archive_records_reading(Abstractformat *format, xx_archive_record_state *state)
{
    (void)format;
    xx_archive_record_state_free(state);
}
