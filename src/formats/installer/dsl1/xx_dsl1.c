/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT
 * Bounded Img3 DSL1 profile: stored or complete standalone PKWARE DCL.
 * Safe ASCII catalog components are flattened to portable underscore names.
 * Header/title, trailing record bytes and the exact-size trailer stay opaque;
 * installer actions, producer provenance and checksum authority are not inferred.
 */
#include "xxfclib/formats/installer/dsl1/xx_dsl1.h"
#include "xxfclib/algo/dcl/xx_dcl.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/global/xx_global.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/strings/xx_string.h"
#include <stdio.h>

#define DSL1_MAX_SOURCE (UINT64_C(64) * 1024U * 1024U)
#define DSL1_MAX_MEMBER (UINT64_C(16) * 1024U * 1024U)
#define DSL1_MAX_TOTAL (UINT64_C(64) * 1024U * 1024U)
#define DSL1_MAX_RECORDS 256U
#define DSL1_OVERHEAD UINT64_C(262144)
#define DSL1_OPTION_BYTES 8192U
typedef struct dsl1_entry { size_t header, header_end, payload, end, packed, raw; uint8_t method; char name[256]; } dsl1_entry;
typedef struct dsl1_info { size_t input_size, count, max_raw; uint64_t total_raw; uint8_t *input; dsl1_entry entries[DSL1_MAX_RECORDS]; } dsl1_info;
typedef struct dsl1_cursor { dsl1_info info; dsl1_entry entry; } dsl1_cursor;
static uint64_t dsl1_budget(Abstractformat *, const xx_list_s *, uint32_t);
static bool dsl1_options_valid(const xx_list_s *);
static bool dsl1_ready(Abstractformat *, const dsl1_info *, const xx_list_s *, xx_pd_struct *);
static uint16_t dsl1_u16(const uint8_t *p) { return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1]<<8U)); }
static uint32_t dsl1_u32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1]<<8U) | ((uint32_t)p[2]<<16U) | ((uint32_t)p[3]<<24U); }
static bool dsl1_read_at(Abstractformat *f, int64_t at, void *buffer, size_t n, xx_pd_struct *pd)
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
static char dsl1_lower(char c) { return c>='A' && c<='Z' ? (char)(c+('a'-'A')) : c; }
static bool dsl1_safe_component(char *name, const uint8_t *field, size_t n)
{
    size_t stem=0U; char lower[256];
    if (!n || n>255U) return false;
    for (size_t i=0U;i<n;++i) {
        unsigned c=field[i];
        if (c<=32U || c>=127U || c=='/' || c=='\\' || c==':' || c=='<' || c=='>' || c=='"' || c=='|' || c=='?' || c=='*') return false;
        name[i]=(char)c;lower[i]=dsl1_lower((char)c);
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
/* Validate each original component before applying this reader's explicit
 * separator-to-underscore output policy; no filesystem state or directory
 * attributes are consulted. The whole owned input preserves the raw names. */
static bool dsl1_safe_name(char *name, const uint8_t *field, size_t n)
{
    size_t start=0U;char component[256];
    if (!n || n>255U) return false;
    for (size_t i=0U;i<=n;++i) {
        if (i==n || field[i]=='/' || field[i]=='\\') {
            if (!dsl1_safe_component(component,field+start,i-start)) return false;
            start=i+1U;
        }
        if (i<n) name[i]=(field[i]=='/' || field[i]=='\\')?'_':(char)field[i];
    }
    name[n]=0;
    return dsl1_safe_component(component,(const uint8_t *)name,n);
}
/* The strict shared scanner must reach DCL END and occupy exactly the
 * declared bytes before the same shared parser emits the exact raw count.
 * Codec passes are bounded by 16MiB input/output, with reader cancellation
 * checked before, between and after them; this does not claim an inner poll. */
static bool dsl1_payload_decode(const uint8_t *input,size_t packed,uint8_t *plain,size_t raw,uint8_t method,xx_pd_struct *pd)
{
    size_t consumed=0U,produced=0U,written=0U;
    if (!input || !plain || (pd && xx_pd_is_stopped(pd))) return false;
    if (method==0U) {
        if (packed!=raw) return false;
        xx_rt_memcpy(plain,input,raw);
        return !(pd && xx_pd_is_stopped(pd));
    }
    if (!raw || method!=2U || !xx_dcl_scan_memory(input,packed,raw,&consumed,&produced) || consumed!=packed || produced!=raw ||
        (pd && xx_pd_is_stopped(pd))) return false;
    return xx_dcl_decode_memory(input,packed,plain,raw,&written) && written==raw && !(pd && xx_pd_is_stopped(pd));
}
static void dsl1_info_free(dsl1_info *info) { if(info) {xx_mem_free(info->input);info->input=NULL;} }
static bool dsl1_parse(Abstractformat *f,dsl1_info *info,const xx_list_s *options,xx_pd_struct *pd)
{
    int64_t total;uint64_t memory;size_t table,expected=32U;uint8_t h[32];bool ok=false;
    if (!info) return false;
    xx_rt_memset(info,0,sizeof(*info));
    if (!f || !f->device || f->base_address<0 || (pd && xx_pd_is_stopped(pd)) || !dsl1_options_valid(options)) return false;
    total=xx_io_total_size(f->device);memory=dsl1_budget(f,options,XX_META_ID_OPT_MEMORY_LIMIT);
    if (total<f->base_address || total-f->base_address<128 || (uint64_t)(total-f->base_address)>DSL1_MAX_SOURCE) return false;
    info->input_size=(size_t)(total-f->base_address);
    if (memory<DSL1_OVERHEAD || info->input_size>memory-DSL1_OVERHEAD || !dsl1_read_at(f,f->base_address,h,sizeof(h),pd) ||
        xx_rt_memcmp(h,"Img3",4U)) return false;
    info->count=dsl1_u16(h+4U);table=dsl1_u32(h+6U);
    if (!info->count || info->count>DSL1_MAX_RECORDS || table<32U || table>info->input_size ||
        info->count>(info->input_size-table)/48U || info->input_size-table-info->count*48U!=48U) return false;
    info->input=(uint8_t *)xx_mem_alloc(info->input_size);
    if (!info->input || !dsl1_read_at(f,f->base_address,info->input,info->input_size,pd) || xx_rt_memcmp(h,info->input,sizeof(h))) goto done;
    /* The exact 48-byte trailer and record bytes 33..47 are preserved as
     * opaque input, without interpreting padding, attributes or actions. */
    for (size_t i=0U;i<info->count;++i) {
        dsl1_entry *e=&info->entries[i];size_t at=table+i*48U,len=0U;
        if (pd && xx_pd_is_stopped(pd)) goto done;
        while (len<16U && info->input[at+len]) ++len;
        if (!len || len>=16U || !dsl1_safe_name(e->name,info->input+at,len)) goto done;
        for (size_t k=len;k<16U;++k) if (info->input[at+k]) goto done;
        for (size_t j=0U;j<i;++j) {
            size_t k=0U;
            while (dsl1_lower(e->name[k])==dsl1_lower(info->entries[j].name[k])) {
                if (!e->name[k]) goto done;
                ++k;
            }
        }
        e->header=at;e->header_end=at+48U;e->payload=dsl1_u32(info->input+at+16U);e->packed=dsl1_u32(info->input+at+20U);
        e->method=info->input[at+28U];e->raw=dsl1_u32(info->input+at+29U);
        if (e->payload!=expected || e->packed>DSL1_MAX_MEMBER || e->raw>DSL1_MAX_MEMBER ||
            expected>table || e->packed>table-expected || (e->method!=0U && e->method!=2U)) goto done;
        if (e->method==0U && e->packed!=e->raw) goto done;
        if (e->method==2U && (!e->raw || e->packed<4U || info->input[e->payload]>1U || info->input[e->payload+1U]<4U || info->input[e->payload+1U]>6U)) goto done;
        e->end=e->payload+e->packed;expected=e->end;
        info->total_raw+=e->raw;if(info->total_raw>DSL1_MAX_TOTAL) goto done;
        if(e->raw>info->max_raw)info->max_raw=e->raw;
    }
    if (expected!=table || !dsl1_ready(f,info,options,pd)) goto done;
    ok=true;
done:
    if (!ok) dsl1_info_free(info);
    return ok;
}
static bool dsl1_unchanged(Abstractformat *f,const dsl1_info *info,xx_pd_struct *pd)
{
    uint8_t *part;size_t at=0U;bool ok=true;int64_t total=xx_io_total_size(f->device);
    if(total<f->base_address || (uint64_t)(total-f->base_address)!=info->input_size)return false;
    part=(uint8_t *)xx_mem_alloc(65536U);if(!part)return false;
    while(at<info->input_size) {
        size_t n=info->input_size-at;if(n>65536U)n=65536U;
        if(!dsl1_read_at(f,f->base_address+(int64_t)at,part,n,pd)||xx_rt_memcmp(part,info->input+at,n)){ok=false;break;}
        at+=n;
    }
    xx_mem_free(part);return ok;
}
static const xx_var *dsl1_option(const xx_list_s *options, uint32_t id)
{
    size_t i;
    if (!options) return NULL;
    for (i = 0U; i < options->count; ++i) {
        const xx_meta *m = (const xx_meta *)xx_list_at((const xx_list_t *)options, i);
        if (m && m->meta_id == id) return &m->var;
    }
    return NULL;
}

static uint64_t dsl1_budget(Abstractformat *f, const xx_list_s *options, uint32_t id)
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

static bool dsl1_options_valid(const xx_list_s *options)
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
            if ((!m->var.val.str.ptr && n) || bytes >= DSL1_OPTION_BYTES || n >= DSL1_OPTION_BYTES - bytes) return false;
            bytes += n + 1U;
        } else if (m->var.type == XX_VAR_TYPE_WSTRING || m->var.type == XX_VAR_TYPE_WSTRING_VIEW) {
            n = m->var.val.wstr.len;
            if ((!m->var.val.wstr.ptr && n) || (DSL1_OPTION_BYTES - bytes) / sizeof(wchar_t) == 0U ||
                n >= (DSL1_OPTION_BYTES - bytes) / sizeof(wchar_t)) return false;
            bytes += (n + 1U) * sizeof(wchar_t);
        } else if (m->var.type >= XX_VAR_TYPE_STRING) return false;
    }
    return true;
}

