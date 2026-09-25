/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/pem/xx_pem.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the file-type constant resolves to UNKNOWN until the enumerator
 * lands.  Delete this block once XX_FILE_TYPE_PEM exists in the enum. */
#ifdef PEM
#define XX_PEM_FILE_TYPE XX_FILE_TYPE_PEM
#else
#define XX_PEM_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

/* Read granularity of the block scanner. */
#define XX_PEM_CHUNK_SIZE 16384U
/* Longest END line ("-----END OPENSSH PRIVATE KEY-----"). */
#define XX_PEM_MAX_END_SIZE 33U
#define XX_PEM_HISTORY_SIZE 64U

typedef struct xx_pem_magic_s {
    const char *text;
    uint8_t size;
    uint8_t kind;
    const char *label; /* The label between "BEGIN " and the dashes. */
} xx_pem_magic;

#define XX_PEM_MAGIC(label, kind) \
    { "-----BEGIN " label "-----", \
      (uint8_t)(sizeof("-----BEGIN " label "-----") - 1U), (uint8_t)(kind), \
      label }

/* binwalk src/signatures/pem.rs: pem_public_key_magic(),
 * pem_private_key_magic(), pem_certificate_magic() - in that order, which is
 * also the order binwalk classifies in.  The 26-byte prefixes binwalk
 * classifies on are all distinct, so the full BEGIN line decides the kind. */
static const xx_pem_magic g_xx_pem_begin[] = {
    XX_PEM_MAGIC("PUBLIC KEY", XX_PEM_KIND_PUBLIC_KEY),
    XX_PEM_MAGIC("RSA PUBLIC KEY", XX_PEM_KIND_PUBLIC_KEY),
    XX_PEM_MAGIC("DSA PUBLIC KEY", XX_PEM_KIND_PUBLIC_KEY),
    XX_PEM_MAGIC("ECDSA PUBLIC KEY", XX_PEM_KIND_PUBLIC_KEY),
    XX_PEM_MAGIC("PRIVATE KEY", XX_PEM_KIND_PRIVATE_KEY),
    XX_PEM_MAGIC("EC PRIVATE KEY", XX_PEM_KIND_PRIVATE_KEY),
    XX_PEM_MAGIC("RSA PRIVATE KEY", XX_PEM_KIND_PRIVATE_KEY),
    XX_PEM_MAGIC("DSA PRIVATE KEY", XX_PEM_KIND_PRIVATE_KEY),
    XX_PEM_MAGIC("OPENSSH PRIVATE KEY", XX_PEM_KIND_PRIVATE_KEY),
    XX_PEM_MAGIC("ANY PRIVATE KEY", XX_PEM_KIND_PRIVATE_KEY),
    XX_PEM_MAGIC("ENCRYPTED PRIVATE KEY", XX_PEM_KIND_PRIVATE_KEY),
    XX_PEM_MAGIC("TSS2 PRIVATE KEY", XX_PEM_KIND_PRIVATE_KEY),
    XX_PEM_MAGIC("CERTIFICATE", XX_PEM_KIND_CERTIFICATE),
};
#define XX_PEM_BEGIN_COUNT (sizeof(g_xx_pem_begin) / sizeof(g_xx_pem_begin[0]))
#define XX_PEM_ENCRYPTED_INDEX 10U

typedef struct xx_pem_end_s {
    const char *text;
    uint8_t size;
} xx_pem_end;

#define XX_PEM_END(label) \
    { "-----END " label "-----", \
      (uint8_t)(sizeof("-----END " label "-----") - 1U) }

/* binwalk src/extractors/pem.rs get_pem_size(): only these seven.  None is a
 * substring of another, so the first match by start is also the first by
 * end, which is what binwalk's find_overlapping_iter().next() returns. */
static const xx_pem_end g_xx_pem_end[] = {
    XX_PEM_END("PUBLIC KEY"),
    XX_PEM_END("CERTIFICATE"),
    XX_PEM_END("PRIVATE KEY"),
    XX_PEM_END("EC PRIVATE KEY"),
    XX_PEM_END("RSA PRIVATE KEY"),
    XX_PEM_END("DSA PRIVATE KEY"),
    XX_PEM_END("OPENSSH PRIVATE KEY"),
};
#define XX_PEM_END_COUNT (sizeof(g_xx_pem_end) / sizeof(g_xx_pem_end[0]))

