/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: ../XArchive/sfx/xspissfx.cpp
 * Independent bounded parser; borrowed source device; safe numbered outputs.
 */
#include "xxfclib/formats/sfx_spis/xx_sfx_spis.h"
#include "xxfclib/formats/spis/xx_spis.h"
#include "../makeself/xx_fourth_wrapper_table.h"
#include "xxfclib/algo/lzh/xx_lzh.h"

/* GP-Install also puts a type-0 SPIS stream in a PE resource named
 * TCOMPRESS/SIDE.  The resource directory supplies the exact compressed
 * extent; scanning for SPIS inside .rsrc would include unrelated resources. */
#define SP_RESOURCE_MAX_SIZE (16U * 1024U * 1024U)
#define SP_RESOURCE_MAX_PLAIN (64U * 1024U * 1024U)

typedef struct sp_resource_root {
    int64_t file_offset;
    uint32_t byte_count;
} sp_resource_root;

static bool sp_resource_span(const sp_resource_root *root, uint32_t offset,
                             uint32_t size) {
    return offset <= root->byte_count && size <= root->byte_count - offset;
}

static bool sp_resource_name(Abstractformat *f, const sp_resource_root *root,
                             uint32_t offset, const char *wanted) {
    uint8_t header[2], data[64];
    size_t i, length = xx_rt_strlen(wanted);
    if (length > sizeof(data) / 2U || !sp_resource_span(root, offset, 2U) ||
        !pm_read(f, root->file_offset + offset, header, sizeof(header)) ||
        pm_le16(header) != length ||
        !sp_resource_span(root, offset + 2U, (uint32_t)(length * 2U)) ||
        !pm_read(f, root->file_offset + offset + 2U, data, length * 2U))
        return false;
    for (i = 0U; i < length; ++i)
        if (data[i * 2U] != (uint8_t)wanted[i] || data[i * 2U + 1U] != 0U)
            return false;
    return true;
}

static bool sp_resource_child(Abstractformat *f, const sp_resource_root *root,
                              uint32_t directory, const char *wanted,
                              bool require_directory, uint32_t *child) {
    uint8_t header[16], entry[8];
    uint32_t count, i;
    bool found = false;
    if (!sp_resource_span(root, directory, sizeof(header)) ||
        !pm_read(f, root->file_offset + directory, header, sizeof(header)))
        return false;
    count = (uint32_t)pm_le16(header + 12) + pm_le16(header + 14);
    if (count == 0U || count > 256U ||
        !sp_resource_span(root, directory + 16U, count * 8U)) return false;
    for (i = 0U; i < count; ++i) {
        uint32_t name, value;
        if (!pm_read(f, root->file_offset + directory + 16U + i * 8U,
                     entry, sizeof(entry))) return false;
        name = pm_le32(entry); value = pm_le32(entry + 4);
        if (!(name & UINT32_C(0x80000000)) ||
            !sp_resource_name(f, root, name & UINT32_C(0x7fffffff), wanted))
            continue;
        if (found || !!(value & UINT32_C(0x80000000)) != require_directory)
            return false;
        *child = value & UINT32_C(0x7fffffff);
        found = true;
    }
    return found;
}

static bool sp_resource_leaf(Abstractformat *f, const sp_resource_root *root,
                             uint32_t directory, uint32_t section_rva,
                             int64_t section_file, uint32_t section_size,
                             int64_t *at, uint32_t *size) {
    uint8_t header[16], entry[8], leaf[16];
    uint32_t count, i;
    bool found = false;
    if (!sp_resource_span(root, directory, sizeof(header)) ||
        !pm_read(f, root->file_offset + directory, header, sizeof(header)))
        return false;
    count = (uint32_t)pm_le16(header + 12) + pm_le16(header + 14);
    if (count == 0U || count > 64U ||
        !sp_resource_span(root, directory + 16U, count * 8U)) return false;
    for (i = 0U; i < count; ++i) {
        uint32_t value, rva, bytes;
        if (!pm_read(f, root->file_offset + directory + 16U + i * 8U,
                     entry, sizeof(entry))) return false;
        value = pm_le32(entry + 4);
        if (value & UINT32_C(0x80000000) ||
            !sp_resource_span(root, value, sizeof(leaf)) ||
            !pm_read(f, root->file_offset + value, leaf, sizeof(leaf)))
            return false;
        rva = pm_le32(leaf); bytes = pm_le32(leaf + 4);
        if (found || rva < section_rva || bytes < 21U ||
            bytes > SP_RESOURCE_MAX_SIZE ||
            rva - section_rva > section_size ||
            bytes > section_size - (rva - section_rva)) return false;
        *at = section_file + (rva - section_rva);
        *size = bytes;
        found = true;
    }
    return found;
}

