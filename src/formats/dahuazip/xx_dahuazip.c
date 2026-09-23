/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/dahuazip/xx_dahuazip.h"
#include "xxfclib/formats/zip/xx_zip.h"

#include "xxfclib/data/xx_data.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the file-type constant resolves to UNKNOWN until the enumerator
 * lands.  Delete this block once XX_FILE_TYPE_DAHUA_ZIP exists in the enum. */
#ifdef DAHUA_ZIP
#define XX_DAHUAZIP_FILE_TYPE XX_FILE_TYPE_DAHUA_ZIP
#else
#define XX_DAHUAZIP_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

/* binwalk's find_zip_eof() greps for this eight byte string, i.e. an EOCD
 * whose "this disk" and "central directory disk" numbers are both zero. */
static const uint8_t xx_dahuazip_eocd_magic[8] = {
    'P', 'K', 0x05U, 0x06U, 0x00U, 0x00U, 0x00U, 0x00U
};
#define XX_DAHUAZIP_EOCD_MAGIC_SIZE 8U

/* Reserved general-purpose flag bits (structures/zip.rs UNUSED_FLAGS_MASK). */
#define XX_DAHUAZIP_UNUSED_FLAGS_MASK 0xD780U

/* Chunk used for the forward EOCD scan.  The scan is linear in the carve and
 * never holds more than this much of the input in memory. */
#define XX_DAHUAZIP_SCAN_CHUNK ((size_t)65536U)

/* Declared-size ceiling handed to xx_zip for every member unless the caller
 * supplies XX_META_ID_OPT_MAX_MEMBER_SIZE itself.  xx_zip already clamps the
 * decoded output of a member to its declared size, so this bounds the output
 * of any single member regardless of what the archive claims. */
#define XX_DAHUAZIP_DEFAULT_MAX_MEMBER ((uint64_t)1024U * 1024U * 1024U)

typedef struct xx_dahuazip_private_s {
    int64_t input_size;
    int64_t header_offset;
    int64_t eocd_offset;  /* absolute */
    int64_t archive_end;  /* absolute, one past the archive comment */
    uint64_t count;
    uint16_t version;
    uint16_t flags;
    uint16_t method;
    uint16_t comment_size;
    bool is_zip64;
} xx_dahuazip_private;

/* ------------------------------------------------------------------------ */
/* The patched view                                                          */
/* ------------------------------------------------------------------------ */

/*
 * A read-only device over [base, base + size) of the parent device in which
 * view bytes 0 and 1 read as "PK" instead of "DH".  This is byte for byte the
 * file binwalk's extract_dahua_zip() writes ("PK" followed by the carve from
 * offset 2), produced on the fly: nothing is copied, reads past the end of
 * the carve return EOF, and the parent is never written, seeked beyond the
 * carve, or closed.  xx_io_close() on the view frees it.
 */
typedef struct xx_dahuazip_view_s {
    xx_io_device device;
    xx_io_device *parent;
    int64_t base;
    int64_t size;
    int64_t position;
} xx_dahuazip_view;

static ssize_t xx_dahuazip_view_read(xx_io_device *device, void *buffer,
                                     size_t size) {
    xx_dahuazip_view *view =
        device ? (xx_dahuazip_view *)device->priv : NULL;
    uint8_t *out = (uint8_t *)buffer;
    size_t wanted;
    size_t done = 0U;
    size_t index;
    if (!view || (!buffer && size != 0U) || view->position < 0 ||
        view->position > view->size) {
        return -1;
    }
    if (size == 0U || view->position == view->size) return 0;
    wanted = size;
    if ((uint64_t)wanted > (uint64_t)(view->size - view->position)) {
        wanted = (size_t)(view->size - view->position);
    }
    if (wanted > (SIZE_MAX >> 1)) wanted = SIZE_MAX >> 1; /* fits ssize_t */
    if (xx_io_seek64(view->parent, view->base + view->position, SEEK_SET) !=
        0) {
        return -1;
    }
    while (done < wanted) {
        ssize_t got = xx_io_read(view->parent, out + done, wanted - done);
        if (got <= 0 || (size_t)got > wanted - done) break;
        done += (size_t)got;
    }
    /* The carve was checked against the parent size when the view was
     * opened, so a short parent read is an I/O error, not the end. */
    if (done == 0U) return -1;
    for (index = 0U; index < XX_DAHUAZIP_PATCH_SIZE; ++index) {
        if ((int64_t)index >= view->position &&
            (uint64_t)((int64_t)index - view->position) < (uint64_t)done) {
            out[(size_t)((int64_t)index - view->position)] =
                (uint8_t)("PK"[index]);
        }
    }
    view->position += (int64_t)done;
    return (ssize_t)done;
}

