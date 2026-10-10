/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: ../XArchive/sfx/xsfx.cpp
 * Bounded independent carrier/container parser. No payload execution.
 */
#include "xxfclib/formats/sfx_zipcentral/xx_sfx_zipcentral.h"
#include "../common/xx_executable_carrier.h"
#include "xx_demolition_password.h"

/* A complete ZIP directory authenticates the payload independently of the
 * executable image. Some UPX PEs claim SizeOfHeaders=4096 while their first
 * raw section begins at 512; the general PE carrier check refuses those.
 * Retain its strict result when available, then use the bounded MZ image
 * size as a lower bound before validating the entire ZIP tail below. */
static bool sfx_carrier_zip_carrier(Abstractformat *f, int64_t *low, xx_pd_struct *pd) {
    return executable_carrier_carrier(f, low, false, false, pd) ||
           sfx_carrier_carrier(f, false, low, pd);
}

static bool sfx_carrier_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    int64_t limit=pm_available(f),low,begin,ecd,dir;size_t n,i;uint8_t *b;bool ok=false;
    if(!sfx_carrier_zip_carrier(f,&low,pd) || limit<22) { return false; } begin=limit>65557 ? limit-65557:0;n=(size_t)(limit-begin);b=(uint8_t *)xx_mem_alloc(n);if(!b || !pm_read(f,begin,b,n)) { if(b) xx_mem_free(b);return false; }
    for(i=n-21;i>0;--i) { size_t p=i-1;uint32_t bytes,offset;int64_t at;if(carrier_stop(pd)) break;
        if(xx_rt_memcmp(b+p,"PK\5\6",4) || p+22+xx_data_get_u16(b+p+20, 2, 0, false)!=n || xx_data_get_u16(b+p+4, 2, 0, false) || xx_data_get_u16(b+p+6, 2, 0, false) || !xx_data_get_u16(b+p+10, 2, 0, false) || xx_data_get_u16(b+p+10, 2, 0, false)==65535 || xx_data_get_u16(b+p+8, 2, 0, false)!=xx_data_get_u16(b+p+10, 2, 0, false)) continue;
        ecd=begin+(int64_t)p;bytes=xx_data_get_u32(b+p+12, 4, 0, false);offset=xx_data_get_u32(b+p+16, 4, 0, false);if(bytes>(uint64_t)ecd || offset==UINT32_MAX) continue;dir=ecd-bytes;at=dir-offset;
        if(at<0 || (at && at<low) || xx_data_get_u16(b+p+10, 2, 0, false)>4096) continue;
        { unsigned j;int64_t cp=dir;bool valid=true;uint8_t cd[46];for(j=0;j<xx_data_get_u16(b+p+10, 2, 0, false);++j) { int64_t local;if(carrier_stop(pd) || cp>ecd-46 || !pm_read(f,cp,cd,46) || xx_rt_memcmp(cd,"PK\1\2",4)) { valid=false;break; }if(xx_data_get_u32(cd+42, 4, 0, false)>(uint64_t)(limit-at)) { valid=false;break; }local=at+xx_data_get_u32(cd+42, 4, 0, false);if(local<low || (uint64_t)46+xx_data_get_u16(cd+28, 2, 0, false)+xx_data_get_u16(cd+30, 2, 0, false)+xx_data_get_u16(cd+32, 2, 0, false)>(uint64_t)(ecd-cp)) { valid=false;break; }cp+=46+xx_data_get_u16(cd+28, 2, 0, false)+xx_data_get_u16(cd+30, 2, 0, false)+xx_data_get_u16(cd+32, 2, 0, false); }if(!valid || cp!=ecd) continue; }
        if(at) { uint8_t local[4];if(!pm_read(f,at,local,4) || xx_rt_memcmp(local,"PK\3\4",4)) continue; }if(!carrier_zip(f,at,limit,pd)) continue;ok=executable_carrier_component(f,s,at,limit-at,"payload.zip");break;
    }xx_mem_free(b);return ok;
}

/* A few PKSFX writers store absolute local and directory offsets in the ZIP
 * records. Their local flags may also differ from the central flags in legacy
 * hint bits, while the encryption/descriptor bits, method, names and extents
 * remain consistent. Validate the entire directory before handing its records
 * directly to the ordinary ZIP reader. */
