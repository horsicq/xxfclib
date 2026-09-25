/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * IrwinPac installation files (*.DL_, *.EX_ and friends).
 *
 * Header, 20 bytes at the base address:
 *
 *   0x00  "IrwinPac"      8 ASCII bytes, no terminator
 *   0x08  u16 LE          the header's own size, always 20
 *   0x0A  10 bytes        not inspected
 *
 * Everything after it is a chain of independent blocks running to the end of
 * the file:
 *
 *   0x00  u16 LE  flag, 0 = stored, 1 = compressed
 *   0x02  u16 LE  chunk size, INCLUDING this 10-byte header
 *   0x04  u16 LE  unpacked size of the block, 16384 for all but the last
 *   0x06  4       uninitialised encoder scratch, constant per file and never
 *                 read -- it differs between files that decode identically
 *   0x0A          payload, chunk size - 10 bytes
 *
 * There is no terminator record and no member count: the chain must consume
 * the file exactly, and a chain that overshoots the end or stops short of it
 * is not this format.
 *
 * A compressed payload is a bit stream consumed MSB-first inside each byte
 * and restarted byte-aligned at every block. Matches never reach across a
 * block boundary, so a block's whole history is the block itself:
 *
 *   0 + <8 bits>                literal byte
 *   1 + 1 + <7 bits>            match, distance 1..127
 *   1 + 0 + <11 bits>           match, distance 1..2047
 *   then the length, a nibble-escalating code:
 *       <2 bits> v, v < 3       -> 2 + v
 *       else <2 bits> v, v < 3  -> 5 + v
 *       else <4 bits> v, v < 15 -> 8 + v, and while v == 15 add 15 to the
 *                                 base and read the next nibble
 *
 * The encoder pads the tail of every block with about a dozen spare bytes, so
 * the block's unpacked size -- not stream exhaustion -- is the stop condition.
 *
 * The container stores no name: the installer script decides the target name,
 * and the file's own name on disk is the only label a real extractor has.
 * Nothing of that reaches a reader working from a device, so the single
 * member gets a fixed name.
 *
 * The uncompressed size is not a stored field either; it is the sum of the
 * per-block unpacked sizes, which the chain walk accumulates.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/irwinpac/xx_irwinpac.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/irwinpac/xx_irwinpac.h"
#include <stdio.h>

#define XX_IRWINPAC_COPY_CHUNK (64 * 1024)

typedef struct xx_irwinpac_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_irwinpac_member;