static bool dsl1_ready(Abstractformat *f, const dsl1_info *info, const xx_list_s *options, xx_pd_struct *pd)
{
    return !(pd && xx_pd_is_stopped(pd)) && dsl1_options_valid(options) &&
        (uint64_t)info->max_raw <= dsl1_budget(f, options, XX_META_ID_OPT_MAX_MEMBER_SIZE) &&
        (uint64_t)info->input_size + info->max_raw + DSL1_OVERHEAD <= dsl1_budget(f, options, XX_META_ID_OPT_MEMORY_LIMIT);
}

/* Bounded owned options keep the 256KiB overhead charge conservative even
 * during wide-path conversion. Views are cloned, never borrowed. */
static bool dsl1_copy_options(xx_list_s *to, const xx_list_s *from)
{
    size_t i, bytes = 0U;
    if (!from) return true;
    if (!dsl1_options_valid(from)) return false;
    for (i = 0U; i < from->count; ++i) {
        const xx_meta *m = (const xx_meta *)xx_list_at((const xx_list_t *)from, i);
        xx_meta copy;
        bool ok = false;
        if (!m) return false;
        xx_meta_init(&copy, m->meta_id);
        if (m->var.type == XX_VAR_TYPE_STRING || m->var.type == XX_VAR_TYPE_STRING_VIEW) {
            size_t n = m->var.val.str.len;
            char *s;
            if (bytes >= DSL1_OPTION_BYTES || n >= DSL1_OPTION_BYTES - bytes) return false;
            bytes += n + 1U;
            s = (char *)xx_mem_alloc(n + 1U);
            if (s) { if (n) xx_rt_memcpy(s, m->var.val.str.ptr, n); s[n] = 0; ok = xx_var_set_str_take(&copy.var, s, n); if (!ok) xx_mem_free(s); }
        } else if (m->var.type == XX_VAR_TYPE_WSTRING || m->var.type == XX_VAR_TYPE_WSTRING_VIEW) {
            size_t n = m->var.val.wstr.len;
            wchar_t *s;
            if ((DSL1_OPTION_BYTES - bytes) / sizeof(wchar_t) == 0U ||
                n >= (DSL1_OPTION_BYTES - bytes) / sizeof(wchar_t)) return false;
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
static char *dsl1_owned_path(const xx_var *v)
{
    size_t n;
    if (!v) return NULL;
    if (v->type == XX_VAR_TYPE_STRING || v->type == XX_VAR_TYPE_STRING_VIEW) {
        char *s;
        n = v->val.str.len;
        if (n >= DSL1_OPTION_BYTES || (!v->val.str.ptr && n)) return NULL;
        s = (char *)xx_mem_alloc(n + 1U);
        if (s) { if (n) xx_rt_memcpy(s, v->val.str.ptr, n); s[n] = 0; }
        return s;
    }
    if (v->type == XX_VAR_TYPE_WSTRING || v->type == XX_VAR_TYPE_WSTRING_VIEW) {
        wchar_t *wide;
        char *s;
        n = v->val.wstr.len;
        if (n >= DSL1_OPTION_BYTES / sizeof(wchar_t) || (!v->val.wstr.ptr && n)) return NULL;
        wide = (wchar_t *)xx_mem_alloc((n + 1U) * sizeof(wchar_t));
        if (!wide) return NULL;
        if (n) xx_rt_memcpy(wide, v->val.wstr.ptr, n * sizeof(wchar_t));
        wide[n] = 0; s = xx_str_unicode_to_utf8(wide); xx_mem_free(wide); return s;
    }
    return NULL;
}


static bool dsl1_decode(Abstractformat *f,const dsl1_cursor *cursor,const xx_list_s *options,uint8_t **result,xx_pd_struct *pd)
{
    const dsl1_entry *e=&cursor->entry;uint8_t *plain;*result=NULL;
    if(!dsl1_ready(f,&cursor->info,options,pd)||!dsl1_unchanged(f,&cursor->info,pd))return false;
    plain=(uint8_t *)xx_mem_alloc(e->raw ? e->raw : 1U);if(!plain)return false;
    if(!dsl1_ready(f,&cursor->info,options,pd)||!dsl1_payload_decode(cursor->info.input+e->payload,e->packed,plain,e->raw,e->method,pd)||
       !dsl1_ready(f,&cursor->info,options,pd)||!dsl1_unchanged(f,&cursor->info,pd)){xx_mem_free(plain);return false;}
    *result=plain;return true;
}
static bool dsl1_record(Abstractformat *f,xx_archive_record_state *s,dsl1_cursor *cursor)
{
    xx_archive_record *r=&s->current_record;const dsl1_entry *e=&cursor->entry;
    r->header_offset=f->base_address+(int64_t)e->header;r->header_size=(int64_t)(e->header_end-e->header);
    r->data_offset=f->base_address+(int64_t)e->payload;r->compressed_size=(int64_t)e->packed;
    return xx_archive_record_set_original_name(r,e->name)&&xx_archive_record_set_meta_u64(r,XX_META_ID_COMPRESSED_SIZE,e->packed)&&
       xx_archive_record_set_meta_u64(r,XX_META_ID_UNCOMPRESSED_SIZE,e->raw)&&xx_archive_record_set_meta_str(r,XX_META_ID_COMPRESSION_METHOD,e->method==0U ? "stored" : "PKWARE DCL (complete stream)")&&
       xx_archive_record_set_meta_bool(r,XX_META_ID_IS_ENCRYPTED,false)&&xx_archive_record_set_meta_bool(r,XX_META_ID_IS_FOLDER,false);
}
void xx_dsl1_init(xx_dsl1 *a, xx_io_device *device, int64_t base)
{
    if (!a) return;
    xx_rt_memset(a, 0, sizeof(*a)); xx_format_init(&a->format, device, base);
    a->format.endian = XX_ENDIAN_LITTLE; a->format.file_type = XX_FILE_TYPE_DSL1;
    a->format.format_type = XX_TYPE_ARCHIVE; a->format.is_archive = true;
    xx_format_set_mime_type(&a->format, "application/x-dsl1"); xx_format_set_extension(&a->format, "dsl");
    a->format.check_is_valid = xx_dsl1_check_is_valid;
    a->format.handle_base_info = xx_dsl1_handle_base_info;
    a->format.get_format_size = xx_dsl1_get_format_size;
    a->format.get_number_of_archive_records = xx_dsl1_get_number_of_archive_records;
    a->format.create_archive_records_reading = xx_dsl1_create_archive_records_reading;
    a->format.get_current_archive_record = xx_dsl1_get_current_archive_record;
    a->format.unpack_current_archive_record = xx_dsl1_unpack_current_archive_record;
    a->format.archive_record_move_to_next = xx_dsl1_archive_record_move_to_next;
    a->format.free_archive_records_reading = xx_dsl1_free_archive_records_reading;
    a->archive_end = -1;
}
xx_dsl1 *xx_dsl1_create(xx_io_device *device, int64_t base)
{ xx_dsl1 *a = (xx_dsl1 *)xx_mem_alloc(sizeof(*a)); if (a) xx_dsl1_init(a, device, base); return a; }
void xx_dsl1_destroy(xx_dsl1 *a) { if (a) xx_format_cleanup_extra_parameters(&a->format); }
void xx_dsl1_free(xx_dsl1 *a) { if (a) { xx_dsl1_destroy(a); xx_mem_free(a); } }
bool xx_dsl1_check_is_valid(Abstractformat *f,xx_pd_struct *pd)
{
    dsl1_info *i;bool ok=false;
    if(!f || dsl1_budget(f,NULL,XX_META_ID_OPT_MEMORY_LIMIT)<DSL1_OVERHEAD || (pd && xx_pd_is_stopped(pd)))return false;
    i=(dsl1_info *)xx_mem_alloc(sizeof(*i));
    if(i){ok=dsl1_parse(f,i,NULL,pd);dsl1_info_free(i);xx_mem_free(i);}return ok;
}
bool xx_dsl1_handle_base_info(Abstractformat *f,xx_pd_struct *pd)
{
    dsl1_info *i=NULL;xx_dsl1 *a=(xx_dsl1 *)f;bool ok=false;
    if(f && dsl1_budget(f,NULL,XX_META_ID_OPT_MEMORY_LIMIT)>=DSL1_OVERHEAD && !(pd && xx_pd_is_stopped(pd)))i=(dsl1_info *)xx_mem_alloc(sizeof(*i));
    if(i && dsl1_parse(f,i,NULL,pd)) {a->number_of_records=i->count;a->archive_end=f->base_address+(int64_t)i->input_size;f->format_size=(int64_t)i->input_size;f->number_of_archive_records=i->count;f->overlay_offset=-1;f->overlay_size=0;f->is_valid=true;f->base_info_handled=true;ok=true;}
    if(i){dsl1_info_free(i);xx_mem_free(i);}
    if(!ok && f){f->is_valid=false;f->base_info_handled=false;f->format_size=-1;f->number_of_archive_records=0U;}return ok;
}
int64_t xx_dsl1_get_format_size(Abstractformat *f,xx_pd_struct *pd)
{return f && (f->base_info_handled||xx_dsl1_handle_base_info(f,pd))?f->format_size:-1;}
uint64_t xx_dsl1_get_number_of_archive_records(Abstractformat *f,xx_pd_struct *pd)
{return f && (f->base_info_handled||xx_dsl1_handle_base_info(f,pd))?f->number_of_archive_records:0U;}
static void dsl1_cursor_free(void *p){dsl1_cursor *c=(dsl1_cursor *)p;if(c){dsl1_info_free(&c->info);xx_mem_free(c);}}
xx_archive_record_state *xx_dsl1_create_archive_records_reading(Abstractformat *f,const xx_list_s *options,xx_pd_struct *pd)
{
    dsl1_cursor *cursor;xx_archive_record_state *s;
    if(!f || !dsl1_options_valid(options) || dsl1_budget(f,options,XX_META_ID_OPT_MEMORY_LIMIT)<DSL1_OVERHEAD || (pd && xx_pd_is_stopped(pd)))return NULL;
    cursor=(dsl1_cursor *)xx_mem_alloc(sizeof(*cursor));
    if(!cursor)return NULL;
    xx_rt_memset(cursor,0,sizeof(*cursor));
    if(!dsl1_parse(f,&cursor->info,options,pd)||!dsl1_ready(f,&cursor->info,options,pd)){dsl1_cursor_free(cursor);return NULL;}
    s=(xx_archive_record_state *)xx_mem_alloc(sizeof(*s));if(!s){dsl1_cursor_free(cursor);return NULL;}
    cursor->entry=cursor->info.entries[0];xx_archive_record_state_init(s,f);s->internal_state=cursor;s->free_internal=dsl1_cursor_free;s->total_records=(int64_t)cursor->info.count;
    if(!dsl1_copy_options(&s->options,options)||!dsl1_record(f,s,cursor)){xx_archive_record_state_free(s);return NULL;}s->current_index=0;s->has_record=true;return s;
}
const xx_archive_record *xx_dsl1_get_current_archive_record(Abstractformat *f,xx_archive_record_state *s)
{return f&&s&&s->format==f&&s->has_record?&s->current_record:NULL;}
bool xx_dsl1_archive_record_move_to_next(Abstractformat *f,xx_archive_record_state *s,xx_pd_struct *pd)
{
    dsl1_cursor *c;if(!f||!s||s->format!=f||!s->has_record||!(c=(dsl1_cursor *)s->internal_state)||!dsl1_ready(f,&c->info,&s->options,pd))return false;
    ++s->current_index;s->has_record=false;xx_archive_record_cleanup(&s->current_record);xx_archive_record_init(&s->current_record);
    if(s->current_index>=(int64_t)c->info.count)return false;
    c->entry=c->info.entries[(size_t)s->current_index];
    if(!dsl1_record(f,s,c))return false;
    s->has_record=true;return true;
}
bool xx_dsl1_unpack_current_archive_record(Abstractformat *f, xx_archive_record_state *s, xx_pd_struct *pd)
{
    const dsl1_cursor *cursor; const xx_var *option, *overwrite;
    uint8_t *plain = NULL; char *base = NULL, *path = NULL; xx_io_device *out = NULL;
    size_t done = 0U; bool ok = false, created = false;
    if (!f || !s || s->format != f || !s->has_record || !(cursor = (const dsl1_cursor *)s->internal_state) || !dsl1_decode(f, cursor, &s->options, &plain, pd)) return false;
    option = dsl1_option(&s->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!option) { xx_mem_free(plain); return true; }
    base = dsl1_owned_path(option);
    if (!base || !dsl1_ready(f, &cursor->info, &s->options, pd)) goto done;
    path = base[0] && base[xx_str_len(base) - 1U] != '/' && base[xx_str_len(base) - 1U] != '\\' ? xx_str_concat3(base, "/", cursor->entry.name) : xx_str_concat(base, cursor->entry.name);
    overwrite = dsl1_option(&s->options, XX_META_ID_OPT_OVERWRITE);
    if (!overwrite) overwrite = xx_format_resolve_extra_parameter(f, NULL, XX_META_ID_OPT_OVERWRITE);
    if (!path || (xx_io_file_exists_a(path) && (!overwrite || !xx_var_get_bool(overwrite))) || !dsl1_ready(f, &cursor->info, &s->options, pd) ||
        !xx_store_create_dirs_a(path, false) || !dsl1_ready(f, &cursor->info, &s->options, pd)) goto done;
    out = xx_io_file_open(path, "wb"); created = out != NULL; ok = created;
    while (ok && done < cursor->entry.raw) {
        size_t step = cursor->entry.raw - done; ssize_t sent;
        if (!dsl1_ready(f, &cursor->info, &s->options, pd)) { ok = false; break; }
        if (step > 65536U) step = 65536U;
        sent = xx_io_write(out, plain + done, step);
        if (sent <= 0 || (size_t)sent > step) ok = false; else done += (size_t)sent;
    }
    if (!dsl1_ready(f, &cursor->info, &s->options, pd)) ok = false;
    if (out && xx_io_close(out) != 0) ok = false;
    out = NULL;
done:
    if (out) xx_io_close(out);
    if (!ok && created) xx_rt_remove(path);
    xx_mem_free(plain); xx_str_free(base); xx_str_free(path); return ok;
}
void xx_dsl1_free_archive_records_reading(Abstractformat *f, xx_archive_record_state *s)
{ (void)f; xx_archive_record_state_free(s); }