static int xx_dahuazip_view_seek64(xx_io_device *device, int64_t offset,
                                   int whence) {
    xx_dahuazip_view *view =
        device ? (xx_dahuazip_view *)device->priv : NULL;
    int64_t origin;
    if (!view) return -1;
    switch (whence) {
        case SEEK_SET: origin = 0; break;
        case SEEK_CUR: origin = view->position; break;
        case SEEK_END: origin = view->size; break;
        default: return -1;
    }
    if ((offset > 0 && origin > INT64_MAX - offset) ||
        (offset < 0 && origin < INT64_MIN - offset)) {
        return -1;
    }
    origin += offset;
    if (origin < 0 || origin > view->size) return -1;
    view->position = origin;
    return 0;
}

static int xx_dahuazip_view_seek(xx_io_device *device, long offset,
                                 int whence) {
    return xx_dahuazip_view_seek64(device, (int64_t)offset, whence);
}

static int64_t xx_dahuazip_view_tell(xx_io_device *device) {
    xx_dahuazip_view *view =
        device ? (xx_dahuazip_view *)device->priv : NULL;
    return view ? view->position : -1;
}

static int64_t xx_dahuazip_view_size(xx_io_device *device) {
    xx_dahuazip_view *view =
        device ? (xx_dahuazip_view *)device->priv : NULL;
    return view ? view->size : -1;
}

static int xx_dahuazip_view_close(xx_io_device *device) {
    xx_dahuazip_view *view =
        device ? (xx_dahuazip_view *)device->priv : NULL;
    if (!view) return -1;
    /* The parent is borrowed; only the view itself is released. */
    xx_mem_zero(view, sizeof(*view));
    xx_mem_free(view);
    return 0;
}

static xx_io_device *xx_dahuazip_view_open(xx_io_device *parent, int64_t base,
                                           int64_t size) {
    xx_dahuazip_view *view;
    int64_t parent_size = xx_io_total_size(parent);
    if (!parent || base < 0 || size < (int64_t)XX_DAHUAZIP_PATCH_SIZE ||
        parent_size < 0 || base > parent_size || size > parent_size - base) {
        return NULL;
    }
    view = (xx_dahuazip_view *)xx_mem_calloc(1U, sizeof(*view));
    if (!view) return NULL;
    view->parent = parent;
    view->base = base;
    view->size = size;
    view->position = 0;
    view->device.priv = view;
    view->device.read = xx_dahuazip_view_read;
    view->device.write = NULL; /* read-only */
    view->device.seek = xx_dahuazip_view_seek;
    view->device.seek64 = xx_dahuazip_view_seek64;
    view->device.tell = xx_dahuazip_view_tell;
    view->device.total_size = xx_dahuazip_view_size;
    view->device.get_total_size = xx_dahuazip_view_size;
    view->device.size = xx_dahuazip_view_size;
    view->device.close = xx_dahuazip_view_close;
    return &view->device;
}

/* ------------------------------------------------------------------------ */
/* Helpers                                                                   */
/* ------------------------------------------------------------------------ */

