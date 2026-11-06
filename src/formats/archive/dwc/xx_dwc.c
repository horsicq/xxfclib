/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT
 * Bounded DWC STORE and bounded 9-15-bit LZW with packed CRC-16/ARC.
 * Active ASCII names are safe and flat; inactive name bytes and timestamps
 * remain opaque. Bounded zero/raw controls only; no producer authority.
 */
#include "xxfclib/formats/archive/dwc/xx_dwc.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/global/xx_global.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/strings/xx_string.h"
#include <stdio.h>

#define DWC_MAX_SOURCE (UINT64_C(64) * 1024U * 1024U)
#define DWC_MAX_MEMBER (UINT64_C(16) * 1024U * 1024U)
#define DWC_MAX_TOTAL (UINT64_C(64) * 1024U * 1024U)
#define DWC_MAX_RECORDS 256U
#define DWC_OVERHEAD UINT64_C(262144)
#define DWC_OPTION_BYTES 8192U
typedef struct dwc_entry { size_t header, header_end, payload, end, packed, raw; uint16_t crc; uint8_t method; char name[256]; } dwc_entry;
typedef struct dwc_info { size_t input_size, count, max_raw; uint64_t total_raw; uint8_t *input; dwc_entry entries[DWC_MAX_RECORDS]; } dwc_info;
typedef struct dwc_cursor { dwc_info info; dwc_entry entry; } dwc_cursor;
static uint64_t dwc_budget(Abstractformat *, const xx_list_s *, uint32_t);
static bool dwc_options_valid(const xx_list_s *);
static bool dwc_ready(Abstractformat *, const dwc_info *, const xx_list_s *, xx_pd_struct *);
static uint16_t dwc_u16(const uint8_t *p) { return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1]<<8U)); }
static uint32_t dwc_u32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1]<<8U) | ((uint32_t)p[2]<<16U) | ((uint32_t)p[3]<<24U); }
static bool dwc_read_at(Abstractformat *f, int64_t at, void *buffer, size_t n, xx_pd_struct *pd)
{
    int64_t saved; size_t done=0U; bool ok;
    if (!f || !f->device || at<0 || (!buffer && n)) return false;
    saved=xx_io_tell(f->device); if (saved<0) return false;
    ok=!(pd && xx_pd_is_stopped(pd)) && xx_io_seek64(f->device,at,SEEK_SET)==0;
    while (ok && done<n) {
        size_t step=n-done; ssize_t got;
        if (pd && xx_pd_is_stopped(pd)) { ok=false; break; }
        if (step>65536U) step=65536U;
        got=xx_io_read(f->device,(uint8_t *)buffer+done,step);
        if (got<=0 || (size_t)got>step) ok=false; else done+=(size_t)got;
    }
    if (xx_io_seek64(f->device,saved,SEEK_SET)!=0) ok=false;
    return ok && !(pd && xx_pd_is_stopped(pd));
}
static char dwc_lower(char c) { return c>='A' && c<='Z' ? (char)(c+('a'-'A')) : c; }
static bool dwc_safe_name(char *name, const uint8_t *field, size_t n)
{
    size_t stem=0U; char lower[256];
    if (!n || n>255U) return false;
    for (size_t i=0U;i<n;++i) {
        unsigned c=field[i];
        if (c<=32U || c>=127U || c=='/' || c=='\\' || c==':' || c=='<' || c=='>' || c=='"' || c=='|' || c=='?' || c=='*') return false;
        name[i]=(char)c;lower[i]=dwc_lower((char)c);
    }
    name[n]=lower[n]=0;
    if (name[n-1U]=='.') return false;
    while (stem<n && lower[stem]!='.') ++stem;
    if (stem==3U && (!xx_rt_memcmp(lower,"con",3U)||!xx_rt_memcmp(lower,"prn",3U)||!xx_rt_memcmp(lower,"aux",3U)||!xx_rt_memcmp(lower,"nul",3U))) return false;
    if (stem==4U && (!xx_rt_memcmp(lower,"com",3U)||!xx_rt_memcmp(lower,"lpt",3U)) && lower[3]>='1' && lower[3]<='9') return false;
    if ((stem==6U && (!xx_rt_memcmp(lower,"clock$",6U)||!xx_rt_memcmp(lower,"conin$",6U))) ||
        (stem==7U && !xx_rt_memcmp(lower,"conout$",7U))) return false;
    return true;
}
/* Bounded DWC method 1: MSB codes grow early from 9 through 15 bits.
 * Before further data at counts 256 + 512k, 000 continues the dictionary;
 * a leading 1 selects bounded literal blocks, then a mandatory reset literal.
 * Nonzero standalone selectors reset the dictionary; saturation is unsupported.
 * Complete phrases and blocks must fit exact raw size. Final occupied byte
 * is exact; residual sub-byte values stay opaque. No CLEAR/END is assigned.
 * NULL output performs the same scan. Fixed dictionary scratch is 128 KiB,
 * within the existing 256 KiB per-iterator overhead charge. */
