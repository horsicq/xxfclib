/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Apple sparse bundle (.sparsebundle, UDSB). Original code written from the
 * format's structure (see xx_apple_sparse_bundle.h); the reader's shape
 * follows the QCOW1 reader (src/formats/qcow1).
 *
 * The device is the bundle's Info.plist. It is parsed with a small, bounded
 * XML property-list scanner that only understands what an Info.plist needs:
 * a prolog, <plist>, one top-level <dict> of <key>/value pairs, where values
 * the reader does not use (including nested <dict>/<array>) are skipped.
 *
 * Reconstruction walks bands 0 .. ceil(size / band-size) - 1 and writes each
 * band's bytes, reading bands/<hex> when it exists and zeros where it does
 * not (a missing band, or the part of a band past the end of its file).
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/apple_sparse_bundle/xx_apple_sparse_bundle.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

#ifdef APPLE_SPARSE_BUNDLE
#define XX_APPLE_SPARSE_BUNDLE_FILE_TYPE XX_FILE_TYPE_APPLE_SPARSE_BUNDLE
#else
#define XX_APPLE_SPARSE_BUNDLE_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

/* A real Info.plist is about 500 bytes. Anything larger is not one, and the
 * probe never reads more than this. */
#define XX_ASB_MAX_PLIST 65536U
#define XX_ASB_MIN_PLIST 64U

#define XX_ASB_TYPE_STRING "com.apple.diskimage.sparsebundle"
#define XX_ASB_MEMBER_NAME "disk.img"

/* hdiutil takes the band size in 512-byte sectors. */
#define XX_ASB_SECTOR 512U
#define XX_ASB_MAX_BAND_SIZE (UINT64_C(1) << 32)
/* 256 TB. Time Machine bundles reach several TB; nothing real is larger. */
#define XX_ASB_MAX_MEDIA_SIZE (UINT64_C(1) << 48)

#define XX_ASB_MAX_PAIRS 4096U
#define XX_ASB_MAX_DEPTH 64U
#define XX_ASB_MAX_NAME 32U

/* Output staging buffer. */
#define XX_ASB_STAGE_SIZE ((size_t)1 << 20)
/* Bytes produced by an unpack call that has no destination. */
#define XX_ASB_PROBE_BYTES (UINT64_C(16) << 20)

typedef struct xx_asb_private_s {
    char *bundle_path;       /**< Owned copy, or NULL. */
    int64_t input_size;
    int64_t base_address;
    uint64_t media_size;
    uint64_t band_size;
    uint64_t number_of_bands;
    uint32_t version;
    bool is_encrypted;
    bool bands_present;
    bool consumed;
} xx_asb_private;

