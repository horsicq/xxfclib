/* Copyright (c) 2026 hors<horsicq@gmail.com> */

/* SPDX-License-Identifier: MIT. Private native 7z resource collection. */
#include <wchar.h>
typedef struct sfx7_bundle { size_t count; xx_7zip *readers[128]; } sfx7_bundle;
typedef struct sfx7_cursor { sfx7_bundle *bundle; size_t index; xx_archive_record_state *nested; } sfx7_cursor;
static void sfx7_bundle_free(sfx7_bundle *b)
{
    if(b) { for(size_t i=0;i<b->count;i++) xx_7zip_free(b->readers[i]); xx_mem_free(b); }
}
static sfx7_bundle *sfx7_nested(Abstractformat *f,xx_pd_struct *pd)
{
    if(!f || carrier_stop(pd)) return NULL;
    xx_sfx_7zip *outer=(xx_sfx_7zip *)f;
    if(!outer->nested_7zip && !xx_sfx_7zip_handle_base_info(f,pd)) return NULL;
    return (sfx7_bundle *)outer->nested_7zip;
}
static int64_t sfx7_size(Abstractformat *f,xx_pd_struct *pd) { return sfx7_nested(f,pd)?f->format_size:-1; }
static uint64_t sfx7_count(Abstractformat *f,xx_pd_struct *pd) { return sfx7_nested(f,pd)?f->number_of_archive_records:0; }
static bool sfx7_options(xx_list_s *dst,const xx_list_s *src,size_t group)
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
static bool sfx7_prefix(sfx7_cursor *c)
{
    if(!c->nested || !c->nested->has_record) return false;
    if(c->bundle->count==1) return true;
    wchar_t prefix[32]; swprintf(prefix,32,L"%u/",(unsigned)c->index+1);
    const wchar_t *name=xx_archive_record_get_original_name_w(&c->nested->current_record);
    wchar_t *joined=name?xx_str_wconcat(prefix,name):NULL;
    bool ok=joined && xx_archive_record_set_original_name_w(&c->nested->current_record,joined); xx_str_wfree(joined); return ok;
}
static void sfx7_cursor_free(void *p)
{
    sfx7_cursor *c=(sfx7_cursor *)p;
    if(c) { if(c->nested) xx_7zip_free_archive_records_reading(NULL,c->nested); xx_mem_free(c); }
}
static xx_archive_record_state *sfx7_records(Abstractformat *f,const xx_list_s *opts,xx_pd_struct *pd)
{
    sfx7_bundle *b=sfx7_nested(f,pd); if(!b || !b->count) return NULL;
    xx_archive_record_state *s=(xx_archive_record_state *)xx_mem_alloc(sizeof(*s)); if(!s) return NULL;
    xx_archive_record_state_init(s,f);
    sfx7_cursor *c=(sfx7_cursor *)xx_mem_calloc(1,sizeof(*c));
    if(!c) { xx_archive_record_state_free(s); return NULL; }
    s->internal_state=c; s->free_internal=sfx7_cursor_free; c->bundle=b;
    if(!sfx7_options(&s->options,opts,0)) { xx_archive_record_state_free(s); return NULL; }
    c->nested=xx_7zip_create_archive_records_reading(&b->readers[0]->format,&s->options,pd);
    if(!sfx7_prefix(c)) { xx_archive_record_state_free(s); return NULL; }
    s->has_record=true; s->current_index=0; s->total_records=(int64_t)f->number_of_archive_records; return s;
}
static const xx_archive_record *sfx7_current(Abstractformat *f,xx_archive_record_state *s)
{
    if(!f || !s || s->format!=f || !s->has_record) return NULL;
    sfx7_cursor *c=(sfx7_cursor *)s->internal_state;
    return c && c->nested?xx_7zip_get_current_archive_record(&c->bundle->readers[c->index]->format,c->nested):NULL;
}
static bool sfx7_next(Abstractformat *f,xx_archive_record_state *s,xx_pd_struct *pd)
{
    if(!f || !s || s->format!=f || !s->has_record || carrier_stop(pd)) return false;
    sfx7_cursor *c=(sfx7_cursor *)s->internal_state; if(!c || !c->nested) return false;
    if(xx_7zip_archive_record_move_to_next(&c->bundle->readers[c->index]->format,c->nested,pd)) {
        if(!sfx7_prefix(c)) { s->has_record=false; return false; }
        ++s->current_index; return true;
    }
    if(carrier_stop(pd) || c->nested->current_index+1<c->nested->total_records) { s->has_record=false; return false; }
    xx_7zip_free_archive_records_reading(NULL,c->nested); c->nested=NULL;
    if(++c->index>=c->bundle->count) { s->has_record=false; return false; }
    c->nested=xx_7zip_create_archive_records_reading(&c->bundle->readers[c->index]->format,&s->options,pd);
    if(!sfx7_prefix(c)) { s->has_record=false; return false; }
    ++s->current_index; return true;
}
static bool sfx7_unpack(Abstractformat *f,xx_archive_record_state *s,xx_pd_struct *pd)
{
    if(!f || !s || s->format!=f || !s->has_record || carrier_stop(pd)) return false;
    sfx7_cursor *c=(sfx7_cursor *)s->internal_state; if(!c || !c->nested) return false;
    if(!sfx7_options(&c->nested->options,&s->options,c->bundle->count>1?c->index+1:0)) return false;
    return xx_7zip_unpack_current_archive_record(&c->bundle->readers[c->index]->format,c->nested,pd);
}
static void sfx7_free_records(Abstractformat *f,xx_archive_record_state *s) { (void)f; xx_archive_record_state_free(s); }