typedef struct dwc_bits { const uint8_t *input; size_t packed,at; } dwc_bits;
static bool dwc_bits_read(dwc_bits *b,unsigned width,unsigned *value,xx_pd_struct *pd)
{
    unsigned out=0U;
    if (!b || !value || !width || width>15U || b->at>b->packed*8U ||
        b->packed*8U-b->at<width || (pd && xx_pd_is_stopped(pd))) return false;
    for (unsigned j=0U;j<width;++j,++b->at) out=(out<<1U)|((b->input[b->at/8U]>>(7U-(unsigned)(b->at%8U)))&1U);
    *value=out;return true;
}
static bool dwc_lzw_bounded(const uint8_t *input,size_t packed,uint8_t *plain,size_t raw,xx_pd_struct *pd)
{
    uint16_t prefix[32768],previous=0U,next=256U;uint8_t suffix[32768],stack[32768];
    size_t produced=0U,codes=0U,since_reset=0U,raw_blocks=0U;unsigned width=9U;
    dwc_bits bits={input,packed,0U};
    if (!input || !packed || packed>DWC_MAX_MEMBER || !raw || (pd && xx_pd_is_stopped(pd))) return false;
    while (produced<raw) {
        unsigned value=0U;uint16_t code,original;size_t n=0U;uint8_t first;bool special;
        if (codes>=DWC_MAX_MEMBER || (pd && xx_pd_is_stopped(pd))) return false;
        if (since_reset%512U==256U) {
            if (!dwc_bits_read(&bits,1U,&value,pd)) return false;
            if (!value) {
                if (!dwc_bits_read(&bits,2U,&value,pd)) return false;
                if (value) {
                    /* All nonzero selectors share the logical reset. The
                     * mandatory next code is a fresh 9-bit literal, without
                     * alignment or changes to global code/raw-block counts. */
                    next=256U;previous=0U;width=9U;since_reset=0U;
                }
            } else {
                unsigned repeat;
                do {
                    unsigned length;
                    if (raw_blocks>=DWC_MAX_MEMBER) return false;
                    do {if(!dwc_bits_read(&bits,1U,&value,pd))return false;} while(value);
                    if (!dwc_bits_read(&bits,15U,&length,pd) || length>raw-produced) return false;
                    ++raw_blocks;
                    for (unsigned j=0U;j<length;++j) {
                        if (!dwc_bits_read(&bits,8U,&value,pd)) return false;
                        if (plain) plain[produced]=(uint8_t)value;
                        ++produced;
                    }
                    if (!dwc_bits_read(&bits,1U,&repeat,pd)) return false;
                } while(repeat);
                /* A raw group must complete its continuation state and leave
                 * room for the unconditional fresh literal after reset. */
                if (produced>=raw) return false;
                next=256U;previous=0U;width=9U;since_reset=0U;
            }
        }
        if (!dwc_bits_read(&bits,width,&value,pd)) return false;
        code=original=(uint16_t)value;
        if (!since_reset) {
            if (code>=256U) return false;
            if (plain) plain[produced]=(uint8_t)code;
            ++produced;++codes;++since_reset;previous=code;continue;
        }
        if (code>next) return false;
        special=code==next;
        if (special) code=previous;
        while (code>=256U) {
            if (code>=next || n>=sizeof(stack) || prefix[code]>=code || (pd && xx_pd_is_stopped(pd))) return false;
            stack[n++]=suffix[code];code=prefix[code];
        }
        if (n>=sizeof(stack)) return false;
        stack[n++]=(uint8_t)code;first=(uint8_t)code;
        if (n+(special?1U:0U)>raw-produced) return false;
        for (size_t j=n;j>0U;--j) {
            if (pd && xx_pd_is_stopped(pd)) return false;
            if (plain) plain[produced]=stack[j-1U];
            ++produced;
        }
        if (special) {if(plain)plain[produced]=first;++produced;}
        if (next>=32768U) return false;
        prefix[next]=previous;suffix[next]=first;++next;
        if (width<15U && next>=((1U<<width)-1U)) ++width;
        previous=original;++codes;++since_reset;
    }
    return (bits.at+7U)/8U==packed && !(pd && xx_pd_is_stopped(pd));
}
/* One bounded packed CRC pass is used for catalog validation and emission.
 * A NULL output validates only. Empty STORE still verifies its declared CRC
 * and cancellation state; extraction later materializes an ordinary file. */