static bool sp_pe_resource(Abstractformat *f, pm_stream *s,
                           xx_pd_struct *pd) {
    uint8_t header[64], entry[40], blob[21];
    uint32_t pe, resource_rva, resource_size, section_rva = 0U;
    uint32_t section_size = 0U, type, name, leaf_size;
    uint16_t sections, optional_size, optional_magic;
    int64_t section_file = -1, leaf_at, limit = pm_available(f);
    sp_resource_root root;
    uint8_t *packed = NULL, *plain = NULL;
    uint32_t raw_size, sum = 0U;
    size_t in = 0U, out = 0U, i, packed_size;
    uint8_t last = 0U;
    bool ok = false;

    if (wg_stop(pd) || limit < 512 || !pm_read(f, 0, header, 64) ||
        xx_rt_memcmp(header, "MZ", 2)) return false;
    pe = pm_le32(header + 60);
    if (pe < 64U || pe > 1048576U || !wg_range(limit, pe, 24U) ||
        !pm_read(f, pe, header, 24) || xx_rt_memcmp(header, "PE\0\0", 4))
        return false;
    sections = pm_le16(header + 6); optional_size = pm_le16(header + 20);
    if (!sections || sections > 96U || optional_size < 128U ||
        optional_size > 4096U ||
        !wg_range(limit, (uint64_t)pe + 24U, optional_size) ||
        !pm_read(f, (int64_t)pe + 24, header, 64)) return false;
    optional_magic = pm_le16(header);
    if (optional_magic != 0x10bU && optional_magic != 0x20bU) return false;
    if (optional_size < (optional_magic == 0x10bU ? 120U : 136U))
        return false;
    if (!pm_read(f, (int64_t)pe + 24 +
                    (optional_magic == 0x10bU ? 96 : 112) + 16,
                 header, 8)) return false;
    resource_rva = pm_le32(header); resource_size = pm_le32(header + 4);
    if (resource_size < 16U || resource_size > SP_RESOURCE_MAX_SIZE ||
        !wg_range(limit, (uint64_t)pe + 24U + optional_size,
                  (uint64_t)sections * 40U)) return false;
    for (i = 0U; i < sections; ++i) {
        uint32_t rva, raw, bytes;
        if (wg_stop(pd) || !pm_read(f, (int64_t)pe + 24 + optional_size +
                                    (int64_t)i * 40, entry, 40)) return false;
        rva = pm_le32(entry + 12); bytes = pm_le32(entry + 16);
        raw = pm_le32(entry + 20);
        if (resource_rva < rva || resource_rva - rva > bytes ||
            resource_size > bytes - (resource_rva - rva)) continue;
        if (section_file >= 0 || !wg_range(limit, raw, bytes)) return false;
        section_file = raw; section_rva = rva; section_size = bytes;
    }
    if (section_file < 0) return false;
    root.file_offset = section_file + (resource_rva - section_rva);
    root.byte_count = resource_size;
    if (!sp_resource_child(f, &root, 0U, "TCOMPRESS", true, &type) ||
        !sp_resource_child(f, &root, type, "SIDE", true, &name) ||
        !sp_resource_leaf(f, &root, name, section_rva, section_file,
                          section_size, &leaf_at, &leaf_size)) return false;
    /* The first resource level is type, the second is name and the third
     * contains language leaves.  The entire selected leaf is the SPIS span. */
    if (!pm_read(f, leaf_at, blob, sizeof(blob)) ||
        xx_rt_memcmp(blob, "SPIS\x1aRLE", 8) || blob[12] != 0U ||
        pm_le32(blob + 17) > 2U) return false;
    raw_size = pm_le32(blob + 8);
    if (raw_size < 2U || raw_size > SP_RESOURCE_MAX_PLAIN) return false;
    packed_size = (size_t)leaf_size - sizeof(blob);
    packed = (uint8_t *)xx_mem_alloc(packed_size);
    plain = (uint8_t *)xx_mem_alloc(raw_size);
    if (!packed || !plain || !pm_read(f, leaf_at + (int64_t)sizeof(blob),
                                      packed, packed_size)) goto done;
    while (in < packed_size) {
        uint8_t value = packed[in++];
        if (value == 0x94U) {
            if (in >= packed_size) goto done;
            value = packed[in++];
            if (value >= 2U) {
                size_t repeat = (size_t)value - 1U;
                if (repeat > (size_t)raw_size - out) goto done;
                while (repeat--) plain[out++] = last;
            } else if (value == 0U) {
                if (out == raw_size) goto done;
                plain[out++] = 0x94U;
            }
            last = 0x94U;
        } else {
            if (out == raw_size) goto done;
            plain[out++] = last = value;
        }
    }
    if (out != raw_size || plain[0] != 'B' || plain[1] != 'M' ||
        pm_le32(plain + 2) != raw_size) goto done;
    for (i = 0U; i < out; ++i) sum += plain[i];
    if (sum != pm_le32(blob + 13) || wg_stop(pd) ||
        !pm_add(f, s, "MAINICON.bmp", leaf_at + 21, packed_size)) goto done;
    s->items[s->count - 1U].memory = plain;
    s->items[s->count - 1U].size = raw_size;
    s->size = limit;
    plain = NULL;
    ok = true;
done:
    xx_mem_free(packed);
    xx_mem_free(plain);
    return ok;
}