typedef struct xx_irwinpac_stream_s {
    xx_irwinpac_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_irwinpac_stream;

static void xx_irwinpac_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_irwinpac_read_at(Abstractformat *self, int64_t offset,
                              uint8_t *buffer, size_t size) {
    size_t completed = 0U;

    if (!self || !self->device || offset < 0 ||
        xx_io_seek64(self->device, offset, SEEK_SET) != 0) {
        return false;
    }
    while (completed < size) {
        ssize_t received =
            xx_io_read(self->device, buffer + completed, size - completed);
        if (received <= 0 || (size_t)received > size - completed) {
            return false;
        }
        completed += (size_t)received;
    }
    return true;
}

static bool xx_irwinpac_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_irwinpac_path_safe(const char *name) {
    const char *cursor = name;

    if (!name || !name[0] || name[0] == '/') return false;
    while (*cursor) {
        const char *end = cursor;
        size_t length;
        while (*end && *end != '/') ++end;
        length = (size_t)(end - cursor);
        if (length == 2U && cursor[0] == '.' && cursor[1] == '.') return false;
        cursor = *end ? end + 1 : end;
    }
    return true;
}

static void xx_irwinpac_stream_free(void *pointer) {
    xx_irwinpac_stream *stream = (xx_irwinpac_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_irwinpac_add(xx_irwinpac_stream *stream,
                          const xx_irwinpac_member *member) {
    xx_irwinpac_member *grown = (xx_irwinpac_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


#define XX_IRWINPAC_MAGIC_SIZE 8
#define XX_IRWINPAC_HEADER_SIZE 20
#define XX_IRWINPAC_CHUNK_HEADER_SIZE 10
#define XX_IRWINPAC_MAX_BLOCK 65536
#define XX_IRWINPAC_MAX_BLOCKS (1 << 20)
#define XX_IRWINPAC_MAX_MEMBERS 1
#define XX_IRWINPAC_METHOD_CHUNKED 1U
#define XX_IRWINPAC_MEMBER_NAME "irwinpac.bin"
#define XX_IRWINPAC_MAX_DECODED (256 * 1024 * 1024)

/* Forward declarations: the parse and the decode
 * call into each other's helpers. */
static uint16_t xx_irwinpac_le16(const uint8_t *data);
static bool xx_irwinpac_probe_first(Abstractformat *self, int64_t offset, int64_t chunk_size, int64_t unpacked_size, xx_pd_struct *pd);
static xx_irwinpac_stream *xx_irwinpac_parse(Abstractformat *self, xx_pd_struct *pd);
static bool xx_irwinpac_decode(Abstractformat *self, const xx_irwinpac_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd);


/* The block size field is a u16, so a block can never claim more than this;
 * the constant bounds the probe buffer. */
/* The container holds exactly one logical member with no method field of its
 * own -- the stored/compressed flag is per block, inside the chain -- so this
 * names the framing rather than a container method, and the decode refuses
 * anything else. */

static const uint8_t xx_irwinpac_signature[XX_IRWINPAC_MAGIC_SIZE] = {
    'I', 'r', 'w', 'i', 'n', 'P', 'a', 'c'};

static uint16_t xx_irwinpac_le16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

/* Run the real decoder over the FIRST block only. The chain has no
 * terminator, so a clean end of input on a block boundary is how a member
 * finishes -- which means handing the entry point exactly one chunk decodes
 * that chunk and stops. Random data essentially never produces a token stream
 * whose back-references all point behind the write pointer and whose length
 * codes land exactly on the declared unpacked size, so this is what keeps a
 * chain walk that happens to end on EOF from being published as an archive.
 * Only the first block is probed: the walk is otherwise header-only, and
 * decoding the whole file at parse time would make detection cost O(file). */
static bool xx_irwinpac_probe_first(Abstractformat *self, int64_t offset,
                                    int64_t chunk_size,
                                    int64_t unpacked_size,
                                    xx_pd_struct *pd) {
    uint8_t *chunk = NULL;
    size_t consumed = 0U;
    size_t produced = 0U;
    bool ok;

    if (chunk_size <= 0 || chunk_size > XX_IRWINPAC_MAX_BLOCK +
                                            XX_IRWINPAC_CHUNK_HEADER_SIZE) {
        return false;
    }
    chunk = (uint8_t *)xx_mem_alloc((size_t)chunk_size);
    if (!chunk) return false;
    if (!xx_irwinpac_read_at(self, offset, chunk, (size_t)chunk_size)) {
        xx_mem_free(chunk);
        return false;
    }
    if (pd && xx_pd_is_stopped(pd)) {
        xx_mem_free(chunk);
        return false;
    }
    ok = xx_irwinpac_scan_memory(chunk, (size_t)chunk_size,
                                 (size_t)unpacked_size, &consumed, &produced);
    xx_mem_free(chunk);
    /* The block must account for its whole chunk and produce exactly the size
     * its header declared; either disagreement means the walk misread the
     * framing. */
    return ok && consumed == (size_t)chunk_size &&
           produced == (size_t)unpacked_size;
}

static xx_irwinpac_stream *xx_irwinpac_parse(Abstractformat *self,
                                             xx_pd_struct *pd) {
    xx_irwinpac_stream *stream = NULL;
    xx_irwinpac_member member;
    char *name = NULL;
    uint8_t header[XX_IRWINPAC_HEADER_SIZE];
    uint8_t chunk[XX_IRWINPAC_CHUNK_HEADER_SIZE];
    int64_t total;
    int64_t span;
    int64_t offset;
    int64_t blocks = 0;
    int64_t uncompressed_size = 0;
    int64_t chunk_size;
    int64_t unpacked_size;
    int64_t payload_size;
    uint16_t flag;
    bool probed = false;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < XX_IRWINPAC_HEADER_SIZE + XX_IRWINPAC_CHUNK_HEADER_SIZE) {
        return NULL;
    }
    if (!xx_irwinpac_read_at(self, self->base_address, header,
                             sizeof(header))) {
        return NULL;
    }
    if (xx_rt_memcmp(header, xx_irwinpac_signature,
                     sizeof(xx_irwinpac_signature)) != 0) {
        return NULL;
    }
    /* The header states its own length, and it is 20 in every known file.
     * Checking it costs nothing and rules out anything that merely happens to
     * begin with the eight signature bytes. */
    if ((int64_t)xx_irwinpac_le16(header + 8) != XX_IRWINPAC_HEADER_SIZE) {
        return NULL;
    }

    /* Walk the chain on its 10-byte headers alone. It must consume the file
     * EXACTLY: that arithmetic, more than the signature, is what identifies
     * the container, and a chain landing one byte either side of EOF is a
     * rejection. */
    offset = XX_IRWINPAC_HEADER_SIZE;
    while (offset < span) {
        if (pd && xx_pd_is_stopped(pd)) return NULL;
        if (blocks >= XX_IRWINPAC_MAX_BLOCKS) return NULL;
        if (!xx_irwinpac_range_within(span, offset,
                                      XX_IRWINPAC_CHUNK_HEADER_SIZE)) {
            return NULL;
        }
        if (!xx_irwinpac_read_at(self, self->base_address + offset, chunk,
                                 sizeof(chunk))) {
            return NULL;
        }
        flag = xx_irwinpac_le16(chunk);
        chunk_size = (int64_t)xx_irwinpac_le16(chunk + 2);
        unpacked_size = (int64_t)xx_irwinpac_le16(chunk + 4);

        if (flag > 1U) return NULL;
        /* A chunk that is only its own header carries nothing and would let a
         * run of zeros walk the chain forever at 10 bytes a step. */
        if (chunk_size <= XX_IRWINPAC_CHUNK_HEADER_SIZE) return NULL;
        if (unpacked_size == 0) return NULL;
        payload_size = chunk_size - XX_IRWINPAC_CHUNK_HEADER_SIZE;
        if (!xx_irwinpac_range_within(span, offset, chunk_size)) return NULL;
        /* A stored block is a verbatim copy, so its two sizes must agree. */
        if (flag == 0U && payload_size != unpacked_size) return NULL;

        if (!probed) {
            if (!xx_irwinpac_probe_first(self, self->base_address + offset,
                                         chunk_size, unpacked_size, pd)) {
                return NULL;
            }
            probed = true;
        }

        uncompressed_size += unpacked_size;
        offset += chunk_size;
        ++blocks;
    }
    if (offset != span || blocks == 0) return NULL;

    stream = (xx_irwinpac_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    if (stream->count >= (size_t)XX_IRWINPAC_MAX_MEMBERS) goto fail;
    name = xx_str_dup(XX_IRWINPAC_MEMBER_NAME);
    if (!name) goto fail;

    xx_mem_zero(&member, sizeof(member));
    member.name = name;
    member.header_offset = self->base_address;
    member.header_size = XX_IRWINPAC_HEADER_SIZE;
    /* The decoder wants the chain from its first chunk header, so the
     * published extent starts after the container header and runs to EOF. */
    member.data_offset = self->base_address + XX_IRWINPAC_HEADER_SIZE;
    member.compressed_size = span - XX_IRWINPAC_HEADER_SIZE;
    /* Not a stored field: the sum the chain walk accumulated. */
    member.uncompressed_size = uncompressed_size;
    member.method = XX_IRWINPAC_METHOD_CHUNKED;
    /* The container carries no timestamp of any kind. */
    member.timestamp = 0U;
    member.is_folder = false;

    if (!xx_irwinpac_add(stream, &member)) goto fail;
    name = NULL;

    stream->archive_size = span;
    return stream;

fail:
    xx_str_free(name);
    xx_irwinpac_stream_free(stream);
    return NULL;
}


/* Both sides of the decode are bounded by the container, which is
 * attacker-controlled, so both are capped before they become allocations. */

static bool xx_irwinpac_decode(Abstractformat *self,
                               const xx_irwinpac_member *member,
                               uint8_t **out, size_t *out_size,
                               xx_pd_struct *pd) {
    uint8_t *packed = NULL;
    uint8_t *plain = NULL;
    size_t written = 0U;

    *out = NULL;
    *out_size = 0U;
    if (!member || (pd && xx_pd_is_stopped(pd))) return false;
    /* The only framing this reader publishes; the per-block stored flag is
     * inside the chain and the entry point handles it. */
    if (member->method != XX_IRWINPAC_METHOD_CHUNKED) return false;
    if (member->compressed_size <= 0 ||
        member->compressed_size > (int64_t)XX_IRWINPAC_MAX_DECODED) {
        return false;
    }
    if (member->uncompressed_size <= 0 ||
        member->uncompressed_size > (int64_t)XX_IRWINPAC_MAX_DECODED) {
        return false;
    }

    packed = (uint8_t *)xx_mem_alloc((size_t)member->compressed_size);
    if (!packed) return false;
    if (!xx_irwinpac_read_at(self, member->data_offset, packed,
                             (size_t)member->compressed_size)) {
        xx_mem_free(packed);
        return false;
    }
    if (pd && xx_pd_is_stopped(pd)) {
        xx_mem_free(packed);
        return false;
    }

    plain = (uint8_t *)xx_mem_alloc((size_t)member->uncompressed_size);
    if (!plain) {
        xx_mem_free(packed);
        return false;
    }
    /* The parse derived the output size by summing the chain's own per-block
     * unpacked sizes, so the decoder must land on exactly that number. A
     * shorter result means the two walks disagree, which is corruption --
     * never a partial success to be handed back. */
    if (!xx_irwinpac_decode_memory(packed, (size_t)member->compressed_size,
                                   plain, (size_t)member->uncompressed_size,
                                   &written) ||
        written != (size_t)member->uncompressed_size) {
        xx_mem_free(plain);
        xx_mem_free(packed);
        return false;
    }
    xx_mem_free(packed);
    *out = plain;
    *out_size = (size_t)member->uncompressed_size;
    return true;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_irwinpac_init(xx_irwinpac *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_IRWINPAC;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-irwinpac");
    xx_format_set_extension(&archive->format, "bin");
    archive->format.check_is_valid = xx_irwinpac_check_is_valid;
    archive->format.handle_base_info = xx_irwinpac_handle_base_info;
    archive->format.get_format_size = xx_irwinpac_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_irwinpac_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_irwinpac_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_irwinpac_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_irwinpac_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_irwinpac_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_irwinpac_free_archive_records_reading;
    archive->format.destroy = xx_irwinpac_vtable_destroy;
}

xx_irwinpac *xx_irwinpac_create(xx_io_device *device, int64_t base_address) {
    xx_irwinpac *archive = (xx_irwinpac *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_irwinpac_init(archive, device, base_address);
    return archive;
}

void xx_irwinpac_destroy(xx_irwinpac *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_irwinpac_free(xx_irwinpac *archive) {
    if (!archive) return;
    xx_irwinpac_destroy(archive);
    xx_mem_free(archive);
}

static void xx_irwinpac_vtable_destroy(Abstractformat *self) {
    xx_irwinpac_destroy((xx_irwinpac *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_irwinpac_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_irwinpac_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_irwinpac_parse(self, pd);
    if (!stream) return false;
    xx_irwinpac_stream_free(stream);
    return true;
}

bool xx_irwinpac_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_irwinpac *archive = (xx_irwinpac *)self;
    xx_irwinpac_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_irwinpac_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_irwinpac_stream_free(stream);
    return true;
}

int64_t xx_irwinpac_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_irwinpac_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_irwinpac *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_irwinpac_set_record(xx_archive_record *record,
                                 const xx_irwinpac_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = member->header_size;
    record->data_offset = member->data_offset;
    record->compressed_size = member->compressed_size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->compressed_size) &&
           xx_archive_record_set_meta_u64(
               record, XX_META_ID_UNCOMPRESSED_SIZE,
               (uint64_t)member->uncompressed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          member->method) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP,
                                          member->timestamp) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           member->is_folder) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

static bool xx_irwinpac_copy_options(xx_list_s *target,
                                   const xx_list_s *options) {
    size_t index;

    if (!target || !options) return options == NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *source =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        xx_meta copied;
        if (!source) continue;
        xx_meta_init(&copied, source->meta_id);
        if (!xx_var_copy(&copied.var, &source->var) ||
            !xx_list_append(target, &copied)) {
            xx_meta_cleanup(&copied);
            return false;
        }
    }
    return true;
}

static const xx_var *xx_irwinpac_get_option(const xx_list_s *options,
                                          uint32_t meta_id) {
    size_t index;

    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == meta_id) return &meta->var;
    }
    return NULL;
}

xx_archive_record_state *xx_irwinpac_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_irwinpac_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_irwinpac_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_irwinpac_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_irwinpac_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_irwinpac_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_irwinpac_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_irwinpac_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_irwinpac_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_irwinpac_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_irwinpac_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_irwinpac_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_irwinpac_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_irwinpac_stream *stream;
    const xx_irwinpac_member *member;
    const xx_var *path_option;
    const char *base_path = NULL;
    char *converted_path = NULL;
    char *target_path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U;
    bool result = false;
    bool created = false;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_irwinpac_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_irwinpac_path_safe(member->name)) return false;

    path_option = xx_irwinpac_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_irwinpac_decode(self, member, &plain, &plain_size, pd);
        xx_mem_free(plain);
        return result;
    }
    if (path_option->type == XX_VAR_TYPE_STRING ||
        path_option->type == XX_VAR_TYPE_STRING_VIEW) {
        base_path = xx_var_get_str(path_option);
    } else if (path_option->type == XX_VAR_TYPE_WSTRING ||
               path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        converted_path = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base_path = converted_path;
    }
    if (!base_path) {
        xx_str_free(converted_path);
        return false;
    }
    if (base_path[0] != '\0' &&
        base_path[xx_str_len(base_path) - 1U] != '/' &&
        base_path[xx_str_len(base_path) - 1U] != '\\') {
        target_path = xx_str_concat3(base_path, "/", member->name);
    } else {
        target_path = xx_str_concat(base_path, member->name);
    }
    xx_str_free(converted_path);
    if (!target_path) return false;

    if (member->is_folder) {
        result = xx_store_create_dirs_a(target_path, true);
        xx_str_free(target_path);
        return result;
    }
    if (!xx_store_create_dirs_a(target_path, false) ||
        !xx_irwinpac_decode(self, member, &plain, &plain_size, pd)) {
        xx_str_free(target_path);
        return false;
    }
    {
        xx_io_device *output = xx_io_file_open(target_path, "wb");
        created = output != NULL;
        size_t completed = 0U;

        result = output != NULL;
        while (result && completed < plain_size) {
            ssize_t sent = xx_io_write(output, plain + completed,
                                       plain_size - completed);
            if (sent <= 0 || (size_t)sent > plain_size - completed) {
                result = false;
                break;
            }
            completed += (size_t)sent;
        }
        if (output && xx_io_close(output) != 0) result = false;
    }
    xx_mem_free(plain);
    if (!result && created) xx_rt_remove(target_path);
    xx_str_free(target_path);
    return result;
}

void xx_irwinpac_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