static bool dwc_payload_check(const uint8_t *input,size_t packed,uint8_t *plain,size_t raw,uint16_t expected_crc,uint8_t method,xx_pd_struct *pd)
{
    uint16_t crc=0U;size_t at=0U;
    if (!input || (method!=1U && method!=2U) || (method==2U && packed!=raw) || (pd && xx_pd_is_stopped(pd))) return false;
    while (at<packed) {
        size_t n=packed-at;if(n>65536U)n=65536U;
        if (pd && xx_pd_is_stopped(pd)) return false;
        crc=xx_crc16_arc_calc(crc,input+at,n);
        if (method==2U && plain) xx_rt_memcpy(plain+at,input+at,n);
        at+=n;
    }
    if (crc!=expected_crc || (pd && xx_pd_is_stopped(pd))) return false;
    return method==2U || dwc_lzw_bounded(input,packed,plain,raw,pd);
}
static void dwc_info_free(dwc_info *info) { if(info) {xx_mem_free(info->input);info->input=NULL;} }
static bool dwc_parse(Abstractformat *f,dwc_info *info,const xx_list_s *options,xx_pd_struct *pd)
{
    int64_t total;uint64_t memory;size_t footer,table,total_packed=0U;uint8_t h[27];bool ok=false;
    if (!info) return false;
    xx_rt_memset(info,0,sizeof(*info));
    if (!f || !f->device || f->base_address<0 || (pd && xx_pd_is_stopped(pd)) || !dwc_options_valid(options)) return false;
    total=xx_io_total_size(f->device);memory=dwc_budget(f,options,XX_META_ID_OPT_MEMORY_LIMIT);
    if (total<f->base_address || total-f->base_address<61 || (uint64_t)(total-f->base_address)>DWC_MAX_SOURCE) return false;
    info->input_size=(size_t)(total-f->base_address);footer=info->input_size-27U;
    if (memory<DWC_OVERHEAD || info->input_size>memory-DWC_OVERHEAD ||
        !dwc_read_at(f,f->base_address+(int64_t)footer,h,sizeof(h),pd) ||
        dwc_u16(h)!=27U || h[2]!=34U || xx_rt_memcmp(h+24U,"DWC",3U)) return false;
    /* The bounded profile uses a positive 32-bit count, matching current U3.
     * Deark reads 16 bits here; current high-zero bytes do not establish a
     * universal count width. Other footer metadata is preserved as opaque. */
    info->count=dwc_u32(h+20U);
    if (!info->count || info->count>DWC_MAX_RECORDS || info->count>footer/34U) return false;
    table=footer-info->count*34U;
    info->input=(uint8_t *)xx_mem_alloc(info->input_size);
    if (!info->input || !dwc_read_at(f,f->base_address,info->input,info->input_size,pd) ||
        xx_rt_memcmp(h,info->input+footer,sizeof(h))) goto done;
    for (size_t i=0U;i<info->count;++i) {
        dwc_entry *e=&info->entries[i];size_t at=table+i*34U,len=0U;
        if (pd && xx_pd_is_stopped(pd)) goto done;
        while (len<13U && info->input[at+len]) ++len;
        if (!len || len>=13U || !dwc_safe_name(e->name,info->input+at,len)) goto done;
        /* Inactive name-field bytes after the first NUL are not a pathname. */
        for (size_t j=0U;j<i;++j) {
            size_t k=0U;
            while (dwc_lower(e->name[k])==dwc_lower(info->entries[j].name[k])) {
                if (!e->name[k]) goto done;
                ++k;
            }
        }
        e->header=at;e->header_end=at+34U;e->raw=dwc_u32(info->input+at+13U);
        e->packed=dwc_u32(info->input+at+21U);e->payload=dwc_u32(info->input+at+25U);e->crc=dwc_u16(info->input+at+32U);e->method=info->input[at+29U];
        if ((e->method!=1U && e->method!=2U) || info->input[at+30U] || info->input[at+31U] ||
            (e->method==2U && e->packed!=e->raw) || (e->method==1U && (!e->packed || e->packed>DWC_MAX_MEMBER || !e->raw)) || e->raw>DWC_MAX_MEMBER || e->packed>DWC_MAX_MEMBER || e->payload>table ||
            e->packed>table-e->payload || e->packed>table-total_packed) goto done;
        e->end=e->payload+e->packed;total_packed+=e->packed;
        /* Catalog order does not determine physical payload order. Bounds,
         * disjoint nonempty spans and checked total==table prove exact body
         * coverage. Empty STORE offsets stay bounded but add no coverage. */
        for (size_t j=0U;j<i;++j) {
            const dwc_entry *prior=&info->entries[j];
            if (pd && xx_pd_is_stopped(pd)) goto done;
            if (e->packed && prior->packed && e->payload<prior->end && prior->payload<e->end) goto done;
        }
        info->total_raw+=e->raw;if(info->total_raw>DWC_MAX_TOTAL) goto done;
        if(e->raw>info->max_raw)info->max_raw=e->raw;
        if(!dwc_ready(f,info,options,pd) || !dwc_payload_check(info->input+e->payload,e->packed,NULL,e->raw,e->crc,e->method,pd)) goto done;
    }
    if (total_packed!=table || !dwc_ready(f,info,options,pd)) goto done;
    ok=true;
done:
    if (!ok) dwc_info_free(info);
    return ok;
}
static bool dwc_unchanged(Abstractformat *f,const dwc_info *info,xx_pd_struct *pd)
{
    uint8_t *part;size_t at=0U;bool ok=true;int64_t total=xx_io_total_size(f->device);
    if(total<f->base_address || (uint64_t)(total-f->base_address)!=info->input_size)return false;
    part=(uint8_t *)xx_mem_alloc(65536U);if(!part)return false;
    while(at<info->input_size) {
        size_t n=info->input_size-at;if(n>65536U)n=65536U;
        if(!dwc_read_at(f,f->base_address+(int64_t)at,part,n,pd)||xx_rt_memcmp(part,info->input+at,n)){ok=false;break;}
        at+=n;
    }
    xx_mem_free(part);return ok;
}
static const xx_var *dwc_option(const xx_list_s *options, uint32_t id)
{
    size_t i;
    if (!options) return NULL;
    for (i = 0U; i < options->count; ++i) {
        const xx_meta *m = (const xx_meta *)xx_list_at((const xx_list_t *)options, i);
        if (m && m->meta_id == id) return &m->var;
    }
    return NULL;
}