/* The Win16 InstallUs bootstrap stores two precisely sized SPIS blobs.
 * Each stored or LZH record is self-contained; later SPIS headers start new
 * segments and close the previous segment's declared decoded-size sum. */
static bool sp_installus_members(Abstractformat *f, pm_stream *s,
                                  int64_t begin, int64_t end,
                                  xx_pd_struct *pd) {
    uint8_t header[25], name_bytes[96];
    int64_t at = begin;
    uint64_t declared = 0U, summed = 0U;
    unsigned segment_count = 0U;
    while (at < end) {
        uint16_t name_size;
        uint32_t raw_size, packed_size, checksum, flags, sum = 0U;
        uint8_t method;
        uint8_t *packed = NULL, *plain = NULL;
        size_t written = 0U, i;
        bool okay;
        if (wg_stop(pd)) return false;
        if (end - at >= 21 && pm_read(f, at, header, 21) &&
            !xx_rt_memcmp(header, "SPIS\x1a", 5)) {
            if (segment_count && summed != declared) return false;
            if (xx_rt_memcmp(header + 5, "LZH", 3) || header[12] != 1U ||
                pm_le32(header + 17) != 0U) return false;
            declared = pm_le32(header + 8);
            summed = 0U;
            ++segment_count;
            at += 21;
            continue;
        }
        if (!segment_count || s->count >= 128U || end - at < 25 ||
            !pm_read(f, at, header, sizeof(header))) return false;
        name_size = pm_le16(header);
        raw_size = pm_le32(header + 8);
        packed_size = pm_le32(header + 12);
        method = header[16];
        checksum = pm_le32(header + 17);
        flags = pm_le32(header + 21);
        if (name_size == 0U || name_size >= sizeof(name_bytes) ||
            (method != 0U && method != 2U) || flags != 0U ||
            raw_size > SP_RESOURCE_MAX_PLAIN ||
            packed_size > SP_RESOURCE_MAX_SIZE ||
            (method == 0U && raw_size != packed_size) ||
            (!!raw_size != !!packed_size) ||
            !wg_range(end, (uint64_t)at + 25U,
                      (uint64_t)name_size + packed_size) ||
            raw_size > declared - summed) return false;
        if (!pm_read(f, at + 25, name_bytes, name_size)) return false;
        for (i = 0U; i < name_size; ++i) {
            uint8_t c = name_bytes[i];
            if (c < 0x20U || c > 0x7eU || c == '/' || c == '\\' ||
                c == ':' || c == '<' || c == '>' || c == '"' ||
                c == '|' || c == '?' || c == '*') return false;
        }
        if (name_bytes[0] == '.' || name_bytes[0] == ' ' ||
            name_bytes[name_size - 1U] == '.' ||
            name_bytes[name_size - 1U] == ' ')
            return false;
        name_bytes[name_size] = 0U;
        plain = (uint8_t *)xx_mem_alloc(raw_size ? raw_size : 1U);
        if (!plain) return false;
        if (raw_size) {
            if (method == 0U) {
                okay = pm_read(f, at + 25 + name_size, plain, raw_size);
            } else {
                packed = (uint8_t *)xx_mem_alloc(packed_size);
                okay = packed && pm_read(f, at + 25 + name_size, packed,
                                           packed_size) &&
                       xx_lzh1_decode_memory(packed, packed_size, plain,
                                             raw_size, &written) &&
                       written == raw_size;
            }
            if (okay) for (i = 0U; i < raw_size; ++i) sum += plain[i];
            xx_mem_free(packed);
            /* Stored members may omit the additive checksum (zero field).
             * Compressed members carry it, as do stored members that set it. */
            if (!okay || ((method != 0U || checksum != 0U) && sum != checksum)) {
                xx_mem_free(plain); return false;
            }
        } else if (checksum != 0U) {
            xx_mem_free(plain); return false;
        }
        if (wg_stop(pd) || !pm_add(f, s, (const char *)name_bytes,
                                  at + 25 + name_size, packed_size)) {
            xx_mem_free(plain); return false;
        }
        xx_rt_snprintf(s->items[s->count - 1U].name,
                       sizeof(s->items[s->count - 1U].name), "%s",
                       (const char *)name_bytes);
        s->items[s->count - 1U].memory = plain;
        s->items[s->count - 1U].size = raw_size;
        summed += raw_size;
        at += 25 + name_size + packed_size;
    }
    return segment_count != 0U && summed == declared && at == end;
}

