/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT
 * Bounded Delphi Package Collection framing with complete zlib streams.
 * Output aliases follow a strict flat-ASCII File0..FileN metadata policy.
 * Package installation, OEM names and producer provenance are not inferred.
 */
#include "xxfclib/formats/archive/dpc/xx_dpc.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/global/xx_global.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/strings/xx_string.h"
#include <stdio.h>

#define DPC_MAX_SOURCE (UINT64_C(64) * 1024U * 1024U)
#define DPC_MAX_MEMBER (UINT64_C(16) * 1024U * 1024U)
#define DPC_MAX_TOTAL (UINT64_C(64) * 1024U * 1024U)
#define DPC_MAX_RECORDS 256U
#define DPC_MAX_MAP 65536U
#define DPC_OVERHEAD UINT64_C(262144)
#define DPC_OPTION_BYTES 8192U
typedef struct dpc_entry { size_t header, header_end, payload, end, packed, raw; char name[256]; } dpc_entry;
typedef struct dpc_info { size_t input_size, count, max_raw; uint64_t total_raw; uint8_t *input; dpc_entry entries[DPC_MAX_RECORDS]; } dpc_info;
typedef struct dpc_cursor { dpc_info info; dpc_entry entry; } dpc_cursor;
static uint64_t dpc_budget(Abstractformat *, const xx_list_s *, uint32_t);
static bool dpc_options_valid(const xx_list_s *);
static bool dpc_ready(Abstractformat *, const dpc_info *, const xx_list_s *, xx_pd_struct *);
static uint32_t dpc_u32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1]<<8U) | ((uint32_t)p[2]<<16U) | ((uint32_t)p[3]<<24U); }
static bool dpc_read_at(Abstractformat *f, int64_t at, void *buffer, size_t n, xx_pd_struct *pd)
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
static char dpc_lower(char c) { return c>='A' && c<='Z' ? (char)(c+('a'-'A')) : c; }
static bool dpc_safe_name(char *name, const uint8_t *field, size_t n)
{
    size_t stem=0U; char lower[256];
    if (!n || n>255U) return false;
    for (size_t i=0U;i<n;++i) {
        unsigned c=field[i];
        if (c<=32U || c>=127U || c=='/' || c=='\\' || c==':' || c=='<' || c=='>' || c=='"' || c=='|' || c=='?' || c=='*') return false;
        name[i]=(char)c;lower[i]=dpc_lower((char)c);
    }
    name[n]=lower[n]=0;
    if (name[n-1U]=='.') return false;
    while (stem<n && lower[stem]!='.') ++stem;
    if (stem==3U && (!xx_rt_memcmp(lower,"con",3U)||!xx_rt_memcmp(lower,"prn",3U)||!xx_rt_memcmp(lower,"aux",3U)||!xx_rt_memcmp(lower,"nul",3U))) return false;
    if (stem==4U && (!xx_rt_memcmp(lower,"com",3U)||!xx_rt_memcmp(lower,"lpt",3U)) && lower[3]>='1' && lower[3]<='9') return false;
    return true;
}
/* Alias-only portable output policy; retain the original metadata bytes. */
static bool dpc_alias_name(char *name, const uint8_t *field, size_t n)
{
    while (n && field[n-1U]==' ') --n;
    return dpc_safe_name(name,field,n);
}

typedef struct dpc_sink { uint8_t *output; size_t capacity,position; xx_pd_struct *pd; } dpc_sink;
static ssize_t dpc_sink_write(xx_io_device *device, const void *buffer, size_t n)
{
    dpc_sink *s=(dpc_sink *)device->priv;
    if (!s || (s->pd && xx_pd_is_stopped(s->pd)) || s->position>s->capacity || n>s->capacity-s->position) return -1;
    if (n) xx_rt_memcpy(s->output+s->position,buffer,n);
    s->position+=n; return (ssize_t)n;
}
/* Unlike the permissive generic zlib wrapper, require the whole 32KiB-window
 * stream, exact raw count, final Deflate byte and matching Adler trailer. */