static uint64_t dwc_budget(Abstractformat *f, const xx_list_s *options, uint32_t id)
{
    const xx_var *v = xx_format_resolve_extra_parameter(f, NULL, id);
    uint64_t limit = v ? xx_var_get_u64(v) : UINT64_MAX;
    size_t i;
    if (options) for (i = 0U; i < options->count; ++i) {
        const xx_meta *m = (const xx_meta *)xx_list_at((const xx_list_t *)options, i);
        if (m && m->meta_id == id && xx_var_get_u64(&m->var) < limit) limit = xx_var_get_u64(&m->var);
    }
    return limit;
}

static bool dwc_options_valid(const xx_list_s *options)
{
    size_t i, bytes = 0U;
    if (!options) return true;
    if (options->count > 64U) return false;
    for (i = 0U; i < options->count; ++i) {
        const xx_meta *m = (const xx_meta *)xx_list_at((const xx_list_t *)options, i);
        size_t n;
        if (!m) return false;
        if (m->var.type == XX_VAR_TYPE_STRING || m->var.type == XX_VAR_TYPE_STRING_VIEW) {
            n = m->var.val.str.len;
            if ((!m->var.val.str.ptr && n) || bytes >= DWC_OPTION_BYTES || n >= DWC_OPTION_BYTES - bytes) return false;
            bytes += n + 1U;
        } else if (m->var.type == XX_VAR_TYPE_WSTRING || m->var.type == XX_VAR_TYPE_WSTRING_VIEW) {
            n = m->var.val.wstr.len;
            if ((!m->var.val.wstr.ptr && n) || (DWC_OPTION_BYTES - bytes) / sizeof(wchar_t) == 0U ||
                n >= (DWC_OPTION_BYTES - bytes) / sizeof(wchar_t)) return false;
            bytes += (n + 1U) * sizeof(wchar_t);
        } else if (m->var.type >= XX_VAR_TYPE_STRING) return false;
    }
    return true;
}