static bool sp_blob(Abstractformat *f,int64_t at,int64_t end,xx_pd_struct *pd) {
    uint8_t h[25]; uint64_t total=0,declared; bool single; int64_t p=at+21;
    if(end-at<21 || !pm_read(f,at,h,21) || xx_rt_memcmp(h,"SPIS\x1a",5) || (xx_rt_memcmp(h+5,"NON",3) && xx_rt_memcmp(h+5,"RLE",3) && xx_rt_memcmp(h+5,"LZH",3) && xx_rt_memcmp(h+5,"CUS",3) && xx_rt_memcmp(h+5,"LH5",3)) || h[12]>1 || pm_le32(h+17)>2) return false;
    declared=pm_le32(h+8); single=h[12]==0;
    if(single) { if(!xx_rt_memcmp(h+5,"NON",3) && !pm_le32(h+17) && (declared!=(uint64_t)(end-p) || (pm_le32(h+13) && !wg_sum(f,p,end-p,pm_le32(h+13),pd)))) return false; return p<end || !declared; }
    while(p<end) { uint16_t name; uint32_t packed,raw; if(wg_stop(pd) || end-p<25 || !pm_read(f,p,h,25)) return false;
        name=pm_le16(h); raw=pm_le32(h+8); packed=pm_le32(h+12); if(!name || name>4096 || h[16]>4 || pm_le32(h+21)>2 || (uint64_t)25+name+packed>(uint64_t)(end-p) || (!h[16] && raw!=packed)) return false;
        if(!h[16] && !pm_le32(h+21) && pm_le32(h+17) && !wg_sum(f,p+25+name,packed,pm_le32(h+17),pd)) { return false; } total+=raw; if(total>declared) return false; p+=25+name+packed;
    } return p==end && total==declared;
}

/* The PE security directory uses a file offset, not an RVA.  A signed
 * GP-Install ends its SPIS chain before the aligned WIN_CERTIFICATE table.
 * Only a complete, precisely framed table at EOF may bound that chain. */
static bool sp_pe_payload_end(Abstractformat *f, int64_t overlay,
                              int64_t *end, bool *certificate,
                              xx_pd_struct *pd) {
    uint8_t h[24], padding[7];
    uint32_t pe, directory_count, offset, bytes;
    uint16_t optional_size, magic;
    unsigned directory_base, count_offset;
    int64_t at, limit = pm_available(f);
    *end = limit;
    *certificate = false;
    if (!pm_read(f, 60, h, 4)) return false;
    pe = pm_le32(h);
    if (!pm_read(f, pe, h, 24)) return false;
    optional_size = pm_le16(h + 20);
    if (!pm_read(f, (int64_t)pe + 24, h, 2)) return false;
    magic = pm_le16(h);
    directory_base = magic == 0x10bU ? 96U : 112U;
    count_offset = directory_base - 4U;
    if (optional_size < directory_base) return true;
    if (!pm_read(f, (int64_t)pe + 24 + count_offset, h, 4)) return false;
    directory_count = pm_le32(h);
    if (directory_count < 5U) return true;
    if (optional_size < directory_base + 40U ||
        !pm_read(f, (int64_t)pe + 24 + directory_base + 32U, h, 8))
        return false;
    offset = pm_le32(h); bytes = pm_le32(h + 4);
    if (!offset && !bytes) return true;
    if (!offset || bytes < 8U || (offset & 7U) || offset < overlay ||
        !wg_range(limit, offset, bytes) || (uint64_t)offset + bytes != (uint64_t)limit)
        return false;
    at = offset;
    while (at < limit) {
        uint32_t length;
        uint64_t aligned;
        size_t pad;
        if (wg_stop(pd) || limit - at < 8 || !pm_read(f, at, h, 8)) return false;
        length = pm_le32(h);
        /* The supported signed layout carries PKCS#7, revision 2.0. */
        if (length < 8U || pm_le16(h + 4) != 0x200U ||
            pm_le16(h + 6) != 2U) return false;
        aligned = ((uint64_t)length + 7U) & ~UINT64_C(7);
        if (!wg_range(limit, (uint64_t)at, aligned)) return false;
        pad = (size_t)(aligned - length);
        if (pad && (!pm_read(f, at + length, padding, pad) ||
                    !wg_zero(padding, pad))) return false;
        at += (int64_t)aligned;
    }
    *end = offset;
    *certificate = true;
    return at == limit;
}
static bool wg_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    static const char prologue[]="\x00\x07""Can&cel\x42""PreSetup will prepare the temporary files needed for installation.\x34""Setup needs to run on Win-32. Installation may fail";
    int64_t overlay,cab,cabend,physical=pm_available(f),limit=physical,at=-1; uint8_t h[128]; unsigned count=0;
    bool certificate = false;
    if (sp_pe_resource(f, s, pd)) return true;
    if(wg_pe(f,&overlay,&cab,&cabend,pd)) {
        if (!sp_pe_payload_end(f, overlay, &limit, &certificate, pd)) return false;
        at=overlay;
        if (wg_range(limit, (uint64_t)at, sizeof(h)) &&
            pm_read(f, at, h, sizeof(h)) &&
            !xx_rt_memcmp(h, prologue, sizeof(h))) goto installus;
        while(at<limit) { uint32_t bytes; char name[48];
            if (certificate && limit - at <= 7 && pm_read(f, at, h, (size_t)(limit - at)) &&
                wg_zero(h, (size_t)(limit - at))) { at = limit; break; }
            if(wg_stop(pd) || !wg_range(limit, (uint64_t)at, 4U) || !pm_read(f,at,h,4) || (bytes=pm_le32(h))<21 || !wg_range(limit,at+4,bytes) || !sp_blob(f,at+4,at+4+bytes,pd)) return false;
            xx_rt_snprintf(name,sizeof(name),"payload-%u.spis",count++); if(!pm_add(f,s,name,at+4,bytes)) return false; at+=4+bytes; if(count>4096) return false;
        } if(!count) return false; s->size=physical; return true;
    }
    if(!pm_read(f,0,h,64) || xx_rt_memcmp(h,"MZ",2) || pm_le32(h+60)<64 || !pm_read(f,pm_le32(h+60),h,2) || xx_rt_memcmp(h,"NE",2)) return false;
    {
        const size_t capacity = xx_get_file_buffer_size();
        const size_t n = limit > 1048576 ? 1048576U : (size_t)limit;
        uint8_t prefix[128]; /* KMP state for the format's complete prologue. */
        uint8_t *buffer = (uint8_t *)xx_mem_alloc(capacity);
        size_t position = 0U, matched = 0U, j, k = 0U;
        bool found = false, ok = buffer != NULL;
        prefix[0] = 0U;
        for (j = 1U; j < sizeof(prefix); ++j) {
            while (k && prologue[j] != prologue[k]) k = prefix[k - 1U];
            if (prologue[j] == prologue[k]) ++k;
            prefix[j] = (uint8_t)k;
        }
        /* Read the full former prefix even after a hit: read errors keep their
         * original meaning, while only actual staging follows capacity. */
        while (ok && position < n) {
            size_t amount = n - position, index;
            if (amount > capacity) amount = capacity;
            if (!pm_read(f, (int64_t)position, buffer, amount)) { ok = false; break; }
            for (index = 0U; index < amount && !found; ++index) {
                size_t absolute = position + index;
                char byte = (char)buffer[index];
                if (absolute < 64U) continue;
                if (wg_stop(pd)) { ok = false; break; }
                while (matched && byte != prologue[matched]) matched = prefix[matched - 1U];
                if (byte == prologue[matched]) ++matched;
                if (matched == sizeof(prefix)) {
                    at = (int64_t)(absolute + 1U - sizeof(prefix));
                    found = true;
                }
            }
            position += amount;
        }
        if (buffer) xx_mem_free(buffer);
        if (!ok || !found) return false;
    }
