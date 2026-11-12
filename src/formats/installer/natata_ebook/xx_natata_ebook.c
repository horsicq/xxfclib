/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Natata #1!#name\xb6 payload !2!#name records. Grammar established from
 * local U3 routine 00779570; stored MAIN_PROG_FILE verified against ARC9. */
#include "xxfclib/formats/installer/natata_ebook/xx_natata_ebook.h"
#include "../../xx_payload_members.h"
#include "xxfclib/formats/executable/pe/xx_pe.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/adler32/xx_adler32.h"
#include "xxfclib/codepages/xx_codepage.h"

#define NAT_MEMORY_CAP (256U * 1024U * 1024U)
#define NAT_MEMBER_CAP (256U * 1024U * 1024U)
#define NAT_WORKSPACE (4U * 1024U * 1024U)
#define NAT_MAX_MEMBERS 100000U

typedef struct nat_member {
    bool html, stored;
    uint64_t resident;
    int64_t header_offset, header_size;
} nat_member;
typedef struct nat_sink {
    xx_io_device device;
    xx_io_device *output;
    xx_pd_struct *pd;
    uint64_t count, limit;
    uint32_t adler;
} nat_sink;

static uint64_t nat_limit(Abstractformat *f, uint32_t id, uint64_t fallback)
{
    const xx_var *v = xx_format_resolve_extra_parameter(f, NULL, id);
    uint64_t value = v ? xx_var_get_u64(v) : fallback;
    return value < fallback ? value : fallback;
}

static uint64_t nat_workspace(void)
{
    uint64_t buffer = xx_get_file_buffer_size();
    if (buffer < 1024U*1024U) buffer = 1024U*1024U;
    if (buffer > NAT_MEMORY_CAP/2U) return (uint64_t)NAT_MEMORY_CAP+1U;
    return buffer*2U+2U*1024U*1024U;
}

static bool nat_write(xx_io_device *output, const void *bytes, size_t size, xx_pd_struct *pd)
{
    size_t done = 0;
    while (output && done < size) {
        ssize_t n;
        if (xx_pd_is_stopped(pd)) return false;
        n = xx_io_write(output, (const uint8_t *)bytes+done, size-done);
        if (n <= 0 || (size_t)n > size-done) return false;
        done += (size_t)n;
    }
    return !xx_pd_is_stopped(pd);
}

static ssize_t nat_sink_write(xx_io_device *device, const void *bytes, size_t size)
{
    nat_sink *s = (nat_sink *)device->priv;
    if (size > (size_t)PTRDIFF_MAX || size > s->limit-s->count || !nat_write(s->output, bytes, size, s->pd)) return -1;
    s->count += size;
    s->adler = xx_adler32_update(s->adler, bytes, size);
    return (ssize_t)size;
}

static bool nat_zlib(Abstractformat *f, int64_t start, int64_t available, xx_io_device *output,
                     uint64_t maximum, uint64_t *decoded, int64_t *packed, xx_pd_struct *pd)
{
    uint8_t h[2], trailer[4];
    int64_t consumed = 0;
    nat_sink sink;
    uint32_t adler;
    if (available < 6 || !pm_read(f, start, h, 2U) || !xx_zlib_stream_header_is_valid(h, 2U)) return false;
    xx_mem_zero(&sink, sizeof(sink));
    sink.device.write = nat_sink_write; sink.device.priv = &sink;
    sink.output = output; sink.pd = pd; sink.limit = maximum; sink.adler = 1U;
    if (!xx_deflate_unpack_device_ex(f->device, f->base_address+start+2, available-6,
                                     &sink.device, false, (size_t)1U << ((h[0] >> 4)+8U), NULL, 0U, &consumed, pd) ||
        consumed < 0 || consumed > available-6 || !pm_read(f, start+2+consumed, trailer, 4U)) return false;
    adler = ((uint32_t)trailer[0] << 24) | ((uint32_t)trailer[1] << 16) | ((uint32_t)trailer[2] << 8) | trailer[3];
    if (adler != sink.adler || xx_pd_is_stopped(pd)) return false;
    *packed = consumed+6;
    *decoded = sink.count;
    return true;
}

static bool nat_equal(const char *a, const char *b)
{
    while (*a && *b) {
        unsigned char x = (unsigned char)*a++, y = (unsigned char)*b++;
        if (x >= 'a' && x <= 'z') x -= 'a'-'A';
        if (y >= 'a' && y <= 'z') y -= 'a'-'A';
        if (x != y) return false;
    }
    return !*a && !*b;
}