static bool dwc_ready(Abstractformat *f, const dwc_info *info, const xx_list_s *options, xx_pd_struct *pd)
{
    return !(pd && xx_pd_is_stopped(pd)) && dwc_options_valid(options) &&
        (uint64_t)info->max_raw <= dwc_budget(f, options, XX_META_ID_OPT_MAX_MEMBER_SIZE) &&
        (uint64_t)info->input_size + info->max_raw + DWC_OVERHEAD <= dwc_budget(f, options, XX_META_ID_OPT_MEMORY_LIMIT);
}

/* Bounded owned options keep the 256KiB overhead charge conservative even
 * during wide-path conversion. Views are cloned, never borrowed. */
static bool dwc_copy_options(xx_list_s *to, const xx_list_s *from)
{
    size_t i, bytes = 0U;
    if (!from) return true;
    if (!dwc_options_valid(from)) return false;
    for (i = 0U; i < from->count; ++i) {
        const xx_meta *m = (const xx_meta *)xx_list_at((const xx_list_t *)from, i);
        xx_meta copy;
        bool ok = false;
        if (!m) return false;
        xx_meta_init(&copy, m->meta_id);
        if (m->var.type == XX_VAR_TYPE_STRING || m->var.type == XX_VAR_TYPE_STRING_VIEW) {
            size_t n = m->var.val.str.len;
            char *s;
            if (bytes >= DWC_OPTION_BYTES || n >= DWC_OPTION_BYTES - bytes) return false;
            bytes += n + 1U;
            s = (char *)xx_mem_alloc(n + 1U);
            if (s) { if (n) xx_rt_memcpy(s, m->var.val.str.ptr, n); s[n] = 0; ok = xx_var_set_str_take(&copy.var, s, n); if (!ok) xx_mem_free(s); }
        } else if (m->var.type == XX_VAR_TYPE_WSTRING || m->var.type == XX_VAR_TYPE_WSTRING_VIEW) {
            size_t n = m->var.val.wstr.len;
            wchar_t *s;
            if ((DWC_OPTION_BYTES - bytes) / sizeof(wchar_t) == 0U ||
                n >= (DWC_OPTION_BYTES - bytes) / sizeof(wchar_t)) return false;
            bytes += (n + 1U) * sizeof(wchar_t);
            s = (wchar_t *)xx_mem_alloc((n + 1U) * sizeof(wchar_t));
            if (s) { if (n) xx_rt_memcpy(s, m->var.val.wstr.ptr, n * sizeof(wchar_t)); s[n] = 0; ok = xx_var_set_wstr_take(&copy.var, s, n); if (!ok) xx_mem_free(s); }
        } else if (m->var.type < XX_VAR_TYPE_STRING) ok = xx_var_copy(&copy.var, &m->var);
        if (!ok || !xx_list_append(to, &copy)) { xx_meta_cleanup(&copy); return false; }
    }
    return true;
}