static bool dpc_inflate(const uint8_t *input,size_t packed,uint8_t *plain,size_t raw,xx_pd_struct *pd)
{
    xx_io_device out; dpc_sink sink; size_t consumed=0U;
    if (!input || !plain || packed<8U || input[0]!=0x78U || !xx_zlib_stream_header_is_valid(input,packed) || (pd && xx_pd_is_stopped(pd))) return false;
    xx_rt_memset(&out,0,sizeof(out));sink.output=plain;sink.capacity=raw;sink.position=0U;sink.pd=pd;out.priv=&sink;out.write=dpc_sink_write;
    return xx_deflate_unpack_memory_to_device_ex(input+2U,packed-6U,&out,&consumed,false,pd) && consumed==packed-6U && sink.position==raw &&
        !(pd && xx_pd_is_stopped(pd)) && xx_zlib_stream_trailer_matches(input,packed,plain,raw) && !(pd && xx_pd_is_stopped(pd));
}
static bool dpc_alias_pass(dpc_info *info,const uint8_t *map,size_t n,xx_pd_struct *pd,bool apply,size_t *aliases)
{
    size_t at=0U,ordinal=0U;bool section=false;
    while (at<n) {
        size_t begin=at,len,equal;char key[32],scratch[256];char *name;int keylen;
        if (pd && xx_pd_is_stopped(pd)) return false;
        while (at<n && map[at]!='\r' && map[at]!='\n') ++at;
        len=at-begin;
        if (at<n && map[at]=='\r') ++at;
        if (at<n && map[at]=='\n') ++at;
        if (len>1024U) return false;
        if (!section) {
            if (len==7U && !xx_rt_memcmp(map+begin,"[Files]",7U)) section=true;
            continue;
        }
        if (len<4U || xx_rt_memcmp(map+begin,"File",4U)) break;
        if (ordinal>=info->count || (apply && ordinal+1U>=info->count)) return false;
        name=apply ? info->entries[ordinal+1U].name : scratch;
        equal=0U;while (equal<len && map[begin+equal]!='=') ++equal;
        keylen=snprintf(key,sizeof(key),"File%u",(unsigned)ordinal);
        if (keylen<0 || (size_t)keylen!=equal || equal==len || xx_rt_memcmp(map+begin,key,equal) ||
            !dpc_alias_name(name,map+begin+equal+1U,len-equal-1U)) return false;
        ++ordinal;
    }
    if (!section) return false;
    *aliases=ordinal;return true;
}