static char *nat_name(const uint8_t *bytes, size_t size)
{
    const xx_codepage *cp = xx_codepage_by_number(1252U);
    char *result = (char *)xx_mem_alloc(size*3U+1U), *component;
    size_t i, used = 0;
    if (!result || !cp) { xx_mem_free(result); return NULL; }
    for (i = 0; i < size; ++i) {
        size_t n;
        uint8_t c = bytes[i];
        if (c < 32U || c == ':' || c == '<' || c == '>' || c == '"' || c == '|' || c == '?' || c == '*') goto invalid;
        if (c == '\\' || c == '/') { result[used++] = '/'; continue; }
        n = xx_codepage_byte_to_utf8(cp, c, (unsigned char *)result+used, 3U);
        if (!n) goto invalid;
        used += n;
    }
    result[used] = 0;
    component = result;
    for (i = 0; i <= used; ++i) {
        if (!result[i] || result[i] == '/') {
            size_t n = (size_t)(result+i-component), j = 0;
            char stem[5] = {0};
            if (!n || component[n-1U] == '.' || component[n-1U] == ' ') goto invalid;
            while (j < n && component[j] != '.' && j < 4U) { stem[j] = component[j]; ++j; }
            if (nat_equal(stem,"CON") || nat_equal(stem,"PRN") || nat_equal(stem,"AUX") || nat_equal(stem,"NUL") ||
                (j == 4U && (n == 4U || component[4] == '.') && stem[3] >= '1' && stem[3] <= '9' &&
                 (((stem[0] == 'C' || stem[0] == 'c') && (stem[1] == 'O' || stem[1] == 'o') && (stem[2] == 'M' || stem[2] == 'm')) ||
                  ((stem[0] == 'L' || stem[0] == 'l') && (stem[1] == 'P' || stem[1] == 'p') && (stem[2] == 'T' || stem[2] == 't'))))) goto invalid;
            component = result+i+1U;
        }
    }
    return result;
invalid:
    xx_mem_free(result);
    return NULL;
}

static int64_t nat_pe_extent(Abstractformat *f, int64_t at, xx_pd_struct *pd)
{
    xx_pe *pe = xx_pe_create(f->device, f->base_address+at);
    int64_t size = pe ? xx_pe_get_format_size(&pe->format, pd) : -1;
    xx_pe_free(pe);
    return size;
}