installus:
    { unsigned i; uint64_t sizes[2]={0,0}; unsigned blobs=1; int64_t first; for(i=0;i<9;++i) { if(!pm_read(f,at,h,1) || !wg_range(limit,at+1,h[0])) return false; at+=1+h[0]; }
        if(!pm_read(f,at,h,5)) return false;
        if(xx_rt_memcmp(h,"SPIS\x1a",5)) { blobs=2; for(i=0;i<2;++i) { uint8_t digits[10]; if(!pm_read(f,at,h,1) || !h[0] || h[0]>10 || !pm_read(f,at+1,digits,h[0]) || !wg_decimal((const char *)digits,h[0],&sizes[i])) return false; at+=1+h[0]; } }
        else { if(limit-at<25) return false; sizes[0]=(uint64_t)(limit-at-4); }
        first=at; if(sizes[0]>INT64_MAX-sizes[1] || !wg_range(limit,first,sizes[0]+sizes[1]+4) || (uint64_t)(limit-first)!=sizes[0]+sizes[1]+4) return false;
        for(i=0;i<blobs;++i) {
            char name[48];
            if (sizes[i] < 21) return false;
            if (blobs == 2) {
                if (!sp_installus_members(f, s, at,
                                          at + (int64_t)sizes[i], pd))
                    return false;
            } else {
                if (!sp_blob(f, at, at + (int64_t)sizes[i], pd)) return false;
                xx_rt_snprintf(name, sizeof(name), "payload-%u.spis", i);
                if (!pm_add(f, s, name, at, (int64_t)sizes[i])) return false;
            }
            at += (int64_t)sizes[i];
        }
    } s->size=physical; return true;
}
static bool sp_member_extents(pm_stream *s, xx_pd_struct *pd) {
    wg_extent *ranges;
    size_t i, count = 0U;
    bool valid;
    if (s->count < 2U) return true;
    ranges = (wg_extent *)xx_mem_alloc(s->count * sizeof(*ranges));
    if (!ranges) return false;
    for (i = 0U; i < s->count; ++i) {
        const pm_member *item = &s->items[i];
        if (item->packed_size < 0 || item->offset < 0 ||
            item->offset > INT64_MAX - item->packed_size) {
            xx_mem_free(ranges);
            return false;
        }
        if (item->packed_size) {
            ranges[count].lo = item->offset;
            ranges[count++].hi = item->offset + item->packed_size;
        }
    }
    valid = wg_extents(ranges, count, pd);
    xx_mem_free(ranges);
    return valid;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) { return wg_parse(f,s,pd) && sp_member_extents(s,pd); }