static void xx_asb_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_asb_read_at(xx_io_device *device, int64_t offset, void *data,
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

static bool xx_asb_write_all(xx_io_device *output, const uint8_t *data,
                             size_t size) {
    size_t done = 0U;

    while (done < size) {
        ssize_t sent = xx_io_write(output, data + done, size - done);
        if (sent <= 0 || (size_t)sent > size - done) return false;
        done += (size_t)sent;
    }
    return true;
}

static bool xx_asb_is_sep(char c) { return c == '/' || c == '\\'; }

/* dir + "/" + leaf (no separator added when dir already ends in one). */
static char *xx_asb_join(const char *dir, const char *leaf) {
    size_t length;

    if (!dir || !leaf) return NULL;
    length = xx_str_len(dir);
    if (length == 0U) return xx_str_dup(leaf);
    if (xx_asb_is_sep(dir[length - 1U])) return xx_str_concat(dir, leaf);
    return xx_str_concat3(dir, "/", leaf);
}

/* ----------------------------------------------------------- plist lexer -- */

typedef struct xx_asb_lex_s {
    const uint8_t *p;
    size_t n;
    size_t pos;
} xx_asb_lex;

typedef struct xx_asb_tag_s {
    char name[XX_ASB_MAX_NAME];
    bool closing;
    bool empty;
} xx_asb_tag;

static bool xx_asb_is_ws(uint8_t c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static void xx_asb_skip_ws(xx_asb_lex *l) {
    while (l->pos < l->n && xx_asb_is_ws(l->p[l->pos])) ++l->pos;
}

static bool xx_asb_at(const xx_asb_lex *l, const char *s) {
    size_t length = xx_str_len(s);
    return l->n - l->pos >= length &&
           xx_rt_memcmp(l->p + l->pos, s, length) == 0;
}

/* Advance past the first occurrence of s. */
static bool xx_asb_skip_past(xx_asb_lex *l, const char *s) {
    size_t length = xx_str_len(s);

    while (l->n - l->pos >= length) {
        if (xx_rt_memcmp(l->p + l->pos, s, length) == 0) {
            l->pos += length;
            return true;
        }
        ++l->pos;
    }
    return false;
}

/* Whitespace, <?...?>, <!-- ... -->, <!DOCTYPE ...>. */
static bool xx_asb_skip_misc(xx_asb_lex *l) {
    for (;;) {
        xx_asb_skip_ws(l);
        if (xx_asb_at(l, "<?")) {
            if (!xx_asb_skip_past(l, "?>")) return false;
        } else if (xx_asb_at(l, "<!--")) {
            l->pos += 4U;
            if (!xx_asb_skip_past(l, "-->")) return false;
        } else if (xx_asb_at(l, "<!")) {
            if (!xx_asb_skip_past(l, ">")) return false;
        } else {
            return true;
        }
    }
}

/* Reads one element tag at the cursor. Attributes are skipped (quoted
 * values may contain '>'). */
static bool xx_asb_read_tag(xx_asb_lex *l, xx_asb_tag *tag) {
    size_t length = 0U;
    uint8_t quote = 0U;

    xx_rt_memset(tag, 0, sizeof(*tag));
    if (l->pos >= l->n || l->p[l->pos] != '<') return false;
    ++l->pos;
    if (l->pos < l->n && l->p[l->pos] == '/') {
        tag->closing = true;
        ++l->pos;
    }
    while (l->pos < l->n) {
        uint8_t c = l->p[l->pos];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ||
              c == ':')) {
            break;
        }
        if (length + 1U >= XX_ASB_MAX_NAME) return false;
        tag->name[length++] = (char)c;
        ++l->pos;
    }
    if (length == 0U) return false;
    while (l->pos < l->n) {
        uint8_t c = l->p[l->pos++];
        if (quote) {
            if (c == quote) quote = 0U;
        } else if (c == '"' || c == '\'') {
            quote = c;
        } else if (c == '<') {
            return false;
        } else if (c == '>') {
            if (l->pos >= 2U && l->p[l->pos - 2U] == '/') {
                if (tag->closing) return false;
                tag->empty = true;
            }
            return true;
        }
    }
    return false;
}

/* Text up to the next '<'. Comments inside values are not supported (no
 * writer emits them); the caller then sees an unexpected tag and fails. */
static void xx_asb_read_text(xx_asb_lex *l, size_t *start, size_t *length) {
    *start = l->pos;
    while (l->pos < l->n && l->p[l->pos] != '<') ++l->pos;
    *length = l->pos - *start;
}

static bool xx_asb_expect_close(xx_asb_lex *l, const char *name) {
    xx_asb_tag tag;
    return xx_asb_read_tag(l, &tag) && tag.closing &&
           xx_str_equals(tag.name, name);
}

/* Skips the body of an already-opened container element. */
static bool xx_asb_skip_nested(xx_asb_lex *l) {
    uint32_t depth = 1U;
    size_t start;
    size_t length;
    xx_asb_tag tag;

    while (depth != 0U) {
        if (!xx_asb_skip_misc(l)) return false;
        xx_asb_read_text(l, &start, &length);
        if (l->pos >= l->n) return false;
        if (xx_asb_at(l, "<!--") || xx_asb_at(l, "<?")) continue;
        if (!xx_asb_read_tag(l, &tag)) return false;
        if (tag.empty) continue;
        if (tag.closing) {
            --depth;
        } else {
            if (++depth > XX_ASB_MAX_DEPTH) return false;
        }
    }
    return true;
}

/* Trimmed view of [start, start + length). */
static void xx_asb_trim(const uint8_t *p, size_t *start, size_t *length) {
    while (*length && xx_asb_is_ws(p[*start])) {
        ++*start;
        --*length;
    }
    while (*length && xx_asb_is_ws(p[*start + *length - 1U])) --*length;
}