/* Iterator options are public and may be appended after catalog creation.
 * Consume views by their declared length, never by searching for a NUL. */
static char *dwc_owned_path(const xx_var *v)
{
    size_t n;
    if (!v) return NULL;
    if (v->type == XX_VAR_TYPE_STRING || v->type == XX_VAR_TYPE_STRING_VIEW) {
        char *s;
        n = v->val.str.len;
        if (n >= DWC_OPTION_BYTES || (!v->val.str.ptr && n)) return NULL;
        s = (char *)xx_mem_alloc(n + 1U);
        if (s) { if (n) xx_rt_memcpy(s, v->val.str.ptr, n); s[n] = 0; }
        return s;
    }
    if (v->type == XX_VAR_TYPE_WSTRING || v->type == XX_VAR_TYPE_WSTRING_VIEW) {
        wchar_t *wide;
        char *s;
        n = v->val.wstr.len;
        if (n >= DWC_OPTION_BYTES / sizeof(wchar_t) || (!v->val.wstr.ptr && n)) return NULL;
        wide = (wchar_t *)xx_mem_alloc((n + 1U) * sizeof(wchar_t));
        if (!wide) return NULL;
        if (n) xx_rt_memcpy(wide, v->val.wstr.ptr, n * sizeof(wchar_t));
        wide[n] = 0; s = xx_str_unicode_to_utf8(wide); xx_mem_free(wide); return s;
    }
    return NULL;
}


