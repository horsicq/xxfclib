/* Copyright (c) 2026 hors<horsicq@gmail.com> */

/* SPDX-License-Identifier: MIT. Private native ARJ resource collection; each reading cursor owns its children. */
#include <wchar.h>
typedef struct arjb_bundle { size_t count; xx_arj *readers[128]; uint64_t total; int64_t extent; } arjb_bundle;
typedef struct arjb_cursor { arjb_bundle *bundle; size_t index, local_index; xx_archive_record_state *nested; } arjb_cursor;
static void arjb_bundle_free(arjb_bundle *b)
{
    if(b) { for(size_t i=0;i<b->count;i++) xx_arj_free(b->readers[i]); xx_mem_free(b); }
}
static arjb_bundle *arjb_open(Abstractformat *f,xx_pd_struct *pd)
{
    pm_stream *parts=pm_open(f,pd); if(!parts) return NULL;
    arjb_bundle *b=(arjb_bundle *)xx_mem_calloc(1,sizeof(*b));
    if(!b) { pm_free_stream(parts); return NULL; }
    if(!parts->count || parts->count>128) { pm_free_stream(parts); arjb_bundle_free(b); return NULL; }
    for(size_t i=0;i<parts->count;i++) {
        xx_arj *r=xx_arj_create(f->device,parts->items[i].offset);
        if(!r || carrier_stop(pd) || !xx_arj_handle_base_info(&r->format,pd) || r->format.format_size!=parts->items[i].size ||
           !r->format.number_of_archive_records || r->format.number_of_archive_records>65536 || r->format.number_of_archive_records>65536U-b->total) {
            if(r) xx_arj_free(r);
            pm_free_stream(parts); arjb_bundle_free(b); return NULL;
        }
        b->readers[b->count++]=r; b->total+=r->format.number_of_archive_records;
    }
    b->extent=parts->size; pm_free_stream(parts); return b;
}
static int64_t arjb_size(Abstractformat *f,xx_pd_struct *pd) { return f && !carrier_stop(pd) && (f->base_info_handled || xx_sfx_arj_handle_base_info(f,pd))?f->format_size:-1; }
static uint64_t arjb_count(Abstractformat *f,xx_pd_struct *pd) { return arjb_size(f,pd)>=0?f->number_of_archive_records:0; }
static bool arjb_options(xx_list_s *dst,const xx_list_s *src,size_t group)
{
    xx_list_clear(dst);
    for(size_t i=0;src && i<src->count;i++) {
        const xx_meta *m=(const xx_meta *)xx_list_at(src,i); xx_meta copy;
        if(!m) continue;
        xx_meta_init(&copy,m->meta_id);
        if(!xx_var_copy(&copy.var,&m->var)) { xx_meta_cleanup(&copy); return false; }
        if(group && m->meta_id==XX_META_ID_OPT_UNPACK_PATH) {
            wchar_t prefix[32], *base=NULL, *joined;
            if(m->var.type==XX_VAR_TYPE_STRING || m->var.type==XX_VAR_TYPE_STRING_VIEW) base=xx_str_utf8_to_unicode(xx_var_get_str(&m->var));
            else if(m->var.type==XX_VAR_TYPE_WSTRING || m->var.type==XX_VAR_TYPE_WSTRING_VIEW) base=xx_str_wdup(xx_var_get_wstr(&m->var));
            if(base) {
                swprintf(prefix,32,L"/%u",(unsigned)group); joined=xx_str_wconcat(base,prefix); xx_str_wfree(base);
                bool ok=joined && xx_var_set_wstr(&copy.var,joined); xx_str_wfree(joined);
                if(!ok) { xx_meta_cleanup(&copy); return false; }
            } else { xx_meta_cleanup(&copy); return false; }
        }
        if(!xx_list_append(dst,&copy)) { xx_meta_cleanup(&copy); return false; }
    }
    return true;
}
static bool arjb_prefix(arjb_cursor *c)
{
    if(!c->nested || !c->nested->has_record) return false;
    if(c->bundle->count==1) return true;
    char prefix[32]; xx_rt_snprintf(prefix,sizeof(prefix),"%u/",(unsigned)c->index+1);
    const char *name=xx_archive_record_get_original_name(&c->nested->current_record);
    char *joined=name?xx_str_concat(prefix,name):NULL;
    bool ok=joined && xx_archive_record_set_original_name(&c->nested->current_record,joined); xx_str_free(joined); return ok;
}
static void arjb_cursor_free(void *p)
{
    arjb_cursor *c=(arjb_cursor *)p;
    if(c) { if(c->nested) xx_arj_free_archive_records_reading(NULL,c->nested); arjb_bundle_free(c->bundle); xx_mem_free(c); }
}
static xx_archive_record_state *arjb_records(Abstractformat *f,const xx_list_s *opts,xx_pd_struct *pd)
{
    arjb_bundle *b=arjb_open(f,pd); if(!b) return NULL;
    xx_archive_record_state *s=(xx_archive_record_state *)xx_mem_alloc(sizeof(*s)); if(!s) { arjb_bundle_free(b); return NULL; }
    xx_archive_record_state_init(s,f);
    arjb_cursor *c=(arjb_cursor *)xx_mem_calloc(1,sizeof(*c));
    if(!c) { arjb_bundle_free(b); xx_archive_record_state_free(s); return NULL; }
    s->internal_state=c; s->free_internal=arjb_cursor_free; c->bundle=b;
    if(!arjb_options(&s->options,opts,0)) { xx_archive_record_state_free(s); return NULL; }
    c->nested=xx_arj_create_archive_records_reading(&b->readers[0]->format,&s->options,pd);
    if(!arjb_prefix(c)) { xx_archive_record_state_free(s); return NULL; }
    s->has_record=true; s->current_index=0; s->total_records=(int64_t)b->total; return s;
}
static const xx_archive_record *arjb_current(Abstractformat *f,xx_archive_record_state *s)
{
    if(!f || !s || s->format!=f || !s->has_record) return NULL;
    arjb_cursor *c=(arjb_cursor *)s->internal_state;
    return c && c->nested?xx_arj_get_current_archive_record(&c->bundle->readers[c->index]->format,c->nested):NULL;
}
static bool arjb_next(Abstractformat *f,xx_archive_record_state *s,xx_pd_struct *pd)
{
    if(!f || !s || s->format!=f || !s->has_record || carrier_stop(pd)) return false;
    arjb_cursor *c=(arjb_cursor *)s->internal_state; if(!c || !c->nested) return false;
    if(c->local_index+1<(uint64_t)c->nested->total_records) {
        if(!xx_arj_archive_record_move_to_next(&c->bundle->readers[c->index]->format,c->nested,pd) || !arjb_prefix(c)) {
            s->has_record=false; return false;
        }
        ++c->local_index; ++s->current_index; return true;
    }
    xx_arj_free_archive_records_reading(NULL,c->nested); c->nested=NULL;
    if(++c->index>=c->bundle->count) { s->has_record=false; return false; }
    c->local_index=0;
    c->nested=xx_arj_create_archive_records_reading(&c->bundle->readers[c->index]->format,&s->options,pd);
    if(!arjb_prefix(c)) { s->has_record=false; return false; }
    ++s->current_index; return true;
}
static bool arjb_unpack(Abstractformat *f,xx_archive_record_state *s,xx_pd_struct *pd)
{
    if(!f || !s || s->format!=f || !s->has_record || carrier_stop(pd)) return false;
    arjb_cursor *c=(arjb_cursor *)s->internal_state; if(!c || !c->nested) return false;
    if(!arjb_options(&c->nested->options,&s->options,c->bundle->count>1?c->index+1:0)) return false;
    return xx_arj_unpack_current_archive_record(&c->bundle->readers[c->index]->format,c->nested,pd);
}
static void arjb_free_records(Abstractformat *f,xx_archive_record_state *s) { (void)f; xx_archive_record_state_free(s); }