static bool sfx_carrier_legacy_same_name(Abstractformat *f, int64_t local_at,
                                int64_t central_at, uint16_t length,
                                int64_t local_header, int64_t *legacy_local,
                                wchar_t legacy_name[256], xx_pd_struct *pd) {
    uint8_t local[255], central[255];
    unsigned mismatches = 0U;
    size_t i;
    if (carrier_equal(f, local_at, central_at, length, pd)) return true;
    if (carrier_stop(pd) || *legacy_local >= 0 || !length || length > 255U ||
        !pm_read(f, local_at, local, length) ||
        !pm_read(f, central_at, central, length)) return false;
    for (i = 0U; i < length; ++i) {
        if (local[i] == 0xedU && central[i] == 0xa1U && !mismatches) {
            /* 0xed in the local ANSI spelling and 0xa1 in the central DOS
             * spelling both represent U+00ED. The producer mixed code pages
             * in one otherwise identical name. */
            legacy_name[i] = (wchar_t)0x00edU;
            ++mismatches;
        } else if (local[i] == central[i] && local[i] >= 0x20U &&
                   local[i] < 0x7fU) {
            legacy_name[i] = (wchar_t)local[i];
        } else {
            return false;
        }
    }
    if (mismatches != 1U) return false;
    legacy_name[length] = L'\0';
    *legacy_local = local_header;
    return true;
}

static bool sfx_carrier_absolute_directory(Abstractformat *f, int64_t low, int64_t dir,
                                  int64_t eocd, uint16_t count,
                                  int64_t *first, int64_t *legacy_local,
                                  wchar_t legacy_name[256], xx_pd_struct *pd) {
    carrier_extent *ranges;
    int64_t cp = dir, minimum = INT64_MAX;
    unsigned j;
    bool ok = false;
    if (!count || count > 4096U || dir < low || dir >= eocd) return false;
    *legacy_local = -1;
    legacy_name[0] = L'\0';
    ranges = (carrier_extent *)xx_mem_alloc((size_t)count * sizeof(*ranges));
    if (!ranges) return false;
    for (j = 0; j < count; ++j) {
        uint8_t cd[46], local[30], descriptor[12];
        uint16_t fn, extra, comment, lfn, lextra, flags, local_flags;
        uint32_t packed, raw, crc;
        int64_t at, record, end;
        if (carrier_stop(pd) || cp > eocd - 46 || !pm_read(f, cp, cd, sizeof(cd)) ||
            xx_rt_memcmp(cd, "PK\1\2", 4) || xx_data_get_u16(cd + 34, 2, 0, false)) goto done;
        fn = xx_data_get_u16(cd + 28, 2, 0, false);
        extra = xx_data_get_u16(cd + 30, 2, 0, false);
        comment = xx_data_get_u16(cd + 32, 2, 0, false);
        record = 46 + (int64_t)fn + extra + comment;
        packed = xx_data_get_u32(cd + 20, 4, 0, false);
        raw = xx_data_get_u32(cd + 24, 4, 0, false);
        crc = xx_data_get_u32(cd + 16, 4, 0, false);
        at = (int64_t)xx_data_get_u32(cd + 42, 4, 0, false);
        flags = xx_data_get_u16(cd + 8, 2, 0, false);
        if (!fn || record > eocd - cp || packed == UINT32_MAX ||
            raw == UINT32_MAX || at < low || at > dir - 30 ||
            !pm_read(f, at, local, sizeof(local)) ||
            xx_rt_memcmp(local, "PK\3\4", 4)) goto done;
        lfn = xx_data_get_u16(local + 26, 2, 0, false);
        lextra = xx_data_get_u16(local + 28, 2, 0, false);
        local_flags = xx_data_get_u16(local + 6, 2, 0, false);
        if (lfn != fn || xx_data_get_u16(local + 8, 2, 0, false) != xx_data_get_u16(cd + 10, 2, 0, false) ||
            ((local_flags ^ flags) & 0x0049U) ||
            (uint64_t)30 + lfn + lextra + packed > (uint64_t)(dir - at) ||
            !sfx_carrier_legacy_same_name(f, at + 30, cp + 46, fn, at,
                                 legacy_local, legacy_name, pd)) goto done;
        end = at + 30 + lfn + lextra + packed;
        if (flags & 8U) {
            uint8_t marker[4];
            if (dir - end < 12 || !pm_read(f, end, marker, sizeof(marker)))
                goto done;
            if (!xx_rt_memcmp(marker, "PK\7\10", 4)) end += 4;
            if (dir - end < 12 || !pm_read(f, end, descriptor,
                                           sizeof(descriptor)) ||
                xx_data_get_u32(descriptor, 4, 0, false) != crc ||
                xx_data_get_u32(descriptor + 4, 4, 0, false) != packed ||
                xx_data_get_u32(descriptor + 8, 4, 0, false) != raw) goto done;
            end += 12;
        } else if (xx_data_get_u32(local + 14, 4, 0, false) != crc ||
                   xx_data_get_u32(local + 18, 4, 0, false) != packed ||
                   xx_data_get_u32(local + 22, 4, 0, false) != raw) goto done;
        ranges[j].lo = at;
        ranges[j].hi = end;
        if (at < minimum) minimum = at;
        cp += record;
    }
    ok = cp == eocd && carrier_extents(ranges, count, pd) && minimum >= low;
    if (ok) *first = minimum;
done:
    xx_mem_free(ranges);
    return ok;
}