static bool dpc_aliases(dpc_info *info,const uint8_t *map,size_t n,xx_pd_struct *pd)
{
    size_t aliases=0U,applied=0U;
    for (size_t i=0U;i<n;++i) if (!map[i] || map[i]==127U || (map[i]<32U && map[i]!='\r' && map[i]!='\n' && map[i]!='\t')) return false;
    /* Validate every safe sequential alias before changing catalog names.
     * Exactly one excess alias is ambiguous: preserve all catalog names and
     * original info.dat bytes without assigning, shifting or dropping aliases. */
    if (!dpc_alias_pass(info,map,n,pd,false,&aliases) ||
        (aliases+1U!=info->count && aliases!=info->count)) return false;
    if (aliases==info->count) return true;
    if (!dpc_alias_pass(info,map,n,pd,true,&applied) || applied!=aliases) return false;
    /* Preserve every record with an archive-only, deterministic output name.
     * Compare only finalized earlier names, never future catalog placeholders.
     * Every retry prefixes the original safe alias, not the previous retry. */
    for (size_t i=1U;i<info->count;++i) {
        char alias[256];unsigned attempt=0U;
        xx_rt_memcpy(alias,info->entries[i].name,sizeof(alias));
        for (;;) {
            bool collision=false;
            for (size_t j=0U;j<i;++j) {
                size_t k=0U;
                if (pd && xx_pd_is_stopped(pd)) return false;
                while (dpc_lower(info->entries[i].name[k])==dpc_lower(info->entries[j].name[k])) {
                    if (!info->entries[i].name[k]) { collision=true;break; }
                    ++k;
                }
                if (collision) break;
            }
            if (!collision) break;
            if (attempt>=info->count) return false;
            int n=snprintf(info->entries[i].name,sizeof(info->entries[i].name),"(%u)%s",attempt++,alias);
            if (n<0 || (size_t)n>=sizeof(info->entries[i].name)) return false;
        }
    }
    return true;
}
static void dpc_info_free(dpc_info *info) { if(info) {xx_mem_free(info->input);info->input=NULL;} }
static bool dpc_parse(Abstractformat *f,dpc_info *info,const xx_list_s *options,xx_pd_struct *pd)
{
    int64_t total;uint64_t memory;size_t at,expected=36U;uint8_t h[36],*map=NULL;bool ok=false;
    if (!info) return false;
    xx_rt_memset(info,0,sizeof(*info));
    if (!f || !f->device || f->base_address<0 || (pd && xx_pd_is_stopped(pd)) || !dpc_options_valid(options)) return false;
    total=xx_io_total_size(f->device);memory=dpc_budget(f,options,XX_META_ID_OPT_MEMORY_LIMIT);
    if (total<f->base_address || total-f->base_address<36 || (uint64_t)(total-f->base_address)>DPC_MAX_SOURCE) return false;
    info->input_size=(size_t)(total-f->base_address);
    if (memory<DPC_OVERHEAD || info->input_size>memory-DPC_OVERHEAD || !dpc_read_at(f,f->base_address,h,sizeof(h),pd) ||
        xx_rt_memcmp(h,"Delphi Package Collection\r\n\x1a",28U)) return false;
    info->count=dpc_u32(h+28U);at=dpc_u32(h+32U);
    if (info->count<2U || info->count>DPC_MAX_RECORDS || at<=36U || at>=info->input_size) return false;
    info->input=(uint8_t *)xx_mem_alloc(info->input_size);
    if (!info->input || !dpc_read_at(f,f->base_address,info->input,info->input_size,pd) || xx_rt_memcmp(h,info->input,sizeof(h))) goto done;
    for (size_t i=0U;i<info->count;++i) {
        dpc_entry *e=&info->entries[i];size_t len;
        if (pd && xx_pd_is_stopped(pd)) goto done;
        if (at>=info->input_size) goto done;
        e->header=at;len=info->input[at++];
        if (!len || len>info->input_size-at || info->input_size-at-len<16U || !dpc_safe_name(e->name,info->input+at,len)) goto done;
        for(size_t j=0U;j<i;++j) {
            size_t k=0U;
            while(dpc_lower(e->name[k])==dpc_lower(info->entries[j].name[k])) {
                if(!e->name[k])goto done;
                ++k;
            }
        }
        at+=len;e->payload=dpc_u32(info->input+at);e->raw=dpc_u32(info->input+at+4U);e->packed=dpc_u32(info->input+at+8U);at+=16U;e->header_end=at;
        if (e->payload!=expected || e->packed<8U || e->packed>DPC_MAX_MEMBER || (i==0U && !e->raw) || e->raw>DPC_MAX_MEMBER || e->packed>dpc_u32(h+32U)-expected) goto done;
        e->end=e->payload+e->packed;expected=e->end;
        if (info->input[e->payload]!=0x78U || !xx_zlib_stream_header_is_valid(info->input+e->payload,e->packed)) goto done;
        info->total_raw+=e->raw;if(info->total_raw>DPC_MAX_TOTAL) goto done;
        if(e->raw>info->max_raw)info->max_raw=e->raw;
    }
    if (at!=info->input_size || expected!=dpc_u32(h+32U) || xx_str_cmp(info->entries[0].name,"info.dat") || info->entries[0].raw>DPC_MAX_MAP || !dpc_ready(f,info,options,pd)) goto done;
    map=(uint8_t *)xx_mem_alloc(info->entries[0].raw);
    if (!map || !dpc_inflate(info->input+info->entries[0].payload,info->entries[0].packed,map,info->entries[0].raw,pd) || !dpc_aliases(info,map,info->entries[0].raw,pd) || !dpc_ready(f,info,options,pd)) goto done;
    ok=true;
done:
    xx_mem_free(map);if(!ok)dpc_info_free(info);return ok;
}
static bool dpc_unchanged(Abstractformat *f,const dpc_info *info,xx_pd_struct *pd)
{
    uint8_t *part;size_t at=0U;bool ok=true;int64_t total=xx_io_total_size(f->device);
    if(total<f->base_address || (uint64_t)(total-f->base_address)!=info->input_size)return false;
    part=(uint8_t *)xx_mem_alloc(65536U);if(!part)return false;
    while(at<info->input_size) {
        size_t n=info->input_size-at;if(n>65536U)n=65536U;
        if(!dpc_read_at(f,f->base_address+(int64_t)at,part,n,pd)||xx_rt_memcmp(part,info->input+at,n)){ok=false;break;}
        at+=n;
    }
    xx_mem_free(part);return ok;
}
static const xx_var *dpc_option(const xx_list_s *options, uint32_t id)
{
    size_t i;
    if (!options) return NULL;
    for (i = 0U; i < options->count; ++i) {
        const xx_meta *m = (const xx_meta *)xx_list_at((const xx_list_t *)options, i);
        if (m && m->meta_id == id) return &m->var;
    }
    return NULL;
}