static bool nat_decode(Abstractformat *f, pm_member *m, xx_io_device *output, xx_pd_struct *pd)
{
    nat_member *context = (nat_member *)m->context;
    uint64_t decoded = 0, prefix = context && context->html ? 6U : 0U;
    int64_t packed = 0;
    uint64_t budget = nat_limit(f, XX_META_ID_OPT_MEMORY_LIMIT, NAT_MEMORY_CAP);
    uint64_t workspace = context && context->stored ? NAT_WORKSPACE : nat_workspace();
    if (!context || context->resident > budget || workspace > budget-context->resident ||
        (uint64_t)m->size > nat_limit(f, XX_META_ID_OPT_MAX_MEMBER_SIZE, NAT_MEMBER_CAP) || xx_pd_is_stopped(pd)) return false;
    if (context->stored) {
        uint8_t buffer[16384];
        int64_t at = 0;
        while (at < m->size) {
            size_t n = (uint64_t)(m->size-at) < sizeof(buffer) ? (size_t)(m->size-at) : sizeof(buffer);
            if (xx_pd_is_stopped(pd) || !pm_read(f,m->offset-f->base_address+at,buffer,n) || !nat_write(output,buffer,n,pd)) return false;
            at += (int64_t)n;
        }
        return !xx_pd_is_stopped(pd);
    }
    if (prefix && !nat_write(output,"<html>",6U,pd)) return false;
    return nat_zlib(f, m->offset-f->base_address, m->packed_size, output, (uint64_t)m->size-prefix, &decoded, &packed, pd) &&
           decoded+prefix == (uint64_t)m->size && packed == m->packed_size;
}

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    int64_t total = pm_available(f), at = 0;
    uint64_t budget = nat_limit(f, XX_META_ID_OPT_MEMORY_LIMIT, NAT_MEMORY_CAP), names = 0;
    uint64_t maximum = nat_limit(f, XX_META_ID_OPT_MAX_MEMBER_SIZE, NAT_MEMBER_CAP);
    size_t seen = 0, i;
    uint8_t magic[4];
    if (xx_pd_is_stopped(pd) || total < 11 || budget < NAT_WORKSPACE+sizeof(pm_stream) || !pm_read(f,0,magic,4U)) return false;
    if (magic[0] == 'M' && magic[1] == 'Z') {
        at = nat_pe_extent(f,0,pd);
        if (at < 0 || at >= total) return false;
    }
    while (at < total) {
        uint8_t name[1024], c, footer[4], repeated[1024];
        size_t length = 0;
        int64_t payload, packed = 0;
        uint64_t decoded = 0, prefix = 0, resident;
        char *display;
        bool stored = false, skip;
        nat_member *context = NULL;
        if (xx_pd_is_stopped(pd) || ++seen > NAT_MAX_MEMBERS || !pm_read(f,at,magic,4U) || xx_rt_memcmp(magic,"#1!#",4U)) return false;
        at += 4;
        for (;;) {
            if (!pm_read(f,at++,&c,1U)) return false;
            if (c == 0xb6U) break;
            if (length == sizeof(name)) return false;
            name[length++] = c;
        }
        if (!length) return false;
        display = nat_name(name,length);
        if (!display) return false;
        payload = at;
        skip = nat_equal(display,"XTOC.NATC");
        resident = sizeof(pm_stream) + (uint64_t)(s->count ? s->capacity*2U : 8U)*sizeof(pm_member) +
                   (uint64_t)(s->count+1U)*sizeof(nat_member) + names + length*3U+1U;
        if (resident > budget || NAT_WORKSPACE > budget-resident) { xx_mem_free(display); return false; }
        if (nat_equal(display,"MAIN_PROG_FILE") && pm_read(f,payload,magic,2U) && magic[0] == 'M' && magic[1] == 'Z') {
            packed = nat_pe_extent(f,payload,pd);
            stored = true;
            if (packed < 0 || packed > total-payload || (uint64_t)packed > maximum) { xx_mem_free(display); return false; }
            decoded = (uint64_t)packed;
        } else {
            size_t n = xx_rt_strlen(display);
            prefix = (n >= 4U && nat_equal(display+n-4U,".htm")) || (n >= 5U && nat_equal(display+n-5U,".html")) ? 6U : 0U;
            if (nat_workspace() > budget-resident || prefix > maximum ||
                !nat_zlib(f,payload,total-payload,NULL,maximum-prefix,&decoded,&packed,pd)) { xx_mem_free(display); return false; }
        }
        at = payload+packed;
        if (!pm_read(f,at,footer,4U) || xx_rt_memcmp(footer,"!2!#",4U) ||
            !pm_read(f,at+4,repeated,length) || xx_rt_memcmp(repeated,name,length)) { xx_mem_free(display); return false; }
        at += 4+(int64_t)length;
        if (skip) { xx_mem_free(display); continue; }
        context = (nat_member *)xx_mem_alloc(sizeof(*context));
        if (!context) { xx_mem_free(display); return false; }
        context->html = prefix != 0; context->stored = stored; context->resident = 0;
        context->header_offset = f->base_address+payload-5-(int64_t)length;
        context->header_size = 5+(int64_t)length;
        if (!pm_add(f,s,"natata",payload,packed)) { xx_mem_free(context); xx_mem_free(display); return false; }
        s->items[s->count-1U].display_name = display;
        s->items[s->count-1U].size = (int64_t)(decoded+prefix);
        s->items[s->count-1U].compression_method = stored ? 0U : 8U;
        s->items[s->count-1U].context = context;
        s->items[s->count-1U].free_context = xx_mem_free;
        s->items[s->count-1U].read_all = nat_decode;
        names += xx_rt_strlen(display)+1U;
    }
    for (i = 0; i < s->count; ++i) if (s->items[i].context)
        ((nat_member *)s->items[i].context)->resident = sizeof(pm_stream)+s->capacity*sizeof(pm_member)+s->count*sizeof(nat_member)+names;
    s->size = total;
    return seen && !xx_pd_is_stopped(pd);
}

static const xx_archive_record *nat_current(Abstractformat *f, xx_archive_record_state *state)
{
    const xx_archive_record *record = pm_current(f,state);
    if (record) {
        pm_stream *s = (pm_stream *)state->internal_state;
        nat_member *context = (nat_member *)s->items[s->index].context;
        state->current_record.header_offset = context->header_offset;
        state->current_record.header_size = context->header_size;
    }
    return record;
}

Abstractformat *xx_natata_ebook_create(xx_io_device *device, int64_t base_address)
{
    Abstractformat *f = (Abstractformat *)xx_mem_alloc(sizeof(*f));
    if (f) {
        pm_init(f,device,base_address,XX_FILE_TYPE_NATATA_EBOOK,"exe");
        f->get_current_archive_record = nat_current;
    }
    return f;
}
void xx_natata_ebook_free(Abstractformat *f)
{
    if (f) { xx_format_destroy(f); xx_mem_free(f); }
}
#include "../../xx_format_abstract_extractor_adapter.h"
static Abstractformat *natata_open(xx_io_device *d) { return xx_natata_ebook_create(d,0); }
static const xx_file_type_t natata_types[] = {XX_FILE_TYPE_NATATA_EBOOK};
static const uint8_t natata_pe_magic[] = {'M','Z'}, natata_record_magic[] = {'#','1','!','#'};
static const xx_format_search_anchor natata_anchors[] = {{natata_pe_magic,2U,0U},{natata_record_magic,4U,0U}};
const xx_format_search_desc xx_natata_ebook_search_desc = {natata_types,1U,natata_anchors,2U,natata_open,xx_natata_ebook_free,true};
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(natata_ebook,xx_natata_ebook_search_desc)
