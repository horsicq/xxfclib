/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: ../XArchive/sfx/xsfx.cpp
 * Bounded independent carrier/container parser. No payload execution.
 */
#include "xxfclib/formats/sfx_rtpatch/xx_sfx_rtpatch.h"
#include "../sfx_arc/xx_fifth_wrapper_table.h"

#include "xxfclib/formats/rtpatch/xx_rtpatch.h"
static Abstractformat *nested_open(xx_io_device *d,int64_t at) { xx_rtpatch *r=xx_rtpatch_create(d,at); return r ? &r->format : NULL; }
static void nested_close(Abstractformat *f) { xx_rtpatch_free((xx_rtpatch *)f); }

/* RTPatch 3.20 begins its record area with a directory list followed by a
 * banner list.  A package made entirely of source-dependent delta programs
 * has no ordinary members in the nested reader, so inspect these two lists
 * after that reader has validated the package grammar. */
static bool rt_list(const uint8_t *bytes, size_t size, size_t start,
                    size_t *end, size_t *decoded, bool *directory) {
    uint16_t count;
    size_t at, output = 0U, i, j;
    bool paths = true;
    if (start > size || size - start < 2U) return false;
    count = pm_le16(bytes + start);
    if (!count || count > 4096U) return false;
    at = start + 2U;
    for (i = 0U; i < count; ++i) {
        size_t length;
        if (at >= size || !(length = bytes[at++]) || length > size - at ||
            bytes[at + length - 1U] || output > 65536U - length - 1U)
            return false;
        for (j = 0U; j + 1U < length; ++j) {
            unsigned char c = bytes[at + j];
            if (!c) return false;
            if (c < 0x21U || c > 0x7eU || c == '"' || c == '<' ||
                c == '>' || c == '|' || c == '?' || c == '*') paths = false;
        }
        if (length == 1U) paths = false;
        output += length + 1U;
        at += length;
    }
    *end = at;
    *decoded = output;
    *directory = paths;
    return true;
}
static bool w5_comments(Abstractformat *f, pm_stream *s, xx_pd_struct *pd) {
    static const uint8_t magic[] = {'K', '*'};
    uint8_t mz[64], ne[2];
    int64_t low, limit = pm_available(f), candidate;
    unsigned attempts = 0U;
    if (!w5_carrier(f, false, &low, pd) ||
        !pm_read(f, 0, mz, sizeof(mz)) ||
        pm_le32(mz + 60) < 64U ||
        !pm_read(f, pm_le32(mz + 60), ne, sizeof(ne)) ||
        xx_rt_memcmp(ne, "NE", 2)) return false;
    low = (int64_t)pm_le32(mz + 60) + 64;
    if (low >= limit || limit - low > 16 * 1024 * 1024) return false;
    while (low <= limit - 26 && attempts++ < 128U) {
        Abstractformat *nested;
        uint8_t *source = NULL, *plain = NULL;
        size_t source_size, first_end, second_end, first_raw, second_raw;
        size_t at, out = 0U, i;
        bool first_directory, second_directory;
        bool found = false;
        candidate = xx_io_find_bytes_buffer_optimize_ex(
            f->device, f->base_address + low, limit - low,
            magic, sizeof(magic), xx_get_file_buffer_size(), pd);
        if (candidate < 0 || wg_stop(pd)) return false;
        low = candidate - f->base_address + 1;
        nested = nested_open(f->device, candidate);
        if (!nested) return false;
        if (!xx_format_handle_base_info(nested, pd) ||
            nested->format_size < 26 ||
            !wg_range(limit, (uint64_t)(candidate - f->base_address),
                      (uint64_t)nested->format_size)) {
            nested_close(nested);
            continue;
        }
        source_size = (size_t)(nested->format_size < 65536 ?
                               nested->format_size : 65536);
        source = (uint8_t *)xx_mem_alloc(source_size);
        if (source && pm_read(f, candidate - f->base_address, source,
                              source_size) &&
            pm_le16(source + 2) == 320U &&
            rt_list(source, source_size, 34U, &first_end, &first_raw,
                    &first_directory) && first_directory &&
            rt_list(source, source_size, first_end, &second_end, &second_raw,
                    &second_directory) && !second_directory &&
            second_raw >= 2U && second_raw <= 65536U &&
            second_end <= source_size) {
            plain = (uint8_t *)xx_mem_alloc(second_raw);
            if (plain) {
                at = first_end + 2U;
                for (i = 0U; i < pm_le16(source + first_end); ++i) {
                    size_t length = source[at++];
                    xx_mem_copy(plain + out, source + at, length - 1U);
                    out += length - 1U;
                    plain[out++] = '\r'; plain[out++] = '\n';
                    at += length;
                }
                if (out == second_raw && at == second_end && !wg_stop(pd) &&
                    pm_add(f, s, "Comments.txt",
                           candidate - f->base_address + (int64_t)first_end,
                           (int64_t)(second_end - first_end))) {
                    pm_member *member = &s->items[s->count - 1U];
                    xx_rt_snprintf(member->name, sizeof(member->name),
                                   "Comments.txt");
                    member->memory = plain;
                    member->size = (int64_t)second_raw;
                    s->size = limit;
                    plain = NULL;
                    found = true;
                }
            }
        }
        xx_mem_free(source);
        xx_mem_free(plain);
        nested_close(nested);
        if (found) return true;
    }
    return false;
}
static bool w5_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) { static const uint8_t sig[]={0x4b,0x2a}; int64_t low;
    if (w5_comments(f, s, pd)) return true;
    if(!w5_carrier(f,false,&low,pd)) { return false; } low=64;
    return w5_embedded(f,s,low,sig,sizeof(sig),0,nested_open,nested_close,"payload.rtp",pd);
}

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd) {
    return w5_parse(f, s, pd) && wg_members(s, pd);
}