/* A forward-mostly byte cursor over the device. */
typedef struct xx_pem_reader_s {
    xx_io_device *device;
    int64_t total;
    int64_t buffer_start;
    size_t buffer_size;
    uint8_t *buffer;
    xx_pd_struct *pd;
} xx_pem_reader;

typedef struct xx_pem_block_s {
    int64_t offset;
    int64_t size;
    uint32_t kind;
    uint32_t magic_index;
} xx_pem_block;

typedef struct xx_pem_private_s {
    int64_t input_size;
    int64_t start;
    int64_t end;
    uint64_t count;
    uint64_t certificates;
    uint64_t public_keys;
    uint64_t private_keys;
    uint32_t first_kind;
    int64_t first_size;
    bool any_encrypted;
} xx_pem_private;

typedef struct xx_pem_archive_stream_s {
    xx_pem_reader reader;
    xx_pem_block current;
    uint64_t index;
    uint64_t count;
} xx_pem_archive_stream;

static void xx_pem_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------------------ */
/* Device access                                                             */
/* ------------------------------------------------------------------------ */

static bool xx_pem_read_at(xx_io_device *device, int64_t offset, void *data,
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

static bool xx_pem_reader_open(xx_pem_reader *reader, xx_io_device *device,
                               xx_pd_struct *pd) {
    if (!reader) return false;
    xx_mem_zero(reader, sizeof(*reader));
    reader->device = device;
    reader->pd = pd;
    reader->buffer_start = -1;
    reader->total = device ? xx_io_total_size(device) : -1;
    if (!device || reader->total < 0) return false;
    reader->buffer = (uint8_t *)xx_mem_alloc(XX_PEM_CHUNK_SIZE);
    return reader->buffer != NULL;
}

static void xx_pem_reader_close(xx_pem_reader *reader) {
    if (!reader) return;
    if (reader->buffer) xx_mem_free(reader->buffer);
    reader->buffer = NULL;
    reader->buffer_size = 0U;
    reader->buffer_start = -1;
}

/* 1: *out holds the byte at pos.  0: pos is at or past the end.  -1: I/O
 * error or cancellation. */
static int xx_pem_byte_at(xx_pem_reader *reader, int64_t pos, uint8_t *out) {
    if (!reader || !reader->buffer || !out || pos < 0) return -1;
    if (pos >= reader->total) return 0;
    if (reader->buffer_start < 0 || pos < reader->buffer_start ||
        pos - reader->buffer_start >= (int64_t)reader->buffer_size) {
        int64_t left = reader->total - pos;
        size_t want = left < (int64_t)XX_PEM_CHUNK_SIZE ? (size_t)left
                                                        : XX_PEM_CHUNK_SIZE;
        if (reader->pd && xx_pd_is_stopped(reader->pd)) return -1;
        reader->buffer_start = -1;
        reader->buffer_size = 0U;
        if (!xx_pem_read_at(reader->device, pos, reader->buffer, want)) {
            return -1;
        }
        reader->buffer_start = pos;
        reader->buffer_size = want;
    }
    *out = reader->buffer[(size_t)(pos - reader->buffer_start)];
    return 1;
}

/* ------------------------------------------------------------------------ */
/* One block, exactly as binwalk's pem_parser() + get_pem_size() see it      */
/* ------------------------------------------------------------------------ */

static int xx_pem_base64_value(uint8_t c) {
    if (c >= 'A' && c <= 'Z') return (int)(c - 'A');
    if (c >= 'a' && c <= 'z') return (int)(c - 'a') + 26;
    if (c >= '0' && c <= '9') return (int)(c - '0') + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static int xx_pem_match_begin(xx_pem_reader *reader, int64_t start) {
    uint8_t head[XX_PEM_MAX_MAGIC_SIZE];
    size_t available;
    size_t index;
    if (!reader || start < 0 || start >= reader->total) return -1;
    available = reader->total - start < (int64_t)sizeof(head)
                    ? (size_t)(reader->total - start)
                    : sizeof(head);
    if (available < XX_PEM_MIN_MAGIC_SIZE) return -1;
    for (index = 0U; index < available; ++index) {
        if (xx_pem_byte_at(reader, start + (int64_t)index, &head[index]) != 1) {
            return -1;
        }
    }
    for (index = 0U; index < XX_PEM_BEGIN_COUNT; ++index) {
        const xx_pem_magic *magic = &g_xx_pem_begin[index];
        if (magic->size <= available &&
            xx_rt_memcmp(head, magic->text, magic->size) == 0) {
            return (int)index;
        }
    }
    return -1;
}

enum {
    XX_PEM_PHASE_BEGIN_LINE = 0, /* first delimiter line, ignored */
    XX_PEM_PHASE_BASE64 = 1,     /* between the first and second delimiter */
    XX_PEM_PHASE_TAIL = 2        /* base64 accepted, looking for an END line */
};

/*
 * One forward pass over the block does all of binwalk's checks at once:
 *
 *  - get_pem_size(): the block runs to the end of the first of the seven END
 *    lines found anywhere from the block start (the BEGIN line included), plus
 *    every CR and LF byte after it.
 *  - String::from_utf8(): every byte of that range is strict UTF-8 (no
 *    overlongs, no surrogates, nothing above U+10FFFF).
 *  - decode_pem_data(): str::lines() splits on LF and drops ONE CR before it;
 *    lines starting with "--" are delimiters and the second one ends the
 *    base64; every other line is appended as is; the result must be
 *    non-empty and decode with base64's STANDARD engine, i.e. be canonical
 *    padded RFC 4648 base64 with zero trailing bits.
 *
 * A byte that breaks any of these can be refused as soon as it is seen: while
 * the second delimiter has not been reached no END line can have completed
 * (its dashes would already have failed the base64), so the byte lies inside
 * whatever binwalk would carve.  For the same reason an END line that
 * completes before the second delimiter always means rejection.
 */
static bool xx_pem_parse_block(xx_pem_reader *reader, int64_t start,
                               xx_pem_block *block) {
    uint8_t history[XX_PEM_HISTORY_SIZE];
    size_t history_size = 0U;
    int phase = XX_PEM_PHASE_BEGIN_LINE;
    bool line_start = false;
    bool pending_cr = false;
    bool dash_start = false;
    uint64_t symbols = 0U;
    unsigned padding = 0U;
    int last_value = 0;
    unsigned utf8_need = 0U;
    uint8_t utf8_low = 0x80U;
    uint8_t utf8_high = 0xBFU;
    int64_t limit;
    int64_t pos;
    int magic_index;

    if (block) xx_mem_zero(block, sizeof(*block));
    if (!reader || !block || start < 0 || start >= reader->total) return false;
    magic_index = xx_pem_match_begin(reader, start);
    if (magic_index < 0) return false;

    limit = reader->total - start > (int64_t)XX_PEM_MAX_BLOCK_SIZE
                ? start + (int64_t)XX_PEM_MAX_BLOCK_SIZE
                : reader->total;
    for (pos = start;; ++pos) {
        uint8_t b;
        bool end_found = false;
        if (pos >= limit) return false; /* no END line within reach */
        if (xx_pem_byte_at(reader, pos, &b) != 1) return false;

        /* Strict UTF-8, as Rust's String::from_utf8. */
        if (utf8_need == 0U) {
            if (b < 0x80U) {
                /* ASCII */
            } else if (b >= 0xC2U && b <= 0xDFU) {
                utf8_need = 1U; utf8_low = 0x80U; utf8_high = 0xBFU;
            } else if (b == 0xE0U) {
                utf8_need = 2U; utf8_low = 0xA0U; utf8_high = 0xBFU;
            } else if ((b >= 0xE1U && b <= 0xECU) || b == 0xEEU ||
                       b == 0xEFU) {
                utf8_need = 2U; utf8_low = 0x80U; utf8_high = 0xBFU;
            } else if (b == 0xEDU) {
                utf8_need = 2U; utf8_low = 0x80U; utf8_high = 0x9FU;
            } else if (b == 0xF0U) {
                utf8_need = 3U; utf8_low = 0x90U; utf8_high = 0xBFU;
            } else if (b >= 0xF1U && b <= 0xF3U) {
                utf8_need = 3U; utf8_low = 0x80U; utf8_high = 0xBFU;
            } else if (b == 0xF4U) {
                utf8_need = 3U; utf8_low = 0x80U; utf8_high = 0x8FU;
            } else {
                return false;
            }
        } else {
            if (b < utf8_low || b > utf8_high) return false;
            --utf8_need;
            utf8_low = 0x80U;
            utf8_high = 0xBFU;
        }

        /* END line search over the last XX_PEM_MAX_END_SIZE bytes. */
        if (history_size == sizeof(history)) {
            xx_rt_memcpy(history, history + sizeof(history) - XX_PEM_MAX_END_SIZE,
                         XX_PEM_MAX_END_SIZE);
            history_size = XX_PEM_MAX_END_SIZE;
        }
        history[history_size++] = b;
        if (b == '-') {
            size_t index;
            for (index = 0U; index < XX_PEM_END_COUNT; ++index) {
                const xx_pem_end *end = &g_xx_pem_end[index];
                if (history_size >= end->size &&
                    xx_rt_memcmp(history + history_size - end->size,
                                 end->text, end->size) == 0) {
                    end_found = true;
                    break;
                }
            }
        }

        /* Line and base64 state. */
        if (phase == XX_PEM_PHASE_BEGIN_LINE) {
            if (b == '\n') {
                phase = XX_PEM_PHASE_BASE64;
                line_start = true;
            }
        } else if (phase == XX_PEM_PHASE_BASE64) {
            if (dash_start) {
                /* "-x" is a content line with a dash in it; "--" is the
                 * second delimiter, which closes the base64. */
                if (b != '-') return false;
                if (symbols == 0U || (symbols % 4U) != 0U) return false;
                if (padding == 1U && (last_value & 0x03) != 0) return false;
                if (padding == 2U && (last_value & 0x0F) != 0) return false;
                phase = XX_PEM_PHASE_TAIL;
                dash_start = false;
            } else if (pending_cr) {
                /* A CR not followed by LF stays in the line and is not a
                 * base64 symbol. */
                if (b != '\n') return false;
                pending_cr = false;
                line_start = true;
            } else if (b == '\n') {
                line_start = true;
            } else if (b == '\r') {
                pending_cr = true;
                line_start = false;
            } else if (b == '-') {
                if (!line_start) return false;
                dash_start = true;
                line_start = false;
            } else if (b == '=') {
                if (padding >= 2U) return false;
                ++padding;
                ++symbols;
                line_start = false;
            } else {
                int value = xx_pem_base64_value(b);
                if (value < 0 || padding != 0U) return false;
                last_value = value;
                ++symbols;
                line_start = false;
            }
        }

        if (end_found) {
            if (phase != XX_PEM_PHASE_TAIL) return false;
            break;
        }
    }

    /* Trailing CR / LF bytes belong to the carve. */
    for (++pos; pos < limit; ++pos) {
        uint8_t b;
        int got = xx_pem_byte_at(reader, pos, &b);
        if (got < 0) return false;
        if (got == 0 || (b != '\r' && b != '\n')) break;
    }
    if (pos >= limit && limit < reader->total) {
        /* The newline run reaches the cap: refuse rather than truncate. */
        uint8_t b;
        if (xx_pem_byte_at(reader, pos, &b) == 1 && (b == '\r' || b == '\n')) {
            return false;
        }
    }
    block->offset = start;
    block->size = pos - start;
    block->magic_index = (uint32_t)magic_index;
    block->kind = g_xx_pem_begin[magic_index].kind;
    return block->size > 0;
}

/* The next block of the same file: after blanks only, or none. */
static bool xx_pem_next_block(xx_pem_reader *reader, int64_t after,
                              xx_pem_block *block) {
    int64_t pos = after;
    if (!reader || after < 0) return false;
    while (pos < reader->total) {
        uint8_t b;
        int got;
        if (pos - after > (int64_t)XX_PEM_MAX_GAP_SIZE) return false;
        got = xx_pem_byte_at(reader, pos, &b);
        if (got != 1) return false;
        if (b != ' ' && b != '\t' && b != '\r' && b != '\n') break;
        ++pos;
    }
    if (pos >= reader->total) return false;
    return xx_pem_parse_block(reader, pos, block);
}

/* ------------------------------------------------------------------------ */
/* Whole file                                                                */
/* ------------------------------------------------------------------------ */

static void xx_pem_private_reset(xx_pem_private *parsed) {
    if (!parsed) return;
    xx_mem_zero(parsed, sizeof(*parsed));
    parsed->input_size = -1;
    parsed->start = -1;
    parsed->end = -1;
    parsed->first_size = -1;
}

static void xx_pem_count_kind(xx_pem_private *parsed,
                              const xx_pem_block *block) {
    if (block->kind == XX_PEM_KIND_CERTIFICATE) ++parsed->certificates;
    else if (block->kind == XX_PEM_KIND_PUBLIC_KEY) ++parsed->public_keys;
    else if (block->kind == XX_PEM_KIND_PRIVATE_KEY) ++parsed->private_keys;
    if (block->magic_index == XX_PEM_ENCRYPTED_INDEX) {
        parsed->any_encrypted = true;
    }
}

static bool xx_pem_parse(Abstractformat *self, xx_pem_private *parsed,
                         bool first_only, xx_pd_struct *pd) {
    xx_pem_reader reader;
    xx_pem_block block;
    bool result = false;
    xx_pem_private_reset(parsed);
    if (!self || !self->device || !parsed || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    if (!xx_pem_reader_open(&reader, self->device, pd)) {
        xx_pem_reader_close(&reader);
        return false;
    }
    parsed->input_size = reader.total;
    parsed->start = self->base_address;
    if (!xx_pem_parse_block(&reader, self->base_address, &block)) goto done;
    parsed->count = 1U;
    parsed->first_kind = block.kind;
    parsed->first_size = block.size;
    parsed->end = block.offset + block.size;
    xx_pem_count_kind(parsed, &block);
    while (!first_only && parsed->count < XX_PEM_MAX_BLOCKS) {
        if (pd && xx_pd_is_stopped(pd)) goto done;
        if (!xx_pem_next_block(&reader, parsed->end, &block)) break;
        ++parsed->count;
        parsed->end = block.offset + block.size;
        xx_pem_count_kind(parsed, &block);
    }
    result = true;
done:
    xx_pem_reader_close(&reader);
    if (!result) xx_pem_private_reset(parsed);
    return result;
}

/* ------------------------------------------------------------------------ */
/* Record plumbing                                                           */
/* ------------------------------------------------------------------------ */

static bool xx_pem_copy_options(xx_list_s *destination,
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

static const xx_var *xx_pem_find_option(const xx_list_s *options,
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

/* "<kind>_<index>.pem", built from literals and a number only. */
static bool xx_pem_record_name(char *name, size_t capacity, uint32_t kind,
                               uint64_t index) {
    const char *prefix = xx_pem_kind_to_string(kind);
    char digits[24];
    size_t digit_count = 0U;
    size_t used = 0U;
    size_t prefix_size;
    if (!name || !prefix || capacity == 0U) return false;
    do {
        digits[digit_count++] = (char)('0' + (int)(index % 10U));
        index /= 10U;
    } while (index != 0U && digit_count < sizeof(digits));
    prefix_size = xx_str_len(prefix);
    if (prefix_size + 1U + digit_count + 4U + 1U > capacity) return false;
    xx_rt_memcpy(name, prefix, prefix_size);
    used = prefix_size;
    name[used++] = '_';
    while (digit_count != 0U) name[used++] = digits[--digit_count];
    xx_rt_memcpy(name + used, ".pem", 4U);
    used += 4U;
    name[used] = '\0';
    return true;
}

static bool xx_pem_populate_record(xx_archive_record *record,
                                   const xx_pem_block *block, uint64_t index) {
    char name[64];
    if (!record || !block || block->magic_index >= XX_PEM_BEGIN_COUNT) {
        return false;
    }
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    if (!xx_pem_record_name(name, sizeof(name), block->kind, index)) {
        return false;
    }
    record->header_offset = block->offset;
    record->header_size = 0;
    record->data_offset = block->offset;
    record->compressed_size = block->size;
    return xx_archive_record_set_original_name(record, name) &&
           xx_archive_record_set_meta_str(
               record, XX_META_ID_COMMENT,
               g_xx_pem_begin[block->magic_index].label) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)block->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)block->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          0U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

static void xx_pem_archive_stream_free(void *pointer) {
    xx_pem_archive_stream *stream = (xx_pem_archive_stream *)pointer;
    if (!stream) return;
    xx_pem_reader_close(&stream->reader);
    xx_mem_free(stream);
}

/* ------------------------------------------------------------------------ */
/* Public interface                                                          */
/* ------------------------------------------------------------------------ */

const char *xx_pem_kind_to_string(uint32_t kind) {
    switch (kind) {
    case XX_PEM_KIND_CERTIFICATE: return "certificate";
    case XX_PEM_KIND_PUBLIC_KEY: return "public_key";
    case XX_PEM_KIND_PRIVATE_KEY: return "private_key";
    default: return NULL;
    }
}

void xx_pem_init(xx_pem *pem, xx_io_device *dev, int64_t base_address) {
    if (!pem) return;
    xx_mem_zero(pem, sizeof(*pem));
    xx_format_init(&pem->format, dev, base_address);
    pem->format.file_type = XX_PEM_FILE_TYPE;
    pem->format.format_type = XX_TYPE_ARCHIVE;
    pem->format.is_archive = true;
    xx_format_set_mime_type(&pem->format, "application/x-pem-file");
    xx_format_set_extension(&pem->format, "pem");
    pem->format.check_is_valid = xx_pem_check_is_valid;
    pem->format.handle_base_info = xx_pem_handle_base_info;
    pem->format.get_format_size = xx_pem_get_format_size;
    pem->format.get_number_of_archive_records =
        xx_pem_get_number_of_archive_records;
    pem->format.create_archive_records_reading =
        xx_pem_create_archive_records_reading;
    pem->format.get_current_archive_record =
        xx_pem_get_current_archive_record;
    pem->format.unpack_current_archive_record =
        xx_pem_unpack_current_archive_record;
    pem->format.archive_record_move_to_next =
        xx_pem_archive_record_move_to_next;
    pem->format.free_archive_records_reading =
        xx_pem_free_archive_records_reading;
    pem->format.destroy = xx_pem_vtable_destroy;
    pem->first_block_size = -1;
    pem->archive_end = -1;
}

xx_pem *xx_pem_create(xx_io_device *dev, int64_t base_address) {
    xx_pem *pem = (xx_pem *)xx_mem_alloc(sizeof(*pem));
    if (pem) xx_pem_init(pem, dev, base_address);
    return pem;
}

void xx_pem_destroy(xx_pem *pem) {
    if (!pem) return;
    if (pem->internal) {
        xx_mem_free(pem->internal);
        pem->internal = NULL;
    }
    xx_format_cleanup_extra_parameters(&pem->format);
}

static void xx_pem_vtable_destroy(Abstractformat *self) {
    xx_pem_destroy((xx_pem *)self);
}

void xx_pem_free(xx_pem *pem) {
    if (!pem) return;
    xx_pem_destroy(pem);
    xx_mem_free(pem);
}

bool xx_pem_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_pem_private parsed;
    /* The first block decides validity; the rest only extends the size. */
    return xx_pem_parse(self, &parsed, true, pd);
}

bool xx_pem_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_pem_private *parsed;
    xx_pem *pem = (xx_pem *)self;
    int64_t total_size;
    if (!self) return false;
    parsed = (xx_pem_private *)xx_mem_alloc(sizeof(*parsed));
    if (!parsed || !xx_pem_parse(self, parsed, false, pd)) {
        if (parsed) xx_mem_free(parsed);
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    if (pem->internal) xx_mem_free(pem->internal);
    pem->internal = parsed;
    pem->number_of_records = parsed->count;
    pem->number_of_members = parsed->count;
    pem->number_of_certificates = parsed->certificates;
    pem->number_of_public_keys = parsed->public_keys;
    pem->number_of_private_keys = parsed->private_keys;
    pem->first_kind = parsed->first_kind;
    pem->first_block_size = parsed->first_size;
    pem->archive_end = parsed->end;
    self->is_crypted = parsed->any_encrypted;
    self->format_size = parsed->end - self->base_address;
    total_size = xx_io_total_size(self->device);
    if (total_size > parsed->end) {
        self->overlay_offset = parsed->end;
        self->overlay_size = total_size - parsed->end;
    } else {
        self->overlay_offset = -1;
        self->overlay_size = 0;
    }
    self->number_of_archive_records = parsed->count;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_pem_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return -1;
    }
    return self->format_size;
}

uint64_t xx_pem_get_number_of_archive_records(Abstractformat *self,
                                              xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return ((xx_pem *)self)->number_of_records;
}

xx_archive_record_state *xx_pem_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_record_state *state;
    xx_pem_archive_stream *stream;
    if (!self || !self->device ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    stream = (xx_pem_archive_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!state || !stream) {
        if (state) xx_mem_free(state);
        if (stream) xx_mem_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_pem_archive_stream_free;
    if (!xx_pem_copy_options(&state->options, options) ||
        !xx_pem_reader_open(&stream->reader, self->device, NULL)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    stream->count = ((xx_pem *)self)->number_of_records;
    stream->index = 0U;
    state->total_records = (int64_t)stream->count;
    if (stream->count != 0U &&
        xx_pem_parse_block(&stream->reader, self->base_address,
                           &stream->current) &&
        xx_pem_populate_record(&state->current_record, &stream->current, 0U)) {
        state->has_record = true;
        state->current_index = 0;
    }
    return state;
}

const xx_archive_record *xx_pem_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_pem_archive_record_move_to_next(Abstractformat *self,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    xx_pem_archive_stream *stream;
    xx_pem_block next;
    int64_t after;
    if (!self || !state || state->format != self || !state->has_record ||
        !state->internal_state || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_pem_archive_stream *)state->internal_state;
    after = stream->current.offset + stream->current.size;
    xx_archive_record_cleanup(&state->current_record);
    xx_archive_record_init(&state->current_record);
    state->has_record = false;
    if (stream->index + 1U >= stream->count) return false;
    stream->reader.pd = pd;
    if (!xx_pem_next_block(&stream->reader, after, &next)) {
        stream->reader.pd = NULL;
        return false;
    }
    stream->reader.pd = NULL;
    ++stream->index;
    stream->current = next;
    if (!xx_pem_populate_record(&state->current_record, &stream->current,
                                stream->index)) {
        return false;
    }
    state->has_record = true;
    state->current_index = (int64_t)stream->index;
    return true;
}

bool xx_pem_unpack_current_archive_record(Abstractformat *self,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    const xx_archive_record *record;
    const xx_var *option;
    const char *name;
    const char *base = NULL;
    char *owned_base = NULL;
    char *destination = NULL;
    bool result = false;
    if (!self || !self->device || !state || state->format != self ||
        !state->has_record || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    record = &state->current_record;
    name = xx_archive_record_get_original_name(record);
    if (!name || !name[0]) return false;
    option = xx_pem_find_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!option) {
        int64_t total = xx_io_total_size(self->device);
        return record->data_offset >= 0 && record->compressed_size >= 0 &&
               record->data_offset <= total &&
               record->compressed_size <= total - record->data_offset;
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
    /* The member name is "<kind>_<n>.pem", composed here from literals and a
     * number, so it cannot carry a path separator, a drive or "..". */
    if (base[0] && base[xx_str_len(base) - 1U] != '/' &&
        base[xx_str_len(base) - 1U] != '\\') {
        destination = xx_str_concat3(base, "/", name);
    } else {
        destination = xx_str_concat(base, name);
    }
    if (!destination) goto cleanup;
    if (!xx_store_create_dirs_a(destination, false)) goto cleanup;
    result = xx_store_unpack_device_to_file(self->device, record->data_offset,
                                            record->compressed_size,
                                            destination, pd);
cleanup:
    if (owned_base) xx_str_free(owned_base);
    if (destination) xx_str_free(destination);
    return result;
}

void xx_pem_free_archive_records_reading(Abstractformat *self,
                                         xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

uint64_t xx_pem_get_number_of_records(const xx_pem *pem) {
    return pem ? pem->number_of_records : 0U;
}
uint64_t xx_pem_get_number_of_members(const xx_pem *pem) {
    return pem ? pem->number_of_members : 0U;
}
uint32_t xx_pem_get_first_kind(const xx_pem *pem) {
    return pem ? pem->first_kind : 0U;
}
int64_t xx_pem_get_first_block_size(const xx_pem *pem) {
    return pem ? pem->first_block_size : -1;
}
int64_t xx_pem_get_archive_end(const xx_pem *pem) {
    return pem ? pem->archive_end : -1;
}