static bool xx_dahuazip_read_at(xx_io_device *device, int64_t offset,
                                void *data, size_t size) {
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

/* True when [offset, offset + size) lies inside [0, total_size). */
static bool xx_dahuazip_range_within(int64_t total_size, int64_t offset,
                                     int64_t size) {
    return (total_size >= 0) && (offset >= 0) && (size >= 0) &&
           (offset <= total_size) && (size <= total_size - offset);
}

/* The compression methods binwalk's parse_zip_header() accepts. */
static bool xx_dahuazip_method_allowed(uint16_t method) {
    switch (method) {
        case 0: case 1: case 2: case 3: case 4: case 5: case 6:
        case 8: case 9: case 10: case 12: case 14:
        case 18: case 19: case 20:
        case 93: case 94: case 95: case 96: case 97: case 98: case 99:
            return true;
        default:
            return false;
    }
}

static void xx_dahuazip_private_reset(xx_dahuazip_private *parsed) {
    if (!parsed) return;
    xx_mem_zero(parsed, sizeof(*parsed));
    parsed->input_size = -1;
    parsed->header_offset = -1;
    parsed->eocd_offset = -1;
    parsed->archive_end = -1;
}

/*
 * binwalk's find_zip_eof(): the FIRST occurrence, scanning forward from the
 * "DH" header, of "PK\x05\x06\0\0\0\0" that has a full 22-byte record behind
 * it whose "entries on this disk" equals "total entries" and is non-zero.
 * Later EOCDs are ignored, which is what makes the carve length the first
 * plausible end rather than the last.
 */
static bool xx_dahuazip_find_eocd(xx_io_device *device, int64_t start,
                                  int64_t total, xx_pd_struct *pd,
                                  int64_t *eocd_offset,
                                  uint8_t eocd[XX_DAHUAZIP_EOCD_SIZE]) {
    uint8_t *buffer;
    int64_t position = start;
    bool found = false;
    if (!device || !eocd_offset || !eocd || start < 0 || total < start) {
        return false;
    }
    *eocd_offset = -1;
    buffer = (uint8_t *)xx_mem_alloc(XX_DAHUAZIP_SCAN_CHUNK);
    if (!buffer) return false;
    /* Each pass consumes at least SCAN_CHUNK - 7 new bytes, so the loop runs
     * at most total / (SCAN_CHUNK - 7) + 1 times. */
    while (!found && total - position >= (int64_t)XX_DAHUAZIP_EOCD_SIZE) {
        size_t length = XX_DAHUAZIP_SCAN_CHUNK;
        size_t index = 0U;
        if ((uint64_t)(total - position) < (uint64_t)length) {
            length = (size_t)(total - position);
        }
        if ((pd && xx_pd_is_stopped(pd)) ||
            !xx_dahuazip_read_at(device, position, buffer, length)) {
            break;
        }
        while (index + XX_DAHUAZIP_EOCD_MAGIC_SIZE <= length) {
            const uint8_t *hit = (const uint8_t *)xx_rt_memchr(
                buffer + index, 'P',
                length - index - (XX_DAHUAZIP_EOCD_MAGIC_SIZE - 1U));
            int64_t candidate;
            if (!hit) break;
            index = (size_t)(hit - buffer);
            candidate = position + (int64_t)index;
            if (xx_rt_memcmp(hit, xx_dahuazip_eocd_magic,
                             XX_DAHUAZIP_EOCD_MAGIC_SIZE) == 0 &&
                total - candidate >= (int64_t)XX_DAHUAZIP_EOCD_SIZE) {
                bool have = false;
                if (index + XX_DAHUAZIP_EOCD_SIZE <= length) {
                    xx_rt_memcpy(eocd, hit, XX_DAHUAZIP_EOCD_SIZE);
                    have = true;
                } else {
                    have = xx_dahuazip_read_at(device, candidate, eocd,
                                               XX_DAHUAZIP_EOCD_SIZE);
                }
                if (have) {
                    uint16_t on_disk = xx_data_get_u16(
                        eocd, XX_DAHUAZIP_EOCD_SIZE, 8U, false);
                    uint16_t entries = xx_data_get_u16(
                        eocd, XX_DAHUAZIP_EOCD_SIZE, 10U, false);
                    if (on_disk == entries && entries != 0U) {
                        *eocd_offset = candidate;
                        found = true;
                        break;
                    }
                }
            }
            ++index;
        }
        if (found || position + (int64_t)length >= total) break;
        /* Re-read the last seven bytes: a magic that starts there was not
         * examined in this pass. */
        position += (int64_t)(length - (XX_DAHUAZIP_EOCD_MAGIC_SIZE - 1U));
    }
    xx_mem_free(buffer);
    return found;
}

/* ------------------------------------------------------------------------ */
/* The library ZIP reader over the view                                      */
/* ------------------------------------------------------------------------ */

typedef struct xx_dahuazip_session_s {
    xx_io_device *view;
    xx_zip *zip;
    xx_archive_record_state *zip_state;
} xx_dahuazip_session;

static void xx_dahuazip_session_close(xx_dahuazip_session *session) {
    if (!session) return;
    /* Order matters: the record state refers to the zip object, which
     * refers to the view. */
    if (session->zip_state) {
        xx_format_free_archive_records_reading(&session->zip->format,
                                               session->zip_state);
        session->zip_state = NULL;
    }
    if (session->zip) {
        xx_zip_free(session->zip);
        session->zip = NULL;
    }
    if (session->view) {
        (void)xx_io_close(session->view);
        session->view = NULL;
    }
}

/* Opens the view over [header, end) and lets xx_zip parse it.  Succeeds only
 * when xx_zip settles on the same EOCD binwalk chose, sees no split set, and
 * its first central directory entry is the patched local header at view
 * offset 0 - i.e. the "DH" header really is the archive's first member. */
static bool xx_dahuazip_session_open(Abstractformat *self,
                                     const xx_dahuazip_private *parsed,
                                     const xx_list_s *options,
                                     xx_dahuazip_session *session,
                                     xx_pd_struct *pd) {
    int64_t view_size;
    const xx_archive_record *first;
    if (!self || !parsed || !session) return false;
    xx_mem_zero(session, sizeof(*session));
    if (parsed->header_offset < 0 || parsed->archive_end <= parsed->header_offset ||
        parsed->eocd_offset < parsed->header_offset) {
        return false;
    }
    view_size = parsed->archive_end - parsed->header_offset;
    session->view =
        xx_dahuazip_view_open(self->device, parsed->header_offset, view_size);
    if (!session->view) goto fail;
    session->zip = xx_zip_create(session->view, 0);
    if (!session->zip) goto fail;
    /* Format-wide parameters (password, overwrite, limits) apply to the
     * members, so they are handed on to the inner reader unchanged. */
    {
        size_t index;
        for (index = 0U; index < self->list_extra_parameters.count; ++index) {
            const xx_meta *item = (const xx_meta *)xx_list_at(
                (const xx_list_t *)&self->list_extra_parameters, index);
            if (item && !xx_format_set_extra_parameter(
                            &session->zip->format, item->meta_id,
                            &item->var)) {
                goto fail;
            }
        }
    }
    if (!xx_format_resolve_extra_parameter(self, options,
                                           XX_META_ID_OPT_MAX_MEMBER_SIZE)) {
        xx_var limit;
        bool stored;
        xx_var_init(&limit);
        xx_var_set_u64(&limit, XX_DAHUAZIP_DEFAULT_MAX_MEMBER);
        stored = xx_format_set_extra_parameter(
            &session->zip->format, XX_META_ID_OPT_MAX_MEMBER_SIZE, &limit);
        xx_var_cleanup(&limit);
        if (!stored) goto fail;
    }
    if (!xx_format_handle_base_info(&session->zip->format, pd) ||
        !session->zip->format.is_valid || session->zip->is_split ||
        session->zip->eocd_offset !=
            parsed->eocd_offset - parsed->header_offset ||
        session->zip->format.format_size != view_size ||
        session->zip->number_of_records == 0U) {
        goto fail;
    }
    session->zip_state = xx_format_create_archive_records_reading(
        &session->zip->format, options, pd);
    if (!session->zip_state) goto fail;
    first = xx_format_get_current_archive_record(&session->zip->format,
                                                 session->zip_state);
    if (!first || first->header_offset != 0 ||
        first->data_offset < (int64_t)XX_DAHUAZIP_LOCAL_HEADER_SIZE) {
        goto fail;
    }
    return true;
fail:
    xx_dahuazip_session_close(session);
    return false;
}

/*
 * The binwalk checks, in binwalk's order: the "DH\x03\x04" magic, a 30-byte
 * local header whose reserved flag bits are clear and whose method is one of
 * the defined ZIP methods, then the first plausible EOCD, whose comment must
 * end inside the input (binwalk drops a result that runs past EOF).  binwalk
 * would also accept a Dahua ZIP with no EOCD at all by walking the local
 * headers, but its extractor cannot carve that and there is no directory to
 * list, so such input is rejected here.  Finally the library ZIP reader must
 * accept the patched view.
 */
static bool xx_dahuazip_parse(Abstractformat *self,
                              xx_dahuazip_private *parsed, xx_pd_struct *pd) {
    uint8_t header[XX_DAHUAZIP_LOCAL_HEADER_SIZE];
    uint8_t eocd[XX_DAHUAZIP_EOCD_SIZE];
    xx_dahuazip_session session;
    int64_t eocd_offset = -1;
    xx_dahuazip_private_reset(parsed);
    if (!self || !self->device || !parsed || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    parsed->input_size = xx_io_total_size(self->device);
    parsed->header_offset = self->base_address;
    if (!xx_dahuazip_range_within(parsed->input_size, self->base_address,
                                  (int64_t)(XX_DAHUAZIP_LOCAL_HEADER_SIZE +
                                            XX_DAHUAZIP_EOCD_SIZE)) ||
        !xx_dahuazip_read_at(self->device, self->base_address, header,
                             sizeof(header)) ||
        xx_rt_memcmp(header, XX_DAHUAZIP_MAGIC, XX_DAHUAZIP_MAGIC_SIZE) != 0) {
        goto fail;
    }
    parsed->version = xx_data_get_u16(header, sizeof(header), 4U, false);
    parsed->flags = xx_data_get_u16(header, sizeof(header), 6U, false);
    parsed->method = xx_data_get_u16(header, sizeof(header), 8U, false);
    if ((parsed->flags & XX_DAHUAZIP_UNUSED_FLAGS_MASK) != 0U ||
        !xx_dahuazip_method_allowed(parsed->method)) {
        goto fail;
    }
    if (!xx_dahuazip_find_eocd(self->device, self->base_address,
                               parsed->input_size, pd, &eocd_offset, eocd)) {
        goto fail;
    }
    parsed->eocd_offset = eocd_offset;
    parsed->comment_size = xx_data_get_u16(eocd, sizeof(eocd), 20U, false);
    if (!xx_dahuazip_range_within(
            parsed->input_size, eocd_offset,
            (int64_t)XX_DAHUAZIP_EOCD_SIZE + (int64_t)parsed->comment_size)) {
        goto fail;
    }
    parsed->archive_end = eocd_offset + (int64_t)XX_DAHUAZIP_EOCD_SIZE +
                          (int64_t)parsed->comment_size;
    if (parsed->archive_end - parsed->header_offset <
        (int64_t)(XX_DAHUAZIP_LOCAL_HEADER_SIZE + XX_DAHUAZIP_EOCD_SIZE)) {
        goto fail;
    }
    if (!xx_dahuazip_session_open(self, parsed, NULL, &session, pd)) goto fail;
    parsed->count = session.zip->number_of_records;
    parsed->is_zip64 = session.zip->is_zip64;
    xx_dahuazip_session_close(&session);
    return true;
fail:
    xx_dahuazip_private_reset(parsed);
    return false;
}

/* ------------------------------------------------------------------------ */
/* Record plumbing                                                           */
/* ------------------------------------------------------------------------ */

typedef struct xx_dahuazip_archive_stream_s {
    xx_dahuazip_session session;
    int64_t base;
} xx_dahuazip_archive_stream;

static void xx_dahuazip_archive_stream_free(void *pointer) {
    xx_dahuazip_archive_stream *stream =
        (xx_dahuazip_archive_stream *)pointer;
    if (!stream) return;
    xx_dahuazip_session_close(&stream->session);
    xx_mem_free(stream);
}

static bool xx_dahuazip_copy_options(xx_list_s *destination,
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

/* Publishes one of xx_zip's records with its offsets moved from view
 * coordinates to device coordinates. */
static bool xx_dahuazip_copy_record(xx_archive_record *destination,
                                    const xx_archive_record *source,
                                    int64_t base) {
    size_t index;
    if (!destination || !source) return false;
    xx_archive_record_cleanup(destination);
    xx_archive_record_init(destination);
    if ((source->header_offset > 0 &&
         base > INT64_MAX - source->header_offset) ||
        (source->data_offset > 0 && base > INT64_MAX - source->data_offset)) {
        return false;
    }
    destination->header_offset =
        source->header_offset >= 0 ? source->header_offset + base : -1;
    destination->header_size = source->header_size;
    destination->data_offset =
        source->data_offset >= 0 ? source->data_offset + base : -1;
    destination->compressed_size = source->compressed_size;
    for (index = 0U; index < source->list_meta.count; ++index) {
        const xx_meta *item = (const xx_meta *)xx_list_at(
            (const xx_list_t *)&source->list_meta, index);
        if (item && !xx_archive_record_add_meta(destination, item->meta_id,
                                                &item->var)) {
            xx_archive_record_cleanup(destination);
            xx_archive_record_init(destination);
            return false;
        }
    }
    return true;
}

/* Member names come from the archive.  xx_zip already refuses absolute,
 * drive-qualified and dot-segment names; control characters are refused here
 * as well before anything reaches the file system. */
#define XX_DAHUAZIP_NAME_CHECK(name, type)                                   \
    do {                                                                     \
        const type *segment = (name);                                        \
        const type *cursor = (name);                                         \
        if ((name)[0] == '/' || (name)[0] == '\\' ||                         \
            ((name)[0] != 0 && (name)[1] == ':')) {                          \
            return false;                                                    \
        }                                                                    \
        for (;; ++cursor) {                                                  \
            bool at_end = *cursor == 0;                                      \
            if (!at_end && ((unsigned long)*cursor < 0x20UL ||               \
                            (unsigned long)*cursor == 0x7FUL ||              \
                            *cursor == ':')) {                               \
                return false;                                                \
            }                                                                \
            if (at_end || *cursor == '/' || *cursor == '\\') {               \
                size_t length = (size_t)(cursor - segment);                  \
                if (length == 2U && segment[0] == '.' && segment[1] == '.') {\
                    return false;                                            \
                }                                                            \
                if (at_end) break;                                           \
                segment = cursor + 1;                                        \
            }                                                                \
        }                                                                    \
    } while (0)

static bool xx_dahuazip_wide_name_is_safe(const wchar_t *name) {
    if (!name) return true;
    XX_DAHUAZIP_NAME_CHECK(name, wchar_t);
    return true;
}

static bool xx_dahuazip_name_is_safe(const char *name) {
    if (!name) return true;
    XX_DAHUAZIP_NAME_CHECK(name, char);
    return true;
}

#undef XX_DAHUAZIP_NAME_CHECK

static bool xx_dahuazip_record_name_is_safe(const xx_archive_record *record) {
    const wchar_t *wide = xx_archive_record_get_original_name_w(record);
    if (wide) return xx_dahuazip_wide_name_is_safe(wide);
    return xx_dahuazip_name_is_safe(
        xx_archive_record_get_original_name(record));
}

/* ------------------------------------------------------------------------ */
/* Public interface                                                          */
/* ------------------------------------------------------------------------ */

static void xx_dahuazip_vtable_destroy(Abstractformat *self);

void xx_dahuazip_init(xx_dahuazip *dahuazip, xx_io_device *dev,
                      int64_t base_address) {
    if (!dahuazip) return;
    xx_mem_zero(dahuazip, sizeof(*dahuazip));
    xx_format_init(&dahuazip->format, dev, base_address);
    dahuazip->format.endian = XX_ENDIAN_LITTLE;
    dahuazip->format.file_type = XX_DAHUAZIP_FILE_TYPE;
    dahuazip->format.format_type = XX_TYPE_ARCHIVE;
    dahuazip->format.is_archive = true;
    xx_format_set_mime_type(&dahuazip->format,
                            "application/x-dahua-firmware");
    xx_format_set_extension(&dahuazip->format, "bin");
    dahuazip->format.check_is_valid = xx_dahuazip_check_is_valid;
    dahuazip->format.handle_base_info = xx_dahuazip_handle_base_info;
    dahuazip->format.get_format_size = xx_dahuazip_get_format_size;
    dahuazip->format.get_number_of_archive_records =
        xx_dahuazip_get_number_of_archive_records;
    dahuazip->format.create_archive_records_reading =
        xx_dahuazip_create_archive_records_reading;
    dahuazip->format.get_current_archive_record =
        xx_dahuazip_get_current_archive_record;
    dahuazip->format.unpack_current_archive_record =
        xx_dahuazip_unpack_current_archive_record;
    dahuazip->format.archive_record_move_to_next =
        xx_dahuazip_archive_record_move_to_next;
    dahuazip->format.free_archive_records_reading =
        xx_dahuazip_free_archive_records_reading;
    dahuazip->format.destroy = xx_dahuazip_vtable_destroy;
    dahuazip->eocd_offset = -1;
    dahuazip->archive_end = -1;
}

xx_dahuazip *xx_dahuazip_create(xx_io_device *dev, int64_t base_address) {
    xx_dahuazip *dahuazip = (xx_dahuazip *)xx_mem_alloc(sizeof(*dahuazip));
    if (dahuazip) xx_dahuazip_init(dahuazip, dev, base_address);
    return dahuazip;
}

void xx_dahuazip_destroy(xx_dahuazip *dahuazip) {
    if (!dahuazip) return;
    if (dahuazip->internal) {
        xx_dahuazip_private_reset((xx_dahuazip_private *)dahuazip->internal);
        xx_mem_free(dahuazip->internal);
        dahuazip->internal = NULL;
    }
    xx_format_cleanup_extra_parameters(&dahuazip->format);
}

static void xx_dahuazip_vtable_destroy(Abstractformat *self) {
    xx_dahuazip_destroy((xx_dahuazip *)self);
}

void xx_dahuazip_free(xx_dahuazip *dahuazip) {
    if (!dahuazip) return;
    xx_dahuazip_destroy(dahuazip);
    xx_mem_free(dahuazip);
}

bool xx_dahuazip_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_dahuazip_private parsed;
    bool result = xx_dahuazip_parse(self, &parsed, pd);
    xx_dahuazip_private_reset(&parsed);
    return result;
}

bool xx_dahuazip_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_dahuazip_private *parsed;
    xx_dahuazip *dahuazip = (xx_dahuazip *)self;
    int64_t total_size;
    if (!self || !dahuazip) return false;
    parsed = (xx_dahuazip_private *)xx_mem_alloc(sizeof(*parsed));
    if (!parsed || !xx_dahuazip_parse(self, parsed, pd)) {
        if (parsed) xx_mem_free(parsed);
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    if (dahuazip->internal) {
        xx_dahuazip_private_reset((xx_dahuazip_private *)dahuazip->internal);
        xx_mem_free(dahuazip->internal);
    }
    dahuazip->internal = parsed;
    dahuazip->number_of_records = parsed->count;
    dahuazip->version_needed = parsed->version;
    dahuazip->flags = parsed->flags;
    dahuazip->compression = parsed->method;
    dahuazip->comment_size = parsed->comment_size;
    dahuazip->is_zip64 = parsed->is_zip64;
    dahuazip->eocd_offset = parsed->eocd_offset;
    dahuazip->archive_end = parsed->archive_end;
    self->format_size = parsed->archive_end - self->base_address;
    total_size = xx_io_total_size(self->device);
    if (total_size > parsed->archive_end) {
        self->overlay_offset = parsed->archive_end;
        self->overlay_size = total_size - parsed->archive_end;
    } else {
        self->overlay_offset = -1;
        self->overlay_size = 0;
    }
    self->file_type = XX_DAHUAZIP_FILE_TYPE;
    self->format_type = XX_TYPE_ARCHIVE;
    self->is_archive = true;
    self->number_of_archive_records = parsed->count;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_dahuazip_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return -1;
    }
    return self->format_size;
}

uint64_t xx_dahuazip_get_number_of_archive_records(Abstractformat *self,
                                                   xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return ((xx_dahuazip *)self)->number_of_records;
}

xx_archive_record_state *xx_dahuazip_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_record_state *state;
    xx_dahuazip_archive_stream *stream;
    xx_dahuazip *dahuazip = (xx_dahuazip *)self;
    const xx_archive_record *first;
    if (!self || !self->device ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd)) ||
        !dahuazip->internal) {
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    stream = (xx_dahuazip_archive_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!state || !stream) {
        if (state) xx_mem_free(state);
        if (stream) xx_mem_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    stream->base = self->base_address;
    state->internal_state = stream;
    state->free_internal = xx_dahuazip_archive_stream_free;
    if (!xx_dahuazip_copy_options(&state->options, options) ||
        !xx_dahuazip_session_open(
            self, (const xx_dahuazip_private *)dahuazip->internal,
            &state->options, &stream->session, pd)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->total_records = (int64_t)dahuazip->number_of_records;
    first = xx_format_get_current_archive_record(&stream->session.zip->format,
                                                 stream->session.zip_state);
    if (first) {
        if (!xx_dahuazip_copy_record(&state->current_record, first,
                                     stream->base)) {
            xx_archive_record_state_free(state);
            return NULL;
        }
        state->has_record = true;
        state->current_index = 0;
    }
    return state;
}

const xx_archive_record *xx_dahuazip_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_dahuazip_archive_record_move_to_next(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_dahuazip_archive_stream *stream;
    const xx_archive_record *next;
    if (!self || !state || state->format != self || !state->has_record ||
        !state->internal_state || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_dahuazip_archive_stream *)state->internal_state;
    xx_archive_record_cleanup(&state->current_record);
    xx_archive_record_init(&state->current_record);
    state->has_record = false;
    if (!stream->session.zip || !stream->session.zip_state ||
        !xx_format_archive_record_move_to_next(&stream->session.zip->format,
                                               stream->session.zip_state,
                                               pd)) {
        return false;
    }
    next = xx_format_get_current_archive_record(&stream->session.zip->format,
                                                stream->session.zip_state);
    if (!next ||
        !xx_dahuazip_copy_record(&state->current_record, next, stream->base)) {
        return false;
    }
    state->has_record = true;
    state->current_index = stream->session.zip_state->current_index;
    return true;
}

bool xx_dahuazip_unpack_current_archive_record(Abstractformat *self,
                                               xx_archive_record_state *state,
                                               xx_pd_struct *pd) {
    xx_dahuazip_archive_stream *stream;
    if (!self || !self->device || !state || state->format != self ||
        !state->has_record || !state->internal_state ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_dahuazip_archive_stream *)state->internal_state;
    if (!stream->session.zip || !stream->session.zip_state ||
        !stream->session.zip_state->has_record ||
        !xx_dahuazip_record_name_is_safe(&state->current_record)) {
        return false;
    }
    /* xx_zip decodes from the view, checks the CRC and writes the member
     * under XX_META_ID_OPT_UNPACK_PATH (or only verifies it without one). */
    return xx_format_unpack_current_archive_record(
        &stream->session.zip->format, stream->session.zip_state, pd);
}

void xx_dahuazip_free_archive_records_reading(Abstractformat *self,
                                              xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

uint64_t xx_dahuazip_get_number_of_records(const xx_dahuazip *dahuazip) {
    return dahuazip ? dahuazip->number_of_records : 0U;
}
int64_t xx_dahuazip_get_eocd_offset(const xx_dahuazip *dahuazip) {
    return dahuazip ? dahuazip->eocd_offset : -1;
}
int64_t xx_dahuazip_get_archive_end(const xx_dahuazip *dahuazip) {
    return dahuazip ? dahuazip->archive_end : -1;
}
bool xx_dahuazip_is_zip64(const xx_dahuazip *dahuazip) {
    return dahuazip ? dahuazip->is_zip64 : false;
}