static bool xx_asb_parse_u64(const uint8_t *p, size_t start, size_t length,
                             uint64_t *value) {
    uint64_t result = 0U;
    size_t index;

    xx_asb_trim(p, &start, &length);
    if (length && p[start] == '+') {
        ++start;
        --length;
    }
    if (length == 0U || length > 20U) return false;
    for (index = 0U; index < length; ++index) {
        uint8_t c = p[start + index];
        uint64_t digit;
        if (c < '0' || c > '9') return false;
        digit = (uint64_t)(c - '0');
        if (result > (UINT64_MAX - digit) / 10U) return false;
        result = result * 10U + digit;
    }
    *value = result;
    return true;
}

static bool xx_asb_text_equals(const uint8_t *p, size_t start, size_t length,
                               const char *s) {
    xx_asb_trim(p, &start, &length);
    return length == xx_str_len(s) && xx_rt_memcmp(p + start, s, length) == 0;
}

/* ----------------------------------------------------------------- parse -- */

typedef struct xx_asb_fields_s {
    bool has_type, has_band, has_size, has_version;
    bool type_ok;
    uint64_t band_size;
    uint64_t size;
    uint64_t version;
} xx_asb_fields;

static bool xx_asb_parse_plist(const uint8_t *p, size_t n,
                               xx_asb_fields *f) {
    xx_asb_lex lex;
    xx_asb_tag tag;
    uint32_t pairs = 0U;

    xx_rt_memset(f, 0, sizeof(*f));
    lex.p = p;
    lex.n = n;
    lex.pos = 0U;
    if (n >= 3U && p[0] == 0xEFU && p[1] == 0xBBU && p[2] == 0xBFU) lex.pos = 3U;

    if (!xx_asb_skip_misc(&lex) || !xx_asb_read_tag(&lex, &tag) ||
        tag.closing || tag.empty || !xx_str_equals(tag.name, "plist")) {
        return false;
    }
    if (!xx_asb_skip_misc(&lex) || !xx_asb_read_tag(&lex, &tag) ||
        tag.closing || tag.empty || !xx_str_equals(tag.name, "dict")) {
        return false;
    }
    for (;;) {
        char key[40];
        size_t key_start, key_length, value_start = 0U, value_length = 0U;
        bool value_text = false;
        xx_asb_tag value;

        if (++pairs > XX_ASB_MAX_PAIRS) return false;
        if (!xx_asb_skip_misc(&lex) || !xx_asb_read_tag(&lex, &tag)) {
            return false;
        }
        if (tag.closing) {
            if (!xx_str_equals(tag.name, "dict")) return false;
            break;
        }
        if (tag.empty || !xx_str_equals(tag.name, "key")) return false;
        xx_asb_read_text(&lex, &key_start, &key_length);
        if (!xx_asb_expect_close(&lex, "key")) return false;
        xx_asb_trim(p, &key_start, &key_length);
        if (key_length >= sizeof(key)) {
            key[0] = '\0'; /* longer than any key the reader uses */
        } else {
            xx_rt_memcpy(key, p + key_start, key_length);
            key[key_length] = '\0';
        }

        if (!xx_asb_skip_misc(&lex) || !xx_asb_read_tag(&lex, &value) ||
            value.closing) {
            return false;
        }
        if (!value.empty) {
            if (xx_str_equals(value.name, "dict") ||
                xx_str_equals(value.name, "array")) {
                if (!xx_asb_skip_nested(&lex)) return false;
            } else {
                xx_asb_read_text(&lex, &value_start, &value_length);
                if (!xx_asb_expect_close(&lex, value.name)) return false;
                value_text = true;
            }
        }

        if (xx_str_equals(key, "diskimage-bundle-type")) {
            if (f->has_type) return false;
            f->has_type = true;
            f->type_ok = value_text && xx_str_equals(value.name, "string") &&
                         xx_asb_text_equals(p, value_start, value_length,
                                            XX_ASB_TYPE_STRING);
        } else if (xx_str_equals(key, "band-size")) {
            if (f->has_band || !value_text ||
                !xx_str_equals(value.name, "integer") ||
                !xx_asb_parse_u64(p, value_start, value_length,
                                  &f->band_size)) {
                return false;
            }
            f->has_band = true;
        } else if (xx_str_equals(key, "size")) {
            if (f->has_size || !value_text ||
                !xx_str_equals(value.name, "integer") ||
                !xx_asb_parse_u64(p, value_start, value_length, &f->size)) {
                return false;
            }
            f->has_size = true;
        } else if (xx_str_equals(key, "bundle-backingstore-version")) {
            if (f->has_version || !value_text ||
                !xx_str_equals(value.name, "integer") ||
                !xx_asb_parse_u64(p, value_start, value_length,
                                  &f->version)) {
                return false;
            }
            f->has_version = true;
        }
    }
    /* </plist>, then nothing but whitespace (or NUL padding). */
    if (!xx_asb_skip_misc(&lex) || !xx_asb_expect_close(&lex, "plist")) {
        return false;
    }
    while (lex.pos < lex.n) {
        uint8_t c = lex.p[lex.pos++];
        if (!xx_asb_is_ws(c) && c != 0U) return false;
    }
    return true;
}