static bool dwc_decode(Abstractformat *f,const dwc_cursor *cursor,const xx_list_s *options,uint8_t **result,xx_pd_struct *pd)
{
    const dwc_entry *e=&cursor->entry;uint8_t *plain;*result=NULL;
    if(!dwc_ready(f,&cursor->info,options,pd)||!dwc_unchanged(f,&cursor->info,pd))return false;
    plain=(uint8_t *)xx_mem_alloc(e->raw ? e->raw : 1U);if(!plain)return false;
    if(!dwc_ready(f,&cursor->info,options,pd)||!dwc_payload_check(cursor->info.input+e->payload,e->packed,plain,e->raw,e->crc,e->method,pd)||
       !dwc_ready(f,&cursor->info,options,pd)||!dwc_unchanged(f,&cursor->info,pd)){xx_mem_free(plain);return false;}
    *result=plain;return true;
}
static bool dwc_record(Abstractformat *f,xx_archive_record_state *s,dwc_cursor *cursor)
{
    xx_archive_record *r=&s->current_record;const dwc_entry *e=&cursor->entry;
    r->header_offset=f->base_address+(int64_t)e->header;r->header_size=(int64_t)(e->header_end-e->header);
    r->data_offset=f->base_address+(int64_t)e->payload;r->compressed_size=(int64_t)e->packed;
    return xx_archive_record_set_original_name(r,e->name)&&xx_archive_record_set_meta_u64(r,XX_META_ID_COMPRESSED_SIZE,e->packed)&&
       xx_archive_record_set_meta_u64(r,XX_META_ID_UNCOMPRESSED_SIZE,e->raw)&&xx_archive_record_set_meta_str(r,XX_META_ID_COMPRESSION_METHOD,e->method==2U?"stored":"DWC LZW 9-15 bounded raw-control subset")&&
       xx_archive_record_set_meta_bool(r,XX_META_ID_IS_ENCRYPTED,false)&&xx_archive_record_set_meta_bool(r,XX_META_ID_IS_FOLDER,false);
}
void xx_dwc_init(xx_dwc *a, xx_io_device *device, int64_t base)
{
    if (!a) return;
    xx_rt_memset(a, 0, sizeof(*a)); xx_format_init(&a->format, device, base);
    a->format.endian = XX_ENDIAN_LITTLE; a->format.file_type = XX_FILE_TYPE_DWC;
    a->format.format_type = XX_TYPE_ARCHIVE; a->format.is_archive = true;
    xx_format_set_mime_type(&a->format, "application/x-dwc"); xx_format_set_extension(&a->format, "dwc");
    a->format.check_is_valid = xx_dwc_check_is_valid;
    a->format.handle_base_info = xx_dwc_handle_base_info;
    a->format.get_format_size = xx_dwc_get_format_size;
    a->format.get_number_of_archive_records = xx_dwc_get_number_of_archive_records;
    a->format.create_archive_records_reading = xx_dwc_create_archive_records_reading;
    a->format.get_current_archive_record = xx_dwc_get_current_archive_record;
    a->format.unpack_current_archive_record = xx_dwc_unpack_current_archive_record;
    a->format.archive_record_move_to_next = xx_dwc_archive_record_move_to_next;
    a->format.free_archive_records_reading = xx_dwc_free_archive_records_reading;
    a->archive_end = -1;
}
xx_dwc *xx_dwc_create(xx_io_device *device, int64_t base)
{ xx_dwc *a = (xx_dwc *)xx_mem_alloc(sizeof(*a)); if (a) xx_dwc_init(a, device, base); return a; }
void xx_dwc_destroy(xx_dwc *a) { if (a) xx_format_cleanup_extra_parameters(&a->format); }
void xx_dwc_free(xx_dwc *a) { if (a) { xx_dwc_destroy(a); xx_mem_free(a); } }
bool xx_dwc_check_is_valid(Abstractformat *f,xx_pd_struct *pd)
{
    dwc_info *i;bool ok=false;
    if(!f || dwc_budget(f,NULL,XX_META_ID_OPT_MEMORY_LIMIT)<DWC_OVERHEAD || (pd && xx_pd_is_stopped(pd)))return false;
    i=(dwc_info *)xx_mem_alloc(sizeof(*i));
    if(i){ok=dwc_parse(f,i,NULL,pd);dwc_info_free(i);xx_mem_free(i);}return ok;
}
bool xx_dwc_handle_base_info(Abstractformat *f,xx_pd_struct *pd)
{
    dwc_info *i=NULL;xx_dwc *a=(xx_dwc *)f;bool ok=false;
    if(f && dwc_budget(f,NULL,XX_META_ID_OPT_MEMORY_LIMIT)>=DWC_OVERHEAD && !(pd && xx_pd_is_stopped(pd)))i=(dwc_info *)xx_mem_alloc(sizeof(*i));
    if(i && dwc_parse(f,i,NULL,pd)) {a->number_of_records=i->count;a->archive_end=f->base_address+(int64_t)i->input_size;f->format_size=(int64_t)i->input_size;f->number_of_archive_records=i->count;f->overlay_offset=-1;f->overlay_size=0;f->is_valid=true;f->base_info_handled=true;ok=true;}
    if(i){dwc_info_free(i);xx_mem_free(i);}
    if(!ok && f){f->is_valid=false;f->base_info_handled=false;f->format_size=-1;f->number_of_archive_records=0U;}return ok;
}
int64_t xx_dwc_get_format_size(Abstractformat *f,xx_pd_struct *pd)
{return f && (f->base_info_handled||xx_dwc_handle_base_info(f,pd))?f->format_size:-1;}
uint64_t xx_dwc_get_number_of_archive_records(Abstractformat *f,xx_pd_struct *pd)
{return f && (f->base_info_handled||xx_dwc_handle_base_info(f,pd))?f->number_of_archive_records:0U;}
static void dwc_cursor_free(void *p){dwc_cursor *c=(dwc_cursor *)p;if(c){dwc_info_free(&c->info);xx_mem_free(c);}}
xx_archive_record_state *xx_dwc_create_archive_records_reading(Abstractformat *f,const xx_list_s *options,xx_pd_struct *pd)
{
    dwc_cursor *cursor;xx_archive_record_state *s;
    if(!f || !dwc_options_valid(options) || dwc_budget(f,options,XX_META_ID_OPT_MEMORY_LIMIT)<DWC_OVERHEAD || (pd && xx_pd_is_stopped(pd)))return NULL;
    cursor=(dwc_cursor *)xx_mem_alloc(sizeof(*cursor));
    if(!cursor)return NULL;
    xx_rt_memset(cursor,0,sizeof(*cursor));
    if(!dwc_parse(f,&cursor->info,options,pd)||!dwc_ready(f,&cursor->info,options,pd)){dwc_cursor_free(cursor);return NULL;}
    s=(xx_archive_record_state *)xx_mem_alloc(sizeof(*s));if(!s){dwc_cursor_free(cursor);return NULL;}
    cursor->entry=cursor->info.entries[0];xx_archive_record_state_init(s,f);s->internal_state=cursor;s->free_internal=dwc_cursor_free;s->total_records=(int64_t)cursor->info.count;
    if(!dwc_copy_options(&s->options,options)||!dwc_record(f,s,cursor)){xx_archive_record_state_free(s);return NULL;}s->current_index=0;s->has_record=true;return s;
}
const xx_archive_record *xx_dwc_get_current_archive_record(Abstractformat *f,xx_archive_record_state *s)
{return f&&s&&s->format==f&&s->has_record?&s->current_record:NULL;}
bool xx_dwc_archive_record_move_to_next(Abstractformat *f,xx_archive_record_state *s,xx_pd_struct *pd)
{
    dwc_cursor *c;if(!f||!s||s->format!=f||!s->has_record||!(c=(dwc_cursor *)s->internal_state)||!dwc_ready(f,&c->info,&s->options,pd))return false;
    ++s->current_index;s->has_record=false;xx_archive_record_cleanup(&s->current_record);xx_archive_record_init(&s->current_record);
    if(s->current_index>=(int64_t)c->info.count)return false;
    c->entry=c->info.entries[(size_t)s->current_index];
    if(!dwc_record(f,s,c))return false;
    s->has_record=true;return true;
}
bool xx_dwc_unpack_current_archive_record(Abstractformat *f, xx_archive_record_state *s, xx_pd_struct *pd)
{
    const dwc_cursor *cursor; const xx_var *option, *overwrite;
    uint8_t *plain = NULL; char *base = NULL, *path = NULL; xx_io_device *out = NULL;
    size_t done = 0U; bool ok = false, created = false;
    if (!f || !s || s->format != f || !s->has_record || !(cursor = (const dwc_cursor *)s->internal_state) || !dwc_decode(f, cursor, &s->options, &plain, pd)) return false;
    option = dwc_option(&s->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!option) { xx_mem_free(plain); return true; }
    base = dwc_owned_path(option);
    if (!base || !dwc_ready(f, &cursor->info, &s->options, pd)) goto done;
    path = base[0] && base[xx_str_len(base) - 1U] != '/' && base[xx_str_len(base) - 1U] != '\\' ? xx_str_concat3(base, "/", cursor->entry.name) : xx_str_concat(base, cursor->entry.name);
    overwrite = dwc_option(&s->options, XX_META_ID_OPT_OVERWRITE);
    if (!overwrite) overwrite = xx_format_resolve_extra_parameter(f, NULL, XX_META_ID_OPT_OVERWRITE);
    if (!path || (xx_io_file_exists_a(path) && (!overwrite || !xx_var_get_bool(overwrite))) || !dwc_ready(f, &cursor->info, &s->options, pd) ||
        !xx_store_create_dirs_a(path, false) || !dwc_ready(f, &cursor->info, &s->options, pd)) goto done;
    out = xx_io_file_open(path, "wb"); created = out != NULL; ok = created;
    while (ok && done < cursor->entry.raw) {
        size_t step = cursor->entry.raw - done; ssize_t sent;
        if (!dwc_ready(f, &cursor->info, &s->options, pd)) { ok = false; break; }
        if (step > 65536U) step = 65536U;
        sent = xx_io_write(out, plain + done, step);
        if (sent <= 0 || (size_t)sent > step) ok = false; else done += (size_t)sent;
    }
    if (!dwc_ready(f, &cursor->info, &s->options, pd)) ok = false;
    if (out && xx_io_close(out) != 0) ok = false;
    out = NULL;
done:
    if (out) xx_io_close(out);
    if (!ok && created) xx_rt_remove(path);
    xx_mem_free(plain); xx_str_free(base); xx_str_free(path); return ok;
}
void xx_dwc_free_archive_records_reading(Abstractformat *f, xx_archive_record_state *s)
{ (void)f; xx_archive_record_state_free(s); }