static uint64_t dpc_budget(Abstractformat *f, const xx_list_s *options, uint32_t id)
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

static bool dpc_options_valid(const xx_list_s *options)
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
            if ((!m->var.val.str.ptr && n) || bytes >= DPC_OPTION_BYTES || n >= DPC_OPTION_BYTES - bytes) return false;
            bytes += n + 1U;
        } else if (m->var.type == XX_VAR_TYPE_WSTRING || m->var.type == XX_VAR_TYPE_WSTRING_VIEW) {
            n = m->var.val.wstr.len;
            if ((!m->var.val.wstr.ptr && n) || (DPC_OPTION_BYTES - bytes) / sizeof(wchar_t) == 0U ||
                n >= (DPC_OPTION_BYTES - bytes) / sizeof(wchar_t)) return false;
            bytes += (n + 1U) * sizeof(wchar_t);
        } else if (m->var.type >= XX_VAR_TYPE_STRING) return false;
    }
    return true;
}

static bool dpc_ready(Abstractformat *f, const dpc_info *info, const xx_list_s *options, xx_pd_struct *pd)
{
    return !(pd && xx_pd_is_stopped(pd)) && dpc_options_valid(options) &&
        (uint64_t)info->max_raw <= dpc_budget(f, options, XX_META_ID_OPT_MAX_MEMBER_SIZE) &&
        (uint64_t)info->input_size + info->max_raw + DPC_OVERHEAD <= dpc_budget(f, options, XX_META_ID_OPT_MEMORY_LIMIT);
}

/* Bounded owned options keep the 256KiB overhead charge conservative even
 * during wide-path conversion. Views are cloned, never borrowed. */