/* The 4.00 PE stubs put the complete package exactly at the PE overlay.
 * Publish its authenticated member table directly.  Older stubs retain the
 * existing comments/opaque-wrapper path below. */
static bool rt_nested_probe(xx_sfx_rtpatch *archive, xx_pd_struct *pd) {
    Abstractformat *f = &archive->format;
    xx_rtpatch *inner = NULL;
    uint8_t header[26];
    int64_t overlay, cabinet, cabinet_end, limit, cursor;
    if (archive->probe_mode != 0U) return archive->probe_mode == 1U;
    archive->probe_mode = 2U;
    limit = pm_available(f);
    if (limit < 64) return false;
    cursor = xx_io_tell(f->device);
    if (!wg_pe(f, &overlay, &cabinet, &cabinet_end, pd) ||
        overlay < 64 || overlay > limit - (int64_t)sizeof(header) ||
        !pm_read(f, overlay, header, sizeof(header)) ||
        xx_rt_memcmp(header, "K*", 2U) ||
        pm_le16(header + 2U) != 400U ||
        pm_le32(header + 0x14U) != 0U ||
        pm_le16(header + 0x18U) != 4U) goto done;
    inner = xx_rtpatch_create(f->device, f->base_address + overlay);
    if (!inner || !xx_format_handle_base_info(&inner->format, pd) ||
        inner->format.number_of_archive_records == 0U ||
        inner->format.number_of_archive_records > 4096U ||
        inner->format.format_size != limit - overlay) goto done;
    archive->inner = inner;
    archive->probe_mode = 1U;
    f->format_size = limit;
    f->number_of_archive_records = inner->format.number_of_archive_records;
    f->overlay_offset = -1;
    f->overlay_size = 0;
    f->is_valid = true;
    f->base_info_handled = true;
    inner = NULL;
done:
    if (cursor >= 0) (void)xx_io_seek64(f->device, cursor, SEEK_SET);
    xx_rtpatch_free(inner);
    return archive->probe_mode == 1U;
}