static void xx_asb_private_cleanup(xx_asb_private *parsed) {
    if (!parsed) return;
    if (parsed->bundle_path) xx_str_free(parsed->bundle_path);
    xx_mem_zero(parsed, sizeof(*parsed));
    parsed->input_size = -1;
}

static void xx_asb_private_free(void *pointer) {
    xx_asb_private *parsed = (xx_asb_private *)pointer;

    if (!parsed) return;
    xx_asb_private_cleanup(parsed);
    xx_mem_free(parsed);
}

/* token: empty for a plain bundle, the "encrcdsa" header when encrypted. */
static bool xx_asb_token_encrypted(const char *bundle_path) {
    char *path = xx_asb_join(bundle_path, "token");
    xx_io_device *device;
    uint8_t head[8];
    bool result = false;

    if (!path) return false;
    device = xx_io_file_open(path, "rb");
    xx_str_free(path);
    if (!device) return false;
    if (xx_io_total_size(device) >= 8 && xx_asb_read_at(device, 0, head, 8U) &&
        xx_rt_memcmp(head, "encrcdsa", 8U) == 0) {
        result = true;
    }
    xx_io_close(device);
    return result;
}

static bool xx_asb_parse(Abstractformat *self, const char *bundle_path,
                         xx_asb_private *parsed, xx_pd_struct *pd) {
    uint8_t *buffer = NULL;
    int64_t total_size;
    int64_t available;
    xx_asb_fields fields;

    if (parsed) {
        xx_mem_zero(parsed, sizeof(*parsed));
        parsed->input_size = -1;
    }
    if (!self || !self->device || !parsed || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    total_size = xx_io_total_size(self->device);
    if (total_size < 0 || self->base_address > total_size) return false;
    available = total_size - self->base_address;
    if (available < (int64_t)XX_ASB_MIN_PLIST ||
        available > (int64_t)XX_ASB_MAX_PLIST) {
        return false;
    }
    buffer = (uint8_t *)xx_mem_alloc((size_t)available);
    if (!buffer) return false;
    if (!xx_asb_read_at(self->device, self->base_address, buffer,
                        (size_t)available)) {
        xx_mem_free(buffer);
        return false;
    }
    if (!xx_asb_parse_plist(buffer, (size_t)available, &fields)) {
        xx_mem_free(buffer);
        return false;
    }
    xx_mem_free(buffer);

    if (!fields.has_type || !fields.type_ok || !fields.has_band ||
        !fields.has_size || fields.size == 0U ||
        fields.band_size < XX_ASB_SECTOR ||
        fields.band_size > XX_ASB_MAX_BAND_SIZE ||
        (fields.band_size % XX_ASB_SECTOR) != 0U ||
        fields.size > XX_ASB_MAX_MEDIA_SIZE ||
        (fields.has_version && (fields.version < 1U || fields.version > 2U))) {
        return false;
    }
    parsed->input_size = total_size;
    parsed->base_address = self->base_address;
    parsed->media_size = fields.size;
    parsed->band_size = fields.band_size;
    parsed->number_of_bands =
        fields.size / fields.band_size +
        ((fields.size % fields.band_size) != 0U ? 1U : 0U);
    parsed->version = fields.has_version ? (uint32_t)fields.version : 0U;

    if (bundle_path && bundle_path[0]) {
        char *bands = xx_asb_join(bundle_path, "bands");
        parsed->bundle_path = xx_str_dup(bundle_path);
        if (!bands || !parsed->bundle_path) {
            if (bands) xx_str_free(bands);
            xx_asb_private_cleanup(parsed);
            return false;
        }
        parsed->bands_present = xx_io_file_exists_a(bands);
        xx_str_free(bands);
        parsed->is_encrypted = xx_asb_token_encrypted(bundle_path);
    }
    return true;
}

/* --------------------------------------------------------------- reader -- */

static void xx_asb_band_name(uint64_t index, char out[20]) {
    static const char digits[] = "0123456789abcdef";
    char reversed[20];
    size_t length = 0U;
    size_t i;

    do {
        reversed[length++] = digits[index & 15U];
        index >>= 4;
    } while (index != 0U && length < 16U);
    for (i = 0U; i < length; ++i) out[i] = reversed[length - 1U - i];
    out[length] = '\0';
}

/* Open bands/<hex>. A band that does not exist is sparse (NULL, true). One
 * that exists but cannot be opened is an error (NULL, false). */
static bool xx_asb_open_band(const xx_asb_private *parsed, uint64_t index,
                             xx_io_device **band) {
    char name[20];
    char *bands;
    char *path;
    bool result = true;

    *band = NULL;
    xx_asb_band_name(index, name);
    bands = xx_asb_join(parsed->bundle_path, "bands");
    if (!bands) return false;
    path = xx_asb_join(bands, name);
    xx_str_free(bands);
    if (!path) return false;
    *band = xx_io_file_open(path, "rb");
    if (!*band && xx_io_file_exists_a(path)) result = false;
    xx_str_free(path);
    return result;
}

/* Produce the disk image. With no output only the first
 * XX_ASB_PROBE_BYTES are produced (the size is a plist value and may claim
 * terabytes). */
static bool xx_asb_write_image(const xx_asb_private *parsed,
                               xx_io_device *output, xx_pd_struct *pd) {
    uint8_t *stage;
    uint64_t index;
    uint64_t produced = 0U;
    uint64_t limit = output ? parsed->media_size
                            : (parsed->media_size < XX_ASB_PROBE_BYTES
                                   ? parsed->media_size
                                   : XX_ASB_PROBE_BYTES);
    bool result = true;

    stage = (uint8_t *)xx_mem_alloc(XX_ASB_STAGE_SIZE);
    if (!stage) return false;
    for (index = 0U; result && index < parsed->number_of_bands &&
                     produced < limit;
         ++index) {
        xx_io_device *band = NULL;
        int64_t band_file_size = 0;
        uint64_t band_bytes = parsed->media_size - index * parsed->band_size;
        uint64_t offset = 0U;

        if (band_bytes > parsed->band_size) band_bytes = parsed->band_size;
        if (!xx_asb_open_band(parsed, index, &band)) {
            result = false;
            break;
        }
        if (band) {
            band_file_size = xx_io_total_size(band);
            if (band_file_size < 0) result = false;
        }
        while (result && offset < band_bytes && produced < limit) {
            size_t chunk = XX_ASB_STAGE_SIZE;
            size_t from_file = 0U;

            if ((uint64_t)chunk > band_bytes - offset) {
                chunk = (size_t)(band_bytes - offset);
            }
            if ((uint64_t)chunk > limit - produced) {
                chunk = (size_t)(limit - produced);
            }
            if (pd && xx_pd_is_stopped(pd)) {
                result = false;
                break;
            }
            if (band && (uint64_t)band_file_size > offset) {
                uint64_t left = (uint64_t)band_file_size - offset;
                from_file = (uint64_t)chunk < left ? chunk : (size_t)left;
            }
            if (from_file != 0U &&
                !xx_asb_read_at(band, (int64_t)offset, stage, from_file)) {
                result = false;
                break;
            }
            if (from_file < chunk) {
                xx_rt_memset(stage + from_file, 0, chunk - from_file);
            }
            if (output && !xx_asb_write_all(output, stage, chunk)) {
                result = false;
                break;
            }
            offset += chunk;
            produced += chunk;
        }
        if (band) xx_io_close(band);
    }
    xx_mem_free(stage);
    return result;
}

/* ------------------------------------------------------------ lifecycle -- */

void xx_apple_sparse_bundle_init(xx_apple_sparse_bundle *bundle,
                                 xx_io_device *dev, int64_t base_address) {
    if (!bundle) return;
    xx_mem_zero(bundle, sizeof(*bundle));
    xx_format_init(&bundle->format, dev, base_address);
    bundle->format.endian = XX_ENDIAN_UNKNOWN;
    bundle->format.file_type = XX_APPLE_SPARSE_BUNDLE_FILE_TYPE;
    bundle->format.format_type = XX_TYPE_ARCHIVE;
    bundle->format.is_archive = false;
    xx_format_set_mime_type(&bundle->format, "application/x-apple-diskimage");
    xx_format_set_extension(&bundle->format, "sparsebundle");
    bundle->format.check_is_valid = xx_apple_sparse_bundle_check_is_valid;
    bundle->format.handle_base_info = xx_apple_sparse_bundle_handle_base_info;
    bundle->format.get_format_size = xx_apple_sparse_bundle_get_format_size;
    bundle->format.get_number_of_archive_records =
        xx_apple_sparse_bundle_get_number_of_archive_records;
    bundle->format.create_archive_records_reading =
        xx_apple_sparse_bundle_create_archive_records_reading;
    bundle->format.get_current_archive_record =
        xx_apple_sparse_bundle_get_current_archive_record;
    bundle->format.unpack_current_archive_record =
        xx_apple_sparse_bundle_unpack_current_archive_record;
    bundle->format.archive_record_move_to_next =
        xx_apple_sparse_bundle_archive_record_move_to_next;
    bundle->format.free_archive_records_reading =
        xx_apple_sparse_bundle_free_archive_records_reading;
    bundle->format.destroy = xx_asb_vtable_destroy;
}

xx_apple_sparse_bundle *xx_apple_sparse_bundle_create(xx_io_device *dev,
                                                      int64_t base_address) {
    xx_apple_sparse_bundle *bundle =
        (xx_apple_sparse_bundle *)xx_mem_alloc(sizeof(*bundle));

    if (bundle) xx_apple_sparse_bundle_init(bundle, dev, base_address);
    return bundle;
}

void xx_apple_sparse_bundle_destroy(xx_apple_sparse_bundle *bundle) {
    if (!bundle) return;
    if (bundle->internal) {
        xx_asb_private_free(bundle->internal);
        bundle->internal = NULL;
    }
    if (bundle->bundle_path) {
        xx_str_free(bundle->bundle_path);
        bundle->bundle_path = NULL;
    }
    if (bundle->owns_device && bundle->format.device) {
        xx_io_close(bundle->format.device);
        bundle->format.device = NULL;
        bundle->owns_device = false;
    }
    xx_format_cleanup_extra_parameters(&bundle->format);
}

static void xx_asb_vtable_destroy(Abstractformat *self) {
    xx_apple_sparse_bundle_destroy((xx_apple_sparse_bundle *)self);
}

void xx_apple_sparse_bundle_free(xx_apple_sparse_bundle *bundle) {
    if (!bundle) return;
    xx_apple_sparse_bundle_destroy(bundle);
    xx_mem_free(bundle);
}

bool xx_apple_sparse_bundle_set_bundle_path(xx_apple_sparse_bundle *bundle,
                                            const char *bundle_path) {
    char *copy = NULL;

    if (!bundle) return false;
    if (bundle_path) {
        if (!bundle_path[0]) return false;
        copy = xx_str_dup(bundle_path);
        if (!copy) return false;
    }
    if (bundle->bundle_path) xx_str_free(bundle->bundle_path);
    bundle->bundle_path = copy;
    if (bundle->internal) {
        xx_asb_private_free(bundle->internal);
        bundle->internal = NULL;
    }
    bundle->number_of_records = 0U;
    bundle->format.base_info_handled = false;
    bundle->format.is_archive = false;
    bundle->format.number_of_archive_records = 0U;
    return true;
}

/* Last path component is Info.plist / Info.bckup (any case)? */
static bool xx_asb_names_plist(const char *path, size_t *dir_length) {
    size_t length = xx_str_len(path);
    size_t leaf = length;

    while (leaf > 0U && !xx_asb_is_sep(path[leaf - 1U])) --leaf;
    if (!xx_str_iequals(path + leaf, "Info.plist") &&
        !xx_str_iequals(path + leaf, "Info.bckup")) {
        return false;
    }
    *dir_length = leaf;
    return true;
}

xx_apple_sparse_bundle *xx_apple_sparse_bundle_open_path(const char *path) {
    xx_apple_sparse_bundle *bundle;
    xx_io_device *device = NULL;
    char *dir = NULL;
    size_t dir_length;

    if (!path || !path[0]) return NULL;
    if (xx_asb_names_plist(path, &dir_length)) {
        /* Strip the leaf and its separator; "Info.plist" alone means ".". */
        if (dir_length == 0U) {
            dir = xx_str_dup(".");
        } else {
            size_t keep = dir_length;
            if (keep > 1U) --keep; /* drop the separator, keep a root "/" */
            dir = xx_str_create_len(keep);
            if (dir) {
                xx_rt_memcpy(dir, path, keep);
                dir[keep] = '\0';
            }
        }
        if (!dir) return NULL;
        device = xx_io_file_open(path, "rb");
    } else {
        char *plist = xx_asb_join(path, "Info.plist");
        dir = xx_str_dup(path);
        if (!plist || !dir) {
            if (plist) xx_str_free(plist);
            if (dir) xx_str_free(dir);
            return NULL;
        }
        device = xx_io_file_open(plist, "rb");
        xx_str_free(plist);
        if (!device) {
            plist = xx_asb_join(path, "Info.bckup");
            if (plist) {
                device = xx_io_file_open(plist, "rb");
                xx_str_free(plist);
            }
        }
    }
    if (!device) {
        xx_str_free(dir);
        return NULL;
    }
    bundle = xx_apple_sparse_bundle_create(device, 0);
    if (!bundle || !xx_apple_sparse_bundle_set_bundle_path(bundle, dir)) {
        if (bundle) xx_apple_sparse_bundle_free(bundle);
        xx_io_close(device);
        xx_str_free(dir);
        return NULL;
    }
    bundle->owns_device = true;
    xx_str_free(dir);
    return bundle;
}

/* --------------------------------------------------------------- format -- */

bool xx_apple_sparse_bundle_check_is_valid(Abstractformat *self,
                                           xx_pd_struct *pd) {
    xx_asb_private parsed;
    bool result = xx_asb_parse(self, NULL, &parsed, pd);

    xx_asb_private_cleanup(&parsed);
    return result;
}

bool xx_apple_sparse_bundle_handle_base_info(Abstractformat *self,
                                             xx_pd_struct *pd) {
    xx_apple_sparse_bundle *bundle = (xx_apple_sparse_bundle *)self;
    xx_asb_private *parsed;

    if (!self) return false;
    parsed = (xx_asb_private *)xx_mem_alloc(sizeof(*parsed));
    if (!parsed || !xx_asb_parse(self, bundle->bundle_path, parsed, pd)) {
        if (parsed) xx_mem_free(parsed);
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    if (bundle->internal) xx_asb_private_free(bundle->internal);
    bundle->internal = parsed;
    bundle->media_size = parsed->media_size;
    bundle->band_size = parsed->band_size;
    bundle->number_of_bands = parsed->number_of_bands;
    bundle->backingstore_version = parsed->version;
    bundle->is_encrypted = parsed->is_encrypted;
    /* The disk is reachable only with the bundle directory and its bands/. */
    bundle->number_of_records =
        (parsed->bundle_path && parsed->bands_present) ? 1U : 0U;
    self->is_archive = bundle->number_of_records != 0U;
    self->is_crypted = parsed->is_encrypted;
    if (parsed->version != 0U) {
        (void)xx_rt_snprintf(self->version, sizeof(self->version), "%u",
                             (unsigned)parsed->version);
    }
    self->format_size = parsed->input_size - self->base_address;
    self->overlay_offset = -1;
    self->overlay_size = 0;
    self->number_of_archive_records = bundle->number_of_records;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_apple_sparse_bundle_get_format_size(Abstractformat *self,
                                               xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return -1;
    }
    return self->format_size;
}

uint64_t xx_apple_sparse_bundle_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return ((xx_apple_sparse_bundle *)self)->number_of_records;
}

/* -------------------------------------------------------------- records -- */

static bool xx_asb_copy_options(xx_list_s *destination,
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

static const xx_var *xx_asb_find_option(const xx_list_s *options,
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

static bool xx_asb_populate_record(xx_archive_record *record,
                                   const xx_asb_private *parsed) {
    if (!record || !parsed) return false;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = parsed->base_address;
    record->header_size = parsed->input_size - parsed->base_address;
    record->data_offset = -1;
    record->compressed_size = 0;
    return xx_archive_record_set_original_name(record, XX_ASB_MEMBER_NAME) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          parsed->media_size) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           parsed->is_encrypted);
}

xx_archive_record_state *xx_apple_sparse_bundle_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_apple_sparse_bundle *bundle = (xx_apple_sparse_bundle *)self;
    xx_archive_record_state *state;
    xx_asb_private *parsed;

    if (!self || !self->device ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd)) ||
        bundle->number_of_records == 0U) {
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    parsed = (xx_asb_private *)xx_mem_calloc(1U, sizeof(*parsed));
    if (!state || !parsed) {
        if (state) xx_mem_free(state);
        if (parsed) xx_mem_free(parsed);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    if (!xx_asb_copy_options(&state->options, options) ||
        !xx_asb_parse(self, bundle->bundle_path, parsed, pd) ||
        !parsed->bundle_path || !parsed->bands_present) {
        xx_asb_private_free(parsed);
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->internal_state = parsed;
    state->free_internal = xx_asb_private_free;
    state->total_records = 1;
    if (!xx_asb_populate_record(&state->current_record, parsed)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_apple_sparse_bundle_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_apple_sparse_bundle_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    xx_asb_private *parsed;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    parsed = (xx_asb_private *)state->internal_state;
    if (parsed) parsed->consumed = true;
    xx_archive_record_cleanup(&state->current_record);
    xx_archive_record_init(&state->current_record);
    state->has_record = false;
    return false;
}

/* XX_META_ID_OPT_MAX_MEMBER_SIZE, when given, caps the disk size an unpack
 * will write (same handling as the VHDX reader). Absent means unlimited up
 * to XX_ASB_MAX_MEDIA_SIZE. */
static bool xx_asb_size_allowed(Abstractformat *format,
                                const xx_list_s *options, uint64_t size) {
    const xx_var *limit = xx_format_resolve_extra_parameter(
        format, options, XX_META_ID_OPT_MAX_MEMBER_SIZE);
    if (!limit) return true;
    switch (limit->type) {
        case XX_VAR_TYPE_UINT8:
        case XX_VAR_TYPE_UINT16:
        case XX_VAR_TYPE_UINT32:
        case XX_VAR_TYPE_UINT64: return size <= xx_var_get_u64(limit);
        case XX_VAR_TYPE_INT8:
        case XX_VAR_TYPE_INT16:
        case XX_VAR_TYPE_INT32:
        case XX_VAR_TYPE_INT64: {
            int64_t value = xx_var_get_i64(limit);
            return value < 0 || size <= (uint64_t)value;
        }
        default: return true;
    }
}

bool xx_apple_sparse_bundle_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    xx_asb_private *parsed;
    const xx_var *option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *destination = NULL;
    xx_io_device *output = NULL;
    bool result;
    bool created = false;

    if (!self || !self->device || !state || state->format != self ||
        !state->has_record || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    parsed = (xx_asb_private *)state->internal_state;
    if (!parsed || parsed->consumed || !parsed->bundle_path) return false;
    /* Encrypted bands are ciphertext; they are not decrypted here. */
    if (parsed->is_encrypted) return false;
    if (!xx_asb_size_allowed(self, &state->options, parsed->media_size)) {
        return false;
    }

    option = xx_asb_find_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!option) return xx_asb_write_image(parsed, NULL, pd);
    if (option->type == XX_VAR_TYPE_STRING ||
        option->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(option);
    } else if (option->type == XX_VAR_TYPE_WSTRING ||
               option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(option));
        base = owned_base;
    }
    if (!base) {
        if (owned_base) xx_str_free(owned_base);
        return false;
    }
    destination = xx_asb_join(base, XX_ASB_MEMBER_NAME);
    if (owned_base) xx_str_free(owned_base);
    if (!destination) return false;
    if (!xx_store_create_dirs_a(destination, false)) {
        xx_str_free(destination);
        return false;
    }
    output = xx_io_file_open(destination, "wb");
    created = output != NULL;
    result = output != NULL && xx_asb_write_image(parsed, output, pd);
    if (output && xx_io_close(output) != 0) result = false;
    if (!result && created) xx_rt_remove(destination);
    xx_str_free(destination);
    return result;
}

void xx_apple_sparse_bundle_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

/* ------------------------------------------------------------ accessors -- */

uint64_t xx_apple_sparse_bundle_get_media_size(
    const xx_apple_sparse_bundle *bundle) {
    return bundle ? bundle->media_size : 0U;
}

uint64_t xx_apple_sparse_bundle_get_band_size(
    const xx_apple_sparse_bundle *bundle) {
    return bundle ? bundle->band_size : 0U;
}