static bool dpc_copy_options(xx_list_s *to, const xx_list_s *from)
{
    size_t i, bytes = 0U;
    if (!from) return true;
    if (!dpc_options_valid(from)) return false;
    for (i = 0U; i < from->count; ++i) {
        const xx_meta *m = (const xx_meta *)xx_list_at((const xx_list_t *)from, i);
        xx_meta copy;
        bool ok = false;
        if (!m) return false;
        xx_meta_init(&copy, m->meta_id);
        if (m->var.type == XX_VAR_TYPE_STRING || m->var.type == XX_VAR_TYPE_STRING_VIEW) {
            size_t n = m->var.val.str.len;
            char *s;
            if (bytes >= DPC_OPTION_BYTES || n >= DPC_OPTION_BYTES - bytes) return false;
            bytes += n + 1U;
            s = (char *)xx_mem_alloc(n + 1U);
            if (s) { if (n) xx_rt_memcpy(s, m->var.val.str.ptr, n); s[n] = 0; ok = xx_var_set_str_take(&copy.var, s, n); if (!ok) xx_mem_free(s); }
        } else if (m->var.type == XX_VAR_TYPE_WSTRING || m->var.type == XX_VAR_TYPE_WSTRING_VIEW) {
            size_t n = m->var.val.wstr.len;
            wchar_t *s;
            if ((DPC_OPTION_BYTES - bytes) / sizeof(wchar_t) == 0U ||
                n >= (DPC_OPTION_BYTES - bytes) / sizeof(wchar_t)) return false;
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
static char *dpc_owned_path(const xx_var *v)
{
    size_t n;
    if (!v) return NULL;
    if (v->type == XX_VAR_TYPE_STRING || v->type == XX_VAR_TYPE_STRING_VIEW) {
        char *s;
        n = v->val.str.len;
        if (n >= DPC_OPTION_BYTES || (!v->val.str.ptr && n)) return NULL;
        s = (char *)xx_mem_alloc(n + 1U);
        if (s) { if (n) xx_rt_memcpy(s, v->val.str.ptr, n); s[n] = 0; }
        return s;
    }
    if (v->type == XX_VAR_TYPE_WSTRING || v->type == XX_VAR_TYPE_WSTRING_VIEW) {
        wchar_t *wide;
        char *s;
        n = v->val.wstr.len;
        if (n >= DPC_OPTION_BYTES / sizeof(wchar_t) || (!v->val.wstr.ptr && n)) return NULL;
        wide = (wchar_t *)xx_mem_alloc((n + 1U) * sizeof(wchar_t));
        if (!wide) return NULL;
        if (n) xx_rt_memcpy(wide, v->val.wstr.ptr, n * sizeof(wchar_t));
        wide[n] = 0; s = xx_str_unicode_to_utf8(wide); xx_mem_free(wide); return s;
    }
    return NULL;
}


static bool dpc_decode(Abstractformat *f,const dpc_cursor *cursor,const xx_list_s *options,uint8_t **result,xx_pd_struct *pd)
{
    const dpc_entry *e=&cursor->entry;uint8_t *plain;*result=NULL;
    if(!dpc_ready(f,&cursor->info,options,pd)||!dpc_unchanged(f,&cursor->info,pd))return false;
    plain=(uint8_t *)xx_mem_alloc(e->raw ? e->raw : 1U);if(!plain)return false;
    if(!dpc_ready(f,&cursor->info,options,pd)||!dpc_inflate(cursor->info.input+e->payload,e->packed,plain,e->raw,pd)||
       !dpc_ready(f,&cursor->info,options,pd)||!dpc_unchanged(f,&cursor->info,pd)){xx_mem_free(plain);return false;}
    *result=plain;return true;
}
static bool dpc_record(Abstractformat *f,xx_archive_record_state *s,dpc_cursor *cursor)
{
    xx_archive_record *r=&s->current_record;const dpc_entry *e=&cursor->entry;
    r->header_offset=f->base_address+(int64_t)e->header;r->header_size=(int64_t)(e->header_end-e->header);
    r->data_offset=f->base_address+(int64_t)e->payload;r->compressed_size=(int64_t)e->packed;
    return xx_archive_record_set_original_name(r,e->name)&&xx_archive_record_set_meta_u64(r,XX_META_ID_COMPRESSED_SIZE,e->packed)&&
       xx_archive_record_set_meta_u64(r,XX_META_ID_UNCOMPRESSED_SIZE,e->raw)&&xx_archive_record_set_meta_str(r,XX_META_ID_COMPRESSION_METHOD,"zlib (complete stream, Adler-32)")&&
       xx_archive_record_set_meta_bool(r,XX_META_ID_IS_ENCRYPTED,false)&&xx_archive_record_set_meta_bool(r,XX_META_ID_IS_FOLDER,false);
}
void xx_dpc_init(xx_dpc *a, xx_io_device *device, int64_t base)
{
    if (!a) return;
    xx_rt_memset(a, 0, sizeof(*a)); xx_format_init(&a->format, device, base);
    a->format.endian = XX_ENDIAN_LITTLE; a->format.file_type = XX_FILE_TYPE_DPC;
    a->format.format_type = XX_TYPE_ARCHIVE; a->format.is_archive = true;
    xx_format_set_mime_type(&a->format, "application/x-dpc"); xx_format_set_extension(&a->format, "dpc");
    a->format.check_is_valid = xx_dpc_check_is_valid;
    a->format.handle_base_info = xx_dpc_handle_base_info;
    a->format.get_format_size = xx_dpc_get_format_size;
    a->format.get_number_of_archive_records = xx_dpc_get_number_of_archive_records;
    a->format.create_archive_records_reading = xx_dpc_create_archive_records_reading;
    a->format.get_current_archive_record = xx_dpc_get_current_archive_record;
    a->format.unpack_current_archive_record = xx_dpc_unpack_current_archive_record;
    a->format.archive_record_move_to_next = xx_dpc_archive_record_move_to_next;
    a->format.free_archive_records_reading = xx_dpc_free_archive_records_reading;
    a->archive_end = -1;
}
xx_dpc *xx_dpc_create(xx_io_device *device, int64_t base)
{ xx_dpc *a = (xx_dpc *)xx_mem_alloc(sizeof(*a)); if (a) xx_dpc_init(a, device, base); return a; }
void xx_dpc_destroy(xx_dpc *a) { if (a) xx_format_cleanup_extra_parameters(&a->format); }
void xx_dpc_free(xx_dpc *a) { if (a) { xx_dpc_destroy(a); xx_mem_free(a); } }
bool xx_dpc_check_is_valid(Abstractformat *f,xx_pd_struct *pd)
{
    dpc_info *i;bool ok=false;
    if(!f || dpc_budget(f,NULL,XX_META_ID_OPT_MEMORY_LIMIT)<DPC_OVERHEAD || (pd && xx_pd_is_stopped(pd)))return false;
    i=(dpc_info *)xx_mem_alloc(sizeof(*i));
    if(i){ok=dpc_parse(f,i,NULL,pd);dpc_info_free(i);xx_mem_free(i);}return ok;
}
bool xx_dpc_handle_base_info(Abstractformat *f,xx_pd_struct *pd)
{
    dpc_info *i=NULL;xx_dpc *a=(xx_dpc *)f;bool ok=false;
    if(f && dpc_budget(f,NULL,XX_META_ID_OPT_MEMORY_LIMIT)>=DPC_OVERHEAD && !(pd && xx_pd_is_stopped(pd)))i=(dpc_info *)xx_mem_alloc(sizeof(*i));
    if(i && dpc_parse(f,i,NULL,pd)) {a->number_of_records=i->count;a->archive_end=f->base_address+(int64_t)i->input_size;f->format_size=(int64_t)i->input_size;f->number_of_archive_records=i->count;f->overlay_offset=-1;f->overlay_size=0;f->is_valid=true;f->base_info_handled=true;ok=true;}
    if(i){dpc_info_free(i);xx_mem_free(i);}
    if(!ok && f){f->is_valid=false;f->base_info_handled=false;f->format_size=-1;f->number_of_archive_records=0U;}return ok;
}
int64_t xx_dpc_get_format_size(Abstractformat *f,xx_pd_struct *pd)
{return f && (f->base_info_handled||xx_dpc_handle_base_info(f,pd))?f->format_size:-1;}
uint64_t xx_dpc_get_number_of_archive_records(Abstractformat *f,xx_pd_struct *pd)
{return f && (f->base_info_handled||xx_dpc_handle_base_info(f,pd))?f->number_of_archive_records:0U;}
static void dpc_cursor_free(void *p){dpc_cursor *c=(dpc_cursor *)p;if(c){dpc_info_free(&c->info);xx_mem_free(c);}}
xx_archive_record_state *xx_dpc_create_archive_records_reading(Abstractformat *f,const xx_list_s *options,xx_pd_struct *pd)
{
    dpc_cursor *cursor;xx_archive_record_state *s;
    if(!f || !dpc_options_valid(options) || dpc_budget(f,options,XX_META_ID_OPT_MEMORY_LIMIT)<DPC_OVERHEAD || (pd && xx_pd_is_stopped(pd)))return NULL;
    cursor=(dpc_cursor *)xx_mem_alloc(sizeof(*cursor));
    if(!cursor)return NULL;
    xx_rt_memset(cursor,0,sizeof(*cursor));
    if(!dpc_parse(f,&cursor->info,options,pd)||!dpc_ready(f,&cursor->info,options,pd)){dpc_cursor_free(cursor);return NULL;}
    s=(xx_archive_record_state *)xx_mem_alloc(sizeof(*s));if(!s){dpc_cursor_free(cursor);return NULL;}
    cursor->entry=cursor->info.entries[0];xx_archive_record_state_init(s,f);s->internal_state=cursor;s->free_internal=dpc_cursor_free;s->total_records=(int64_t)cursor->info.count;
    if(!dpc_copy_options(&s->options,options)||!dpc_record(f,s,cursor)){xx_archive_record_state_free(s);return NULL;}s->current_index=0;s->has_record=true;return s;
}
const xx_archive_record *xx_dpc_get_current_archive_record(Abstractformat *f,xx_archive_record_state *s)
{return f&&s&&s->format==f&&s->has_record?&s->current_record:NULL;}
bool xx_dpc_archive_record_move_to_next(Abstractformat *f,xx_archive_record_state *s,xx_pd_struct *pd)
{
    dpc_cursor *c;if(!f||!s||s->format!=f||!s->has_record||!(c=(dpc_cursor *)s->internal_state)||!dpc_ready(f,&c->info,&s->options,pd))return false;
    ++s->current_index;s->has_record=false;xx_archive_record_cleanup(&s->current_record);xx_archive_record_init(&s->current_record);
    if(s->current_index>=(int64_t)c->info.count)return false;
    c->entry=c->info.entries[(size_t)s->current_index];
    if(!dpc_record(f,s,c))return false;
    s->has_record=true;return true;
}
bool xx_dpc_unpack_current_archive_record(Abstractformat *f, xx_archive_record_state *s, xx_pd_struct *pd)
{
    const dpc_cursor *cursor; const xx_var *option, *overwrite;
    uint8_t *plain = NULL; char *base = NULL, *path = NULL; xx_io_device *out = NULL;
    size_t done = 0U; bool ok = false, created = false;
    if (!f || !s || s->format != f || !s->has_record || !(cursor = (const dpc_cursor *)s->internal_state) || !dpc_decode(f, cursor, &s->options, &plain, pd)) return false;
    option = dpc_option(&s->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!option) { xx_mem_free(plain); return true; }
    base = dpc_owned_path(option);
    if (!base || !dpc_ready(f, &cursor->info, &s->options, pd)) goto done;
    path = base[0] && base[xx_str_len(base) - 1U] != '/' && base[xx_str_len(base) - 1U] != '\\' ? xx_str_concat3(base, "/", cursor->entry.name) : xx_str_concat(base, cursor->entry.name);
    overwrite = dpc_option(&s->options, XX_META_ID_OPT_OVERWRITE);
    if (!overwrite) overwrite = xx_format_resolve_extra_parameter(f, NULL, XX_META_ID_OPT_OVERWRITE);
    if (!path || (xx_io_file_exists_a(path) && (!overwrite || !xx_var_get_bool(overwrite))) || !dpc_ready(f, &cursor->info, &s->options, pd) ||
        !xx_store_create_dirs_a(path, false) || !dpc_ready(f, &cursor->info, &s->options, pd)) goto done;
    out = xx_io_file_open(path, "wb"); created = out != NULL; ok = created;
    while (ok && done < cursor->entry.raw) {
        size_t step = cursor->entry.raw - done; ssize_t sent;
        if (!dpc_ready(f, &cursor->info, &s->options, pd)) { ok = false; break; }
        if (step > 65536U) step = 65536U;
        sent = xx_io_write(out, plain + done, step);
        if (sent <= 0 || (size_t)sent > step) ok = false; else done += (size_t)sent;
    }
    if (!dpc_ready(f, &cursor->info, &s->options, pd)) ok = false;
    if (out && xx_io_close(out) != 0) ok = false;
    out = NULL;
done:
    if (out) xx_io_close(out);
    if (!ok && created) xx_rt_remove(path);
    xx_mem_free(plain); xx_str_free(base); xx_str_free(path); return ok;
}
void xx_dpc_free_archive_records_reading(Abstractformat *f, xx_archive_record_state *s)
{ (void)f; xx_archive_record_state_free(s); }