static bool sfx_carrier_absolute_ensure(xx_sfx_zipcentral *r, xx_pd_struct *pd) {
    Abstractformat *f = &r->format;
    int64_t limit = pm_available(f), low, begin;
    size_t n, i;
    uint8_t *tail;
    if (r->checked_absolute) return r->inner_ready;
    r->checked_absolute = true;
    if (f->base_address != 0 || limit < 22 ||
        !sfx_carrier_zip_carrier(f, &low, pd)) return false;
    begin = limit > 65557 ? limit - 65557 : 0;
    n = (size_t)(limit - begin);
    tail = (uint8_t *)xx_mem_alloc(n);
    if (!tail || !pm_read(f, begin, tail, n)) {
        xx_mem_free(tail);
        return false;
    }
    for (i = n - 21; i > 0; --i) {
        size_t p = i - 1;
        int64_t eocd, dir, first, legacy_local;
        wchar_t legacy_name[256];
        uint16_t count;
        uint32_t bytes, offset;
        if (carrier_stop(pd)) break;
        if (xx_rt_memcmp(tail + p, "PK\5\6", 4) ||
            p + 22 + xx_data_get_u16(tail + p + 20, 2, 0, false) != n ||
            xx_data_get_u16(tail + p + 4, 2, 0, false) || xx_data_get_u16(tail + p + 6, 2, 0, false) ||
            !(count = xx_data_get_u16(tail + p + 10, 2, 0, false)) ||
            count != xx_data_get_u16(tail + p + 8, 2, 0, false)) continue;
        eocd = begin + (int64_t)p;
        bytes = xx_data_get_u32(tail + p + 12, 4, 0, false);
        offset = xx_data_get_u32(tail + p + 16, 4, 0, false);
        if (bytes > (uint64_t)eocd || offset == UINT32_MAX) continue;
        dir = eocd - bytes;
        if (dir != (int64_t)offset ||
            !sfx_carrier_absolute_directory(f, low, dir, eocd, count, &first,
                                   &legacy_local, legacy_name, pd))
            continue;
        xx_zip_init(&r->inner, f->device, first);
        if (xx_zip_check_is_valid(&r->inner.format, pd) &&
            xx_zip_handle_base_info(&r->inner.format, pd) &&
            r->inner.cd_offset == dir && r->inner.eocd_offset == eocd &&
            r->inner.format.number_of_archive_records == count &&
            r->inner.format.format_size == limit) {
            if (legacy_local >= 0) {
                r->legacy_name = xx_str_wdup(legacy_name);
                if (!r->legacy_name) {
                    xx_zip_destroy(&r->inner);
                    continue;
                }
                r->legacy_local_offset = legacy_local;
            }
            r->inner_ready = true;
            break;
        }
        xx_zip_destroy(&r->inner);
    }
    xx_mem_free(tail);
    return r->inner_ready;
}

/* The native ZIP reader interprets central and local offsets from device
 * position zero. A bounded view makes that coordinate system match a ZIP
 * embedded after an executable while keeping every read inside its tail. */
typedef struct sfx_carrier_view {
    xx_io_device device;
    xx_io_device *source;
    int64_t begin;
    int64_t length;
    int64_t position;
} sfx_carrier_view;