/* PE GP-Install overlays contain several independently framed SPIS volumes.
 * Each view has its own EOF; the native reader must never see the following
 * volume or the four-byte length prefix between volumes. Views borrow the
 * original device and seek before every read, so interleaved readers cannot
 * accidentally consume one another's cursor. */
#define SP_DIRECT_MAX_VOLUMES 128U
#define SP_DIRECT_MAX_BYTES (256U * 1024U * 1024U)
#define SP_DIRECT_MAX_RECORDS 100000U

typedef struct sp_view {
    xx_io_device device;
    xx_io_device *source;
    int64_t begin;
    int64_t length;
    int64_t position;
} sp_view;

typedef struct sp_inner {
    sp_view view;
    xx_spis archive;
    uint64_t records;
    bool initialized;
} sp_inner;

typedef struct sp_direct {
    sp_inner *volumes;
    size_t count;
    uint64_t records;
    int64_t format_size;
} sp_direct;

typedef struct sp_direct_records {
    xx_archive_record_state base;
    xx_archive_record_state *active;
    size_t volume;
} sp_direct_records;

static ssize_t sp_view_read(xx_io_device *device, void *buffer, size_t size) {
    sp_view *view = (sp_view *)device->priv;
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

static int sp_view_seek64(xx_io_device *device, int64_t offset, int whence) {
    sp_view *view = (sp_view *)device->priv;
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

static int sp_view_seek(xx_io_device *device, long offset, int whence) {
    return sp_view_seek64(device, (int64_t)offset, whence);
}

static int64_t sp_view_tell(xx_io_device *device) {
    sp_view *view = (sp_view *)device->priv;
    return view ? view->position : -1;
}

static int64_t sp_view_size(xx_io_device *device) {
    sp_view *view = (sp_view *)device->priv;
    return view ? view->length : -1;
}

static void sp_view_init(sp_view *view, xx_io_device *source,
                         int64_t begin, int64_t length) {
    xx_mem_zero(view, sizeof(*view));
    view->source = source;
    view->begin = begin;
    view->length = length;
    view->device.priv = view;
    view->device.read = sp_view_read;
    view->device.seek = sp_view_seek;
    view->device.seek64 = sp_view_seek64;
    view->device.tell = sp_view_tell;
    view->device.total_size = sp_view_size;
}

static void sp_direct_free(sp_direct *direct) {
    size_t i;
    if (!direct) return;
    for (i = 0; i < direct->count; ++i)
        if (direct->volumes[i].initialized)
            xx_spis_destroy(&direct->volumes[i].archive);
    xx_mem_free(direct->volumes);
    xx_mem_free(direct);
}

static bool spis_ensure_inner(xx_sfx_spis *reader, xx_pd_struct *pd) {
    Abstractformat *outer = &reader->format;
    pm_stream *carrier;
    sp_direct *direct = NULL;
    size_t i;
    uint64_t bytes = 0U;
    bool valid = false;

    if (reader->checked_inner) return reader->inner_state != NULL;
    reader->checked_inner = true;
    carrier = pm_open(outer, pd);
    if (!carrier) return false;
    if (!carrier->count || carrier->count > SP_DIRECT_MAX_VOLUMES ||
        carrier->size <= 0 || carrier->size > pm_available(outer)) goto done;
    direct = (sp_direct *)xx_mem_calloc(1, sizeof(*direct));
    if (!direct) goto done;
    direct->volumes = (sp_inner *)xx_mem_calloc(carrier->count,
                                               sizeof(*direct->volumes));
    if (!direct->volumes) goto done;
    direct->count = carrier->count;
    direct->format_size = carrier->size;
    for (i = 0; i < carrier->count; ++i) {
        const pm_member *payload = &carrier->items[i];
        sp_inner *inner = &direct->volumes[i];
        xx_archive_record_state *state;
        const xx_archive_record *record;
        uint64_t count = 0U;
        size_t name_length = xx_rt_strlen(payload->name);

        /* Resource BMPs and already-decoded InstallUs members stay in the
         * existing carrier view. Only actual numbered SPIS blobs delegate. */
        if (payload->memory || name_length < 5U ||
            xx_rt_memcmp(payload->name + name_length - 5U, ".spis", 5U) ||
            payload->size < 21 || payload->offset < outer->base_address ||
            payload->offset > INT64_MAX - payload->size ||
            payload->offset + payload->size > xx_io_size(outer->device) ||
            (uint64_t)payload->size > SP_DIRECT_MAX_BYTES - bytes)
            goto done;
        bytes += (uint64_t)payload->size;
        sp_view_init(&inner->view, outer->device, payload->offset,
                     payload->size);
        xx_spis_init(&inner->archive, &inner->view.device, 0);
        inner->initialized = true;
        if (!xx_spis_handle_base_info(&inner->archive.format, pd) ||
            inner->archive.format.format_size != payload->size ||
            !inner->archive.format.number_of_archive_records ||
            inner->archive.format.number_of_archive_records >
                SP_DIRECT_MAX_RECORDS - direct->records)
            goto done;
        state = xx_spis_create_archive_records_reading(&inner->archive.format,
                                                        NULL, pd);
        if (!state) goto done;
        valid = true;
        while ((record = xx_spis_get_current_archive_record(
                    &inner->archive.format, state)) != NULL) {
            if (wg_stop(pd) || ++count > SP_DIRECT_MAX_RECORDS ||
                record->compressed_size < 0 || record->data_offset < 0 ||
                record->data_offset > payload->size ||
                record->compressed_size >
                    payload->size - record->data_offset) {
                valid = false;
                break;
            }
            if (!xx_spis_archive_record_move_to_next(&inner->archive.format,
                                                      state, pd)) break;
        }
        xx_spis_free_archive_records_reading(&inner->archive.format, state);
        if (!valid || count != inner->archive.format.number_of_archive_records)
            goto done;
        inner->records = count;
        direct->records += count;
    }
    reader->inner_state = direct;
    direct = NULL;
done:
    sp_direct_free(direct);
    pm_free_stream(carrier);
    return reader->inner_state != NULL;
}

static int64_t spis_size(Abstractformat *f, xx_pd_struct *pd) {
    return xx_sfx_spis_handle_base_info(f, pd) ? f->format_size : -1;
}

static uint64_t spis_count(Abstractformat *f, xx_pd_struct *pd) {
    return xx_sfx_spis_handle_base_info(f, pd)
               ? f->number_of_archive_records : 0;
}

/* Publish offsets in the original executable's address space even though the
 * decoder itself reads from a bounded zero-based view. */
static bool spis_publish_record(sp_direct_records *records,
                                sp_direct *direct) {
    sp_inner *inner = &direct->volumes[records->volume];
    const xx_archive_record *source = xx_spis_get_current_archive_record(
        &inner->archive.format, records->active);
    xx_archive_record *target = &records->base.current_record;
    size_t i;
    if (!source) return false;
    if ((source->header_offset >= 0 &&
         source->header_offset > inner->view.length) ||
        (source->data_offset >= 0 &&
         source->data_offset > inner->view.length)) return false;
    xx_archive_record_cleanup(target);
    xx_archive_record_init(target);
    target->header_offset = source->header_offset < 0
                                ? source->header_offset
                                : inner->view.begin + source->header_offset;
    target->header_size = source->header_size;
    target->data_offset = source->data_offset < 0
                              ? source->data_offset
                              : inner->view.begin + source->data_offset;
    target->compressed_size = source->compressed_size;
    for (i = 0; i < source->list_meta.count; ++i) {
        const xx_meta *item = (const xx_meta *)xx_list_at(&source->list_meta, i);
        xx_meta copy;
        if (!item) continue;
        xx_meta_init(&copy, item->meta_id);
        if (!xx_var_copy(&copy.var, &item->var) ||
            !xx_list_append(&target->list_meta, &copy)) {
            xx_meta_cleanup(&copy);
            xx_archive_record_cleanup(target);
            xx_archive_record_init(target);
            return false;
        }
    }
    return true;
}

static xx_archive_record_state *spis_records(Abstractformat *f,
                                              const xx_list_s *options,
                                              xx_pd_struct *pd) {
    xx_sfx_spis *reader = (xx_sfx_spis *)f;
    sp_direct *direct;
    sp_direct_records *records;
    size_t i;
    if (!spis_ensure_inner(reader, pd)) return pm_create_records(f, options, pd);
    direct = (sp_direct *)reader->inner_state;
    records = (sp_direct_records *)xx_mem_alloc(sizeof(*records));
    if (!records) return NULL;
    xx_archive_record_state_init(&records->base, f);
    records->active = NULL;
    records->volume = 0U;
    records->base.total_records = (int64_t)direct->records;
    for (i = 0; options && i < options->count; ++i) {
        const xx_meta *option = (const xx_meta *)xx_list_at(options, i);
        xx_meta copy;
        if (!option) continue;
        xx_meta_init(&copy, option->meta_id);
        if (!xx_var_copy(&copy.var, &option->var) ||
            !xx_list_append(&records->base.options, &copy)) {
            xx_meta_cleanup(&copy);
            xx_archive_record_state_free(&records->base);
            return NULL;
        }
    }
    records->active = xx_spis_create_archive_records_reading(
        &direct->volumes[0].archive.format, &records->base.options, pd);
    if (!records->active) {
        xx_archive_record_state_free(&records->base);
        return NULL;
    }
    records->base.current_index = 0;
    records->base.has_record = spis_publish_record(records, direct);
    if (!records->base.has_record) {
        xx_spis_free_archive_records_reading(
            &direct->volumes[0].archive.format, records->active);
        xx_archive_record_state_free(&records->base);
        return NULL;
    }
    return &records->base;
}

static const xx_archive_record *spis_current(Abstractformat *f,
                                              xx_archive_record_state *state) {
    xx_sfx_spis *reader = (xx_sfx_spis *)f;
    sp_direct_records *records = (sp_direct_records *)state;
    sp_direct *direct = (sp_direct *)reader->inner_state;
    if (!direct) return pm_current(f, state);
    if (!state || state->format != f || !state->has_record || !records->active)
        return NULL;
    return &state->current_record;
}

static bool spis_next(Abstractformat *f, xx_archive_record_state *state,
                      xx_pd_struct *pd) {
    xx_sfx_spis *reader = (xx_sfx_spis *)f;
    sp_direct *direct = (sp_direct *)reader->inner_state;
    sp_direct_records *records = (sp_direct_records *)state;
    sp_inner *inner;
    if (!direct) return pm_next(f, state, pd);
    if (!state || state->format != f || !state->has_record || !records->active ||
        wg_stop(pd)) return false;
    inner = &direct->volumes[records->volume];
    if (xx_spis_archive_record_move_to_next(&inner->archive.format,
                                             records->active, pd)) {
        ++state->current_index;
        state->has_record = spis_publish_record(records, direct);
        return state->has_record;
    }
    if ((uint64_t)(records->active->current_index + 1) < inner->records) {
        state->has_record = false;
        return false;
    }
    xx_spis_free_archive_records_reading(&inner->archive.format,
                                          records->active);
    records->active = NULL;
    if (++records->volume >= direct->count) {
        state->has_record = false;
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        return false;
    }
    inner = &direct->volumes[records->volume];
    records->active = xx_spis_create_archive_records_reading(
        &inner->archive.format, &state->options, pd);
    state->has_record = records->active != NULL &&
                        spis_publish_record(records, direct);
    if (state->has_record) ++state->current_index;
    return state->has_record;
}

static bool spis_unpack(Abstractformat *f, xx_archive_record_state *state,
                        xx_pd_struct *pd) {
    xx_sfx_spis *reader = (xx_sfx_spis *)f;
    sp_direct *direct = (sp_direct *)reader->inner_state;
    sp_direct_records *records = (sp_direct_records *)state;
    if (!direct) return pm_unpack(f, state, pd);
    if (!state || state->format != f || !state->has_record || !records->active)
        return false;
    return xx_spis_unpack_current_archive_record(
        &direct->volumes[records->volume].archive.format, records->active, pd);
}

static void spis_free_records(Abstractformat *f,
                              xx_archive_record_state *state) {
    xx_sfx_spis *reader = (xx_sfx_spis *)f;
    sp_direct *direct = (sp_direct *)reader->inner_state;
    sp_direct_records *records = (sp_direct_records *)state;
    if (!direct) {
        pm_free_records(f, state);
        return;
    }
    if (!records) return;
    if (records->active)
        xx_spis_free_archive_records_reading(
            &direct->volumes[records->volume].archive.format, records->active);
    xx_archive_record_state_free(&records->base);
}

void xx_sfx_spis_init(xx_sfx_spis *r,xx_io_device *d,int64_t b) {
    Abstractformat *f;
    if (!r) return;
    xx_mem_zero(r, sizeof(*r));
    f = &r->format;
    pm_init(f, d, b, XX_FILE_TYPE_SFX_SPIS, "exe");
    f->check_is_valid = xx_sfx_spis_check_is_valid;
    f->handle_base_info = xx_sfx_spis_handle_base_info;
    f->get_format_size = spis_size;
    f->get_number_of_archive_records = spis_count;
    f->create_archive_records_reading = spis_records;
    f->get_current_archive_record = spis_current;
    f->archive_record_move_to_next = spis_next;
    f->unpack_current_archive_record = spis_unpack;
    f->free_archive_records_reading = spis_free_records;
}
xx_sfx_spis *xx_sfx_spis_create(xx_io_device *d,int64_t b) { xx_sfx_spis *r=(xx_sfx_spis *)xx_mem_alloc(sizeof(*r)); if(r) xx_sfx_spis_init(r,d,b); return r; }
void xx_sfx_spis_destroy(xx_sfx_spis *r) { if(r) { sp_direct_free((sp_direct *)r->inner_state); xx_format_cleanup_extra_parameters(&r->format); } }
void xx_sfx_spis_free(xx_sfx_spis *r) { if(r) { xx_sfx_spis_destroy(r); xx_mem_free(r); } }
bool xx_sfx_spis_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return f && (spis_ensure_inner((xx_sfx_spis *)f,pd) || pm_valid(f,pd)); }
bool xx_sfx_spis_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {
    xx_sfx_spis *reader = (xx_sfx_spis *)f;
    sp_direct *direct;
    int64_t end;
    if (!f) return false;
    if (!spis_ensure_inner(reader, pd)) return pm_handle(f, pd);
    direct = (sp_direct *)reader->inner_state;
    f->format_size = direct->format_size;
    f->number_of_archive_records = direct->records;
    end = f->base_address + direct->format_size;
    f->overlay_size = xx_io_size(f->device) - end;
    f->overlay_offset = f->overlay_size > 0 ? end : -1;
    f->is_valid = true;
    f->base_info_handled = true;
    return true;
}
