/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Corel / LEAD Technologies "LTEC" installer archives (SETUP.LTA, CorelDRAW
 * 4-6 era). The codec is xx_corelltec_decode_member().
 *
 *   header, 9 bytes at offset 0:
 *     0x00  "LTEC"
 *     0x04  u32 LE   meaning not established (it is neither the archive size,
 *                    the plaintext size nor any block size)
 *     0x08  u8       always zero
 *
 *   directory, starting at 0x09 and running straight into the payload:
 *     0x00  u16 LE   record length = 14 + strlen(name) + 1
 *     0x02  u32 LE   block offset, relative to the first byte AFTER the
 *                    directory
 *     0x06  u32 LE   the member's offset inside that block's PLAINTEXT
 *     0x0a  u32 LE   member size, plaintext bytes
 *     0x0e  char[]   NUL-terminated name, 8.3 upper case
 *
 * There is no directory terminator and no member count: the directory ends
 * where the first block begins, so the last record is the one whose successor
 * is no longer well formed. That is only safe to decide because the two offset
 * fields are fully redundant -- consecutive members of one block satisfy
 * offset == previous offset + previous size exactly, and the first member of a
 * block restarts at zero. The walk enforces that chain, so a stray "LTEC"
 * cannot be followed into a plausible directory.
 *
 * The blocks are SOLID and they OVERLAP: the recorded offset of the next block
 * is the encoder's flushed-byte count, two behind its bit register, so the
 * last symbols of a block live in the first bytes of the next one. Each block
 * is therefore handed to the codec with a few bytes of tail past its own
 * extent (XX_CORELLTEC_BLOCK_TAIL); feeding only the accounted size decodes
 * the tail of most blocks wrongly while the rest of the file still looks
 * right.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/corelltec/xx_corelltec.h"

#include "xxfclib/algo/corelltec/xx_corelltec.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

/* The alias macro is defined next to the enumerator in xxfc_defs.h, so testing
 * for it picks up the real file type as soon as CORELLTEC is registered there.
 * See the port report for the registration this needs. */
#ifdef CORELLTEC
#define XX_CORELLTEC_FILE_TYPE XX_FILE_TYPE_CORELLTEC
#else
#define XX_CORELLTEC_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define XX_CORELLTEC_HEADER_SIZE 9
#define XX_CORELLTEC_RECORD_FIXED_SIZE 14 /* u16 len + three u32 fields */
#define XX_CORELLTEC_MIN_NAME_LENGTH 1
/* The longest name in the reference corpus is twelve ("SETUPAPI.IN_"); the cap
 * is loose enough to survive a longer one and tight enough that random bytes
 * cannot pass as a record. */
#define XX_CORELLTEC_MAX_NAME_LENGTH 64
#define XX_CORELLTEC_MAX_MEMBERS 0x40000
#define XX_CORELLTEC_MAX_MEMBER_SIZE ((int64_t)0x40000000) /* 1 GB */
#define XX_CORELLTEC_MAX_BLOCK_SIZE ((int64_t)0x40000000)  /* 1 GB */
/* Two bytes is the measured maximum overlap; four is handed to the codec so a
 * variant with a slightly deeper bit register still decodes, at the cost of
 * two bytes it will simply not read. */
#define XX_CORELLTEC_BLOCK_TAIL 4
/* check_is_valid() trial-decodes this much of the first block: enough to walk
 * the 16-bit prelude, all three Huffman tables and several thousand symbols,
 * and small enough to stay cheap on a file that only happens to start
 * "LTEC". */
#define XX_CORELLTEC_PROBE_SIZE 4096
/* ...reading at most this many packed bytes to do it. */
#define XX_CORELLTEC_PROBE_READ 0x20000

typedef struct xx_corelltec_block_s {
    int64_t file_offset;      /* absolute offset of the packed block */
    int64_t compressed_size;  /* packed bytes the directory accounts for */
    int64_t stream_size;      /* what the codec actually has to see */
    int64_t uncompressed_size;/* plaintext bytes */
} xx_corelltec_block;