static ssize_t sfx_carrier_view_read(xx_io_device *device, void *buffer, size_t size) {
    sfx_carrier_view *view = (sfx_carrier_view *)device->priv;
    ssize_t received;
    if (!view || (!buffer && size) || view->position > view->length)
        return -1;
    if (size > (size_t)(view->length - view->position))
        size = (size_t)(view->length - view->position);
    if (size > 65536U) size = 65536U;
    if (!size) return 0;
    if (xx_io_seek64(view->source, view->begin + view->position, SEEK_SET) != 0)
        return -1;
    received = xx_io_read(view->source, buffer, size);
    if (received < 0 || (size_t)received > size) return -1;
    view->position += received;
    return received;
}

static int sfx_carrier_view_seek64(xx_io_device *device, int64_t offset, int whence) {
    sfx_carrier_view *view = (sfx_carrier_view *)device->priv;
    int64_t base;
    if (!view) return -1;
    if (whence == SEEK_SET) base = 0;
    else if (whence == SEEK_CUR) base = view->position;
    else if (whence == SEEK_END) base = view->length;
    else return -1;
    if (offset < -base || offset > view->length - base) return -1;
    view->position = base + offset;
    return 0;
}

static int sfx_carrier_view_seek(xx_io_device *device, long offset, int whence) {
    return sfx_carrier_view_seek64(device, (int64_t)offset, whence);
}

static int64_t sfx_carrier_view_tell(xx_io_device *device) {
    sfx_carrier_view *view = (sfx_carrier_view *)device->priv;
    return view ? view->position : -1;
}

static int64_t sfx_carrier_view_size(xx_io_device *device) {
    sfx_carrier_view *view = (sfx_carrier_view *)device->priv;
    return view ? view->length : -1;
}

static void sfx_carrier_view_init(sfx_carrier_view *view, xx_io_device *source,
                         int64_t begin, int64_t length) {
    xx_mem_zero(view, sizeof(*view));
    view->source = source;
    view->begin = begin;
    view->length = length;
    view->device.priv = view;
    view->device.read = sfx_carrier_view_read;
    view->device.seek = sfx_carrier_view_seek;
    view->device.seek64 = sfx_carrier_view_seek64;
    view->device.tell = sfx_carrier_view_tell;
    view->device.total_size = sfx_carrier_view_size;
}

/* Conventional InfoZIP SFX files keep ZIP offsets relative to the first local
 * header. The carrier parser has already found and validated that complete
 * ZIP tail; expose its members directly instead of a synthetic payload.zip. */
static bool sfx_carrier_relative_ensure(xx_sfx_zipcentral *r, xx_pd_struct *pd) {
    Abstractformat *f = &r->format;
    pm_stream *carrier;
    pm_member *payload;
    sfx_carrier_view *view = NULL;
    xx_archive_record_state *state = NULL;
    const xx_archive_record *record;
    int64_t end;
    uint64_t count = 0;
    bool valid = false;

    if (r->checked_relative) return r->inner_ready;
    r->checked_relative = true;
    carrier = pm_open(f, pd);
    if (!carrier) return false;
    if (carrier->count != 1 || carrier->size <= 0 ||
        carrier->size > pm_available(f)) goto done;
    payload = &carrier->items[0];
    if (payload->memory || payload->size <= 0 ||
        payload->offset < f->base_address ||
        payload->offset > INT64_MAX - payload->size ||
        payload->offset + payload->size != f->base_address + carrier->size)
        goto done;
    end = payload->size;
    view = (sfx_carrier_view *)xx_mem_alloc(sizeof(*view));
    if (!view) goto done;
    sfx_carrier_view_init(view, f->device, payload->offset, payload->size);
    xx_zip_init(&r->inner, &view->device, 0);
    if (!xx_zip_handle_base_info(&r->inner.format, pd) ||
        r->inner.format.format_size != end ||
        !r->inner.format.number_of_archive_records ||
        r->inner.format.number_of_archive_records > 4096U) goto discard;
    state = xx_zip_create_archive_records_reading(&r->inner.format, NULL, pd);
    if (!state) goto discard;
    valid = true;
    while ((record = xx_zip_get_current_archive_record(&r->inner.format,
                                                        state)) != NULL) {
        if (carrier_stop(pd) || ++count > 4096U || record->compressed_size < 0 ||
            record->data_offset < 0 ||
            record->data_offset > end ||
            record->compressed_size > end - record->data_offset) {
            valid = false;
            break;
        }
        if (!xx_zip_archive_record_move_to_next(&r->inner.format, state, pd))
            break;
    }
    if (count != r->inner.format.number_of_archive_records) valid = false;
    xx_zip_free_archive_records_reading(&r->inner.format, state);
    if (valid) {
        r->relative_view = view;
        view = NULL;
        r->inner_ready = true;
        goto done;
    }
discard:
    xx_zip_destroy(&r->inner);
done:
    xx_mem_free(view);
    pm_free_stream(carrier);
    return r->inner_ready;
}