static int64_t rt_size(Abstractformat *f, xx_pd_struct *pd) {
    return f && (f->base_info_handled ||
                 xx_sfx_rtpatch_handle_base_info(f, pd)) ?
           f->format_size : -1;
}
static uint64_t rt_count(Abstractformat *f, xx_pd_struct *pd) {
    return f && (f->base_info_handled ||
                 xx_sfx_rtpatch_handle_base_info(f, pd)) ?
           f->number_of_archive_records : 0U;
}
static xx_archive_record_state *rt_records(Abstractformat *f,
                                            const xx_list_s *options,
                                            xx_pd_struct *pd) {
    xx_sfx_rtpatch *archive = (xx_sfx_rtpatch *)f;
    if (rt_nested_probe(archive, pd))
        return xx_format_create_archive_records_reading(
            &archive->inner->format, options, pd);
    return pm_create_records(f, options, pd);
}
static const xx_archive_record *rt_current(Abstractformat *f,
                                           xx_archive_record_state *state) {
    xx_sfx_rtpatch *archive = (xx_sfx_rtpatch *)f;
    return archive->inner ?
        xx_format_get_current_archive_record(&archive->inner->format, state) :
        pm_current(f, state);
}
static bool rt_next(Abstractformat *f, xx_archive_record_state *state,
                    xx_pd_struct *pd) {
    xx_sfx_rtpatch *archive = (xx_sfx_rtpatch *)f;
    return archive->inner ?
        xx_format_archive_record_move_to_next(&archive->inner->format,
                                               state, pd) :
        pm_next(f, state, pd);
}
static bool rt_unpack(Abstractformat *f, xx_archive_record_state *state,
                      xx_pd_struct *pd) {
    xx_sfx_rtpatch *archive = (xx_sfx_rtpatch *)f;
    return archive->inner ?
        xx_format_unpack_current_archive_record(&archive->inner->format,
                                                 state, pd) :
        pm_unpack(f, state, pd);
}
static void rt_free_records(Abstractformat *f,
                            xx_archive_record_state *state) {
    xx_sfx_rtpatch *archive = (xx_sfx_rtpatch *)f;
    if (archive->inner)
        xx_format_free_archive_records_reading(&archive->inner->format,
                                                state);
    else pm_free_records(f, state);
}

void xx_sfx_rtpatch_init(xx_sfx_rtpatch *archive, xx_io_device *device,
                          int64_t base_address) {
    Abstractformat *f;
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    f = &archive->format;
    pm_init(f, device, base_address, XX_FILE_TYPE_SFX_RTPATCH, "exe");
    f->is_executable = true;
    f->check_is_valid = xx_sfx_rtpatch_check_is_valid;
    f->handle_base_info = xx_sfx_rtpatch_handle_base_info;
    f->get_format_size = rt_size;
    f->get_number_of_archive_records = rt_count;
    f->create_archive_records_reading = rt_records;
    f->get_current_archive_record = rt_current;
    f->archive_record_move_to_next = rt_next;
    f->unpack_current_archive_record = rt_unpack;
    f->free_archive_records_reading = rt_free_records;
}
xx_sfx_rtpatch *xx_sfx_rtpatch_create(xx_io_device *device,
                                       int64_t base_address) {
    xx_sfx_rtpatch *archive =
        (xx_sfx_rtpatch *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_sfx_rtpatch_init(archive, device, base_address);
    return archive;
}
void xx_sfx_rtpatch_destroy(xx_sfx_rtpatch *archive) {
    if (!archive) return;
    xx_rtpatch_free(archive->inner);
    xx_format_cleanup_extra_parameters(&archive->format);
}
void xx_sfx_rtpatch_free(xx_sfx_rtpatch *archive) {
    if (!archive) return;
    xx_sfx_rtpatch_destroy(archive);
    xx_mem_free(archive);
}
bool xx_sfx_rtpatch_check_is_valid(Abstractformat *f, xx_pd_struct *pd) {
    if (!f) return false;
    if (rt_nested_probe((xx_sfx_rtpatch *)f, pd)) return true;
    return pm_valid(f, pd);
}
bool xx_sfx_rtpatch_handle_base_info(Abstractformat *f,
                                      xx_pd_struct *pd) {
    if (!f) return false;
    if (rt_nested_probe((xx_sfx_rtpatch *)f, pd)) return true;
    return pm_handle(f, pd);
}