typedef struct xx_corelltec_member_s {
    char *name;
    int64_t record_offset;
    int64_t record_size;
    int64_t offset_in_block;
    int64_t size;
    int32_t block_index;
} xx_corelltec_member;

typedef struct xx_corelltec_stream_s {
    xx_corelltec_member *items;
    size_t count;
    size_t index;
    xx_corelltec_block *blocks;
    size_t block_count;
    int64_t directory_size;
    int64_t archive_size;
} xx_corelltec_stream;

static void xx_corelltec_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static uint16_t xx_corelltec_le16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t xx_corelltec_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static bool xx_corelltec_read_at(Abstractformat *self, int64_t offset,
                                 uint8_t *buffer, size_t size) {
    size_t completed = 0U;

    if (!self || !self->device || offset < 0 ||
        xx_io_seek64(self->device, offset, SEEK_SET) != 0) {
        return false;
    }
    while (completed < size) {
        ssize_t received =
            xx_io_read(self->device, buffer + completed, size - completed);
        if (received <= 0 || (size_t)received > size - completed) return false;
        completed += (size_t)received;
    }
    return true;
}

static bool xx_corelltec_range_within(int64_t total, int64_t offset,
                                      int64_t size) {
    return total >= 0 && offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Names are 8.3, upper case, and the corpus uses only A-Z 0-9 '.' '_'. The
 * format stores no directories, so a path separator means the record is not a
 * record rather than that the member lives in a subdirectory. */
static bool xx_corelltec_name_valid(const uint8_t *name, size_t size) {
    size_t index;

    if (size < (size_t)XX_CORELLTEC_MIN_NAME_LENGTH ||
        size > (size_t)XX_CORELLTEC_MAX_NAME_LENGTH) {
        return false;
    }
    for (index = 0U; index < size; ++index) {
        const uint8_t character = name[index];
        if (character < 0x20U || character > 0x7eU) return false;
        if (character == '/' || character == '\\' || character == ':') {
            return false;
        }
    }
    return true;
}

/* The name check above already refuses every separator, so this only has to
 * catch the two relative names that are otherwise well formed. */
static bool xx_corelltec_path_safe(const char *name) {
    if (!name || !name[0]) return false;
    if (name[0] == '.' &&
        (name[1] == '\0' || (name[1] == '.' && name[2] == '\0'))) {
        return false;
    }
    return true;
}

static void xx_corelltec_stream_free(void *pointer) {
    xx_corelltec_stream *stream = (xx_corelltec_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream->blocks);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of member->name. */
static bool xx_corelltec_add_member(xx_corelltec_stream *stream,
                                    const xx_corelltec_member *member) {
    xx_corelltec_member *grown = (xx_corelltec_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}

static bool xx_corelltec_add_block(xx_corelltec_stream *stream,
                                   const xx_corelltec_block *block) {
    xx_corelltec_block *grown = (xx_corelltec_block *)xx_mem_realloc(
        stream->blocks, sizeof(*grown) * (stream->block_count + 1U));

    if (!grown) return false;
    stream->blocks = grown;
    stream->blocks[stream->block_count++] = *block;
    return true;
}

/* ------------------------------------------------------------ the probe -- */

/* Bounded trial decode of the first block. The directory arithmetic is already
 * strong, but a container that parses and then emits garbage at exit 0 is
 * worse than no support, so the codec has to agree before the file is
 * claimed. */
static bool xx_corelltec_probe(Abstractformat *self,
                               const xx_corelltec_block *block) {
    uint8_t *packed;
    uint8_t *plain;
    size_t read_size;
    size_t probe_size;
    size_t written = 0U;
    bool result;

    if (!self || !block) return false;
    read_size = (size_t)((block->stream_size < XX_CORELLTEC_PROBE_READ)
                             ? block->stream_size
                             : XX_CORELLTEC_PROBE_READ);
    probe_size = (size_t)((block->uncompressed_size < XX_CORELLTEC_PROBE_SIZE)
                              ? block->uncompressed_size
                              : XX_CORELLTEC_PROBE_SIZE);
    if (read_size == 0U || probe_size == 0U) return false;

    packed = (uint8_t *)xx_mem_alloc(read_size);
    if (!packed) return false;
    if (!xx_corelltec_read_at(self, block->file_offset, packed, read_size)) {
        xx_mem_free(packed);
        return false;
    }
    plain = (uint8_t *)xx_mem_alloc(probe_size);
    if (!plain) {
        xx_mem_free(packed);
        return false;
    }
    /* decode_memory is the whole-block entry point: asked for a prefix of the
     * block's plaintext it produces exactly that prefix, which is what a probe
     * wants. A short read of the packed stream is fine -- the probe stops long
     * before the block's end. */
    result = xx_corelltec_decode_memory(packed, read_size, plain, probe_size,
                                        &written) &&
             written == probe_size;
    xx_mem_free(plain);
    xx_mem_free(packed);
    return result;
}

/* --------------------------------------------------------------- parse -- */

static xx_corelltec_stream *xx_corelltec_parse(Abstractformat *self,
                                               bool probe_stream,
                                               xx_pd_struct *pd) {
    xx_corelltec_stream *stream = NULL;
    xx_corelltec_member member;
    xx_corelltec_block block;
    uint8_t header[XX_CORELLTEC_HEADER_SIZE];
    uint8_t record[XX_CORELLTEC_RECORD_FIXED_SIZE];
    uint8_t *name_field = NULL;
    int64_t total;
    int64_t span;
    int64_t offset;
    int64_t current_block_offset = -1;
    int64_t expected_in_block = 0;
    int64_t payload_offset;
    int64_t payload_size;
    size_t index;
    char *name = NULL;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < (int64_t)(XX_CORELLTEC_HEADER_SIZE +
                         XX_CORELLTEC_RECORD_FIXED_SIZE + 2)) {
        return NULL;
    }
    if (pd && xx_pd_is_stopped(pd)) return NULL;
    if (!xx_corelltec_read_at(self, self->base_address, header,
                              sizeof(header))) {
        return NULL;
    }
    if (xx_rt_memcmp(header, "LTEC", 4U) != 0) return NULL;
    /* The ninth byte is zero in every known sample and is part of the gate:
     * without it "LTEC" plus four arbitrary bytes is a two-in-a-billion match
     * away from being walked as a directory. */
    if (header[8] != 0U) return NULL;

    stream = (xx_corelltec_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    offset = (int64_t)XX_CORELLTEC_HEADER_SIZE;
    for (;;) {
        int64_t record_size;
        int64_t block_offset;
        int64_t offset_in_block;
        int64_t member_size;
        int64_t name_size;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (!xx_corelltec_range_within(span, offset,
                                       XX_CORELLTEC_RECORD_FIXED_SIZE)) {
            break;
        }
        if (!xx_corelltec_read_at(self, self->base_address + offset, record,
                                  sizeof(record))) {
            goto fail;
        }
        record_size = (int64_t)xx_corelltec_le16(record);
        block_offset = (int64_t)xx_corelltec_le32(record + 2);
        offset_in_block = (int64_t)xx_corelltec_le32(record + 6);
        member_size = (int64_t)xx_corelltec_le32(record + 10);

        /* The name field is the rest of the record, terminator included. */
        name_size = record_size - XX_CORELLTEC_RECORD_FIXED_SIZE;
        if (name_size < (int64_t)XX_CORELLTEC_MIN_NAME_LENGTH + 1 ||
            name_size > (int64_t)XX_CORELLTEC_MAX_NAME_LENGTH + 1) {
            break;
        }
        if (!xx_corelltec_range_within(
                span, offset + XX_CORELLTEC_RECORD_FIXED_SIZE, name_size)) {
            break;
        }
        name_field = (uint8_t *)xx_mem_alloc((size_t)name_size);
        if (!name_field) goto fail;
        if (!xx_corelltec_read_at(
                self,
                self->base_address + offset + XX_CORELLTEC_RECORD_FIXED_SIZE,
                name_field, (size_t)name_size)) {
            goto fail;
        }
        /* Fixed relationship, not a search: the terminator is the LAST byte of
         * the record, so a NUL anywhere else means this is not a record. */
        if (name_field[name_size - 1] != 0U ||
            !xx_corelltec_name_valid(name_field, (size_t)name_size - 1U)) {
            xx_mem_free(name_field);
            name_field = NULL;
            break;
        }
        name = (char *)xx_mem_alloc((size_t)name_size);
        if (!name) goto fail;
        xx_rt_memcpy(name, name_field, (size_t)name_size);
        xx_mem_free(name_field);
        name_field = NULL;

        if (member_size <= 0 || member_size > XX_CORELLTEC_MAX_MEMBER_SIZE) {
            break;
        }
        if (offset_in_block > XX_CORELLTEC_MAX_BLOCK_SIZE - member_size) break;

        if (block_offset != current_block_offset) {
            /* A new block: its offset must move forward and its first member
             * must restart the in-block cursor at zero. */
            if (block_offset <= current_block_offset) break;
            if (offset_in_block != 0) break;
            /* The directory ends exactly where the payload begins, and the
             * payload begins with block offset 0. */
            if (stream->block_count == 0U && block_offset != 0) break;

            xx_mem_zero(&block, sizeof(block));
            block.file_offset = block_offset; /* rebased once sizes are known */
            if (!xx_corelltec_add_block(stream, &block)) goto fail;
            current_block_offset = block_offset;
            expected_in_block = 0;
        }
        /* Members of one block tile its plaintext without gaps or overlap. */
        if (offset_in_block != expected_in_block) break;
        expected_in_block = offset_in_block + member_size;

        if (stream->count >= (size_t)XX_CORELLTEC_MAX_MEMBERS) break;

        xx_mem_zero(&member, sizeof(member));
        member.name = name;
        member.record_offset = self->base_address + offset;
        member.record_size = record_size;
        member.block_index = (int32_t)(stream->block_count - 1U);
        member.offset_in_block = offset_in_block;
        member.size = member_size;
        if (!xx_corelltec_add_member(stream, &member)) goto fail;
        name = NULL;

        stream->blocks[stream->block_count - 1U].uncompressed_size =
            expected_in_block;
        if (expected_in_block > XX_CORELLTEC_MAX_BLOCK_SIZE) goto fail;

        offset += record_size;
    }
    if (name) {
        xx_str_free(name);
        name = NULL;
    }
    if (stream->count == 0U || stream->block_count == 0U) goto fail;

    stream->directory_size = offset - (int64_t)XX_CORELLTEC_HEADER_SIZE;
    payload_offset = offset;
    if (payload_offset >= span) goto fail;
    payload_size = span - payload_offset;

    /* Block offsets are relative to the first payload byte; sizes come from
     * the gap to the next block, and the last block runs to end of file. */
    for (index = 0U; index < stream->block_count; ++index) {
        xx_corelltec_block *current = &stream->blocks[index];
        const int64_t relative = current->file_offset;
        const int64_t next_relative =
            (index + 1U < stream->block_count)
                ? stream->blocks[index + 1U].file_offset
                : payload_size;

        if (relative >= payload_size) goto fail;
        if (next_relative <= relative) goto fail;
        current->file_offset = self->base_address + payload_offset + relative;
        current->compressed_size = next_relative - relative;
        current->stream_size = current->compressed_size +
                               (int64_t)XX_CORELLTEC_BLOCK_TAIL;
        if (current->stream_size > span - (payload_offset + relative)) {
            current->stream_size = span - (payload_offset + relative);
        }
        if (current->uncompressed_size <= 0) goto fail;
    }

    if (probe_stream && !xx_corelltec_probe(self, &stream->blocks[0])) {
        goto fail;
    }
    if (pd && xx_pd_is_stopped(pd)) goto fail;

    /* The last block runs to end of file, so the archive is the whole span. */
    stream->archive_size = span;
    return stream;

fail:
    if (name_field) xx_mem_free(name_field);
    if (name) xx_str_free(name);
    xx_corelltec_stream_free(stream);
    return NULL;
}

/* -------------------------------------------------------------- decode -- */

static bool xx_corelltec_decode(Abstractformat *self,
                                const xx_corelltec_stream *stream,
                                const xx_corelltec_member *member,
                                uint8_t **out, size_t *out_size,
                                xx_pd_struct *pd) {
    const xx_corelltec_block *block;
    uint8_t *input;
    uint8_t *output;
    size_t written = 0U;

    *out = NULL;
    *out_size = 0U;
    if (!self || !stream || !member) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    if (member->block_index < 0 ||
        (size_t)member->block_index >= stream->block_count) {
        return false;
    }
    block = &stream->blocks[member->block_index];
    if (member->size <= 0 || member->size > XX_CORELLTEC_MAX_MEMBER_SIZE) {
        return false;
    }
    if (block->stream_size <= 0 ||
        block->stream_size > XX_CORELLTEC_MAX_BLOCK_SIZE) {
        return false;
    }
    /* The codec produces offset_in_block + size bytes of block plaintext into
     * its own scratch and hands back the member's slice; the prefix cannot be
     * skipped because a match may reach back to the block's first byte. */
    if (member->offset_in_block < 0 ||
        member->offset_in_block > block->uncompressed_size - member->size) {
        return false;
    }

    input = (uint8_t *)xx_mem_alloc((size_t)block->stream_size);
    if (!input) return false;
    if (!xx_corelltec_read_at(self, block->file_offset, input,
                              (size_t)block->stream_size)) {
        xx_mem_free(input);
        return false;
    }
    output = (uint8_t *)xx_mem_alloc((size_t)member->size);
    if (!output) {
        xx_mem_free(input);
        return false;
    }
    if (!xx_corelltec_decode_member(input, (size_t)block->stream_size,
                                    (size_t)member->offset_in_block, output,
                                    (size_t)member->size, &written) ||
        written != (size_t)member->size) {
        xx_mem_free(output);
        xx_mem_free(input);
        return false;
    }
    xx_mem_free(input);
    *out = output;
    *out_size = (size_t)member->size;
    return true;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_corelltec_init(xx_corelltec *archive, xx_io_device *device,
                       int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_CORELLTEC_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-corel-lta");
    xx_format_set_extension(&archive->format, "lta");
    archive->format.check_is_valid = xx_corelltec_check_is_valid;
    archive->format.handle_base_info = xx_corelltec_handle_base_info;
    archive->format.get_format_size = xx_corelltec_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_corelltec_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_corelltec_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_corelltec_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_corelltec_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_corelltec_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_corelltec_free_archive_records_reading;
    archive->format.destroy = xx_corelltec_vtable_destroy;
}

xx_corelltec *xx_corelltec_create(xx_io_device *device, int64_t base_address) {
    xx_corelltec *archive = (xx_corelltec *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_corelltec_init(archive, device, base_address);
    return archive;
}

void xx_corelltec_destroy(xx_corelltec *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
    archive->number_of_blocks = 0U;
}

void xx_corelltec_free(xx_corelltec *archive) {
    if (!archive) return;
    xx_corelltec_destroy(archive);
    xx_mem_free(archive);
}

static void xx_corelltec_vtable_destroy(Abstractformat *self) {
    xx_corelltec_destroy((xx_corelltec *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_corelltec_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_corelltec_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    /* With the probe: the codec has to agree before the file is claimed. */
    stream = xx_corelltec_parse(self, true, pd);
    if (!stream) return false;
    xx_corelltec_stream_free(stream);
    return true;
}

bool xx_corelltec_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_corelltec *archive = (xx_corelltec *)self;
    xx_corelltec_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    /* No probe here: validity has already been decided and a second trial
     * decode of the first block would buy nothing. */
    stream = xx_corelltec_parse(self, false, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    archive->number_of_blocks = stream->block_count;
    /* The last block runs to end of file, so there is never an overlay. */
    self->overlay_offset = -1;
    self->overlay_size = 0;
    xx_corelltec_stream_free(stream);
    return true;
}

int64_t xx_corelltec_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_corelltec_get_number_of_archive_records(Abstractformat *self,
                                                    xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_corelltec *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_corelltec_set_record(xx_archive_record *record,
                                    const xx_corelltec_stream *stream,
                                    const xx_corelltec_member *member) {
    const xx_corelltec_block *block;

    if (member->block_index < 0 ||
        (size_t)member->block_index >= stream->block_count) {
        return false;
    }
    block = &stream->blocks[member->block_index];
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->record_offset;
    record->header_size = member->record_size;
    /* Solid container: the record's stream is the whole block. */
    record->data_offset = block->file_offset;
    record->compressed_size = block->stream_size;
    return xx_archive_record_set_original_name(record, member->name) &&
           /* The block's packed size, shared by every member of that block:
            * the format stores no per-member packed size. */
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)block->compressed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)member->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          0U) &&
           /* Which slice of the block's plaintext the member owns. */
           xx_archive_record_set_meta_u64(
               record, XX_META_ID_RELATIVE_OFFSET_LOCAL_HEADER,
               (uint64_t)member->offset_in_block) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

static bool xx_corelltec_copy_options(xx_list_s *target,
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

static const xx_var *xx_corelltec_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_corelltec_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_corelltec_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_corelltec_parse(self, false, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_corelltec_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_corelltec_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_corelltec_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_corelltec_set_record(&state->current_record, stream,
                                  &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_corelltec_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_corelltec_archive_record_move_to_next(Abstractformat *self,
                                              xx_archive_record_state *state,
                                              xx_pd_struct *pd) {
    xx_corelltec_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_corelltec_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_corelltec_set_record(&state->current_record, stream,
                                                &stream->items[stream->index]);
    return state->has_record;
}

bool xx_corelltec_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    xx_corelltec_stream *stream;
    const xx_corelltec_member *member;
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
    stream = (xx_corelltec_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_corelltec_path_safe(member->name)) return false;

    path_option =
        xx_corelltec_get_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        result = xx_corelltec_decode(self, stream, member, &plain, &plain_size,
                                     pd);
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
    if (base_path[0] != '\0' && base_path[xx_str_len(base_path) - 1U] != '/' &&
        base_path[xx_str_len(base_path) - 1U] != '\\') {
        target_path = xx_str_concat3(base_path, "/", member->name);
    } else {
        target_path = xx_str_concat(base_path, member->name);
    }
    xx_str_free(converted_path);
    if (!target_path) return false;

    if (!xx_store_create_dirs_a(target_path, false) ||
        !xx_corelltec_decode(self, stream, member, &plain, &plain_size, pd)) {
        xx_str_free(target_path);
        return false;
    }
    {
        xx_io_device *output = xx_io_file_open(target_path, "wb");
        created = output != NULL;
        size_t completed = 0U;

        result = output != NULL;
        while (result && completed < plain_size) {
            ssize_t sent =
                xx_io_write(output, plain + completed, plain_size - completed);
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

void xx_corelltec_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