static bool sfx_carrier_ensure(xx_sfx_zipcentral *r, xx_pd_struct *pd) {
    return r && (r->inner_ready || sfx_carrier_absolute_ensure(r, pd) ||
                 sfx_carrier_relative_ensure(r, pd));
}



static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) { return sfx_carrier_parse(f,s,pd) && carrier_members(s,pd); }
static int64_t sfx_carrier_size(Abstractformat *f, xx_pd_struct *pd) {
    return xx_sfx_zipcentral_handle_base_info(f, pd) ? f->format_size : -1;
}
static uint64_t sfx_carrier_count(Abstractformat *f, xx_pd_struct *pd) {
    return xx_sfx_zipcentral_handle_base_info(f, pd) ? f->number_of_archive_records : 0;
}
/* ZIP's historical default name encoding is DOS OEM 437. The generic ZIP
 * reader follows the host ANSI code page for unflagged names, so normalize
 * only validated SFX ZIP members here; flag 11 still denotes UTF-8. */
static const uint16_t sfx_carrier_cp437[128] = {
    0x00C7, 0x00FC, 0x00E9, 0x00E2, 0x00E4, 0x00E0, 0x00E5, 0x00E7,
    0x00EA, 0x00EB, 0x00E8, 0x00EF, 0x00EE, 0x00EC, 0x00C4, 0x00C5,
    0x00C9, 0x00E6, 0x00C6, 0x00F4, 0x00F6, 0x00F2, 0x00FB, 0x00F9,
    0x00FF, 0x00D6, 0x00DC, 0x00A2, 0x00A3, 0x00A5, 0x20A7, 0x0192,
    0x00E1, 0x00ED, 0x00F3, 0x00FA, 0x00F1, 0x00D1, 0x00AA, 0x00BA,
    0x00BF, 0x2310, 0x00AC, 0x00BD, 0x00BC, 0x00A1, 0x00AB, 0x00BB,
    0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556,
    0x2555, 0x2563, 0x2551, 0x2557, 0x255D, 0x255C, 0x255B, 0x2510,
    0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x255E, 0x255F,
    0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C, 0x2567,
    0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256B,
    0x256A, 0x2518, 0x250C, 0x2588, 0x2584, 0x258C, 0x2590, 0x2580,
    0x03B1, 0x00DF, 0x0393, 0x03C0, 0x03A3, 0x03C3, 0x00B5, 0x03C4,
    0x03A6, 0x0398, 0x03A9, 0x03B4, 0x221E, 0x03C6, 0x03B5, 0x2229,
    0x2261, 0x00B1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00F7, 0x2248,
    0x00B0, 0x2219, 0x00B7, 0x221A, 0x207F, 0x00B2, 0x25A0, 0x00A0
};

static bool sfx_carrier_read_device(xx_io_device *device, int64_t at,
                           void *data, size_t size) {
    size_t done = 0U;
    int64_t limit = xx_io_size(device);
    if (at < 0 || limit < at || size > (uint64_t)(limit - at) ||
        xx_io_seek64(device, at, SEEK_SET) != 0) return false;
    while (done < size) {
        ssize_t got = xx_io_read(device, (uint8_t *)data + done, size - done);
        if (got <= 0 || (size_t)got > size - done) return false;
        done += (size_t)got;
    }
    return true;
}

static bool sfx_carrier_normalize_record(xx_sfx_zipcentral *r,
                                xx_archive_record_state *state,
                                xx_pd_struct *pd) {
    xx_archive_record *record;
    xx_io_device *device;
    uint8_t header[30], name[4096];
    wchar_t wide[4097];
    int64_t local;
    uint16_t length;
    size_t i;
    bool high = false;
    if (!state || !state->has_record) return true;
    record = &state->current_record;
    if (r->legacy_name && record->header_offset == r->legacy_local_offset)
        return xx_archive_record_set_original_name_w(record, r->legacy_name);
    device = r->inner.format.device;
    local = record->header_offset;
    if (carrier_stop(pd) || !sfx_carrier_read_device(device, local, header, sizeof(header)) ||
        xx_rt_memcmp(header, "PK\3\4", 4)) return false;
    length = xx_data_get_u16(header + 26, 2, 0, false);
    if (!length || length > 4096U || (xx_data_get_u16(header + 6, 2, 0, false) & 0x0800U))
        return true;
    if (record->data_offset != local + 30 + (int64_t)length +
                               xx_data_get_u16(header + 28, 2, 0, false) ||
        !sfx_carrier_read_device(device, local + 30, name, length)) return false;
    for (i = 0U; i < length; ++i) {
        if (carrier_stop(pd) || name[i] == 0U) return false;
        high |= name[i] >= 0x80U;
        wide[i] = (wchar_t)(name[i] < 0x80U ? name[i] :
                            sfx_carrier_cp437[name[i] - 0x80U]);
    }
    if (!high) return true;
    wide[length] = L'\0';
    return xx_archive_record_set_original_name_w(record, wide);
}
static xx_archive_record_state *sfx_carrier_records(Abstractformat *f,
                                            const xx_list_s *opts,
                                            xx_pd_struct *pd) {
    xx_sfx_zipcentral *r = (xx_sfx_zipcentral *)f;
    xx_archive_record_state *state;
    demolition_state *demo;
    if (!sfx_carrier_ensure(r, pd)) return pm_create_records(f, opts, pd);
    state = xx_zip_create_archive_records_reading(&r->inner.format, opts, pd);
    if (!state) return NULL;
    /* NULL-format API calls must keep using the wrapper callbacks, including
     * the private ZIP-state adapter and current outer parameter precedence. */
    state->format = f;
    demo = (demolition_state *)xx_mem_alloc(sizeof(*demo));
    if (!demo) {
        xx_zip_free_archive_records_reading(&r->inner.format, state);
        return NULL;
    }
    xx_mem_zero(demo, sizeof(*demo));
    demo->preferred = -1;
    demo->remaining_work = UINT64_C(1073741824);
    demolition_locate(f, demo, pd);
    if (!sfx_carrier_normalize_record(r, state, pd) ||
        !demolition_record(r, state, demo, pd)) {
        demolition_free(demo);
        xx_zip_free_archive_records_reading(&r->inner.format, state);
        return NULL;
    }
    if (demo->count) {
        demo->zip_state = state->internal_state;
        demo->zip_free = state->free_internal;
        demolition_attach(state, demo);
    } else demolition_free(demo);
    return state;
}
static const xx_archive_record *sfx_carrier_current(Abstractformat *f,
                                           xx_archive_record_state *state) {
    xx_sfx_zipcentral *r = (xx_sfx_zipcentral *)f;
    return r->inner_ready
               ? xx_zip_get_current_archive_record(&r->inner.format, state)
               : pm_current(f, state);
}
static bool sfx_carrier_next(Abstractformat *f, xx_archive_record_state *state,
                    xx_pd_struct *pd) {
    xx_sfx_zipcentral *r = (xx_sfx_zipcentral *)f;
    demolition_state *demo;
    bool result;
    if (!r->inner_ready) return pm_next(f, state, pd);
    demo = demolition_detach(state);
    result = xx_zip_archive_record_move_to_next(&r->inner.format, state, pd) &&
             sfx_carrier_normalize_record(r, state, pd) &&
             demolition_record(r, state, demo, pd);
    if (demo) demolition_attach(state, demo);
    return result;
}
static bool sfx_carrier_unpack(Abstractformat *f, xx_archive_record_state *state,
                      xx_pd_struct *pd) {
    xx_sfx_zipcentral *r = (xx_sfx_zipcentral *)f;
    demolition_state *demo;
    xx_list_s saved_options, saved_parameters, options;
    const char *recovered;
    bool result;
    size_t i;
    if (!r->inner_ready) return pm_unpack(f, state, pd);
    if (!state || !state->has_record || carrier_stop(pd)) return false;
    demo = demolition_detach(state);
    saved_options = state->options;
    saved_parameters = r->inner.format.list_extra_parameters;
    /* The outer parameters are borrowed only for this synchronous operation.
     * Clearing/replacing an outer password never leaves a stale child value. */
    r->inner.format.list_extra_parameters = f->list_extra_parameters;
    recovered = xx_archive_record_get_meta_str(&state->current_record,
                                                XX_META_ID_PASSWORD);
    xx_list_init(&options, sizeof(xx_meta), NULL);
    if (recovered && !xx_format_resolve_extra_parameter(
            f, &saved_options, XX_META_ID_OPT_PASSWORD)) {
        xx_meta password;
        for (i = 0; i < saved_options.count; ++i) {
            const xx_meta *item = (const xx_meta *)xx_list_at(&saved_options, i);
            if (!item || !xx_list_append(&options, item)) goto failed;
        }
        xx_meta_init(&password, XX_META_ID_OPT_PASSWORD);
        xx_var_set_str_view(&password.var, recovered, xx_rt_strlen(recovered));
        if (!xx_list_append(&options, &password)) goto failed;
        state->options = options;
    }
    result = xx_zip_unpack_current_archive_record(&r->inner.format, state, pd);
    goto done;
failed:
    result = false;
done:
    state->options = saved_options;
    r->inner.format.list_extra_parameters = saved_parameters;
    xx_list_cleanup(&options);
    if (demo) demolition_attach(state, demo);
    return result;
}
static void sfx_carrier_free_records(Abstractformat *f, xx_archive_record_state *state) {
    xx_sfx_zipcentral *r = (xx_sfx_zipcentral *)f;
    if (r->inner_ready)
        xx_zip_free_archive_records_reading(&r->inner.format, state);
    else pm_free_records(f, state);
}
void xx_sfx_zipcentral_init(xx_sfx_zipcentral *r,xx_io_device *d,int64_t b) {
    Abstractformat *f;
    if (!r) return;
    xx_mem_zero(r, sizeof(*r));
    r->legacy_local_offset = -1;
    f = &r->format;
    pm_init(f, d, b, XX_FILE_TYPE_SFX_ZIPCENTRAL, "exe");
    f->check_is_valid = xx_sfx_zipcentral_check_is_valid;
    f->handle_base_info = xx_sfx_zipcentral_handle_base_info;
    f->get_format_size = sfx_carrier_size;
    f->get_number_of_archive_records = sfx_carrier_count;
    f->create_archive_records_reading = sfx_carrier_records;
    f->get_current_archive_record = sfx_carrier_current;
    f->archive_record_move_to_next = sfx_carrier_next;
    f->unpack_current_archive_record = sfx_carrier_unpack;
    f->free_archive_records_reading = sfx_carrier_free_records;
}
xx_sfx_zipcentral *xx_sfx_zipcentral_create(xx_io_device *d,int64_t b) { xx_sfx_zipcentral *r=(xx_sfx_zipcentral *)xx_mem_alloc(sizeof(*r)); if(r) xx_sfx_zipcentral_init(r,d,b); return r; }
void xx_sfx_zipcentral_destroy(xx_sfx_zipcentral *r) { if(r) { if(r->inner_ready) xx_zip_destroy(&r->inner); xx_mem_free(r->relative_view); xx_str_wfree(r->legacy_name); xx_format_cleanup_extra_parameters(&r->format); } }
void xx_sfx_zipcentral_free(xx_sfx_zipcentral *r) { if(r) { xx_sfx_zipcentral_destroy(r); xx_mem_free(r); } }
bool xx_sfx_zipcentral_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return f && (sfx_carrier_ensure((xx_sfx_zipcentral *)f,pd) || pm_valid(f,pd)); }
bool xx_sfx_zipcentral_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {
    xx_sfx_zipcentral *r = (xx_sfx_zipcentral *)f;
    if (!f) return false;
    if (!sfx_carrier_ensure(r,pd)) return pm_handle(f,pd);
    f->format_size = pm_available(f);
    f->number_of_archive_records = r->inner.format.number_of_archive_records;
    f->overlay_size = 0;
    f->overlay_offset = -1;
    f->is_valid = true;
    f->base_info_handled = true;
    return true;
}
